#ifndef CHAT_H
#define CHAT_H

#include <Arduino.h>

// ============================================================
//  Capa de chat sobre LoRa: sobre con emisor + id de mensaje y ACK de entrega.
//
//  Formato (texto plano antes de cifrar):
//    MENSAJE:  0x01 | emisor(1) | msgId(1) | texto...
//    ACK:      0x06 | destino(1) | msgId(1)        (destino = emisor original)
//
//  Los bytes 0x01/0x06 no colisionan con los protocolos de los juegos ('T' de
//  Tetris, "HR" de HippoRadar). Un paquete sin sobre se trata como CHAT_OTHER
//  (mensaje plano/legado).
// ============================================================

enum ChatKind { CHAT_OTHER = 0, CHAT_MSG = 1, CHAT_ACK = 2 };

// Clasifica un paquete crudo ya descifrado. Si es CHAT_MSG rellena text/sender/
// msgId; si es CHAT_ACK rellena sender(=destino) y msgId.
ChatKind Chat_parse(const String &raw, String &text, uint8_t &sender, uint8_t &msgId);

// Envia un mensaje de chat (lo cifra/transmite, lo guarda como ENVIADO en el
// historial y arma la espera de ACK). Devuelve false si fallo la transmision.
bool Chat_send(const String &text);

// Envia un ACK por un mensaje recibido (destino = emisor original).
void Chat_sendAck(uint8_t targetId, uint8_t msgId);

// --- Confirmacion de entrega del ULTIMO mensaje enviado ---
bool Chat_awaitingAck();                       // hay un envio pendiente de confirmar
bool Chat_delivered();                         // el ultimo envio fue confirmado (ACK)
void Chat_noteAck(uint8_t targetId, uint8_t msgId);  // marcar entregado si coincide
void Chat_resetPending();                      // limpiar estado de espera

#endif
