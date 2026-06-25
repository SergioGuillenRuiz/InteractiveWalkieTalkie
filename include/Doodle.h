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
//  El dibujo viaja como 72 bytes (marcador 0x04 del protocolo de chat);
//  24x24 = 72 B + 2 de cabecera = 74, cabe en un solo paquete LoRa cifrado.
//
//  Recepcion: el dibujo entrante NO interrumpe; va al Historial como un mensaje
//  mas y se ve/abre desde alli (el registro "[dibujo]" muestra el dibujo real).
// ============================================================

#define DOODLE_DIM      24
#define DOODLE_ROWBYTES (DOODLE_DIM / 8)              // 3 bytes por fila (MSB primero)
#define DOODLE_BYTES    (DOODLE_DIM * DOODLE_ROWBYTES) // 72

void startDoodle();                                   // editor (5o modo del menu Enviar)

// Recepcion. Se guarda el ultimo dibujo recibido junto con (emisor, epoch) para
// poder verlo desde el Historial (el registro lleva ese mismo sello de tiempo).
void Doodle_onReceived(uint8_t sender, const uint8_t *buf, unsigned long epoch);

// Apertura desde el Historial: ¿el dibujo guardado es el de este registro?
bool Doodle_isStored(uint8_t sender, unsigned long epoch);
void Doodle_drawStored(int ox, int oy);                      // pinta el dibujo guardado (sin clear/display)

#endif
