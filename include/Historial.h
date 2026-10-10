#ifndef HISTORY_H
#define HISTORY_H

#include <Arduino.h>

// ============================================================
//  Historial de mensajes (recibidos y enviados)
//  indice 0 = mas reciente
// ============================================================
void History_load();                          // Llamar en setup()

void History_addIncoming(const String &msg, uint8_t sender);  // recibido (sender 0 = desconocido)
void History_addOutgoing(const String &msg);                  // enviado por mi
void History_addMessage(const String &msg);                   // compat -> recibido, sender desconocido

// --- Dibujos (lienzo 24x24, DOODLE_BYTES bytes, ver Doodle.h) --------------------------------------
// Un dibujo es un registro mas del historial (texto "[dibujo]"): lleva su propio sello de tiempo, su
// emisor y sus flags (enviado / no leido) y el lienzo viaja DENTRO del registro, tanto en RAM como en la
// EEPROM, asi que se conserva tras reiniciar y se descarta o se borra a la vez que su registro.
void History_addIncomingDoodle(const uint8_t *bitmap, uint8_t sender);
void History_addOutgoingDoodle(const uint8_t *bitmap);
bool           History_isDoodle(int index);          // el registro es un dibujo con su lienzo guardado
const uint8_t *History_getDoodle(int index);         // DOODLE_BYTES bytes, o nullptr si no es un dibujo

String        History_getMessage(int index);
unsigned long History_getTimestamp(int index);
bool          History_isOutgoing(int index);   // true = lo enviaste tu
uint8_t       History_getSender(int index);    // id del emisor (0 = desconocido / propio)
bool          History_isFromThisBoot(int index); // true = recibido/enviado en esta sesion (antiguedad fiable)

// --- No leidos --------------------------------------------------------------
// Todo mensaje RECIBIDO entra como "no leido" (bit persistente en EEPROM: sobrevive a un
// reinicio); los enviados nunca lo estan. Indices como el resto de la API (0 = mas reciente).
int  History_unreadCount();               // cuantos mensajes recibidos siguen sin leer
bool History_isUnread(int index);
void History_markRead(int index);         // marca UNO como leido
void History_markReadUpTo(int index);     // marca como leidos los mas recientes 0..index

int  History_count();
void History_deleteMessage(int index);

// Suma 'delta' segundos a las marcas de tiempo guardadas (y persiste). Se usa cuando
// el reloj SALTA (se pone la hora a mano o se adopta la del companero): asi las
// antiguedades ("hace 3m") no cambian aunque el epoch de referencia sea otro.
void History_shiftTimestamps(int32_t delta);

#endif
