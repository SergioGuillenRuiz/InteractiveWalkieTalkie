#ifndef CRYPTO_H
#define CRYPTO_H

#include <Arduino.h>

// ============================================================
//  Primitivas criptograficas propias del proyecto (sin dependencias extra):
//   - SHA-256 (FIPS 180-4)
//   - X25519 (RFC 7748): intercambio de claves de curva eliptica, version de TweetNaCl (dominio publico)
//   - AES-CMAC (RFC 4493, NIST SP 800-38B) sobre el AES-128 de tiny-AES: etiqueta de autenticacion
// Se validan en el simulador contra los vectores oficiales (scripts/crypto.sim, "crypto selftest").
// ============================================================

void Crypto_sha256(const uint8_t *data, size_t len, uint8_t out[32]);

// Multiplicacion de X25519: out = scalar * point (point de 32 bytes). Tarda ~1 s en el ESP8266 (llama a yield()).
void Crypto_x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]);
// Clave publica: out = scalar * 9
void Crypto_x25519Base(uint8_t out[32], const uint8_t scalar[32]);

// AES-CMAC con clave de 16 bytes -> etiqueta de 16 bytes
void Crypto_aesCmac(const uint8_t key[16], const uint8_t *msg, size_t len, uint8_t out[16]);

// Comparacion en tiempo constante
bool Crypto_equal(const uint8_t *a, const uint8_t *b, size_t n);

// Autotest con los vectores oficiales: true si todo coincide
bool Crypto_selfTest();

#endif
