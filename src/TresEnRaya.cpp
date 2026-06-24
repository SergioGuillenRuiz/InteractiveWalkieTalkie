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
static void tttCenter(const String &s, int y) {
  display.setTextSize(1);
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(s.c_str(), 0, 0, &bx, &by, &bw, &bh);
  int x = (128 - (int)bw) / 2; if (x < 0) x = 0;
  display.setCursor(x, y); display.print(s);
}

static void drawCell(int idx, int ox, int oy, int cs) {
  int cx = ox + (idx % 3) * cs, cy = oy + (idx / 3) * cs;
  uint8_t v = board[idx];
  const int pad = 7;
  if (v == 1) {                                   // X (grosor 2, centrada en la casilla)
    for (int t = 0; t < 2; t++) {
      display.drawLine(cx + pad, cy + pad + t, cx + cs - pad, cy + cs - pad + t, SH110X_WHITE);
      display.drawLine(cx + cs - pad, cy + pad + t, cx + pad, cy + cs - pad + t, SH110X_WHITE);
    }
  } else if (v == 2) {                            // O (grosor 2)
    int r = cs / 2 - pad;
    display.drawCircle(cx + cs / 2, cy + cs / 2, r, SH110X_WHITE);
    display.drawCircle(cx + cs / 2, cy + cs / 2, r - 1, SH110X_WHITE);
  }
}

static void drawBoard(bool myTurn) {
  display.clearDisplay();
  display.setTextSize(1); display.setTextColor(SH110X_WHITE);
  tttCenter("3 en raya", 2);

  const int cs = 32, ox = 16, oy = 13;            // rejilla 3x32 = 96, centrada (margen 16)
  display.drawLine(ox + cs,     oy, ox + cs,     oy + 3 * cs, SH110X_WHITE);
  display.drawLine(ox + 2 * cs, oy, ox + 2 * cs, oy + 3 * cs, SH110X_WHITE);
  display.drawLine(ox, oy + cs,     ox + 3 * cs, oy + cs,     SH110X_WHITE);
  display.drawLine(ox, oy + 2 * cs, ox + 3 * cs, oy + 2 * cs, SH110X_WHITE);
  for (int i = 0; i < 9; i++) drawCell(i, ox, oy, cs);

  if (myTurn && !over) {                           // cursor en la casilla activa
    int cx = ox + (cursor % 3) * cs, cy = oy + (cursor / 3) * cs;
    display.drawRect(cx + 2, cy + 2, cs - 4, cs - 4, SH110X_WHITE);
  }

  String s;
  if (over) {
    uint8_t w = winnerOf(board);
    s = (w == myRole) ? "GANAS!" : (w ? "Pierdes" : "Empate");
    s += "   B: salir";
  } else s = myTurn ? (myRole == 1 ? "Tu turno (X)" : "Tu turno (O)") : "Turno del rival...";
  tttCenter(s, 117);
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
      tttCenter("3 en raya", 2);
      // dos circulos buscandose (icono simple y simetrico)
      display.drawCircle(50, 56, 9, SH110X_WHITE);
      display.drawCircle(78, 56, 9, SH110X_WHITE);
      tttCenter("Buscando rival...", 80);
      tttCenter("manten B para salir", 117);
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
