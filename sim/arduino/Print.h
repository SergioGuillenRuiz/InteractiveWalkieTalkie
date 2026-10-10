// Host mock of the Arduino Print base class.
#ifndef SIM_PRINT_H
#define SIM_PRINT_H

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include "WString.h"

#define DEC 10
#define HEX 16
#define BIN 2

class Print {
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t *buf, size_t size) {
        size_t n = 0;
        while (size--) { if (write(*buf++)) n++; else break; }
        return n;
    }
    size_t write(const char *str) {
        if (!str) return 0;
        return write((const uint8_t *)str, strlen(str));
    }

    size_t print(const char *s) { return write(s); }
    size_t print(const String &s) { return write((const uint8_t *)s.c_str(), s.length()); }
    size_t print(char c) { return write((uint8_t)c); }
    size_t print(unsigned char b, int base = DEC) { return print((unsigned long)b, base); }
    size_t print(int n, int base = DEC) { return print((long)n, base); }
    size_t print(unsigned int n, int base = DEC) { return print((unsigned long)n, base); }
    size_t print(long n, int base = DEC) {
        char buf[40];
        if (base == HEX) snprintf(buf, sizeof(buf), "%lX", n);
        else if (base == BIN) return printBin((unsigned long)n);
        else snprintf(buf, sizeof(buf), "%ld", n);
        return write(buf);
    }
    size_t print(unsigned long n, int base = DEC) {
        char buf[40];
        if (base == HEX) snprintf(buf, sizeof(buf), "%lX", n);
        else if (base == BIN) return printBin(n);
        else snprintf(buf, sizeof(buf), "%lu", n);
        return write(buf);
    }
    size_t print(double n, int digits = 2) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.*f", digits, n);
        return write(buf);
    }

    size_t println() { return write("\r\n"); }
    size_t println(const char *s) { size_t n = print(s); return n + println(); }
    size_t println(const String &s) { size_t n = print(s); return n + println(); }
    size_t println(char c) { size_t n = print(c); return n + println(); }
    size_t println(unsigned char b, int base = DEC) { size_t n = print(b, base); return n + println(); }
    size_t println(int v, int base = DEC) { size_t n = print(v, base); return n + println(); }
    size_t println(unsigned int v, int base = DEC) { size_t n = print(v, base); return n + println(); }
    size_t println(long v, int base = DEC) { size_t n = print(v, base); return n + println(); }
    size_t println(unsigned long v, int base = DEC) { size_t n = print(v, base); return n + println(); }
    size_t println(double v, int digits = 2) { size_t n = print(v, digits); return n + println(); }

private:
    size_t printBin(unsigned long v) {
        char buf[40]; int i = 0;
        if (!v) buf[i++] = '0';
        while (v) { buf[i++] = char('0' + (v & 1)); v >>= 1; }
        char out[40]; int j = 0;
        while (i) out[j++] = buf[--i];
        out[j] = 0;
        return write(out);
    }
};

// Stream: Print + lectura (la libreria LoRa real hereda de el)
class Stream : public Print {
public:
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;
    virtual void flush() = 0;
    void setTimeout(unsigned long t) { timeout_ = t; }
protected:
    unsigned long timeout_ = 1000;
};

#endif // SIM_PRINT_H
