#include "Historial.h"

const int MAX_MESSAGES = 20;
String messageHistory[MAX_MESSAGES];
unsigned long messageTime[MAX_MESSAGES];
int messageIndex = 0;
int messageCount = 0;

// Añade un mensaje al historial (circular, sobrescribe el más antiguo)
void History_addMessage(const String &msg) {
  messageHistory[messageIndex] = msg;
  messageTime[messageIndex] = millis();        
  messageIndex = (messageIndex + 1) % MAX_MESSAGES;
  if (messageCount < MAX_MESSAGES) messageCount++;
}

String History_getMessage(int index) {
  if (index < 0 || index >= MAX_MESSAGES) return String("");
  int pos = (messageIndex + MAX_MESSAGES - 1 - index) % MAX_MESSAGES;
  return messageHistory[pos];
}

unsigned long History_getTimestamp(int index) {
  if (index < 0 || index >= messageCount) return 0;
  int pos = (messageIndex + MAX_MESSAGES - 1 - index) % MAX_MESSAGES;
  return messageTime[pos];
}

int History_count() {
  return messageCount;
}
