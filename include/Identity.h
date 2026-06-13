#ifndef IDENTITY_H
#define IDENTITY_H

#include <Arduino.h>

// Identificador de 1 byte (1..254) propio de este equipo. Es estable (no cambia
// entre arranques) y practicamente unico por hardware: se deriva del chip ID del
// ESP. 0 queda reservado para "desconocido".
//
// En el simulador (sin ESP) se toma de la variable de entorno SIM_CHIPID, de modo
// que dos instancias puedan tener IDs distintos en las pruebas.
uint8_t Device_id();

#endif
