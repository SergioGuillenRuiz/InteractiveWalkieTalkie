#include <Arduino.h>
#include <EEPROM.h>
#include "Clock.h"
#include "EepromMap.h"

// Cabecera de la region: 3 bytes de magia + 1 de version, luego 4B de epoch.
static const uint8_t CK_MAGIC[3] = { 'C', 'K', '1' };

// Persistir el avance del reloj como mucho cada 30 min (no desgastar flash):
// un peer re-sincroniza en la siguiente baliza, asi que basta con un respaldo
// grueso para sobrevivir un reinicio sin companero cerca.
static const uint32_t PERSIST_INTERVAL_S = 1800;

static uint32_t s_base = 0;            // epoch en el instante millis()==s_millisAtBase
static uint32_t s_millisAtBase = 0;   // millis() cuando se fijo s_base
static bool     s_synced = false;
static uint32_t s_lastPersist = 0;    // epoch del ultimo guardado

static bool magicOk() {
  for (int i = 0; i < 3; i++) if (EEPROM.read(EE_CLOCK_BASE + i) != CK_MAGIC[i]) return false;
  return true;
}

static void writeEpoch(uint32_t ep) {
  EEPROM.begin(EE_TOTAL_SIZE);          // tamano total: preservar las demas regiones
  for (int i = 0; i < 3; i++) EEPROM.write(EE_CLOCK_BASE + i, CK_MAGIC[i]);
  EEPROM.write(EE_CLOCK_BASE + 3, 1);   // version
  EEPROM.write(EE_CLOCK_BASE + 4, (ep >> 24) & 0xFF);
  EEPROM.write(EE_CLOCK_BASE + 5, (ep >> 16) & 0xFF);
  EEPROM.write(EE_CLOCK_BASE + 6, (ep >> 8) & 0xFF);
  EEPROM.write(EE_CLOCK_BASE + 7, ep & 0xFF);
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
  if (magicOk()) {
    ep  = (uint32_t)EEPROM.read(EE_CLOCK_BASE + 4) << 24;
    ep |= (uint32_t)EEPROM.read(EE_CLOCK_BASE + 5) << 16;
    ep |= (uint32_t)EEPROM.read(EE_CLOCK_BASE + 6) << 8;
    ep |= (uint32_t)EEPROM.read(EE_CLOCK_BASE + 7);
  }
  EEPROM.end();
  rebase(ep);
  s_lastPersist = ep;
  s_synced = (ep > 0);   // habia un epoch guardado -> lo damos por bueno
  Serial.print("[Clock] epoch cargado: "); Serial.println(ep);
}

void Clock_set(uint32_t epoch) {
  if (epoch == 0) return;            // 0 = "sin hora"; no marca el reloj como fijado
  rebase(epoch);
  s_synced = true;
  writeEpoch(epoch);
  Serial.print("[Clock] hora fijada: "); Serial.println(epoch);
}

void Clock_syncFromPeer(uint32_t peerEpoch) {
  if (peerEpoch == 0) return;
  if (peerEpoch > Clock_now()) {     // adoptar la hora mayor (monotona, convergente)
    rebase(peerEpoch);
    s_synced = true;
    // Persistir SOLO si el salto respecto al ultimo guardado es relevante: los dos
    // relojes derivan ±1 s y, sin este umbral, cada baliza (cada 30 s) reescribiria
    // flash. Con el umbral se persiste como mucho cada PERSIST_INTERVAL_S de avance.
    if (peerEpoch >= s_lastPersist + PERSIST_INTERVAL_S) writeEpoch(peerEpoch);
    Serial.print("[Clock] sincronizado con peer: "); Serial.println(peerEpoch);
  }
}

void Clock_tickPersist() {
  uint32_t now = Clock_now();
  if (now >= s_lastPersist + PERSIST_INTERVAL_S) writeEpoch(now);
}

bool Clock_isSynced() { return s_synced; }

String Clock_hhmm() {
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
