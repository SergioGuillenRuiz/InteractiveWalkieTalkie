// API interna del motor del simulador (reloj virtual, entradas, serial, LoRa).
#ifndef SIM_STATE_H
#define SIM_STATE_H

#include <cstdint>
#include <string>
#include <vector>

namespace sim {

// Excepción lanzada cuando se supera el "deadline" (posible bloqueo esperando
// una entrada que nunca llega). El driver la captura y reporta el aviso.
struct Timeout {};

// Tipos de evento programables en la línea de tiempo virtual.
//   EV_INJECT: inyecta un paquete LoRa diferido; 'value' indexa una tabla de
//   paquetes del runner (sim_main) via el hook registrado con setInjectHook().
enum EvKind { EV_POT = 0, EV_MORSE = 1, EV_FINISH = 2, EV_INJECT = 3, EV_BATTERY = 4 };

// --- Reloj virtual ---
uint32_t now();                 // millis() virtuales
long long lockMs();             // reloj comun del modo sincronizado (ms completados desde el arranque; no se reinicia con reboot-cold)
void advance(uint32_t ms);      // avanza el reloj (lo usa delay()); aplica eventos
void setDeadline(uint32_t absMs);
void clearDeadline();
void setPumpHook(void (*fn)(uint32_t ms));  // modo interactivo: el motor lo llama desde cada delay()
void setInjectHook(void (*fn)(int idx));    // fija el handler de los eventos EV_INJECT

// --- Eventos temporizados de entrada ---
void scheduleAt(uint32_t absMs, EvKind kind, int value);  // value: pot 0..1023, botón 0/1
void applyDue();                // aplica los eventos con tiempo <= now()
bool nextEventTime(uint32_t *t);// tiempo del próximo evento pendiente

// --- Entrada inmediata ---
void setPot(int v);
void setMorse(bool down);
void setFinish(bool down);
int  getPot();
bool morseDown();
bool finishDown();

// --- Batería simulada (canal ADC PIN_VBAT; valor crudo 0..1023) ---
void setBatteryRaw(int raw);
int  getBatteryRaw();

// --- Reinicio "en frío": reinicia el reloj virtual a 0 (millis() vuelve a 0),
//     para testear que algo persiste de verdad entre arranques. ---
void resetClock();

// --- Serial capturado ---
void serialPut(char c);
std::string serialLog();
void serialClear();

} // namespace sim

// --- LoRa (implementado en lora_mock.cpp) ---
void simLoraInject(const std::string &packet, int rssi = -42);   // encola un paquete entrante (RX) con RSSI
std::string simLoraLastSent();                    // último paquete transmitido (TX)
std::vector<std::string> simLoraSentRing();       // últimos N TX (para 'expect sent' robusto frente a balizas)
void simLoraSetLoopback(bool on);                 // eco TX -> RX

#endif // SIM_STATE_H
