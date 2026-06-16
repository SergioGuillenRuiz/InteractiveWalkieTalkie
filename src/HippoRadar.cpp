#include <Arduino.h>
#include <math.h>
#include <LoRa.h>
#include "Display.h"
#include "Inputs.h"
#include "States.h"
#include "MyLora.h"
#include "Historial.h"
#include "Identity.h"
#include "HippoRadar.h"

// ============================================================
//  HIPPO RADAR  -  utilidad para localizar al otro equipo
//
//  No es un juego: usa SOLO el hardware existente (radio LoRa + pantalla +
//  botones) para guiar al usuario hasta el otro equipo (se asume otro par
//  identico cerca). El aparato MANDA: el usuario solo sigue lo que sale en
//  pantalla, no elige modos ni calibra nada.
//
//  Flujo guiado (3 pantallas, sin decisiones):
//   1) BUSCANDO  -> espera a oir al otro equipo por radio.
//   2) GIRA      -> "gira despacio". Mientras giras, la senal sube y baja
//                   (el cuerpo/antena hacen de patron direccional). Palabra
//                   gigante FRIO/TIBIO/CALIENTE + barra. Cuando te paras en el
//                   punto mas fuerte, se bloquea solo: "AQUI!".
//   3) VE AQUI   -> flecha grande hacia donde estas mirando (= hacia el otro)
//                   y los metros en grande. "te acercas / te alejas" al andar.
//
//  Sin brujula ni giroscopio: la direccion no es absoluta, es "parate donde
//  la senal es mas fuerte". La distancia sale del RSSI (modelo log-distancia).
//
//  Controles:
//   A -> volver a buscar direccion (en la pantalla final)
//   B -> salir
//  Sin acentos ni 'n~': la fuente del OLED no los representa.
// ============================================================

// Modelo log-distancia:  d = 10^((refRssi - rssi) / (10 * PLE))
static const float refRssi = -45.0f;     // RSSI tipico a ~1 m (modo largo alcance)
static const float PLE     = 2.8f;       // exponente de perdidas (entorno mixto)
static const float D2R     = 0.017453293f;

enum Phase { PH_SEARCH, PH_TURN, PH_LOCKED };
static Phase   phase;

static uint8_t ownId;
static float   rssiEMA;                  // RSSI suavizado
static bool    haveLink;
static unsigned long lastHeard, nextPing;
static float   distM;                    // distancia estimada (m)

// Busqueda de direccion (girando)
static float   maxR, minR;               // extremos de RSSI vistos en esta vuelta
static unsigned long maxAt;              // instante del ultimo maximo (mejora real)
static unsigned long turnStart;          // inicio de la fase de giro
static bool    locking;                  // estas quieto en el punto fuerte
static float   lockProg;                 // 0..1 progreso del bloqueo

// Seguimiento al andar
static float   distRef, distTrend;

// ============================================================
//  Utilidades
// ============================================================
static void centerPrint(const String &s, int y, uint8_t size = 1) {
  display.setTextSize(size);
  display.setTextColor(SH110X_WHITE);
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(s.c_str(), 0, 0, &bx, &by, &bw, &bh);
  int x = (128 - (int)bw) / 2; if (x < 0) x = 0;
  display.setCursor(x, y); display.print(s);
}

// Radio en modo LARGO ALCANCE solo durante el radar (mas sensibilidad y potencia
// = mas distancia). Los dos equipos deben estar en el radar para oirse. Al salir
// se restauran los parametros del chat (mas rapido).
static void radioRangeMode() { LoRa.idle(); LoRa.setSpreadingFactor(10); LoRa.setTxPower(20); }
static void radioRestore()   { LoRa.idle(); LoRa.setSpreadingFactor(LORA_SPREADING); LoRa.setTxPower(LORA_POWER); }

static float estDistance(float rssi) {
  float d = powf(10.0f, (refRssi - rssi) / (10.0f * PLE));
  if (d < 0.3f) d = 0.3f;
  if (d > 300.0f) d = 300.0f;
  return d;
}

// ============================================================
//  Radio
// ============================================================
static void sendPing() {
  String p = "HR";
  p += (char)ownId;
  Lora_send(p);
}

// Lee paquetes: los "HR" del companero actualizan la senal; el resto van al
// historial (para no perder mensajes normales mientras usas el radar).
static void rxTick() {
  while (Lora_hasMessage()) {
    String m = Lora_readMessage();
    if (m.length() >= 2 && m[0] == 'H' && m[1] == 'R') {
      if (m.length() < 3 || (uint8_t)m[2] != ownId) {       // ping del companero
        int r = Lora_lastRssi();
        if (rssiEMA < -190.0f) rssiEMA = (float)r;
        else                   rssiEMA += ((float)r - rssiEMA) * 0.40f;
        lastHeard = millis();
        haveLink = true;
      }
    } else {
      History_addMessage(m);
    }
  }
}

// ============================================================
//  Pantalla 1: BUSCANDO
// ============================================================
static void drawSearch(unsigned long now) {
  display.clearDisplay();
  int cx = 64, cy = 40;
  int base = 8 + (int)((now / 120) % 26);                   // anillo que crece y se reinicia
  display.drawCircle(cx, cy, base, SH110X_WHITE);
  display.drawCircle(cx, cy, base / 2, SH110X_WHITE);
  display.fillCircle(cx, cy, 3, SH110X_WHITE);
  centerPrint("BUSCANDO", 80, 2);
  centerPrint("Acerca los equipos", 106, 1);
  display.display();
}

// ============================================================
//  Pantalla 2: GIRA DESPACIO (frio/caliente -> se bloquea solo)
// ============================================================
static void drawTurn(unsigned long now) {
  display.clearDisplay();
  centerPrint("GIRA DESPACIO", 2, 1);

  // icono de giro: aro con un punto orbitando
  int gx = 64, gy = 26, gr = 11;
  display.drawCircle(gx, gy, gr, SH110X_WHITE);
  float oa = ((now % 1200) / 1200.0f) * 6.2832f;
  display.fillCircle(gx + (int)(gr * cosf(oa)), gy + (int)(gr * sinf(oa)), 3, SH110X_WHITE);

  // senal normalizada entre lo mas flojo y lo mas fuerte visto
  float n = 0.5f;
  if (maxR - minR > 1.0f) { n = (rssiEMA - minR) / (maxR - minR); if (n < 0) n = 0; if (n > 1) n = 1; }

  const char *w = locking ? "AQUI!" : (n < 0.34f ? "FRIO" : (n < 0.7f ? "TIBIO" : "CALIENTE"));
  centerPrint(w, 46, 2);

  // barra de senal (se llena cuanto mas fuerte)
  int bx = 14, bw = 100, by = 80, bh = 12, segs = 12;
  int fill = (int)(n * segs + 0.5f);
  int sw = bw / segs;
  for (int i = 0; i < segs; i++) {
    int sx = bx + i * sw;
    if (i < fill) display.fillRect(sx, by, sw - 2, bh, SH110X_WHITE);
    else          display.drawRect(sx, by, sw - 2, bh, SH110X_WHITE);
  }
  // progreso de bloqueo (mantente quieto en el punto fuerte)
  if (locking) {
    int p = (int)(lockProg * bw); if (p > bw) p = bw; if (p < 0) p = 0;
    display.fillRect(bx, by + bh + 3, p, 3, SH110X_WHITE);
  }

  centerPrint("B salir", 116, 1);
  display.display();
}

// ============================================================
//  Pantalla 3: VE HACIA AQUI (flecha + metros)
// ============================================================
static void drawLocked(unsigned long now) {
  display.clearDisplay();
  centerPrint("VE HACIA AQUI", 2, 1);

  // flecha grande hacia arriba (= hacia donde estas mirando)
  display.fillTriangle(64, 22, 44, 50, 84, 50, SH110X_WHITE);
  display.fillRect(57, 50, 14, 24, SH110X_WHITE);

  char buf[10]; snprintf(buf, sizeof(buf), "%d m", (int)(distM + 0.5f));
  centerPrint(buf, 82, 2);

  if      (distTrend < -0.05f) centerPrint("te acercas", 104, 1);
  else if (distTrend >  0.05f) centerPrint("te alejas", 104, 1);

  centerPrint("A buscar  B salir", 116, 1);
  display.display();
}

// ============================================================
//  Pantalla de inicio
// ============================================================
static bool startScreen() {
  display.clearDisplay();
  centerPrint("HIPPO RADAR", 2, 1);
  display.drawCircle(64, 34, 16, SH110X_WHITE);
  display.drawCircle(64, 34, 8, SH110X_WHITE);
  display.fillCircle(64, 34, 2, SH110X_WHITE);
  display.fillCircle(75, 25, 3, SH110X_WHITE);
  centerPrint("Localiza al otro", 60);
  centerPrint("equipo. Tu solo", 72);
  centerPrint("sigue la pantalla", 84);
  centerPrint("A: empezar   B: salir", 116);
  display.display();

  static bool pa = false, pb = false;
  while (true) {
    bool a = isMorsePressed(), b = isFinishPressed();
    if (a && !pa) { pa = a; return true; }
    if (b && !pb) { pb = b; return false; }
    pa = a; pb = b;
    backgroundTick();
    delay(10);
  }
}

// ============================================================
//  Entrada principal
// ============================================================
void startHippoRadar() {
  randomSeed(analogRead(A0) ^ micros());
  ownId = Device_id();   // id estable y unico por equipo (no aleatorio): evita que
                         // dos equipos saquen el mismo id y se ignoren mutuamente.

  if (!startScreen()) { mainState = STATE_IDLE; Display_clear(); return; }
  while (isMorsePressed()) { backgroundTick(); delay(10); }   // soltar la A de "empezar"

  radioRangeMode();                                           // largo alcance mientras dure el radar
  rssiEMA = -200.0f; haveLink = false; lastHeard = 0;
  distM = 0; distRef = 0; distTrend = 0;
  phase = PH_SEARCH;
  unsigned long now = millis();
  nextPing = now;
  bool prevA = false;

  while (true) {
    now = millis();

    // B: salir (con anti-rebote)
    if (isFinishPressed()) { delay(40); if (isFinishPressed()) { radioRestore(); mainState = STATE_IDLE; Display_clear(); return; } }
    bool a = isMorsePressed();

    // radio: pings frecuentes para reaccionar al girar
    if (now >= nextPing) { sendPing(); nextPing = now + 300 + (unsigned long)random(80); }
    rxTick();
    if (now - lastHeard > 3500) haveLink = false;
    if (haveLink) distM = estDistance(rssiEMA);
    if (!haveLink) phase = PH_SEARCH;

    if (phase == PH_SEARCH) {
      if (haveLink) { phase = PH_TURN; maxR = -999.0f; minR = 999.0f; maxAt = now; turnStart = now; locking = false; lockProg = 0; }
      drawSearch(now);

    } else if (phase == PH_TURN) {
      // Solo reinicia el "asentamiento" si hay una mejora REAL (>0.5 dB): asi,
      // mientras la senal sube no se bloquea; se bloquea cuando te paras y ya no
      // encuentras nada mas fuerte durante un rato.
      if (rssiEMA > maxR + 0.5f) { maxR = rssiEMA; maxAt = now; }
      else if (rssiEMA > maxR)   { maxR = rssiEMA; }
      if (rssiEMA < minR) minR = rssiEMA;

      float spread = maxR - minR;
      unsigned long turning = now - turnStart;
      float needSpread = (turning > 18000) ? 0.0f : 5.0f;    // tras un rato, relaja para no atascar
      bool nearPeak = (rssiEMA >= maxR - 2.0f);
      bool ready    = nearPeak && spread >= needSpread;
      unsigned long settled = now - maxAt;

      locking  = ready && settled > 250;                     // quieto en el punto fuerte
      lockProg = (settled > 250) ? (float)(settled - 250) / 1150.0f : 0.0f;
      if (ready && settled > 1400) { phase = PH_LOCKED; distRef = distM; distTrend = 0; }

      drawTurn(now);

    } else { // PH_LOCKED
      distRef += (distM - distRef) * 0.05f;                  // referencia lenta
      distTrend = distM - distRef;                           // <0 te acercas, >0 te alejas
      if (a && !prevA) { phase = PH_TURN; maxR = -999.0f; minR = 999.0f; maxAt = now; turnStart = now; locking = false; lockProg = 0; }
      drawLocked(now);
    }

    prevA = a;
    delay(20);
  }
}
