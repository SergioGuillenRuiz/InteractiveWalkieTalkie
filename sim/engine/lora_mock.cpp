// Implementación del mock de radio LoRa.
#include <string>
#include <deque>

#include "LoRa.h"
#include "sim_state.h"

namespace {
std::deque<std::string> g_rx;   // paquetes entrantes pendientes
std::string g_curRx;            // paquete en recepción (tras parsePacket)
size_t      g_curPos = 0;
std::string g_txAccum;          // paquete en transmisión (entre begin/endPacket)
std::string g_lastSent;         // último paquete transmitido
bool        g_loopback = false;
}

int  LoRaClass::begin(long /*freq*/) { return 1; }

void LoRaClass::beginPacket() { g_txAccum.clear(); }

size_t LoRaClass::write(uint8_t b) { g_txAccum += (char)b; return 1; }

int LoRaClass::endPacket(bool /*async*/) {
    g_lastSent = g_txAccum;
    if (g_loopback) g_rx.push_back(g_txAccum);   // eco: lo enviado vuelve como recibido
    g_txAccum.clear();
    return 1;
}

int LoRaClass::parsePacket(int /*size*/) {
    if (g_rx.empty()) return 0;
    g_curRx = g_rx.front();
    g_rx.pop_front();
    g_curPos = 0;
    return (int)g_curRx.size();
}

int LoRaClass::available() { return (int)(g_curRx.size() - g_curPos); }

int LoRaClass::read() {
    if (g_curPos >= g_curRx.size()) return -1;
    return (uint8_t)g_curRx[g_curPos++];
}

int LoRaClass::peek() {
    if (g_curPos >= g_curRx.size()) return -1;
    return (uint8_t)g_curRx[g_curPos];
}

LoRaClass LoRa;

// --- API hacia el driver del simulador ---
void simLoraInject(const std::string &packet) { g_rx.push_back(packet); }
std::string simLoraLastSent() { return g_lastSent; }
void simLoraSetLoopback(bool on) { g_loopback = on; }
