#include <Arduino.h>
#include "Config.h"
#include "Display.h"
#include "Morse.h"
#include "States.h"
#include "Inputs.h"


const char* nombresJuegos[5] = {
  "Tetris2v2", "Poker", "RefillGame", "Choose4Me", "HippoRadar"
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
static String currentTitle = "";
static String currentLine  = "";

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
void Display_update() {
  unsigned long now = millis();
  if (now - lastUpdate >= DISPLAY_REFRESH_MS) {
    lastUpdate = now;
  }
  display.display();
}

// -----------------------------------------------------------------------------
// Limpieza completa de pantalla
// -----------------------------------------------------------------------------
void Display_clear() {
  display.clearDisplay();
  display.display();
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
  display.display();
}

void animateHippo() {
  
  static int frame = 0;

  display.fillRect(50, 55, 81, 81, SH110X_BLACK); // Borra únicamente el área del hipopótamo
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
  frame = (frame + 1) % 9;
  delay(350);
}

void animateHippoWithZzz() {
  static int frame = 0;         // Control del frame de la animación del hipopótamo
  static int zzzY = 80;         // Posición inicial de las 'zzz' (cerca de la cabeza del hipopótamo)
  static int prevZzzY = 80;     // Posición previa de las 'zzz'
  static unsigned long lastResetTime = 0; // Tiempo del último reseteo de las 'zzz'
  static bool isPaused = false; // Indica si está en pausa para el reseteo

  // Borra solo el área de las 'zzz' del frame anterior si no están en pausa
  if (!isPaused && prevZzzY > 60) {
    display.fillRect(50, prevZzzY - 16, 20, 24, SH110X_BLACK);
  }

  // Dibujar solo la parte dinámica (área del hipopótamo)
  display.fillRect(50, 55, 81, 81, SH110X_BLACK); // Borra el área del hipopótamo

  // Animación del hipopótamo
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

  // Dibujar las 'zzz' si están por debajo del límite y no están en pausa
  if (!isPaused && zzzY > 60) {
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(60, zzzY);
    display.print("Z");

    display.setCursor(55, zzzY - 8); // Segunda 'Z' ligeramente arriba
    display.print("Z");

    display.setCursor(50, zzzY - 16); // Tercera 'Z' aún más arriba
    display.print("Z");
  }

  // Actualiza el contenido dinámico
  display.display();

  // Guarda la posición previa de las 'zzz'
  prevZzzY = zzzY;

  // Control de pausa y reinicio de las 'zzz'
  if (zzzY <= 60) {
    if (!isPaused) {
      // Inicia la pausa
      isPaused = true;
      lastResetTime = millis();
    } else if (millis() - lastResetTime >= 1500) {
      // Termina la pausa después de 1.5 segundos
      isPaused = false;
      zzzY = 80; // Reinicia en la posición inicial
    }
  } else {
    // Avanza las 'zzz' hacia arriba si no están en pausa
    zzzY -= 2;
  }

  // Avanza el frame del hipopótamo
  frame = (frame + 1) % 9;

  // Pausa para la animación del hipopótamo
  delay(350);
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

void animateHippoBonked() {
  static int posX = 65;     // Coordenada X del hipopótamo
  static int posY = 70;     // Coordenada Y del hipopótamo
  static bool isBonked = false; // Estado del hipopótamo

  if (!isBonked) {
    for (int i = 0; i < 3; i++) { // Tres golpes animados
      // Mostrar el palo en golpes impares
      if (i % 2 == 0) {
        for (int j = 0; j < 4; j++) {
          display.drawLine(posX - 30 + j, posY + 15 + j, posX + 5 + j, posY - 15 + j, SH110X_WHITE);
        }
        display.setCursor(posX + 10, posY - 10); // Posición del texto
        display.print("bonk"); // Mostrar texto "bonk"
      }

      display.drawBitmap(posX, posY, hippoBitMap60, 60, 60, SH110X_WHITE); // Mostrar el hipopótamo (no parpadea)
      display.display();
      delay(400); // El golpe dura un poco más (400 ms)

      // Borrar el palo y el texto en golpes pares o después de mostrar
      for (int j = 0; j < 4; j++) {
        display.drawLine(posX - 30 + j, posY + 15 + j, posX + 5 + j, posY - 15 + j, SH110X_BLACK);
      }
      display.fillRect(posX + 10, posY - 10, 40, 10, SH110X_BLACK); // Borrar texto "bonk"
      display.display();
      delay(100); // Pausa breve entre golpes
    }

    // Después del último golpe, limpiar el palo y texto una vez más
    for (int j = 0; j < 4; j++) {
      display.drawLine(posX - 30 + j, posY + 15 + j, posX + 5 + j, posY - 15 + j, SH110X_BLACK); // Borrar palo
    }
    display.fillRect(posX + 10, posY - 10, 40, 10, SH110X_BLACK); // Borrar texto "bonk"
    display.display();

    // Cambiar al estado "bonked" (hipopótamo patas arriba)
    display.fillRect(posX, posY, 60, 60, SH110X_BLACK); // Borra el hipopótamo anterior
    display.drawBitmap(posX, posY, hippoBitMapBonked60, 60, 60, SH110X_WHITE); // Dibuja el hipopótamo patas arriba
    display.display();

    isBonked = true; // Actualizar el estado
  }
}

void animateHippoChasingHeart() {
  int hippoX = 100, hippoY = 90; // Coordenadas iniciales del hipopótamo (derecha)
  int heartX = 10, heartY = 64;  // Coordenadas iniciales del corazón (mitad de pantalla)
  bool heartOnGround = false;    // Estado del corazón: si ha caído al suelo
  bool heartCaught = false;      // Estado del corazón: si ha sido atrapado

  // Animación inicial: el hipopótamo asoma la cabeza más alto
  display.drawBitmap(hippoX, hippoY + 12, hippoBitMap40, 40, 20, SH110X_WHITE); // Dibuja solo la parte superior del hipopótamo (subido 8 píxeles más)
  display.display();
  delay(500); // El hipopótamo asoma por medio segundo

  // Animación de caída del corazón
  while (!heartOnGround) {
    // Borrar el corazón de la posición actual
    drawHeart(heartX, heartY, SH110X_BLACK);

    // Mover el corazón hacia abajo
    heartY++;

    // Dibujar el corazón
    drawHeart(heartX, heartY, SH110X_WHITE);
    display.display();
    delay(20); // Control de velocidad

    // Comprobar si el corazón ha llegado al "suelo" donde está el hipopótamo
    if (heartY >= hippoY + 15) {
      heartOnGround = true;
    }
  }

  // Borrar la parte del hipopótamo que estaba asomando
  display.fillRect(hippoX, hippoY + 12, 40, 20, SH110X_BLACK);
  display.display();

  // Animación de movimiento del hipopótamo hacia el corazón
  while (!heartCaught) {
    // Borrar el hipopótamo de la posición actual
    display.fillRect(hippoX, hippoY, 40, 40, SH110X_BLACK);

    // Mover el hipopótamo hacia el corazón
    hippoX--;

    // Dibujar el hipopótamo y el corazón
    display.drawBitmap(hippoX, hippoY, hippoBitMap40, 40, 40, SH110X_WHITE);
    drawHeart(heartX, heartY, SH110X_WHITE);
    display.display();
    delay(20); // Control de velocidad

    // Comprobar si el hipopótamo ha atrapado el corazón
    if (abs(hippoX - heartX) < 5) {
      heartCaught = true;
    }
  }

  // Borrar el corazón justo antes de los saltitos
  drawHeart(heartX, heartY, SH110X_BLACK);
  display.display();

  // Animación de saltos de felicidad
  for (int i = 0; i < 10; i++) { // Realiza 5 saltos (sube y baja)
    // Borrar el hipopótamo de la posición actual
    display.fillRect(hippoX, hippoY, 40, 40, SH110X_BLACK);

    // Saltar hacia arriba
    if (i % 2 == 0) hippoY -= 10; // Subir
    else hippoY += 10;            // Bajar

    // Dibujar el hipopótamo
    display.drawBitmap(hippoX, hippoY, hippoBitMap40, 40, 40, SH110X_WHITE);
    display.display();
    delay(150); // Tiempo de cada salto
  }

  // Borrar el hipopótamo al finalizar
  display.fillRect(hippoX, hippoY, 40, 40, SH110X_BLACK);
  display.display();
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

void animateHippoGivingHeart() {
  const int screenWidth = 128;  // Ancho de la pantalla
  const int screenHeight = 128; // Alto de la pantalla
  int hippoX = 100, hippoY = 90; // Coordenadas iniciales del hipopótamo (derecha)
  int heartX = screenWidth / 2 + 10, heartY = 110; // Coordenadas del corazón (un poco a la derecha del centro)

  // Dibujar el corazón inicial en el suelo (sin borrar iconos)
  drawHeart(heartX, heartY, SH110X_WHITE);
  display.display();

  // Fase 1: Movimiento lento hacia el corazón
  while (hippoX > heartX + 10) {
    // Dibujar la animación sin borrar los iconos
    display.fillRect(0, 50, screenWidth, screenHeight - 50, SH110X_BLACK); // Reducido el borrado a partir de y = 40

    // Mover el hipopótamo lentamente hacia la izquierda
    hippoX -= 1;

    // Dibujar el hipopótamo y el corazón
    display.drawBitmap(hippoX, hippoY, hippoBitMap40, 40, 40, SH110X_WHITE);
    drawHeart(heartX, heartY, SH110X_WHITE);
    display.display();
    delay(50); // Movimiento lento
  }

  // Pausa de un segundo
  delay(1000);

  // Fase 2: Movimiento rápido hacia el corazón (acelerando un poco más)
  while (hippoX > heartX - 5) {
    // Dibujar la animación sin borrar los iconos
    display.fillRect(0, 50, screenWidth, screenHeight - 50, SH110X_BLACK); // Reducido el borrado a partir de y = 40

    // Mover el hipopótamo rápidamente hacia la izquierda
    hippoX -= 5;  // Acelerando el movimiento

    // Dibujar el hipopótamo y el corazón
    display.drawBitmap(hippoX, hippoY, hippoBitMap40, 40, 40, SH110X_WHITE);
    drawHeart(heartX, heartY, SH110X_WHITE);
    display.display();
    delay(30); // Movimiento rápido
  }

  // Fase 3: Saltitos sincronizados
  int mouthOffsetX = -5; // Desplazamiento del corazón en la boca
  int mouthOffsetY = 20; // Posición vertical del corazón en la boca

  for (int i = 0; i < 20; i++) { // Realiza 15 saltos
    // Dibujar la animación sin borrar los iconos
    display.fillRect(0, 50, screenWidth, screenHeight - 50, SH110X_BLACK); // Reducido el borrado a partir de y = 40

    // Saltar hacia arriba o volver a bajar
    if (i % 2 == 0) hippoY -= 10; // Subir
    else hippoY += 10;            // Bajar

    // Moverse hacia la izquierda más rápido
    hippoX -= 5;

    // Dibujar el hipopótamo y el corazón en la boca
    display.drawBitmap(hippoX, hippoY, hippoBitMap40, 40, 40, SH110X_WHITE);
    drawHeart(hippoX + mouthOffsetX, hippoY + mouthOffsetY, SH110X_WHITE);
    display.display();
    delay(60); // Tiempo de cada salto reducido
  }

  // Borrar el área de animación (sin borrar los iconos)
  display.fillRect(0, 50, screenWidth, screenHeight - 50, SH110X_BLACK);
  display.display();

  // Pausa final de 2 segundos
  delay(2000);
}

void moveCursor() { 
  static int lastCursorX = -1;
  //No está bien ajustado a 3 posiciones, revisar
  int raw = getPotValue(1023);
  int cursorX;
  int sel = 0;

  if (raw < 400) { cursorX = 20;  sel = 0; }
  else if (raw < 800) { cursorX = 68; sel = 1; }
  else { cursorX = 115; sel = 2; }

  cursorPos = sel;

  if (cursorX != lastCursorX) {
    if (lastCursorX >= 0) {
      display.fillTriangle(
        lastCursorX, 49,
        lastCursorX - 3, 55,
        lastCursorX + 3, 55,
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
    lastCursorX = cursorX;
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

void drawInstantMessagesMenu() {
  display.clearDisplay();

  int seleccion = getPotValue(7);

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

void drawGamesMenu() {
  display.clearDisplay();

  int seleccion = getPotValue(5);

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
