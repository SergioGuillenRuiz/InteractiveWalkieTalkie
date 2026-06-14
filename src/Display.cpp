#include <Arduino.h>
#include "Config.h"
#include "Display.h"
#include "Morse.h"
#include "States.h"
#include "Inputs.h"


const char* nombresJuegos[5] = {
  "Tetris Coop", "Poker", "RefillGame", "Choose4Me", "HippoRadar"
};

const unsigned char* iconosJuegos[5] = {
  iconoTetris, iconoPoker, iconoRefill, iconoDado, iconoRadar
};

const char* nombresMensajes[8] = {
  "cansada", "hambrienta", "meh", "kissy",
  "happy", "busy busy", "Zi", "Nour"
};

const unsigned char* iconosMensajes[8] = {
  iconoZZZ, iconoHambre, iconoCaraSeria, iconoCorazon,
  iconoCaraFeliz, iconoMochila, iconoTick, iconoCruz
};

// -----------------------------------------------------------------------------
// Instancia del display
// -----------------------------------------------------------------------------
Adafruit_SH1107 display = Adafruit_SH1107(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

// Variables internas
static unsigned long lastUpdate = 0;

// Posición previa del cursor del menú principal (compartida entre drawMenu y
// moveCursor para poder forzar su redibujado al re-entrar al menú).
static int menuCursorLastX = -1;

//Variables de gestión de animaciones
static HippoAnimation currentAnimation = ANIM_NORMAL;
static HippoAnimation forcedAnimation = ANIM_NONE;
static unsigned long animationStartTime = 0;
static bool animationLock = false;

// -----------------------------------------------------------------------------
// Inicialización del display
// -----------------------------------------------------------------------------
void Display_begin(){

  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(0x3C)) {
    Serial.println("[Display] ERROR: No se encuentra la pantalla OLED");
    Serial.println("[Display] Revisa las conexiones y la dirección I2C");
  }    
  Serial.println("[Display]  OK: Módulo OLED inicializado correctamente");
  display.clearDisplay();
 
}


// -----------------------------------------------------------------------------
// Mostrar texto centrado en pantalla
// -----------------------------------------------------------------------------

void Display_centerText(const String &text) {
  display.clearDisplay();
  display.setTextSize(TEXT_NORMAL);

  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text.c_str(), 0, 0, &x1, &y1, &w, &h);

  int16_t x = (SCREEN_WIDTH - w) / 2;
  int16_t y = (SCREEN_HEIGHT - h) / 2;

  display.setCursor(x, y);
  display.setTextColor(SH110X_WHITE);
  display.print(text);
  display.display();
}

// -----------------------------------------------------------------------------
// Refresco periódico (por si hay animaciones o indicadores activos)
// -----------------------------------------------------------------------------
static bool s_panelOn = true;

void Display_update() {
  if (!s_panelOn) return;   // panel apagado (suspensión): no malgastar el bus I2C
  unsigned long now = millis();
  if (now - lastUpdate >= DISPLAY_REFRESH_MS) {
    lastUpdate = now;
    display.display();   // volcado periódico (las funciones de dibujo ya
                         // refrescan al cambiar su contenido)
  }
}

// -----------------------------------------------------------------------------
// Limpieza completa de pantalla
// -----------------------------------------------------------------------------
void Display_clear() {
  display.clearDisplay();
  display.display();
}

// -----------------------------------------------------------------------------
// Encendido/apagado del panel OLED (DISPLAYOFF apaga el charge-pump del panel,
// reduciendo el consumo durante la suspensión).
// -----------------------------------------------------------------------------
void Display_setPower(bool on) {
  s_panelOn = on;
  display.oled_command(on ? SH110X_DISPLAYON : SH110X_DISPLAYOFF);
}

// -----------------------------------------------------------------------------
// BLOQUE DE FUNCIONES DE DIBUJADO Y ANIMACIÓN
// -----------------------------------------------------------------------------

void drawMenu(){
  // Texto Enviar mensaje
  display.setCursor(15, 40);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.print("Env");
  // Texto Historial de mensajes
  display.setCursor(57, 40);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.print("Hist");
  // Texto Juegos
  display.setCursor(97, 40);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.print("Jueg");
  //Iconos
  display.drawBitmap(3, 5, flechaBitMap, 40, 30, SH110X_WHITE);
  display.drawBitmap(48, 5, carpetaBitMap, 40, 30, SH110X_WHITE);
  display.drawBitmap(90, 5, iconoMando, 40, 30, SH110X_WHITE);

  // No se vuelca aquí: el volcado a pantalla lo hacen moveCursor() y
  // Display_update(). Así el menú se redibuja en el buffer en cada iteración
  // (reparando lo que pinten las animaciones) sin saturar el bus I2C.
}

// Fuerza que el siguiente moveCursor() repinte el cursor del menú principal
// (necesario al (re)entrar al IDLE, tras un Display_clear()).
void Display_resetMenuCursor() {
  menuCursorLastX = -1;
}

bool animateHippo() {
  static int frame = 0;
  static unsigned long lastFrameTime = 0;
  static bool needsRedraw = true;
  const unsigned long FRAME_DELAY_MS = 350;
  
  unsigned long now = millis();
  
  // Si ya dibujamos este frame y no ha pasado el tiempo suficiente, salir
  if (!needsRedraw && (now - lastFrameTime < FRAME_DELAY_MS)) {
    return false;
  }
  
  // Es hora de cambiar de frame
  if (now - lastFrameTime >= FRAME_DELAY_MS) {
    frame = (frame + 1) % 9;
    needsRedraw = true;
    lastFrameTime = now;
  }
  
  // Dibujar el frame actual
  if (needsRedraw) {
    // Borrar área del hipopótamo
    display.fillRect(50, 55, 81, 81, SH110X_BLACK);
    
    // Dibujar frame actual
    switch (frame) {
      case 0: display.drawBitmap(65, 70, hippoBitMap60, 60, 60, SH110X_WHITE); break;
      case 1: display.drawBitmap(65, 70, hippoBitMap61, 61, 61, SH110X_WHITE); break;
      case 2: display.drawBitmap(65, 70, hippoBitMap62, 63, 63, SH110X_WHITE); break;
      case 3: display.drawBitmap(65, 70, hippoBitMap63, 63, 63, SH110X_WHITE); break;
      case 4: display.drawBitmap(65, 70, hippoBitMap64, 64, 64, SH110X_WHITE); break;
      case 5: display.drawBitmap(65, 70, hippoBitMap63, 63, 63, SH110X_WHITE); break;
      case 6: display.drawBitmap(65, 70, hippoBitMap62, 62, 62, SH110X_WHITE); break;
      case 7: display.drawBitmap(65, 70, hippoBitMap61, 61, 61, SH110X_WHITE); break;
      case 8: display.drawBitmap(65, 70, hippoBitMap60, 60, 60, SH110X_WHITE); break;
    }
    
    display.display();
    needsRedraw = false;
  }
  
  return true;
}

bool animateHippoWithZzz() {
  static unsigned long lastFrameTime = 0;
  static unsigned long lastZzzSpawnTime = 0;
  static int frame = 0;
  
  // Sistema de partículas MEJORADO
  #define MAX_ZZZ 5  // Menos ZZZ para que no sea caótico
  static struct {
    int y;
    int prevY;  // Para borrar la posición anterior
    bool active;
  } zzzParticles[MAX_ZZZ];
  
  static bool initialized = false;
  
  // CONFIGURACIÓN MEJORADA
  const unsigned long FRAME_DELAY = 350;     // Hipopótamo normal
  const unsigned long ZZZ_SPAWN_DELAY = 400; // Nueva ZZZ cada 400ms (RITMO BUENO)
  const int ZZZ_SPEED = 2;                   // Velocidad SUAVE (antes era 3)
  const int ZZZ_START_Y = 95;                // Empezar cerca de la boca
  const int ZZZ_END_Y = -25;                 // Salir completamente por arriba
  const int ZZZ_HEIGHT = 25;                 // Altura total de las 3 Z

  unsigned long now = millis();
  
  // INICIALIZACIÓN
  if (!initialized) {
    for (int i = 0; i < MAX_ZZZ; i++) {
      zzzParticles[i].active = false;
      zzzParticles[i].y = 0;
      zzzParticles[i].prevY = 0;
    }
    initialized = true;
  }
  
  // 1. ANIMACIÓN DEL HIPOPÓTAMO
  if (now - lastFrameTime >= FRAME_DELAY) {
    // Borrar solo el área del hipopótamo (NO las ZZZ)
    display.fillRect(65, 70, 60, 60, SH110X_BLACK);
    
    frame = (frame + 1) % 9;
    
    switch (frame) {
      case 0: display.drawBitmap(65, 70, hippoBitMap60, 60, 60, SH110X_WHITE); break;
      case 1: display.drawBitmap(65, 70, hippoBitMap61, 61, 61, SH110X_WHITE); break;
      case 2: display.drawBitmap(65, 70, hippoBitMap62, 63, 63, SH110X_WHITE); break;
      case 3: display.drawBitmap(65, 70, hippoBitMap63, 63, 63, SH110X_WHITE); break;
      case 4: display.drawBitmap(65, 70, hippoBitMap64, 64, 64, SH110X_WHITE); break;
      case 5: display.drawBitmap(65, 70, hippoBitMap63, 63, 63, SH110X_WHITE); break;
      case 6: display.drawBitmap(65, 70, hippoBitMap62, 62, 62, SH110X_WHITE); break;
      case 7: display.drawBitmap(65, 70, hippoBitMap61, 61, 61, SH110X_WHITE); break;
      case 8: display.drawBitmap(65, 70, hippoBitMap60, 60, 60, SH110X_WHITE); break;
    }
    
    lastFrameTime = now;
  }
  
  // 2. GENERAR NUEVAS ZZZ (con ritmo)
  if (now - lastZzzSpawnTime >= ZZZ_SPAWN_DELAY) {
    // Buscar slot libre
    for (int i = 0; i < MAX_ZZZ; i++) {
      if (!zzzParticles[i].active) {
        zzzParticles[i].y = ZZZ_START_Y;
        zzzParticles[i].prevY = ZZZ_START_Y;
        zzzParticles[i].active = true;
        break;
      }
    }
    lastZzzSpawnTime = now;
  }
  
  // 3. PRIMERO: BORRAR TODAS LAS ZZZ ANTERIORES
  for (int i = 0; i < MAX_ZZZ; i++) {
    if (zzzParticles[i].active) {
      // Solo borrar si la posición anterior era visible
      if (zzzParticles[i].prevY >= 0 && zzzParticles[i].prevY <= 128) {
        display.fillRect(50, zzzParticles[i].prevY - 16, 20, ZZZ_HEIGHT, SH110X_BLACK);
      }
    }
  }
  
  // 4. MOVER TODAS LAS ZZZ
  for (int i = 0; i < MAX_ZZZ; i++) {
    if (zzzParticles[i].active) {
      // Guardar posición anterior antes de mover
      zzzParticles[i].prevY = zzzParticles[i].y;
      
      // Mover hacia arriba
      zzzParticles[i].y -= ZZZ_SPEED;
      
      // Si sale completamente de pantalla, desactivar
      if (zzzParticles[i].y <= ZZZ_END_Y) {
        zzzParticles[i].active = false;
      }
    }
  }
  
  // 5. DIBUJAR TODAS LAS ZZZ NUEVAS
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  
  for (int i = 0; i < MAX_ZZZ; i++) {
    if (zzzParticles[i].active && zzzParticles[i].y >= 0 && zzzParticles[i].y <= 128) {
      // Dibujar las 3 Z en cascada
      display.setCursor(60, zzzParticles[i].y);
      display.print("Z");
      
      display.setCursor(55, zzzParticles[i].y - 8);
      display.print("Z");
      
      display.setCursor(50, zzzParticles[i].y - 16);
      display.print("Z");
    }
  }
  
  // 6. ACTUALIZAR PANTALLA
  display.display();
  
  return false;
}
void animateHippoAproaching() {
  static int frame = 0;    // Control del frame de animación
  static int size = 60;    // Tamaño inicial del hipopótamo
  static int posX = 65;    // Coordenada X inicial
  static int posY = 70;    // Coordenada Y inicial

  display.clearDisplay();//limpia la pantalla antes de empezar

  while (size <= 110) {    // Bucle para completar toda la animación
    // Borra el área dinámica del hipopótamo
    display.fillRect(posX - 5, posY - 5, size + 10, size + 10, SH110X_BLACK);

    // Selecciona el bitmap según el tamaño y el frame actual
    if (frame % 2 == 0) {
      // Hipopótamo normal
      switch (size) {
        case 60: display.drawBitmap(posX, posY, hippoBitMap60, size, size, SH110X_WHITE); break;
        case 65: display.drawBitmap(posX, posY, hippoBitMap65, size, size, SH110X_WHITE); break;
        case 70: display.drawBitmap(posX, posY, hippoBitMap70, size, size, SH110X_WHITE); break;
        case 75: display.drawBitmap(posX, posY, hippoBitMap75, size, size, SH110X_WHITE); break;
        case 80: display.drawBitmap(posX, posY, hippoBitMap80, size, size, SH110X_WHITE); break;
        case 85: display.drawBitmap(posX, posY, hippoBitMap85, size, size, SH110X_WHITE); break;
        case 90: display.drawBitmap(posX, posY, hippoBitMap90, size, size, SH110X_WHITE); break;
        case 95: display.drawBitmap(posX, posY, hippoBitMap95, size, size, SH110X_WHITE); break;
        case 100: display.drawBitmap(posX, posY, hippoBitMap100, size, size, SH110X_WHITE); break;
        case 105: display.drawBitmap(posX, posY, hippoBitMap105, size, size, SH110X_WHITE); break;
        case 110: display.drawBitmap(posX, posY, hippoBitMap110, size, size, SH110X_WHITE); break;
      }
    } else {
      // Hipopótamo invertido
      switch (size) {
        case 60: display.drawBitmap(posX, posY, hippoBitMapInv60, size, size, SH110X_WHITE); break;
        case 65: display.drawBitmap(posX, posY, hippoBitMapInv65, size, size, SH110X_WHITE); break;
        case 70: display.drawBitmap(posX, posY, hippoBitMapInv70, size, size, SH110X_WHITE); break;
        case 75: display.drawBitmap(posX, posY, hippoBitMapInv75, size, size, SH110X_WHITE); break;
        case 80: display.drawBitmap(posX, posY, hippoBitMapInv80, size, size, SH110X_WHITE); break;
        case 85: display.drawBitmap(posX, posY, hippoBitMapInv85, size, size, SH110X_WHITE); break;
        case 90: display.drawBitmap(posX, posY, hippoBitMapInv90, size, size, SH110X_WHITE); break;
        case 95: display.drawBitmap(posX, posY, hippoBitMapInv95, size, size, SH110X_WHITE); break;
        case 100: display.drawBitmap(posX, posY, hippoBitMapInv100, size, size, SH110X_WHITE); break;
        case 105: display.drawBitmap(posX, posY, hippoBitMapInv105, size, size, SH110X_WHITE); break;
        case 110: display.drawBitmap(posX, posY, hippoBitMapInv110, size, size, SH110X_WHITE); break;
      }
    }

    // Actualiza la pantalla
    display.display();

    // Avanza el frame
    frame++;

    // Incrementa el tamaño del hipopótamo y ajusta su posición en diagonal hacia arriba a la izquierda
    if (frame % 2 == 0) {
      size += 5; // Incrementa el tamaño
      posX -= 4; // Desplaza hacia la izquierda
      posY -= 4; // Desplaza hacia arriba
    }

    // Pausa para la animación
    delay(225);
  }

  // Si el tamaño es el máximo (110x110)
  display.setCursor(0, 25);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.print("Muacky :)");
  display.display();

  // Pausa de 2.5 segundos mirando al usuario
  delay(2500);

  // Borra toda la pantalla antes de reiniciar
  display.clearDisplay();

  // Reinicia valores
  size = 60;
  posX = 65;
  posY = 70;
  frame = 0;
}

void animateHippoRetreating() {
  static int frameR = 0;      // Control del frame de animación
  static int sizeR = 110;     // Tamaño inicial del hipopótamo
  static int posXR = 25;      // Coordenada X inicial
  static int posYR = 30;      // Coordenada Y inicial

  while (sizeR > 60) {  // Mantén la animación activa hasta que alcance el tamaño mínimo
    // Borra el área dinámica del hipopótamo
    display.fillRect(posXR - 5, posYR - 5, sizeR + 10, sizeR + 10, SH110X_BLACK);

    // Selecciona el bitmap según el tamaño y el frame actual
    if (frameR % 2 == 0) {
      switch (sizeR) {
        case 110: display.drawBitmap(posXR, posYR, hippoBitMap110, sizeR, sizeR, SH110X_WHITE); break;
        case 105: display.drawBitmap(posXR, posYR, hippoBitMap105, sizeR, sizeR, SH110X_WHITE); break;
        case 100: display.drawBitmap(posXR, posYR, hippoBitMap100, sizeR, sizeR, SH110X_WHITE); break;
        case 95: display.drawBitmap(posXR, posYR, hippoBitMap95, sizeR, sizeR, SH110X_WHITE); break;
        case 90: display.drawBitmap(posXR, posYR, hippoBitMap90, sizeR, sizeR, SH110X_WHITE); break;
        case 85: display.drawBitmap(posXR, posYR, hippoBitMap85, sizeR, sizeR, SH110X_WHITE); break;
        case 80: display.drawBitmap(posXR, posYR, hippoBitMap80, sizeR, sizeR, SH110X_WHITE); break;
        case 75: display.drawBitmap(posXR, posYR, hippoBitMap75, sizeR, sizeR, SH110X_WHITE); break;
        case 70: display.drawBitmap(posXR, posYR, hippoBitMap70, sizeR, sizeR, SH110X_WHITE); break;
        case 65: display.drawBitmap(posXR, posYR, hippoBitMap65, sizeR, sizeR, SH110X_WHITE); break;
        case 60: display.drawBitmap(posXR, posYR, hippoBitMap60, sizeR, sizeR, SH110X_WHITE); break;
      }
    } else {
      switch (sizeR) {
        case 110: display.drawBitmap(posXR, posYR, hippoBitMapInv110, sizeR, sizeR, SH110X_WHITE); break;
        case 105: display.drawBitmap(posXR, posYR, hippoBitMapInv105, sizeR, sizeR, SH110X_WHITE); break;
        case 100: display.drawBitmap(posXR, posYR, hippoBitMapInv100, sizeR, sizeR, SH110X_WHITE); break;
        case 95: display.drawBitmap(posXR, posYR, hippoBitMapInv95, sizeR, sizeR, SH110X_WHITE); break;
        case 90: display.drawBitmap(posXR, posYR, hippoBitMapInv90, sizeR, sizeR, SH110X_WHITE); break;
        case 85: display.drawBitmap(posXR, posYR, hippoBitMapInv85, sizeR, sizeR, SH110X_WHITE); break;
        case 80: display.drawBitmap(posXR, posYR, hippoBitMapInv80, sizeR, sizeR, SH110X_WHITE); break;
        case 75: display.drawBitmap(posXR, posYR, hippoBitMapInv75, sizeR, sizeR, SH110X_WHITE); break;
        case 70: display.drawBitmap(posXR, posYR, hippoBitMapInv70, sizeR, sizeR, SH110X_WHITE); break;
        case 65: display.drawBitmap(posXR, posYR, hippoBitMapInv65, sizeR, sizeR, SH110X_WHITE); break;
        case 60: display.drawBitmap(posXR, posYR, hippoBitMapInv60, sizeR, sizeR, SH110X_WHITE); break;
      }
    }

    // Actualiza la pantalla
    display.display();

    // Decrementa el tamaño del hipopótamo y ajusta su posición en diagonal hacia abajo a la derecha
    sizeR -= 5;
    posXR += 4;
    posYR += 4;

    // Avanza el frame
    frameR++;

    // Pausa para la animación
    delay(225);
  }

  // Reinicia valores
  sizeR = 110;
  posXR = 25;
  posYR = 30;
  frameR = 0;
}

bool animateHippoBonked() {
  static enum {
    STATE_INIT,
    STATE_BONK_1_WITH_STICK,
    STATE_BONK_1_CLEAR,
    STATE_BONK_2_WITH_STICK,
    STATE_BONK_2_CLEAR,
    STATE_BONK_3_WITH_STICK,
    STATE_BONK_3_CLEAR,
    STATE_FLIPPED_HIPPO,
    STATE_DONE
  } animState = STATE_INIT;
  
  static unsigned long stateStartTime = 0;
  static int posX = 65;
  static int posY = 70;
  static int bonkCount = 0;

  unsigned long now = millis();
  bool animationFinished = false;

  switch (animState) {
    
    // ----------------------------------------------------
    case STATE_INIT:
      // Reinicializar variables
      posX = 65;
      posY = 70;
      bonkCount = 0;
      animState = STATE_BONK_1_WITH_STICK;
      stateStartTime = now;
      break;
    
    // ----------------------------------------------------
    case STATE_BONK_1_WITH_STICK:
      // Primer golpe con palo (400ms)
      if (now - stateStartTime >= 400) {
        // Dibujar palo
        for (int j = 0; j < 4; j++) {
          display.drawLine(posX - 30 + j, posY + 15 + j, posX + 5 + j, posY - 15 + j, SH110X_WHITE);
        }
        
        // Dibujar texto "bonk"
        display.setCursor(posX + 10, posY - 10);
        display.print("bonk");
        
        // Dibujar hipopótamo normal
        display.drawBitmap(posX, posY, hippoBitMap60, 60, 60, SH110X_WHITE);
        display.display();
        
        bonkCount++;
        animState = STATE_BONK_1_CLEAR;
        stateStartTime = now;
      }
      break;
    
    // ----------------------------------------------------
    case STATE_BONK_1_CLEAR:
      // Limpiar palo y texto (100ms)
      if (now - stateStartTime >= 100) {
        // Borrar palo
        for (int j = 0; j < 4; j++) {
          display.drawLine(posX - 30 + j, posY + 15 + j, posX + 5 + j, posY - 15 + j, SH110X_BLACK);
        }
        
        // Borrar texto
        display.fillRect(posX + 10, posY - 10, 40, 10, SH110X_BLACK);
        
        // Volver a dibujar hipopótamo (sin palo)
        display.drawBitmap(posX, posY, hippoBitMap60, 60, 60, SH110X_WHITE);
        display.display();
        
        animState = STATE_BONK_2_WITH_STICK;
        stateStartTime = now;
      }
      break;
    
    // ----------------------------------------------------
    case STATE_BONK_2_WITH_STICK:
      // Segundo golpe con palo (400ms)
      if (now - stateStartTime >= 400) {
        // Dibujar palo
        for (int j = 0; j < 4; j++) {
          display.drawLine(posX - 30 + j, posY + 15 + j, posX + 5 + j, posY - 15 + j, SH110X_WHITE);
        }
        
        // Dibujar texto "bonk"
        display.setCursor(posX + 10, posY - 10);
        display.print("bonk");
        
        // Dibujar hipopótamo
        display.drawBitmap(posX, posY, hippoBitMap60, 60, 60, SH110X_WHITE);
        display.display();
        
        bonkCount++;
        animState = STATE_BONK_2_CLEAR;
        stateStartTime = now;
      }
      break;
    
    // ----------------------------------------------------
    case STATE_BONK_2_CLEAR:
      // Limpiar palo y texto (100ms)
      if (now - stateStartTime >= 100) {
        // Borrar palo
        for (int j = 0; j < 4; j++) {
          display.drawLine(posX - 30 + j, posY + 15 + j, posX + 5 + j, posY - 15 + j, SH110X_BLACK);
        }
        
        // Borrar texto
        display.fillRect(posX + 10, posY - 10, 40, 10, SH110X_BLACK);
        
        // Dibujar hipopótamo
        display.drawBitmap(posX, posY, hippoBitMap60, 60, 60, SH110X_WHITE);
        display.display();
        
        animState = STATE_BONK_3_WITH_STICK;
        stateStartTime = now;
      }
      break;
    
    // ----------------------------------------------------
    case STATE_BONK_3_WITH_STICK:
      // Tercer golpe con palo (400ms)
      if (now - stateStartTime >= 400) {
        // Dibujar palo
        for (int j = 0; j < 4; j++) {
          display.drawLine(posX - 30 + j, posY + 15 + j, posX + 5 + j, posY - 15 + j, SH110X_WHITE);
        }
        
        // Dibujar texto "bonk"
        display.setCursor(posX + 10, posY - 10);
        display.print("bonk");
        
        // Dibujar hipopótamo
        display.drawBitmap(posX, posY, hippoBitMap60, 60, 60, SH110X_WHITE);
        display.display();
        
        bonkCount++;
        animState = STATE_BONK_3_CLEAR;
        stateStartTime = now;
      }
      break;
    
    // ----------------------------------------------------
    case STATE_BONK_3_CLEAR:
      // Limpiar palo y texto final (100ms)
      if (now - stateStartTime >= 100) {
        // Borrar palo final
        for (int j = 0; j < 4; j++) {
          display.drawLine(posX - 30 + j, posY + 15 + j, posX + 5 + j, posY - 15 + j, SH110X_BLACK);
        }
        
        // Borrar texto final
        display.fillRect(posX + 10, posY - 10, 40, 10, SH110X_BLACK);
        display.display();
        
        animState = STATE_FLIPPED_HIPPO;
        stateStartTime = now;
      }
      break;
    
    // ----------------------------------------------------
    case STATE_FLIPPED_HIPPO:
      // Mostrar hipopótamo patas arriba (500ms)
      if (now - stateStartTime >= 500) {
        // Borrar hipopótamo anterior
        display.fillRect(posX, posY, 60, 60, SH110X_BLACK);
        
        // Dibujar hipopótamo patas arriba
        display.drawBitmap(posX, posY, hippoBitMapBonked60, 60, 60, SH110X_WHITE);
        display.display();

        animState = STATE_DONE;
        stateStartTime = now;
      }
      break;
    
    // ----------------------------------------------------
    case STATE_DONE:
      // Mantener posición final (1000ms) antes de terminar
      if (now - stateStartTime >= 1000) {
        // Resetear para próxima ejecución
        animState = STATE_INIT;
        animationFinished = true;
      }
      break;
  }
  
  return animationFinished;
}

bool animateHippoChasingHeart() {
  static enum {
    STATE_INIT,
    STATE_PEEKING,
    STATE_HEART_FALLING,
    STATE_CHASING,
    STATE_JUMPING,
    STATE_DONE
  } animState = STATE_INIT;
  
  static unsigned long stateStartTime = 0;
  static int hippoX, hippoY, heartX, heartY;

  unsigned long now = millis();
  bool animationFinished = false;

  switch (animState) {

    // ----------------------------------------------------
    case STATE_INIT:
      // Inicializar variables
      hippoX = 100;
      hippoY = 90;
      heartX = 10;
      heartY = 64;
      animState = STATE_PEEKING;
      stateStartTime = now;
      // Dibujar posición inicial
      display.drawBitmap(hippoX, hippoY + 12, hippoBitMap40, 40, 20, SH110X_WHITE);
      display.display();
      break;
    
    // ----------------------------------------------------
    case STATE_PEEKING:
      // El hipopótamo asoma por 500ms
      if (now - stateStartTime >= 500) {
        animState = STATE_HEART_FALLING;
        stateStartTime = now;
      }
      break;
    
    // ----------------------------------------------------
    case STATE_HEART_FALLING:
      // Mover corazón hacia abajo cada 20ms
      if (now - stateStartTime >= 20) {
        // Borrar corazón anterior
        drawHeart(heartX, heartY, SH110X_BLACK);
        
        // Mover corazón
        heartY++;
        
        // Dibujar corazón nuevo
        drawHeart(heartX, heartY, SH110X_WHITE);
        display.display();
        
        stateStartTime = now;
        
        // Comprobar si llegó al suelo
        if (heartY >= hippoY + 15) {
          // Borrar la cabeza del hipopótamo que asomaba
          display.fillRect(hippoX, hippoY + 12, 40, 20, SH110X_BLACK);
          display.display();
          animState = STATE_CHASING;
          stateStartTime = now;
        }
      }
      break;
    
    // ----------------------------------------------------
    case STATE_CHASING:
      // Mover hipopótamo hacia el corazón cada 20ms
      if (now - stateStartTime >= 20) {
        // Borrar hipopótamo anterior
        display.fillRect(hippoX, hippoY, 40, 40, SH110X_BLACK);
        
        // Mover hipopótamo hacia la izquierda
        hippoX--;
        
        // Dibujar hipopótamo y corazón
        display.drawBitmap(hippoX, hippoY, hippoBitMap40, 40, 40, SH110X_WHITE);
        drawHeart(heartX, heartY, SH110X_WHITE);
        display.display();
        
        stateStartTime = now;
        
        // Comprobar si atrapó el corazón
        if (abs(hippoX - heartX) < 5) {
          // Borrar corazón
          drawHeart(heartX, heartY, SH110X_BLACK);
          display.display();
          animState = STATE_JUMPING;
          stateStartTime = now;
        }
      }
      break;
    
    // ----------------------------------------------------
    case STATE_JUMPING: {
      // Saltos de felicidad (10 saltos = 5 ciclos arriba/abajo)
      static int jumpCount = 0;
      static bool jumpingUp = true;

      if (now - stateStartTime >= 150) {
        // Borrar hipopótamo anterior
        display.fillRect(hippoX, hippoY, 40, 40, SH110X_BLACK);
        
        // Mover arriba o abajo
        if (jumpingUp) {
          hippoY -= 10; // Subir
        } else {
          hippoY += 10; // Bajar
        }
        
        // Dibujar hipopótamo
        display.drawBitmap(hippoX, hippoY, hippoBitMap40, 40, 40, SH110X_WHITE);
        display.display();
        
        jumpingUp = !jumpingUp;
        jumpCount++;
        stateStartTime = now;
        
        if (jumpCount >= 10) { // 5 ciclos completos
          jumpCount = 0;
          animState = STATE_DONE;
          stateStartTime = now;
        }
      }
      break;
    }

    // ----------------------------------------------------
    case STATE_DONE:
      // Limpiar y terminar después de breve pausa
      if (now - stateStartTime >= 100) {
        // Borrar el hipopótamo
        display.fillRect(hippoX, hippoY, 40, 40, SH110X_BLACK);
        display.display();
        
        // Resetear estado para próxima ejecución
        animState = STATE_INIT;
        animationFinished = true;
      }
      break;
  }
  
  return animationFinished;
}

void drawHeart(int x, int y, int color) {
  // Un corazón relleno y bien definido con píxeles
  static const uint8_t heartBitmap[10][10] = {
    {0, 1, 1, 0, 0, 0, 0, 1, 1, 0},
    {1, 1, 1, 1, 0, 0, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 1, 1, 1, 1, 1, 1, 1, 1, 0},
    {0, 0, 1, 1, 1, 1, 1, 1, 0, 0},
    {0, 0, 0, 1, 1, 1, 1, 0, 0, 0},
    {0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 0, 0, 0, 0},
  };

  // Dibujar el bitmap del corazón
  for (int i = 0; i < 10; i++) {
    for (int j = 0; j < 10; j++) {
      if (heartBitmap[i][j]) {
        display.drawPixel(x + j - 5, y + i - 5, color);
      }
    }
  }
}

bool animateHippoGivingHeart() {
  static enum {
    STATE_INIT,
    STATE_PHASE1_SLOW_MOVE,
    STATE_PHASE2_FAST_MOVE,
    STATE_PHASE3_JUMPING,
    STATE_PAUSE,
    STATE_CLEANUP,
    STATE_DONE
  } animState = STATE_INIT;
  
  static unsigned long stateStartTime = 0;
  static unsigned long lastMoveTime = 0;
  static int hippoX, hippoY, heartX, heartY;
  static const int screenWidth = 128;
  static const int screenHeight = 128;
  static int mouthOffsetX = -5; // Desplazamiento del corazón en la boca
  static int mouthOffsetY = 20; // Posición vertical del corazón en la boca
  static int jumpCounter = 0;
  static bool heartDrawn = false;

  unsigned long now = millis();
  bool animationFinished = false;

  switch (animState) {
    
    // ----------------------------------------------------
    case STATE_INIT:
      // Inicializar variables
      hippoX = 100;
      hippoY = 90;
      heartX = screenWidth / 2 + 10;
      heartY = 110;
      jumpCounter = 0;
      heartDrawn = false;
      
      // Dibujar corazón inicial
      if (!heartDrawn) {
        drawHeart(heartX, heartY, SH110X_WHITE);
        display.display();
        heartDrawn = true;
      }
      
      animState = STATE_PHASE1_SLOW_MOVE;
      stateStartTime = now;
      lastMoveTime = now;
      break;
    
    // ----------------------------------------------------
    case STATE_PHASE1_SLOW_MOVE:
      // Movimiento lento hacia el corazón (1 pixel cada 50ms)
      if (now - lastMoveTime >= 50 && hippoX > heartX + 10) {
        // Borrar área de animación (sin iconos superiores)
        display.fillRect(0, 50, screenWidth, screenHeight - 50, SH110X_BLACK);
        
        // Mover hipopótamo
        hippoX -= 1;
        
        // Dibujar hipopótamo y corazón
        display.drawBitmap(hippoX, hippoY, hippoBitMap40, 40, 40, SH110X_WHITE);
        drawHeart(heartX, heartY, SH110X_WHITE);
        display.display();
        
        lastMoveTime = now;
      } 
      else if (hippoX <= heartX + 10) {
        // Fin de fase 1
        animState = STATE_PAUSE;
        stateStartTime = now;
      }
      break;
    
    // ----------------------------------------------------
    case STATE_PAUSE:
      // Pausa de 1 segundo entre fases
      if (now - stateStartTime >= 1000) {
        animState = STATE_PHASE2_FAST_MOVE;
        stateStartTime = now;
        lastMoveTime = now;
      }
      break;
    
    // ----------------------------------------------------
    case STATE_PHASE2_FAST_MOVE:
      // Movimiento rápido (5 pixels cada 30ms)
      if (now - lastMoveTime >= 30 && hippoX > heartX - 5) {
        // Borrar área de animación
        display.fillRect(0, 50, screenWidth, screenHeight - 50, SH110X_BLACK);
        
        // Mover hipopótamo rápidamente
        hippoX -= 5;
        
        // Dibujar hipopótamo y corazón
        display.drawBitmap(hippoX, hippoY, hippoBitMap40, 40, 40, SH110X_WHITE);
        drawHeart(heartX, heartY, SH110X_WHITE);
        display.display();
        
        lastMoveTime = now;
      }
      else if (hippoX <= heartX - 5) {
        // Fin de fase 2 - el hipopótamo está en el corazón
        animState = STATE_PHASE3_JUMPING;
        stateStartTime = now;
        lastMoveTime = now;
      }
      break;
    
    // ----------------------------------------------------
    case STATE_PHASE3_JUMPING:
      // Saltitos sincronizados (15 saltos)
      if (now - lastMoveTime >= 60) {
        // Borrar área de animación
        display.fillRect(0, 50, screenWidth, screenHeight - 50, SH110X_BLACK);
        
        // Saltar arriba o abajo
        if (jumpCounter % 2 == 0) {
          hippoY -= 10; // Subir
        } else {
          hippoY += 10; // Bajar
        }
        
        // Moverse hacia la izquierda
        hippoX -= 5;
        
        // Dibujar hipopótamo con corazón en la boca
        display.drawBitmap(hippoX, hippoY, hippoBitMap40, 40, 40, SH110X_WHITE);
        drawHeart(hippoX + mouthOffsetX, hippoY + mouthOffsetY, SH110X_WHITE);
        display.display();
        
        jumpCounter++;
        lastMoveTime = now;
        
        if (jumpCounter >= 20) { // 10 ciclos completos (arriba/abajo)
          animState = STATE_CLEANUP;
          stateStartTime = now;
        }
      }
      break;
    
    // ----------------------------------------------------
    case STATE_CLEANUP:
      // Borrar área de animación final
      display.fillRect(0, 50, screenWidth, screenHeight - 50, SH110X_BLACK);
      display.display();
      
      animState = STATE_DONE;
      stateStartTime = now;
      break;
    
    // ----------------------------------------------------
    case STATE_DONE:
      // Breve pausa final de 500ms antes de terminar
      if (now - stateStartTime >= 500) {
        // Resetear estado para próxima ejecución
        animState = STATE_INIT;
        animationFinished = true;
      }
      break;
  }
  
  return animationFinished;
}

void moveCursor() {
  static int stableValue = 0;
  static unsigned long lastPotRead = 0;
  const unsigned long POT_READ_INTERVAL = 20;
  
  unsigned long now = millis();
  if (now - lastPotRead < POT_READ_INTERVAL) {
    return;
  }
  lastPotRead = now;
  
  int raw = analogRead(A0);
  
  static int readings[3] = {0, 0, 0};
  static int readIndex = 0;
  
  readings[readIndex] = raw;
  readIndex = (readIndex + 1) % 3;
  
  int smoothed = (readings[0] + readings[1] + readings[2]) / 3;
  
  // IMPORTANTE: Resetear timer de animación si hay cambio
  if (abs(smoothed - stableValue) > 8) {
    stableValue = smoothed;
    resetAnimationTimer(); // Esta línea es crucial
  }
  
  int cursorX;
  int sel = 0;
  
  if (stableValue < 341) {
    cursorX = 20;  
    sel = 0; 
  } 
  else if (stableValue < 682) {
    cursorX = 68; 
    sel = 1; 
  } 
  else {
    cursorX = 115; 
    sel = 2; 
  }
  
  cursorPos = sel;
  
  if (cursorX != menuCursorLastX) {
    if (menuCursorLastX >= 0) {
      display.fillTriangle(
        menuCursorLastX, 49,
        menuCursorLastX - 3, 55,
        menuCursorLastX + 3, 55,
        SH110X_BLACK
      );
    }

    display.fillTriangle(
      cursorX, 49,
      cursorX - 3, 55,
      cursorX + 3, 55,
      SH110X_WHITE
    );

    display.display();
    menuCursorLastX = cursorX;
  }
}

void drawMorseSuggestion() {
  const int colWidth = 60;
  const int rowHeight = 10;
  const int startX = 0;
  const int startY = 48;
  const int visibleRows = 5;
  const int cols = 2;
  const int itemsPerPage = visibleRows * cols;

  // Filtrar morseTable según el prefijo actual (morsePrefix)
  String filtered[50];
  int filteredCount = 0;

  for (int i = 0; i < morseTableSize; i++) {
    int sep = morseTable[i].indexOf(':');
    if (sep != -1) {
      String code = morseTable[i].substring(sep + 2);  // después de ": "
      if (morsePrefix != "" && code.startsWith(morsePrefix)) {
        filtered[filteredCount++] = morseTable[i];
      }
    }
  }

  if (filteredCount == 0) {
    // Mostrar todo si no hay coincidencias o prefijo vacío
    for (int i = 0; i < morseTableSize; i++) {
      filtered[i] = morseTable[i];
    }
    filteredCount = morseTableSize;
  }

  // Lectura del potenciómetro suavizada
  int potValue = getPotValue(1023);
  int maxOffset = max(0, filteredCount - itemsPerPage);
  int scrollSteps = maxOffset + 1;
  int stepSize = 1024 / scrollSteps;
  int newOffset = constrain(potValue / stepSize, 0, maxOffset);

  if (newOffset != morseScrollOffset || lastMorsePrefix != morsePrefix || mensajeEnviado || mensajeCancelado) {
    morseScrollOffset = newOffset;
    lastMorseScrollOffset = morseScrollOffset;

    // Limpiar solo la zona inferior
    display.fillRect(0, startY, 128, rowHeight * visibleRows, SH110X_BLACK);

    for (int i = 0; i < itemsPerPage; i++) {
      int index = morseScrollOffset + i;
      if (index >= filteredCount) break;

      int row = i / cols;
      int col = i % cols;
      int x = startX + col * colWidth;
      int y = startY + row * rowHeight;

      display.setCursor(x, y);
      display.println(filtered[index]);
    }

    display.display();
		lastMorsePrefix = morsePrefix;
  }
}

void drawMorseTable() {
  const int colWidth = 60;
  const int rowHeight = 10;
  const int startX = 0;
  const int startY = 48;  
  const int morseTableCols = 2;
  const int morseTableVisibleRows = 4;  
  const int morseTableItemsPerPage = morseTableCols * morseTableVisibleRows;

  int potValue = getPotValue(1023);

  // Dividir el rango del potenciómetro en secciones discretas
  int maxOffset = max(0, morseTableSize - morseTableItemsPerPage);
  int scrollSteps = maxOffset + 1;
  int stepSize = 1024 / scrollSteps;
  int newScrollOffset = constrain(potValue / stepSize, 0, maxOffset);

  // Siempre actualizar
  morseScrollOffset = newScrollOffset;

  // Borrar solo el área inferior del listado
  display.fillRect(0, startY, 128, rowHeight * morseTableVisibleRows, SH110X_BLACK);

  // Dibujar nuevas letras/morse
  for (int i = 0; i < morseTableItemsPerPage; i++) {
    int index = morseScrollOffset + i;
    if (index >= morseTableSize) break;

    int row = i / morseTableCols;
    int col = i % morseTableCols;

    int x = startX + col * colWidth;
    int y = startY + row * rowHeight;

    display.setCursor(x, y);
    display.println(morseTable[index]);
  }

  display.display();  // Mostrar los cambios
}

// Rueda de letras: arriba el texto en construccion (con cursor "_"); en el
// centro la letra seleccionada en grande con marco y sus vecinas a los lados;
// abajo las pistas de control. Solo vuelca a la pantalla cuando cambia algo
// (o si force), para no saturar el bus I2C ni parpadear.
void drawDial(const String &msg, const char *charset, int len, int index, bool force) {
  static int    lastIndex = -999;
  static String lastMsg = String((char)1);   // valor imposible -> primer dibujo
  if (!force && index == lastIndex && msg == lastMsg) return;
  lastIndex = index;
  lastMsg = msg;

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  // --- Cabecera + mensaje en construccion ---
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Escribe:");

  // Ajuste simple por ancho (21 chars/linea, hasta 4 lineas). Si se pasa,
  // mostramos el final (lo ultimo escrito). El "_" marca donde se escribe.
  const int MAXC = 21;
  const int MAXLINES = 4;
  String body = msg + "_";
  int maxChars = MAXC * MAXLINES;
  if ((int)body.length() > maxChars) body = body.substring(body.length() - maxChars);
  int y = 12;
  for (int i = 0; i < (int)body.length(); i += MAXC) {
    display.setCursor(0, y);
    display.print(body.substring(i, min((int)body.length(), i + MAXC)));
    y += 10;
  }

  // --- Rueda de caracteres ---
  // (espacio se dibuja como "_" para que sea visible)
  const int cy = 74;
  const int cw = 18;                  // ancho de un caracter a tamano 3 (6*3)
  const int cx = (128 - cw) / 2;

  // Vecinas (pequenas) a izquierda y derecha de la central.
  display.setTextSize(1);
  int neigh[4]      = { index - 2, index - 1, index + 1, index + 2 };
  int neighX[4]     = { 20, 38, 84, 102 };
  for (int i = 0; i < 4; i++) {
    int n = ((neigh[i] % len) + len) % len;
    char c = charset[n];
    display.setCursor(neighX[i], cy + 8);
    display.print(c == ' ' ? '_' : c);
  }

  // Letra central grande con marco.
  int ci = ((index % len) + len) % len;
  char cc = charset[ci];
  display.setTextSize(3);
  display.setCursor(cx, cy);
  display.print(cc == ' ' ? '_' : cc);
  display.drawRect(cx - 6, cy - 4, cw + 12, 30, SH110X_WHITE);

  // --- Pistas de control ---
  display.setTextSize(1);
  display.setCursor(0, 110);
  display.print("A:poner  B:borrar");
  display.setCursor(0, 119);
  display.print("(manten B = enviar)");

  display.display();
}

void drawMorse() {

  // Limpiar solo la zona superior
  display.fillRect(0, 0, 128,40, SH110X_BLACK);

  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.println("CREACION DE MENSAJE");
  display.println();

  display.setTextSize(1);
  display.println("MENSAJE HASTA AHORA:");

  // Mostramos mensaje en texto
  display.setTextSize(1);
  display.println(mensajeAEnviar);

  // Mostramos código Morse
  display.setTextSize(1);
  display.print("Morse: ");
  display.println(morseCode);

  if (morsePrefix.length() == 0) {
    drawMorseTable();
    lastMorsePrefix = "";
  } else {
    drawMorseSuggestion();
  }

  display.display();
}

void drawInstantMessagesMenu(int seleccion, bool force) {
  // Sólo redibujar cuando cambia la selección (o cuando se fuerza al entrar).
  static int lastSel = -1;
  if (!force && seleccion == lastSel) return;
  lastSel = seleccion;

  display.clearDisplay();

  const int cols = 3;
  const int rows = 3;
  const int iconSize = 20;
  const int spacingX = 14;
  const int spacingY = 20;
  const int lift = 6;

  int totalWidth = cols * iconSize + (cols - 1) * spacingX;
  int marginX = max(0, (SCREEN_WIDTH - totalWidth) / 2);
  int marginY = 10;

  for (int i = 0; i < cols * rows; i++) {
    if (i >= 8) continue; // hueco si no hay icono
    int fila = i / cols;
    int col = i % cols;
    int x = marginX + col * (iconSize + spacingX);
    int y = marginY + fila * (iconSize + spacingY);

    if (i == seleccion) {
      y -= lift;
      display.setTextSize(1);
      display.setTextColor(SH110X_WHITE);
      int textW = strlen(nombresMensajes[i]) * 6;
      int textX = x + (iconSize / 2) - (textW / 2);
      int textY = y + iconSize + 2;
      display.setCursor(textX, textY);
      display.print(nombresMensajes[i]);
    }

    display.drawBitmap(x, y, iconosMensajes[i], iconSize, iconSize, SH110X_WHITE);
  }

  display.display();
}

void drawGamesMenu(int seleccion, bool force) {
  // Sólo redibujar cuando cambia la selección (o cuando se fuerza al entrar).
  static int lastSel = -1;
  if (!force && seleccion == lastSel) return;
  lastSel = seleccion;

  display.clearDisplay();

  const int cols = 3;
  const int rows = 2;
  const int iconSize = 20;
  const int spacingX = 14;
  const int spacingY = 35;
  const int lift = 6;

  int totalWidth = cols * iconSize + (cols - 1) * spacingX;
  int marginX = max(0, (SCREEN_WIDTH - totalWidth) / 2);
  int marginY = 20;

  for (int i = 0; i < cols * rows; i++) {
    if (i >= 5) continue;
    int fila = i / cols;
    int col = i % cols;
    int x = marginX + col * (iconSize + spacingX);
    int y = marginY + fila * (iconSize + spacingY);

    if (i == seleccion) {
      y -= lift;
      display.setTextSize(1);
      display.setTextColor(SH110X_WHITE);
      int textW = strlen(nombresJuegos[i]) * 6;
      int textX = x + (iconSize / 2) - (textW / 2);
      int textY = y + iconSize + 2;
      display.setCursor(textX, textY);
      display.print(nombresJuegos[i]);
    }

    display.drawBitmap(x, y, iconosJuegos[i], iconSize, iconSize, SH110X_WHITE);
  }

  display.display();
}

void updateHippoAnimation() {
    static bool wasSleeping = false;
    static bool needsCleanup = false;
    
    // Solo en estado IDLE
    if (mainState != STATE_IDLE) {
        if (currentAnimation != ANIM_NORMAL) {
            needsCleanup = true;
        }
        currentAnimation = ANIM_NORMAL;
        animationLock = false;
        wasSleeping = false;
        return;
    }

    unsigned long now = millis();
    unsigned long inactiveTime = now - lastInteraction;

    // LIMPIAR PANTALLA SI ES NECESARIO
    if (needsCleanup) {
        display.fillRect(20, 50, 88, 75, SH110X_BLACK); // Área de animaciones
        display.display();
        needsCleanup = false;
        currentAnimation = ANIM_NORMAL;
    }

    // 1. Animaciones forzadas (máxima prioridad)
    if (forcedAnimation != ANIM_NONE) {
        // Limpiar antes de nueva animación
        display.fillRect(20, 50, 88, 75, SH110X_BLACK);
        display.display();
        
        currentAnimation = forcedAnimation;
        animationStartTime = now;
        animationLock = true;
        forcedAnimation = ANIM_NONE;
        wasSleeping = false;
    }

    // 2. Animaciones especiales en curso
    if (animationLock) {
        bool animationFinished = false;
        
        switch (currentAnimation) {
            case ANIM_CHASING_HEART:
                animationFinished = animateHippoChasingHeart();
                break;
            case ANIM_GIVING_HEART:
                animationFinished = animateHippoGivingHeart();
                break;
            case ANIM_BONKED:
                animationFinished = animateHippoBonked();
                break;
            default:
                animationLock = false;
                break;
        }
        
        if (animationFinished) {
            animationLock = false;
            lastInteraction = now;
            currentAnimation = ANIM_NORMAL;
            needsCleanup = true; // Marcar para limpiar pantalla
        }
        return;
    }

    // 3. Lógica de sueño/inactividad
    if (inactiveTime > 20000) {
        if (!wasSleeping) {
            // Limpiar antes de sueño
            display.fillRect(20, 50, 88, 75, SH110X_BLACK);
            display.display();
            
            currentAnimation = ANIM_SLEEPY;
            wasSleeping = true;
        }
    } 
    else {
        if (wasSleeping) {
            // Limpiar al salir del sueño
            display.clearDisplay();
            display.display();
            
            currentAnimation = ANIM_NORMAL;
            wasSleeping = false;
        } 
        else if (currentAnimation != ANIM_SLEEPY) {
            currentAnimation = ANIM_NORMAL;
        }
    }

    // 4. EJECUTAR ANIMACIÓN
    switch (currentAnimation) {
        case ANIM_NORMAL:
            animateHippo();
            break;
        case ANIM_SLEEPY:
            animateHippoWithZzz();
            break;
        default:
            animateHippo();
            break;
    }
}

void triggerAnimation(HippoAnimation anim) {
    // Limpiar área de animación antes de nueva animación
    display.fillRect(20, 50, 88, 75, SH110X_BLACK);
    display.display();
    
    forcedAnimation = anim;
    animationLock = false;
    lastInteraction = millis(); // Resetear inactividad
}

void resetAnimationTimer() {
    lastInteraction = millis();
    if (currentAnimation == ANIM_SLEEPY) {
        currentAnimation = ANIM_NORMAL;
    }
}
