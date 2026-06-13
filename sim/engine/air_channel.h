#ifndef AIR_CHANNEL_H
#define AIR_CHANNEL_H

// ============================================================
//  "Aire" compartido: canal de radio LoRa entre varios procesos del simulador.
//
//  Modela la difusion RF real: lo que TRANSMITE un dispositivo lo RECIBEN todos
//  los demas (menos el propio emisor, half-duplex). El medio es un directorio
//  compartido donde cada transmision es un fichero de paquete; los receptores
//  leen los paquetes nuevos que no han emitido ellos.
//
//  Cada proceso = un dispositivo real e independiente (firmware + EEPROM propios).
// ============================================================

#include <string>
#include <vector>
#include <utility>

// Activa el aire para este proceso. dir = directorio compartido; node = etiqueta
// unica del dispositivo (para no recibir lo propio); rssi = potencia con la que
// los demas "oyen" sus transmisiones (dBm).
void air_init(const char *dir, const char *node, int rssi);

bool air_enabled();

// Publica un paquete (hex ya cifrado, tal cual sale de la radio) en el aire.
void air_publish(const std::string &payloadHex);

// Recoge los paquetes nuevos de OTROS nodos: pares (hex, rssi).
void air_poll(std::vector<std::pair<std::string, int>> &out);

#endif
