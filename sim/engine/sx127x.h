#ifndef SX127X_H
#define SX127X_H

// ============================================================
//  Modelo del chip LoRa SX1276 a nivel de REGISTROS.
//
//  El firmware ejecuta la libreria LoRa REAL (sim/vendor/lora, arduino-LoRa 0.8.0), que habla
//  con el chip por SPI. Este modelo hace de chip: recibe esos bytes SPI y reproduce lo que
//  hace el silicio (SX1276 datasheet Rev.7):
//    - modos SLEEP / STDBY / TX / RX continuo / RX unico, y el cambio de modo al escribir RegOpMode;
//    - banderas IRQ (RxDone, TxDone, RxTimeout, PayloadCrcError), que se borran escribiendo 1;
//    - FIFO de 256 bytes con puntero que se autoincrementa;
//    - TRANSMITIR tarda el tiempo en el aire REAL de la trama (formula del datasheet con SF, BW,
//      CR, preambulo, CRC y cabecera) y la libreria BLOQUEA ese tiempo (endPacket espera a TxDone);
//      mientras transmite la radio NO escucha (half-duplex);
//    - RECIBIR: una trama solo se oye si el chip esta en un modo de recepcion cuando se detecta su
//      preambulo y sigue en el hasta que acaba; en RX unico ademas expira a los SymbTimeout
//      simbolos (RegSymbTimeout) y vuelve a STDBY; con otro SF/BW/frecuencia/palabra de sincronismo
//      no se oye; si dos tramas se solapan se pierden (colision); con CRC activo una trama
//      corrupta levanta PayloadCrcError.
//  El simulador anterior devolvia los paquetes inyectados al instante y siempre, de modo que no
//  podia detectar una radio sorda ni una ocupacion del canal imposible.
//
//  Modo "ideal" (sx::setIdeal): sin tiempo en el aire y siempre escuchando (como el mock antiguo).
// ============================================================

#include <cstdint>
#include <string>
#include <vector>
#include "air_channel.h"

namespace sx {

// Tiempo en el aire (ms) de una trama LoRa (Semtech AN1200.13 / SX1276 4.1.1.6).
double airtimeMs(int payloadBytes, int sf, long bw, int cr, int preamble, bool crc, bool implicitHeader);

// ---- Plataforma: el motor del simulador reenvia aqui pines y SPI ----
void nssWrite(bool level);      // pin NSS (chip select, activo en bajo)
void rstWrite(bool level);      // pin RST (el flanco de subida reinicia el chip)
uint8_t spiTransfer(uint8_t b); // un byte por SPI (solo cuenta con NSS bajo)
bool dio0Level();               // nivel del pin DIO0 (RxDone/TxDone segun RegDioMapping1)

// ---- El "mundo": tramas que llegan al chip ----
void setIdeal(bool on);
bool ideal();
void setLoopback(bool on);                   // lo transmitido vuelve como recibido (al acabar de emitirse)
// "Compañero educado": un equipo inyectado por el guion no empieza a emitir mientras el firmware esta
// emitiendo (escucha antes de hablar): su trama espera a que acabe la emision del firmware. Un compañero
// real hace lo mismo (y reintenta si no recibe el ACK); sin esto, los tests dependerian de que la
// baliza/ACK/reintento del firmware, que salen en instantes aleatorios, no coincidan con la inyeccion.
void setPolite(bool on);
bool polite();
AirFrame &peerFrame();                       // parametros por defecto de las tramas inyectadas
void inject(const std::string &payload, int rssi = -42, bool corrupt = false);   // trama que empieza AHORA
void resetWorld();                           // reinicio en frio del reloj virtual: se olvidan tramas en vuelo

// ---- Observacion (tests) ----
struct TxRecord { double start, end; std::string payload; AirFrame p; };
enum RxOutcome {
    RX_PENDING = 0,
    RX_HEARD,             // recibida (RxDone) y entregada al firmware
    RX_CRC_ERROR,         // recibida con CRC malo: la libreria la descarta
    RX_LOST_STANDBY,      // el chip estaba en STDBY al detectarse el preambulo: sorda
    RX_LOST_SLEEP,        // ... en SLEEP
    RX_LOST_TX,           // ... transmitiendo (half-duplex)
    RX_LOST_RXTIMEOUT,    // RX unico ya habia expirado
    RX_LOST_ABORTED,      // se salio de RX antes de que acabara la trama
    RX_LOST_COLLISION,    // se solapo con otra trama
    RX_LOST_MISMATCH,     // otro SF / BW / frecuencia / sincronismo
    RX_OVERRUN            // llego otra trama antes de que el firmware leyera esta (se pisa)
};
struct RxRecord { double start, end; size_t len; RxOutcome outcome; AirFrame p; };

const std::vector<TxRecord> &txLog();
const std::vector<RxRecord> &rxLog();
const char *outcomeName(RxOutcome o);
double txAirtimeBetween(double t0, double t1);     // ms emitiendo dentro de [t0, t1]
std::string lastSent();                            // ultima trama transmitida (bytes)
std::vector<std::string> sentRing(size_t n = 16);  // ultimas n tramas transmitidas
std::string report();                              // resumen legible: TX, RX, tiempo por modo, energia estimada
void sync();                                       // pone al dia el estado del chip hasta el instante actual (se hace solo en cada acceso)
void forceMode(int mode);                          // FALLO simulado: el chip cae a SLEEP/STDBY sin que el firmware lo pida
int  currentMode();                                // 0 SLEEP, 1 STDBY, 3 TX, 5 RX continuo, 6 RX unico
bool listening();                                  // RX continuo o RX unico vigente

}  // namespace sx

#endif
