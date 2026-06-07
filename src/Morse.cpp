#include <Arduino.h>
#include "Morse.h"      
#include "Display.h"  
#include "Inputs.h" 

String morsePrefix = "";
String lastMorsePrefix = "";
String morseCode = "";
String mensajeAEnviar = "";

bool mensajeEnviado = false;
bool mensajeCancelado = false;
bool primeraVezMenu = true;
bool dentroMenuEnviar = false;

int morseScrollOffset = 0;
int lastMorseScrollOffset = 0;

String morseTable[] = {
  "A: .-",   "B: -...", "C: -.-.", "D: -..",  "E: .",    "F: ..-.",
  "G: --.",  "H: ....", "I: ..",   "J: .---", "K: -.-",  "L: .-..",
  "M: --",   "N: -.",   "O: ---",  "P: .--.", "Q: --.-", "R: .-.",
  "S: ...",  "T: -",    "U: ..-",  "V: ...-", "W: .--",  "X: -..-",
  "Y: -.--", "Z: --..", "ESP: ....."   
};
const int morseTableSize = sizeof(morseTable) / sizeof(morseTable[0]);

/*****************************************************************************************************************************/
/* Conversión de código Morse a carácter ASCII                                         */
/*****************************************************************************************************************************/
char morseToChar(const String& morse) {
  if (morse == ".-") return 'A';
  if (morse == "-...") return 'B';
  if (morse == "-.-.") return 'C';
  if (morse == "-..") return 'D';
  if (morse == ".") return 'E';
  if (morse == "..-.") return 'F';
  if (morse == "--.") return 'G';
  if (morse == "....") return 'H';
  if (morse == "..") return 'I';
  if (morse == ".---") return 'J';
  if (morse == "-.-") return 'K';
  if (morse == ".-..") return 'L';
  if (morse == "--") return 'M';
  if (morse == "-.") return 'N';
  if (morse == "---") return 'O';
  if (morse == ".--.") return 'P';
  if (morse == "--.-") return 'Q';
  if (morse == ".-.") return 'R';
  if (morse == "...") return 'S';
  if (morse == "-") return 'T';
  if (morse == "..-") return 'U';
  if (morse == "...-") return 'V';
  if (morse == ".--") return 'W';
  if (morse == "-..-") return 'X';
  if (morse == "-.--") return 'Y';
  if (morse == "--..") return 'Z';
  if (morse == ".....") return ' ';  // espacio
  return '?';
}

/*****************************************************************************************************************************/
/* Captura y creación del mensaje Morse                                               */
/*****************************************************************************************************************************/
MorseResult createMorseMessage() {
  if (!dentroMenuEnviar) return MORSE_NONE;

  static unsigned long morsePressStart = 0;
  static unsigned long finishPressStart = 0;
  static unsigned long lastInputTime = 0;
  static bool morseWasPressed = false;
  static bool finishWasPressed = false;
  static bool morseLongArmed = false;
  static bool finishLongArmed = false;

  unsigned long now = millis();

  if (primeraVezMenu) {
    lastInputTime = now;
    primeraVezMenu = false;
  }

  drawMorse();

  bool morsePressedNow = isMorsePressed();
  if (morsePressedNow && !morseWasPressed) {
    morsePressStart = now;
    morseLongArmed = false;
  }
  if (morsePressedNow && !morseLongArmed && (now - morsePressStart >= MORSE_CANCEL_HOLD)) {
    morseLongArmed = true;
  }
  if (!morsePressedNow && morseWasPressed) {
    unsigned long dur = now - morsePressStart;
    morseWasPressed = false;
    if (morseLongArmed) {
      mensajeCancelado = true;
      mensajeAEnviar = "";
      morseCode = "";
      morsePrefix = "";
      lastMorsePrefix = "";
      morseScrollOffset = 0;
      morseLongArmed = false;
      finishLongArmed = false;
      morseWasPressed = false;
      finishWasPressed = false;
      lastInputTime = now;
      return MORSE_CANCELLED;
    } else {
      char symbol = (dur < MORSE_DASH_MIN) ? '.' : '-';
      morseCode += symbol;
      morsePrefix = morseCode;
      lastInputTime = now;
      drawMorse();
      return MORSE_NONE;
    }
  }
  morseWasPressed = morsePressedNow;

  bool finishPressedNow = isFinishPressed();
  if (finishPressedNow && !finishWasPressed) {
    finishPressStart = now;
    finishLongArmed = false;
  }
  if (finishPressedNow && !finishLongArmed && (now - finishPressStart >= MORSE_SEND_HOLD)) {
    finishLongArmed = true;
  }
  if (!finishPressedNow && finishWasPressed) {
    finishWasPressed = false;
    if (finishLongArmed) {
      if (!morseCode.isEmpty()) {
        mensajeAEnviar += morseToChar(morseCode);
        morseCode = "";
      }
      mensajeEnviado = true;
      morsePrefix = "";
      lastMorsePrefix = "";
      morseScrollOffset = 0;
      morseLongArmed = false;
      finishLongArmed = false;
      morseWasPressed = false;
      finishWasPressed = false;
      lastInputTime = now;
      return MORSE_SENT;
    } else {
      if (!morseCode.isEmpty()) {
        char letra = morseToChar(morseCode);
        mensajeAEnviar += letra;
        morseCode = "";
        morsePrefix = "";
        lastMorsePrefix = "";
        morseScrollOffset = 0;
        lastInputTime = now;
        drawMorse();
        return MORSE_NONE;
      }
    }
  }
  finishWasPressed = finishPressedNow;

  if ((now - lastInputTime) > MORSE_INACTIVITY) {
    mensajeCancelado = true;
    mensajeAEnviar = "";
    morseCode = "";
    morsePrefix = "";
    lastMorsePrefix = "";
    morseScrollOffset = 0;
    morseWasPressed = false;
    finishWasPressed = false;
    morseLongArmed = false;
    finishLongArmed = false;
    lastInputTime = now;
    return MORSE_CANCELLED;
  }

  return MORSE_NONE;
}