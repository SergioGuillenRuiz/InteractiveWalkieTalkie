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

// Política de la radio: ESCUCHAR (recepción continua; estado normal) o DORMIR (suspensión
// prolongada: ni oye ni emite, ahorra ~11 mA).
enum RadioPolicy { RP_LISTEN, RP_SLEEP };
static RadioPolicy g_policy = RP_LISTEN;
static uint32_t    g_deepSleepMs = LORA_DEEP_SLEEP;   // ver Lora_setDeepSleepMs()
static bool        g_rangeMode = false;               // HippoRadar: SF10 / +20 dBm
static uint32_t    g_lastWatch = 0;

// Registros del SX127x que se consultan directamente (la librería LoRa no permite leer las banderas
// sin cambiar el modo de la radio, ver Lora_hasMessage()).
#define SX_REG_FIFO        0x00
#define SX_REG_OP_MODE     0x01
#define SX_REG_FIFO_ADDR   0x0D    // puntero de acceso por SPI a la FIFO
#define SX_REG_FIFO_RXCUR  0x10    // dirección de la FIFO donde empieza la última trama recibida
#define SX_REG_IRQ_FLAGS   0x12
#define SX_REG_RX_NB_BYTES 0x13    // longitud de la última trama recibida
#define SX_REG_MODEM_STAT  0x18    // bits 0..3: señal detectada / sincronizada / recibiendo / cabecera válida
#define SX_REG_PKT_RSSI    0x1A
#define SX_IRQ_RX_DONE     0x40
#define SX_IRQ_TX_DONE     0x08
#define SX_OPMODE_MASK     0x07    // bits de modo de RegOpMode
#define SX_OPMODE_TX       0x03
#define SX_IRQ_CRC_ERROR   0x20
#define SX_OPMODE_RXCONT   0x85    // LoRa + recepción continua

// Cola de tramas ya sacadas del chip pero aun no entregadas al firmware (cifradas, tal como llegaron).
// Emitir comparte la FIFO del chip con la recepcion: una trama completa y sin leer se destruiria al
// emitir, asi que antes de emitir se guarda aqui. Lora_hasMessage() sirve primero esta cola.
#define RXQ_MAX 3
static String  g_rxq[RXQ_MAX];
static int     g_rxqRssi[RXQ_MAX];
static uint8_t g_rxqN = 0;

// Lectura de un registro por SPI (mismo protocolo y ajustes que la librería).
static uint8_t sxRead(uint8_t addr) {
  digitalWrite(PIN_LORA_NSS, LOW);
  SPI.beginTransaction(SPISettings(8E6, MSBFIRST, SPI_MODE0));
  SPI.transfer(addr & 0x7F);
  uint8_t v = SPI.transfer(0x00);
  SPI.endTransaction();
  digitalWrite(PIN_LORA_NSS, HIGH);
  return v;
}

static void sxWrite(uint8_t addr, uint8_t value) {
  digitalWrite(PIN_LORA_NSS, LOW);
  SPI.beginTransaction(SPISettings(8E6, MSBFIRST, SPI_MODE0));
  SPI.transfer(addr | 0x80);
  SPI.transfer(value);
  SPI.endTransaction();
  digitalWrite(PIN_LORA_NSS, HIGH);
}

// Versión "imprimible" de un paquete para el log: los sobres binarios (chat,
// balizas) llevan bytes no imprimibles (incluido 0x00) que corromperían un log
// volcado a fichero. Se sustituyen por '.'; el texto normal queda intacto.
static String logSafe(const String &s) {
  String out = "";
  for (uint16_t i = 0; i < s.length(); i++) {
    char c = s[i];
    out += (c >= 0x20 && c <= 0x7E) ? c : '.';
  }
  return out;
}

// Si el chip tiene una trama completa sin leer, la saca a la cola.
//
// La FIFO se lee DIRECTAMENTE por SPI, sin salir de recepcion continua. La libreria LoRa (modo sondeo) solo
// sabe recibir con parsePacket(), que deja la radio en "recepcion unica" (caduca a los ~100 simbolos) y, al
// llegar un paquete, la pasa a reposo: una trama que ya se estuviera recibiendo en ese momento (dos equipos
// que emiten seguidos) se abortaria. Leyendo por SPI el chip sigue escuchando todo el rato y la trama
// siguiente no se pierde mientras se lea la anterior en los ms que dura su preambulo.
static void stashPending() {
  uint8_t irq = sxRead(SX_REG_IRQ_FLAGS);
  if (!(irq & SX_IRQ_RX_DONE)) return;
  sxWrite(SX_REG_IRQ_FLAGS, irq);          // borra las banderas vistas (se borran escribiendo 1)
  if (irq & SX_IRQ_CRC_ERROR) {
    Serial.println("[LoRa] Trama con CRC erróneo descartada");
    return;
  }
  uint8_t len = sxRead(SX_REG_RX_NB_BYTES);
  sxWrite(SX_REG_FIFO_ADDR, sxRead(SX_REG_FIFO_RXCUR));
  String raw = "";
  for (uint8_t i = 0; i < len; i++) raw += (char)sxRead(SX_REG_FIFO);
  int rssi = (int)sxRead(SX_REG_PKT_RSSI) - (LORA_FREQUENCY < 868E6 ? 164 : 157);
  if (len == 0) return;
  if (g_rxqN == RXQ_MAX) {                 // cola llena: se pierde la mas antigua
    Serial.println("[LoRa] Cola de recepcion llena: se descarta la mas antigua");
    for (int i = 1; i < RXQ_MAX; i++) { g_rxq[i - 1] = g_rxq[i]; g_rxqRssi[i - 1] = g_rxqRssi[i]; }
    g_rxqN--;
  }
  g_rxq[g_rxqN] = raw;
  g_rxqRssi[g_rxqN] = rssi;
  g_rxqN++;
}

// ---- Emision asincrona ----
// LoRa.endPacket() espera a que acabe la emision (120-700 ms segun la trama): el equipo quedaria sordo a los
// botones todo ese tiempo (una pulsacion corta se pierde: el antirrebote mide desde que la ve) y las
// animaciones se pararian. Se emite en modo asincrono (endPacket(true)): el chip emite solo y el firmware
// sigue con su bucle. Al acabar hay que volver a escuchar (la radio no oye mientras emite): lo hace
// txBusy(), que se consulta en cada entrada de esta capa (envio, lectura, vigilancia...).
static bool g_txActive = false;

static bool txBusy() {
  if (!g_txActive) return false;
  if ((sxRead(SX_REG_OP_MODE) & SX_OPMODE_MASK) == SX_OPMODE_TX) return true;   // (LoRa.isTransmitting() es privada)
  sxWrite(SX_REG_IRQ_FLAGS, SX_IRQ_TX_DONE);   // acabo: se borra la bandera TxDone (se borra escribiendo 1)
  g_txActive = false;
  if (g_policy == RP_LISTEN) LoRa.receive();
  return false;
}

static void waitTxEnd() { while (txBusy()) delay(1); }

// El modem esta recibiendo una trama (senal detectada / sincronizada / recibiendo / cabecera valida).
static bool channelBusy() { return (sxRead(SX_REG_MODEM_STAT) & 0x0F) != 0; }

// Escuchar antes de hablar (ver CSMA_* en Config.h).
static void waitChannelClear() {
  uint32_t t0 = millis();
  bool deferred = false;
  while (true) {
    stashPending();                        // la trama que acaba de recibirse no debe pisarla nuestra emision
    if (channelBusy()) {
      deferred = true;
    } else if (deferred) {
      // Acaba de liberarse: pausa aleatoria y se vuelve a mirar (otros equipos esperaban lo mismo).
      deferred = false;
      delay(1 + (uint32_t)random(CSMA_BACKOFF_MS));
      stashPending();
      if (!channelBusy()) return;
      deferred = true;
    } else {
      return;
    }
    if (millis() - t0 >= CSMA_MAX_WAIT_MS) {
      Serial.println("[LoRa] Canal ocupado demasiado tiempo: se emite igualmente");
      return;
    }
    delay(1);
  }
}

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
  // CRC en el paquete: una trama con bits erróneos (ruido, colisión parcial) la descarta el propio
  // chip en vez de llegar al descifrado (que solo detecta algunos errores y podría dar basura).
  LoRa.enableCrc();

  // Marcar como listo y empezar a escuchar
  g_rxqN = 0;
  g_txActive = false;
  g_rangeMode = false;
  g_deepSleepMs = LORA_DEEP_SLEEP;
  loraReady = true;
  g_policy = RP_LISTEN;
  LoRa.receive();

  Serial.println("[LoRa] Módulo iniciado correctamente.");
  Serial.print("[LoRa] Frecuencia: "); Serial.println(LORA_FREQUENCY);
  Serial.print("[LoRa] Potencia: "); Serial.println(LORA_POWER);
  Serial.println("[LoRa] Esperando mensajes...");
}

// ============================================================
//  Política de la radio (escuchar / dormir)
// ============================================================

void Lora_listen() {
  if (!loraReady) return;
  waitTxEnd();
  bool wasAsleep = (g_policy == RP_SLEEP);
  g_policy = RP_LISTEN;
  if (wasAsleep) { LoRa.idle(); delay(2); }   // salir de sleep: el oscilador necesita un instante
  LoRa.receive();
  g_lastWatch = millis();
}

void Lora_sleep() {
  if (!loraReady) return;
  waitTxEnd();
  g_policy = RP_SLEEP;
  LoRa.sleep();
}

bool Lora_isListening() { return loraReady && g_policy == RP_LISTEN; }

void     Lora_setDeepSleepMs(uint32_t ms) { g_deepSleepMs = ms; }
uint32_t Lora_deepSleepMs()               { return g_deepSleepMs; }

bool Lora_busy() { return loraReady && g_policy == RP_LISTEN && (txBusy() || channelBusy()); }
bool Lora_isAsleep()    { return loraReady && g_policy == RP_SLEEP; }

// HippoRadar: largo alcance (SF10 y +20 dBm) mientras dura el radar y vuelta a los parámetros del
// chat. Se mantiene la política vigente (si escuchaba, sigue escuchando con los parámetros nuevos).
bool Lora_rangeMode() { return g_rangeMode; }

void Lora_setRangeMode(bool on) {
  if (!loraReady) return;
  waitTxEnd();
  g_rangeMode = on;
  LoRa.idle();
  LoRa.setSpreadingFactor(on ? 10 : LORA_SPREADING);
  LoRa.setTxPower(on ? 20 : LORA_POWER);
  if (g_policy == RP_LISTEN) LoRa.receive();
}

// ============================================================
//  Envío de mensajes
// ============================================================

bool Lora_send(const String &message) {
  if (!loraReady) {
    Serial.println("[LoRa] No inicializado, no se puede enviar.");
    return false;
  }
  if (g_policy == RP_SLEEP) return false;     // radio dormida (ahorro): el equipo no emite

  Serial.print("[LoRa] Enviando: ");
  Serial.println(logSafe(message));

  String encrypted = SimpleCrypto_encrypt(message);
  if (encrypted.length() == 0) {
    Serial.println("[LoRa] Error al encriptar mensaje");
    return false;
  }
  Serial.print("[LoRa] Encriptado: ");
  Serial.println(encrypted);

  waitTxEnd();             // la emision anterior, si sigue en el aire
  waitChannelClear();      // escuchar antes de hablar (y guardar lo ya recibido: la FIFO es la misma)
  stashPending();

  LoRa.beginPacket();
  LoRa.print(encrypted);
  LoRa.endPacket(true);    // asincrono: el chip emite solo; txBusy() vuelve a escuchar cuando acaba
  g_txActive = true;
  return true;
}

// ============================================================
//  Comprobación y lectura de mensajes
// ============================================================

bool Lora_hasMessage() {
  if (!loraReady || g_policy != RP_LISTEN) return false;
  if (txBusy()) return false;              // emitiendo: la radio no oye

  if (g_rxqN == 0) stashPending();
  if (g_rxqN == 0) return false;

  lastReceived = g_rxq[0];                 // la mas antigua primero
  lastRssi = g_rxqRssi[0];
  for (int i = 1; i < (int)g_rxqN; i++) { g_rxq[i - 1] = g_rxq[i]; g_rxqRssi[i - 1] = g_rxqRssi[i]; }
  g_rxqN--;
  g_rxq[g_rxqN] = "";

  Serial.print("[LoRa] Mensaje recibido (crudo): ");
  Serial.println(lastReceived);

  String decrypted = SimpleCrypto_decrypt(lastReceived);
  if (decrypted.length() == 0) {
    Serial.println("[LoRa] Error al desencriptar mensaje");
    return false;
  }
  lastReceived = decrypted;

  Serial.print("[LoRa] Desencriptado: ");
  Serial.println(logSafe(lastReceived));
  return true;
}

String Lora_readMessage() {
  String msg = lastReceived;
  lastReceived = "";
  return msg;
}

int Lora_lastRssi() { return lastRssi; }

// ============================================================
//  Actualización periódica (vigilancia)
// ============================================================

// Cada 500 ms comprueba que el chip sigue en recepción continua. Si cayó (reinicio del módulo, fallo de
// alimentación, un paquete con CRC erróneo en el modo antiguo...) lo vuelve a poner: una radio que
// "cree" escuchar y no lo hace es el peor fallo (el equipo parece vivo pero no recibe nada).
void Lora_update() {
  if (!loraReady || g_policy != RP_LISTEN) return;
  if (txBusy()) return;                    // emitiendo (o recien terminado: ya esta de nuevo escuchando)
  uint32_t now = millis();
  if (now - g_lastWatch < 500) return;
  g_lastWatch = now;
  uint8_t om = sxRead(SX_REG_OP_MODE);
  if (om != SX_OPMODE_RXCONT) {
    Serial.print("[LoRa] La radio no estaba en recepcion (modo 0x"); Serial.print(om, HEX);
    Serial.println("): se re-arma");
    LoRa.receive();
  }
}

// ============================================================
//  Estado del módulo
// ============================================================

bool Lora_isReady() {
  return loraReady;
}
