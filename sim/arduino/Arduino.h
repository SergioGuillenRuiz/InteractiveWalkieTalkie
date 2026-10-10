// Host mock of <Arduino.h> for the PC simulator.
// Declara la API del core de Arduino que usa el firmware; la implementación
// vive en sim/engine/sim_runtime.cpp (reloj virtual, pines, Serial...).
#ifndef SIM_ARDUINO_H
#define SIM_ARDUINO_H

// --- STL primero, antes de definir macros min/max ---
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#include "pgmspace.h"
#include "WString.h"
#include "Print.h"

#ifndef ARDUINO
#define ARDUINO 100
#endif

// ---------------------------------------------------------------------------
// Tipos y constantes
// ---------------------------------------------------------------------------
typedef uint8_t byte;
typedef bool boolean;

#ifndef HIGH
#define HIGH 0x1
#define LOW  0x0
#endif
#define INPUT        0x00
#define OUTPUT       0x01
#define INPUT_PULLUP 0x02
#define LSBFIRST 0
#define MSBFIRST 1
#define RISING  0x01
#define FALLING 0x02
#define CHANGE  0x03

// Constantes binarias del core de Arduino que usa la libreria LoRa
#define B111  7
#define B1000 8
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? ((value) |= (1UL << (bit))) : ((value) &= ~(1UL << (bit))))

// Pin analógico del potenciómetro (id de pin ficticio en el simulador)
#define A0 17

class __FlashStringHelper;
#define F(str) (reinterpret_cast<const __FlashStringHelper *>(PSTR(str)))

// ---------------------------------------------------------------------------
// Macros matemáticas (estilo Arduino). map() es función (devuelve long).
// ---------------------------------------------------------------------------
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif
#ifndef abs
#define abs(x) ((x) > 0 ? (x) : -(x))
#endif

static inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
    if (in_max == in_min) return out_min;
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Constantes y conversiones angulares (las usa Adafruit_GFX)
#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#ifndef TWO_PI
#define TWO_PI 6.283185307179586476925286766559
#endif
#ifndef HALF_PI
#define HALF_PI 1.5707963267948966192313216916398
#endif
#ifndef DEG_TO_RAD
#define DEG_TO_RAD 0.017453292519943295769236907684886
#endif
#ifndef RAD_TO_DEG
#define RAD_TO_DEG 57.295779513082320876798154814105
#endif
#ifndef radians
#define radians(deg) ((deg) * DEG_TO_RAD)
#endif
#ifndef degrees
#define degrees(rad) ((rad) * RAD_TO_DEG)
#endif
#ifndef sq
#define sq(x) ((x) * (x))
#endif

// Builtins de GCC que MSVC no tiene
#ifdef _MSC_VER
#define __builtin_bswap16(x) _byteswap_ushort(x)
#define __builtin_bswap32(x) _byteswap_ulong(x)
#endif

// ---------------------------------------------------------------------------
// Tiempo
// ---------------------------------------------------------------------------
unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void yield();

// ---------------------------------------------------------------------------
// E/S digital y analógica
// ---------------------------------------------------------------------------
void pinMode(uint8_t pin, uint8_t mode);
int  digitalRead(uint8_t pin);
void digitalWrite(uint8_t pin, uint8_t val);
int  analogRead(uint8_t pin);
void analogWrite(uint8_t pin, int val);

// Interrupciones: el firmware no las usa (la libreria LoRa solo en onReceive/onTxDone, no usados)
typedef void (*voidFuncPtr)(void);
inline void attachInterrupt(uint8_t, voidFuncPtr, int) {}
inline void detachInterrupt(uint8_t) {}
#define digitalPinToInterrupt(p) (p)

// ---------------------------------------------------------------------------
// Aleatorios
// ---------------------------------------------------------------------------
long random(long howbig);
long random(long howsmall, long howbig);
void randomSeed(unsigned long seed);

// ---------------------------------------------------------------------------
// Serial
// ---------------------------------------------------------------------------
class HardwareSerial : public Print {
public:
    void begin(unsigned long baud) { (void)baud; }
    void end() {}
    int  available() { return 0; }
    int  read() { return -1; }
    void flush() {}
    size_t write(uint8_t c) override;
};
extern HardwareSerial Serial;

#endif // SIM_ARDUINO_H
