#include <Arduino.h>
#include "Config.h"

//=============================================================
// VARIABLES DE CONTROL
//=============================================================

 extern unsigned long lastInteraction;
 extern unsigned long lastTimeReceived;
 extern int cursorPos;
 extern int buttonPressCount;
 extern unsigned long firstPressTime;

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

//=============================================================
// FUNCIONES DE GESTIÓN DE ESTADOS
//=============================================================

bool handleIdle();   
bool handleSleep();
bool handleSendMenu();
bool handleHistoryMenu();  
bool handleGamesMenu();