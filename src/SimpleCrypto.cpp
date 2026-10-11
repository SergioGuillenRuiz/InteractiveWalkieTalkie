#include <Arduino.h>
#include "SimpleCrypto.h"
#include "Crypto.h"
#include "Pairing.h"
#include "Identity.h"

extern "C" {
#include "aes.h"
}


// Añadir padding PKCS7
static uint16_t addPad(uint8_t* data, uint16_t len, uint16_t maxLen) {
    uint8_t pad = 16 - (len % 16);
    
    if (len + pad > maxLen) return 0;
    
    for (uint8_t i = 0; i < pad; i++) {
        data[len + i] = pad;
    }
    
    return len + pad;
}

// Quitar padding PKCS7
static bool removePad(uint8_t* data, uint16_t* len) {
    if (*len == 0 || *len % 16 != 0) return false;
    
    uint8_t pad = data[*len - 1];
    if (pad == 0 || pad > 16) return false;
    
    for (uint8_t i = 1; i <= pad; i++) {
        if (data[*len - i] != pad) return false;
    }
    
    *len -= pad;
    return true;
}

// ============================================================
// Formato en el aire (BINARIO), con la clave de la pareja (ver Pairing.h):
//
//   nonce(8) | bloques AES-128-CBC (N x 16) | etiqueta(8)
//
//   nonce    = id del emisor(1) | numero de secuencia(4) | 3 bytes aleatorios. El numero de secuencia crece en cada emision
//              y es unico por emisor, asi que ningun nonce se repite aunque el generador aleatorio fallara. El receptor
//              solo acepta numeros posteriores al ultimo visto (anti-repeticion).
//   IV       = AES-ECB(clave de cifrado, nonce | 8 ceros): distinto en cada paquete e impredecible.
//   etiqueta = AES-CMAC(clave de autenticacion, nonce | cifrado) truncada a 8 bytes. Se comprueba ANTES de descifrar:
//              un paquete alterado, de otra pareja o inventado se rechaza.
//
// La longitud es la de siempre (16 + 16*N): el IV de 16 bytes se sustituye por nonce + etiqueta de 8 + 8. Un paquete LoRa
// admite 255 B => texto plano de hasta 223 bytes.
// ============================================================

#define CRYPTO_MAX_PLAIN   223
#define CRYPTO_MAX_BUF     224      // 14 bloques de 16: el relleno de un texto de <= 223 bytes no pasa de 224 (8 + 224 + 8 = 240 <= 255)

static void deriveIv(const uint8_t enc[16], const uint8_t nonce[8], uint8_t iv[16]) {
    memcpy(iv, nonce, 8);
    memset(iv + 8, 0, 8);
    struct AES_ctx ctx;
    AES_init_ctx(&ctx, enc);
    AES_ECB_encrypt(&ctx, iv);
}

static void makeTag(const uint8_t mac[16], const uint8_t *nonceAndCipher, uint16_t len, uint8_t tag[8]) {
    uint8_t full[16];
    Crypto_aesCmac(mac, nonceAndCipher, len, full);
    memcpy(tag, full, 8);
}

String SimpleCrypto_encryptWithKeys(const String& text, const uint8_t enc[16], const uint8_t mac[16], uint8_t senderId, uint32_t seq) {
    if (text.length() == 0) return "";
    uint16_t len = text.length();
    if (len > CRYPTO_MAX_PLAIN) return "";

    uint8_t nonce[8];
    nonce[0] = senderId;
    nonce[1] = seq >> 24; nonce[2] = seq >> 16; nonce[3] = seq >> 8; nonce[4] = seq;
    nonce[5] = (uint8_t)random(256); nonce[6] = (uint8_t)random(256); nonce[7] = (uint8_t)random(256);
    uint8_t iv[16];
    deriveIv(enc, nonce, iv);

    uint8_t buffer[8 + CRYPTO_MAX_BUF + 8];           // nonce | cifrado | etiqueta
    memcpy(buffer, nonce, 8);
    uint8_t *ct = buffer + 8;
    memcpy(ct, text.c_str(), len);
    uint16_t paddedLen = addPad(ct, len, CRYPTO_MAX_BUF);
    if (paddedLen == 0) return "";

    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, enc, iv);
    AES_CBC_encrypt_buffer(&ctx, ct, paddedLen);

    makeTag(mac, buffer, 8 + paddedLen, buffer + 8 + paddedLen);

    String out;
    for (uint16_t i = 0; i < 8 + paddedLen + 8; i++) out += (char)buffer[i];
    return out;
}

String SimpleCrypto_encryptAs(const String& text, uint8_t senderId, uint32_t seq) {
    uint8_t enc[16], mac[16];
    if (!Pairing_keys(enc, mac)) return "";           // sin pareja no hay clave
    return SimpleCrypto_encryptWithKeys(text, enc, mac, senderId, seq);
}

String SimpleCrypto_encrypt(const String& text) {
    if (!Pairing_isPaired()) return "";
    return SimpleCrypto_encryptAs(text, Device_id(), Pairing_nextTxSeq());
}

// Comprueba la etiqueta y descifra. Si 'seqOut' no es nulo devuelve el numero de secuencia del paquete.
static String openPacket(const String& data, uint32_t* seqOut, uint8_t* idOut) {
    // Formato: nonce(8) + N bloques de 16 (al menos uno) + etiqueta(8)
    if (data.length() < 32 || data.length() % 16 != 0 || data.length() > 16 + CRYPTO_MAX_BUF) return "";

    uint8_t enc[16], mac[16];
    if (!Pairing_keys(enc, mac)) return "";

    uint16_t total = data.length();
    uint16_t cipherLen = total - 16;
    uint8_t buffer[8 + CRYPTO_MAX_BUF + 8];
    for (uint16_t i = 0; i < total; i++) buffer[i] = (uint8_t)data[i];

    uint8_t tag[8];
    makeTag(mac, buffer, 8 + cipherLen, tag);
    if (!Crypto_equal(tag, buffer + 8 + cipherLen, 8)) return "";      // alterado, de otra pareja o inventado

    if (idOut) *idOut = buffer[0];
    if (seqOut) *seqOut = ((uint32_t)buffer[1] << 24) | ((uint32_t)buffer[2] << 16) | ((uint32_t)buffer[3] << 8) | buffer[4];

    uint8_t iv[16];
    deriveIv(enc, buffer, iv);
    uint8_t* ct = buffer + 8;
    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, enc, iv);
    AES_CBC_decrypt_buffer(&ctx, ct, cipherLen);

    uint16_t n = cipherLen;
    if (!removePad(ct, &n)) return "";

    String result;
    for (uint16_t i = 0; i < n; i++) result += (char)ct[i];
    return result;
}

String SimpleCrypto_decrypt(const String& data) {
    return openPacket(data, nullptr, nullptr);
}

String SimpleCrypto_decryptRx(const String& data) {
    uint32_t seq = 0; uint8_t id = 0;
    String plain = openPacket(data, &seq, &id);
    if (plain.length() == 0) return "";
    if (!Pairing_acceptRxSeq(id, seq)) return "";          // repeticion de un paquete antiguo
    return plain;
}
