#ifndef HISTORY_H
#define HISTORY_H

#include <Arduino.h>

extern const int MAX_MESSAGES;
extern String messageHistory[];
extern int messageIndex;

void History_addMessage(const String &msg);
String History_getMessage(int index);

#endif