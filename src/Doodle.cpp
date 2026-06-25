#include <Arduino.h>
#include <string.h>
#include "Doodle.h"
#include "Display.h"
#include "Inputs.h"
#include "States.h"
#include "Chat.h"

// ============================================================
//  Lienzo 24x24: editor con pote + 2 botones, y visor de dibujos recibidos
//  (con reapertura desde el Historial).
// ============================================================

static const unsigned long SEND_HOLD_MS = 1500;   // mantener B -> enviar
static const unsigned long A_HOLD_MS    = 300;    // mantener A -> modo "mover en X"
static const unsigned long CANCEL_MS    = 25000;  // inactividad -> descartar

// --- Acceso a bits del lienzo (fila = DOODLE_ROWBYTES bytes, MSB primero) ---
static inline bool getPx(const uint8_t *b, int x, int y) {
  return (b[y * DOODLE_ROWBYTES + (x >> 3)] >> (7 - (x & 7))) & 1;
}
static inline void setPx(uint8_t *b, int x, int y) {     // pulsar A: pinta
  b[y * DOODLE_ROWBYTES + (x >> 3)] |= (uint8_t)(1 << (7 - (x & 7)));
}
static inline void clearPx(uint8_t *b, int x, int y) {   // pulsar B: borra
  b[y * DOODLE_ROWBYTES + (x >> 3)] &= (uint8_t)~(1 << (7 - (x & 7)));
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
  display.setCursor(0, 110); display.print("A pinta B borra Pot:Y");
  display.setCursor(0, 119); display.print("A+Pot:X B largo:envia");
  display.display();
}

void startDoodle() {
  uint8_t canvas[DOODLE_BYTES];
  memset(canvas, 0, sizeof(canvas));
  int cursorX = 0, cursorY = 0;
  unsigned long lastInput = millis();
  unsigned long mStart = 0, fStart = 0;
  bool mWas = false, fWas = false, mHold = false, fLong = false;
  int lastX = -1, lastY = -1; bool dirty = true;

  while (isMorsePressed()) { backgroundTick(); delay(10); }   // soltar la A de "entrar"

  while (true) {
    backgroundTick();
    unsigned long now = millis();
    bool mNow = isMorsePressed(), fNow = isFinishPressed();

    // Flancos de pulsacion
    if (mNow && !mWas) { mStart = now; mHold = false; }
    if (fNow && !fWas) { fStart = now; fLong = false; }

    // A mantenida -> modo "mover en X": el pote pasa a controlar la COLUMNA
    if (mNow && !mHold && now - mStart >= A_HOLD_MS) { mHold = true; lastInput = now; }

    // Potenciometro: FILA (Y, sube/baja) por defecto; COLUMNA (X) con A mantenida
    int cell = getPotValue(DOODLE_DIM - 1);
    if (mHold) {
      if (cell != cursorX) { cursorX = cell; lastInput = now; }
    } else if (!mNow) {
      if (cell != cursorY) { cursorY = cell; lastInput = now; }
    }

    // B mantenida -> ENVIAR
    if (fNow && !fLong && now - fStart >= SEND_HOLD_MS) {
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

    // Sueltas: pulsar A (corta) PINTA, pulsar B (corta) BORRA
    if (!mNow && mWas) {
      if (!mHold) { setPx(canvas, cursorX, cursorY); lastInput = now; dirty = true; }
      mHold = false;
    }
    if (!fNow && fWas && !fLong) { clearPx(canvas, cursorX, cursorY); lastInput = now; dirty = true; }
    mWas = mNow; fWas = fNow;

    if (now - lastInput > CANCEL_MS) break;   // inactividad -> descartar

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
//  Recepcion. Se guarda el ULTIMO dibujo recibido junto con (emisor, epoch),
//  el mismo sello de tiempo que lleva su registro en el Historial, para poder
//  REABRIRLO desde alli (Doodle_isStored()/Doodle_drawStored()).
// ============================================================
static uint8_t       g_rx[DOODLE_BYTES];
static uint8_t       g_rxFrom    = 0;
static unsigned long g_rxEpoch   = 0;
static bool          g_hasStored = false;   // hay un dibujo guardado (epoch puede ser 0 sin reloj)

void Doodle_onReceived(uint8_t sender, const uint8_t *buf, unsigned long epoch) {
  memcpy(g_rx, buf, DOODLE_BYTES);
  g_rxFrom    = sender;
  g_rxEpoch   = epoch;
  g_hasStored = true;
}

// --- Reapertura desde el Historial ---
bool Doodle_isStored(uint8_t sender, unsigned long epoch) {
  return g_hasStored && g_rxFrom == sender && g_rxEpoch == epoch;
}

void Doodle_drawStored(int ox, int oy) {
  blitCanvas(g_rx, ox, oy, 4, 4);   // 24x4 = 96 px (sin clear/display)
}
