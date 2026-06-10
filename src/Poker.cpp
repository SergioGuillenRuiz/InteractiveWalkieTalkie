#include <Arduino.h>
#include "Display.h"
#include "Inputs.h"
#include "States.h"

// ============================================================
//  POKER  (Texas Hold'em heads-up: jugador vs CPU)
//  Controles en la mesa:  B = cambiar accion,  A = confirmar.
// ============================================================

#define START_CHIPS 1000
#define SMALL_BLIND 10
#define BIG_BLIND   20

// Ritmo (ms): bajos = mas agil
#define D_THINK  380    // "CPU pensando"
#define D_ACT    520    // mostrar una accion
#define D_STREET 430    // nombre de la calle (FLOP, TURN...)

enum Suit { HEARTS, DIAMONDS, CLUBS, SPADES };
enum Rank { TWO = 2, THREE, FOUR, FIVE, SIX, SEVEN, EIGHT, NINE, TEN, JACK, QUEEN, KING, ACE };
enum Action { ACT_CHECK_CALL, ACT_BET_RAISE, ACT_FOLD };

struct Card { Suit suit; Rank rank; };
struct Player { Card hand[2]; int chips; int bet; bool folded; bool isDealer; };

static Card   deck[52];
static Card   community[5];
static Player player, cpu;
static int    pot, currentBet, deckIndex, communityCount;
static int    cpuRaises;
static Action selectedAction = ACT_CHECK_CALL;
static String msgLine = "";

long evaluateScore(Card hand[2], Card comm[5], int commCount);   // fwd

// ============================================================
//  Entrada (flanco) y texto centrado
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

static void centerAt(const String &s, int y, uint8_t size = 1) {
  display.setTextSize(size);
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(s.c_str(), 0, 0, &bx, &by, &bw, &bh);
  int x = (128 - (int)bw) / 2; if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(s);
  display.setTextSize(1);
}

// ============================================================
//  Nombres de carta / jugada
// ============================================================
static String rankName(int r) {
  if (r <= 10) return String(r);
  if (r == 11) return "J";
  if (r == 12) return "Q";
  if (r == 13) return "K";
  return "A";
}

static const char *categoryName(long score) {     // en mayusculas para el rotulo grande
  switch ((int)(score / 1048576L)) {
    case 8: return "ESC.COLOR"; case 7: return "POKER";   case 6: return "FULL";
    case 5: return "COLOR";     case 4: return "ESCALERA"; case 3: return "TRIO";
    case 2: return "DOBLE PAR"; case 1: return "PAREJA";   default: return "CARTA ALTA";
  }
}

static String handDesc(long score) {               // descripcion para el showdown
  int cat = (int)(score / 1048576L);
  int a   = (int)((score / 65536L) % 16);
  switch (cat) {
    case 8: return "Esc. de color";
    case 7: return String("Poker de ") + rankName(a);
    case 6: return "Full house";
    case 5: return "Color";
    case 4: return "Escalera";
    case 3: return String("Trio de ") + rankName(a);
    case 2: return "Doble pareja";
    case 1: return String("Pareja de ") + rankName(a);
    default: return rankName(a) + String(" alto");
  }
}

// ============================================================
//  Evaluacion de manos (kickers, rueda A-2-3-4-5, escalera de color)
// ============================================================
static int straightHigh(const bool *pres) {
  for (int high = 14; high >= 5; high--) {
    bool ok = true;
    for (int k = 0; k < 5; k++) { int r = high - k; if (r == 1) r = 14; if (!pres[r]) { ok = false; break; } }
    if (ok) return high;
  }
  return 0;
}
static long packScore(int cat, int a, int b, int c, int d, int e) {
  return (((((long)cat * 16 + a) * 16 + b) * 16 + c) * 16 + d) * 16 + e;
}
static void topKickers(const int *rc, int ex1, int ex2, int want, int *out) {
  int idx = 0;
  for (int r = 14; r >= 2 && idx < want; r--) {
    if (r == ex1 || r == ex2) continue;
    int c = rc[r];
    while (c-- > 0 && idx < want) out[idx++] = r;
  }
  while (idx < want) out[idx++] = 0;
}
long evaluateScore(Card hand[2], Card comm[5], int commCount) {
  int n = 2 + commCount;
  int  rc[15] = {0}, sc[4] = {0};
  bool pres[15]; bool sp[4][15];
  for (int i = 0; i < 15; i++) pres[i] = false;
  for (int s = 0; s < 4; s++) for (int i = 0; i < 15; i++) sp[s][i] = false;
  for (int i = 0; i < n; i++) {
    Card c = (i < 2) ? hand[i] : comm[i - 2];
    rc[c.rank]++; sc[c.suit]++; sp[c.suit][c.rank] = true; pres[c.rank] = true;
  }
  int flushSuit = -1;
  for (int s = 0; s < 4; s++) if (sc[s] >= 5) { flushSuit = s; break; }
  int sfHigh = (flushSuit >= 0) ? straightHigh(sp[flushSuit]) : 0;
  int stHigh = straightHigh(pres);
  int quad = 0, trips = 0, trips2 = 0, pair1 = 0, pair2 = 0;
  for (int r = 14; r >= 2; r--) {
    if (rc[r] == 4) quad = r;
    else if (rc[r] == 3) { if (!trips) trips = r; else if (!trips2) trips2 = r; }
    else if (rc[r] == 2) { if (!pair1) pair1 = r; else if (!pair2) pair2 = r; }
  }
  int k[5];
  if (sfHigh) return packScore(8, sfHigh, 0, 0, 0, 0);
  if (quad)   { topKickers(rc, quad, -1, 1, k); return packScore(7, quad, k[0], 0, 0, 0); }
  if (trips && (pair1 || trips2)) { int bp = (trips2 > pair1) ? trips2 : pair1; return packScore(6, trips, bp, 0, 0, 0); }
  if (flushSuit >= 0) {
    int f[5], fi = 0;
    for (int r = 14; r >= 2 && fi < 5; r--) if (sp[flushSuit][r]) f[fi++] = r;
    while (fi < 5) f[fi++] = 0;
    return packScore(5, f[0], f[1], f[2], f[3], f[4]);
  }
  if (stHigh) return packScore(4, stHigh, 0, 0, 0, 0);
  if (trips)  { topKickers(rc, trips, -1, 2, k); return packScore(3, trips, k[0], k[1], 0, 0); }
  if (pair1 && pair2) { topKickers(rc, pair1, pair2, 1, k); return packScore(2, pair1, pair2, k[0], 0, 0); }
  if (pair1)  { topKickers(rc, pair1, -1, 3, k); return packScore(1, pair1, k[0], k[1], k[2], 0); }
  topKickers(rc, -1, -1, 5, k);
  return packScore(0, k[0], k[1], k[2], k[3], k[4]);
}

// ============================================================
//  Dibujo
// ============================================================
static void drawSuit(int x, int y, Suit suit, uint16_t color) {
  switch (suit) {
    case HEARTS:
      display.fillCircle(x - 2, y - 2, 2, color); display.fillCircle(x + 2, y - 2, 2, color);
      display.fillTriangle(x - 4, y - 1, x + 4, y - 1, x, y + 4, color); break;
    case DIAMONDS:
      display.fillTriangle(x, y - 4, x - 3, y, x, y + 4, color);
      display.fillTriangle(x, y - 4, x + 3, y, x, y + 4, color); break;
    case CLUBS:
      display.fillCircle(x, y - 3, 2, color); display.fillCircle(x - 2, y, 2, color);
      display.fillCircle(x + 2, y, 2, color); display.drawLine(x, y, x, y + 4, color); break;
    case SPADES:
      display.fillTriangle(x, y - 4, x - 3, y + 1, x + 3, y + 1, color);
      display.fillCircle(x - 2, y + 1, 2, color); display.fillCircle(x + 2, y + 1, 2, color);
      display.drawLine(x, y, x, y + 4, color); break;
  }
}
static String rankStr(Rank r) {
  if (r <= 10) return String((int)r);
  if (r == JACK) return "J"; if (r == QUEEN) return "Q"; if (r == KING) return "K"; return "A";
}
static void drawCard(int x, int y, Card c, bool visible) {
  display.fillRoundRect(x, y, 20, 28, 3, SH110X_WHITE);
  if (!visible) {
    display.fillRoundRect(x + 2, y + 2, 16, 24, 2, SH110X_BLACK);
    display.setTextColor(SH110X_WHITE); display.setTextSize(1);
    display.setCursor(x + 7, y + 10); display.print("?");
  } else {
    display.setTextColor(SH110X_BLACK); display.setTextSize(1);
    display.setCursor(x + 2, y + 2); display.print(rankStr(c.rank));
    drawSuit(x + 10, y + 18, c.suit, SH110X_BLACK);
  }
}
// Distintivo de repartidor: una "D" en blanco invertido (bien visible).
static void drawDealerBadge(int x, int y) {
  display.fillRect(x, y, 9, 9, SH110X_WHITE);
  display.setTextColor(SH110X_BLACK);
  display.setTextSize(1);
  display.setCursor(x + 2, y + 1);
  display.print("D");
  display.setTextColor(SH110X_WHITE);
}

// reveal=false durante la partida (muestra TU jugada arriba),
// reveal=true en el showdown (revela las cartas de la CPU).
static void drawTable(bool reveal) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Cabecera: fichas de cada uno + distintivo de repartidor
  display.setCursor(0, 0);
  display.print("CPU:");
  display.print(cpu.chips);
  if (cpu.isDealer) drawDealerBadge(display.getCursorX() + 2, 0);
  {
    String t = String("TU:") + String(player.chips);
    int16_t x1, y1; uint16_t w, h; display.getTextBounds(t.c_str(), 0, 0, &x1, &y1, &w, &h);
    int extra = player.isDealer ? 11 : 0;
    int x = 128 - (int)w - extra; if (x < 0) x = 0;
    display.setCursor(x, 0); display.print(t);
    if (player.isDealer) drawDealerBadge(x + (int)w + 2, 0);
  }

  // Zona superior: cartas de la CPU (solo en showdown) o TU jugada actual
  if (reveal) {
    drawCard(40, 12, cpu.hand[0], true);
    drawCard(64, 12, cpu.hand[1], true);
  } else {
    long sc = evaluateScore(player.hand, community, communityCount);
    centerAt("TU JUGADA", 12, 1);
    centerAt(categoryName(sc), 22, 2);
  }

  // Banda central: mensaje flotante o el bote
  centerAt(msgLine.length() ? msgLine : (String("BOTE: ") + String(pot)), 42, 1);

  // Mesa (cartas comunitarias)
  for (int i = 0; i < communityCount; i++) drawCard(10 + i * 22, 50, community[i], true);

  // Tus cartas
  drawCard(40, 84, player.hand[0], true);
  drawCard(64, 84, player.hand[1], true);
}

static void flashMessage(const String &m, unsigned long ms) {
  msgLine = m;
  drawTable(false);
  display.display();
  unsigned long t = millis();
  while (millis() - t < ms) { backgroundTick(); delay(10); }
  msgLine = "";
}

// ============================================================
//  Baraja
// ============================================================
static void initDeck() {
  int idx = 0;
  for (int s = 0; s < 4; s++)
    for (int r = 2; r <= 14; r++) { deck[idx].suit = (Suit)s; deck[idx].rank = (Rank)r; idx++; }
  for (int i = 51; i > 0; i--) { int j = random(i + 1); Card t = deck[i]; deck[i] = deck[j]; deck[j] = t; }
  deckIndex = 0;
}
static Card dealOne() { return deck[deckIndex++]; }
static void dealHole() {
  player.hand[0] = dealOne(); cpu.hand[0] = dealOne();
  player.hand[1] = dealOne(); cpu.hand[1] = dealOne();
}
static void dealCommunity(int cnt) { for (int i = 0; i < cnt; i++) community[communityCount++] = dealOne(); }
static int betSize() { int s = ((pot / 2 + 5) / 10) * 10; return (s < BIG_BLIND) ? BIG_BLIND : s; }

// ============================================================
//  Acciones (barra con opciones resaltadas: B mueve, A confirma)
// ============================================================
static void drawActionBar(int call, bool canRaise, int raiseSize) {
  display.fillRect(0, 114, 128, 14, SH110X_BLACK);
  display.drawLine(0, 113, 127, 113, SH110X_WHITE);
  display.setTextSize(1);

  String lbl[3];
  lbl[0] = (call > 0) ? (String("VEO ") + String(call)) : String("PASO");
  lbl[1] = String("SUBO ") + String(raiseSize);
  lbl[2] = "TIRO";
  const int xs[3] = {3, 49, 99};

  for (int i = 0; i < 3; i++) {
    if (i == 1 && !canRaise) continue;
    int16_t bx, by; uint16_t bw, bh;
    display.getTextBounds(lbl[i].c_str(), 0, 0, &bx, &by, &bw, &bh);
    if ((int)selectedAction == i) {                  // opcion seleccionada -> resaltada
      display.fillRect(xs[i] - 2, 116, (int)bw + 3, 11, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }
    display.setCursor(xs[i], 118);
    display.print(lbl[i]);
  }
  display.setTextColor(SH110X_WHITE);
}

static int getHumanAction(int call, bool canRaise, int raiseSize) {
  if (selectedAction == ACT_BET_RAISE && !canRaise) selectedAction = ACT_CHECK_CALL;
  drawTable(false);
  drawActionBar(call, canRaise, raiseSize);
  display.display();
  while (true) {
    char kk = pollButton();
    if (kk == 'B') {
      do { selectedAction = (Action)((selectedAction + 1) % 3); }
      while (selectedAction == ACT_BET_RAISE && !canRaise);
      drawActionBar(call, canRaise, raiseSize);
      display.display();
    } else if (kk == 'A') {
      return selectedAction;
    }
    backgroundTick();
    delay(10);
  }
}

static int cpuDecide(int call, bool canRaise, int raiseSize) {
  (void)raiseSize;
  flashMessage("CPU pensando", D_THINK);
  long score = evaluateScore(cpu.hand, community, communityCount);
  int conf;
  if (score >= packScore(2, 0, 0, 0, 0, 0))       conf = 70 + random(25);
  else if (score >= packScore(1, 11, 0, 0, 0, 0)) conf = 48 + random(20);
  else if (score >= packScore(1, 0, 0, 0, 0, 0))  conf = 30 + random(18);
  else                                            conf = random(20);
  if (communityCount == 0) {
    int hi = max(cpu.hand[0].rank, cpu.hand[1].rank);
    if (hi >= 12) conf += 15;
    if (cpu.hand[0].suit == cpu.hand[1].suit) conf += 8;
    if (cpu.hand[0].rank == cpu.hand[1].rank)  conf = 85 + random(10);
  }
  if (conf < 40 && random(100) < 15) conf = 70 + random(15);
  conf = constrain(conf, 0, 95);
  if (call == 0) {
    if (conf > 72 && canRaise) return ACT_BET_RAISE;
    return ACT_CHECK_CALL;
  }
  if (conf > 82 && canRaise && cpuRaises < 2) { cpuRaises++; return ACT_BET_RAISE; }
  if (conf > 42 || (conf > 28 && call <= BIG_BLIND)) return ACT_CHECK_CALL;
  return ACT_FOLD;
}

// ============================================================
//  Ronda de apuestas
// ============================================================
static bool bettingRound(bool preflop) {
  selectedAction = ACT_CHECK_CALL;
  if (player.chips == 0 || cpu.chips == 0) return true;

  bool humanTurn = preflop ? player.isDealer : !player.isDealer;
  int toAct = 2;

  while (toAct > 0) {
    Player &a = humanTurn ? player : cpu;
    Player &b = humanTurn ? cpu : player;
    if (a.chips == 0) { toAct--; humanTurn = !humanTurn; continue; }

    int  call = currentBet - a.bet;
    bool canRaise = (a.chips > call);
    int  rs = betSize();
    int  action = humanTurn ? getHumanAction(call, canRaise, rs)
                            : cpuDecide(call, canRaise, rs);
    String who = humanTurn ? "TU " : "CPU ";

    if (action == ACT_FOLD) { a.folded = true; flashMessage(who + "se retira", D_ACT + 150); return false; }
    if (action == ACT_BET_RAISE && canRaise) {
      int need = call + rs;
      int pay = (need < a.chips) ? need : a.chips;
      a.chips -= pay; a.bet += pay; pot += pay; currentBet = a.bet;
      flashMessage(who + (call > 0 ? "sube +" : "apuesta ") + String(pay), D_ACT);
      toAct = 1;
    } else if (call <= 0) {
      flashMessage(who + "pasa", D_ACT - 120);
      toAct--;
    } else {
      int pay = (call < a.chips) ? call : a.chips;
      a.chips -= pay; a.bet += pay; pot += pay;
      if (a.chips == 0 && a.bet < currentBet) {
        int ex = currentBet - a.bet; b.chips += ex; b.bet -= ex; pot -= ex; currentBet = a.bet;
        flashMessage(who + "ALL-IN", D_ACT + 250); toAct = 0;
      } else {
        flashMessage(who + "iguala", D_ACT - 120); toAct--;
      }
    }
    humanTurn = !humanTurn;
  }
  return true;
}

// ============================================================
//  Reparto / fin de mano
// ============================================================
static void resetBets() { currentBet = 0; player.bet = 0; cpu.bet = 0; }

static void resetRound(bool playerDealer) {
  initDeck();
  communityCount = 0; pot = 0; currentBet = 0; cpuRaises = 0;
  player.folded = cpu.folded = false; player.bet = 0; cpu.bet = 0;
  player.isDealer = playerDealer; cpu.isDealer = !playerDealer;
  dealHole();
  Player &sb = playerDealer ? player : cpu;
  Player &bb = playerDealer ? cpu : player;
  int sbAmt = (SMALL_BLIND < sb.chips) ? SMALL_BLIND : sb.chips;
  int bbAmt = (BIG_BLIND   < bb.chips) ? BIG_BLIND   : bb.chips;
  sb.chips -= sbAmt; sb.bet = sbAmt;
  bb.chips -= bbAmt; bb.bet = bbAmt;
  pot = sbAmt + bbAmt;
  currentBet = (bbAmt > sbAmt) ? bbAmt : sbAmt;
}

static void waitContinue() {
  while (true) {
    char kk = pollButton();
    if (kk == 'A') return;
    if (kk == 'B') { mainState = STATE_IDLE; Display_clear(); return; }
    backgroundTick();
    delay(10);
  }
}

static void resultBox(const String &res, const String &l1, const String &l2) {
  display.fillRect(4, 42, 120, 44, SH110X_BLACK);
  display.drawRect(4, 42, 120, 44, SH110X_WHITE);
  display.setTextColor(SH110X_WHITE);
  centerAt(res, 44, 2);
  centerAt(l1, 62, 1);
  centerAt(l2, 73, 1);
  display.setCursor(8, 118); display.print("A: sigue   B: salir");
  display.display();
}

static void awardByFold() {
  Player &w = player.folded ? cpu : player;
  w.chips += pot;
  drawTable(true);
  if (player.folded) resultBox("CPU GANA", "Te has retirado", String("Gana ") + String(pot));
  else               resultBox("GANAS",    "CPU se retira",   String("Ganas ") + String(pot));
  pot = 0;
  waitContinue();
}

static void showdown() {
  long ps = evaluateScore(player.hand, community, 5);
  long cs = evaluateScore(cpu.hand, community, 5);
  drawTable(true);
  String res;
  if (ps > cs)      { res = "GANAS";    player.chips += pot; }
  else if (cs > ps) { res = "CPU GANA"; cpu.chips += pot; }
  else              { res = "EMPATE";   int half = pot / 2; player.chips += pot - half; cpu.chips += half; }
  pot = 0;
  resultBox(res, String("TU: ") + handDesc(ps), String("CPU: ") + handDesc(cs));
  waitContinue();
}

static void playRound(bool playerDealer) {
  resetRound(playerDealer);
  flashMessage("PRE-FLOP", D_STREET);
  if (!bettingRound(true)) { awardByFold(); return; }
  bool allIn = (player.chips == 0 || cpu.chips == 0);

  dealCommunity(3); resetBets(); flashMessage("FLOP", D_STREET);
  if (!allIn) { if (!bettingRound(false)) { awardByFold(); return; } allIn = (player.chips == 0 || cpu.chips == 0); }

  dealCommunity(1); resetBets(); flashMessage("TURN", D_STREET);
  if (!allIn) { if (!bettingRound(false)) { awardByFold(); return; } allIn = (player.chips == 0 || cpu.chips == 0); }

  dealCommunity(1); resetBets(); flashMessage("RIVER", D_STREET);
  if (!allIn) { if (!bettingRound(false)) { awardByFold(); return; } }

  showdown();
}

// ============================================================
//  Entrada
// ============================================================
void startPoker() {
  randomSeed(analogRead(A0) + millis());

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  centerAt("POKER", 16, 2);
  centerAt("Heads-up vs CPU", 42, 1);
  centerAt("A: Jugar", 64, 1);
  centerAt("B: Salir", 78, 1);
  centerAt("En la mesa:", 102, 1);
  centerAt("B cambia / A confirma", 114, 1);
  display.display();
  while (true) {
    char kk = pollButton();
    if (kk == 'A') break;
    if (kk == 'B') { mainState = STATE_IDLE; Display_clear(); return; }
    backgroundTick(); delay(10);
  }

  player.chips = START_CHIPS;
  cpu.chips = START_CHIPS;
  bool playerDealer = true;

  while (mainState != STATE_IDLE) {
    playRound(playerDealer);
    if (mainState == STATE_IDLE) return;

    Serial.print("[Poker] total: ");
    Serial.println(player.chips + cpu.chips);

    if (player.chips <= 0 || cpu.chips <= 0) {
      display.clearDisplay();
      display.setTextColor(SH110X_WHITE);
      centerAt(player.chips > 0 ? "GANASTE" : "BANCARROTA", 36, 2);
      centerAt("A: Otra   B: Salir", 80, 1);
      display.display();
      while (true) {
        char kk = pollButton();
        if (kk == 'A') { player.chips = START_CHIPS; cpu.chips = START_CHIPS; playerDealer = true; break; }
        if (kk == 'B') { mainState = STATE_IDLE; Display_clear(); return; }
        backgroundTick(); delay(10);
      }
      continue;
    }
    playerDealer = !playerDealer;
  }
}
