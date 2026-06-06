#include <Arduino.h>
#include "Config.h"   
#include "Inputs.h"  
#include "Display.h" 

ButtonState morseButton  = { HIGH, HIGH, 0 };
ButtonState finishButton = { HIGH, HIGH, 0 };

// ------------------------------------------------------------
// Antirrebote
// ------------------------------------------------------------

static bool readButton(ButtonState &btn, uint8_t pin) {
  bool raw = digitalRead(pin); 
  if (raw != btn.lastState) {
    btn.lastChange = millis();
    btn.lastState = raw;
  }

  if ((unsigned long)(millis() - btn.lastChange) > BUTTON_DEBOUNCE_TIME_MS) {
    btn.currentState = btn.lastState;
  }

  bool pressed = (btn.currentState == LOW);
  
  // Resetear temporizador de animación cuando se presiona un botón
  if (pressed && btn.lastState == LOW) {
    resetAnimationTimer();
  }
  
  return pressed;
}

void menuTransitionDelay() {
  delay(200);
  unsigned long start = millis();
  while (isMorsePressed() || isFinishPressed()) {
    if (millis() - start > 1000) break;
    delay(10);
  }
  delay(80);
}

// ------------------------------------------------------------
// Getters de estado de botones 
// ------------------------------------------------------------
bool isMorsePressed() {
  return readButton(morseButton, PIN_MORSE_BUTTON);
}

bool isFinishPressed() {
  return readButton(finishButton, PIN_FINISH_BUTTON);
}

// ------------------------------------------------------------
// Lectura estable del potenciómetro (A0) con debounce y mapeo
//
// El antirrebote se aplica sobre la lectura CRUDA y el mapeo se hace al
// final. Así el estado estático no depende de "maxIndex" y la misma
// función puede usarse desde menús distintos sin contaminación cruzada.
// ------------------------------------------------------------
int getPotValue(int maxIndex) {
  if (maxIndex <= 0) return 0;

  static int stableRaw = 0;
  static int candidateRaw = 0;
  static unsigned long since = 0;

  const int RAW_NOISE = 8;   // umbral para ignorar el ruido del ADC

  int raw = analogRead(A0);

  if (abs(raw - candidateRaw) > RAW_NOISE) {
    candidateRaw = raw;
    since = millis();
  } else if ((unsigned long)(millis() - since) >= POT_DEBOUNCE_TIME_MS) {
    // El potenciómetro se ha estabilizado en un nuevo valor
    if (candidateRaw != stableRaw) {
      stableRaw = candidateRaw;
      resetAnimationTimer();   // movimiento real del usuario
    }
  }

  int mapped = map(stableRaw, 0, 1023, 0, maxIndex);
  return constrain(mapped, 0, maxIndex);
}