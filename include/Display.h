#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "Config.h"

// ============================================================
// Configuración de la pantalla
// ============================================================

#define SCREEN_WIDTH        128
#define SCREEN_HEIGHT       128
#define DISPLAY_REFRESH_MS  100

#define TEXT_SMALL 1
#define TEXT_NORMAL 2

// ============================================================
// Variables Globales Accesibles
// ============================================================

extern Adafruit_SH1107 display;
extern const char* nombresMensajes[];
extern const char* nombresJuegos[];

enum HippoAnimation {
  ANIM_NORMAL = 0,        // animateHippo()
  ANIM_SLEEPY = 1,        // animateHippoWithZzz()
  ANIM_CHASING_HEART = 2, // animateHippoChasingHeart()
  ANIM_GIVING_HEART = 3,  // animateHippoGivingHeart()
  ANIM_BONKED = 4,        // animateHippoBonked()
  ANIM_NONE = 5
};

// ============================================================
// Funciones Display
// ============================================================

void Display_begin();

void Display_update();
void Display_clear();
void Display_setPower(bool on);   // enciende/apaga el panel OLED (ahorro batería)

void Display_centerText(const String &text);

void drawMenu();
void drawInstantMessagesMenu(int seleccion, bool force = false);
void drawGamesMenu(int seleccion, bool force = false);
void moveCursor();
void Display_resetMenuCursor();

void drawMorseSuggestion();
void drawMorseTable();
void drawMorse();

// Rueda de letras (composer): mensaje en construccion + dial de caracteres.
// finishVerb completa la pista "(manten B = <verbo>)": "enviar" o "guardar".
void drawDial(const String &msg, const char *charset, int len, int index, bool force = false, const char *finishVerb = "enviar");

bool animateHippo();
bool animateHippoWithZzz();
void animateHippoAproaching();
void animateHippoRetreating();
bool animateHippoBonked();
bool animateHippoChasingHeart();
bool animateHippoGivingHeart();
void drawHeart(int x, int y, int color);

void updateHippoAnimation();
void triggerAnimation(HippoAnimation anim);
void resetAnimationTimer(); 

// ============================================================
// BITMAPS (definidos en src/Bitmaps.cpp)
// ============================================================
extern const unsigned char martilloBitMap[] PROGMEM;
extern const unsigned char flechaBitMap[] PROGMEM;
extern const unsigned char carpetaBitMap[] PROGMEM;
extern const unsigned char hippoBitMap40[] PROGMEM;
extern const unsigned char hippoBitMap60[] PROGMEM;
extern const unsigned char hippoBitMap61[] PROGMEM;
extern const unsigned char hippoBitMap62[] PROGMEM;
extern const unsigned char hippoBitMap63[] PROGMEM;
extern const unsigned char hippoBitMap64[] PROGMEM;
extern const unsigned char hippoBitMap65[] PROGMEM;
extern const unsigned char hippoBitMap70[] PROGMEM;
extern const unsigned char hippoBitMap75[] PROGMEM;
extern const unsigned char hippoBitMap80[] PROGMEM;
extern const unsigned char hippoBitMap81[] PROGMEM;
extern const unsigned char hippoBitMap82[] PROGMEM;
extern const unsigned char hippoBitMap83[] PROGMEM;
extern const unsigned char hippoBitMap84[] PROGMEM;
extern const unsigned char hippoBitMap85[] PROGMEM;
extern const unsigned char hippoBitMap90[] PROGMEM;
extern const unsigned char hippoBitMap95[] PROGMEM;
extern const unsigned char hippoBitMap100[] PROGMEM;
extern const unsigned char hippoBitMap105[] PROGMEM;
extern const unsigned char hippoBitMap110[] PROGMEM;
extern const unsigned char hippoBitMapInv60[] PROGMEM;
extern const unsigned char hippoBitMapInv61[] PROGMEM;
extern const unsigned char hippoBitMapInv62[] PROGMEM;
extern const unsigned char hippoBitMapInv63[] PROGMEM;
extern const unsigned char hippoBitMapInv64[] PROGMEM;
extern const unsigned char hippoBitMapInv65[] PROGMEM;
extern const unsigned char hippoBitMapInv70[] PROGMEM;
extern const unsigned char hippoBitMapInv75[] PROGMEM;
extern const unsigned char hippoBitMapInv80[] PROGMEM;
extern const unsigned char hippoBitMapInv85[] PROGMEM;
extern const unsigned char hippoBitMapInv90[] PROGMEM;
extern const unsigned char hippoBitMapInv95[] PROGMEM;
extern const unsigned char hippoBitMapInv100[] PROGMEM;
extern const unsigned char hippoBitMapInv105[] PROGMEM;
extern const unsigned char hippoBitMapInv110[] PROGMEM;
extern const unsigned char hippoBitMapBonked60[] PROGMEM;
extern const unsigned char iconoCorazon[] PROGMEM;
extern const unsigned char iconoZZZ[] PROGMEM;
extern const unsigned char iconoCaraSeria[] PROGMEM;
extern const unsigned char iconoCaraFeliz[] PROGMEM;
extern const unsigned char iconoHambre[] PROGMEM;
extern const unsigned char iconoMochila[] PROGMEM;
extern const unsigned char iconoTick[] PROGMEM;
extern const unsigned char iconoCruz[] PROGMEM;
extern const unsigned char iconoMando[] PROGMEM;
extern const unsigned char iconoTetris[] PROGMEM;
extern const unsigned char iconoPoker[] PROGMEM;
extern const unsigned char iconoRefill[] PROGMEM;
extern const unsigned char iconoDado[] PROGMEM;
extern const unsigned char iconoRuleta[] PROGMEM;
extern const unsigned char iconoRadar[] PROGMEM;

#endif
