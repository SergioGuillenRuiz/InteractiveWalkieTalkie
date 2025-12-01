#include <Arduino.h>
#include "Config.h"
#include "Display.h"
#include "MyLora.h"
#include "Morse.h"
#include "States.h"


#include "Historial.h"// BORRAR ESTA LÍNEA DESPUÉS DE TESTEAR


// ============================================================
// setup
// ============================================================
void setup() {

  History_addMessage("Test message"); // BORRAR ESTA LÍNEA DESPUÉS DE TESTEAR

  Serial.begin(115200);

  setupPins();

  Display_begin();

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

        animateHippo();
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
