#ifndef MYLORA_H
#define MYLORA_H

#include <Arduino.h>
#include "Config.h"

// ------------------------------------------------------------
// Parámetros LoRa
// ------------------------------------------------------------
#define LORA_FREQUENCY     868E6      // 868 MHz (EU)
#define LORA_POWER         17         // dBm
#define LORA_SPREADING     7          // SF7
#define LORA_BANDWIDTH     125E3      // 125 kHz

// ============================================================
//  Variables globales accesibles
// ============================================================
extern bool loraReady;
extern String lastReceived;     

// ============================================================
//  Funciones Lora
//
//  La radio ESCUCHA en recepción continua en todos los estados (menús, juegos, esperas bloqueantes) y
//  tras cada emisión vuelve a escuchar. Solo se apaga (Lora_sleep) si se activa el ahorro opcional.
// ============================================================

void Lora_begin();

// Vigilancia: comprueba cada 500 ms que el chip sigue escuchando y lo re-arma si cayó.
// Llamar a menudo (la hace backgroundTick()).
void Lora_update();

// Política de la radio
void Lora_listen();            // recepción continua (estado normal; también al despertar de la suspensión)
void Lora_sleep();             // radio dormida: no oye ni emite (suspensión prolongada, ahorro de energía)
bool Lora_isListening();
bool Lora_isAsleep();
// Ahorro de energía opcional en suspensión: si en este tiempo (ms) no se oye nada, la radio se duerme del todo
// (no oye ni emite hasta despertar el equipo). 0 = nunca. Valor inicial: LORA_DEEP_SLEEP (Config.h).
void     Lora_setDeepSleepMs(uint32_t ms);
uint32_t Lora_deepSleepMs();
// true si el modem esta recibiendo una trama ahora mismo (el canal esta ocupado): emitir ahora la
// destruiria. Las emisiones que pueden esperar (balizas, reintentos) lo comprueban; Lora_send() ya
// escucha antes de hablar por su cuenta.
bool Lora_busy();
void Lora_setRangeMode(bool on);   // HippoRadar: SF10 y +20 dBm (largo alcance) / vuelta al chat
// true mientras dura el modo de largo alcance (SF10): la radio solo oye y emite en SF10, asi que NO oye el
// chat (SF7) ni lo emite; mientras tanto no se sacan balizas, ACK ni reintentos del chat.
bool Lora_rangeMode();

// Emite un mensaje (cifrado). ESCUCHA ANTES DE HABLAR: si entra una trama espera a que acabe (ver
// CSMA_* en Config.h) y no pisa una trama ya recibida y aun sin leer (la guarda en una cola).
// La emision es ASINCRONA: vuelve en cuanto el chip empieza a emitir (la trama tarda 120-700 ms en el
// aire) para que el equipo siga atendiendo botones y animaciones; mientras emite no oye, y al acabar
// vuelve solo a escuchar (se atiende en Lora_update/Lora_hasMessage/Lora_send). Una nueva emision espera
// a que acabe la anterior.
bool Lora_send(const String &msg);

// Hay un mensaje recibido y descifrado listo para Lora_readMessage().
bool Lora_hasMessage();

bool Lora_isReady();

String Lora_readMessage();

int Lora_lastRssi();    // RSSI (dBm) del ultimo paquete recibido (para el radar)

#endif
