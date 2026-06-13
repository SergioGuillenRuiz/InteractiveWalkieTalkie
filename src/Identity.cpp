#include "Identity.h"
#include <stdlib.h>

uint8_t Device_id() {
  static uint8_t cached = 0;
  if (cached) return cached;

  uint32_t raw;
#if defined(ESP8266)
  raw = ESP.getChipId();                       // unico por chip
#elif defined(ESP32)
  raw = (uint32_t)ESP.getEfuseMac();
#else
  // Simulador: permite IDs distintos por dispositivo (SIM_CHIPID).
  const char *e = getenv("SIM_CHIPID");
  raw = e ? (uint32_t)strtoul(e, nullptr, 0) : 0x12345678u;
#endif

  cached = (uint8_t)(raw % 254u) + 1u;          // 1..254 (0 = desconocido)
  return cached;
}
