// Implementación del "core Arduino" para el host: reloj virtual, planificador
// de eventos de entrada, E/S, aleatorios, Serial y EEPROM respaldada en fichero.
#include <vector>
#include <string>
#include <cstdio>
#include <random>

#include "sim_state.h"
#include "Arduino.h"
#include "Config.h"
#include "EEPROM.h"
#include "Wire.h"
#include "SPI.h"

// Las macros de Arduino estorban al STL; fuera a partir de aquí.
#undef min
#undef max
#undef abs
#undef constrain

// ---------------------------------------------------------------------------
// Estado del motor
// ---------------------------------------------------------------------------
namespace {
struct Ev { uint32_t t; int kind; int value; };

uint32_t g_now = 0;
uint32_t g_deadline = 0xFFFFFFFFu;
int  g_pot = 512;
bool g_morse = false;
bool g_finish = false;
std::string g_serial;
std::vector<Ev> g_events;
std::mt19937 g_rng(12345u);
void (*g_pump)(uint32_t) = nullptr;   // hook del modo interactivo (teclado + render + ritmo)

void applyEvent(const Ev &e) {
    switch (e.kind) {
        case sim::EV_POT:    g_pot = e.value; break;
        case sim::EV_MORSE:  g_morse = (e.value != 0); break;
        case sim::EV_FINISH: g_finish = (e.value != 0); break;
    }
}
} // namespace

namespace sim {

uint32_t now() { return g_now; }
void setDeadline(uint32_t absMs) { g_deadline = absMs; }
void clearDeadline() { g_deadline = 0xFFFFFFFFu; }
void setPumpHook(void (*fn)(uint32_t)) { g_pump = fn; }

void scheduleAt(uint32_t absMs, EvKind kind, int value) {
    g_events.push_back(Ev{absMs, (int)kind, value});
}

bool nextEventTime(uint32_t *t) {
    bool any = false; uint32_t best = 0;
    for (const auto &e : g_events) { if (!any || e.t < best) { best = e.t; any = true; } }
    if (any && t) *t = best;
    return any;
}

void applyDue() {
    // aplica todos los eventos con t <= g_now en orden temporal
    while (true) {
        int idx = -1; uint32_t best = 0;
        for (size_t i = 0; i < g_events.size(); ++i)
            if (g_events[i].t <= g_now && (idx < 0 || g_events[i].t < best)) { best = g_events[i].t; idx = (int)i; }
        if (idx < 0) break;
        applyEvent(g_events[idx]);
        g_events.erase(g_events.begin() + idx);
    }
}

void advance(uint32_t ms) {
    uint32_t target = g_now + ms;
    while (true) {
        int idx = -1; uint32_t best = 0;
        for (size_t i = 0; i < g_events.size(); ++i)
            if (idx < 0 || g_events[i].t < best) { best = g_events[i].t; idx = (int)i; }
        if (idx < 0 || best > target) break;
        g_now = (best < g_now) ? g_now : best;     // los eventos pasados se aplican ya
        applyEvent(g_events[idx]);
        g_events.erase(g_events.begin() + idx);
        if (g_now > g_deadline) throw Timeout();
    }
    g_now = target;
    applyDue();
    if (g_now > g_deadline) throw Timeout();
    if (g_pump) g_pump(ms);   // modo interactivo: sondea teclado, redibuja y marca el ritmo
}

void setPot(int v) { g_pot = v < 0 ? 0 : (v > 1023 ? 1023 : v); }
void setMorse(bool down) { g_morse = down; }
void setFinish(bool down) { g_finish = down; }
int  getPot() { return g_pot; }
bool morseDown() { return g_morse; }
bool finishDown() { return g_finish; }

void serialPut(char c) { g_serial += c; }
std::string serialLog() { return g_serial; }
void serialClear() { g_serial.clear(); }

} // namespace sim

// ---------------------------------------------------------------------------
// Tiempo (Arduino)
// ---------------------------------------------------------------------------
unsigned long millis() { return g_now; }
unsigned long micros() { return (unsigned long)g_now * 1000UL; }
void delay(unsigned long ms) { sim::advance((uint32_t)ms); }
void delayMicroseconds(unsigned int us) { sim::advance(us / 1000u); }
void yield() { sim::applyDue(); }

// ---------------------------------------------------------------------------
// E/S
// ---------------------------------------------------------------------------
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t, uint8_t) {}
void analogWrite(uint8_t, int) {}

int digitalRead(uint8_t pin) {
    if (pin == PIN_MORSE_BUTTON)  return g_morse  ? LOW : HIGH;  // INPUT_PULLUP: pulsado = LOW
    if (pin == PIN_FINISH_BUTTON) return g_finish ? LOW : HIGH;
    return HIGH;
}

int analogRead(uint8_t pin) {
    if (pin == POT_PIN) return g_pot;
    return 0;
}

// ---------------------------------------------------------------------------
// Aleatorios
// ---------------------------------------------------------------------------
long random(long howbig) {
    if (howbig <= 0) return 0;
    return (long)(g_rng() % (uint32_t)howbig);
}
long random(long howsmall, long howbig) {
    if (howbig <= howsmall) return howsmall;
    return howsmall + (long)(g_rng() % (uint32_t)(howbig - howsmall));
}
void randomSeed(unsigned long seed) { if (seed) g_rng.seed((uint32_t)seed); }

// ---------------------------------------------------------------------------
// Serial / Wire / SPI globales
// ---------------------------------------------------------------------------
size_t HardwareSerial::write(uint8_t c) { sim::serialPut((char)c); return 1; }
HardwareSerial Serial;
TwoWire Wire;
SPIClass SPI;

// ---------------------------------------------------------------------------
// EEPROM respaldada en fichero (persiste el historial entre ejecuciones)
// ---------------------------------------------------------------------------
namespace {
std::string g_eePath = "sim_eeprom.bin";
std::vector<uint8_t> g_ee;
}

void EEPROMClass::setBackingFile(const char *path) { g_eePath = path ? path : "sim_eeprom.bin"; }

void EEPROMClass::begin(size_t size) {
    g_ee.assign(size, 0xFF);   // flash sin escribir = 0xFF
    FILE *f = fopen(g_eePath.c_str(), "rb");
    if (f) {
        std::vector<uint8_t> tmp(size);
        size_t n = fread(tmp.data(), 1, size, f);
        for (size_t i = 0; i < n && i < size; ++i) g_ee[i] = tmp[i];
        fclose(f);
    }
}
uint8_t EEPROMClass::read(int address) {
    if (address < 0 || (size_t)address >= g_ee.size()) return 0xFF;
    return g_ee[address];
}
void EEPROMClass::write(int address, uint8_t value) {
    if (address < 0 || (size_t)address >= g_ee.size()) return;
    g_ee[address] = value;
}
bool EEPROMClass::commit() {
    FILE *f = fopen(g_eePath.c_str(), "wb");
    if (!f) return false;
    if (!g_ee.empty()) fwrite(g_ee.data(), 1, g_ee.size(), f);
    fclose(f);
    return true;
}
void EEPROMClass::end() { commit(); }

EEPROMClass EEPROM;
