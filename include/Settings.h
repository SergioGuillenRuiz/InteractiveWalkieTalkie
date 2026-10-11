#ifndef SETTINGS_H
#define SETTINGS_H

// Menu de Ajustes (mantener B ~1,5 s en el menu principal): Poner la hora / Emparejar.
// Es bloqueante y vuelve al menu principal.
void startSettings();

// Aviso "sin pareja" (para Enviar cuando el equipo aun no esta emparejado). Vuelve al cabo de unos segundos o con un boton.
void showNeedPairing();

#endif
