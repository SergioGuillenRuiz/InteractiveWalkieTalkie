#ifndef CLOCK_H
#define CLOCK_H

#include <Arduino.h>

// ============================================================
//  Reloj de pared aproximado y COMPARTIDO entre los dos equipos.
//
//  No hay RTC. Se mantiene un "epoch base" (segundos) en RAM:
//      Clock_now() = base + (millis() - millisAtBase)/1000
//  El base se PERSISTE en EEPROM (sobrevive a reinicios) y se SINCRONIZA con el
//  companero por radio: cada baliza/mensaje lleva la hora del emisor y se adopta
//  la MAYOR (reloj monotono y convergente). Asi las antiguedades del historial
//  dejan de perderse al reiniciar (ya no "--") y coinciden entre equipos.
//
//  Si nunca se ha fijado ni sincronizado una hora real, el "epoch" es
//  simplemente el tiempo de funcionamiento acumulado: sigue dando antiguedades
//  utiles y consistentes dentro de y entre sesiones.
// ============================================================

void     Clock_load();                 // setup(): restaura el epoch de EEPROM
uint32_t Clock_now();                  // segundos epoch actuales
void     Clock_set(uint32_t epoch);    // fija la hora (p.ej. desde un peer fiable) y persiste
void     Clock_syncFromPeer(uint32_t peerEpoch);  // adopta la hora del peer si es mayor
void     Clock_tickPersist();          // persiste el epoch de vez en cuando (desde el tick)
bool     Clock_isSynced();             // true si la hora se fijo o sincronizo con un peer
String   Clock_hhmm();                 // "HH:MM" para mostrar

#endif
