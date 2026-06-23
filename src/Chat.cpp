#include "Chat.h"
#include "MyLora.h"
#include "Historial.h"
#include "Identity.h"
#include "Clock.h"
#include "Battery.h"
#include "Config.h"
#include "EepromMap.h"
#include "States.h"        // mainState / STATE_IDLE / STATE_SLEEP (gating de baliza)
#include <EEPROM.h>

// Marcadores de protocolo (1er byte del texto plano, antes de cifrar).
static const char CHAT_MARK_MSG    = 0x01;
static const char CHAT_MARK_BEACON = 0x02;
static const char CHAT_MARK_NUDGE  = 0x03;
static const char CHAT_MARK_DOODLE = 0x04;
static const char CHAT_MARK_ACK    = 0x06;

// Contador de id de mensaje propio (1..255, evita 0).
static uint8_t g_nextMsgId = 1;

// Estado de la confirmacion de entrega del ultimo mensaje enviado (pantalla de resultado).
static bool    g_awaiting   = false;
static bool    g_delivered  = false;
static uint8_t g_lastSentId = 0;

// --- Campos de la ultima baliza parseada ---
static uint8_t  s_beaconSender = 0;
static uint32_t s_beaconEpoch  = 0;
static uint8_t  s_beaconBatt   = 0xFF;
static uint8_t  s_beaconFlags  = 0;

// --- Dedup de recepcion (anillo de los ultimos N (sender,msgId)) ---
#define SEEN_N 8
static uint8_t g_seenS[SEEN_N];
static uint8_t g_seenM[SEEN_N];
static int     g_seenHead = 0;

// --- Presencia del companero ---
static uint8_t  g_peerId    = 0;
static uint8_t  g_peerBatt  = 0xFF;
static uint32_t g_lastHeard = 0;
static bool     g_everHeard = false;
static bool     g_wasOnline = false;

// --- Baliza periodica ---
static uint32_t g_lastBeacon   = 0;
static bool     g_beaconedOnce = false;

// --- Outbox (mensajes enviados sin confirmar) ---
struct OutItem { uint8_t state; uint8_t msgId; uint8_t target; String text; uint8_t retries; uint32_t nextAt; };
static OutItem g_out[OB_SLOTS];
static const uint8_t OB_MAGIC[3] = { 'O', 'B', '1' };

// ============================================================
//  Utilidades
// ============================================================
static uint8_t nextMsgId() {
  uint8_t id = g_nextMsgId++;
  if (g_nextMsgId == 0) g_nextMsgId = 1;
  return id;
}

static bool txMessage(uint8_t mid, const String &text) {
  String p;
  p += CHAT_MARK_MSG;
  p += (char)Device_id();
  p += (char)mid;
  p += text;
  return Lora_send(p);
}

static void sendBeacon() {
  String p;
  p += CHAT_MARK_BEACON;
  p += (char)Device_id();
  p += (char)(Battery_isLow() ? BEACON_FLAG_LOWBATT : 0);
  uint32_t ep = Clock_now();
  p += (char)((ep >> 24) & 0xFF);
  p += (char)((ep >> 16) & 0xFF);
  p += (char)((ep >> 8) & 0xFF);
  p += (char)(ep & 0xFF);
  p += (char)Battery_percent();
  Lora_send(p);
}

// ============================================================
//  Outbox en EEPROM
// ============================================================
static int  obSlotAddr(int i) { return EE_OUTBOX_BASE + OB_HDR_SIZE + i * OB_SLOT_SIZE; }
static bool obMagicOk() { for (int i = 0; i < 3; i++) if (EEPROM.read(EE_OUTBOX_BASE + i) != OB_MAGIC[i]) return false; return true; }
static void obWriteHeader() { for (int i = 0; i < 3; i++) EEPROM.write(EE_OUTBOX_BASE + i, OB_MAGIC[i]); EEPROM.write(EE_OUTBOX_BASE + 3, 1); }

static void obWriteSlot(int i) {
  int a = obSlotAddr(i);
  EEPROM.write(a + 0, g_out[i].state);
  EEPROM.write(a + 1, g_out[i].msgId);
  EEPROM.write(a + 2, g_out[i].target);
  uint8_t len = (uint8_t)min((size_t)g_out[i].text.length(), (size_t)(OB_MAX_MSG_LEN - 1));
  EEPROM.write(a + 3, len);
  for (uint8_t k = 0; k < len; k++) EEPROM.write(a + 4 + k, (uint8_t)g_out[i].text[k]);
  EEPROM.write(a + 4 + len, '\0');
}

static void obSaveAll() {
  EEPROM.begin(EE_TOTAL_SIZE);          // tamano total: preservar las demas regiones
  obWriteHeader();
  for (int i = 0; i < OB_SLOTS; i++) obWriteSlot(i);
  EEPROM.commit();
  EEPROM.end();
}

static void outboxAdd(uint8_t mid, const String &text) {
  int slot = -1;
  for (int i = 0; i < OB_SLOTS; i++) if (g_out[i].state != 1) { slot = i; break; }
  if (slot < 0) { slot = 0; Serial.println("[Chat] Outbox llena: se descarta el pendiente mas antiguo"); }
  g_out[slot].state   = 1;
  g_out[slot].msgId   = mid;
  g_out[slot].target  = Device_id();
  g_out[slot].text    = text;
  g_out[slot].retries = 0;
  g_out[slot].nextAt  = millis() + MSG_RETRY_MS;
  obSaveAll();
}

static void outboxRemove(uint8_t mid) {
  bool changed = false;
  for (int i = 0; i < OB_SLOTS; i++)
    if (g_out[i].state == 1 && g_out[i].msgId == mid) { g_out[i].state = 0; g_out[i].text = ""; changed = true; }
  if (changed) {
    obSaveAll();
    // Confirmacion de entrega independiente del estado de la pantalla de resultado
    // (ese estado es volatil y no sobrevive a un reinicio; la salida de la outbox si).
    Serial.print("[Chat] Entregado msg "); Serial.println(mid);
  }
}

static void flushOutbox(uint32_t now) {
  for (int i = 0; i < OB_SLOTS; i++)
    if (g_out[i].state == 1) { g_out[i].retries = 0; g_out[i].nextAt = now; }
}

int Chat_pendingCount() { int n = 0; for (int i = 0; i < OB_SLOTS; i++) if (g_out[i].state == 1) n++; return n; }

void Chat_load() {
  // Estado de RAM a valores de arranque. En hardware las estaticas ya arrancan
  // asi; en el simulador un "reboot" re-llama setup() sin reinicializarlas, asi
  // que lo hacemos explicito para que el reinicio sea fiel (sin presencia, dedup
  // ni confirmaciones "fantasma" heredadas de antes del reinicio).
  g_awaiting = false; g_delivered = false; g_lastSentId = 0;
  g_peerId = 0; g_peerBatt = 0xFF; g_lastHeard = 0; g_everHeard = false; g_wasOnline = false;
  g_beaconedOnce = false; g_lastBeacon = 0;
  g_seenHead = 0;
  for (int i = 0; i < SEEN_N; i++) { g_seenS[i] = 0; g_seenM[i] = 0; }
  g_nextMsgId = 1;

  EEPROM.begin(EE_TOTAL_SIZE);
  if (!obMagicOk()) {
    obWriteHeader();
    for (int i = 0; i < OB_SLOTS; i++) { g_out[i].state = 0; g_out[i].text = ""; EEPROM.write(obSlotAddr(i), 0); }
    EEPROM.commit();
    EEPROM.end();
    Serial.println("[Chat] Outbox inicializada");
    return;
  }
  for (int i = 0; i < OB_SLOTS; i++) {
    int a = obSlotAddr(i);
    uint8_t st  = EEPROM.read(a + 0);
    uint8_t mid = EEPROM.read(a + 1);
    uint8_t tg  = EEPROM.read(a + 2);
    uint8_t len = EEPROM.read(a + 3);
    String t = "";
    if (st == 1 && len > 0 && len < OB_MAX_MSG_LEN) {
      for (uint8_t k = 0; k < len; k++) t += (char)EEPROM.read(a + 4 + k);
      g_out[i].state = 1;
    } else {
      g_out[i].state = 0;
    }
    g_out[i].msgId   = mid;
    g_out[i].target  = tg;
    g_out[i].text    = t;
    g_out[i].retries = 0;
    g_out[i].nextAt  = millis() + 2000;   // reintentar poco despues del arranque
  }
  EEPROM.end();
  // Evitar reutilizar un msgId que todavia esta pendiente en la outbox: el
  // contador arranca por encima del mayor pendiente (si no, tras un reinicio un
  // envio nuevo reusaria el id de un mensaje aun sin confirmar y el ACK casaria mal).
  uint8_t maxMid = 0;
  for (int i = 0; i < OB_SLOTS; i++) if (g_out[i].state == 1 && g_out[i].msgId > maxMid) maxMid = g_out[i].msgId;
  if (maxMid != 0) { g_nextMsgId = (uint8_t)(maxMid + 1); if (g_nextMsgId == 0) g_nextMsgId = 1; }

  Serial.print("[Chat] Outbox cargada, pendientes: "); Serial.println(Chat_pendingCount());
}

// ============================================================
//  Parse
// ============================================================
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
  if (raw.length() >= 2 && raw[0] == CHAT_MARK_NUDGE) {
    sender = (uint8_t)raw[1];
    return CHAT_NUDGE;
  }
  if (raw.length() >= 34 && raw[0] == CHAT_MARK_DOODLE) {   // 2 + 32 bytes de lienzo
    sender = (uint8_t)raw[1];
    return CHAT_DOODLE;
  }
  if (raw.length() >= 7 && raw[0] == CHAT_MARK_BEACON) {
    sender         = (uint8_t)raw[1];
    s_beaconSender = (uint8_t)raw[1];
    s_beaconFlags = (uint8_t)raw[2];
    s_beaconEpoch = ((uint32_t)(uint8_t)raw[3] << 24) | ((uint32_t)(uint8_t)raw[4] << 16) |
                    ((uint32_t)(uint8_t)raw[5] << 8)  |  (uint32_t)(uint8_t)raw[6];
    s_beaconBatt  = (raw.length() >= 8) ? (uint8_t)raw[7] : 0xFF;
    msgId = 0;
    return CHAT_BEACON;
  }
  return CHAT_OTHER;
}

uint32_t Chat_beaconEpoch() { return s_beaconEpoch; }
uint8_t  Chat_beaconBatt()  { return s_beaconBatt; }
uint8_t  Chat_beaconFlags() { return s_beaconFlags; }

// ============================================================
//  Envio
// ============================================================
bool Chat_send(const String &text) {
  if (text.length() == 0) return false;

  uint8_t mid = nextMsgId();
  Serial.print("[Chat] Enviando: ");
  Serial.println(text);

  if (!txMessage(mid, text)) return false;

  History_addOutgoing(text);
  outboxAdd(mid, text);          // encolar (persistente) para reintentos/recuperacion

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

void Chat_sendNudge() {
  String packet;
  packet += CHAT_MARK_NUDGE;
  packet += (char)Device_id();
  Lora_send(packet);
  Serial.println("[Nudge] enviado");
}

void Chat_sendDoodle(const uint8_t *buf32) {
  String packet;
  packet += CHAT_MARK_DOODLE;
  packet += (char)Device_id();
  for (int i = 0; i < 32; i++) packet += (char)buf32[i];
  Lora_send(packet);
  Serial.println("[Dibujo] enviado");
}

bool Chat_awaitingAck() { return g_awaiting; }
bool Chat_delivered()   { return g_delivered; }

void Chat_noteAck(uint8_t targetId, uint8_t msgId) {
  if (targetId != Device_id()) return;       // el ACK no es para nosotros
  outboxRemove(msgId);                        // entregado: sacar de la outbox
  if (g_awaiting && msgId == g_lastSentId) {
    g_delivered = true;
    Serial.println("[Chat] Confirmado: entregado");
  }
}

void Chat_resetPending() {
  g_awaiting  = false;
  g_delivered = false;
}

// ============================================================
//  Dedup
// ============================================================
bool Chat_seenBefore(uint8_t sender, uint8_t msgId) {
  for (int i = 0; i < SEEN_N; i++) if (g_seenS[i] == sender && g_seenM[i] == msgId) return true;
  g_seenS[g_seenHead] = sender;
  g_seenM[g_seenHead] = msgId;
  g_seenHead = (g_seenHead + 1) % SEEN_N;
  return false;
}

// ============================================================
//  Presencia
// ============================================================
void Chat_noteHeard(uint8_t peerId) {
  if (peerId == 0 || peerId == Device_id()) return;
  g_peerId    = peerId;
  g_lastHeard = millis();
  g_everHeard = true;
}

bool Chat_peerOnline() {
  if (!g_everHeard) return false;
  return (uint32_t)(millis() - g_lastHeard) < PRESENCE_TIMEOUT_MS;
}

uint8_t Chat_peerId()   { return g_peerId; }
uint8_t Chat_peerBatt() { return g_peerBatt; }

// Procesa la ultima baliza parseada: presencia + bateria del peer + sincronizar
// el reloj con su epoch. (El llamador ya comprobo Chat_parse()==CHAT_BEACON.)
void Chat_handleBeacon() {
  if (s_beaconSender == 0 || s_beaconSender == Device_id()) return;   // ignorar eco propio
  Chat_noteHeard(s_beaconSender);
  g_peerBatt = s_beaconBatt;
  Clock_syncFromPeer(s_beaconEpoch);
}

// ============================================================
//  Tick periodico: balizas + reintentos
// ============================================================
void Chat_tick() {
  uint32_t now = millis();
  Clock_tickPersist();

  // Baliza: solo en estados "ambientales" (IDLE/SLEEP) para no colisionar con los
  // paquetes de juego ni ensuciar el ultimo TX durante un envio.
  bool ambient = (mainState == STATE_IDLE || mainState == STATE_SLEEP);
  if (ambient) {
    if (!g_beaconedOnce && now >= 2000) { sendBeacon(); g_lastBeacon = now; g_beaconedOnce = true; }
    else if (g_beaconedOnce && (uint32_t)(now - g_lastBeacon) >= BEACON_INTERVAL_MS) { sendBeacon(); g_lastBeacon = now; }
  }

  // Presencia: al pasar offline->online, avisar y reactivar los reintentos de la
  // outbox; al pasar online->offline, avisar.
  bool online = Chat_peerOnline();
  if (online && !g_wasOnline) { Serial.println("[Presencia] companero en alcance"); flushOutbox(now); }
  else if (!online && g_wasOnline) Serial.println("[Presencia] companero fuera de alcance");
  g_wasOnline = online;

  // Reintentos de los mensajes sin confirmar.
  for (int i = 0; i < OB_SLOTS; i++) {
    if (g_out[i].state != 1) continue;
    if (now < g_out[i].nextAt) continue;
    if (g_out[i].retries < MSG_RETRY_MAX) {
      txMessage(g_out[i].msgId, g_out[i].text);
      g_out[i].retries++;
      g_out[i].nextAt = now + MSG_RETRY_MS;
      Serial.print("[Chat] Reintento msg "); Serial.print(g_out[i].msgId);
      Serial.print(" ("); Serial.print(g_out[i].retries); Serial.println(")");
    }
  }
}
