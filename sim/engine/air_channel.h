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
//  Lo que se comparte son las tramas (bytes + parametros de modulacion); el modelo del chip
//  receptor (sx127x.cpp) decide si las oye segun si escucha en ese momento y con que parametros.
//
//  Dos modos de tiempo:
//   - LIBRE (por defecto): cada proceso lleva su PROPIO reloj virtual, anclado al reloj de pared
//     con --pace. Los relojes quedan desfasados decenas de ms, asi que dos tramas que en la
//     realidad se pisarian (o no) dependen del azar del arranque: sirve para ver la comunicacion
//     funcionando, no para medir colisiones.
//   - SINCRONIZADO (--nodes N, solo POSIX): los N procesos comparten el MISMO tiempo virtual y
//     avanzan juntos milisegundo a milisegundo (barrera en memoria compartida). Lo emitido en el
//     ms t lo oyen los demas desde el ms t+1 con su instante de inicio exacto, asi que las
//     colisiones, el half-duplex y los tiempos en el aire son los de la realidad y el resultado es
//     DETERMINISTA (no depende de la velocidad de la maquina ni del azar del arranque).
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
    double startMs = -1;      // modo sincronizado: instante (reloj comun) en que empezo a emitirse; <0 = ahora
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

// ---- Modo sincronizado ----
// Activa el modo sincronizado para 'nodes' procesos (cada uno lo llama con el mismo valor) y espera a que
// arranquen todos. Devuelve false si no esta disponible (Windows) o no se pudo.
bool air_lockstep_enable(int nodes);
bool air_lockstep();                         // activo en este proceso
// El proceso acaba de completar el ms 'doneMs' del reloj comun (cuenta ms desde el arranque): espera
// a que los demas hayan completado tambien el suyo.
void air_lockstep_tick(long long doneMs);
void air_lockstep_bye();                     // este proceso termina: los demas dejan de esperarle

#endif
