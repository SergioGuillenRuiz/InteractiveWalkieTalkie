# arduino-LoRa 0.8.0 (sin modificar)

Copia literal de [sandeepmistry/arduino-LoRa](https://github.com/sandeepmistry/arduino-LoRa) en su
version **0.8.0** (`src/LoRa.h`, `src/LoRa.cpp`, licencia MIT): la misma que resuelve
`sandeepmistry/LoRa@^0.8.0` en `platformio.ini`.

El simulador compila esta libreria **tal cual** y simula el chip SX1276 por debajo, a nivel de
registros (`../../engine/sx127x.cpp`): asi el firmware ejecuta el codigo REAL de la libreria
(`parsePacket()`, `idle()`, `receive()`, `endPacket()`...) y se reproduce lo que hace de verdad
con la radio (en que modo la deja, cuando escucha, cuanto bloquea al transmitir).
