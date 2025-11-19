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
// Tiempos generales
// ------------------------------------------------------------

#define LORA_DEEP_SLEEP    900000
#define SLEEP_TIMEOUT      300000

// ============================================================
// Gestión de estados principales
// ============================================================

enum MainState {
  STATE_IDLE,
  STATE_SLEEP,
  STATE_SEND_MENU,
  STATE_HISTORY_MENU,
  STATE_GAMES_MENU
};

extern MainState mainState;
extern unsigned long stateSince;

inline void changeState(MainState s) {
  mainState = s;
  stateSince = millis();
}

// ============================================================
// Gestión de subestados de envío de mensajes
// ============================================================

enum SendSubState {
    SEND_WAIT,
    SEND_MORSE,
    SEND_INSTANT_MSG
};

extern SendSubState sendSubState;
extern unsigned long sendSubStateSince;

inline void changeSendSubState(SendSubState s) {
    sendSubState = s;
    sendSubStateSince = millis();
}

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
