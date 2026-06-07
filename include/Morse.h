#ifndef MORSE_H
#define MORSE_H

#include <Arduino.h>
#include "Config.h"
#include "Display.h"

// ------------------------------------------------------------
// Parámetros Morse
// ------------------------------------------------------------
#define MORSE_DOT_MAX      250        // duración máxima punto (ms)
#define MORSE_DASH_MIN     250        // duración mínima raya (ms)
#define SYMBOL_TIMEOUT     600        // separación entre símbolos (ms)
#define WORD_TIMEOUT       1000       // separación entre palabras (ms)

#define MORSE_CANCEL_HOLD  2000       // pulsación larga de MORSE -> cancelar
#define MORSE_SEND_HOLD    1500       // pulsación larga de FINISH -> enviar
#define MORSE_INACTIVITY   45000      // sin actividad -> cancelar automáticamente

// ============================================================
//  Variables globales accesibles
// ============================================================

enum MorseResult { MORSE_NONE = 0, MORSE_SENT = 1, MORSE_CANCELLED = -1 };

extern String morsePrefix;          
extern String lastMorsePrefix;     
extern String morseCode;            
extern String mensajeAEnviar;       

extern bool mensajeEnviado;      
extern bool mensajeCancelado;    
extern bool primeraVezMenu;       
extern bool dentroMenuEnviar;    

extern int morseScrollOffset;
extern int lastMorseScrollOffset;

extern String morseTable[];
extern const int morseTableSize;

// ============================================================
//  Funciones Morse
// ============================================================

char morseToChar(const String &morse);

MorseResult createMorseMessage();

#endif 
