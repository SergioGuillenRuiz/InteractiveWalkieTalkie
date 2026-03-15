#ifndef MOCK_SH110X_H
#define MOCK_SH110X_H

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>


#define SH110X_WHITE SSD1306_WHITE
#define SH110X_BLACK SSD1306_BLACK

class Adafruit_SH1107 : public Adafruit_SSD1306 {
private:
  Adafruit_SSD1306 bottomScreen;
  uint8_t topPrev[1024];
  uint8_t botPrev[1024];

public:
  Adafruit_SH1107(uint16_t w, uint16_t h, TwoWire *twi = &Wire,
                  int8_t rst_pin = -1)
      : Adafruit_SSD1306(128, 64, twi, rst_pin),
        bottomScreen(128, 64, twi, rst_pin) {

    // Engañamos a GFX sobre nuestro tamaño real
    WIDTH = 128;
    HEIGHT = 128;
    _width = 128;
    _height = 128;

    memset(topPrev, 0, 1024);
    memset(botPrev, 0, 1024);
  }

  bool begin(uint8_t i2c_addr = 0x3C, bool reset = true) {
    Wire.begin();
    Wire.setClock(800000);
    bool topOK = Adafruit_SSD1306::begin(SSD1306_SWITCHCAPVCC, 0x3C, reset);
    bool bottomOK = bottomScreen.begin(SSD1306_SWITCHCAPVCC, 0x3D, reset);

    // begin() resetea el tamaño internamente, así que volvemos a engañarlo
    WIDTH = 128;
    HEIGHT = 128;
    _width = 128;
    _height = 128;

    return topOK && bottomOK;
  }

  void setContrast(uint8_t contrast) {}

  void clearDisplay(void) {
    Adafruit_SSD1306::clearDisplay();
    bottomScreen.clearDisplay();
  }

  void display(void) {
    uint8_t *topBuf = Adafruit_SSD1306::getBuffer();
    uint8_t *botBuf = bottomScreen.getBuffer();

    // ====================================================================
    // EL TRUCO VITAL: Restauramos la altura a 64 ANTES de transmitir
    // para evitar el desbordamiento de memoria que destruía tu pantalla.
    // ====================================================================
    HEIGHT = 64;

    // Enviamos a la pantalla superior solo si su búfer cambió
    if (memcmp(topBuf, topPrev, 1024) != 0) {
      Adafruit_SSD1306::display();
      memcpy(topPrev, topBuf, 1024);
    }

    // Enviamos a la pantalla inferior solo si su búfer cambió
    if (memcmp(botBuf, botPrev, 1024) != 0) {
      bottomScreen.display();
      memcpy(botPrev, botBuf, 1024);
    }

    // VOLVEMOS A ENGAÑAR A GFX PARA QUE PERMITA DIBUJAR HASTA Y=127
    HEIGHT = 128;
  }

  // ====================================================================
  // ENRUTAMIENTO GEOMÉTRICO (Súper rápido y estable)
  // ====================================================================
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (y < 64)
      Adafruit_SSD1306::drawPixel(x, y, color);
    else
      bottomScreen.drawPixel(x, y - 64, color);
  }

  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
    if (y < 64) {
      if (y + h <= 64) {
        Adafruit_SSD1306::drawFastVLine(x, y, h, color);
      } else {
        int16_t hTop = 64 - y;
        Adafruit_SSD1306::drawFastVLine(x, y, hTop, color);
        bottomScreen.drawFastVLine(x, 0, h - hTop, color);
      }
    } else {
      bottomScreen.drawFastVLine(x, y - 64, h, color);
    }
  }

  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    if (y < 64)
      Adafruit_SSD1306::drawFastHLine(x, y, w, color);
    else
      bottomScreen.drawFastHLine(x, y - 64, w, color);
  }

  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                uint16_t color) override {
    if (y < 64) {
      if (y + h <= 64) {
        Adafruit_SSD1306::fillRect(x, y, w, h, color);
      } else {
        int16_t hTop = 64 - y;
        Adafruit_SSD1306::fillRect(x, y, w, hTop, color);
        bottomScreen.fillRect(x, 0, w, h - hTop, color);
      }
    } else {
      bottomScreen.fillRect(x, y - 64, w, h, color);
    }
  }
};

#endif