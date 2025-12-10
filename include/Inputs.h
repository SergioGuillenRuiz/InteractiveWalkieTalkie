#ifndef INPUTS_H
#define INPUTS_H

#include <Arduino.h>
#include "Config.h"

#define BUTTON_DEBOUNCE_TIME_MS  50UL
#define POT_DEBOUNCE_TIME_MS    100UL
// ============================================================
//  Variables globales accesibles
// ============================================================ 

struct ButtonState {
  bool currentState;        
  bool lastState;           
  unsigned long lastChange; 
};

// Instancias de botones 
extern ButtonState morseButton;   
extern ButtonState finishButton;  

// ============================================================
//  Funciones de los botones    
// ============================================================

bool isMorsePressed();

bool isFinishPressed();

int getPotValue(int maxIndex);

void menuTransitionDelay();

#endif
