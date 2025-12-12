#ifndef HISTORY_H
#define HISTORY_H

#include <Arduino.h>

// Funciones esenciales con timestamps
void History_load();                      // Llamar en setup()
void History_addMessage(const String &msg);
String History_getMessage(int index);     // 0 = más reciente
unsigned long History_getTimestamp(int index); // Timestamp del mensaje
int History_count();                      // Total mensajes

#endif