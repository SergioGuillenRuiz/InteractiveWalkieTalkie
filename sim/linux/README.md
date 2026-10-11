# Simulador — lanzadores Linux (GNOME)

Aquí viven los scripts con los que se compila y se usa el simulador nativo (descrito en [`../README.md`](../README.md)):
compila el firmware real de `../../src` con la capa mock (`../arduino`), las librerías (`../vendor`), el motor
(`../engine`) y ejecuta los scripts de prueba (`../scripts`). Linux es la plataforma del simulador.

| Script / fichero      | Qué hace |
|-----------------------|----------|
| `build.sh` (g++/gcc)  | Compila → `../out/linux/walkie_sim` |
| `run.sh`              | Modo interactivo o un script |
| `run_tests.sh`        | Toda la batería de tests |
| `seed_tests.sh`       | Toda la batería con muchas secuencias aleatorias (`./seed_tests.sh [desde] [hasta] [paralelo]`) |
| `net.sh`              | N dispositivos interactivos comunicados por radio |
| `net_test.sh`         | Prueba automatizada multi-dispositivo (11 escenas A–K: chat, hora, dibujos, las partidas de 3 en raya, Tetris y HippoRadar entre dos firmwares reales (16 semillas cada una) y el **emparejamiento** entre dos equipos sin clave (8 semillas: mismo código en los dos y clave nueva funcionando)). Los procesos comparten el mismo tiempo virtual (`--nodes N`: colisiones reales y resultado determinista, ~15 s) |
| `console_linux.cpp`   | Ajuste de la consola para píxeles cuadrados |
| `platform_linux.h`    | Teclado sin eco (termios), pid, ruta del ejecutable |
| `term_common.sh`      | Funciones comunes de `run.sh` y `net.sh` (tamaño y zoom de la ventana) |

Los artefactos (ejecutable, objetos, EEPROM y capturas) quedan en `sim/out/linux/`.

## Requisitos

- `g++` y `gcc` (C++17). En Debian/Ubuntu: `sudo apt install build-essential`;
  en Fedora: `sudo dnf install gcc-c++`; en Arch: `sudo pacman -S base-devel`.
- Para las ventanas: **GNOME Terminal** (`gnome-terminal`). Sin él, el modo
  interactivo se ejecuta en el terminal actual.
- Opcional: `xrandr` (calcula el zoom según el monitor) e ImageMagick (convierte
  las capturas BMP a PNG en `net_test.sh`).

## Uso rápido

```bash
cd sim/linux

./run_tests.sh            # compila y ejecuta TODA la batería de tests
./run.sh                  # compila y abre el modo INTERACTIVO en una ventana nueva
./run.sh --here           # modo interactivo en el terminal actual
./run.sh ../scripts/nav.sim   # ejecuta un script concreto
./build.sh                # solo compilar -> ../out/linux/walkie_sim
./net.sh 3                # 3 dispositivos, cada uno en su ventana
./net_test.sh             # prueba automatizada multi-dispositivo
```

`build.sh` es incremental (solo recompila lo que cambió) y compila en paralelo.
Admite las variables `CXX`, `CC` (p.ej. `CXX=clang++ CC=clang ./build.sh`) y `JOBS`.

### Modo interactivo

Teclas: `m` / `n` pulsar, `M` / `N` pulsación larga,
flechas arriba/abajo para el potenciómetro, `r` simula recepción LoRa, `q` salir.

El render cuadrado necesita 128 columnas y ~68 filas. `run.sh` y `net.sh` abren
GNOME Terminal con `--geometry=132x72` y un `--zoom` calculado para que quepa en
el monitor. Si no te encaja, fuerza otro zoom:

```bash
SIM_ZOOM=0.6 ./run.sh
```

Si la ventana se queda pequeña, el simulador pasa a la vista compacta (braille);
agranda la ventana o reduce el zoom (`Ctrl + -`) para volver a los píxeles
cuadrados. Al salir (también con `Ctrl+C`) se restaura el modo del terminal.

### Ejecutable directo

Opciones del ejecutable (ver `../README.md`):

```bash
../out/linux/walkie_sim --fresh ../scripts/history.sim
SIM_CHIPID=200 ../out/linux/walkie_sim --interactive --color --air /tmp/aire --node 2
SIM_SEED=7 ../out/linux/walkie_sim --fresh ../scripts/peers.sim   # otra secuencia aleatoria (retardos de ACK, jitter...)
```

Para descubrir tests que solo pasan con una combinación afortunada de tiempos, ejecútalos con muchas semillas:
`./seed_tests.sh 1 50` (toda la batería, 4 procesos en paralelo) o
`for s in $(seq 1 50); do SIM_SEED=$s ../out/linux/walkie_sim --fresh ../scripts/x.sim; done` (una suite).
`SIM_TRACE=1` vuelca cada línea del puerto serie por la salida de error con su instante virtual (`[  12345] ...`): sirve para ver
CUÁNDO pasó cada cosa en una escena de varios equipos.

## Notas de implementación

- `../engine/sim_main.cpp` incluye `platform_linux.h`, que da el teclado sin eco (`_kbhit`/`_getch`, con los
  nombres de `<conio.h>` con los que está escrito el motor), el pid y la ruta del ejecutable.
- `platform_linux.h` traduce las flechas VT (`ESC [ A` / `ESC [ B`) al formato de `conio` (`0xE0` + 72/80) y deja
  pasar intacta la respuesta DSR con la que el simulador mide el terminal.
