#ifndef DOODLE_H
#define DOODLE_H

#include <Arduino.h>

// ============================================================
//  Lienzo: dibuja un mensaje-dibujo de 24x24 y envialo al companero.
//
//  Editor (desde el menu Juegos > Dibujar). Controles:
//    - Potenciometro: mueve el cursor ARRIBA/ABAJO (fila, Y).
//    - Mantener A + potenciometro: mueve IZQUIERDA/DERECHA (columna, X).
//    - Pulsar A: PINTA el pixel del cursor.   - Pulsar B: lo BORRA.
//    - Mantener B: ENVIA el dibujo.           - Inactividad: cancela.
//  El dibujo viaja como 72 bytes (marcador 0x04 del protocolo de chat);
//  24x24 = 72 B + 2 de cabecera = 74, cabe en un solo paquete LoRa cifrado.
//
//  Recepcion: el dibujo entrante se muestra a pantalla completa al volver al
//  IDLE (Doodle_pending()/Doodle_showPending()) Y queda guardado para poder
//  REABRIRLO desde el Historial (el registro "[dibujo]" muestra el dibujo real).
// ============================================================

#define DOODLE_DIM      24
#define DOODLE_ROWBYTES (DOODLE_DIM / 8)              // 3 bytes por fila (MSB primero)
#define DOODLE_BYTES    (DOODLE_DIM * DOODLE_ROWBYTES) // 72

void startDoodle();                                   // editor (desde el menu Juegos)

// Recepcion. Se guarda el ultimo dibujo recibido junto con (emisor, epoch) para
// poder reabrirlo desde el Historial (el registro lleva ese mismo sello de tiempo).
void Doodle_onReceived(uint8_t sender, const uint8_t *buf, unsigned long epoch);
bool Doodle_pending();                                       // hay un dibujo por mostrar
void Doodle_showPending();                                   // lo muestra (bloqueante) y lo descarta

// Reapertura desde el Historial: ¿el dibujo guardado es el de este registro?
bool Doodle_isStored(uint8_t sender, unsigned long epoch);
void Doodle_drawStored(int ox, int oy);                      // pinta el dibujo guardado (sin clear/display)

#endif
