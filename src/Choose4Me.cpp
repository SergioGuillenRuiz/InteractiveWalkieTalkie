#include <Arduino.h>
#include "Display.h"
#include "Inputs.h"
#include "States.h"

// ============================================================
// CONSTANTES Y VARIABLES DEL JUEGO
// ============================================================

// Respuestas posibles (como una bola mágica)
const char* respuestas[] = {
  "SI",           // 0
  "NO",           // 1
  "DEFINITIVAMENTE",  // 2
  "NO TE LO CREES NI TU",     // 3
  "CLARO QUE SI",     // 4
  "JAMAS",        // 5
  "ES SEGURO",    // 6
  "CONCENTRATE Y PREGUNTA DE NUEVO", // 8
  "SIN DUDA",     // 9
  "MI FUENTE DICE QUE NO", // 10
  "PERSPECTIVAS BUENAS", // 11
  "SI, DEFINITIVAMENTE", // 12
  "NO CUENTES CON ESO", // 13
  "PUEDES CONTAR CON ELLO", // 14
  "TODAS LAS SENALES APUNTAN A SI", // 16
};

const int NUM_RESPUESTAS = sizeof(respuestas) / sizeof(respuestas[0]);

// ============================================================
// FUNCIONES DEL JUEGO
// ============================================================

// Dibuja una "bola mágica" estilo 8-ball
void drawMagicBall(int x, int y, int size, bool shaking = false) {
  // Círculo exterior (bola)
  display.fillCircle(x + size/2, y + size/2, size/2, SH110X_WHITE);
  
  // Círculo interior (área de texto)
  display.fillCircle(x + size/2, y + size/2, size/2 - 4, SH110X_BLACK);
  
  // Triángulo central (estilo 8-ball)
  int centerX = x + size/2;
  int centerY = y + size/2;
  int triangleSize = size/4;
  
  if (shaking) {
    // Triángulo "tembloroso" - desplazado aleatoriamente
    int offsetX = random(-3, 4);
    int offsetY = random(-3, 4);
    display.fillTriangle(
      centerX + offsetX, centerY - triangleSize + offsetY,
      centerX - triangleSize + offsetX, centerY + triangleSize + offsetY,
      centerX + triangleSize + offsetX, centerY + triangleSize + offsetY,
      SH110X_WHITE
    );
  } else {
    // Triángulo normal
    display.fillTriangle(
      centerX, centerY - triangleSize,
      centerX - triangleSize, centerY + triangleSize,
      centerX + triangleSize, centerY + triangleSize,
      SH110X_WHITE
    );
  }
  
  // Número "8" en la parte inferior (opcional, pequeño)
  display.setCursor(centerX - 3, centerY + triangleSize - 8);
  display.setTextSize(1);
  display.setTextColor(SH110X_BLACK, SH110X_WHITE); // Texto negro sobre fondo blanco
  display.print("8");
}

// Animación de "tirando los dados" o "girando la ruleta"
void animateDecisionMaking() {
  const int DURACION_ANIMACION = 2000; // 2 segundos de animación
  const int NUM_FRAMES = 20;
  const int FRAME_DELAY = DURACION_ANIMACION / NUM_FRAMES;
  
  unsigned long startTime = millis();
  int frame = 0;
  
  // Posición de la "bola" o "ruleta"
  int ballX = 44;
  int ballY = 20;
  int ballSize = 40;
  
  while (millis() - startTime < DURACION_ANIMACION) {
    // Limpiar área de animación
    display.fillRect(ballX - 5, ballY - 5, ballSize + 10, ballSize + 10, SH110X_BLACK);
    
    // Dibujar bola con efecto de "shake"
    bool shaking = (frame % 2 == 0); // Alternar entre shaking y no shaking
    drawMagicBall(ballX, ballY, ballSize, shaking);
    
    // Mostrar texto "Pensando..."
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(30, 70);
    display.print("Pensando");
    
    // Puntos animados "..." 
    int dots = (frame / 5) % 4;
    for (int i = 0; i < dots; i++) {
      display.print(".");
    }
    
    display.display();
    
    // Pequeña pausa no bloqueante (permitir interrupciones)
    unsigned long frameStart = millis();
    while (millis() - frameStart < FRAME_DELAY) {
      // Permitir chequeo de botones (para cancelar)
      if (isFinishPressed()) {
        return; // Salir si presionan FINISH
      }
      delay(10);
    }
    
    frame++;
  }
}

// Función principal del juego Choose4Me
// Bucle iterativo (sin recursión) para que repetir "Otra vez" no consuma pila.
void playChoose4Me() {
  bool running = true;

  while (running) {
    // ---- Pantalla de inicio ----
    Display_clear();
    display.setCursor(20, 10);
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.println("CHOOSE4ME");
    display.println();
    display.setCursor(0, 30);
    display.println("Haz tu pregunta");
    display.println("mentalmente...");
    display.println();
    display.println();
    display.println();
    display.println("A: Respuesta");
    display.println();
    display.println("B: Salir");
    display.display();

    // ---- Esperar: MORSE = respuesta, FINISH = salir ----
    bool wantAnswer = false;
    bool waiting = true;
    while (waiting) {
      if (isMorsePressed()) {
        delay(50);
        if (isMorsePressed()) { wantAnswer = true; waiting = false; break; }
      }
      if (isFinishPressed()) {
        delay(50);
        if (isFinishPressed()) { running = false; waiting = false; break; }
      }
      delay(10);
    }
    if (!wantAnswer) break;                 // FINISH: salir del juego

    // Esperar a que se suelte el botón antes de continuar
    while (isMorsePressed()) delay(10);

    // ---- Animación de "pensando" ----
    Display_clear();
    animateDecisionMaking();

    // ---- Respuesta aleatoria ----
    int respuestaIndex = random(NUM_RESPUESTAS);

    Display_clear();
    drawMagicBall(44, 10, 40, false);
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);

    int lineHeight = 10;
    int currentY = 60;
    String remaining = respuestas[respuestaIndex];
    while (remaining.length() > 0) {
      int cutPoint = remaining.length();
      if (remaining.length() > 20) {        // Aprox 20 caracteres por línea
        cutPoint = 20;
        for (int i = cutPoint; i > 0; i--) {  // cortar por un espacio
          if (remaining.charAt(i) == ' ') { cutPoint = i; break; }
        }
      }

      String line = remaining.substring(0, cutPoint);
      remaining = remaining.substring(cutPoint);

      int16_t tx, ty;
      uint16_t tw, th;
      display.getTextBounds(line.c_str(), 0, 0, &tx, &ty, &tw, &th);
      int textX = (128 - tw) / 2;
      display.setCursor(textX, currentY);
      display.print(line);

      currentY += lineHeight;
      if (currentY > 110) break;            // No salir de pantalla
    }

    display.setCursor(20, 110);
    display.print("A: Otra vez");
    display.setCursor(25, 120);
    display.print("B: Salir");
    display.display();

    // ---- Esperar decisión: MORSE = otra vez, FINISH = salir ----
    bool resultShown = true;
    while (resultShown) {
      if (isMorsePressed()) {
        delay(50);
        if (isMorsePressed()) { resultShown = false; break; }   // otra vez
      }
      if (isFinishPressed()) {
        delay(50);
        if (isFinishPressed()) { resultShown = false; running = false; break; }
      }
      delay(10);
    }

    // Esperar a que se suelten los botones antes de repetir/salir
    while (isMorsePressed() || isFinishPressed()) delay(10);
  }

  // Limpiar pantalla al salir
  Display_clear();
}

// Función para iniciar el juego desde el menú principal
void startChoose4Me() {
  // Inicializar semilla aleatoria con lectura analógica
  randomSeed(analogRead(A0) + millis());
  
  // Ejecutar juego
  playChoose4Me();
  
  // Volver al estado IDLE después de jugar
  mainState = STATE_IDLE;
  Display_clear();
}