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

int  History_count();
void History_deleteMessage(int index);

// Suma 'delta' segundos a las marcas de tiempo guardadas (y persiste). Se usa cuando
// el reloj SALTA (se pone la hora a mano o se adopta la del companero): asi las
// antiguedades ("hace 3m") no cambian aunque el epoch de referencia sea otro.
void History_shiftTimestamps(int32_t delta);

#endif
