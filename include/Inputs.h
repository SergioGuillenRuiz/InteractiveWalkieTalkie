#ifndef INPUTS_H
#define INPUTS_H

#include <Arduino.h>
#include "Config.h"


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

void menuTransitionDelay();

#endif
