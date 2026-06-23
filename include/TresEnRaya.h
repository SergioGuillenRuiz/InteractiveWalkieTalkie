#ifndef TRESENRAYA_H
#define TRESENRAYA_H

// Tres en raya por LoRa para dos equipos. Emparejamiento automatico (el id menor
// juega con X y mueve primero); sincronizacion por ESTADO (cada jugada difunde el
// tablero entero, asi un paquete perdido se autocorrige). Desde el menu Juegos.
void startTresEnRaya();

#endif
