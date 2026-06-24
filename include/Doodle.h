#ifndef DOODLE_H
#define DOODLE_H

#include <Arduino.h>

// ============================================================
//  Lienzo: dibuja un mensaje-dibujo de 24x24 y envialo al companero.
//
//  Editor (desde el menu Juegos): el pote elige la COLUMNA (X); MORSE corto
//  pinta/borra el pixel; MORSE largo baja una fila; FINISH corto sube una fila;
//  FINISH largo ENVIA. Inactividad -> cancela. El dibujo viaja como 72 bytes
//  (marcador 0x04 del protocolo de chat); 24x24 = 72 B + 2 de cabecera = 74,
//  cabe en un solo paquete LoRa cifrado (limite de texto plano = 95).
//
//  Recepcion: el dibujo entrante queda "pendiente" y se muestra a pantalla
//  completa al volver al IDLE (Doodle_pending()/Doodle_showPending()).
// ============================================================

#define DOODLE_DIM      24
#define DOODLE_ROWBYTES (DOODLE_DIM / 8)              // 3 bytes por fila (MSB primero)
#define DOODLE_BYTES    (DOODLE_DIM * DOODLE_ROWBYTES) // 72

void startDoodle();                                   // editor (desde el menu Juegos)

void Doodle_onReceived(uint8_t sender, const uint8_t *buf);  // guarda un dibujo entrante
bool Doodle_pending();                                       // hay un dibujo por mostrar
void Doodle_showPending();                                   // lo muestra (bloqueante) y lo descarta

#endif
