#ifndef SIMPLECRYPTO_H
#define SIMPLECRYPTO_H

#include <Arduino.h>

// Cifrar texto -> hexadecimal
String SimpleCrypto_encrypt(const String& text);

// Descifrar hexadecimal -> texto  
String SimpleCrypto_decrypt(const String& hex);

#endif