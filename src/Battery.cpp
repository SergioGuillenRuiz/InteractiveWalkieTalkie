#include <Arduino.h>
#include "Config.h"
#include "Battery.h"

// Lectura cruda del ADC de la batería (0..1023).
static int batteryReadRaw() {
#if defined(ESP8266)
  // HARDWARE REAL: el único ADC (A0) lo usa el potenciómetro. Hasta cablear una
  // fuente real de batería (divisor en un ADC dedicado / multiplexor, o
  // ESP.getVcc() sacrificando el pote) devolvemos "lleno" para no dar falsas
  // alarmas. SUSTITUIR esta línea por la lectura real cuando se cablee y, para que
  // el medidor no parpadee por el ruido del ADC, suavizarla (media móvil / EMA):
  return 1023;   // <-- AJUSTAR AL HARDWARE
#else
  return analogRead(PIN_VBAT);   // simulador: canal de batería mockeado
#endif
}

uint8_t Battery_percent() {
  int raw = batteryReadRaw();
#if defined(ESP8266)
  // Mapeo del ADC real a % (AJUSTAR a tu divisor: raw vacío..lleno).
  long pct = map(raw, 0, 1023, 0, 100);
#else
  long pct = (long)raw * 100 / 1023;   // simulador: lineal y predecible
#endif
  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;
  return (uint8_t)pct;
}

bool Battery_isLow() {
  static bool low = false;             // histéresis para no parpadear
  uint8_t pct = Battery_percent();
  if (low) { if (pct >= BATT_OK_PCT)  low = false; }
  else     { if (pct <= BATT_LOW_PCT) low = true;  }
  return low;
}
