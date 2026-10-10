#include <Arduino.h>
#include <EEPROM.h>
#include "Clock.h"
#include "EepromMap.h"

// Cabecera de la region: 3 bytes de magia + 1 de version, luego 4B de epoch y,
// desde la version 2, 1B con la generacion de ajuste. Una EEPROM con version 1
// (sin generacion) se carga con generacion 0.
static const uint8_t CK_MAGIC[3] = { 'C', 'K', '1' };
#define CK_VERSION 2

// Persistir el avance del reloj como mucho cada 30 min (no desgastar flash):
// un peer re-sincroniza en la siguiente baliza, asi que basta con un respaldo
// grueso para sobrevivir un reinicio sin companero cerca.
static const uint32_t PERSIST_INTERVAL_S = 1800;

static uint32_t s_base = 0;            // epoch en el instante millis()==s_millisAtBase
static uint32_t s_millisAtBase = 0;   // millis() cuando se fijo s_base
static bool     s_synced = false;
static uint32_t s_lastPersist = 0;    // epoch del ultimo guardado
static uint8_t  s_gen = 0;            // generacion de ajuste (0 = nunca se ha fijado a mano)

// --- Generaciones (1..255 con vuelta; 0 = nunca fijada) ---
static bool genNewer(uint8_t a, uint8_t b) {      // true si 'a' es MAS RECIENTE que 'b'
  if (a == b) return false;
  if (b == 0) return true;                         // cualquier ajuste manual supera a "nunca fijada"
  if (a == 0) return false;
  return (uint8_t)(a - b) < 128;                   // aritmetica de numeros de serie
}
static uint8_t nextGen(uint8_t g) { g++; if (g == 0) g = 1; return g; }

static bool magicOk() {
  for (int i = 0; i < 3; i++) if (EEPROM.read(EE_CLOCK_BASE + i) != CK_MAGIC[i]) return false;
  return true;
}

static void writeEpoch(uint32_t ep) {
  EEPROM.begin(EE_TOTAL_SIZE);          // tamano total: preservar las demas regiones
  for (int i = 0; i < 3; i++) EEPROM.write(EE_CLOCK_BASE + i, CK_MAGIC[i]);
  EEPROM.write(EE_CLOCK_BASE + 3, CK_VERSION);
  EEPROM.write(EE_CLOCK_BASE + 4, (ep >> 24) & 0xFF);
  EEPROM.write(EE_CLOCK_BASE + 5, (ep >> 16) & 0xFF);
  EEPROM.write(EE_CLOCK_BASE + 6, (ep >> 8) & 0xFF);
  EEPROM.write(EE_CLOCK_BASE + 7, ep & 0xFF);
  EEPROM.write(EE_CLOCK_BASE + 8, s_gen);
  EEPROM.commit();
  EEPROM.end();
  s_lastPersist = ep;
}

static void rebase(uint32_t epoch) {
  s_base = epoch;
  s_millisAtBase = millis();
}

uint32_t Clock_now() {
  return s_base + (uint32_t)((millis() - s_millisAtBase) / 1000UL);
}

void Clock_load() {
  EEPROM.begin(EE_TOTAL_SIZE);
  uint32_t ep = 0;
  uint8_t gen = 0;
  if (magicOk()) {
    ep  = (uint32_t)EEPROM.read(EE_CLOCK_BASE + 4) << 24;
    ep |= (uint32_t)EEPROM.read(EE_CLOCK_BASE + 5) << 16;
    ep |= (uint32_t)EEPROM.read(EE_CLOCK_BASE + 6) << 8;
    ep |= (uint32_t)EEPROM.read(EE_CLOCK_BASE + 7);
    if (EEPROM.read(EE_CLOCK_BASE + 3) >= 2) gen = EEPROM.read(EE_CLOCK_BASE + 8);
  }
  EEPROM.end();
  rebase(ep);
  s_lastPersist = ep;
  s_gen = gen;
  s_synced = (ep > 0);   // habia un epoch guardado -> lo damos por bueno
  Serial.print("[Clock] epoch cargado: "); Serial.print(ep);
  Serial.print(" gen "); Serial.println(gen);
}

int32_t Clock_set(uint32_t epoch) {
  if (epoch == 0) return 0;          // 0 = "sin hora"; no marca el reloj como fijado
  uint32_t before = Clock_now();
  rebase(epoch);
  s_synced = true;
  s_gen = nextGen(s_gen);
  writeEpoch(epoch);
  Serial.print("[Clock] hora fijada: "); Serial.print(epoch);
  Serial.print(" gen "); Serial.println(s_gen);
  return (int32_t)(epoch - before);
}

int32_t Clock_setTimeOfDay(int hour, int minute) {
  if (hour < 0) hour = 0;     if (hour > 23)   hour = 23;
  if (minute < 0) minute = 0; if (minute > 59) minute = 59;
  uint32_t now = Clock_now();
  uint32_t day = now - (now % 86400UL);          // conserva el dia
  uint32_t ep  = day + (uint32_t)hour * 3600UL + (uint32_t)minute * 60UL;
  if (ep == 0) ep = 1;                            // 0 significa "sin hora"
  return Clock_set(ep);
}

int32_t Clock_syncFromPeer(uint32_t peerEpoch, uint8_t peerGen) {
  if (peerEpoch == 0) return 0;
  uint32_t now = Clock_now();

  if (genNewer(peerGen, s_gen)) {
    // El peer fijo la hora DESPUES que nosotros: es la autoritativa, aunque sea
    // anterior a la nuestra (correccion hacia atras).
    rebase(peerEpoch);
    s_synced = true;
    s_gen = peerGen;
    writeEpoch(peerEpoch);
    Serial.print("[Clock] hora adoptada del peer: "); Serial.print(peerEpoch);
    Serial.print(" gen "); Serial.println(peerGen);
    return (int32_t)(peerEpoch - now);
  }
  if (genNewer(s_gen, peerGen)) return 0;   // la nuestra es mas reciente: la ignoramos

  if (peerEpoch > now) {             // misma generacion: converge a la mayor (monotono)
    rebase(peerEpoch);
    s_synced = true;
    // Persistir SOLO si el salto respecto al ultimo guardado es relevante: los dos
    // relojes derivan +-1 s y, sin este umbral, cada baliza (cada 30 s) reescribiria
    // flash. Con el umbral se persiste como mucho cada PERSIST_INTERVAL_S de avance.
    if (peerEpoch >= s_lastPersist + PERSIST_INTERVAL_S) writeEpoch(peerEpoch);
    Serial.print("[Clock] sincronizado con peer: "); Serial.println(peerEpoch);
  }
  return 0;
}

void Clock_tickPersist() {
  uint32_t now = Clock_now();
  if (now >= s_lastPersist + PERSIST_INTERVAL_S) writeEpoch(now);
}

uint8_t Clock_gen() { return s_gen; }
bool Clock_isTimeSet() { return s_gen != 0; }
bool Clock_isSynced() { return s_synced; }

String Clock_hhmm() {
  if (!Clock_isTimeSet()) return String("--:--");   // nunca se ha puesto la hora real
  uint32_t secsOfDay = Clock_now() % 86400UL;
  int h = (int)(secsOfDay / 3600);
  int m = (int)((secsOfDay % 3600) / 60);
  // Construir "HH:MM" con aritmética de char (evita ambigüedad de String += int).
  String r = "";
  r += (char)('0' + (h / 10) % 10);
  r += (char)('0' + h % 10);
  r += ':';
  r += (char)('0' + (m / 10) % 10);
  r += (char)('0' + m % 10);
  return r;
}
