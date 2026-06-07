#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================
// Configuración general del proyecto
// ============================================================

// Nombre y versión
#define PROJECT_NAME       "Proyecto Gogoaty"
#define PROJECT_VERSION    "V2.0"

// ------------------------------------------------------------
// Pines del hardware
// ------------------------------------------------------------

// Módulo LoRa SX127x
#define PIN_LORA_NSS       15
#define PIN_LORA_RST       16
#define PIN_LORA_DIO0      5

// Botones físicos
#define PIN_MORSE_BUTTON   4
#define PIN_FINISH_BUTTON  0
#define POT_PIN            A0

// Pantalla OLED (I2C)
#define OLED_I2C_ADDRESS   0x3C
#define OLED_SDA 2
#define OLED_SCL 3

// ------------------------------------------------------------
// Tiempos generales (ms)
// ------------------------------------------------------------

#define LORA_DEEP_SLEEP    900000   // sin recibir nada -> dormir la radio
#define SLEEP_TIMEOUT      300000   // inactividad -> suspender (5 min)

// Despertar desde SLEEP: nº de pulsaciones y ventana de tiempo
#define WAKE_PRESS_COUNT   3
#define WAKE_WINDOW_MS     45000

// Tiempo mínimo que permanece visible una pantalla de resultado (Enviado, etc.)
#define RESULT_MIN_MS      800

// Light-sleep de la CPU durante la suspensión (gran ahorro, ESP8266).
// Ponlo a 0 si en tu placa diera problemas al despertar (recuperable con RESET).
#define ENABLE_CPU_LIGHT_SLEEP  1

// ------------------------------------------------------------
// Función de configuración de pines
// ------------------------------------------------------------
inline void setupPins() {
  pinMode(PIN_MORSE_BUTTON, INPUT_PULLUP);
  pinMode(PIN_FINISH_BUTTON, INPUT_PULLUP);
  // Los pines del LoRa son gestionados por la librería
  pinMode(POT_PIN, INPUT);
}

#endif
