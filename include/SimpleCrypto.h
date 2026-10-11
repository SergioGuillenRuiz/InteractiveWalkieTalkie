#ifndef SIMPLECRYPTO_H
#define SIMPLECRYPTO_H

#include <Arduino.h>

// Cifrado y autenticacion de los paquetes con la clave de la pareja (ver Pairing.h y el formato en SimpleCrypto.cpp):
//   nonce(8) | AES-128-CBC | etiqueta AES-CMAC(8).

// Cifra un texto (hasta 223 bytes; puede llevar bytes 0x00) como ESTE equipo, con el siguiente numero de secuencia.
// Devuelve "" si esta vacio, es demasiado largo o el equipo no esta emparejado.
String SimpleCrypto_encrypt(const String& text);

// Igual, como si lo emitiera otro equipo (id y numero de secuencia dados). Para el simulador (equipos inyectados).
String SimpleCrypto_encryptAs(const String& text, uint8_t senderId, uint32_t seq);

// Igual, con unas claves dadas (el simulador lo usa para fabricar paquetes de OTRA pareja).
String SimpleCrypto_encryptWithKeys(const String& text, const uint8_t enc[16], const uint8_t mac[16], uint8_t senderId, uint32_t seq);

// Comprueba la etiqueta y descifra, SIN anti-repeticion (herramientas y pruebas). "" si no es valido.
String SimpleCrypto_decrypt(const String& data);

// Lo que usa la radio al recibir: comprueba la etiqueta, rechaza numeros de secuencia ya vistos y descifra.
String SimpleCrypto_decryptRx(const String& data);

#endif
