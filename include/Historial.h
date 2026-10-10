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
