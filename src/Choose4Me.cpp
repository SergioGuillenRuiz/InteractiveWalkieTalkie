#include <Arduino.h>
#include "Display.h"
#include "Inputs.h"
#include "States.h"

// ============================================================
// Choose4Me: una bola mágica (8-ball). Piensa una pregunta y da una respuesta
// aleatoria. Sin acentos ni 'ñ': la fuente del OLED no los representa.
// ============================================================

const char* respuestas[] = {
  "SI",
  "NO",
  "DEFINITIVAMENTE",
  "NO TE LO CREES NI TU",
  "CLARO QUE SI",
  "JAMAS",
  "ES SEGURO",
  "CONCENTRATE Y PREGUNTA DE NUEVO",
  "SIN DUDA",
  "MI FUENTE DICE QUE NO",
  "PERSPECTIVAS BUENAS",
  "SI, DEFINITIVAMENTE",
  "NO CUENTES CON ESO",
  "PUEDES CONTAR CON ELLO",
  "TODAS LAS SENALES APUNTAN A SI"
};
const int NUM_RESPUESTAS = sizeof(respuestas) / sizeof(respuestas[0]);

// ------------------------------------------------------------
// Utilidades de dibujo
// ------------------------------------------------------------

// Imprime una línea de texto centrada horizontalmente.
static void centerPrint(const String &s, int y, uint8_t size = 1) {
  display.setTextSize(size);
  display.setTextColor(SH110X_WHITE);
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(s.c_str(), 0, 0, &bx, &by, &bw, &bh);
  int x = (128 - (int)bw) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(s);
}

// Dibuja la bola mágica 8-ball: bola blanca con un "8". 'shaking' la agita.
void drawMagicBall(int x, int y, int size, bool shaking = false) {
  int jx = 0, jy = 0;
  if (shaking) { jx = random(-2, 3); jy = random(-2, 3); }
  int cx = x + size / 2 + jx;
  int cy = y + size / 2 + jy;
  display.fillCircle(cx, cy, size / 2, SH110X_WHITE);
  display.setTextSize(2);
  display.setTextColor(SH110X_BLACK);   // "8" negro sobre la bola blanca
  display.setCursor(cx - 5, cy - 7);
  display.print("8");
  display.setTextSize(1);
}

// Dibuja la respuesta partida por palabras y centrada en el hueco entre la bola
// y las opciones. Si cabe en una sola línea, la muestra grande (tamaño 2).
static void drawAnswer(const String &msg) {
  const int MAX_CHARS = 20;
  String lines[6];
  int n = 0;
  String rem = msg;
  while (rem.length() > 0 && n < 6) {
    int cut = rem.length();
    if ((int)rem.length() > MAX_CHARS) {
      cut = MAX_CHARS;
      for (int i = cut; i > 0; i--)
        if (rem.charAt(i) == ' ') { cut = i; break; }
    }
    lines[n++] = rem.substring(0, cut);
    rem = rem.substring(cut);
    if (rem.length() > 0 && rem.charAt(0) == ' ') rem = rem.substring(1);
  }

  // Respuesta de una sola línea que cabe a tamaño 2 -> grande y centrada.
  if (n == 1) {
    display.setTextSize(2);
    int16_t bx, by; uint16_t bw, bh;
    display.getTextBounds(lines[0].c_str(), 0, 0, &bx, &by, &bw, &bh);
    if ((int)bw <= 124) { centerPrint(lines[0], 68, 2); return; }
  }

  // Varias líneas -> tamaño 1, centradas verticalmente entre la bola y las opciones.
  int y = 52 + (58 - n * 12) / 2;
  if (y < 52) y = 52;
  for (int i = 0; i < n; i++) { centerPrint(lines[i], y); y += 12; }
}

// Animación "Pensando..." mientras se decide la respuesta.
void animateDecisionMaking() {
  const unsigned long DURACION = 2000;
  const unsigned long FRAME = 120;
  unsigned long start = millis();
  int frame = 0;

  while (millis() - start < DURACION) {
    display.clearDisplay();
    drawMagicBall(44, 22, 40, true);

    // "Pensando" centrado fijo + puntos animados que NO desplazan el texto
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    int baseX = (128 - 8 * 6) / 2;   // "Pensando" tiene 8 caracteres
    display.setCursor(baseX, 84);
    display.print("Pensando");
    int dots = frame % 4;
    for (int i = 0; i < dots; i++) display.print(".");
    display.display();

    unsigned long fs = millis();
    while (millis() - fs < FRAME) {
      if (isFinishPressed()) return;   // permitir cancelar
      backgroundTick();
      delay(10);
    }
    frame++;
  }
}

// ------------------------------------------------------------
// Pantallas
// ------------------------------------------------------------

static void drawStartScreen() {
  Display_clear();
  centerPrint("CHOOSE4ME", 2, 2);
  drawMagicBall(44, 22, 40, false);
  centerPrint("Piensa tu pregunta", 70);
  centerPrint("A: Preguntar", 96);
  centerPrint("B: Salir", 110);
  display.display();
}

static void drawResultScreen(const char *answer) {
  Display_clear();
  drawMagicBall(44, 6, 40, false);
  drawAnswer(answer);
  centerPrint("A: Otra vez", 110);
  centerPrint("B: Salir", 120);
  display.display();
}

// ------------------------------------------------------------
// Bucle del juego
// ------------------------------------------------------------

void playChoose4Me() {
  bool running = true;

  while (running) {
    drawStartScreen();

    // Esperar: MORSE = preguntar, FINISH = salir
    bool wantAnswer = false;
    bool waiting = true;
    while (waiting) {
      if (isMorsePressed()) { delay(50); if (isMorsePressed()) { wantAnswer = true; break; } }
      if (isFinishPressed()) { delay(50); if (isFinishPressed()) { running = false; break; } }
      backgroundTick();
      delay(10);
    }
    if (!wantAnswer) break;
    while (isMorsePressed()) delay(10);   // esperar a soltar

    // Pensar y mostrar respuesta
    Display_clear();
    animateDecisionMaking();
    drawResultScreen(respuestas[random(NUM_RESPUESTAS)]);

    // Esperar: MORSE = otra vez, FINISH = salir
    bool resultShown = true;
    while (resultShown) {
      if (isMorsePressed()) { delay(50); if (isMorsePressed()) { resultShown = false; break; } }
      if (isFinishPressed()) { delay(50); if (isFinishPressed()) { resultShown = false; running = false; break; } }
      backgroundTick();
      delay(10);
    }
    while (isMorsePressed() || isFinishPressed()) delay(10);   // esperar a soltar
  }

  Display_clear();
}

void startChoose4Me() {
  randomSeed(analogRead(A0) + millis());
  playChoose4Me();
  mainState = STATE_IDLE;
  Display_clear();
}
