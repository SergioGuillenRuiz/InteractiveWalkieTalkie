// Host stub of <SPI.h>. La radio LoRa está mockeada, así que el SPI no se usa.
#ifndef SIM_SPI_H
#define SIM_SPI_H

#include <cstdint>

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
    uint8_t transfer(uint8_t data) { return data; }
    void setFrequency(uint32_t f) { (void)f; }
};

extern SPIClass SPI;

#endif // SIM_SPI_H
