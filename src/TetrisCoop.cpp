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
#define CELL 7         // px por celda
#define BX 3           // origen x del tablero (px)
#define BY 1           // origen y del tablero (px)

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
static unsigned long p1FallAt, p2FallAt, p1MoveAt, botActAt, bot1ActAt;

// ---- bot (CPU) ----
struct Bot { int tx, tr; bool planned; unsigned long dropAt; };
static Bot  botB;                 // controla la pieza P2 (la CPU)
static Bot  botA;                 // P1 jugada por CPU (solo para validar con 2 CPUs)
static bool twoBots = false;      // si true, P1 tambien la juega una CPU

// ---- anti-atasco (tablero bloqueado) ----
static unsigned long lastProgress;
static int prevP1y, prevP2y, prevLines;

// ---- 2 jugadores por LoRa ----
static int  remoteTargetX, remoteRot, remoteDrop;   // ultimo input recibido del companero
static int  appliedRot, appliedDrop;
static unsigned long lastBcast, lastInput;

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
static bool gravityStep(Piece &pc, Piece *other, bool &over, int spawnX) {
  Piece t = pc; t.y++;
  if (!collides(t, other)) { pc.y = t.y; return false; }   // hueco libre: baja
  if (collides(t, NULL)) {                                  // pila/suelo: fijar
    lockPiece(pc);
    applyClearAndScore(clearLines());
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

// colision contra un tablero dado (sin la otra pieza)
static bool collidesB(const Piece &pc, uint8_t b[BH][BW]) {
  for (int i = 0; i < 4; i++) {
    int cx = pc.x + PIECES[pc.type][pc.rot][i][0];
    int cy = pc.y + PIECES[pc.type][pc.rot][i][1];
    if (cx < 0 || cx >= BW || cy >= BH) return true;
    if (cy >= 0 && b[cy][cx]) return true;
  }
  return false;
}
static void stampPiece(const Piece &pc, uint8_t b[BH][BW]) {
  for (int i = 0; i < 4; i++) {
    int cx = pc.x + PIECES[pc.type][pc.rot][i][0];
    int cy = pc.y + PIECES[pc.type][pc.rot][i][1];
    if (cy >= 0 && cy < BH && cx >= 0 && cx < BW) b[cy][cx] = 1;
  }
}

static void botPlan(const Piece &me, const Piece *partner, Bot &b) {
  // Tablero base = pila + donde caeria la pieza del companero. Asi la CPU COOPERA:
  // completa filas contando con la otra jugada, en vez de construir por su cuenta.
  uint8_t base[BH][BW];
  memcpy(base, board, sizeof(board));
  if (partner && partner->alive) {
    Piece p = *partner, d = *partner; d.y++;
    while (!collidesB(d, base)) { p.y = d.y; d.y++; }
    stampPiece(p, base);
  }

  float best = -1e9f;
  b.tr = me.rot; b.tx = me.x;
  for (int rot = 0; rot < 4; rot++) {
    for (int x = -2; x < BW; x++) {
      Piece t; t.type = me.type; t.rot = rot; t.x = x; t.y = 0; t.alive = true;
      if (collidesB(t, base)) continue;
      Piece d = t; d.y++;
      while (!collidesB(d, base)) { t.y = d.y; d.y++; }
      if (t.y < 0) continue;
      uint8_t tmp[BH][BW];
      memcpy(tmp, base, sizeof(base));
      stampPiece(t, tmp);
      int cl = clearLinesIn(tmp);
      float s = evalBoard(tmp, cl);
      if (s > best) { best = s; b.tr = rot; b.tx = t.x; }
    }
  }
  b.planned = true;
}

// Un paso del bot: rota o se desplaza una celda hacia el objetivo; si ya esta
// alineado (o atascado por la otra pieza) suelta de golpe y bloquea.
// El bot coloca arriba RAPIDO (limpio, antes de que la gravedad lo arrastre) pero
// baja DESPACIO (tranquilo). Asi no se atasca ni se ve frenetico.
static void botStep(Piece &me, Piece *partner, Bot &b, unsigned long &fallAt) {
  if (!b.planned) { botPlan(me, partner, b); b.dropAt = millis() + 240; }
  if (me.rot != b.tr) {                              // 1. rotar (mientras esta arriba)
    Piece t = me; t.rot = (me.rot + 1) % 4;
    if (!collides(t, partner)) { me.rot = t.rot; return; }
  }
  if (me.x != b.tx) {                                // 2. acercarse a la columna objetivo
    int dir = (b.tx > me.x) ? 1 : -1;
    Piece t = me; t.x += dir;
    if (!collides(t, partner)) { me.x = t.x; return; }
    Piece d = me; d.y++;                              // la otra pieza estorba -> esquivar por debajo
    if (!collides(d, partner)) { me.y = d.y; return; }
  }
  // 3. ya colocado: bajar una celda a ritmo tranquilo; al apoyarse en pila/suelo, fijar
  if (millis() >= b.dropAt) {
    Piece t = me; t.y++;
    if (!collides(t, partner)) me.y = t.y;
    else if (collides(t, NULL)) fallAt = 0;
    b.dropAt = millis() + 220;
  }
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
  // HUD derecha (compacto)
  int hx = BX + BW * CELL + 4;
  display.setTextSize(1); display.setTextColor(SH110X_WHITE);
  display.setCursor(hx, 8);   display.print("PTS");
  display.setCursor(hx, 18);  display.print(score);
  display.setCursor(hx, 36);  display.print("LIN");
  display.setCursor(hx, 46);  display.print(lines);
  display.setCursor(hx, 64);  display.print(p2isBot ? "CPU" : "COOP");
  // leyenda piezas
  display.fillRect(hx, 86, 5, 5, SH110X_WHITE);  display.setCursor(hx + 8, 86); display.print("Tu");
  display.drawRect(hx, 98, 5, 5, SH110X_WHITE);  display.setCursor(hx + 8, 98); display.print(p2isBot ? "CPU" : "P2");
  display.display();
}

// ============================================================
//  Red (2 jugadores por LoRa). Los paquetes de juego empiezan por 'T'. backgroundTick() atiende la radio
//  (los mensajes de chat, ACK y balizas siguen funcionando durante la partida) y deja aqui los paquetes de
//  juego, que se recogen con Game_nextPacket().
// ============================================================
static void resetBoard();
static bool gameOverScreen();

static const char *HX = "0123456789abcdef";
static void putB(String &s, int v) { s += HX[(v >> 4) & 0xF]; s += HX[v & 0xF]; }
static int  hxv(char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; return 0; }
static int  getB(const String &s, int &i) { int v = hxv(s[i]) * 16 + hxv(s[i + 1]); i += 2; return v; }

static void tcSend(const String &m) { Lora_send(m); }
static String tcRecv() {
  backgroundTick();                                  // chat, ACK, balizas y reintentos tambien durante la partida
  String m;
  while (Game_nextPacket(m)) if (m.length() && m[0] == 'T') return m;
  return "";
}

// Estado completo host->cliente
static String encodeState(bool over) {
  String s = "TS";
  uint8_t by[23]; memset(by, 0, sizeof(by));
  int bit = 0;
  for (int y = 0; y < BH; y++) for (int x = 0; x < BW; x++) { if (board[y][x]) by[bit >> 3] |= (1 << (bit & 7)); bit++; }
  for (int i = 0; i < 23; i++) putB(s, by[i]);
  putB(s, P1.type); putB(s, P1.rot); putB(s, P1.x + 2); putB(s, P1.y + 2); putB(s, P1.alive ? 1 : 0);
  putB(s, P2.type); putB(s, P2.rot); putB(s, P2.x + 2); putB(s, P2.y + 2); putB(s, P2.alive ? 1 : 0);
  putB(s, (score >> 16) & 0xFF); putB(s, (score >> 8) & 0xFF); putB(s, score & 0xFF); putB(s, lines & 0xFF); putB(s, over ? 1 : 0);
  return s;
}
static void decodeState(const String &s, bool &over) {
  int i = 2;
  uint8_t by[23];
  for (int k = 0; k < 23; k++) by[k] = getB(s, i);
  int bit = 0;
  for (int y = 0; y < BH; y++) for (int x = 0; x < BW; x++) { board[y][x] = (by[bit >> 3] >> (bit & 7)) & 1; bit++; }
  P1.type = getB(s, i); P1.rot = getB(s, i); P1.x = getB(s, i) - 2; P1.y = getB(s, i) - 2; P1.alive = getB(s, i);
  P2.type = getB(s, i); P2.rot = getB(s, i); P2.x = getB(s, i) - 2; P2.y = getB(s, i) - 2; P2.alive = getB(s, i);
  score = ((long)getB(s, i) << 16); score |= ((long)getB(s, i) << 8); score |= getB(s, i); lines = getB(s, i); over = getB(s, i);
}

// Input cliente->host: columna objetivo + contadores de rotacion/caida
static String encodeInput(int tx, int rotC, int dropC) {
  String s = "TI"; putB(s, tx + 2); putB(s, rotC & 0xFF); putB(s, dropC & 0xFF); return s;
}
static void decodeInput(const String &s) {
  int i = 2; remoteTargetX = getB(s, i) - 2; remoteRot = getB(s, i); remoteDrop = getB(s, i);
}

// El host aplica el input del companero a P2 (como hace el jugador con P1).
static void applyRemoteToP2(unsigned long now) {
  static unsigned long mvAt = 0;
  int pend = (remoteRot - appliedRot) & 0xFF;
  for (int r = 0; r < pend && r < 4; r++) { Piece t = P2; t.rot = (P2.rot + 1) % 4; if (!collides(t, &P1)) P2.rot = t.rot; }
  appliedRot = remoteRot;
  if (now >= mvAt && P2.x != remoteTargetX && P2.alive) {
    int dir = remoteTargetX > P2.x ? 1 : -1; Piece t = P2; t.x += dir; if (!collides(t, &P1)) P2.x = t.x;
    mvAt = now + 80;
  }
  if (((remoteDrop - appliedDrop) & 0xFF) && P2.alive) { dropTo(P2, &P1); p2FallAt = 0; appliedDrop = remoteDrop; }
}

// Lobby: el host anuncia, el cliente busca. true = conectados.
static bool lobby(bool host) {
  unsigned long t = 0;
  while (true) {
    unsigned long now = millis();
    display.clearDisplay();
    centerPrint("TETRIS COOP", 20, 1);
    centerPrint(host ? "Esperando" : "Buscando", 54, 1);
    centerPrint(host ? "companero..." : "partida...", 66, 1);
    centerPrint("B: cancelar", 100, 1);
    display.display();
    String m = tcRecv();
    if (host)  { if (m == "TJ") { for (int k = 0; k < 3; k++) tcSend("TS_GO"); return true; } if (now - t > 600) { tcSend("TH"); t = now; } }
    else       { if (m == "TH") { for (int k = 0; k < 4; k++) tcSend("TJ"); return true; } if (now - t > 700) { tcSend("TJ"); t = now; } }
    if (isFinishPressed()) { delay(40); if (isFinishPressed()) return false; }
    delay(25);
  }
}

// Bucle del cliente: no simula; pinta el estado recibido y envia su input.
static void clientGame() {
  resetBoard();
  bool over = false, started = false;
  static bool pa = false, pb = false;
  uint8_t rotC = 0, dropC = 0;
  unsigned long bHold = 0;
  while (isMorsePressed() || isFinishPressed()) { backgroundTick(); delay(10); }
  while (true) {
    unsigned long now = millis();
    String m = tcRecv();
    if (m.startsWith("TS") && m.length() > 10) { decodeState(m, over); started = true; }
    if (m == "TQ") { mainState = STATE_IDLE; Display_clear(); return; }

    int tx = map(getPotValue(BW - 1), 0, BW - 1, 0, BW - 4);
    bool a = isMorsePressed(), b = isFinishPressed();
    if (a && !pa) rotC++;
    if (b && !pb) bHold = now;
    if (!b && pb && now - bHold < 500) dropC++;
    if (b && now - bHold > 800) { tcSend("TQ"); mainState = STATE_IDLE; Display_clear(); return; }
    pa = a; pb = b;
    if (now - lastInput > 200) { tcSend(encodeInput(tx, rotC, dropC)); lastInput = now; }

    if (started) { drawGame(); if (over) { if (!gameOverScreen()) { mainState = STATE_IDLE; Display_clear(); return; } resetBoard(); over = false; started = false; } }
    else { display.clearDisplay(); centerPrint("Conectando...", 56, 1); display.display(); }
    delay(25);
  }
}

// ============================================================
//  Pantallas
// ============================================================
static int modeSelect() {     // 0=solo, 1=crear(host), 2=unirse(cliente), -1=salir
  while (true) {
    int sel = getPotValue(2);
    display.clearDisplay();
    centerPrint("TETRIS COOP", 8, 1);
    centerPrint(((sel == 0) ? "> " : "  ") + String("Jugar con CPU"), 44, 1);
    centerPrint(((sel == 1) ? "> " : "  ") + String("2 Jug: Crear"), 58, 1);
    centerPrint(((sel == 2) ? "> " : "  ") + String("2 Jug: Unirse"), 72, 1);
    centerPrint("Pote: elegir", 96, 1);
    centerPrint("A: ok  B: salir", 112, 1);
    display.display();
    char k = pollButton();
    if (k == 'A') return sel;
    if (k == 'B') return -1;
    backgroundTick();
    delay(20);
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
  Game_dropPackets();                                // paquetes de juego que quedaran de antes

  int mode = modeSelect();
  if (mode < 0) { mainState = STATE_IDLE; Display_clear(); return; }

  if (mode == 2) {                                   // unirse: cliente
    if (lobby(false)) clientGame();
    mainState = STATE_IDLE; Display_clear(); return;
  }
  if (mode == 1 && !lobby(true)) { mainState = STATE_IDLE; Display_clear(); return; }
  p2isBot = (mode == 0);
  bool host = (mode == 1);

  while (true) {
    resetBoard();
    if (host) gravMs = 1100;                          // mas lento: la radio tiene retardo
    bool over = false;
    if (!spawnPiece(P1, 1, NULL) || !spawnPiece(P2, 5, &P1)) over = true;
    botA.planned = botB.planned = false;
    appliedRot = appliedDrop = remoteRot = remoteDrop = 0; remoteTargetX = P2.x;
    unsigned long now = millis();
    p1FallAt = p2FallAt = now + gravMs;
    p1MoveAt = botActAt = bot1ActAt = now; lastBcast = lastInput = now;
    lastProgress = now; prevP1y = P1.y; prevP2y = P2.y; prevLines = lines;
    while (isMorsePressed() || isFinishPressed()) { backgroundTick(); delay(10); }

    while (!over) {
      if (host) { String m = tcRecv(); if (m.startsWith("TI")) decodeInput(m); }   // (tcRecv incluye backgroundTick)
      else backgroundTick();
      now = millis();

      if (twoBots) { if (P1.alive && now >= bot1ActAt) { botStep(P1, &P2, botA, p1FallAt); bot1ActAt = now + 90; } }
      else if (playerControl(now) == 1) { if (host) tcSend("TQ"); mainState = STATE_IDLE; Display_clear(); return; }

      if (p2isBot && P2.alive && now >= botActAt) { botStep(P2, &P1, botB, p2FallAt); botActAt = now + 90; }
      if (host) applyRemoteToP2(now);

      if (P1.alive && now >= p1FallAt) { if (gravityStep(P1, &P2, over, 1)) botA.planned = false; p1FallAt = now + gravMs; }
      if (P2.alive && now >= p2FallAt) { if (gravityStep(P2, &P1, over, 5)) botB.planned = false; p2FallAt = now + gravMs; }

      if (host && now - lastBcast > 280) { tcSend(encodeState(over)); lastBcast = now; }

      // anti-atasco: si nada se mueve durante un rato, el tablero esta bloqueado
      if (lines != prevLines || P1.y != prevP1y || P2.y != prevP2y) {
        prevLines = lines; prevP1y = P1.y; prevP2y = P2.y; lastProgress = now;
      } else if (now - lastProgress > 2200) { over = true; }

      drawGame();
      delay(25);
    }

    if (host) tcSend(encodeState(true));
    if (!gameOverScreen()) { if (host) tcSend("TQ"); mainState = STATE_IDLE; Display_clear(); return; }
  }
}
