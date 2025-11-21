#include "Historial.h"

const int MAX_MESSAGES = 20;
String messageHistory[MAX_MESSAGES];
int messageIndex = 0;

// Añade un mensaje al historial (circular, sobrescribe el más antiguo)
void History_addMessage(const String &msg) {
  messageHistory[messageIndex] = msg;
  messageIndex = (messageIndex + 1) % MAX_MESSAGES;
}

// Devuelve mensaje por índice relativo:
//  index = 0 -> mensaje más reciente
//  index = MAX_MESSAGES-1 -> el más antiguo almacenado
String History_getMessage(int index) {
  if (index < 0 || index >= MAX_MESSAGES) return String("");
  int pos = (messageIndex + MAX_MESSAGES - 1 - index) % MAX_MESSAGES;
  return messageHistory[pos];
}
