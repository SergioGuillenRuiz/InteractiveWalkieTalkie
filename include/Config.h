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

// Mantener B este tiempo en el menú principal abre "Poner la hora" (ClockSetup.h)
#define CLOCK_SETUP_HOLD_MS  1500

// Light-sleep de la CPU durante la suspensión (gran ahorro, ESP8266).
// Ponlo a 0 si en tu placa diera problemas al despertar (recuperable con RESET).
#define ENABLE_CPU_LIGHT_SLEEP  1

// ------------------------------------------------------------
// Mensajería
// ------------------------------------------------------------

// Longitud máxima del texto de un mensaje enviable. El sobre de chat añade 3
// bytes y el cifrado limita el texto plano a 95 (ver SimpleCrypto.cpp): 95-3 = 92.
// Quien componga mensajes (Morse, Rueda, Frasero) debe respetar este tope para
// que el envío no falle silenciosamente al cifrar.
#define MSG_MAX_TEXT_LEN   92

// --- Entrega fiable (reintentos) ---
// Tras enviar, si no llega el ACK se reintenta hasta MSG_RETRY_MAX veces cada
// MSG_RETRY_MS. Agotados los reintentos activos, el mensaje queda en la outbox
// y se reenvia cuando se detecta de nuevo al companero (presencia). MSG_RETRY_MS
// es mayor que el timeout de "(sin confirmar)" (3 s) para no solapar de mas.
#define MSG_RETRY_MS       5000UL
#define MSG_RETRY_MAX      3

// --- Presencia (baliza "estoy aqui") ---
// Cada equipo emite una baliza periodica; si no se oye al companero en
// PRESENCE_TIMEOUT_MS se considera "fuera de alcance".
#define BEACON_INTERVAL_MS   30000UL
#define PRESENCE_TIMEOUT_MS  90000UL

// ------------------------------------------------------------
// Batería
// ------------------------------------------------------------

// Canal ADC de la batería. OJO: el ESP8266 tiene UN solo ADC y A0 ya lo usa el
// potenciómetro, así que en la placa real medir la batería requiere hardware
// dedicado (divisor en otro ADC / multiplexor) o ESP.getVcc() (sacrificando el
// pote). PIN_VBAT es un canal lógico que el simulador mockea; en hardware real,
// ver Battery.cpp (batteryReadRaw) para cablear la fuente real.
#define PIN_VBAT             0xA0    // canal lógico (no es A0=17); lo mockea el sim

// Umbrales de aviso de batería baja, con histéresis para no parpadear.
#define BATT_LOW_PCT         15      // por debajo -> "Batería baja"
#define BATT_OK_PCT          20      // por encima -> se borra el aviso

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
