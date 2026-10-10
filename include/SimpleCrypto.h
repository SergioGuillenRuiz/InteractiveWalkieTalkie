#ifndef SIMPLECRYPTO_H
#define SIMPLECRYPTO_H

#include <Arduino.h>

// Cifra un texto (hasta 223 bytes; puede llevar bytes 0x00) -> BINARIO: IV(16) | bloques AES-128-CBC.
// Devuelve "" si esta vacio o es demasiado largo.
String SimpleCrypto_encrypt(const String& text);

// Descifra un paquete binario (IV + bloques) -> texto. Devuelve "" si el formato no es valido.
String SimpleCrypto_decrypt(const String& data);

#endif
