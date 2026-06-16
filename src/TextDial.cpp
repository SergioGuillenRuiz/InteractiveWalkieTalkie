#include <Arduino.h>
#include "TextDial.h"
#include "Display.h"
#include "Inputs.h"
#include "Morse.h"   // MORSE_CANCEL_HOLD, MORSE_SEND_HOLD, MORSE_INACTIVITY

// Rueda de caracteres: el espacio va primero (extremo del pote facil de
// alcanzar) seguido de A..Z. 27 entradas -> ~38 cuentas de ADC por letra,
// holgado frente al ruido del conversor.
static const char CHARSET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const int  CHARSET_LEN = sizeof(CHARSET) - 1;   // sin el '\0'

// Limite de longitud del mensaje (cabe de sobra en el sobre de chat + cifrado).
static const int DIAL_MAX_LEN = 60;

// Histeresis de seleccion: la letra solo cambia cuando el pote se estabiliza.
static const unsigned long SEL_DEBOUNCE_MS = 120;

String dialMessage = "";

// Verbo de la pista "(manten B = ...)". Por defecto "enviar" (componer un
// mensaje); el Frasero lo pone a "guardar" al editar una pieza propia.
static const char *s_finishVerb = "enviar";

// Longitud maxima del texto compuesto (configurable; por defecto DIAL_MAX_LEN).
static int s_maxLen = DIAL_MAX_LEN;

// --- Estado interno (a nivel de modulo para poder reiniciarlo desde dialReset) ---
static bool          s_init = false;
static int           s_selectedIdx = 0;
static int           s_candidateIdx = 0;
static unsigned long s_candidateSince = 0;
static unsigned long s_lastInputTime = 0;
static unsigned long s_morsePressStart = 0;
static unsigned long s_finishPressStart = 0;
static bool          s_morseWasPressed = false;
static bool          s_finishWasPressed = false;
static bool          s_morseLongArmed = false;
static bool          s_finishLongArmed = false;

void dialReset() {
  dialMessage = "";
  s_init = false;
  s_finishVerb = "enviar";
  s_maxLen = DIAL_MAX_LEN;
}

void dialResetWith(const String &initial) {
  dialMessage = initial;
  if ((int)dialMessage.length() > DIAL_MAX_LEN)
    dialMessage = dialMessage.substring(0, DIAL_MAX_LEN);
  s_init = false;
  s_finishVerb = "enviar";
  s_maxLen = DIAL_MAX_LEN;
}

void dialSetFinishLabel(const char *label) {
  s_finishVerb = (label && label[0]) ? label : "enviar";
}

void dialSetMaxLen(int maxLen) {
  if (maxLen < 1)            maxLen = 1;
  if (maxLen > DIAL_MAX_LEN) maxLen = DIAL_MAX_LEN;
  s_maxLen = maxLen;
  if ((int)dialMessage.length() > s_maxLen)
    dialMessage = dialMessage.substring(0, s_maxLen);
}

DialResult dialTick() {
  unsigned long now = millis();
  bool firstDraw = false;

  if (!s_init) {
    s_init = true;
    s_candidateIdx   = getPotValue(CHARSET_LEN - 1);
    s_selectedIdx    = s_candidateIdx;
    s_candidateSince = now;
    s_lastInputTime  = now;
    s_morseWasPressed = s_finishWasPressed = false;
    s_morseLongArmed  = s_finishLongArmed  = false;
    firstDraw = true;
  }

  // --- Seleccion con el pote (con histeresis temporal) ---
  int mapped = getPotValue(CHARSET_LEN - 1);
  if (mapped != s_candidateIdx) {
    s_candidateIdx = mapped;
    s_candidateSince = now;
  } else if (now - s_candidateSince >= SEL_DEBOUNCE_MS) {
    s_selectedIdx = s_candidateIdx;
  }

  drawDial(dialMessage, CHARSET, CHARSET_LEN, s_selectedIdx, firstDraw, s_finishVerb);

  // --- MORSE: corto = anadir letra, largo = cancelar ---
  bool morseNow = isMorsePressed();
  if (morseNow && !s_morseWasPressed) { s_morsePressStart = now; s_morseLongArmed = false; }
  if (morseNow && !s_morseLongArmed && (now - s_morsePressStart >= MORSE_CANCEL_HOLD)) {
    s_morseLongArmed = true;
  }
  if (!morseNow && s_morseWasPressed) {
    s_morseWasPressed = false;
    if (s_morseLongArmed) {
      s_morseLongArmed = false;
      dialMessage = "";
      return DIAL_CANCELLED;
    }
    if ((int)dialMessage.length() < s_maxLen) {
      dialMessage += CHARSET[s_selectedIdx];
    }
    s_lastInputTime = now;
  }
  s_morseWasPressed = morseNow;

  // --- FINISH: corto = borrar, largo = enviar ---
  bool finishNow = isFinishPressed();
  if (finishNow && !s_finishWasPressed) { s_finishPressStart = now; s_finishLongArmed = false; }
  if (finishNow && !s_finishLongArmed && (now - s_finishPressStart >= MORSE_SEND_HOLD)) {
    s_finishLongArmed = true;
  }
  if (!finishNow && s_finishWasPressed) {
    s_finishWasPressed = false;
    if (s_finishLongArmed) {
      s_finishLongArmed = false;
      s_lastInputTime = now;
      if (dialMessage.length() > 0) return DIAL_SENT;   // no enviar vacio
    } else {
      if (dialMessage.length() > 0)
        dialMessage = dialMessage.substring(0, dialMessage.length() - 1);
      s_lastInputTime = now;
    }
  }
  s_finishWasPressed = finishNow;

  // --- Inactividad: cancelar automaticamente ---
  if (now - s_lastInputTime > MORSE_INACTIVITY) {
    dialMessage = "";
    return DIAL_CANCELLED;
  }

  return DIAL_NONE;
}
