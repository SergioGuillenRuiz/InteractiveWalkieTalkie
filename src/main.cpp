#include <Arduino.h>
#include "Config.h"
#include "Display.h"
#include "MyLora.h"
#include "Morse.h"
#include "States.h"
#include "Historial.h"



// ============================================================
// setup
// ============================================================
void setup() {

  Serial.begin(115200);

  setupPins();

  Display_begin();

  History_load();

  Lora_begin();

  //MENSAJES DE PRUEBA: BORRAR AL TERMINAR DE TESTEEAR
  //History_addMessage("Hola, probando.");
  //History_addMessage("Otro mensaje para el historial.");
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
