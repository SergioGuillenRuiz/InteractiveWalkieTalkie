#include <Arduino.h>
#include "Settings.h"
#include "ClockSetup.h"
#include "Pairing.h"
#include "Display.h"
#include "Inputs.h"
#include "States.h"

static const unsigned long INACTIVITY_CANCEL_MS = 60000;   // sin tocar nada -> sale

static void centerPrint(const String &s, int y, uint8_t size = 1) {
  display.setTextSize(size);
  display.setTextColor(SH110X_WHITE);
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(s.c_str(), 0, 0, &bx, &by, &bw, &bh);
  int x = (128 - (int)bw) / 2; if (x < 0) x = 0;
  display.setCursor(x, y); display.print(s);
}

static void drawSettings(int sel) {
  display.clearDisplay();
  drawTitleBar("Ajustes");
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(14, 40); display.print(sel == 0 ? "> Poner la hora" : "  Poner la hora");
  display.setCursor(14, 58); display.print(sel == 1 ? "> Emparejar" : "  Emparejar");
  display.setCursor(14, 76);
  display.print(Pairing_isPaired() ? "(con pareja)" : "(SIN PAREJA)");
  centerPrint("Pote: elegir", 94, 1);
  centerPrint("A: abrir  B: salir", 106, 1);
  display.display();
}

void startSettings() {
  while (isFinishPressed() || isMorsePressed()) { backgroundTick(); delay(10); }   // soltar la B de "abrir"
  int sel = getPotValue(1), shown = -1;
  bool pa = false, pb = false;
  unsigned long lastInput = millis();
  int chosen = -1;

  while (true) {
    backgroundTick(false);
    unsigned long now = millis();
    int m = getPotValue(1);
    if (m != sel) { sel = m; lastInput = now; }
    bool a = isMorsePressed(), b = isFinishPressed();
    if (a && !pa) { chosen = sel; break; }
    if (b && !pb) break;
    pa = a; pb = b;
    if (now - lastInput > INACTIVITY_CANCEL_MS) break;
    if (sel != shown) { drawSettings(sel); shown = sel; }
    delay(15);
  }

  if (chosen == 0) { startClockSetup(); return; }      // cada pantalla vuelve sola al menu principal
  if (chosen == 1) { startPairing(); return; }

  while (isFinishPressed() || isMorsePressed()) { backgroundTick(); delay(10); }
  mainState = STATE_IDLE;
  lastInteraction = millis();
  Display_clear();
}

void showNeedPairing() {
  display.clearDisplay();
  drawTitleBar("Sin pareja");
  centerPrint("Primero hay que", 30, 1);
  centerPrint("emparejar los dos", 42, 1);
  centerPrint("equipos. En el menu", 54, 1);
  centerPrint("principal manten B", 74, 1);
  centerPrint("y elige Emparejar", 86, 1);
  display.display();
  unsigned long t0 = millis();
  bool pa = isMorsePressed(), pb = isFinishPressed();
  while (millis() - t0 < 4000) {
    backgroundTick(false);
    bool a = isMorsePressed(), b = isFinishPressed();
    if (((a && !pa) || (b && !pb)) && millis() - t0 > 300) break;
    pa = a; pb = b;
    delay(15);
  }
  while (isFinishPressed() || isMorsePressed()) { backgroundTick(); delay(10); }
  lastInteraction = millis();
  Display_clear();
}
