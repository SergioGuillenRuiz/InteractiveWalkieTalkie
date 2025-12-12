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

}

// ============================================================
// maquina de estados principal
// ============================================================
void loop() {

  Lora_update();
  Display_update();
  
    // ==================== STATE_IDLE ====================
    if (mainState == STATE_IDLE) {
        
        if (handleIdle()) return;

    }

    // ==================== STATE_SLEEP ====================
    if (mainState == STATE_SLEEP) {

        if (handleSleep()) return;
    }

    // ==================== STATE_HISTORY_MENU ====================
    if (mainState == STATE_HISTORY_MENU) {

        if (handleHistoryMenu()) return;
    }

    // ==================== STATE_GAMES_MENU ====================
    if (mainState == STATE_GAMES_MENU) {

        if (handleGamesMenu()) return;
    }

    // ==================== STATE_SEND_MENU ====================
    if(mainState == STATE_SEND_MENU) {

        if (handleSendMenu()) return;

    }
}
