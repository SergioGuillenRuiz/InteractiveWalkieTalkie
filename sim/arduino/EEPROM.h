// Host mock of <EEPROM.h> (estilo ESP8266/ESP32) respaldado por un fichero,
// de modo que el historial persiste entre ejecuciones (como la flash real).
#ifndef SIM_EEPROM_H
#define SIM_EEPROM_H

#include <cstdint>
#include <cstddef>

class EEPROMClass {
public:
    void begin(size_t size);
    uint8_t read(int address);
    void write(int address, uint8_t value);
    bool commit();
    void end();

    // Ruta del fichero de respaldo (configurable desde el simulador)
    static void setBackingFile(const char *path);
};

extern EEPROMClass EEPROM;

#endif // SIM_EEPROM_H
