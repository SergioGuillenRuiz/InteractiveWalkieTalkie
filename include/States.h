#ifndef STATES_H
#define STATES_H

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

// ============================================================
// Gestión de subestados de envío de mensajes
// ============================================================

enum SendSubState {
    SEND_WAIT,
    SEND_MORSE,
    SEND_INSTANT_MSG,
    SEND_DIAL,
    SEND_PHRASE,
    SEND_DOODLE
};

extern SendSubState sendSubState;

//=============================================================
// FUNCIONES DE GESTIÓN DE ESTADOS
//=============================================================

// Atiende la radio (recibe y guarda mensajes) y refresca la pantalla.
// Llamar en el bucle principal y en cualquier espera bloqueante para poder
// recibir mensajes en todo momento.
void backgroundTick();

bool handleIdle();
bool handleSleep();
bool handleSendMenu();
bool handleHistoryMenu();
bool handleGamesMenu();

#endif