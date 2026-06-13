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
// ============================================================

void Lora_begin();

void Lora_update();

bool Lora_send(const String &msg);

bool Lora_hasMessage();

bool Lora_isReady();

String Lora_readMessage();

int Lora_lastRssi();    // RSSI (dBm) del ultimo paquete recibido (para el radar)

#endif
