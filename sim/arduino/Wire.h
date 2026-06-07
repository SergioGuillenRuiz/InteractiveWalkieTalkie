// Host stub of <Wire.h> (I2C). El display se dibuja directamente vía GFX,
// así que el I2C real no se usa.
#ifndef SIM_WIRE_H
#define SIM_WIRE_H

#include <cstdint>
#include <cstddef>

class TwoWire {
public:
    void begin() {}
    void begin(int sda, int scl) { (void)sda; (void)scl; }
    void setClock(uint32_t hz) { (void)hz; }
    void beginTransmission(uint8_t addr) { (void)addr; }
    uint8_t endTransmission(bool stop = true) { (void)stop; return 0; }
    size_t write(uint8_t v) { (void)v; return 1; }
    size_t write(const uint8_t *buf, size_t n) { (void)buf; return n; }
    uint8_t requestFrom(uint8_t addr, uint8_t n) { (void)addr; return n; }
    int available() { return 0; }
    int read() { return -1; }
};

extern TwoWire Wire;

#endif // SIM_WIRE_H
