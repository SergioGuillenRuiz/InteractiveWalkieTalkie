// Host mock of la librería LoRa (sandeepmistry) para SX127x.
// Captura lo transmitido y entrega lo inyectado por el simulador (RX).
#ifndef SIM_LORA_H
#define SIM_LORA_H

#include <cstdint>
#include "Arduino.h"

class LoRaClass : public Print {
public:
    // Configuración
    void setPins(int nss, int reset, int dio0) { (void)nss; (void)reset; (void)dio0; }
    int  begin(long frequency);
    void setSpreadingFactor(int sf) { (void)sf; }
    void setSignalBandwidth(long bw) { (void)bw; }
    void setTxPower(int level) { (void)level; }
    void setFrequency(long f) { (void)f; }
    void enableCrc() {}
    void idle() {}
    void sleep() {}

    // Transmisión
    void beginPacket();
    int  endPacket(bool async = false);
    size_t write(uint8_t b) override;        // acumula en el paquete TX

    // Recepción
    int  parsePacket(int size = 0);
    int  available();
    int  read();
    int  peek();
    int  packetRssi() { return -42; }
    float packetSnr() { return 9.0f; }
};

extern LoRaClass LoRa;

#endif // SIM_LORA_H
