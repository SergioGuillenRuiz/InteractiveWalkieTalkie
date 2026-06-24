#include <Arduino.h>
#include <string.h>
#include "Doodle.h"
#include "Display.h"
#include "Inputs.h"
#include "States.h"
#include "Chat.h"

// ============================================================
//  Lienzo 16x16: editor con pote + 2 botones, y visor de dibujos recibidos.
// ============================================================

static const unsigned long SEND_HOLD_MS = 1500;   // FINISH largo -> enviar
static const unsigned long ROW_HOLD_MS  = 600;    // MORSE largo -> bajar fila
static const unsigned long CANCEL_MS    = 25000;  // inactividad -> descartar

// --- Acceso a bits del lienzo (fila = DOODLE_ROWBYTES bytes, MSB primero) ---
static inline bool getPx(const uint8_t *b, int x, int y) {
  return (b[y * DOODLE_ROWBYTES + (x >> 3)] >> (7 - (x & 7))) & 1;
}
static inline void togglePx(uint8_t *b, int x, int y) {
  b[y * DOODLE_ROWBYTES + (x >> 3)] ^= (uint8_t)(1 << (7 - (x & 7)));
}

// --- Dibujado del lienzo (escala 'pitch'; celda 'cell') ---
static void blitCanvas(const uint8_t *b, int ox, int oy, int pitch, int cell) {
  for (int y = 0; y < DOODLE_DIM; y++)
    for (int x = 0; x < DOODLE_DIM; x++)
      if (getPx(b, x, y)) display.fillRect(ox + x * pitch, oy + y * pitch, cell, cell, SH110X_WHITE);
}

static void drawEditor(const uint8_t *c, int cx, int cy) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0); display.print("Dibujo");
  const int ox = 16, oy = 12, pitch = 4, cell = 4;   // 24x4 = 96 px
  blitCanvas(c, ox, oy, pitch, cell);
  display.fillRect(ox + cx * pitch, oy + cy * pitch, cell, cell, SH110X_INVERSE);   // cursor
  display.setCursor(0, 110); display.print("A pinta  manten:baja");
  display.setCursor(0, 119); display.print("B sube   manten:envia");
  display.display();
}

void startDoodle() {
  uint8_t canvas[DOODLE_BYTES];
  memset(canvas, 0, sizeof(canvas));
  int cursorX = 0, cursorY = 0;
  unsigned long lastInput = millis();
  unsigned long mStart = 0, fStart = 0;
  bool mWas = false, fWas = false, mLong = false, fLong = false;
  int lastX = -1, lastY = -1; bool dirty = true;

  while (isMorsePressed()) { backgroundTick(); delay(10); }   // soltar la A de "entrar"

  while (true) {
    backgroundTick();

    int px = getPotValue(DOODLE_DIM - 1);
    if (px != cursorX) { cursorX = px; lastInput = millis(); }

    bool mNow = isMorsePressed(), fNow = isFinishPressed();
    if (mNow && !mWas) { mStart = millis(); mLong = false; }
    if (fNow && !fWas) { fStart = millis(); fLong = false; }
    if (mNow && !mLong && millis() - mStart >= ROW_HOLD_MS) {
      mLong = true; cursorY = (cursorY + 1) % DOODLE_DIM; lastInput = millis();
    }
    if (fNow && !fLong && millis() - fStart >= SEND_HOLD_MS) {
      fLong = true;
      Chat_sendDoodle(canvas);
      Display_clear();
      display.setTextSize(1); display.setTextColor(SH110X_WHITE);
      display.setCursor(0, 0); display.println("Dibujo enviado!");
      display.display();
      unsigned long t0 = millis();
      while (millis() - t0 < 800) { backgroundTick(); delay(10); }
      break;
    }
    bool mShort = (!mNow && mWas) && !mLong;
    bool fShort = (!fNow && fWas) && !fLong;
    if (mShort) { togglePx(canvas, cursorX, cursorY); lastInput = millis(); }
    if (fShort) { cursorY = (cursorY + DOODLE_DIM - 1) % DOODLE_DIM; lastInput = millis(); }
    mWas = mNow; fWas = fNow;

    if (millis() - lastInput > CANCEL_MS) break;   // inactividad -> descartar

    if (dirty || cursorX != lastX || cursorY != lastY) {
      drawEditor(canvas, cursorX, cursorY);
      lastX = cursorX; lastY = cursorY; dirty = false;
    }
    delay(15);
  }

  while (isFinishPressed() || isMorsePressed()) { backgroundTick(); delay(10); }
  mainState = STATE_IDLE;
  Display_clear();
}

// ============================================================
//  Recepcion
// ============================================================
static uint8_t g_rx[DOODLE_BYTES];
static uint8_t g_rxFrom = 0;
static bool    g_pending = false;

void Doodle_onReceived(uint8_t sender, const uint8_t *buf) {
  memcpy(g_rx, buf, DOODLE_BYTES);
  g_rxFrom = sender;
  g_pending = true;
}

bool Doodle_pending() { return g_pending; }

void Doodle_showPending() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0); display.print("Dibujo de #"); display.print(g_rxFrom);
  blitCanvas(g_rx, 16, 12, 4, 4);   // 24x4 = 96 px
  display.setCursor(0, 119); display.print("A/B: ok");
  display.display();

  while (!isMorsePressed() && !isFinishPressed()) { backgroundTick(); delay(15); }
  while (isMorsePressed() || isFinishPressed()) { backgroundTick(); delay(10); }
  g_pending = false;
  Display_clear();
}
