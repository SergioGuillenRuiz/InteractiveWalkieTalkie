#include <Arduino.h>
#include "Config.h"   
#include "Inputs.h"   

ButtonState morseButton  = { HIGH, HIGH, 0 };
ButtonState finishButton = { HIGH, HIGH, 0 };

// ------------------------------------------------------------
// Antirrebote
// ------------------------------------------------------------
static const unsigned long DEBOUNCE_TIME_MS = 50UL; 

static bool readButton(ButtonState &btn, uint8_t pin) {
  bool raw = digitalRead(pin); 
  if (raw != btn.lastState) {
    btn.lastChange = millis();
    btn.lastState = raw;
  }

  // si ha pasado suficiente tiempo desde el último cambio, actualizamos el estado estable
  if ((unsigned long)(millis() - btn.lastChange) > DEBOUNCE_TIME_MS) {
    btn.currentState = btn.lastState;
  }

  // retornamos true cuando está PRESIONADO (activo LOW)
  return (btn.currentState == LOW);
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
