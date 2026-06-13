#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "Config.h"
#include "MyLora.h"
#include "SimpleCrypto.h"


// Estado interno del módulo
bool loraReady = false;
String lastReceived = "";
static int lastRssi = -200;   // RSSI del último paquete recibido

// ============================================================
//  Inicialización del módulo LoRa
// ============================================================

void Lora_begin() {
  Serial.println("[LoRa] Inicializando...");
  SPI.begin();
  // Configurar pines
  LoRa.setPins(PIN_LORA_NSS, PIN_LORA_RST, PIN_LORA_DIO0);

  // Intentar inicialización
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("[LoRa] Error: no se pudo iniciar el módulo.");
    loraReady = false;
    return;
  }

  // Configurar parámetros
  LoRa.setSpreadingFactor(LORA_SPREADING);
  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.setTxPower(LORA_POWER);

  // Marcar como listo
  loraReady = true;

  Serial.println("[LoRa] Módulo iniciado correctamente.");
  Serial.print("[LoRa] Frecuencia: "); Serial.println(LORA_FREQUENCY);
  Serial.print("[LoRa] Potencia: "); Serial.println(LORA_POWER);
  Serial.println("[LoRa] Esperando mensajes...");
}

// ============================================================
//  Envío de mensajes
// ============================================================

bool Lora_send(const String &message) {
  if (!loraReady) {
    Serial.println("[LoRa] No inicializado, no se puede enviar.");
    return false;
  }

  Serial.print("[LoRa] Enviando: ");
  Serial.println(message);
  
  String encrypted = SimpleCrypto_encrypt(message);
  if (encrypted.length() == 0) {
    Serial.println("[LoRa] Error al encriptar mensaje");
    return false;
  }
  Serial.print("[LoRa] Encriptado: ");
  Serial.println(encrypted);

  LoRa.beginPacket();
  LoRa.print(encrypted);
  LoRa.endPacket();

  return true;
}

// ============================================================
//  Comprobación y lectura de mensajes
// ============================================================

bool Lora_hasMessage() {
  if (!loraReady) return false;

  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    lastRssi = LoRa.packetRssi();   // capturar antes de leer/descifrar
    lastReceived = "";
    while (LoRa.available()) {
      lastReceived += (char)LoRa.read();
    }
    Serial.print("[LoRa] Mensaje recibido (crudo): ");
    Serial.println(lastReceived);
    
    String decrypted = SimpleCrypto_decrypt(lastReceived);
    if (decrypted.length() == 0) {
      Serial.println("[LoRa] Error al desencriptar mensaje");
      return false;
    }
    lastReceived = decrypted;
    
    Serial.print("[LoRa] Desencriptado: ");
    Serial.println(lastReceived);
    return true;
  }
  return false;
}

String Lora_readMessage() {
  String msg = lastReceived;
  lastReceived = "";
  return msg;
}

int Lora_lastRssi() { return lastRssi; }

// ============================================================
//  Actualización periódica (loop auxiliar)
// ============================================================

void Lora_update() {
  // Por ahora no hay tareas periódicas
  // En el futuro: RSSI, control de errores, ACKs, etc.
  if (!loraReady) return;
}

// ============================================================
//  Estado del módulo
// ============================================================

bool Lora_isReady() {
  return loraReady;
}

