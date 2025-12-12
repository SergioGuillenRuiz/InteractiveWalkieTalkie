#include "Historial.h"
#include <EEPROM.h>

// ============================================================
// CONFIGURACIÓN
// ============================================================

#define EEPROM_SIZE 2048        // Para 10 mensajes con timestamp
#define MAX_MSG_LENGTH 100      // 100 chars por mensaje
#define MAX_MESSAGES 10         // 10 mensajes máximo
#define MSG_SLOT_SIZE (MAX_MSG_LENGTH + 4) // +4 bytes timestamp

// Variables en RAM
String messageHistory[MAX_MESSAGES];
unsigned long messageTime[MAX_MESSAGES];  // Timestamps
int messageCount = 0;
int nextIndex = 0;

// ============================================================
// FUNCIONES EEPROM
// ============================================================

// Guardar mensaje + timestamp
static void saveToEEPROM(int slot, const String &msg, unsigned long timestamp) {
    EEPROM.begin(EEPROM_SIZE);
    
    int addr = slot * MSG_SLOT_SIZE;
    
    // Guardar timestamp (4 bytes)
    EEPROM.write(addr++, (timestamp >> 24) & 0xFF);
    EEPROM.write(addr++, (timestamp >> 16) & 0xFF);
    EEPROM.write(addr++, (timestamp >> 8) & 0xFF);
    EEPROM.write(addr++, timestamp & 0xFF);
    
    // Guardar longitud del mensaje (1 byte)
    uint8_t len = (uint8_t)min((size_t)msg.length(), (size_t)(MAX_MSG_LENGTH - 1));
    EEPROM.write(addr++, len);
    
    // Guardar texto
    for (uint8_t i = 0; i < len; i++) {
        EEPROM.write(addr + i, msg[i]);
    }
    EEPROM.write(addr + len, '\0'); // Null terminator
    
    EEPROM.commit();
    EEPROM.end();
}

// Cargar mensaje + timestamp
static bool loadFromEEPROM(int slot, String &msg, unsigned long &timestamp) {
    EEPROM.begin(EEPROM_SIZE);
    
    int addr = slot * MSG_SLOT_SIZE;
    
    // Leer timestamp
    timestamp = 0;
    timestamp |= (unsigned long)EEPROM.read(addr++) << 24;
    timestamp |= (unsigned long)EEPROM.read(addr++) << 16;
    timestamp |= (unsigned long)EEPROM.read(addr++) << 8;
    timestamp |= (unsigned long)EEPROM.read(addr++);
    
    // Si timestamp es 0xFFFFFFFF, slot vacío
    if (timestamp == 0xFFFFFFFF) {
        EEPROM.end();
        return false;
    }
    
    // Leer longitud
    uint8_t len = EEPROM.read(addr++);
    if (len == 0 || len > MAX_MSG_LENGTH - 1) {
        EEPROM.end();
        return false;
    }
    
    // Leer texto
    msg = "";
    for (uint8_t i = 0; i < len; i++) {
        char c = EEPROM.read(addr + i);
        if (c == '\0') break;
        msg += c;
    }
    
    EEPROM.end();
    return true;
}

// ============================================================
// FUNCIONES PÚBLICAS
// ============================================================

// 1. Cargar todo al iniciar
void History_load() {
    messageCount = 0;
    
    for (int i = 0; i < MAX_MESSAGES; i++) {
        String msg;
        unsigned long timestamp;
        
        if (loadFromEEPROM(i, msg, timestamp)) {
            messageHistory[messageCount] = msg;
            messageTime[messageCount] = timestamp;
            messageCount++;
        }
    }
    
    nextIndex = messageCount % MAX_MESSAGES;
    Serial.print("[Historial] Cargados ");
    Serial.print(messageCount);
    Serial.println(" mensajes");
}

// 2. Añadir mensaje (guarda automáticamente en EEPROM)
void History_addMessage(const String &msg) {
    if (msg.length() == 0) return;
    
    // Cortar si es muy largo
    String shortMsg = msg;
    if (shortMsg.length() > MAX_MSG_LENGTH - 1) {
        shortMsg = shortMsg.substring(0, MAX_MSG_LENGTH - 1);
    }
    
    unsigned long timestamp = millis();
    
    // Guardar en RAM
    messageHistory[nextIndex] = shortMsg;
    messageTime[nextIndex] = timestamp;
    
    // Guardar en EEPROM
    saveToEEPROM(nextIndex, shortMsg, timestamp);
    
    // Actualizar índices
    nextIndex = (nextIndex + 1) % MAX_MESSAGES;
    if (messageCount < MAX_MESSAGES) {
        messageCount++;
    }
    
    Serial.print("[Historial] Guardado: ");
    Serial.println(shortMsg);
}

// 3. Obtener mensaje (índice 0 = más reciente)
String History_getMessage(int index) {
    if (index < 0 || index >= messageCount) return "";
    
    if (messageCount < MAX_MESSAGES) {
        // Buffer no lleno: índice directo invertido
        return messageHistory[messageCount - 1 - index];
    } else {
        // Buffer lleno: cálculo circular
        int circularIndex = (nextIndex + MAX_MESSAGES - 1 - index) % MAX_MESSAGES;
        return messageHistory[circularIndex];
    }
}

// 4. Obtener timestamp del mensaje
unsigned long History_getTimestamp(int index) {
    if (index < 0 || index >= messageCount) return 0;
    
    if (messageCount < MAX_MESSAGES) {
        // Buffer no lleno
        return messageTime[messageCount - 1 - index];
    } else {
        // Buffer lleno
        int circularIndex = (nextIndex + MAX_MESSAGES - 1 - index) % MAX_MESSAGES;
        return messageTime[circularIndex];
    }
}

// 5. Contar mensajes
int History_count() {
    return messageCount;
}