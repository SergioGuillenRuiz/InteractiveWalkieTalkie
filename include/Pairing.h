#ifndef PAIRING_H
#define PAIRING_H

#include <Arduino.h>

// ============================================================
//  Emparejamiento: clave PROPIA de cada pareja de equipos.
//
//  Los dos equipos se emparejan una vez (Ajustes -> Emparejar): intercambian claves publicas X25519 por radio
//  (con compromiso previo, para que nadie pueda ajustar su clave a lo que ve) y cada pantalla muestra un codigo de
//  6 digitos derivado de la clave compartida. Si el codigo es el MISMO en los dos equipos (lo comprueba el usuario),
//  no hay nadie en medio: se guardan en la EEPROM una clave de cifrado y otra de autenticacion que solo conocen esos
//  dos equipos. Todo el trafico posterior va cifrado (AES-128-CBC), autenticado (AES-CMAC) y con contador
//  anti-repeticion (ver SimpleCrypto.h). Un equipo sin emparejar no emite ni acepta nada de chat.
// ============================================================

void Pairing_load();                                   // en setup(): lee la clave de la EEPROM
bool Pairing_isPaired();
void Pairing_clear();                                  // olvida la clave (sin emparejar)
bool Pairing_keys(uint8_t enc[16], uint8_t mac[16]);   // false si esta sin emparejar

uint32_t Pairing_nextTxSeq();                          // contador de la proxima emision (nunca se repite, ni tras reiniciar)
bool     Pairing_acceptRxSeq(uint8_t senderId, uint32_t seq);   // anti-repeticion: true si es posterior al ultimo de ESE emisor (y lo anota)

void Pairing_randomBytes(uint8_t *out, size_t n);      // n <= 32: mezcla RNG por hardware, ruido de la radio, ADC y tiempos

// Derivacion de claves (funciones puras; el simulador las usa para hacer de "otro equipo" en las pruebas).
struct PairSecrets { uint8_t enc[16], mac[16]; uint32_t sas; uint8_t role; };   // role: 1 si mi clave publica es la "menor", 2 si la "mayor"
bool Pairing_derive(const uint8_t myPriv[32], const uint8_t myPub[32], const uint8_t peerPub[32], PairSecrets &out);   // false: secreto no valido
void Pairing_confirmTag(const uint8_t mac[16], const uint8_t myPub[32], const uint8_t peerPub[32], uint8_t who, uint8_t out[16]);   // who: rol de QUIEN confirma

void Pairing_onFrame(const String &raw);               // trama de emparejamiento recibida (la entrega MyLora)
void startPairing();                                   // pantalla de emparejar (bloqueante); vuelve al menu principal

#endif
