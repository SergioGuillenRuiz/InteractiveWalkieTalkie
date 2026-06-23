#include <Arduino.h>
#include <string.h>
#include "TresEnRaya.h"
#include "Display.h"
#include "Inputs.h"
#include "States.h"
#include "MyLora.h"
#include "Identity.h"

// ============================================================
//  Tres en raya por LoRa (2 jugadores).
//
//  Protocolo (marcador 0x07, no colisiona con chat 0x01..0x06 ni con los juegos
//  ASCII 'T'/'H'):
//    HELLO:  0x07 'H' | emisor               (emparejamiento)
//    ESTADO: 0x07 'S' | emisor | tablero[9] | fin(1)   (1 byte por casilla)
//
//  Emparejamiento: ambos difunden HELLO; al oir al rival se fija el rol por id
//  (id menor = X, mueve primero). La sincronizacion es por ESTADO: quien mueve
//  difunde el tablero completo y el rival lo adopta (robusto ante perdidas).
// ============================================================

static const char TTT_MARK = 0x07;
static const unsigned long HELLO_EVERY = 500;

static uint8_t board[9];     // 0 vacio, 1 = X, 2 = O
static uint8_t myRole;       // 1 = X, 2 = O, 0 = sin emparejar
static uint8_t peerId;
static bool    paired;
static bool    over;
static uint8_t cursor;

// --- Lineas ganadoras ---
static const uint8_t LINES[8][3] = {
  {0,1,2},{3,4,5},{6,7,8}, {0,3,6},{1,4,7},{2,5,8}, {0,4,8},{2,4,6}
};

static uint8_t winnerOf(const uint8_t *b) {
  for (int i = 0; i < 8; i++) {
    uint8_t a = b[LINES[i][0]];
    if (a && a == b[LINES[i][1]] && a == b[LINES[i][2]]) return a;
  }
  return 0;
}
static bool boardFull(const uint8_t *b) { for (int i = 0; i < 9; i++) if (!b[i]) return false; return true; }
static int  filled(const uint8_t *b) { int n = 0; for (int i = 0; i < 9; i++) if (b[i]) n++; return n; }
static uint8_t turnRole(const uint8_t *b) { return (filled(b) % 2 == 0) ? 1 : 2; }   // X mueve en par

// --- Radio ---
static void sendHello() { String p; p += TTT_MARK; p += 'H'; p += (char)Device_id(); Lora_send(p); }
static void sendState() {
  String p; p += TTT_MARK; p += 'S'; p += (char)Device_id();
  for (int i = 0; i < 9; i++) p += (char)board[i];
  p += (char)(over ? 1 : 0);
  Lora_send(p);
}
// Devuelve 0 (nada), 'H' o 'S'; rellena 'sender'. Adopta el tablero si es 'S'.
static char recvPkt(uint8_t &sender) {
  if (!Lora_hasMessage()) return 0;
  String m = Lora_readMessage();
  if (m.length() < 3 || m[0] != TTT_MARK) return 0;   // no es nuestro: descartar
  char type = m[1];
  sender = (uint8_t)m[2];
  if (type == 'S' && m.length() >= 13) {
    uint8_t nb[9]; int fin = 0;
    for (int i = 0; i < 9; i++) { nb[i] = (uint8_t)m[3 + i]; if (nb[i]) fin++; }
    // Adoptar solo estados igual o MAS nuevos (mas fichas): asi un re-envio antiguo
    // no hace retroceder la partida y un estado perdido se recupera con el reenvio.
    if (fin >= filled(board)) {
      for (int i = 0; i < 9; i++) board[i] = nb[i];
      over = (m[12] != 0) || winnerOf(board) || boardFull(board);
    }
  }
  return type;
}

// --- Dibujo ---
static void drawCell(int idx, int ox, int oy, int cs) {
  int cx = ox + (idx % 3) * cs, cy = oy + (idx / 3) * cs;
  uint8_t v = board[idx];
  if (v == 1) {                                   // X
    display.drawLine(cx + 6, cy + 6, cx + cs - 6, cy + cs - 6, SH110X_WHITE);
    display.drawLine(cx + cs - 6, cy + 6, cx + 6, cy + cs - 6, SH110X_WHITE);
  } else if (v == 2) {                            // O
    display.drawCircle(cx + cs / 2, cy + cs / 2, cs / 2 - 6, SH110X_WHITE);
  }
}
static void drawBoard(bool myTurn) {
  display.clearDisplay();
  display.setTextSize(1); display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0); display.print("3 en raya");
  display.setCursor(96, 0); display.print(myRole == 1 ? "Tu:X" : "Tu:O");

  const int ox = 7, oy = 14, cs = 38;             // rejilla 3x38 = 114
  for (int i = 1; i < 3; i++) {
    display.drawLine(ox + i * cs, oy, ox + i * cs, oy + 3 * cs, SH110X_WHITE);
    display.drawLine(ox, oy + i * cs, ox + 3 * cs, oy + i * cs, SH110X_WHITE);
  }
  for (int i = 0; i < 9; i++) drawCell(i, ox, oy, cs);

  if (myTurn && !over) {                           // cursor en la casilla activa
    int cx = ox + (cursor % 3) * cs, cy = oy + (cursor / 3) * cs;
    display.drawRect(cx + 2, cy + 2, cs - 4, cs - 4, SH110X_WHITE);
  }

  display.setCursor(0, 119);
  if (over) {
    uint8_t w = winnerOf(board);
    if (w == myRole)      display.print("GANAS!  B: salir");
    else if (w)           display.print("Pierdes B: salir");
    else                  display.print("Empate  B: salir");
  } else if (myTurn) display.print("Tu turno: A coloca");
  else               display.print("Turno del rival...");
  display.display();
}

void startTresEnRaya() {
  memset(board, 0, sizeof(board));
  myRole = 0; peerId = 0; paired = false; over = false; cursor = 4;

  unsigned long lastHello = 0, lastResend = 0;
  bool mWas = false, fWas = false;
  unsigned long fStart = 0; bool fLong = false;

  while (isMorsePressed()) { delay(10); }          // soltar la A de "entrar"

  while (true) {
    unsigned long now = millis();

    // --- Radio entrante ---
    uint8_t sender = 0;
    char t = recvPkt(sender);
    if (t && !paired && sender != Device_id()) {   // emparejar al oir al rival
      peerId = sender; myRole = (Device_id() < peerId) ? 1 : 2; paired = true;
      Serial.print("[3enRaya] emparejado con #"); Serial.print(peerId);
      Serial.print(" soy "); Serial.println(myRole == 1 ? "X" : "O");
    }

    // --- Salir: FINISH largo ---
    bool fNow = isFinishPressed();
    if (fNow && !fWas) { fStart = now; fLong = false; }
    if (fNow && !fLong && now - fStart >= 800) { fLong = true; mainState = STATE_IDLE; Display_clear(); return; }
    bool fShort = (!fNow && fWas) && !fLong; fWas = fNow;

    if (!paired) {
      if (now - lastHello >= HELLO_EVERY) { sendHello(); lastHello = now; }
      display.clearDisplay();
      display.setTextSize(1); display.setTextColor(SH110X_WHITE);
      display.setCursor(0, 0); display.print("3 en raya");
      display.setCursor(8, 56); display.print("Buscando rival...");
      display.setCursor(0, 119); display.print("manten B: salir");
      display.display();
      delay(20);
      continue;
    }

    bool myTurn = !over && (turnRole(board) == myRole);

    // --- Robustez: mientras espero al rival, reenvio mi estado cada ~2 s (con
    //     jitter) por si se perdio mi ultima jugada. El rival solo adopta estados
    //     igual o mas nuevos, asi que el reenvio nunca hace retroceder la partida. ---
    if (!over && !myTurn && now - lastResend > (unsigned long)(1800 + random(400))) {
      sendState(); lastResend = now;
    }

    // --- Mi jugada ---
    if (myTurn) {
      int c = getPotValue(8);
      if (c != cursor) cursor = c;
      bool mNow = isMorsePressed();
      bool mTap = (!mNow && mWas);
      if (mTap && board[cursor] == 0) {            // colocar ficha
        board[cursor] = myRole;
        over = winnerOf(board) || boardFull(board);
        sendState();
        Serial.print("[3enRaya] juego casilla "); Serial.println(cursor);
      }
      mWas = mNow;
    } else {
      mWas = isMorsePressed();
    }

    // --- Fin de partida: salir con FINISH corto ---
    if (over && fShort) { mainState = STATE_IDLE; Display_clear(); return; }

    drawBoard(myTurn);
    delay(20);
  }
}
