#include <Arduino.h>
#include "Config.h"
#include "Display.h"
#include "MyLora.h"
#include "Morse.h"
#include "Inputs.h"
#include "Historial.h"
#include "States.h"


// ============================================================
// setup
// ============================================================
void setup() {

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
  
    if (mainState == STATE_IDLE) {

        if (handleIdle()) return;

    }

    if (mainState == STATE_SLEEP) {

        if (handleSleep()) return;
    }

    // ==================== STATE_HISTORY_MENU ====================
    if (mainState == STATE_HISTORY_MENU) {

        Display_centerText("HISTORY MENU");

        if (isFinishPressed()) {
            delay(50);
            if (isFinishPressed()) {
                mainState = STATE_IDLE;
                menuTransitionDelay();
                return;
            }
        }

        return;
    }

    // ==================== STATE_GAMES_MENU ====================
    if (mainState == STATE_GAMES_MENU) {

        Display_centerText("GAMES MENU");
        
        if (isFinishPressed()) {
            delay(50);
            if (isFinishPressed()) {
                mainState = STATE_IDLE;
                menuTransitionDelay();
                return;
            }
        }

        return;
    }

    // ==================== STATE_SEND_MENU ====================
    if(mainState == STATE_SEND_MENU) {

        if (handleSendMenu()) return;

    }
}
