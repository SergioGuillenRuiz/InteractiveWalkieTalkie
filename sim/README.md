# Simulador nativo del walkie-talkie

Simulador de PC que **compila y ejecuta el firmware real** de `../src` (la misma
`setup()`/`loop()`, la misma máquina de estados, el mismo Adafruit_GFX y el mismo
tiny-AES que la placa). Permite probar las funcionalidades y la navegación de los
menús **sin tocar el hardware**, de forma rápida, reproducible y automatizable.

No es una reimplementación: la lógica que se ejecuta es exactamente la del
microcontrolador. Solo se sustituye la capa de hardware (Arduino core, OLED, LoRa,
botones, potenciómetro, EEPROM) por una capa *mock* equivalente.

```
        TU FIRMWARE (../src/*.cpp)  ── sin cambios ──┐
                                                      │  compila con MSVC
   capa mock Arduino  (sim/arduino) ─────────────────┤  -> walkie_sim.exe
   Adafruit_GFX + tiny-AES reales (sim/vendor) ──────┤
   motor del simulador (sim/engine) ─────────────────┘
```

## Requisitos

- **Visual Studio Build Tools** (o Visual Studio) con *"Desktop development with C++"*
  (proporciona `cl.exe`). El script lo localiza automáticamente con `vswhere`.
- Windows. (No requiere PlatformIO, ni internet, ni token de Wokwi.)

## Uso rápido

```bat
cd sim

run_tests.bat          ::  compila y ejecuta TODA la batería de tests
run.bat                ::  compila y abre el modo INTERACTIVO (teclado)
run.bat scripts\nav.sim::  compila y ejecuta un script concreto
build.bat              ::  solo compilar -> out\walkie_sim.exe
```

### Modo interactivo (teclado)

```
[m] / [n]              pulsar (tap) Morse / Finish
[Shift+M] / [Shift+N]  pulsación larga Morse / Finish
[Flecha arriba/abajo]  girar el potenciómetro (+ / -)
[r]                    simular recepción LoRa ("happy")
[q]                    salir
```
Las teclas también se muestran bajo la pantalla mientras corre. La pantalla OLED
128×128 se dibuja con caracteres braille (2×4 píxeles por carácter), así ocupa
64×32 caracteres y los píxeles se ven cuadrados, a tamaño parecido al real.
Conviene maximizar la ventana del terminal (necesita ~34 líneas de alto).

## Tests automatizados

Los scripts viven en `scripts/*.sim`. Cada uno conduce el firmware y comprueba
aserciones; el ejecutable devuelve código de salida ≠ 0 si alguna falla.

| Script             | Qué prueba |
|--------------------|------------|
| `nav.sim`          | Navegación por los 3 menús y vuelta a IDLE |
| `send_instant.sim` | Enviar un mensaje predefinido + verificar el cifrado LoRa |
| `send_morse.sim`   | Crear una letra en Morse y enviarla |
| `lora_rx.sim`      | Recepción LoRa: descifrado y guardado en historial |
| `history.sim`      | Recibir varios mensajes, verlos y borrar uno |
| `sleep_wake.sim`   | Suspensión por inactividad y despertar con 3 pulsaciones |
| `games.sim`        | Poker, Choose4Me y juego "Próximamente" (incluye regresiones) |

Ejecutar uno con salida detallada:

```bat
out\walkie_sim.exe --fresh scripts\history.sim
```

## Lenguaje de scripts

Un comando por línea. Las líneas que empiezan por `#`, y todo lo que siga a ` #`,
son comentarios.

**Entradas**
```
pot <0-1023>            fija el potenciómetro
pot% <0-100>            fija el potenciómetro en %
morse down|up           estado del botón Morse (manual)
finish down|up          estado del botón Finish (manual)
tap morse|finish [ms]   pulsación completa (por defecto 150 ms)
hold morse|finish <ms>  pulsación larga
in <ms> morse|finish down|up   programa un evento dentro de <ms> (timeline)
in <ms> pot <v>                 programa un cambio de potenciómetro
```

**Tiempo** (ejecutan el `loop()` real avanzando el reloj virtual)
```
wait <ms>    avanza ejecutando el firmware
run <ms>     alias de wait (estilo timeline)
ff <ms>      avance rápido (para timeouts largos, p.ej. el sueño de 5 min)
```

**LoRa**
```
lora rx <texto>     inyecta un mensaje entrante (lo cifra con la clave del firmware)
lora rxraw <hex>    inyecta bytes crudos
lora loopback on|off  reenvía lo transmitido como recibido
lora sent           muestra el último paquete transmitido (y su descifrado)
```

**Inspección y aserciones**
```
screen              dibuja el OLED en el terminal
text                imprime el texto dibujado en pantalla
serial              imprime el log serie del firmware
shot [nombre]       guarda una captura BMP en out\
expect text <sub>      la pantalla contiene <sub>
expect notext <sub>    la pantalla NO contiene <sub>
expect serial <sub>    el log serie contiene <sub>
expect sent <sub>      el último TX LoRa descifra y contiene <sub>
expect pixel <x> <y> on|off   estado de un píxel
print <texto>          imprime una nota
reboot                 re-ejecuta setup() (recarga el historial de EEPROM)
reset-eeprom           borra la EEPROM
```

### Ejemplo

```
wait 300
pot 80              # cursor en "Enviar"
wait 200
tap morse           # entrar
wait 200
expect text Selecciona
```

## Opciones del ejecutable

```
walkie_sim.exe [script.sim] [opciones]
  --interactive     modo teclado
  --fresh           borra la EEPROM al arrancar
  --color           color en el render del terminal
  --eeprom <fich>   fichero de respaldo de EEPROM (por defecto out\eeprom.bin)
  --shots <dir>     carpeta de capturas (por defecto la del .exe)
  --scale <n>       escala de las capturas BMP (por defecto 4 -> 512×512)
```
Sin script y sin `--interactive`, lee comandos por la entrada estándar.

## Fidelidad respecto al hardware real

| Aspecto | Simulador |
|---|---|
| Lógica del firmware | **idéntica** (compila `../src` sin cambios) |
| Render OLED | **idéntico** (Adafruit_GFX + fuente reales, 128×128, 1 bpp) |
| Cifrado de mensajes | **idéntico** (tiny-AES real, AES-128-CBC + PKCS7) |
| Historial / EEPROM | respaldado en fichero; persiste entre ejecuciones |
| Botones (antirrebote) | reloj virtual; el antirrebote se ejerce de verdad |
| Potenciómetro | valor 0–1023 controlable |
| LoRa (SX1276) | mock: captura TX, inyecta RX, eco opcional |
| Tiempos | reloj virtual determinista (no en tiempo real, salvo modo interactivo) |

Diferencias: no se simula el RF físico (ruido, alcance), ni el ruido del ADC, ni
la latencia exacta del bus I²C. Para eso haría falta el hardware real.

## Estructura

```
sim/
  arduino/    capa mock del core de Arduino (Arduino.h, Wire, SPI, EEPROM, LoRa,
              Adafruit_SH110X host sobre GFX, String, Print, pgmspace)
  vendor/     librerías reales vendorizadas (Adafruit_GFX, tiny-AES)
  engine/     motor: reloj virtual, eventos de entrada, framebuffer, driver/main
  scripts/    scripts de prueba (.sim)
  out/        artefactos de compilación (ignorado por git)
  build.bat / run.bat / run_tests.bat
```

## Cómo añadir un test

1. Crea `scripts/mi_test.sim` con comandos + `expect ...`.
2. `run.bat scripts\mi_test.sim` para depurarlo (usa `screen`/`shot`/`text`).
3. Quedará incluido automáticamente en `run_tests.bat`.
