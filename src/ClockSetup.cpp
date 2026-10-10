#include <Arduino.h>
#include "ClockSetup.h"
#include "Display.h"
#include "Inputs.h"
#include "States.h"
#include "Clock.h"
#include "Chat.h"
#include "Historial.h"
#include "Doodle.h"

static const unsigned long INACTIVITY_CANCEL_MS = 60000;   // sin tocar nada -> cancela

// Maximo de cada campo: horas, decenas de minuto, unidades de minuto.
static const int FIELD_MAX[3] = { 23, 5, 9 };

static void centerPrint(const String &s, int y, uint8_t size = 1) {
  display.setTextSize(size);
  display.setTextColor(SH110X_WHITE);
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(s.c_str(), 0, 0, &bx, &by, &bw, &bh);
  int x = (128 - (int)bw) / 2; if (x < 0) x = 0;
  display.setCursor(x, y); display.print(s);
}

// "HH:MM" en grande con el campo activo resaltado (casilla blanca, digitos en negro).
static void drawSetup(int field, const int v[3]) {
  display.clearDisplay();
  drawTitleBar("Poner la hora");

  const int cx[5] = { 19, 37, 55, 73, 91 };              // x de cada caracter (celda de 18 px)
  const char ch[5] = { (char)('0' + v[0] / 10), (char)('0' + v[0] % 10), ':',
                       (char)('0' + v[1]), (char)('0' + v[2]) };
  int boxX = (field == 0) ? 17 : (field == 1 ? 71 : 89);
  int boxW = (field == 0) ? 40 : 22;
  display.fillRoundRect(boxX, 31, boxW, 30, 3, SH110X_WHITE);

  display.setTextSize(3);
  for (int i = 0; i < 5; i++) {
    bool active = (field == 0 && i <= 1) || (field == 1 && i == 3) || (field == 2 && i == 4);
    display.setTextColor(active ? SH110X_BLACK : SH110X_WHITE);
    display.setCursor(cx[i], 34);
    display.print(ch[i]);
  }

  static const char *NAMES[3] = { "Horas", "Minutos: decenas", "Minutos: unidades" };
  centerPrint(NAMES[field], 70, 1);

  centerPrint("Pote: cambiar valor", 92, 1);
  centerPrint(field == 2 ? "A: guardar" : "A: siguiente", 104, 1);
  centerPrint(field == 0 ? "B: cancelar" : "B: atras", 115, 1);
  display.display();
}

// Espera atendiendo la radio. Lee los botones para mantener fresco su antirrebote:
// si no, un boton soltado durante la espera seguiria "pulsado" y al volver al menu
// principal se colaria como una pulsacion fantasma.
static void waitMs(unsigned long ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) { backgroundTick(); isMorsePressed(); isFinishPressed(); delay(10); }
}

void startClockSetup() {
  // Valores de partida: la hora actual (solo cambian al mover el pote).
  uint32_t secs = Clock_now() % 86400UL;
  int v[3];
  v[0] = (int)(secs / 3600);
  int mm = (int)((secs % 3600) / 60);
  v[1] = mm / 10;
  v[2] = mm % 10;

  int field = 0;
  int lastMapped = getPotValue(FIELD_MAX[0]);
  bool pa = false, pb = false;
  unsigned long lastInput = millis();
  bool dirty = true, save = false;

  while (isFinishPressed() || isMorsePressed()) { backgroundTick(); delay(10); }   // soltar la B de "abrir"

  while (true) {
    backgroundTick();
    unsigned long now = millis();

    // Pote: cambia el campo activo cuando se mueve de verdad (ya antirrebotado).
    int m = getPotValue(FIELD_MAX[field]);
    if (m != lastMapped) { lastMapped = m; v[field] = m; lastInput = now; dirty = true; }

    bool a = isMorsePressed(), b = isFinishPressed();
    if (a && !pa) {                                  // A: aceptar campo
      lastInput = now;
      if (field < 2) { field++; lastMapped = getPotValue(FIELD_MAX[field]); dirty = true; }
      else { save = true; break; }
    }
    if (b && !pb) {                                  // B: campo anterior / cancelar
      lastInput = now;
      if (field > 0) { field--; lastMapped = getPotValue(FIELD_MAX[field]); dirty = true; }
      else break;
    }
    pa = a; pb = b;

    if (now - lastInput > INACTIVITY_CANCEL_MS) break;   // abandonado: cancelar

    if (dirty) { drawSetup(field, v); dirty = false; }
    delay(15);
  }

  if (save) {
    int hour = v[0], minute = v[1] * 10 + v[2];
    int32_t step = Clock_setTimeOfDay(hour, minute);
    History_shiftTimestamps(step);      // las antiguedades no cambian aunque el epoch salte
    Chat_beaconSoon();                  // el companero adopta la hora ya, no a los 30 s
    Serial.print("[Clock] puesta a mano: "); Serial.println(Clock_hhmm());

    display.clearDisplay();
    drawTitleBar("Poner la hora");
    display.drawBitmap(54, 24, iconoTick, 20, 20, SH110X_WHITE);
    centerPrint("Hora guardada", 52, 1);
    centerPrint(Clock_hhmm(), 70, 3);
    display.display();
    waitMs(1500);
  } else {
    display.clearDisplay();
    centerPrint("Cancelado", 56, 1);
    display.display();
    waitMs(600);
  }

  while (isFinishPressed() || isMorsePressed()) { backgroundTick(); delay(10); }
  mainState = STATE_IDLE;
  lastInteraction = millis();
  Display_clear();
}
