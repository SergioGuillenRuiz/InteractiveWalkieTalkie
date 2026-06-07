// Host implementation of Adafruit_SH1107 (128x128 monochrome OLED).
// Hereda del Adafruit_GFX REAL, así que el texto, fuentes, bitmaps y
// primitivas se renderizan exactamente igual que en el hardware.
#ifndef SIM_ADAFRUIT_SH110X_H
#define SIM_ADAFRUIT_SH110X_H

#include <Adafruit_GFX.h>
#include <Wire.h>
#include <string>
#include <cstring>

#define SH110X_BLACK   0
#define SH110X_WHITE   1
#define SH110X_INVERSE 2
#define SH110X_DISPLAYOFF 0xAE
#define SH110X_DISPLAYON  0xAF

class Adafruit_SH1107 : public Adafruit_GFX {
public:
    static const int W = 128;
    static const int H = 128;

    Adafruit_SH1107(uint16_t w, uint16_t h, TwoWire *twi = nullptr, int8_t rst = -1)
        : Adafruit_GFX(w, h) {
        (void)twi; (void)rst;
        memset(pixels_, 0, sizeof(pixels_));
        frames_ = 0;
    }

    bool begin(uint8_t addr = 0x3C, bool reset = true) {
        (void)addr; (void)reset;
        clearDisplay();
        return true;
    }

    void clearDisplay() {
        memset(pixels_, 0, sizeof(pixels_));
        text_.clear();
    }

    void display() { frames_++; }

    void setContrast(uint8_t c) { (void)c; }
    void invertDisplay(bool i) { (void)i; }
    void oled_command(uint8_t c) { (void)c; }
    uint8_t *getBuffer() { return pixels_; }   // 1 byte por píxel (0/1)

    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || x >= W || y < 0 || y >= H) return;
        if (color == SH110X_INVERSE) pixels_[y * W + x] ^= 1;
        else                         pixels_[y * W + x] = (color != SH110X_BLACK) ? 1 : 0;
    }

    // Captura el texto dibujado (para aserciones) y además lo pinta.
    size_t write(uint8_t c) override {
        if (c != '\r') text_ += (char)c;
        return Adafruit_GFX::write(c);
    }

    // --- API del simulador ---
    bool simPixel(int x, int y) const {
        if (x < 0 || x >= W || y < 0 || y >= H) return false;
        return pixels_[y * W + x] != 0;
    }
    const std::string &simText() const { return text_; }
    unsigned long simFrames() const { return frames_; }

private:
    uint8_t pixels_[W * H];   // 0/1 por píxel
    std::string text_;        // texto dibujado desde el último clearDisplay
    unsigned long frames_;
};

#endif // SIM_ADAFRUIT_SH110X_H
