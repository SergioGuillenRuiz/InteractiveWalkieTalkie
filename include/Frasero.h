#ifndef FRASERO_H
#define FRASERO_H

#include <Arduino.h>

// ============================================================
//  Frasero: composicion de frases por piezas (Sujeto + Verbo + Objeto).
//
//  Cada categoria tiene una rueda de opciones que recorres con el pote:
//      [ (vacio) ] + [ tus piezas propias ] + [ piezas por defecto ]
//  Las piezas propias (creadas con la Rueda, TextDial) aparecen ANTES que
//  las de fabrica y se guardan en EEPROM. Cualquier slot puede quedar vacio.
//
//  Controles al COMPONER:
//      pote          -> elige opcion de la categoria activa
//      MORSE (corto) -> fija y avanza (en la ultima: compone y envia)
//      MORSE (largo) -> gestionar piezas propias de la categoria activa
//      FINISH (corto)-> retrocede (en la primera: salir)
//      FINISH (largo)-> enviar ya (saltando los vacios)
// ============================================================

enum FraseroResult { FR_NONE = 0, FR_SENT = 1, FR_EXIT = -1 };

extern String fraseroMessage;   // frase compuesta (valida tras FR_SENT)

void Frasero_load();            // cargar piezas propias de EEPROM (en setup)
void fraseroReset();            // empezar una composicion nueva (al entrar al modo)
FraseroResult fraseroTick();    // atender un ciclo; FR_SENT/FR_EXIT al terminar

#endif
