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
static const unsigned long RESULT_MAX_MS = 30000; // pantalla de resultado sin tocar nada -> volver

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

void Doodle_draw(const uint8_t *bitmap, int ox, int oy) {
  blitCanvas(bitmap, ox, oy, 4, 4);   // 24x4 = 96 px (sin clear/display)
}

// Texto centrado horizontalmente en la fila y.
static void centerText(const String &t, int y, uint8_t size) {
  display.setTextSize(size);
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(t.c_str(), 0, 0, &bx, &by, &bw, &bh);
  int x = (SCREEN_WIDTH - (int)bw) / 2; if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(t);
}

// Pantalla de resultado del envio (como la de los mensajes de texto):
// fase 0 = esperando el ACK, 1 = entregado, 2 = sin confirmar (se seguira reintentando).
static void drawResult(const uint8_t *canvas, int phase) {
  Display_clear();
  display.setTextColor(SH110X_WHITE);
  blitCanvas(canvas, 52, 4, 1, 1);                      // miniatura 24x24 (1 px por celda)
  if (phase == 1) {                                     // tick
    for (int t = 0; t < 3; t++) {
      display.drawLine(51, 50 + t, 60, 59 + t, SH110X_WHITE);
      display.drawLine(60, 59 + t, 79, 40 + t, SH110X_WHITE);
    }
    centerText("Entregado!", 68, 2);
    centerText("visto por el otro", 108, 1);
  } else {
    centerText("Enviado", 50, 2);
    centerText(phase == 0 ? "esperando confirm..." : "(sin confirmar)", 108, 1);
  }
  display.display();
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
      Chat_resetPending();
      bool ok = Chat_sendDoodle(canvas);
      if (!ok) {
        Display_clear();
        display.setTextSize(1); display.setTextColor(SH110X_WHITE);
        display.setCursor(0, 0); display.println("Error al enviar");
        display.display();
      }
      unsigned long t0 = millis();
      int phase = 0;
      if (ok) drawResult(canvas, phase);
      // Pantalla de resultado: se queda hasta pulsar un boton (como la de texto). Mientras tanto la radio
      // sigue atendida (llega el ACK, se reintenta...) y el texto se actualiza solo.
      while (isFinishPressed() || isMorsePressed()) { backgroundTick(); delay(10); }   // soltar la B del envio
      while (true) {
        backgroundTick();
        unsigned long tn = millis();
        if (ok && phase != 1) {
          if (Chat_awaitingAck() && Chat_delivered()) { phase = 1; drawResult(canvas, phase); }
          else if (phase == 0 && tn - t0 > 3000)       { phase = 2; drawResult(canvas, phase); }
        }
        if (tn - t0 >= RESULT_MIN_MS && (isMorsePressed() || isFinishPressed())) break;
        if (tn - t0 > RESULT_MAX_MS) break;
        delay(10);
      }
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
