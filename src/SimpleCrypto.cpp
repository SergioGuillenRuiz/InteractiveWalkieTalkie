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

// IV fijo (mismo en ambos dispositivos)
static uint8_t IV[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
};

// Byte a hexadecimal
static char toHex(uint8_t nibble) {
    nibble &= 0x0F;
    return nibble < 10 ? '0' + nibble : 'A' + (nibble - 10);
}

// Bytes a String hexadecimal
static String toHexString(const uint8_t* data, uint16_t len) {
    String result;
    for (uint16_t i = 0; i < len; i++) {
        result += toHex(data[i] >> 4);
        result += toHex(data[i]);
    }
    return result;
}

// Hexadecimal a bytes
static bool fromHexString(const String& hex, uint8_t* out, uint16_t maxLen) {
    if (hex.length() % 2 != 0) return false;
    
    uint16_t byteCount = hex.length() / 2;
    if (byteCount > maxLen) return false;
    
    for (uint16_t i = 0; i < byteCount; i++) {
        char high = hex[i * 2];
        char low = hex[i * 2 + 1];
        
        out[i] = (high <= '9' ? high - '0' : high - 'A' + 10) << 4 |
                 (low <= '9' ? low - '0' : low - 'A' + 10);
    }
    
    return true;
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
// ============================================================

String SimpleCrypto_encrypt(const String& text) {
    if (text.length() == 0) return "";
    
    uint8_t buffer[128];
    uint16_t len = text.length();
    
    if (len > 100) return "";
    
    memcpy(buffer, text.c_str(), len);
    
    uint16_t paddedLen = addPad(buffer, len, 128);
    if (paddedLen == 0) return "";
    
    // Usar TinyAES correctamente
    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, KEY, IV);
    AES_CBC_encrypt_buffer(&ctx, buffer, paddedLen);
    
    return toHexString(buffer, paddedLen);
}

String SimpleCrypto_decrypt(const String& hex) {
    if (hex.length() == 0 || hex.length() % 32 != 0) return "";
    
    uint8_t buffer[128];
    uint16_t byteLen = hex.length() / 2;
    
    if (!fromHexString(hex, buffer, 128)) return "";
    
    // Usar TinyAES correctamente
    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, KEY, IV);
    AES_CBC_decrypt_buffer(&ctx, buffer, byteLen);
    
    if (!removePad(buffer, &byteLen)) return "";
    
    String result;
    for (uint16_t i = 0; i < byteLen; i++) {
        result += (char)buffer[i];
    }
    
    return result;
}