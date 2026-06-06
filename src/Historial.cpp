#include "Historial.h"
#include <EEPROM.h>

// ============================================================
// CONFIGURACIÓN
// ============================================================
#define MAX_MSG_LENGTH 100                       // chars por mensaje (incluye terminador)
#define MAX_MESSAGES   10                        // mensajes máximo
// Cada slot: 4 bytes timestamp + 1 byte longitud + texto + '\0'
#define MSG_SLOT_SIZE  (MAX_MSG_LENGTH + 5)
#define EEPROM_SIZE    (MSG_SLOT_SIZE * MAX_MESSAGES)

// Historial en RAM. Almacenado de forma lineal y cronológica:
//   índice 0           = mensaje más ANTIGUO
//   índice count-1     = mensaje más RECIENTE
// (la API expone el índice 0 como el más reciente, ver History_getMessage)
static String        messageHistory[MAX_MESSAGES];
static unsigned long messageTime[MAX_MESSAGES];
static int           messageCount = 0;

// ============================================================
// FUNCIONES EEPROM (asumen EEPROM.begin() ya llamado)
// ============================================================

static void writeSlot(int slot, const String &msg, unsigned long timestamp) {
    int addr = slot * MSG_SLOT_SIZE;

    EEPROM.write(addr++, (timestamp >> 24) & 0xFF);
    EEPROM.write(addr++, (timestamp >> 16) & 0xFF);
    EEPROM.write(addr++, (timestamp >> 8) & 0xFF);
    EEPROM.write(addr++, timestamp & 0xFF);

    uint8_t len = (uint8_t)min((size_t)msg.length(), (size_t)(MAX_MSG_LENGTH - 1));
    EEPROM.write(addr++, len);

    for (uint8_t i = 0; i < len; i++) EEPROM.write(addr + i, (uint8_t)msg[i]);
    EEPROM.write(addr + len, '\0');
}

static bool readSlot(int slot, String &msg, unsigned long &timestamp) {
    int addr = slot * MSG_SLOT_SIZE;

    timestamp  = (unsigned long)EEPROM.read(addr++) << 24;
    timestamp |= (unsigned long)EEPROM.read(addr++) << 16;
    timestamp |= (unsigned long)EEPROM.read(addr++) << 8;
    timestamp |= (unsigned long)EEPROM.read(addr++);

    if (timestamp == 0xFFFFFFFF) return false;   // slot vacío

    uint8_t len = EEPROM.read(addr++);
    if (len == 0 || len > MAX_MSG_LENGTH - 1) return false;

    msg = "";
    for (uint8_t i = 0; i < len; i++) {
        char c = EEPROM.read(addr + i);
        if (c == '\0') break;
        msg += c;
    }
    return true;
}

// Persiste todo el historial en una sola operación de flash.
static void saveAll() {
    EEPROM.begin(EEPROM_SIZE);
    for (int i = 0; i < MAX_MESSAGES; i++) {
        if (i < messageCount) writeSlot(i, messageHistory[i], messageTime[i]);
        else                  writeSlot(i, "", 0xFFFFFFFF);   // marcar vacío
    }
    EEPROM.commit();
    EEPROM.end();
}

// ============================================================
// FUNCIONES PÚBLICAS
// ============================================================

// 1. Cargar todo al iniciar
void History_load() {
    messageCount = 0;

    EEPROM.begin(EEPROM_SIZE);
    for (int i = 0; i < MAX_MESSAGES; i++) {
        String msg;
        unsigned long timestamp;
        if (readSlot(i, msg, timestamp)) {
            messageHistory[messageCount] = msg;
            messageTime[messageCount]    = timestamp;
            messageCount++;
        }
    }
    EEPROM.end();

    Serial.print("[Historial] Cargados ");
    Serial.print(messageCount);
    Serial.println(" mensajes");
}

// 2. Añadir mensaje (guarda automáticamente en EEPROM)
void History_addMessage(const String &msg) {
    if (msg.length() == 0) return;

    String shortMsg = msg;
    if (shortMsg.length() > MAX_MSG_LENGTH - 1)
        shortMsg = shortMsg.substring(0, MAX_MSG_LENGTH - 1);

    unsigned long timestamp = millis();

    if (messageCount < MAX_MESSAGES) {
        // Hay hueco: añadir al final y guardar sólo ese slot
        messageHistory[messageCount] = shortMsg;
        messageTime[messageCount]    = timestamp;
        messageCount++;

        EEPROM.begin(EEPROM_SIZE);
        writeSlot(messageCount - 1, shortMsg, timestamp);
        EEPROM.commit();
        EEPROM.end();
    } else {
        // Buffer lleno: descartar el más antiguo y desplazar el resto
        for (int i = 0; i < MAX_MESSAGES - 1; i++) {
            messageHistory[i] = messageHistory[i + 1];
            messageTime[i]    = messageTime[i + 1];
        }
        messageHistory[MAX_MESSAGES - 1] = shortMsg;
        messageTime[MAX_MESSAGES - 1]    = timestamp;
        saveAll();
    }

    Serial.print("[Historial] Guardado: ");
    Serial.println(shortMsg);
}

// 3. Obtener mensaje (índice 0 = más reciente)
String History_getMessage(int index) {
    if (index < 0 || index >= messageCount) return "";
    return messageHistory[messageCount - 1 - index];
}

// 4. Obtener timestamp del mensaje (índice 0 = más reciente)
unsigned long History_getTimestamp(int index) {
    if (index < 0 || index >= messageCount) return 0;
    return messageTime[messageCount - 1 - index];
}

// 5. Contar mensajes
int History_count() {
    return messageCount;
}

// 6. Borrar mensaje (índice 0 = más reciente)
void History_deleteMessage(int index) {
    if (index < 0 || index >= messageCount) {
        Serial.println("[Historial] Indice invalido para borrar");
        return;
    }

    int realIndex = messageCount - 1 - index;   // posición real en el array

    // Desplazar los posteriores una posición hacia atrás para tapar el hueco
    for (int i = realIndex; i < messageCount - 1; i++) {
        messageHistory[i] = messageHistory[i + 1];
        messageTime[i]    = messageTime[i + 1];
    }

    messageCount--;
    messageHistory[messageCount] = "";
    messageTime[messageCount]    = 0;

    saveAll();

    Serial.print("[Historial] Borrado. Total: ");
    Serial.println(messageCount);
}
