// Host stub of <SPI.h>. Los bytes que el firmware (la libreria LoRa real) manda por SPI los
// recibe el modelo del chip SX1276 (sim/engine/sx127x.cpp), seleccionado por el pin NSS.
#ifndef SIM_SPI_H
#define SIM_SPI_H

#include <cstdint>

#define SPI_MODE0 0x00
#define SPI_MODE1 0x04
#define SPI_MODE2 0x08
#define SPI_MODE3 0x0C

class SPISettings {
public:
    SPISettings() {}
    SPISettings(uint32_t clock, uint8_t order, uint8_t mode) {
        (void)clock; (void)order; (void)mode;
    }
};

class SPIClass {
public:
    void begin() {}
    void end() {}
    void beginTransaction(SPISettings s) { (void)s; }
    void endTransaction() {}
    uint8_t transfer(uint8_t data);          // definido en sx127x.cpp
    void setFrequency(uint32_t f) { (void)f; }
};

extern SPIClass SPI;

#endif // SIM_SPI_H
