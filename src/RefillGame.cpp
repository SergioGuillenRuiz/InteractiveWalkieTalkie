#include <Arduino.h>
#include "Display.h"
#include "Inputs.h"
#include "States.h"
#include "RefillGame.h"

// ============================================================
//  REFILL GAME - "La Cerveceria"
//
//  Van llegando jarras (cola arriba). La siguiente jarra se coloca SOLA en el
//  grifo y empieza a llenarse automaticamente: el POTENCIOMETRO regula el caudal.
//  PULSA A una vez para parar y entregarla. Puntos = cuanto te acercas a la marca
//  + lo rapido. Desbordar o quedarte muy corto cuesta una vida (3). Tras una breve
//  pausa entra la siguiente jarra automaticamente. Cuantas mas completes, mas
//  rapido llegan. Si la cola se llena -> game over.
//  Sin acentos ni 'n~': la fuente del OLED no los representa.
// ============================================================

// ---- Cola ----
#define BAR_CAP   5
#define MAX_WAIT  (BAR_CAP - 1)

// ---- Jarra del grifo (centrada) ----
#define MUG_W   46
#define MUG_X   41
#define MUG_Y   48
#define MUG_H   74
#define MUG_IL  (MUG_X + 5)
#define MUG_IR  (MUG_X + MUG_W - 5)
#define MUG_TOP (MUG_Y + 7)
#define MUG_BOT (MUG_Y + MUG_H - 6)
#define INNER_H (MUG_BOT - MUG_TOP)

// ---- Cola arriba ----
#define SHELF_Y 34
#define QMUG_W  14
#define QMUG_H  15
#define Q_X0    8
#define Q_STEP  22

// ---- Reglas ----
#define LIVES_START   3
#define MAX_FLOW      62.0f
#define SHORT_MARGIN  15
#define ARRIVAL_START 4500UL
#define ARRIVAL_STEP  170UL
#define ARRIVAL_MIN   1500UL
#define REFILL_PAUSE  650UL          // pausa breve entre jarra y jarra

struct Jug { int target; };
static Jug   bar[BAR_CAP];
static int   barCount;
static bool  serving;                 // hay una jarra llenandose en el grifo
static float level;
static unsigned long grabTime;
static unsigned long pauseUntil;      // no se sirve hasta este instante
static float potFlow;
static int   lives;
static long  score;
static int   completed;
static unsigned long arrivalInterval;
static unsigned long nextArrival;
static String feedbackMsg;
static bool   feedbackGood;
static unsigned long feedbackUntil;

// ============================================================
//  Utilidades
// ============================================================
static char pollButton() {
  static bool pa = false, pb = false;
  bool a = isMorsePressed(), b = isFinishPressed();
  char r = 0;
  if (a && !pa) r = 'A';
  else if (b && !pb) r = 'B';
  pa = a; pb = b;
  return r;
}

static void centerPrint(const String &s, int y, uint8_t size = 1) {
  display.setTextSize(size);
  display.setTextColor(SH110X_WHITE);
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(s.c_str(), 0, 0, &bx, &by, &bw, &bh);
  int x = (128 - (int)bw) / 2; if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(s);
}

// ============================================================
//  Jarra bonita (reutilizable): cristal, asa, cerveza, espuma,
//  brillo y burbujas. fillFrac 0..1.
// ============================================================
static void drawMugShape(int x, int y, int w, int h, float fillFrac) {
  int il = x + 5, ir = x + w - 5;
  int top = y + 7, bot = y + h - 6;
  int inner = bot - top;

  // Cristal (doble contorno) + labio
  display.drawRoundRect(x, y, w, h, 5, SH110X_WHITE);
  display.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 4, SH110X_WHITE);
  display.drawFastHLine(x + 4, y + 4, w - 8, SH110X_WHITE);
  // Asa
  int hy = y + h / 4, hh = h / 2;
  display.drawRoundRect(x + w - 3, hy, 12, hh, 5, SH110X_WHITE);
  display.drawRoundRect(x + w - 2, hy + 1, 10, hh - 2, 4, SH110X_WHITE);

  int lv = (int)(fillFrac * inner + 0.5f);
  if (lv < 0) lv = 0; if (lv > inner) lv = inner;
  int bt = bot - lv;

  // Brillo del cristal en la zona vacia
  if (bt > top + 6) display.drawFastVLine(il + 1, top + 4, bt - top - 6, SH110X_WHITE);

  if (lv > 0) {
    display.fillRect(il, bt, ir - il, bot - bt, SH110X_WHITE);     // cerveza
    if (lv > 10) {                                                  // burbujas
      display.drawPixel(il + 5, bot - 8, SH110X_BLACK);
      display.drawPixel(ir - 6, bot - 13, SH110X_BLACK);
      display.drawPixel(il + 10, bot - 18, SH110X_BLACK);
    }
    for (int fx = il + 3; fx <= ir - 1; fx += 6)                    // espuma
      display.fillCircle(fx, bt - 1, 3, SH110X_WHITE);
  }
}

// Marca objetivo de la jarra del grifo: triangulo + linea (color adaptado)
static void drawTargetMark() {
  int ty = MUG_BOT - bar[0].target;
  int dcol = ((int)level >= bar[0].target) ? SH110X_BLACK : SH110X_WHITE;
  for (int dx = MUG_IL; dx < MUG_IR; dx += 4) display.drawFastHLine(dx, ty, 2, dcol);
  display.fillTriangle(MUG_X - 10, ty - 4, MUG_X - 10, ty + 4, MUG_X - 3, ty, SH110X_WHITE);
}

// ============================================================
//  Mini-jarra (cola)
// ============================================================
static void drawMiniMug(int x, int y, float targetFrac) {
  display.drawRoundRect(x, y, QMUG_W, QMUG_H, 3, SH110X_WHITE);
  display.drawRoundRect(x + QMUG_W - 2, y + 4, 5, 7, 2, SH110X_WHITE);
  // Marca del nivel al que habra que llenarla (se ve venir)
  int top = y + 3, bot = y + QMUG_H - 3;
  int ty = bot - (int)(targetFrac * (bot - top) + 0.5f);
  for (int dx = x + 3; dx < x + QMUG_W - 2; dx += 2) display.drawPixel(dx, ty, SH110X_WHITE);
  display.drawFastHLine(x + 1, ty, 2, SH110X_WHITE);              // anclaje izquierdo
}

// ============================================================
//  Grifo + chorro
// ============================================================
static void drawTap(bool pouring, float flow, int beerTopY) {
  int sx = 64;
  display.fillRoundRect(sx - 11, 36, 22, 6, 2, SH110X_WHITE);   // barra del grifo
  display.fillRoundRect(sx + 7, 32, 4, 8, 2, SH110X_WHITE);     // palanca
  display.fillRect(sx - 2, 42, 4, 4, SH110X_WHITE);             // boquilla
  // Chorro: solo si sale cerveza de verdad; mas ancho/rapido a mas caudal
  if (pouring && flow > 0.04f) {
    int off = (millis() / 30) % 4;
    int w = (flow > 0.55f) ? 3 : 2;
    for (int yy = 46; yy < beerTopY - 2; yy += 4)
      display.fillRect(sx - w / 2, yy + off, w, 3, SH110X_WHITE);
  }
}

// ============================================================
//  HUD + cola + cartel
// ============================================================
static void drawHUD() {
  for (int i = 0; i < LIVES_START; i++) {
    int cx = 8 + i * 11;
    if (i < lives) drawHeart(cx, 7, SH110X_WHITE);
    else           display.drawRect(cx - 4, 3, 8, 8, SH110X_WHITE);
  }
  display.setTextSize(1); display.setTextColor(SH110X_WHITE);
  // jarras completadas (centro)
  display.setCursor(54, 3); display.print("x"); display.print(completed);
  // puntos (derecha)
  String pts = String(score);
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(pts.c_str(), 0, 0, &bx, &by, &bw, &bh);
  display.setCursor(126 - (int)bw, 3); display.print(pts);
}

static void drawQueue() {
  display.drawFastHLine(0, SHELF_Y, 100, SH110X_WHITE);          // la barra
  int waiting = barCount - 1;
  for (int i = 0; i < MAX_WAIT && i < waiting; i++)
    drawMiniMug(Q_X0 + i * Q_STEP, SHELF_Y - QMUG_H, bar[1 + i].target / (float)INNER_H);
  if (waiting >= MAX_WAIT && (millis() / 250) % 2) {
    display.setTextSize(1); display.setTextColor(SH110X_WHITE);
    display.setCursor(Q_X0 + MAX_WAIT * Q_STEP - 6, 12); display.print("!");
  }
}

static void drawFeedback() {
  if (millis() >= feedbackUntil) return;
  display.setTextSize(1);
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(feedbackMsg.c_str(), 0, 0, &bx, &by, &bw, &bh);
  int w = (int)bw + 10, x = (128 - w) / 2, y = 64, h = 15;
  if (feedbackGood) {
    display.fillRoundRect(x, y, w, h, 4, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  } else {
    display.fillRoundRect(x, y, w, h, 4, SH110X_BLACK);
    display.drawRoundRect(x, y, w, h, 4, SH110X_WHITE);
    display.setTextColor(SH110X_WHITE);
  }
  display.setCursor(x + 5, y + 4); display.print(feedbackMsg);
}

static void drawGame(bool pouring, float flow) {
  display.clearDisplay();
  drawHUD();
  drawQueue();
  if (barCount > 0) {
    int lv = (int)level; if (lv > INNER_H) lv = INNER_H;
    drawTap(pouring, flow, MUG_BOT - lv);
    drawMugShape(MUG_X, MUG_Y, MUG_W, MUG_H, level / (float)INNER_H);
    drawTargetMark();
  } else {
    drawTap(false, 0.0f, MUG_BOT);
    drawMugShape(MUG_X, MUG_Y, MUG_W, MUG_H, 0.0f);
  }
  drawFeedback();
  display.display();
}

// ============================================================
//  Logica
// ============================================================
static int randomTarget() { return random((int)(INNER_H * 0.35f), (int)(INNER_H * 0.85f)); }

static unsigned long computeInterval() {
  unsigned long red = (unsigned long)completed * ARRIVAL_STEP;
  if (red > ARRIVAL_START - ARRIVAL_MIN) return ARRIVAL_MIN;
  return ARRIVAL_START - red;
}

static void popFront() {
  for (int i = 1; i < barCount; i++) bar[i - 1] = bar[i];
  barCount--;
  serving = false; level = 0;
}

static void say(const String &m, bool good) {
  feedbackMsg = m; feedbackGood = good; feedbackUntil = millis() + 850;
}

static const char *rating(int dist) {
  if (dist <= 2)  return "PERFECTA!";
  if (dist <= 6)  return "GENIAL";
  if (dist <= 12) return "BIEN";
  return "VALE";
}

static void serveJug(unsigned long now) {
  int dist = (int)level - bar[0].target; if (dist < 0) dist = -dist;
  int closePts = 100 - dist * 7; if (closePts < 0) closePts = 0;
  int t = (int)(now - grabTime);
  int speedPts = 70 - t / 60;    if (speedPts < 0) speedPts = 0;
  int pts = closePts + speedPts;
  score += pts;
  completed++;
  say(String(rating(dist)) + " +" + String(pts), true);
  popFront();
}

static void initGame() {
  barCount = 0; serving = false; level = 0; potFlow = 0;
  lives = LIVES_START; score = 0; completed = 0;
  feedbackUntil = 0;
  arrivalInterval = ARRIVAL_START;
  bar[barCount++].target = randomTarget();
  nextArrival = millis() + arrivalInterval;
  pauseUntil = millis() + REFILL_PAUSE;     // breve respiro antes de la 1a jarra
}

// ============================================================
//  Pantallas
// ============================================================
static bool startScreen() {
  display.clearDisplay();
  centerPrint("CERVECERIA", 4, 1);
  drawMugShape(44, 18, 40, 50, 0.72f);              // jarra-logo, bien llena
  centerPrint("Llena hasta la marca", 74, 1);
  centerPrint("POTE: regula caudal", 86, 1);
  centerPrint("A: para y entrega", 98, 1);
  centerPrint("A: Jugar  B: Salir", 116, 1);
  display.display();
  while (true) {
    char k = pollButton();
    if (k == 'A') return true;
    if (k == 'B') return false;
    backgroundTick();
    delay(10);
  }
}

static bool gameOverScreen(bool barFull) {
  display.clearDisplay();
  centerPrint("GAME OVER", 14, 2);
  drawMugShape(50, 34, 28, 30, barFull ? 1.0f : 0.0f);
  centerPrint(barFull ? "Barra llena!" : "Sin vidas!", 70, 1);
  centerPrint(String("Puntos: ") + String(score), 84, 1);
  centerPrint(String("Jarras: ") + String(completed), 96, 1);
  centerPrint("A: Otra  B: Salir", 116, 1);
  display.display();
  while (true) {
    char k = pollButton();
    if (k == 'A') return true;
    if (k == 'B') return false;
    backgroundTick();
    delay(10);
  }
}

// ============================================================
//  Entrada principal
// ============================================================
void startRefillGame() {
  randomSeed(analogRead(A0) ^ micros());

  if (!startScreen()) { mainState = STATE_IDLE; Display_clear(); return; }

  while (true) {
    initGame();
    while (isMorsePressed()) { backgroundTick(); delay(10); }     // soltar la A de "jugar"
    bool prevA = isMorsePressed(), over = false, barFull = false;
    unsigned long last = millis();

    while (!over) {
      backgroundTick();
      unsigned long now = millis();
      float dt = (now - last) / 1000.0f; last = now;
      if (dt > 0.10f) dt = 0.10f;

      if (isFinishPressed()) { delay(40); if (isFinishPressed()) { mainState = STATE_IDLE; Display_clear(); return; } }

      if (now >= nextArrival) {
        if (barCount >= BAR_CAP) { over = true; barFull = true; break; }
        bar[barCount++].target = randomTarget();
        arrivalInterval = computeInterval();
        nextArrival = now + arrivalInterval;
      }

      // Caudal: curva de valvula (control fino con poco caudal) + suavizado, para
      // un llenado realista y sin tirones.
      int potRaw = analogRead(A0);
      float pf = (potRaw <= 24) ? 0.0f : (float)(potRaw - 24) / (1023.0f - 24.0f);
      float tf = pf * pf;
      potFlow += (tf - potFlow) * 0.25f;

      // La siguiente jarra entra sola y empieza a llenarse (sin pulsar nada)
      if (!serving && barCount > 0 && now >= pauseUntil) {
        serving = true; level = 0; grabTime = now;
      }

      // Mientras sirve, el caudal lo regula el potenciometro
      if (serving) {
        level += potFlow * MAX_FLOW * dt;
        if (level >= INNER_H) {                        // desborda -> pierdes vida, siguiente
          lives--; say("DESBORDA!", false); popFront();
          pauseUntil = now + REFILL_PAUSE;
        }
      }

      // Pulsar A una vez: deja de salir cerveza y se entrega la jarra
      bool aNow = isMorsePressed();
      if (serving && aNow && !prevA) {
        if (bar[0].target - (int)level > SHORT_MARGIN) { lives--; say("MUY POCO!", false); popFront(); }
        else serveJug(now);
        pauseUntil = now + REFILL_PAUSE;
      }
      prevA = aNow;

      if (lives <= 0) { over = true; break; }

      drawGame(serving, potFlow);
      delay(15);
    }

    delay(250);
    if (!gameOverScreen(barFull)) { mainState = STATE_IDLE; Display_clear(); return; }
  }
}
