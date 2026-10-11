#include <Arduino.h>
#include <EEPROM.h>
#include <string.h>
#include "Pairing.h"
#include "Crypto.h"
#include "EepromMap.h"
#include "Display.h"
#include "Inputs.h"
#include "States.h"
#include "MyLora.h"

// ============================================================
//  1) ALMACEN DE CLAVES (EEPROM)
//     0..2 magia {'P','K','1'} | 3 version | 4..19 clave de cifrado | 20..35 clave de autenticacion
//     36..39 contador de emision reservado | 40..59 cuatro entradas de recepcion (id del emisor + tramo de 4096)
//
//  Contadores anti-repeticion. Cada emision lleva un numero de secuencia creciente (viaja dentro de la parte
//  autenticada del paquete) y el receptor solo acepta numeros posteriores al ultimo que vio: un paquete grabado y
//  reenviado despues se rechaza. Para no gastar la flash (un commit por paquete la agotaria) no se guarda cada numero:
//   - emision: se reserva por tramos de 4096. Al arrancar se empieza en el valor guardado y se guarda +4096; nunca se
//     repite un numero aunque el equipo se reinicie o se apague en cualquier momento;
//   - recepcion: se guarda el tramo del ultimo numero visto. Tras reiniciar solo se rechaza lo anterior a ese tramo:
//     queda una ventana de hasta 4096 paquetes recientes que un atacante podria repetir justo tras un reinicio.
// ============================================================
static const uint8_t PK_MAGIC[3] = { 'P', 'K', '1' };
static const uint8_t PK_VERSION = 1;
static const uint32_t SEQ_CHUNK = 4096;

static bool     g_paired = false;
static uint8_t  g_enc[16], g_mac[16];
static uint32_t g_txSeq = 1, g_txPersisted = 0;
// Recepcion: un contador por EMISOR (el id viaja en la parte autenticada del paquete). Con una pareja solo hay uno; la
// tabla admite 4 por si varios equipos comparten clave.
struct RxEntry { uint8_t id; uint32_t last; uint32_t persisted; bool used; };
static RxEntry g_rx[4];

static void putU32(int addr, uint32_t v) { for (int i = 0; i < 4; i++) EEPROM.write(addr + i, (v >> (8 * i)) & 0xFF); }
static uint32_t getU32(int addr) { uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (uint32_t)EEPROM.read(addr + i) << (8 * i); return v; }

static void save() {
  EEPROM.begin(EE_TOTAL_SIZE);
  for (int i = 0; i < 3; i++) EEPROM.write(EE_PAIR_BASE + i, PK_MAGIC[i]);
  EEPROM.write(EE_PAIR_BASE + 3, PK_VERSION);
  for (int i = 0; i < 16; i++) { EEPROM.write(EE_PAIR_BASE + 4 + i, g_enc[i]); EEPROM.write(EE_PAIR_BASE + 20 + i, g_mac[i]); }
  putU32(EE_PAIR_BASE + 36, g_txPersisted);
  for (int i = 0; i < 4; i++) {
    EEPROM.write(EE_PAIR_BASE + 40 + 5 * i, g_rx[i].used ? g_rx[i].id : 0);
    putU32(EE_PAIR_BASE + 41 + 5 * i, g_rx[i].used ? g_rx[i].persisted : 0);
  }
  EEPROM.commit();
  EEPROM.end();
}

static void install(const uint8_t enc[16], const uint8_t mac[16]) {
  memcpy(g_enc, enc, 16); memcpy(g_mac, mac, 16);
  g_paired = true;
  g_txSeq = 1; g_txPersisted = SEQ_CHUNK;
  memset(g_rx, 0, sizeof(g_rx));
  save();
}

void Pairing_load() {
  EEPROM.begin(EE_TOTAL_SIZE);
  bool ok = true;
  for (int i = 0; i < 3; i++) if (EEPROM.read(EE_PAIR_BASE + i) != PK_MAGIC[i]) ok = false;
  if (ok && EEPROM.read(EE_PAIR_BASE + 3) != PK_VERSION) ok = false;
  uint32_t txP = 0;
  memset(g_rx, 0, sizeof(g_rx));
  if (ok) {
    for (int i = 0; i < 16; i++) { g_enc[i] = EEPROM.read(EE_PAIR_BASE + 4 + i); g_mac[i] = EEPROM.read(EE_PAIR_BASE + 20 + i); }
    txP = getU32(EE_PAIR_BASE + 36);
    for (int i = 0; i < 4; i++) {
      uint8_t id = EEPROM.read(EE_PAIR_BASE + 40 + 5 * i);
      if (id != 0 && id != 0xFF) { g_rx[i].used = true; g_rx[i].id = id; g_rx[i].persisted = g_rx[i].last = getU32(EE_PAIR_BASE + 41 + 5 * i); }
    }
  }
  EEPROM.end();
  g_paired = ok;

#if !defined(ESP8266)
  if (!g_paired) {                       // simulador: clave de pruebas (en la placa real hay que emparejar)
    uint8_t d[32];
    const char *label = "walkie-simulator-test-key";
    Crypto_sha256((const uint8_t *)label, strlen(label), d);
    install(d, d + 16);
    return;
  }
#endif
  if (g_paired) {
    g_txSeq = txP ? txP : 1;             // numero 0 no se usa
    g_txPersisted = (txP ? txP : 1) + SEQ_CHUNK;
    save();                              // reserva el tramo que se va a usar
  }
}

bool Pairing_isPaired() { return g_paired; }

void Pairing_clear() {
  g_paired = false;
  memset(g_enc, 0, 16); memset(g_mac, 0, 16);
  EEPROM.begin(EE_TOTAL_SIZE);
  for (int i = 0; i < EE_PAIR_SIZE; i++) EEPROM.write(EE_PAIR_BASE + i, 0xFF);
  EEPROM.commit();
  EEPROM.end();
}

bool Pairing_keys(uint8_t enc[16], uint8_t mac[16]) {
  if (!g_paired) return false;
  memcpy(enc, g_enc, 16); memcpy(mac, g_mac, 16);
  return true;
}

uint32_t Pairing_nextTxSeq() {
  if (g_txSeq >= g_txPersisted) { g_txPersisted += SEQ_CHUNK; save(); }
  return g_txSeq++;
}

bool Pairing_acceptRxSeq(uint8_t senderId, uint32_t seq) {
  RxEntry *e = nullptr, *victim = &g_rx[0];
  for (int i = 0; i < 4; i++) {
    if (g_rx[i].used && g_rx[i].id == senderId) { e = &g_rx[i]; break; }
    if (!g_rx[i].used) victim = &g_rx[i];
    else if (victim->used && g_rx[i].last < victim->last) victim = &g_rx[i];
  }
  bool isNew = false;
  if (!e) { e = victim; memset(e, 0, sizeof(*e)); e->used = true; e->id = senderId; isNew = true; }
  if (!isNew && seq <= e->last) return false;
  e->last = seq;
  if (isNew || seq - e->persisted >= SEQ_CHUNK) { e->persisted = seq & ~(SEQ_CHUNK - 1); save(); }
  return true;
}

// ============================================================
//  2) ALEATORIOS
//  El generador por hardware del ESP8266 pierde calidad con el WiFi apagado (como aqui), asi que no se fia de el
//  solo: se mezclan con SHA-256 varias fuentes independientes -- el generador por hardware, el ruido de banda ancha
//  que mide el propio chip de radio (es lo que usa Semtech para generar aleatorios), el ruido del ADC y los tiempos --
//  y el resultado anterior.
// ============================================================
void Pairing_randomBytes(uint8_t *out, size_t n) {
  static uint8_t chain[32];
  uint8_t buf[32 + 64 + 8 + 32 + 32];
  size_t k = 0;
  memcpy(buf + k, chain, 32); k += 32;
#if defined(ESP8266)
  for (int i = 0; i < 16; i++) { uint32_t r = RANDOM_REG32; memcpy(buf + k, &r, 4); k += 4; }
#else
  for (int i = 0; i < 16; i++) { uint32_t r = (uint32_t)random(0x7FFFFFFF) ^ ((uint32_t)random(0x7FFFFFFF) << 1); memcpy(buf + k, &r, 4); k += 4; }
#endif
  uint32_t t1 = micros(), t2 = millis(); memcpy(buf + k, &t1, 4); memcpy(buf + k + 4, &t2, 4); k += 8;
  for (int i = 0; i < 32; i++) { buf[k++] = (uint8_t)analogRead(A0); delayMicroseconds(150); }
  Lora_entropy(buf + k, 32); k += 32;
  uint8_t d[32];
  Crypto_sha256(buf, k, d);
  memcpy(chain, d, 32);
  memcpy(out, d, n > 32 ? 32 : n);
}

// ============================================================
//  3) PROTOCOLO DE EMPAREJAMIENTO
//
//  Tramas (sin cifrar; empiezan por 'W' y miden 18 o 34 B: los paquetes cifrados siempre miden multiplo de 16):
//    'W','H', compromiso(16)  = SHA-256(clave publica)[0..16]       "esta es la clave que usare" (sin revelarla)
//    'W','K', clave publica(32)                                      solo se envia al conocer el compromiso del otro
//    'W','C', confirmacion(16) = AES-CMAC(clave de autenticacion, "C" | rol | claves publicas ordenadas)
//
//  1. Cada equipo genera un par X25519 y difunde su COMPROMISO.
//  2. Al oir el del otro, difunde tambien su clave publica. Revelarla DESPUES de haberse comprometido ambos impide
//     que un atacante elija su clave en funcion de la que ve (si pudiera, podria probar claves hasta que el codigo de
//     comprobacion coincidiera en las dos pantallas).
//  3. Con las dos claves publicas: secreto compartido X25519 -> SHA-256 -> clave de cifrado y de autenticacion, y un
//     codigo de 6 digitos. Si alguien se interpone, cada equipo calcula un secreto distinto y los codigos NO coinciden.
//  4. El usuario compara los codigos en las dos pantallas. A = igual: se envia la confirmacion (demuestra que se tiene
//     la clave). B = distinto: se aborta.
//  5. Cuando cada equipo ha confirmado y ha recibido la confirmacion valida del otro, guarda la clave.
// ============================================================
static const uint8_t FR_MAGIC = 'W';

enum PairState { PS_OFF, PS_SEARCH, PS_SAS, PS_DONE, PS_ABORT };
static PairState g_state = PS_OFF;
static bool     g_active = false;

static uint8_t  myPriv[32], myPub[32], myCommit[16];
static uint8_t  peerCommit[16], peerPub[32], rxPub[32], rxConfirm[16];
static bool     haveCommit, havePendingPub, haveKey, gotConfirm, localOk, peerOk;
static PairSecrets sec;

void Pairing_onFrame(const String &raw) {
  if (!g_active || raw.length() < 2 || (uint8_t)raw[0] != FR_MAGIC) return;
  char type = raw[1];
  if (type == 'H' && raw.length() == 18) {
    if (!haveKey) {                                  // el ultimo compromiso gana mientras no haya clave (el otro pudo reiniciar)
      for (int i = 0; i < 16; i++) peerCommit[i] = (uint8_t)raw[2 + i];
      haveCommit = true;
    }
  } else if (type == 'K' && raw.length() == 34) {
    if (haveCommit && !haveKey) { for (int i = 0; i < 32; i++) rxPub[i] = (uint8_t)raw[2 + i]; havePendingPub = true; }
  } else if (type == 'C' && raw.length() == 18) {
    for (int i = 0; i < 16; i++) rxConfirm[i] = (uint8_t)raw[2 + i];
    gotConfirm = true;
  }
}

static void orderPubs(const uint8_t a[32], const uint8_t b[32], uint8_t low[32], uint8_t high[32], uint8_t &roleOfA) {
  if (memcmp(a, b, 32) < 0) { memcpy(low, a, 32); memcpy(high, b, 32); roleOfA = 1; }
  else                      { memcpy(low, b, 32); memcpy(high, a, 32); roleOfA = 2; }
}

// Deriva claves y codigo a partir del secreto compartido. false si el secreto no es valido (clave de orden bajo).
bool Pairing_derive(const uint8_t myPriv[32], const uint8_t myPub[32], const uint8_t peerPub[32], PairSecrets &out) {
  uint8_t shared[32];
  Crypto_x25519(shared, myPriv, peerPub);
  uint8_t nz = 0;
  for (int i = 0; i < 32; i++) nz |= shared[i];
  if (!nz) return false;
  uint8_t low[32], high[32];
  orderPubs(myPub, peerPub, low, high, out.role);
  uint8_t buf[16 + 32 + 64], d[32];
  const char *lk = "walkie-keys-v1";
  size_t k = strlen(lk);
  memcpy(buf, lk, k);
  memcpy(buf + k, shared, 32); k += 32;
  memcpy(buf + k, low, 32); k += 32;
  memcpy(buf + k, high, 32); k += 32;
  Crypto_sha256(buf, k, d);
  memcpy(out.enc, d, 16); memcpy(out.mac, d + 16, 16);
  const char *ls = "walkie-sas-v1";
  k = strlen(ls);
  memcpy(buf, ls, k);
  memcpy(buf + k, shared, 32); k += 32;
  memcpy(buf + k, low, 32); k += 32;
  memcpy(buf + k, high, 32); k += 32;
  Crypto_sha256(buf, k, d);
  uint32_t v = ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) | ((uint32_t)d[2] << 8) | d[3];
  out.sas = v % 1000000UL;
  memset(shared, 0, 32);
  return true;
}

// Confirmacion: AES-CMAC(clave de autenticacion, "C" | rol de quien confirma | claves publicas ordenadas). Lleva el rol para que
// no sirva devolverle a un equipo su propia confirmacion.
void Pairing_confirmTag(const uint8_t mac[16], const uint8_t myPub[32], const uint8_t peerPub[32], uint8_t who, uint8_t out[16]) {
  uint8_t low[32], high[32], r;
  orderPubs(myPub, peerPub, low, high, r);
  uint8_t m[2 + 64];
  m[0] = 'C'; m[1] = who;
  memcpy(m + 2, low, 32); memcpy(m + 34, high, 32);
  Crypto_aesCmac(mac, m, sizeof(m), out);
}

static void centerPrint(const String &s, int y, uint8_t size = 1) {
  display.setTextSize(size);
  display.setTextColor(SH110X_WHITE);
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(s.c_str(), 0, 0, &bx, &by, &bw, &bh);
  int x = (128 - (int)bw) / 2; if (x < 0) x = 0;
  display.setCursor(x, y); display.print(s);
}

static void screen(const char *l1, const char *l2, const char *l3, const char *foot1, const char *foot2) {
  display.clearDisplay();
  drawTitleBar("Emparejar");
  if (l1) centerPrint(l1, 26, 1);
  if (l2) centerPrint(l2, 40, 1);
  if (l3) centerPrint(l3, 54, 1);
  if (foot1) centerPrint(foot1, 104, 1);
  if (foot2) centerPrint(foot2, 115, 1);
  display.display();
}

static void drawSas() {
  display.clearDisplay();
  drawTitleBar("Emparejar");
  centerPrint("Codigo:", 22, 1);
  char c[8];
  snprintf(c, sizeof(c), "%06lu", (unsigned long)sec.sas);
  centerPrint(c, 36, 3);
  centerPrint("Es el MISMO en el", 70, 1);
  centerPrint("otro equipo?", 81, 1);
  if (!localOk) { centerPrint("A: si, igual", 104, 1); centerPrint("B: no, cancelar", 115, 1); }
  else          { centerPrint("Esperando al otro...", 104, 1); centerPrint("B: cancelar", 115, 1); }
  display.display();
}

static void pairDelay(unsigned long ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) { backgroundTick(false); isMorsePressed(); isFinishPressed(); delay(10); }
}

// Una trama de emparejamiento, en el orden H, K (si se conoce el compromiso del otro), C (si ya se confirmo).
static void sendNext(int &idx) {
  String f;
  f += (char)FR_MAGIC;
  int kinds[3], n = 0;
  kinds[n++] = 'H';
  if (haveCommit) kinds[n++] = 'K';
  if (haveKey && localOk) kinds[n++] = 'C';
  int kind = kinds[idx % n];
  idx++;
  f += (char)kind;
  if (kind == 'H') for (int i = 0; i < 16; i++) f += (char)myCommit[i];
  else if (kind == 'K') for (int i = 0; i < 32; i++) f += (char)myPub[i];
  else { uint8_t t[16]; Pairing_confirmTag(sec.mac, myPub, peerPub, sec.role, t); for (int i = 0; i < 16; i++) f += (char)t[i]; }
  Lora_sendRaw(f);
}

void startPairing() {
  while (isMorsePressed() || isFinishPressed()) { backgroundTick(); delay(10); }

  if (Pairing_isPaired()) {
    screen("Este equipo ya tiene", "pareja. Emparejar de", "nuevo cambia la clave.", "A: emparejar de nuevo", "B: salir");
    bool go = false;
    while (true) {
      backgroundTick();
      if (isMorsePressed()) { go = true; break; }
      if (isFinishPressed()) break;
      delay(15);
    }
    while (isMorsePressed() || isFinishPressed()) { backgroundTick(); delay(10); }
    if (!go) { mainState = STATE_IDLE; lastInteraction = millis(); Display_clear(); return; }
  }

  screen("Preparando...", "(unos segundos)", nullptr, nullptr, nullptr);
  Pairing_randomBytes(myPriv, 32);
  Crypto_x25519Base(myPub, myPriv);
  uint8_t h[32];
  Crypto_sha256(myPub, 32, h);
  memcpy(myCommit, h, 16);

  haveCommit = havePendingPub = haveKey = gotConfirm = localOk = peerOk = false;
  g_state = PS_SEARCH; g_active = true;
  Serial.println("[Pair] buscando al otro equipo");

  unsigned long t0 = millis(), nextTx = 0, doneAt = 0;
  int idx = 0;
  bool pa = false, pb = false;
  const char *abortMsg = nullptr;

  while (true) {
    unsigned long now = millis();
    backgroundTick(false);

    // Emitir la siguiente trama cada 500-800 ms (con el canal libre)
    if ((long)(now - nextTx) >= 0 && !Lora_busy()) {
      sendNext(idx);
      nextTx = now + 500 + random(300);
    }

    // Clave publica del otro recibida: comprobar el compromiso y calcular
    if (havePendingPub && !haveKey) {
      havePendingPub = false;
      uint8_t hh[32];
      Crypto_sha256(rxPub, 32, hh);
      if (Crypto_equal(hh, peerCommit, 16)) {
        memcpy(peerPub, rxPub, 32);
        screen("Calculando...", "(unos segundos)", nullptr, nullptr, nullptr);
        if (Pairing_derive(myPriv, myPub, peerPub, sec)) {
          haveKey = true; g_state = PS_SAS;
          Serial.print("[Pair] codigo: "); Serial.println((unsigned long)sec.sas);
        } else { abortMsg = "Clave no valida"; break; }
      } else {
        haveCommit = false;                          // no coincide con el compromiso: se descarta y se espera otro
        Serial.println("[Pair] clave publica que no coincide con el compromiso: descartada");
      }
    }

    // Confirmacion del otro
    if (gotConfirm && haveKey) {
      gotConfirm = false;
      uint8_t exp[16];
      Pairing_confirmTag(sec.mac, myPub, peerPub, sec.role == 1 ? 2 : 1, exp);
      if (Crypto_equal(exp, rxConfirm, 16)) { peerOk = true; }
      else Serial.println("[Pair] confirmacion invalida: ignorada");
    }

    bool a = isMorsePressed(), b = isFinishPressed();
    if (g_state == PS_SAS && a && !pa && !localOk) { localOk = true; Serial.println("[Pair] confirmado en este equipo"); }
    if (b && !pb && g_state != PS_DONE) { abortMsg = "Cancelado"; break; }
    if (g_state == PS_DONE && ((a && !pa) || (b && !pb)) && now - doneAt > 1500) break;
    pa = a; pb = b;

    if (g_state == PS_SAS && localOk && peerOk) {
      install(sec.enc, sec.mac);
      g_state = PS_DONE; doneAt = now;
      Serial.println("[Pair] emparejado: clave guardada");
    }

    if (now - t0 > 180000UL && g_state != PS_DONE) { abortMsg = "Tiempo agotado"; break; }

    switch (g_state) {
      case PS_SEARCH:
        screen("Pon el otro equipo", "tambien en Emparejar", (now / 500) % 2 ? "Buscando.." : "Buscando...", nullptr, "B: cancelar");
        break;
      case PS_SAS:  drawSas(); break;
      case PS_DONE: screen("Emparejado!", "Solo vosotros dos", "podeis hablar.", nullptr, "A: salir"); break;
      default: break;
    }
    delay(20);
  }

  if (abortMsg) {
    Serial.print("[Pair] "); Serial.println(abortMsg);
    screen(abortMsg, "No se ha cambiado", "nada.", nullptr, nullptr);
    pairDelay(1500);
  }
  g_active = false; g_state = PS_OFF;
  memset(myPriv, 0, 32); memset(&sec, 0, sizeof(sec));
  while (isMorsePressed() || isFinishPressed()) { backgroundTick(); delay(10); }
  mainState = STATE_IDLE;
  lastInteraction = millis();
  Display_clear();
}
