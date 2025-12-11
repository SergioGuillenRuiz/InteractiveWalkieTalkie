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

// Dibuja una ruleta/slot machine simple
void drawSlotMachine(int x, int y, int width, int height, const char* text = "") {
  // Marco exterior
  display.drawRoundRect(x, y, width, height, 5, SH110X_WHITE);
  
  // Área interior
  display.fillRoundRect(x + 2, y + 2, width - 4, height - 4, 3, SH110X_BLACK);
  
  // Texto centrado
  if (strlen(text) > 0) {
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    
    // Calcular centro
    int16_t tx, ty;
    uint16_t tw, th;
    display.getTextBounds(text, 0, 0, &tx, &ty, &tw, &th);
    
    int textX = x + (width - tw) / 2;
    int textY = y + (height - th) / 2;
    
    display.setCursor(textX, textY);
    display.print(text);
  }
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
void playChoose4Me() {
  // Pantalla de inicio
  Display_clear();
  
  // Título
  display.setCursor(20, 10);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.println("CHOOSE4ME");
  display.println();
  
  // Instrucciones
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
  
  // Esperar a que el usuario presione MORSE para obtener respuesta
  // o FINISH para salir
  bool waiting = true;
  while (waiting) {
    if (isMorsePressed()) {
      delay(50);
      if (isMorsePressed()) {
        // Obtener respuesta
        waiting = false;
        
        // Animación de "pensando"
        Display_clear();
        animateDecisionMaking();
        
        // Seleccionar respuesta aleatoria
        int respuestaIndex = random(NUM_RESPUESTAS);
        
        // Mostrar respuesta
        Display_clear();
        
        // Dibujar bola mágica con resultado
        drawMagicBall(44, 10, 40, false);
        
        // Mostrar respuesta (con wrap de texto si es muy larga)
        display.setTextSize(1);
        display.setTextColor(SH110X_WHITE);
        
        String respuesta = respuestas[respuestaIndex];
        int maxWidth = 120;
        int lineHeight = 10;
        int currentY = 60;
        
        // Separar en líneas si es necesario
        String remaining = respuesta;
        while (remaining.length() > 0) {
          // Encontrar dónde cortar
          int cutPoint = remaining.length();
          if (remaining.length() > 20) { // Aprox 20 caracteres por línea
            cutPoint = 20;
            // Buscar espacio para cortar limpio
            for (int i = cutPoint; i > 0; i--) {
              if (remaining.charAt(i) == ' ') {
                cutPoint = i;
                break;
              }
            }
          }
          
          String line = remaining.substring(0, cutPoint);
          remaining = remaining.substring(cutPoint);
          
          // Centrar texto
          int16_t tx, ty;
          uint16_t tw, th;
          display.getTextBounds(line.c_str(), 0, 0, &tx, &ty, &tw, &th);
          
          int textX = (128 - tw) / 2;
          display.setCursor(textX, currentY);
          display.print(line);
          
          currentY += lineHeight;
          
          if (currentY > 110) break; // No salir de pantalla
        }
        
        // Instrucciones para continuar
        display.setCursor(20, 110);
        display.print("A: Otra vez");
        display.println();
        display.setCursor(25, 120);
        display.print("B: Salir");
        
        display.display();
        
        // Esperar decisión del usuario
        bool resultShown = true;
        while (resultShown) {
          if (isMorsePressed()) {
            delay(50);
            if (isMorsePressed()) {
              // Otra pregunta
              resultShown = false;
              playChoose4Me(); // Llamada recursiva (con cuidado con stack)
              return;
            }
          }
          
          if (isFinishPressed()) {
            delay(50);
            if (isFinishPressed()) {
              // Salir del juego
              resultShown = false;
              waiting = false;
            }
          }
          
          delay(10); // Pequeña pausa para no saturar CPU
        }
      }
    }
    
    if (isFinishPressed()) {
      delay(50);
      if (isFinishPressed()) {
        // Salir sin obtener respuesta
        waiting = false;
      }
    }
    
    delay(10); // Pequeña pausa para no saturar CPU
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