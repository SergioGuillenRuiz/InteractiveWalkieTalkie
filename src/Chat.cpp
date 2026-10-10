#include "Chat.h"
#include "MyLora.h"
#include "Historial.h"
#include "Identity.h"
#include "Clock.h"
#include "Battery.h"
#include "Doodle.h"
#include "Config.h"
#include "EepromMap.h"
#include <EEPROM.h>

// Marcadores de protocolo (1er byte del texto plano, antes de cifrar).
static const char CHAT_MARK_MSG    = 0x01;
static const char CHAT_MARK_BEACON = 0x02;
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
static uint8_t  s_beaconGen    = 0;

// --- Dedup de recepcion (anillo de los ultimos N (sender,msgId)) ---
#define SEEN_N 16        // con varios equipos entran mas mensajes y reintentos: ventana mas amplia
static uint8_t g_seenS[SEEN_N];
static uint8_t g_seenM[SEEN_N];
static int     g_seenHead = 0;

// --- Presencia: tabla de companeros (id 0 = hueco libre) ---
struct Peer { uint8_t id; uint32_t lastHeard; uint8_t batt; uint8_t flags; bool wasOnline; };
static Peer     g_peers[MAX_PEERS];
static uint8_t  g_lastPeerId = 0;     // ultimo peer oido (Chat_peerId)
static bool     g_wasOnline  = false; // agregado: habia alguien en alcance

// --- Baliza periodica (con jitter) ---
static uint32_t g_beaconDue = 0;      // millis() en que toca la proxima baliza

// --- ACK pendientes de enviar (cada uno sale tras un retardo aleatorio, ver Config.h) ---
struct PendingAck { bool used; uint8_t target; uint8_t msgId; uint32_t dueAt; };
static PendingAck g_acks[ACK_QUEUE];

// --- Outbox (mensajes enviados sin confirmar) ---
// state: 0 = libre, 1 = mensaje de texto pendiente (text), 2 = dibujo pendiente (doodle).
struct OutItem { uint8_t state; uint8_t msgId; uint8_t target; String text; uint8_t retries; uint32_t nextAt; uint8_t doodle[DOODLE_BYTES]; };
#define OB_TEXT    1
#define OB_DOODLE  2
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

static bool txDoodle(uint8_t mid, const uint8_t *bitmap) {
  String p;
  p += CHAT_MARK_DOODLE;
  p += (char)Device_id();
  p += (char)mid;
  for (int i = 0; i < DOODLE_BYTES; i++) p += (char)bitmap[i];
  return Lora_send(p);
}

// Tiempo hasta el siguiente reintento de un mensaje: MSG_RETRY_MS +-MSG_RETRY_JITTER_MS.
static uint32_t nextRetryDelay() {
  return MSG_RETRY_MS - MSG_RETRY_JITTER_MS + (uint32_t)random(2 * MSG_RETRY_JITTER_MS + 1);
}

// Intervalo hasta la proxima baliza: BEACON_INTERVAL_MS +-BEACON_JITTER_MS.
static uint32_t nextBeaconDelay() {
  return BEACON_INTERVAL_MS - BEACON_JITTER_MS + (uint32_t)random(2 * BEACON_JITTER_MS + 1);
}

static void sendBeacon() {
  Serial.println("[Chat] Baliza");
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
  p += (char)Clock_gen();               // generacion de ajuste de la hora (ver Clock.h)
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
  if (g_out[i].state == OB_DOODLE) {                // el lienzo ocupa el hueco del texto (bytes crudos)
    EEPROM.write(a + 3, DOODLE_BYTES);
    for (uint8_t k = 0; k < DOODLE_BYTES; k++) EEPROM.write(a + 4 + k, g_out[i].doodle[k]);
    EEPROM.write(a + 4 + DOODLE_BYTES, '\0');
    return;
  }
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

static void outboxAdd(uint8_t mid, const String &text, const uint8_t *doodle = nullptr) {
  int slot = -1;
  for (int i = 0; i < OB_SLOTS; i++) if (g_out[i].state == 0) { slot = i; break; }
  if (slot < 0) { slot = 0; Serial.println("[Chat] Outbox llena: se descarta el pendiente mas antiguo"); }
  g_out[slot].state   = doodle ? OB_DOODLE : OB_TEXT;
  g_out[slot].msgId   = mid;
  g_out[slot].target  = Device_id();
  g_out[slot].text    = text;
  if (doodle) memcpy(g_out[slot].doodle, doodle, DOODLE_BYTES);
  g_out[slot].retries = 0;
  g_out[slot].nextAt  = millis() + nextRetryDelay();
  obSaveAll();
}

static void outboxRemove(uint8_t mid) {
  bool changed = false;
  for (int i = 0; i < OB_SLOTS; i++)
    if (g_out[i].state != 0 && g_out[i].msgId == mid) { g_out[i].state = 0; g_out[i].text = ""; changed = true; }
  if (changed) {
    obSaveAll();
    // Confirmacion de entrega independiente del estado de la pantalla de resultado
    // (ese estado es volatil y no sobrevive a un reinicio; la salida de la outbox si).
    Serial.print("[Chat] Entregado msg "); Serial.println(mid);
  }
}

static void flushOutbox(uint32_t now) {
  for (int i = 0; i < OB_SLOTS; i++)
    if (g_out[i].state != 0) { g_out[i].retries = 0; g_out[i].nextAt = now; }
}

int Chat_pendingCount() { int n = 0; for (int i = 0; i < OB_SLOTS; i++) if (g_out[i].state != 0) n++; return n; }

void Chat_load() {
  // Estado de RAM a valores de arranque. En hardware las estaticas ya arrancan
  // asi; en el simulador un "reboot" re-llama setup() sin reinicializarlas, asi
  // que lo hacemos explicito para que el reinicio sea fiel (sin presencia, dedup
  // ni confirmaciones "fantasma" heredadas de antes del reinicio).
  g_awaiting = false; g_delivered = false; g_lastSentId = 0;
  for (int i = 0; i < MAX_PEERS; i++) g_peers[i].id = 0;
  g_lastPeerId = 0; g_wasOnline = false;
  for (int i = 0; i < ACK_QUEUE; i++) g_acks[i].used = false;
  g_beaconDue = millis() + 2000 + (uint32_t)random(2000);   // 1a baliza a los 2-4 s del arranque
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
    if (st == OB_TEXT && len > 0 && len < OB_MAX_MSG_LEN) {
      for (uint8_t k = 0; k < len; k++) t += (char)EEPROM.read(a + 4 + k);
      g_out[i].state = OB_TEXT;
    } else if (st == OB_DOODLE && len == DOODLE_BYTES) {
      for (uint8_t k = 0; k < DOODLE_BYTES; k++) g_out[i].doodle[k] = EEPROM.read(a + 4 + k);
      g_out[i].state = OB_DOODLE;
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
  for (int i = 0; i < OB_SLOTS; i++) if (g_out[i].state != 0 && g_out[i].msgId > maxMid) maxMid = g_out[i].msgId;
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
  if (raw.length() >= DOODLE_PAYLOAD_OFFSET + DOODLE_BYTES && raw[0] == CHAT_MARK_DOODLE) {
    sender = (uint8_t)raw[1];
    msgId  = (uint8_t)raw[2];
    return CHAT_DOODLE;
  }
  if (raw.length() >= 7 && raw[0] == CHAT_MARK_BEACON) {
    sender         = (uint8_t)raw[1];
    s_beaconSender = (uint8_t)raw[1];
    s_beaconFlags = (uint8_t)raw[2];
    s_beaconEpoch = ((uint32_t)(uint8_t)raw[3] << 24) | ((uint32_t)(uint8_t)raw[4] << 16) |
                    ((uint32_t)(uint8_t)raw[5] << 8)  |  (uint32_t)(uint8_t)raw[6];
    s_beaconBatt  = (raw.length() >= 8) ? (uint8_t)raw[7] : 0xFF;
    s_beaconGen   = (raw.length() >= 9) ? (uint8_t)raw[8] : 0;
    msgId = 0;
    return CHAT_BEACON;
  }
  return CHAT_OTHER;
}

uint32_t Chat_beaconEpoch() { return s_beaconEpoch; }
uint8_t  Chat_beaconBatt()  { return s_beaconBatt; }
uint8_t  Chat_beaconFlags() { return s_beaconFlags; }
uint8_t  Chat_beaconGen()   { return s_beaconGen; }

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

// El ACK no sale al instante: si varios equipos reciben el mismo mensaje, sus ACK saldrian a la vez y se
// pisarian en el emisor (que no oiria ninguno). Cada uno espera un retardo aleatorio; ver Chat_tick().
void Chat_queueAck(uint8_t targetId, uint8_t msgId) {
  for (int i = 0; i < ACK_QUEUE; i++)
    if (g_acks[i].used && g_acks[i].target == targetId && g_acks[i].msgId == msgId) return;   // ya en camino
  int slot = -1;
  for (int i = 0; i < ACK_QUEUE; i++) if (!g_acks[i].used) { slot = i; break; }
  if (slot < 0) {                                  // cola llena: sale ya el mas antiguo para hacer sitio
    slot = 0;
    for (int i = 1; i < ACK_QUEUE; i++) if ((int32_t)(g_acks[i].dueAt - g_acks[slot].dueAt) < 0) slot = i;
    Chat_sendAck(g_acks[slot].target, g_acks[slot].msgId);
  }
  g_acks[slot].used   = true;
  g_acks[slot].target = targetId;
  g_acks[slot].msgId  = msgId;
  g_acks[slot].dueAt  = millis() + ACK_DELAY_MIN_MS + (uint32_t)random(ACK_DELAY_MAX_MS - ACK_DELAY_MIN_MS + 1);
}

bool Chat_sendDoodle(const uint8_t *bitmap) {
  uint8_t mid = nextMsgId();
  if (!txDoodle(mid, bitmap)) return false;
  Serial.print("[Dibujo] enviado msg "); Serial.println(mid);

  History_addOutgoingDoodle(bitmap);
  outboxAdd(mid, "", bitmap);    // encolar (persistente) para reintentos/recuperacion, como un mensaje de texto

  g_awaiting   = true;
  g_delivered  = false;
  g_lastSentId = mid;
  return true;
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
static bool peerAlive(const Peer &p, uint32_t now) {
  return p.id != 0 && (uint32_t)(now - p.lastHeard) < PRESENCE_TIMEOUT_MS;
}

static Peer *findPeer(uint8_t id) {
  for (int i = 0; i < MAX_PEERS; i++) if (g_peers[i].id == id) return &g_peers[i];
  return nullptr;
}

// Entrada del peer; si es nuevo, un hueco libre o, si no hay, el que lleva mas tiempo sin oirse.
static Peer *touchPeer(uint8_t id) {
  Peer *p = findPeer(id);
  if (p) return p;
  uint32_t now = millis();
  int slot = -1;
  for (int i = 0; i < MAX_PEERS; i++) if (g_peers[i].id == 0) { slot = i; break; }
  if (slot < 0) {                                   // tabla llena: sustituir el mas antiguo
    uint32_t worst = 0;
    for (int i = 0; i < MAX_PEERS; i++) {
      uint32_t age = (uint32_t)(now - g_peers[i].lastHeard);
      if (slot < 0 || age > worst) { worst = age; slot = i; }
    }
    Serial.print("[Presencia] tabla llena: se olvida el equipo #"); Serial.println(g_peers[slot].id);
  }
  g_peers[slot].id = id;
  g_peers[slot].lastHeard = now;
  g_peers[slot].batt = 0xFF;
  g_peers[slot].flags = 0;
  g_peers[slot].wasOnline = false;
  return &g_peers[slot];
}

void Chat_noteHeard(uint8_t peerId) {
  if (peerId == 0 || peerId == Device_id()) return;
  Peer *p = touchPeer(peerId);
  p->lastHeard = millis();
  g_lastPeerId = peerId;
}

int Chat_peersOnline() {
  uint32_t now = millis();
  int n = 0;
  for (int i = 0; i < MAX_PEERS; i++) if (peerAlive(g_peers[i], now)) n++;
  return n;
}

bool Chat_peerOnline() { return Chat_peersOnline() > 0; }

uint8_t Chat_peerId() { return g_lastPeerId; }

uint8_t Chat_peerBatt() {
  uint32_t now = millis();
  uint8_t lowest = 0xFF;
  for (int i = 0; i < MAX_PEERS; i++)
    if (peerAlive(g_peers[i], now) && g_peers[i].batt <= 100 && (lowest == 0xFF || g_peers[i].batt < lowest))
      lowest = g_peers[i].batt;
  return lowest;
}

bool Chat_peerBattLow() {
  uint32_t now = millis();
  for (int i = 0; i < MAX_PEERS; i++)
    if (peerAlive(g_peers[i], now) && (g_peers[i].flags & BEACON_FLAG_LOWBATT)) return true;
  return false;
}

// Procesa la ultima baliza parseada: presencia + bateria del peer + sincronizar
// el reloj con su epoch. (El llamador ya comprobo Chat_parse()==CHAT_BEACON.)
void Chat_handleBeacon() {
  if (s_beaconSender == 0 || s_beaconSender == Device_id()) return;   // ignorar eco propio
  Chat_noteHeard(s_beaconSender);
  Peer *p = findPeer(s_beaconSender);
  if (p) { p->batt = s_beaconBatt; p->flags = s_beaconFlags; }
  // Hora del peer. Si es de una generacion de ajuste MAS RECIENTE el reloj salta: se
  // reajustan las marcas de tiempo guardadas para que las antiguedades no cambien.
  int32_t step = Clock_syncFromPeer(s_beaconEpoch, s_beaconGen);
  if (step != 0) History_shiftTimestamps(step);
}

void Chat_beaconSoon() {
  g_beaconDue = millis();                         // ya toca emitir
}

// ============================================================
//  Tick periodico: balizas + reintentos
// ============================================================
void Chat_tick() {
  uint32_t now = millis();
  Clock_tickPersist();

  // Como mucho UNA emision por vuelta: cada una bloquea 120-350 ms (el tiempo en el aire) y encadenar dos
  // (un ACK y la baliza, p.ej.) dejaria al equipo sordo a los botones el doble: se puede perder una pulsacion
  // corta. Lo que no cabe espera a la vuelta siguiente (unos ms despues, ya con los botones atendidos).
  bool txDone = false;

  // ACK pendientes cuyo retardo ya paso (en el radar de largo alcance, SF10, nadie del chat oye: esperan).
  if (!Lora_isAsleep() && !Lora_rangeMode()) {
    for (int i = 0; i < ACK_QUEUE; i++) {
      if (!g_acks[i].used || (int32_t)(now - g_acks[i].dueAt) < 0) continue;
      g_acks[i].used = false;
      Chat_sendAck(g_acks[i].target, g_acks[i].msgId);
      txDone = true;
      break;
    }
  }

  // Balizas y reintentos salen en cualquier estado, tambien durante una partida (la emision es asincrona y
  // escucha antes de hablar, asi que no cuelga el juego ni pisa sus paquetes). No salen con la radio dormida
  // (ahorro opcional) ni en el radar de largo alcance (HippoRadar): ahi la radio va en SF10 y el chat, en SF7,
  // no se oye en ninguno de los dos sentidos; al volver del radar se anuncia (Chat_beaconSoon) y reintenta.
  bool radioOn = !Lora_isAsleep() && !Lora_rangeMode();
  // (Escuchar antes de hablar: si entra una trama o hay un paquete sin leer, la baliza espera unos ms
  //  en vez de destruirlo; g_beaconDue no se toca y se reintenta en la siguiente vuelta.)
  if (radioOn && !txDone && (int32_t)(now - g_beaconDue) >= 0 && !Lora_busy()) {
    sendBeacon();
    g_beaconDue = now + nextBeaconDelay();
    txDone = true;
  }

  // Presencia, equipo por equipo: al pasar offline->online se avisa y se reactivan los
  // reintentos de la outbox (puede ser justo quien no recibio un mensaje); al pasar
  // online->offline, se avisa. Ademas, el agregado "algun companero en alcance".
  int nOnline = 0;
  for (int i = 0; i < MAX_PEERS; i++) {
    if (g_peers[i].id == 0) continue;
    bool alive = peerAlive(g_peers[i], now);
    if (alive && !g_peers[i].wasOnline) {
      Serial.print("[Presencia] equipo #"); Serial.print(g_peers[i].id); Serial.println(" en alcance");
      flushOutbox(now);
    } else if (!alive && g_peers[i].wasOnline) {
      Serial.print("[Presencia] equipo #"); Serial.print(g_peers[i].id); Serial.println(" fuera de alcance");
    }
    g_peers[i].wasOnline = alive;
    if (alive) nOnline++;
  }
  bool online = nOnline > 0;
  if (online && !g_wasOnline) Serial.println("[Presencia] companero en alcance");
  else if (!online && g_wasOnline) Serial.println("[Presencia] companero fuera de alcance");
  g_wasOnline = online;

  // Reintentos de los mensajes sin confirmar.
  for (int i = 0; i < OB_SLOTS; i++) {
    if (g_out[i].state == 0 || !radioOn) continue;
    if (now < g_out[i].nextAt) continue;
    if (txDone || Lora_busy()) break;     // ya se emitio en esta vuelta / entra algo: el reintento espera a la siguiente
    if (g_out[i].retries < MSG_RETRY_MAX) {
      if (g_out[i].state == OB_DOODLE) txDoodle(g_out[i].msgId, g_out[i].doodle);
      else                             txMessage(g_out[i].msgId, g_out[i].text);
      g_out[i].retries++;
      g_out[i].nextAt = now + nextRetryDelay();
      txDone = true;
      Serial.print("[Chat] Reintento msg "); Serial.print(g_out[i].msgId);
      Serial.print(" ("); Serial.print(g_out[i].retries); Serial.println(")");
    }
  }
}
