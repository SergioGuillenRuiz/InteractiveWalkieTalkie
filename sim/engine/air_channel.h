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
//  Como en la radio real, solo se oye lo emitido MIENTRAS la radio esta
//  encendida: un dispositivo que arranca despues no recibe paquetes anteriores.
//  Los paquetes caducan a los pocos segundos y se borran del directorio.
//
//  Cada proceso = un dispositivo real e independiente (firmware + EEPROM propios).
//  Cada proceso lleva su PROPIO reloj virtual: lo que se comparte son las tramas (bytes
//  + parametros de modulacion); el modelo del chip receptor (sx127x.cpp) decide si las
//  oye segun si escucha en ese momento y con que parametros.
// ============================================================

#include <string>
#include <vector>

// Una trama en el aire: los bytes tal cual salen de la radio (binarios) y los parametros
// con que se modularon (para que el receptor sepa si puede demodularla).
struct AirFrame {
    std::string payload;
    int  rssi = -50;          // dBm con que la oye el receptor
    long freq = 868000000;    // Hz
    int  sf = 7;              // factor de dispersion
    long bw = 125000;         // Hz
    int  cr = 1;              // 1..4 => 4/5..4/8
    int  preamble = 8;        // simbolos
    bool crc = true;
    int  sync = 0x12;
};

// Activa el aire para este proceso. dir = directorio compartido; node = etiqueta
// unica del dispositivo (para no recibir lo propio); rssi = potencia con la que
// los demas "oyen" sus transmisiones (dBm).
void air_init(const char *dir, const char *node, int rssi);

bool air_enabled();

// Publica una trama (la que esta transmitiendo este equipo) en el aire.
void air_publish(const AirFrame &frame);

// Recoge las tramas nuevas de OTROS nodos.
void air_poll(std::vector<AirFrame> &out);

#endif
