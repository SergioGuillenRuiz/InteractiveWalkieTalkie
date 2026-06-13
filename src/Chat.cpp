#include "Chat.h"
#include "MyLora.h"
#include "Historial.h"
#include "Identity.h"

// Marcadores de protocolo (1er byte del texto plano, antes de cifrar).
static const char CHAT_MARK_MSG = 0x01;
static const char CHAT_MARK_ACK = 0x06;

// Contador de id de mensaje propio (1..255, evita 0).
static uint8_t g_nextMsgId = 1;

// Estado de la confirmacion de entrega del ultimo mensaje enviado.
static bool    g_awaiting   = false;
static bool    g_delivered  = false;
static uint8_t g_lastSentId = 0;

static uint8_t nextMsgId() {
  uint8_t id = g_nextMsgId++;
  if (g_nextMsgId == 0) g_nextMsgId = 1;
  return id;
}

ChatKind Chat_parse(const String &raw, String &text, uint8_t &sender, uint8_t &msgId) {
  if (raw.length() >= 3 && raw[0] == CHAT_MARK_MSG) {
    sender = (uint8_t)raw[1];
    msgId  = (uint8_t)raw[2];
    text   = raw.substring(3);
    return CHAT_MSG;
  }
  if (raw.length() >= 3 && raw[0] == CHAT_MARK_ACK) {
    sender = (uint8_t)raw[1];   // destino del ACK (= emisor original)
    msgId  = (uint8_t)raw[2];
    return CHAT_ACK;
  }
  return CHAT_OTHER;
}

bool Chat_send(const String &text) {
  if (text.length() == 0) return false;

  uint8_t mid = nextMsgId();

  // Log legible (el TX cifrado lleva el sobre binario).
  Serial.print("[Chat] Enviando: ");
  Serial.println(text);

  String packet;
  packet += CHAT_MARK_MSG;
  packet += (char)Device_id();
  packet += (char)mid;
  packet += text;

  bool ok = Lora_send(packet);
  if (!ok) return false;

  History_addOutgoing(text);

  g_awaiting   = true;
  g_delivered  = false;
  g_lastSentId = mid;
  return true;
}

void Chat_sendAck(uint8_t targetId, uint8_t msgId) {
  String packet;
  packet += CHAT_MARK_ACK;
  packet += (char)targetId;
  packet += (char)msgId;
  Lora_send(packet);
  Serial.print("[Chat] ACK a #"); Serial.print(targetId);
  Serial.print(" msg "); Serial.println(msgId);
}

bool Chat_awaitingAck() { return g_awaiting; }
bool Chat_delivered()   { return g_delivered; }

void Chat_noteAck(uint8_t targetId, uint8_t msgId) {
  if (g_awaiting && targetId == Device_id() && msgId == g_lastSentId) {
    g_delivered = true;
    Serial.println("[Chat] Confirmado: entregado");
  }
}

void Chat_resetPending() {
  g_awaiting  = false;
  g_delivered = false;
}
