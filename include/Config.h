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

// AHORRO DE ENERGÍA OPCIONAL (en suspensión). Si no se oye NADA (ni mensajes ni balizas del compañero)
// durante este tiempo, la radio se duerme del todo: ahorra ~11 mA, pero el equipo deja de oír y de avisar
// hasta que lo despiertes (con 3 pulsaciones; al despertar emite una baliza y el compañero le reenvía lo
// pendiente). 0 = no dormir nunca la radio: siempre alcanzable (lo que necesita el aviso con la pantalla
// apagada), a cambio de ~11 mA continuos. Es el valor inicial: se puede cambiar en marcha con
// Lora_setDeepSleepMs() (p.ej. desde un futuro ajuste de "ahorro de energía"). Qué política es la buena
// depende del uso (alcanzable siempre / más autonomía) y queda pendiente de decidir; por defecto, la radio
// no se duerme nunca.
#define LORA_DEEP_SLEEP    0
#define SLEEP_TIMEOUT      300000   // inactividad -> suspender (5 min)

// Despertar desde SLEEP: nº de pulsaciones y ventana de tiempo
#define WAKE_PRESS_COUNT   3
#define WAKE_WINDOW_MS     45000

// Tiempo mínimo que permanece visible una pantalla de resultado (Enviado, etc.)
#define RESULT_MIN_MS      800

// Mantener B este tiempo en el menú principal abre "Poner la hora" (ClockSetup.h)
#define CLOCK_SETUP_HOLD_MS  1500

// Con la pantalla apagada (suspensión), un mensaje nuevo la enciende este tiempo para
// avisar (A = leer, B = cerrar; sin pulsar, vuelve a apagarse sola).
#define SLEEP_ALERT_MS       8000

// Light-sleep de la CPU durante la suspensión (gran ahorro, ESP8266).
// Ponlo a 0 si en tu placa diera problemas al despertar (recuperable con RESET).
#define ENABLE_CPU_LIGHT_SLEEP  1

// ------------------------------------------------------------
// Mensajería
// ------------------------------------------------------------

// Longitud máxima del texto de un mensaje enviable. El sobre de chat añade 3 bytes; el cifrado admite hasta
// 223 bytes de texto plano (un paquete LoRa binario de 255 B, ver SimpleCrypto.cpp), pero el historial y la
// outbox guardan 99 caracteres y un mensaje largo ocupa mucho el canal (95 B de texto = 112 B en el aire =
// 190 ms a SF7), asi que el tope sigue en 92 + 3 = 95.
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
// La baliza sale cada BEACON_INTERVAL_MS +-BEACON_JITTER_MS (aleatorio): dos equipos
// encendidos a la vez no deben emitir SIEMPRE juntos (la radio no oye mientras emite:
// se taparian la baliza una y otra vez y no se verian nunca).
#define BEACON_JITTER_MS     3000UL
// Companeros que se recuerdan a la vez (presencia, bateria): con mas, se sustituye el
// que lleva mas tiempo sin oirse.
#define MAX_PEERS            4

// --- Ritmo de los juegos por radio ---
// La radio es half-duplex y el canal es uno solo: un equipo que emite mas del ~50 % del tiempo no puede oir al
// otro, y la ocupacion de los dos suma (el limite fisico es 100 %). Los paquetes viajan en binario (un paquete
// corto = 32 B = 72 ms a SF7; el estado de Tetris = 64 B = 118 ms; un ping del radar a SF10 = 453 ms) y los
// juegos emiten a un ritmo que deja el canal libre para el otro y para el chat:
//  - HippoRadar: un ping cada RADAR_PING_MS + 0..RADAR_PING_JITTER_MS (453 ms en el aire: ~34 % por equipo).
//    Antes salia cada ~340 ms con paquetes de 698 ms (205 %): el equipo estaba SIEMPRE emitiendo, sordo a los
//    pings del otro y con el bucle colgado esperando a que acabase cada emision.
//  - Tetris 2J: el anfitrion difunde el estado cada TETRIS_STATE_MS (118 ms: ~35 %) y el invitado envia su
//    entrada cada TETRIS_INPUT_MS (72 ms: ~26 %). Antes 302 ms cada 280 ms (108 %) y 118 ms cada 200 ms.
#define RADAR_PING_MS         1200UL
#define RADAR_PING_JITTER_MS  300UL
#define TETRIS_STATE_MS       340UL
#define TETRIS_INPUT_MS       280UL

// --- Acceso al canal ---
// La radio es half-duplex y el canal es uno solo: lo que se emite mientras otro equipo emite se pierde
// (las dos tramas). Por eso:
//  - ESCUCHAR ANTES DE HABLAR: antes de emitir se mira si entra una trama; si el canal esta ocupado se espera a
//    que acabe y luego una pausa aleatoria 1..CSMA_BACKOFF_MS (dos equipos que esperaban lo mismo no deben
//    salir a la vez). Pasados CSMA_MAX_WAIT_MS se emite igualmente.
//  - Varios equipos que reciben el MISMO mensaje lo confirman (ACK) en el mismo instante y los ACK se pisarian
//    en el emisor: cada ACK sale tras un retardo aleatorio ACK_DELAY_MIN_MS..ACK_DELAY_MAX_MS.
//  - Los reintentos se reparten +-MSG_RETRY_JITTER_MS: dos equipos que perdieron su mensaje a la vez
//    (colision) no deben reintentarlo otra vez a la vez.
#define CSMA_MAX_WAIT_MS     1500UL
#define CSMA_BACKOFF_MS      80UL
#define ACK_DELAY_MIN_MS     10UL
#define ACK_DELAY_MAX_MS     300UL
#define ACK_QUEUE            4          // ACK pendientes de enviar a la vez
#define MSG_RETRY_JITTER_MS  1500UL

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
