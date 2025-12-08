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
// ------------------------------------------------------------
int getPotValue(int maxIndex) {
  if (maxIndex <= 0) return 0;

  static int stable = 0;
  static int candidate = 0;
  static unsigned long since = 0;
  static int lastStable = -1;

  int raw = analogRead(A0);
  int mapped = map(raw, 0, 1023, 0, maxIndex);
  mapped = constrain(mapped, 0, maxIndex);

  if (mapped != candidate) {
    candidate = mapped;
    since = millis();
  } else {
    if ((unsigned long)(millis() - since) >= POT_DEBOUNCE_TIME_MS) {
      stable = candidate;
      // Resetear temporizador solo cuando el valor estable cambia
      if (stable != lastStable) {
        resetAnimationTimer();
        lastStable = stable;
      }
    }
  }

  return stable;
}