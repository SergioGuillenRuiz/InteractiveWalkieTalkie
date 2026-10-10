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

// Paquetes de JUEGO (Tetris 'T..', Tres en raya 0x07, HippoRadar "HR"). backgroundTick() es el UNICO que lee
// la radio: atiende ella los paquetes de chat (mensajes, ACK, balizas, dibujos) y deja aqui los de juego para
// la partida en curso, que los recoge con Game_nextPacket(). Asi, durante una partida los mensajes siguen
// llegando (al historial, con su ACK) y los ACK/balizas no se confunden con paquetes del juego.
bool Game_nextPacket(String &out, int *rssi = nullptr);   // el siguiente paquete de juego ya descifrado (y su RSSI en dBm); false si no hay
void Game_dropPackets();             // descarta los pendientes (al empezar una partida)

bool handleIdle();
bool handleSleep();
bool handleSendMenu();
bool handleHistoryMenu();
bool handleGamesMenu();

#endif