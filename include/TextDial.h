#ifndef TEXTDIAL_H
#define TEXTDIAL_H

#include <Arduino.h>

// ============================================================
//  Rueda de letras: composicion de texto libre usando el
//  potenciometro como dial, mas rapida que el Morse.
//
//    Pote          -> elige la letra (espacio + A..Z)
//    MORSE (corto) -> anade la letra seleccionada
//    MORSE (largo) -> cancelar
//    FINISH (corto)-> borrar la ultima letra
//    FINISH (largo)-> enviar
//
//  El texto compuesto queda en dialMessage. Se reutilizan los
//  mismos tiempos de pulsacion larga que el Morse (ver Morse.h).
// ============================================================

enum DialResult { DIAL_NONE = 0, DIAL_SENT = 1, DIAL_CANCELLED = -1 };

extern String dialMessage;

// Prepara una composicion nueva (limpia el texto y el estado interno).
// Llamar al entrar al modo y tras cada envio/cancelacion.
void dialReset();

// Como dialReset() pero arrancando con un texto inicial (para editarlo).
void dialResetWith(const String &initial);

// Cambia el verbo de la pista "(manten B = ...)". Por defecto "enviar"; usar
// "guardar" al editar piezas. dialReset()/dialResetWith() lo devuelven a "enviar".
void dialSetFinishLabel(const char *label);

// Atiende un ciclo de composicion (lee pote/botones y dibuja). Devuelve
// DIAL_SENT o DIAL_CANCELLED cuando termina; DIAL_NONE mientras se compone.
DialResult dialTick();

#endif
