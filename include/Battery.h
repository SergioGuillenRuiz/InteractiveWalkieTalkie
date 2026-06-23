#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>

// ============================================================
//  Estado de la batería (LiPo 3.2V).
//
//  AVISO DE HARDWARE: el ESP8266 tiene UN solo ADC y A0 ya lo usa el
//  potenciómetro. Medir la batería en la placa real requiere hardware dedicado
//  (divisor en otro ADC / multiplexor) o ESP.getVcc() (sacrificando el pote).
//  Por defecto, en hardware la lectura devuelve "lleno" (sin falsas alarmas)
//  hasta que se cablee la fuente real; ver batteryReadRaw() en Battery.cpp.
//  En el simulador la batería es un canal mockeado (comando .sim "battery").
// ============================================================

uint8_t Battery_percent();   // 0..100
bool    Battery_isLow();     // aviso con histéresis (BATT_LOW_PCT / BATT_OK_PCT)

#endif
