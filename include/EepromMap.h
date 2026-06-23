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
//   [EE_OUTBOX_BASE .. +SIZE)      -> Outbox: mensajes sin confirmar (ver Chat.cpp)
//   [EE_CLOCK_BASE .. +SIZE)       -> Reloj: epoch persistido (ver Clock.cpp)
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

// --- Region OUTBOX: mensajes enviados pendientes de confirmacion (ACK) ---
// Permite reintentar tras un corte de enlace e incluso tras reiniciar.
// Slot: 1B estado + 1B msgId + 1B destino + 1B longitud + texto(+\0).
#define EE_OUTBOX_BASE     (EE_FRASERO_BASE + EE_FRASERO_SIZE)   // 1378
#define OB_HDR_SIZE        4            // 3 magia {'O','B','1'} + 1 version
#define OB_MAX_MSG_LEN     100          // mismo limite que el historial (99 utiles + \0)
#define OB_SLOT_SIZE       (1 + 1 + 1 + 1 + OB_MAX_MSG_LEN)      // 104
#define OB_SLOTS           4            // mensajes pendientes simultaneos
#define EE_OUTBOX_SIZE     (OB_HDR_SIZE + OB_SLOT_SIZE * OB_SLOTS)   // 420

// --- Region RELOJ: epoch (segundos) persistido para sobrevivir reinicios ---
#define EE_CLOCK_BASE      (EE_OUTBOX_BASE + EE_OUTBOX_SIZE)     // 1798
#define CLK_HDR_SIZE       4            // 3 magia {'C','K','1'} + 1 version
#define EE_CLOCK_SIZE      (CLK_HDR_SIZE + 4)                    // 8 (4B epoch)

// Tamano total que TODOS deben pasar a EEPROM.begin() (un commit con un tamano
// menor borraria a 0xFF las regiones de arriba, asi que SIEMPRE EE_TOTAL_SIZE).
#define EE_TOTAL_SIZE      (EE_CLOCK_BASE + EE_CLOCK_SIZE)       // 1806

// Cota fisica: el sector de EEPROM del ESP8266 es 4096 B y el backing del
// simulador usa 2048 B; el mapa debe caber en ambos.
#if (EE_TOTAL_SIZE > 2048)
#error "EE_TOTAL_SIZE no cabe en el tamano de EEPROM del simulador (2048)"
#endif

#endif
