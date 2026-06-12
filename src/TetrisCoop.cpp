#include <Arduino.h>
#include "Display.h"
#include "Inputs.h"
#include "States.h"
#include "MyLora.h"
#include "TetrisCoop.h"

// ============================================================
//  TETRIS COOP
//  Tetris clasico pero con DOS piezas a la vez en el MISMO tablero. Los dos
//  jugadores comparten tablero y puntuacion: hay que coordinarse para no
//  chocarse ni dejar agujeros. Se puede jugar contra un BOT (modo solo) o con
//  otro equipo por radio LoRa (2 jugadores).
//
//  Controles (cada jugador en su equipo):
//    POTENCIOMETRO -> mueve la pieza a izquierda/derecha
//    A (morse)     -> rota
//    B (finish)    -> caida rapida (toque) / salir (mantener)
//  Sin acentos ni 'n~': la fuente del OLED no los representa.
// ============================================================

#define BW 10          // ancho tablero (celdas)
#define BH 18          // alto tablero (celdas)
#define CELL 6         // px por celda
#define BX 3           // origen x del tablero (px)
#define BY 8           // origen y del tablero (px)

// 7 piezas x 4 rotaciones x 4 celdas (col,fila) en una caja 4x4
static const int8_t PIECES[7][4][4][2] = {
  // I
  {{{0,1},{1,1},{2,1},{3,1}}, {{2,0},{2,1},{2,2},{2,3}}, {{0,2},{1,2},{2,2},{3,2}}, {{1,0},{1,1},{1,2},{1,3}}},
  // O
  {{{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}}},
  // T
  {{{1,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{2,1},{1,2}}, {{0,1},{1,1},{2,1},{1,2}}, {{1,0},{0,1},{1,1},{1,2}}},
  // S
  {{{1,0},{2,0},{0,1},{1,1}}, {{1,0},{1,1},{2,1},{2,2}}, {{1,1},{2,1},{0,2},{1,2}}, {{0,0},{0,1},{1,1},{1,2}}},
  // Z
  {{{0,0},{1,0},{1,1},{2,1}}, {{2,0},{1,1},{2,1},{1,2}}, {{0,1},{1,1},{1,2},{2,2}}, {{1,0},{0,1},{1,1},{0,2}}},
  // J
  {{{0,0},{0,1},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{1,2}}, {{0,1},{1,1},{2,1},{2,2}}, {{1,0},{1,1},{0,2},{1,2}}},
  // L
  {{{2,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{1,2},{2,2}}, {{0,1},{1,1},{2,1},{0,2}}, {{0,0},{1,0},{1,1},{1,2}}},
};

struct Piece { int type, rot, x, y; bool alive; };

static uint8_t board[BH][BW];
static Piece P1, P2;
static long  score;
static int   lines;
static bool  p2isBot;
static int   bag[7], bagN;

// ---- temporizadores ----
static unsigned long gravMs;
static unsigned long p1FallAt, p2FallAt, p1MoveAt, botActAt;

// ---- bot ----
static int botTargetX, botTargetRot;
static bool botPlanned;

// ============================================================
//  Entrada (flanco)
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
  display.setCursor(x, y); display.print(s);
}

// ============================================================
//  Motor
// ============================================================
static int nextFromBag() {
  if (bagN == 0) {
    for (int i = 0; i < 7; i++) bag[i] = i;
    for (int i = 6; i > 0; i--) { int j = random(i + 1); int t = bag[i]; bag[i] = bag[j]; bag[j] = t; }
    bagN = 7;
  }
  return bag[--bagN];
}

// colision de pc contra bordes, pila y (opcional) la otra pieza activa
static bool collides(const Piece &pc, const Piece *other) {
  for (int i = 0; i < 4; i++) {
    int cx = pc.x + PIECES[pc.type][pc.rot][i][0];
    int cy = pc.y + PIECES[pc.type][pc.rot][i][1];
    if (cx < 0 || cx >= BW || cy >= BH) return true;
    if (cy >= 0 && board[cy][cx]) return true;
    if (other && other->alive) {
      for (int j = 0; j < 4; j++) {
        int ox = other->x + PIECES[other->type][other->rot][j][0];
        int oy = other->y + PIECES[other->type][other->rot][j][1];
        if (ox == cx && oy == cy) return true;
      }
    }
  }
  return false;
}

static void lockPiece(const Piece &pc) {
  for (int i = 0; i < 4; i++) {
    int cx = pc.x + PIECES[pc.type][pc.rot][i][0];
    int cy = pc.y + PIECES[pc.type][pc.rot][i][1];
    if (cy >= 0 && cy < BH && cx >= 0 && cx < BW) board[cy][cx] = 1;
  }
}

static int clearLinesIn(uint8_t b[BH][BW]) {
  int cleared = 0;
  for (int y = BH - 1; y >= 0; y--) {
    bool full = true;
    for (int x = 0; x < BW; x++) if (!b[y][x]) { full = false; break; }
    if (full) {
      cleared++;
      for (int yy = y; yy > 0; yy--)
        for (int x = 0; x < BW; x++) b[yy][x] = b[yy - 1][x];
      for (int x = 0; x < BW; x++) b[0][x] = 0;
      y++;   // revisar la misma fila otra vez
    }
  }
  return cleared;
}
static int clearLines() { return clearLinesIn(board); }

// Coloca pieza nueva buscando una columna libre cerca de spawnX (para no chocar
// con la pieza activa del otro jugador). Devuelve false si no cabe (game over).
static bool spawnPiece(Piece &pc, int spawnX, const Piece *other) {
  pc.type = nextFromBag();
  pc.rot = 0; pc.alive = true;
  for (int yy = 0; yy >= -2; yy--) {
    for (int d = 0; d <= 5; d++) {
      for (int s = (d == 0 ? 1 : -1); s <= 1; s += 2) {
        pc.x = spawnX + d * s; pc.y = yy;
        if (pc.x < -2 || pc.x > BW) continue;
        if (!collides(pc, other)) return true;
      }
    }
  }
  return false;
}

static void applyClearAndScore(int n) {
  if (n <= 0) return;
  static const int tab[5] = {0, 40, 100, 300, 1200};
  score += tab[n];
  lines += n;
  gravMs = 700 - (lines / 8) * 55;
  if (gravMs < 130) gravMs = 130;
}

// Baja la pieza hasta apoyarse (en la pila o en la otra pieza activa).
static void dropTo(Piece &pc, const Piece *other) {
  Piece t = pc; while (!collides(t, other)) { pc.y = t.y; t.y++; }
}

// Un paso de gravedad. Solo se BLOQUEA si se apoya en la pila o el suelo; si solo
// le estorba la otra pieza activa, espera (no flota ni se fija en el aire).
static bool gravityStep(Piece &pc, Piece *other, bool &over, int spawnX, bool isBot) {
  Piece t = pc; t.y++;
  if (!collides(t, other)) { pc.y = t.y; return false; }   // hueco libre: baja
  if (collides(t, NULL)) {                                  // pila/suelo: fijar
    lockPiece(pc);
    applyClearAndScore(clearLines());
    if (isBot) botPlanned = false;
    if (!spawnPiece(pc, spawnX, other)) over = true;
    return true;
  }
  return false;   // solo le estorba la otra pieza: esperar a que se aparte
}

// ============================================================
//  Bot "muy bueno": evalua cada (rotacion, columna), simula la caida sobre la
//  PILA y puntua el resultado (pesos El-Tetris). Luego mueve su pieza alli.
// ============================================================
static float evalBoard(uint8_t b[BH][BW], int justCleared) {
  int aggH = 0, holes = 0, bump = 0;
  int h[BW];
  for (int x = 0; x < BW; x++) {
    int top = BH;
    for (int y = 0; y < BH; y++) if (b[y][x]) { top = y; break; }
    h[x] = BH - top;
    aggH += h[x];
    bool seen = false;
    for (int y = 0; y < BH; y++) { if (b[y][x]) seen = true; else if (seen) holes++; }
  }
  for (int x = 0; x < BW - 1; x++) { int d = h[x] - h[x + 1]; bump += d < 0 ? -d : d; }
  return -0.510066f * aggH + 0.760666f * justCleared - 0.35663f * holes - 0.184483f * bump;
}

static void botPlan() {
  float best = -1e9f;
  botTargetRot = P2.rot; botTargetX = P2.x;
  for (int rot = 0; rot < 4; rot++) {
    for (int x = -2; x < BW; x++) {
      Piece t; t.type = P2.type; t.rot = rot; t.x = x; t.y = 0; t.alive = true;
      if (collides(t, NULL)) continue;
      while (!collides(t, NULL)) t.y++;       // caer
      t.y--;
      if (t.y < 0) continue;
      uint8_t tmp[BH][BW];
      memcpy(tmp, board, sizeof(board));
      for (int i = 0; i < 4; i++) {
        int cx = t.x + PIECES[t.type][rot][i][0];
        int cy = t.y + PIECES[t.type][rot][i][1];
        if (cy >= 0 && cy < BH && cx >= 0 && cx < BW) tmp[cy][cx] = 1;
      }
      int cl = clearLinesIn(tmp);          // limpiar antes de puntuar (clave)
      float s = evalBoard(tmp, cl);
      if (s > best) { best = s; botTargetRot = rot; botTargetX = t.x; }
    }
  }
  botPlanned = true;
}

// Un paso del bot: rota o se desplaza una celda hacia el objetivo; si ya esta
// alineado (o atascado por la otra pieza) suelta de golpe y bloquea.
static void botStep() {
  if (!botPlanned) botPlan();
  if (P2.rot != botTargetRot) {
    Piece t = P2; t.rot = (P2.rot + 1) % 4;
    if (!collides(t, &P1)) { P2.rot = t.rot; return; }
  }
  if (P2.x != botTargetX) {
    int dir = (botTargetX > P2.x) ? 1 : -1;
    Piece t = P2; t.x += dir;
    if (!collides(t, &P1)) { P2.x = t.x; return; }
    return;                                        // bloqueado por la otra pieza: esperar
  }
  // alineado -> caida rapida (se fijara en la gravedad si toca pila/suelo)
  dropTo(P2, &P1);
  p2FallAt = 0;
}

// ============================================================
//  Dibujo
// ============================================================
static void cellFilled(int cx, int cy) {
  if (cy < 0) return;
  display.fillRect(BX + cx * CELL + 1, BY + cy * CELL + 1, CELL - 1, CELL - 1, SH110X_WHITE);
}
static void cellHollow(int cx, int cy) {
  if (cy < 0) return;
  display.drawRect(BX + cx * CELL + 1, BY + cy * CELL + 1, CELL - 1, CELL - 1, SH110X_WHITE);
}

static void drawPiece(const Piece &pc, bool filled) {
  if (!pc.alive) return;
  for (int i = 0; i < 4; i++) {
    int cx = pc.x + PIECES[pc.type][pc.rot][i][0];
    int cy = pc.y + PIECES[pc.type][pc.rot][i][1];
    if (filled) cellFilled(cx, cy); else cellHollow(cx, cy);
  }
}

static void drawGame() {
  display.clearDisplay();
  // marco
  display.drawRect(BX - 1, BY - 1, BW * CELL + 2, BH * CELL + 2, SH110X_WHITE);
  // pila
  for (int y = 0; y < BH; y++)
    for (int x = 0; x < BW; x++)
      if (board[y][x]) cellFilled(x, y);
  drawPiece(P1, true);    // tu pieza: solida
  drawPiece(P2, false);   // la otra: hueca
  // HUD derecha
  int hx = BX + BW * CELL + 6;
  display.setTextSize(1); display.setTextColor(SH110X_WHITE);
  display.setCursor(hx, 10);  display.print("PUNTOS");
  display.setCursor(hx, 20);  display.print(score);
  display.setCursor(hx, 36);  display.print("LINEAS");
  display.setCursor(hx, 46);  display.print(lines);
  display.setCursor(hx, 64);  display.print(p2isBot ? "vs BOT" : "COOP");
  // leyenda piezas
  display.fillRect(hx, 84, 5, 5, SH110X_WHITE);  display.setCursor(hx + 9, 84); display.print("Tu");
  display.drawRect(hx, 96, 5, 5, SH110X_WHITE);  display.setCursor(hx + 9, 96); display.print(p2isBot ? "Bot" : "P2");
  display.display();
}

// ============================================================
//  Pantallas
// ============================================================
static int modeSelect() {     // 0=solo bot, 1=2 jugadores, -1=salir
  int sel = 0;
  unsigned long t = 0;
  while (true) {
    sel = getPotValue(1);     // 0 o 1
    display.clearDisplay();
    centerPrint("TETRIS COOP", 8, 1);
    // dos piezas decorativas
    centerPrint(((sel == 0) ? "> " : "  ") + String("Solo (vs Bot)"), 50, 1);
    centerPrint(((sel == 1) ? "> " : "  ") + String("2 Jugadores"), 64, 1);
    centerPrint("Pote: elegir", 96, 1);
    centerPrint("A: jugar  B: salir", 112, 1);
    display.display();
    char k = pollButton();
    if (k == 'A') return sel;
    if (k == 'B') return -1;
    backgroundTick();
    delay(20);
    (void)t;
  }
}

static bool gameOverScreen() {   // true = otra
  display.clearDisplay();
  centerPrint("GAME OVER", 22, 2);
  centerPrint(String("Puntos: ") + String(score), 56, 1);
  centerPrint(String("Lineas: ") + String(lines), 70, 1);
  centerPrint("A: Otra  B: Salir", 104, 1);
  display.display();
  while (true) {
    char k = pollButton();
    if (k == 'A') return true;
    if (k == 'B') return false;
    backgroundTick();
    delay(15);
  }
}

// ============================================================
//  Partida
// ============================================================
static void resetBoard() {
  for (int y = 0; y < BH; y++) for (int x = 0; x < BW; x++) board[y][x] = 0;
  score = 0; lines = 0; bagN = 0; gravMs = 700;
}

// Mueve P1 segun el potenciometro (columna objetivo) y aplica A/B.
// Devuelve: 0 normal, 1 salir.
static int playerControl(unsigned long now) {
  static unsigned long bHoldStart = 0;
  // movimiento horizontal con el pote (una celda cada 90 ms)
  if (now >= p1MoveAt) {
    int targetX = map(getPotValue(BW - 1), 0, BW - 1, 0, BW - 4);   // col objetivo de la caja
    if (targetX != P1.x && P1.alive) {
      int dir = targetX > P1.x ? 1 : -1;
      Piece t = P1; t.x += dir;
      if (!collides(t, &P2)) P1.x = t.x;
    }
    p1MoveAt = now + 90;
  }
  // rotar / caer / salir
  bool a = isMorsePressed(), b = isFinishPressed();
  static bool pa = false, pb = false;
  if (a && !pa && P1.alive) {                       // rotar
    Piece t = P1; t.rot = (P1.rot + 1) % 4;
    if (!collides(t, &P2)) P1.rot = t.rot;
    else { t.x = P1.x - 1; if (!collides(t, &P2)) { P1.rot = t.rot; P1.x = t.x; } else { t.x = P1.x + 1; if (!collides(t, &P2)) { P1.rot = t.rot; P1.x = t.x; } } }
  }
  if (b && !pb) bHoldStart = now;                   // empieza pulsacion B
  if (!b && pb) {                                   // soltar B
    if (now - bHoldStart < 500 && P1.alive) {       // toque corto -> caida rapida
      dropTo(P1, &P2);
      p1FallAt = 0;
    }
  }
  if (b && (now - bHoldStart > 800)) return 1;      // mantener B -> salir
  pa = a; pb = b;
  return 0;
}

void startTetrisCoop() {
  randomSeed(analogRead(A0) ^ micros());

  int mode = modeSelect();
  if (mode < 0) { mainState = STATE_IDLE; Display_clear(); return; }
  p2isBot = (mode == 0);   // (el modo 2 jugadores por LoRa se anade despues)

  while (true) {
    resetBoard();
    bool over = false;
    if (!spawnPiece(P1, 1, NULL) || !spawnPiece(P2, 5, &P1)) over = true;
    botPlanned = false;
    unsigned long now = millis();
    p1FallAt = p2FallAt = now + gravMs;
    p1MoveAt = botActAt = now;
    // soltar botones de entrada
    while (isMorsePressed() || isFinishPressed()) { backgroundTick(); delay(10); }

    while (!over) {
      backgroundTick();
      now = millis();

      if (playerControl(now) == 1) { mainState = STATE_IDLE; Display_clear(); return; }

      // jugador 2 (bot)
      if (p2isBot && P2.alive && now >= botActAt) {
        botStep();
        botActAt = now + 110;
      }

      // gravedad
      if (P1.alive && now >= p1FallAt) { gravityStep(P1, &P2, over, 1, false); p1FallAt = now + gravMs; }
      if (P2.alive && now >= p2FallAt) { gravityStep(P2, &P1, over, 5, true);  p2FallAt = now + gravMs; }

      drawGame();
      delay(25);
    }

    if (!gameOverScreen()) { mainState = STATE_IDLE; Display_clear(); return; }
  }
}
