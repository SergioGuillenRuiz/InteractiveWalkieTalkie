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
128×128 se dibuja con **medios bloques** (`▀ ▄ █`): cada carácter pinta 2 píxeles
verticales rellenos, así los píxeles salen **cuadrados y sólidos**, igual que el
OLED real. Ocupa siempre **128×64 caracteres** (alto fijo), por lo que el terminal
debe tener al menos 128 columnas y ~68 filas; **maximiza la ventana** y, si no
cabe entera, **reduce la fuente** (Ctrl + −) — eso además hace los píxeles más
pequeños y más parecidos al hardware.

El modo interactivo corre el firmware en tiempo real y lee el teclado **incluso
mientras un juego o menú está esperando una pulsación**, de modo que se puede
entrar y jugar a cualquier pantalla (Poker, Choose4Me, etc.) sin bloqueos.

### Reproducir teclas con guion (demos / pruebas)

`run.bat --keys guion.txt` reproduce una secuencia de teclas sin necesidad de
teclado. Cada línea es `<ms> <acción>`, donde `ms` son milisegundos (virtuales)
desde el arranque y la acción es una tecla (`m`, `n`, `M`, `N`, `r`, `q`) o
`pot <valor>`. Útil para reproducir un fallo o grabar una demo de un juego.
Conviene maximizar la ventana del terminal (necesita ~34 líneas de alto).

## Comunicación multi-dispositivo (varios equipos a la vez)

Para probar la comunicación **real** entre equipos, el simulador puede unir varios
procesos en un **"aire" compartido**: lo que **transmite** un dispositivo lo
**reciben** todos los demás (menos él mismo, half-duplex), igual que la difusión
RF real. Cada proceso es un dispositivo **independiente y real**: su propio
firmware, su propia EEPROM y su propia identidad de equipo (`Device_id`).

No es una maqueta: el cifrado AES, el sobre de chat, el auto-ACK de entrega y el
historial son los mismos que en la placa. Lo único simulado es el medio (un
directorio donde cada transmisión es un fichero de paquete que los demás leen).

### Interactivo — varias ventanas que se hablan

```bat
cd sim
net.bat 3      ::  abre 3 dispositivos, cada uno en su ventana, comunicados por radio
net.bat        ::  por defecto, 2 dispositivos
```

Cada ventana es un equipo con su teclado (`m`/`n`, mayúsculas para pulsación larga,
flechas para el potenciómetro, `q` para salir). Envía un mensaje desde uno y
aparecerá en el historial de los demás; verás la confirmación **"Entregado"** en
el emisor cuando otro equipo lo recibe.

### Automatizado — para testear futuras funciones de comunicación

`net_test.ps1` lanza varios dispositivos en paralelo (sin teclado), conduce un
intercambio y comprueba aserciones sobre lo que cada uno recibió. Es la **plantilla
para probar futuras funciones** que impliquen a más de un equipo:

```bat
cd sim
powershell -ExecutionPolicy Bypass -File net_test.ps1
```

Lanza 3 equipos reales; el #1 difunde un mensaje y se verifica que #2 y #3 lo
reciben y que el #1 recibe el ACK de ambos. Internamente cada equipo corre en modo
`--keys` (tiempo real, con guion de teclas) y comparte el aire con
`--air <dir> --node <id>`; su salida serie se vuelca a un fichero y se comprueba.

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
| `chat.sim`         | Chat: id de emisor, auto-ACK, enviados en historial, entrega y fechas tras reinicio |
| `sleep_wake.sim`   | Suspensión por inactividad y despertar con 3 pulsaciones |
| `games.sim`        | Poker, Choose4Me y juego "Próximamente" (incluye regresiones) |
| `reliable.sim`     | Entrega fiable: dedup de RX + reintentos + outbox persistente (sobrevive a reboot) |
| `presence.sim`     | Presencia del compañero por baliza: online tras oírla, offline tras el timeout |
| `clock.sim`        | Reloj compartido: hora fijada/sincronizada, antigüedad real y persistencia tras reboot |
| `battery.sim`      | Aviso de batería baja con histéresis (medidor + "!" en la barra de estado) |

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
chatmsg <emisor> <msgId> <texto>   inyecta un MENSAJE de chat de un peer (con sobre)
chatack <destino> <msgId>          inyecta un ACK de un peer (confirmación de entrega)
presence <peerId> [epoch] [batt]   inyecta una BALIZA de presencia de un peer
in <ms> presence <peerId> [epoch] [batt]   baliza diferida (durante esperas bloqueantes)
```

**Batería / hora / reinicio** (para las features de batería, presencia y reloj)
```
battery <0-100>        fija el nivel de batería simulado (canal ADC mockeado)
in <ms> battery <pct>  cambio de batería diferido (durante una espera bloqueante)
settime <epoch>        fija el reloj de pared (segundos epoch) y lo persiste
reboot-cold            reinicio EN FRÍO: re-ejecuta setup() Y pone millis()=0
```
`reboot-cold` conserva la EEPROM (es justo lo que se quiere probar: que algo
persista) y la batería mockeada, pero descarta los eventos diferidos pendientes
(`in <ms> ...` que aún no hayan vencido), igual que un reinicio real perdería
entradas en vuelo.

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
  --keys <fich>     modo interactivo reproduciendo teclas con guion
  --fresh           borra la EEPROM al arrancar
  --color           color en el render del terminal
  --eeprom <fich>   fichero de respaldo de EEPROM (por defecto out\eeprom.bin)
  --shots <dir>     carpeta de capturas (por defecto la del .exe)
  --scale <n>       escala de las capturas BMP (por defecto 4 -> 512×512)
  --air <dir>       conecta este dispositivo al "aire" compartido (radio multi-dispositivo)
  --node <etiqueta> identidad única en el aire (para no oír lo propio)
  --rssi <dBm>      potencia con que los demás oyen sus transmisiones (por defecto -50)
```
Sin script y sin `--interactive`, lee comandos por la entrada estándar.
Para dar un id de equipo distinto a cada dispositivo, exporta `SIM_CHIPID` antes de
lanzarlo (`Device_id()` lo deriva de ahí en el simulador).

## Fidelidad respecto al hardware real

| Aspecto | Simulador |
|---|---|
| Lógica del firmware | **idéntica** (compila `../src` sin cambios) |
| Render OLED | **idéntico** (Adafruit_GFX + fuente reales, 128×128, 1 bpp) |
| Cifrado de mensajes | **idéntico** (tiny-AES real, AES-128-CBC + PKCS7) |
| Historial / EEPROM | respaldado en fichero; persiste entre ejecuciones |
| Botones (antirrebote) | reloj virtual; el antirrebote se ejerce de verdad |
| Potenciómetro | valor 0–1023 controlable |
| LoRa (SX1276) | mock: captura TX, inyecta RX, eco opcional; **aire compartido real entre varios procesos** (`--air`) |
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
