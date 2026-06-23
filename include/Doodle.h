#ifndef DOODLE_H
#define DOODLE_H

#include <Arduino.h>

// ============================================================
//  Lienzo: dibuja un mensaje-dibujo de 16x16 y envialo al companero.
//
//  Editor (desde el menu Juegos): el pote elige la COLUMNA (X); MORSE corto
//  pinta/borra el pixel; MORSE largo baja una fila; FINISH corto sube una fila;
//  FINISH largo ENVIA. Inactividad -> cancela. El dibujo viaja como 32 bytes
//  (marcador 0x04 del protocolo de chat).
//
//  Recepcion: el dibujo entrante queda "pendiente" y se muestra a pantalla
//  completa al volver al IDLE (Doodle_pending()/Doodle_showPending()).
// ============================================================

#define DOODLE_DIM   16
#define DOODLE_BYTES 32   // 16x16 bits, una fila por 2 bytes (MSB primero)

void startDoodle();                                   // editor (desde el menu Juegos)

void Doodle_onReceived(uint8_t sender, const uint8_t *buf);  // guarda un dibujo entrante
bool Doodle_pending();                                       // hay un dibujo por mostrar
void Doodle_showPending();                                   // lo muestra (bloqueante) y lo descarta

#endif
