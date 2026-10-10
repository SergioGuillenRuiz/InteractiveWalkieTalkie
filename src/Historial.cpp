#include "Historial.h"
#include <EEPROM.h>
#include "EepromMap.h"
#include "Clock.h"
#include "Doodle.h"

// ============================================================
// CONFIGURACION
// ============================================================
#define MAX_MSG_LENGTH 100                       // chars por mensaje (incluye terminador)
#define MAX_MESSAGES   10                        // mensajes maximo

// Cabecera: 3 bytes de magia + 1 de version. Si no coincide al cargar, la EEPROM
// es de un formato antiguo (o esta sin inicializar) y se reformatea vacia.
#define HDR_SIZE       4
static const uint8_t MAGIC[3] = { 'W', 'M', '2' };   // Walkie Messages v2
#define FORMAT_VERSION 1

// Cada slot: 4B timestamp + 1B flags + 1B sender + 1B longitud + texto + '\0'
#define MSG_SLOT_SIZE  (4 + 1 + 1 + 1 + MAX_MSG_LENGTH)
#define EEPROM_SIZE    (HDR_SIZE + MSG_SLOT_SIZE * MAX_MESSAGES)

// El mapa de EEPROM (EepromMap.h) asume que esta region acaba en EE_HISTORIAL_END.
// Si se toca MAX_MESSAGES/MAX_MSG_LENGTH y deja de cuadrar, ajusta EE_HISTORIAL_END.
static_assert(EEPROM_SIZE == EE_HISTORIAL_END, "EE_HISTORIAL_END no coincide con EEPROM_SIZE");

#define FLAG_OUTGOING  0x01                       // bit0: el mensaje lo envie yo
#define FLAG_UNREAD    0x02                       // bit1: recibido y aun sin leer (persistente)
#define FLAG_DOODLE    0x04                       // bit2: es un dibujo: el hueco del texto guarda el lienzo (DOODLE_BYTES)

static_assert(DOODLE_BYTES <= MAX_MSG_LENGTH - 1, "el lienzo de un dibujo no cabe en el hueco del texto del registro");
static const char DOODLE_TEXT[] = "[dibujo]";         // lo que ve la lista del historial

// Historial en RAM (cronologico): 0 = mas antiguo, count-1 = mas reciente.
// (la API expone el indice 0 como el mas RECIENTE)
static String        messageHistory[MAX_MESSAGES];
static unsigned long messageTime[MAX_MESSAGES];
static uint8_t       messageFlags[MAX_MESSAGES];
static uint8_t       messageSender[MAX_MESSAGES];
static uint8_t       messageDoodle[MAX_MESSAGES][DOODLE_BYTES];   // lienzo de los registros con FLAG_DOODLE
static bool          messageThisBoot[MAX_MESSAGES];   // RAM: recibido/enviado en esta sesion
static int           messageCount = 0;

// ============================================================
// EEPROM (asumen EEPROM.begin() ya llamado)
// ============================================================
static int slotAddr(int slot) { return HDR_SIZE + slot * MSG_SLOT_SIZE; }

static void writeSlot(int slot, const String &msg, unsigned long ts, uint8_t flags, uint8_t sender,
                      const uint8_t *doodle = nullptr) {
    int addr = slotAddr(slot);

    EEPROM.write(addr++, (ts >> 24) & 0xFF);
    EEPROM.write(addr++, (ts >> 16) & 0xFF);
    EEPROM.write(addr++, (ts >> 8) & 0xFF);
    EEPROM.write(addr++, ts & 0xFF);

    EEPROM.write(addr++, flags);
    EEPROM.write(addr++, sender);

    if ((flags & FLAG_DOODLE) && doodle) {             // dibujo: el lienzo ocupa el hueco del texto
        EEPROM.write(addr++, DOODLE_BYTES);
        for (uint8_t i = 0; i < DOODLE_BYTES; i++) EEPROM.write(addr + i, doodle[i]);
        EEPROM.write(addr + DOODLE_BYTES, '\0');
        return;
    }

    uint8_t len = (uint8_t)min((size_t)msg.length(), (size_t)(MAX_MSG_LENGTH - 1));
    EEPROM.write(addr++, len);

    for (uint8_t i = 0; i < len; i++) EEPROM.write(addr + i, (uint8_t)msg[i]);
    EEPROM.write(addr + len, '\0');
}

static bool readSlot(int slot, String &msg, unsigned long &ts, uint8_t &flags, uint8_t &sender, uint8_t *doodle) {
    int addr = slotAddr(slot);

    ts  = (unsigned long)EEPROM.read(addr++) << 24;
    ts |= (unsigned long)EEPROM.read(addr++) << 16;
    ts |= (unsigned long)EEPROM.read(addr++) << 8;
    ts |= (unsigned long)EEPROM.read(addr++);

    if (ts == 0xFFFFFFFF) return false;   // slot vacio

    flags  = EEPROM.read(addr++);
    sender = EEPROM.read(addr++);

    uint8_t len = EEPROM.read(addr++);
    if (len == 0 || len > MAX_MSG_LENGTH - 1) return false;

    if (flags & FLAG_DOODLE) {                         // dibujo: bytes del lienzo (pueden valer 0)
        if (len != DOODLE_BYTES) return false;
        for (uint8_t i = 0; i < DOODLE_BYTES; i++) doodle[i] = EEPROM.read(addr + i);
        msg = DOODLE_TEXT;
        return true;
    }

    msg = "";
    for (uint8_t i = 0; i < len; i++) {
        char c = EEPROM.read(addr + i);
        if (c == '\0') break;
        msg += c;
    }
    return true;
}

static bool magicOk() {
    for (int i = 0; i < 3; i++) if (EEPROM.read(i) != MAGIC[i]) return false;
    return true;
}

static void writeHeader() {
    for (int i = 0; i < 3; i++) EEPROM.write(i, MAGIC[i]);
    EEPROM.write(3, FORMAT_VERSION);
}

// Persiste todo el historial en una sola operacion de flash.
static void saveAll() {
    EEPROM.begin(EE_TOTAL_SIZE);   // tamano total: preservar la region del Frasero
    writeHeader();
    for (int i = 0; i < MAX_MESSAGES; i++) {
        if (i < messageCount) writeSlot(i, messageHistory[i], messageTime[i], messageFlags[i], messageSender[i], messageDoodle[i]);
        else                  writeSlot(i, "", 0xFFFFFFFF, 0, 0);   // marcar vacio
    }
    EEPROM.commit();
    EEPROM.end();
}

// ============================================================
// API PUBLICA
// ============================================================
void History_load() {
    messageCount = 0;

    EEPROM.begin(EE_TOTAL_SIZE);   // tamano total: preservar la region del Frasero

    if (!magicOk()) {
        // Formato antiguo o EEPROM virgen: reformatear vacio.
        writeHeader();
        for (int i = 0; i < MAX_MESSAGES; i++) writeSlot(i, "", 0xFFFFFFFF, 0, 0);
        EEPROM.commit();
        EEPROM.end();
        Serial.println("[Historial] EEPROM inicializada (formato nuevo)");
        return;
    }

    for (int i = 0; i < MAX_MESSAGES; i++) {
        String msg; unsigned long ts; uint8_t flags, sender;
        if (readSlot(i, msg, ts, flags, sender, messageDoodle[messageCount])) {
            messageHistory[messageCount] = msg;
            messageTime[messageCount]    = ts;
            messageFlags[messageCount]   = flags;
            messageSender[messageCount]  = sender;
            messageThisBoot[messageCount] = false;   // cargado de antes -> antiguedad no fiable
            messageCount++;
        }
    }
    EEPROM.end();

    Serial.print("[Historial] Cargados ");
    Serial.print(messageCount);
    Serial.println(" mensajes");
}

// Nucleo de insercion: anade un mensaje con sus metadatos y lo persiste.
// Mueve el registro 'src' al hueco 'dst' (todas sus columnas, lienzo incluido).
static void moveEntry(int dst, int src) {
    messageHistory[dst]  = messageHistory[src];
    messageTime[dst]     = messageTime[src];
    messageFlags[dst]    = messageFlags[src];
    messageSender[dst]   = messageSender[src];
    messageThisBoot[dst] = messageThisBoot[src];
    if (messageFlags[src] & FLAG_DOODLE) memcpy(messageDoodle[dst], messageDoodle[src], DOODLE_BYTES);
}

static void addEntry(const String &msg, uint8_t flags, uint8_t sender, const uint8_t *doodle = nullptr) {
    if (msg.length() == 0) return;
    if (doodle) flags |= FLAG_DOODLE;

    String shortMsg = msg;
    if (shortMsg.length() > MAX_MSG_LENGTH - 1)
        shortMsg = shortMsg.substring(0, MAX_MSG_LENGTH - 1);

    // Marca de tiempo en SEGUNDOS epoch del reloj compartido (Clock), no millis():
    // asi la antiguedad sobrevive a reinicios y coincide entre los dos equipos.
    // 0xFFFFFFFF es el centinela de "slot vacio" en EEPROM, asi que se evita.
    unsigned long ts = Clock_now();
    if (ts == 0xFFFFFFFFUL) ts = 0xFFFFFFFEUL;

    if (messageCount < MAX_MESSAGES) {
        // Hay hueco: anadir al final y guardar solo ese slot
        int i = messageCount;
        messageHistory[i]  = shortMsg;
        messageTime[i]     = ts;
        messageFlags[i]    = flags;
        messageSender[i]   = sender;
        messageThisBoot[i] = true;
        if (doodle) memcpy(messageDoodle[i], doodle, DOODLE_BYTES);
        messageCount++;

        EEPROM.begin(EE_TOTAL_SIZE);   // tamano total: preservar la region del Frasero
        writeHeader();
        writeSlot(i, shortMsg, ts, flags, sender, doodle);
        EEPROM.commit();
        EEPROM.end();
    } else {
        // Buffer lleno: descartar el mas antiguo y desplazar el resto
        for (int i = 0; i < MAX_MESSAGES - 1; i++) moveEntry(i, i + 1);
        int last = MAX_MESSAGES - 1;
        messageHistory[last]  = shortMsg;
        messageTime[last]     = ts;
        messageFlags[last]    = flags;
        messageSender[last]   = sender;
        messageThisBoot[last] = true;
        if (doodle) memcpy(messageDoodle[last], doodle, DOODLE_BYTES);
        saveAll();
    }

    Serial.print("[Historial] Guardado: ");
    Serial.println(shortMsg);
}

void History_addIncomingDoodle(const uint8_t *bitmap, uint8_t sender) { addEntry(DOODLE_TEXT, FLAG_UNREAD, sender, bitmap); }
void History_addOutgoingDoodle(const uint8_t *bitmap)                 { addEntry(DOODLE_TEXT, FLAG_OUTGOING, 0, bitmap); }

bool History_isDoodle(int index) {
    if (index < 0 || index >= messageCount) return false;
    return (messageFlags[messageCount - 1 - index] & FLAG_DOODLE) != 0;
}

const uint8_t *History_getDoodle(int index) {
    return History_isDoodle(index) ? messageDoodle[messageCount - 1 - index] : nullptr;
}

void History_addIncoming(const String &msg, uint8_t sender) { addEntry(msg, FLAG_UNREAD, sender); }
void History_addOutgoing(const String &msg)                 { addEntry(msg, FLAG_OUTGOING, 0); }
void History_addMessage(const String &msg)                  { addEntry(msg, FLAG_UNREAD, 0); }

String History_getMessage(int index) {
    if (index < 0 || index >= messageCount) return "";
    return messageHistory[messageCount - 1 - index];
}

unsigned long History_getTimestamp(int index) {
    if (index < 0 || index >= messageCount) return 0;
    return messageTime[messageCount - 1 - index];
}

bool History_isOutgoing(int index) {
    if (index < 0 || index >= messageCount) return false;
    return (messageFlags[messageCount - 1 - index] & FLAG_OUTGOING) != 0;
}

uint8_t History_getSender(int index) {
    if (index < 0 || index >= messageCount) return 0;
    return messageSender[messageCount - 1 - index];
}

bool History_isFromThisBoot(int index) {
    if (index < 0 || index >= messageCount) return false;
    return messageThisBoot[messageCount - 1 - index];
}

int History_count() { return messageCount; }

int History_unreadCount() {
    int n = 0;
    for (int i = 0; i < messageCount; i++) if (messageFlags[i] & FLAG_UNREAD) n++;
    return n;
}

bool History_isUnread(int index) {
    if (index < 0 || index >= messageCount) return false;
    return (messageFlags[messageCount - 1 - index] & FLAG_UNREAD) != 0;
}

void History_markRead(int index) {
    if (!History_isUnread(index)) return;
    messageFlags[messageCount - 1 - index] &= (uint8_t)~FLAG_UNREAD;
    saveAll();
    Serial.print("[Historial] leido; quedan sin leer: "); Serial.println(History_unreadCount());
}

void History_markReadUpTo(int index) {
    bool changed = false;
    for (int i = 0; i <= index && i < messageCount; i++) {
        uint8_t &f = messageFlags[messageCount - 1 - i];
        if (f & FLAG_UNREAD) { f &= (uint8_t)~FLAG_UNREAD; changed = true; }
    }
    if (changed) {
        saveAll();
        Serial.print("[Historial] leidos; quedan sin leer: "); Serial.println(History_unreadCount());
    }
}

void History_shiftTimestamps(int32_t delta) {
    if (delta == 0 || messageCount == 0) return;
    for (int i = 0; i < messageCount; i++) {
        uint32_t t = (uint32_t)messageTime[i] + (uint32_t)delta;   // 32 bits como en la EEPROM (el sim tiene long de 64)
        if (t == 0xFFFFFFFFUL) t = 0xFFFFFFFEUL;                    // 0xFFFFFFFF = slot vacio
        messageTime[i] = t;
    }
    saveAll();
    Serial.print("[Historial] marcas de tiempo desplazadas "); Serial.print((long)delta); Serial.println(" s");
}

void History_deleteMessage(int index) {
    if (index < 0 || index >= messageCount) {
        Serial.println("[Historial] Indice invalido para borrar");
        return;
    }

    int realIndex = messageCount - 1 - index;

    for (int i = realIndex; i < messageCount - 1; i++) moveEntry(i, i + 1);

    messageCount--;
    messageHistory[messageCount]  = "";
    messageTime[messageCount]     = 0;
    messageFlags[messageCount]    = 0;
    messageSender[messageCount]   = 0;
    messageThisBoot[messageCount] = false;

    saveAll();

    Serial.print("[Historial] Borrado. Total: ");
    Serial.println(messageCount);
}
