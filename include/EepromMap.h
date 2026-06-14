#ifndef EEPROM_MAP_H
#define EEPROM_MAP_H

// ============================================================
//  Mapa unico de la EEPROM (un solo sector de flash en el ESP8266).
//
//  En ESP8266 (y en el mock del simulador) un commit() reescribe TODO el
//  buffer del tamano pasado a begin(): lo que quede por encima de ese tamano
//  se borra a 0xFF. Por eso TODOS los modulos que usan la EEPROM deben hacer
//  begin(EE_TOTAL_SIZE), de modo que cada commit preserve las regiones ajenas
//  (se leen al hacer begin y se reescriben intactas).
//
//   [0 .. EE_HISTORIAL_END)        -> Historial (ver Historial.cpp)
//   [EE_FRASERO_BASE .. +SIZE)     -> Frasero: piezas propias (ver Frasero.cpp)
// ============================================================

// Fin de la region del historial. DEBE coincidir con su EEPROM_SIZE
// (HDR_SIZE + MSG_SLOT_SIZE * MAX_MESSAGES = 4 + 107*10 = 1074).
// Un static_assert en Historial.cpp avisa si deja de coincidir.
#define EE_HISTORIAL_END   1074

// --- Region del Frasero (piezas propias del usuario) ---
#define FR_CATEGORIES      3            // Sujeto / Verbo / Objeto
#define FR_SLOTS_PER_CAT   4            // piezas propias por categoria
#define FR_PIECE_MAXLEN    24           // chars utiles por pieza
#define FR_HDR_SIZE        4            // 3 magia + 1 version
#define FR_SLOT_SIZE       (1 + FR_PIECE_MAXLEN)   // 1B longitud + texto

#define EE_FRASERO_BASE    EE_HISTORIAL_END
#define EE_FRASERO_SIZE    (FR_HDR_SIZE + FR_SLOT_SIZE * FR_CATEGORIES * FR_SLOTS_PER_CAT)

// Tamano total que TODOS deben pasar a EEPROM.begin().
#define EE_TOTAL_SIZE      (EE_FRASERO_BASE + EE_FRASERO_SIZE)

#endif
