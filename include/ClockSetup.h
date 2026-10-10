#ifndef CLOCKSETUP_H
#define CLOCKSETUP_H

// ============================================================
//  Pantalla "Poner la hora". Se abre MANTENIENDO B ~1,5 s en el menu principal.
//
//  No hay forma de teclear la hora por el puerto serie en la placa (el SCL del
//  OLED esta en el pin RX), asi que se ajusta con el pote y los dos botones, en
//  tres pasos: horas (0-23), decenas de minuto (0-5) y unidades de minuto (0-9).
//    - Pote:  cambia el valor del campo activo (parte de la hora actual).
//    - A:     acepta el campo y pasa al siguiente (en el ultimo, GUARDA).
//    - B:     vuelve al campo anterior (en el primero, CANCELA).
//    - Sin tocar nada durante 60 s: cancela.
//  Al guardar: sube la generacion de ajuste (la hora puesta a mano gana a la del
//  companero aunque se corrija hacia atras), reajusta las marcas del Historial y
//  difunde una baliza enseguida. La hora es la LOCAL (sin zona horaria).
//
//  Bloqueante (como los juegos): atiende la radio con backgroundTick() y deja
//  mainState = STATE_IDLE al salir.
// ============================================================

void startClockSetup();

#endif
