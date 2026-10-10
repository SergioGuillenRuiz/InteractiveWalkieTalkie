#ifndef DOODLE_H
#define DOODLE_H

#include <Arduino.h>

// ============================================================
//  Lienzo: dibuja un mensaje-dibujo de 24x24 y envialo al companero.
//
//  Editor (menu Enviar > Dibujar, 5o modo). Controles:
//    - Potenciometro: mueve el cursor ARRIBA/ABAJO (fila, Y).
//    - Mantener A + potenciometro: mueve IZQUIERDA/DERECHA (columna, X).
//    - Pulsar A: PINTA el pixel del cursor.   - Pulsar B: lo BORRA.
//    - Mantener B: ENVIA el dibujo.           - Inactividad: cancela.
//  El dibujo viaja como 72 bytes (marcador 0x04 del protocolo de chat) con emisor y msgId:
//  24x24 = 72 B + 3 de cabecera = 75, cabe en un solo paquete LoRa cifrado. Como un mensaje de texto,
//  se confirma (ACK), se reintenta hasta que llega y no se duplica.
//
//  Enviar: tras mantener B sale la pantalla de resultado ("Enviado" -> "Entregado!" o "(sin confirmar)")
//  y el dibujo queda en el Historial como ENVIADO, con su lienzo.
//  Recepcion: el dibujo entrante NO interrumpe; va al Historial como un mensaje mas (no leido) y se
//  ve/abre desde alli: el lienzo vive dentro de su registro, asi que sobrevive a un reinicio.
// ============================================================

#define DOODLE_DIM      24
#define DOODLE_ROWBYTES (DOODLE_DIM / 8)              // 3 bytes por fila (MSB primero)
#define DOODLE_BYTES    (DOODLE_DIM * DOODLE_ROWBYTES) // 72

void startDoodle();                                   // editor (5o modo del menu Enviar)

// Pinta un lienzo (DOODLE_BYTES bytes) con la esquina superior izquierda en (ox, oy), a 4 px por celda
// (24 x 4 = 96 px). No limpia ni vuelca la pantalla.
void Doodle_draw(const uint8_t *bitmap, int ox, int oy);

#endif
