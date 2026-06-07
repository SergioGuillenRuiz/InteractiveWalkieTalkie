// API interna del motor del simulador (reloj virtual, entradas, serial, LoRa).
#ifndef SIM_STATE_H
#define SIM_STATE_H

#include <cstdint>
#include <string>

namespace sim {

// Excepción lanzada cuando se supera el "deadline" (posible bloqueo esperando
// una entrada que nunca llega). El driver la captura y reporta el aviso.
struct Timeout {};

// Tipos de evento programables en la línea de tiempo virtual.
enum EvKind { EV_POT = 0, EV_MORSE = 1, EV_FINISH = 2 };

// --- Reloj virtual ---
uint32_t now();                 // millis() virtuales
void advance(uint32_t ms);      // avanza el reloj (lo usa delay()); aplica eventos
void setDeadline(uint32_t absMs);
void clearDeadline();

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

// --- Serial capturado ---
void serialPut(char c);
std::string serialLog();
void serialClear();

} // namespace sim

// --- LoRa (implementado en lora_mock.cpp) ---
void simLoraInject(const std::string &packet);   // encola un paquete entrante (RX)
std::string simLoraLastSent();                    // último paquete transmitido (TX)
void simLoraSetLoopback(bool on);                 // eco TX -> RX

#endif // SIM_STATE_H
