#include <Arduino.h>
#include "Config.h"
#include "Display.h"
#include "MyLora.h"
#include "Morse.h"
#include "States.h"
#include "Historial.h"
#include "Identity.h"
#include "Frasero.h"
#include "Clock.h"
#include "Chat.h"

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#endif

// ============================================================
// setup
// ============================================================
void setup() {

  Serial.begin(115200);

#if defined(ESP8266)
  // El proyecto NO usa WiFi, pero el ESP8266 enciende el modem al arrancar
  // (gasta ~decenas de mA para nada). Apagarlo (modo NULL) es el mayor ahorro
  // de batería y no afecta a nada del firmware.
  WiFi.mode(WIFI_OFF);
#endif

  setupPins();

  // En hardware real las globales arrancan en su valor inicial; en el simulador
  // un "reboot" re-llama setup() sin reinicializarlas, así que las fijamos aquí
  // explícitamente para que un reinicio sea fiel: arranca en IDLE y sin temporizadores
  // colgados (si no, tras un reinicio en frío millis()-lastInteraction se desbordaría
  // y entraría en suspensión de inmediato).
  mainState        = STATE_IDLE;
  sendSubState     = SEND_WAIT;
  lastInteraction  = millis();
  lastTimeReceived = millis();   // (para LORA_DEEP_SLEEP: "tiempo sin oir nada" cuenta desde el arranque)
  cursorPos        = 0;
  buttonPressCount = 0;
  firstPressTime   = 0;

  Display_begin();

  History_load();
  Frasero_load();
  Clock_load();          // restaura el epoch persistido (hora compartida)
  Chat_load();           // restaura la outbox (mensajes sin confirmar)

  Serial.print("[ID] Equipo #"); Serial.println(Device_id());

  Lora_begin();
}

// ============================================================
// maquina de estados principal
// ============================================================
void loop() {

  backgroundTick();   // recibe mensajes (en todo momento) y refresca pantalla

  switch (mainState) {
    case STATE_IDLE:         handleIdle();        break;
    case STATE_SLEEP:        handleSleep();       break;
    case STATE_HISTORY_MENU: handleHistoryMenu(); break;
    case STATE_GAMES_MENU:   handleGamesMenu();   break;
    case STATE_SEND_MENU:    handleSendMenu();    break;
  }
}
