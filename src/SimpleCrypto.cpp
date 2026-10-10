#include <Arduino.h>
#include "SimpleCrypto.h"

extern "C" {
#include "aes.h"
}

// Clave AES compartida - ¡CAMBIAR ESTO! (16 bytes)
static const uint8_t KEY[16] = {
    0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
    0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C
};

// Genera un IV aleatorio de 16 bytes. El IV viaja en claro al principio de cada
// mensaje (es lo estándar en CBC; no es secreto). Así, dos mensajes con el mismo
// texto producen cifrados distintos.
static void fillRandomIV(uint8_t *iv) {
#if defined(ESP8266)
    for (int i = 0; i < 16; i += 4) {
        uint32_t r = RANDOM_REG32;          // RNG por hardware del ESP8266
        iv[i + 0] = (uint8_t)(r);
        iv[i + 1] = (uint8_t)(r >> 8);
        iv[i + 2] = (uint8_t)(r >> 16);
        iv[i + 3] = (uint8_t)(r >> 24);
    }
#elif defined(ESP32)
    for (int i = 0; i < 16; i += 4) {
        uint32_t r = esp_random();
        iv[i + 0] = (uint8_t)(r);
        iv[i + 1] = (uint8_t)(r >> 8);
        iv[i + 2] = (uint8_t)(r >> 16);
        iv[i + 3] = (uint8_t)(r >> 24);
    }
#else
    for (int i = 0; i < 16; i++) iv[i] = (uint8_t)random(256);
#endif
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
// ÚNICAS 2 FUNCIONES PÚBLICAS
//
// Formato en el aire (BINARIO): IV(16) | bloques cifrados AES-128-CBC (N x 16). Antes viajaba en hexadecimal
// (el doble de bytes y de tiempo en el aire: 32 B en vez de 64 B para un mensaje corto = 72 ms en vez de
// 118 ms a SF7). Un paquete LoRa admite 255 B: 16 + 16*N <= 255 => texto plano de hasta 223 bytes.
// ============================================================

#define CRYPTO_MAX_PLAIN   223
#define CRYPTO_MAX_BUF     224      // 14 bloques de 16: el relleno de un texto de <= 223 bytes no pasa de 224 (16 + 224 = 240 <= 255)

String SimpleCrypto_encrypt(const String& text) {
    if (text.length() == 0) return "";

    uint16_t len = text.length();
    if (len > CRYPTO_MAX_PLAIN) return "";

    uint8_t iv[16];
    fillRandomIV(iv);

    uint8_t buffer[CRYPTO_MAX_BUF];
    memcpy(buffer, text.c_str(), len);

    uint16_t paddedLen = addPad(buffer, len, CRYPTO_MAX_BUF);
    if (paddedLen == 0) return "";

    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, KEY, iv);
    AES_CBC_encrypt_buffer(&ctx, buffer, paddedLen);

    // El IV (en claro) precede al texto cifrado
    String out;
    for (uint16_t i = 0; i < 16; i++) out += (char)iv[i];
    for (uint16_t i = 0; i < paddedLen; i++) out += (char)buffer[i];
    return out;
}

String SimpleCrypto_decrypt(const String& data) {
    // Formato: IV(16) + N bloques cifrados de 16 bytes (al menos uno)
    if (data.length() < 32 || data.length() % 16 != 0 || data.length() > 16 + CRYPTO_MAX_BUF) return "";

    uint8_t iv[16];
    for (int i = 0; i < 16; i++) iv[i] = (uint8_t)data[i];

    uint16_t byteLen = data.length() - 16;
    uint8_t buffer[CRYPTO_MAX_BUF];
    for (uint16_t i = 0; i < byteLen; i++) buffer[i] = (uint8_t)data[16 + i];

    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, KEY, iv);
    AES_CBC_decrypt_buffer(&ctx, buffer, byteLen);

    if (!removePad(buffer, &byteLen)) return "";

    String result;
    for (uint16_t i = 0; i < byteLen; i++) {
        result += (char)buffer[i];
    }

    return result;
}
