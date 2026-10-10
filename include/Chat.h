#ifndef CHAT_H
#define CHAT_H

#include <Arduino.h>

// ============================================================
//  Capa de chat sobre LoRa: sobre con emisor + id de mensaje, ACK de entrega,
//  entrega fiable (reintentos + outbox persistente + dedup) y baliza de
//  presencia (con hora y bateria para sincronizar reloj y mostrar al companero).
//
//  Formato (texto plano antes de cifrar):
//    MENSAJE:  0x01 | emisor(1) | msgId(1) | texto...
//    ACK:      0x06 | destino(1) | msgId(1)            (destino = emisor original)
//    BALIZA:   0x02 | emisor(1) | flags(1) | epoch(4, BE) | bateria%(1) | generacionHora(1)
//    DIBUJO:   0x04 | emisor(1) | bytes del lienzo (MSB primero)
//
//  Los marcadores 0x01/0x02/0x04/0x06 no colisionan con los protocolos de los
//  juegos ('T' de Tetris, "HR" de HippoRadar). Un paquete sin sobre = CHAT_OTHER.
// ============================================================

enum ChatKind { CHAT_OTHER = 0, CHAT_MSG = 1, CHAT_ACK = 2, CHAT_BEACON = 3, CHAT_DOODLE = 5 };

// Flags de la baliza
#define BEACON_FLAG_LOWBATT  0x01

// Clasifica un paquete crudo ya descifrado. CHAT_MSG -> text/sender/msgId;
// CHAT_ACK -> sender(=destino)/msgId; CHAT_BEACON -> sender y los campos de
// baliza accesibles via Chat_beacon*().
ChatKind Chat_parse(const String &raw, String &text, uint8_t &sender, uint8_t &msgId);

// Campos de la ULTIMA baliza parseada (validos tras Chat_parse()==CHAT_BEACON).
uint32_t Chat_beaconEpoch();
uint8_t  Chat_beaconBatt();
uint8_t  Chat_beaconFlags();
uint8_t  Chat_beaconGen();     // generacion de ajuste de la hora del emisor (0 si no la lleva)

// Procesa la ultima baliza recibida: presencia + bateria del peer + sync de reloj.
void Chat_handleBeacon();

void Chat_load();                              // setup(): carga la outbox de EEPROM

// Envia un mensaje de chat (cifra/transmite, lo guarda como ENVIADO, lo encola en
// la outbox y arma la espera de ACK). false si fallo la transmision.
bool Chat_send(const String &text);

// Envia un ACK por un mensaje recibido (destino = emisor original).
void Chat_sendAck(uint8_t targetId, uint8_t msgId);

// Envia un dibujo (lienzo cuadrado, MSB primero), fire-and-forget.
void Chat_sendDoodle(const uint8_t *buf32);

// Adelanta la proxima baliza a la siguiente llamada de Chat_tick() (p.ej. tras poner
// la hora a mano: asi el companero la adopta enseguida y no a los 30 s).
void Chat_beaconSoon();

// Tareas periodicas: emite la baliza de presencia y reintenta los mensajes de la
// outbox sin confirmar. Llamar a menudo (desde backgroundTick()).
void Chat_tick();

// --- Confirmacion de entrega del ULTIMO mensaje enviado (pantalla de resultado) ---
bool Chat_awaitingAck();
bool Chat_delivered();
void Chat_noteAck(uint8_t targetId, uint8_t msgId);  // marcar entregado + sacar de la outbox
void Chat_resetPending();

// --- Dedup de recepcion: true si (sender,msgId) ya se vio (y lo registra) ---
bool Chat_seenBefore(uint8_t sender, uint8_t msgId);

// --- Presencia de los companeros (hasta MAX_PEERS a la vez, cada uno por separado) ---
void    Chat_noteHeard(uint8_t peerId);  // llamar al oir CUALQUIER paquete de un peer
bool    Chat_peerOnline();               // hay ALGUN peer oido hace < PRESENCE_TIMEOUT
int     Chat_peersOnline();              // cuantos peers estan en alcance
uint8_t Chat_peerId();                   // id del ultimo peer oido (0 = ninguno)
// Bateria de los peers EN ALCANCE: se resume en el nivel MAS BAJO conocido (0xFF = ninguno la
// comunica) y en "alguno avisa de bateria baja". No depende de quien emitio la ultima
// baliza (antes la pantalla saltaba de un equipo a otro).
uint8_t Chat_peerBatt();
bool    Chat_peerBattLow();

// --- Outbox ---
int  Chat_pendingCount();                // mensajes en la outbox sin confirmar

#endif
