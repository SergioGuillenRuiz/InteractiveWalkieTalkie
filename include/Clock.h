#ifndef CLOCK_H
#define CLOCK_H

#include <Arduino.h>

// ============================================================
//  Reloj de pared aproximado y COMPARTIDO entre los dos equipos.
//
//  No hay RTC. Se mantiene un "epoch base" (segundos) en RAM:
//      Clock_now() = base + (millis() - millisAtBase)/1000
//  El base se PERSISTE en EEPROM (sobrevive a reinicios) y se SINCRONIZA con el
//  companero por radio: cada baliza/mensaje lleva la hora del emisor.
//
//  La hora la fija una PERSONA (menu "Poner la hora", ver ClockSetup.h): es la
//  hora LOCAL tal como se ve en cualquier reloj, asi que no hay zona horaria ni
//  horario de verano que gestionar (si cambia, se vuelve a poner).
//
//  GENERACION DE AJUSTE. Para que la hora puesta a mano gane siempre (incluso si
//  se corrige HACIA ATRAS) cada ajuste manual sube un contador de 1 byte
//  (1..255, con vuelta; 0 = nunca se ha fijado) que viaja en la baliza:
//    - generacion del peer MAS RECIENTE que la propia -> se adopta su hora tal cual;
//    - generacion MAS ANTIGUA -> se ignora (ya nos adoptara el el a nosotros);
//    - IGUAL -> se adopta la MAYOR (reloj monotono; absorbe la deriva de +-1 s).
//
//  Si nunca se ha fijado una hora real (generacion 0), el "epoch" es simplemente
//  el tiempo de funcionamiento acumulado: da antiguedades utiles, pero la hora
//  que se muestra es "--:--".
// ============================================================

void     Clock_load();                 // setup(): restaura el epoch y la generacion de EEPROM
uint32_t Clock_now();                  // segundos epoch actuales

// Ajuste AUTORITATIVO del epoch (sube la generacion y persiste). Devuelve el salto
// aplicado (nuevo - anterior, en segundos): quien llama debe reajustar con el las
// marcas de tiempo guardadas (History_shiftTimestamps) para que las antiguedades
// no cambien. (El simulador lo usa para el comando "settime".)
int32_t  Clock_set(uint32_t epoch);

// Ajuste manual de la hora del dia (la que teclea el usuario): conserva el dia y
// pone los segundos a 0. Devuelve el salto igual que Clock_set().
int32_t  Clock_setTimeOfDay(int hour, int minute);

// Sincronizacion con la baliza de un peer (ver "generacion de ajuste"). Devuelve
// el salto aplicado cuando se adopta una generacion MAS RECIENTE (0 en cualquier
// otro caso: ignorada, o convergencia de deriva dentro de la misma generacion).
int32_t  Clock_syncFromPeer(uint32_t peerEpoch, uint8_t peerGen);

uint8_t  Clock_gen();                  // generacion de ajuste actual (0 = nunca fijada)
bool     Clock_isTimeSet();            // true si alguien fijo la hora real (generacion != 0)
void     Clock_tickPersist();          // persiste el epoch de vez en cuando (desde el tick)
bool     Clock_isSynced();             // true si hay un epoch fiable para las antiguedades
String   Clock_hhmm();                 // "HH:MM" para mostrar ("--:--" si nunca se ha fijado)

#endif
