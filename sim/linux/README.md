# Simulador — versión Linux (GNOME)

Versión Linux del simulador nativo. Es **el mismo simulador** que el de Windows
(`../README.md`): compila el firmware real de `../../src` con la misma capa mock
(`../arduino`), las mismas librerías (`../vendor`), el mismo motor (`../engine`)
y los mismos scripts de prueba (`../scripts`). Solo cambia la capa de plataforma
y los lanzadores, que viven separados en esta carpeta:

| Windows (`sim/`)            | Linux (`sim/linux/`)           | Qué hace |
|-----------------------------|--------------------------------|----------|
| `build.bat` (MSVC `cl`)     | `build.sh` (g++/gcc)           | Compila → `out/walkie_sim.exe` / `out/linux/walkie_sim` |
| `run.bat`                   | `run.sh`                       | Modo interactivo o un script |
| `run_tests.bat`             | `run_tests.sh`                 | Toda la batería de tests |
| `net.bat` + `net.ps1`       | `net.sh`                       | N dispositivos interactivos comunicados por radio |
| `net_test.ps1`              | `net_test.sh`                  | Prueba automatizada multi-dispositivo |
| `engine/console_win.cpp`    | `console_linux.cpp`            | Ajuste de la consola para píxeles cuadrados |
| `<conio.h>`, `<process.h>`  | `platform_linux.h`             | Teclado sin eco (termios), pid, ruta del ejecutable |

Las compilaciones están separadas: Windows deja sus artefactos en `sim/out/` y
Linux en `sim/out/linux/` (ejecutable, objetos, EEPROM y capturas).

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

Mismas teclas que en Windows: `m` / `n` pulsar, `M` / `N` pulsación larga,
flechas arriba/abajo para el potenciómetro, `r` simula recepción LoRa, `q` salir.

El render cuadrado necesita 128 columnas y ~68 filas. `run.sh` y `net.sh` abren
GNOME Terminal con `--geometry=132x72` y un `--zoom` calculado para que quepa en
el monitor (lo mismo que `run.bat` consigue fijando la fuente de `conhost`). Si
no te encaja, fuerza otro zoom:

```bash
SIM_ZOOM=0.6 ./run.sh
```

Si la ventana se queda pequeña, el simulador pasa a la vista compacta (braille);
agranda la ventana o reduce el zoom (`Ctrl + -`) para volver a los píxeles
cuadrados. Al salir (también con `Ctrl+C`) se restaura el modo del terminal.

### Ejecutable directo

Las opciones son las mismas que las de `walkie_sim.exe` (ver `../README.md`):

```bash
../out/linux/walkie_sim --fresh ../scripts/history.sim
SIM_CHIPID=200 ../out/linux/walkie_sim --interactive --color --air /tmp/aire --node 2
```

## Notas de implementación

- `../engine/sim_main.cpp` es compartido; solo tiene unos pocos `#ifdef _WIN32`
  (cabeceras `conio.h`/`process.h`, `timeBeginPeriod`, `chcp` y el separador de
  ruta de la EEPROM). En Windows se compila exactamente igual que antes.
- `platform_linux.h` traduce las flechas VT (`ESC [ A` / `ESC [ B`) al formato de
  `conio` (`0xE0` + 72/80) y deja pasar intacta la respuesta DSR con la que el
  simulador mide el terminal.
