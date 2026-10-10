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

> **¿Linux?** Hay una versión equivalente (GNOME, g++) en [`linux/`](linux/README.md)
> con sus propios scripts (`build.sh`, `run.sh`, `run_tests.sh`, `net.sh`,
> `net_test.sh`). Usa el mismo firmware, motor y scripts de prueba que esta.

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
Como en la radio real, un equipo solo oye lo que se emite mientras está
encendido (si arranca después, no recibe los mensajes anteriores) y cada paquete
caduca a los 10 s y se borra, así el directorio no crece en sesiones largas.

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

`net_test.sh` (Linux) / `net_test.ps1` (Windows) lanza varios dispositivos en paralelo (sin teclado),
conducen un intercambio y comprueban aserciones sobre lo que cada uno recibió. Son la **plantilla
para probar futuras funciones** que impliquen a más de un equipo:

```bat
cd sim
powershell -ExecutionPolicy Bypass -File net_test.ps1     ::  Windows
./linux/net_test.sh                                       #   Linux
```

Escenas: difusión de 1 equipo a 2 (con los ACK de ambos), mensajes en los dos sentidos, **emisión
simultánea** (las dos tramas se pisan y se recuperan por reintentos), hora puesta a mano que adopta
otro equipo, tres equipos viéndose por las balizas y un **dibujo** de un equipo a otro (con su confirmación). Internamente cada equipo corre en modo `--keys`
(con guion de teclas) y comparte el aire con `--air <dir> --node <id>`; su salida serie y el resumen de
radio (`[radio]`: qué emitió y qué le llegó, y por qué se perdió lo que no oyó) se vuelcan a un fichero
y se comprueban.

#### Tiempo sincronizado entre procesos (`--nodes N`, Linux/macOS)

Con `--nodes N` los N procesos comparten **el mismo tiempo virtual** y avanzan juntos milisegundo a
milisegundo (barrera en memoria compartida: `<aire>/board.bin`). Lo que emite un equipo en el ms *t* lo
oyen los demás desde el ms *t+1* con su instante de inicio exacto, de modo que los tiempos en el aire, el
half-duplex y las **colisiones son los de la realidad** y el resultado es **determinista**: no depende de la
velocidad de la máquina ni del azar del arranque (las 6 escenas tardan ~2 s). Sin `--nodes` cada proceso
lleva su propio reloj, anclado al reloj de pared con `--pace`: sirve para ver la comunicación funcionando
(es lo que usa `net.sh` interactivo), pero los relojes quedan desfasados decenas de ms y dos tramas que en
la realidad se pisarían, o no, dependen del azar. En Windows el modo sincronizado no está disponible
(`net_test.ps1` usa el modelo `--radio ideal`, sin tiempo en el aire, con `--pace`).

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
| `doodle.sim`       | Lienzo 24×24: editor, envío con pantalla de resultado ("Enviado" → "Entregado!" / "(sin confirmar)"), reintentos, el dibujo enviado y el recibido en el Historial |
| `ttt_rx.sim`       | Chat DENTRO de Tres en raya: mensaje guardado y confirmado sin interrumpir la partida, baliza, ACK ajeno y paquetes de otros juegos sin basura en el historial, balizas y reintentos durante la partida |
| `tetris_rx.sim`    | Lo mismo dentro de Tetris Coop (cliente): lobby y partida |
| `hippo_rx.sim`     | HippoRadar (SF10) y el chat: pings por la cola de juego, el chat (SF7) no se oye durante el radar, sin balizas ni reintentos mientras dura, y al salir se anuncia y el chat vuelve |
| `doodle_store.sim` | Dibujos persistentes: el lienzo vive en su registro del Historial (sobrevive a reinicios, sigue a su registro al desplazarse o borrarse, sin duplicados, outbox persistente con el mismo lienzo) |
| `clock_set.sim`    | Pantalla "Poner la hora": abrir (B mantenida), tres pasos con el pote, cancelar, guardar, persistencia, radio activa durante el ajuste |
| `clock_gen.sim`    | Autoridad de la hora entre equipos (generación de ajuste: más reciente gana aunque sea hacia atrás, igual converge, vuelta del contador, baliza antigua) y edades del historial conservadas |
| `radio_model.sim`  | Autotest del modelo de radio: la librería LoRa real contra el chip simulado (tiempos en el aire del roadmap, bloqueo al transmitir, reglas de recepción) |
| `radio_rx.sim`     | La radio escucha SIEMPRE (menú, juegos, esperas bloqueantes, tras emitir), vigilancia que re-arma un chip caído, sueño de la radio solo en suspensión prolongada y consumo |
| `radio_csma.sim`   | Acceso al canal: escuchar antes de hablar, no destruir una trama sin leer al emitir, leer una trama sin abortar la siguiente, ACK con retardo aleatorio y reintentos con dispersión |
| `unread.sim`       | Mensajes no leídos: insignia, puntos de "nuevo", persistencia, leídos al abrir/salir de la lista, 9+, enviados y ACK/duplicados no cuentan |
| `sleep_alert.sim`  | Aviso con la pantalla apagada: vista previa 8 s, A lee / B cierra, no cuenta para el despertar, duplicados/ACK/balizas no avisan |
| `peers.sim`        | Varios equipos: tabla de compañeros (presencia por equipo, batería mínima, tabla llena, mismo msgId de emisores distintos, dedup de 16, reactivar reintentos) |
| `beacon_jitter.sim`| Balizas con jitter: primera a los 2-4 s y siguientes cada 27-33 s, no todas iguales |
| `peer_batt.sim`    | Batería del compañero junto al corazón de presencia (nivel, "!" de batería baja, oculta fuera de alcance) |
| `ui_fixes.sim`     | Cursor del menú principal (no desaparece al dormir/despertar ni tras las animaciones), hora de la barra de estado y etiquetas del menú de juegos |
| `ttt.sim`          | Tres en raya por LoRa: emparejamiento, roles, jugada y detección de fin |

Ejecutar uno con salida detallada:

```bat
out\walkie_sim.exe --fresh scripts\history.sim
```

## Lenguaje de scripts

Un comando por línea. Las líneas que empiezan por `#`, y todo lo que siga a ` # `
(almohadilla **precedida y seguida de espacio**), son comentarios. Un `#` pegado a un
texto forma parte del argumento: `expect serial ACK a #50` busca `ACK a #50`.

Un error en el propio guion (comando o comprobación desconocidos, argumentos que
faltan o no válidos, un mensaje inyectado demasiado largo para que el firmware
lo cifre, una captura que no se puede guardar) cuenta como `[FAIL]` e indica la
línea, para que una errata no deje pasar un test sin comprobar nada.

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
blind <ms>   el reloj avanza pero el firmware NO corre (como si estuviera ocupado en otra cosa)
ff <ms>      avance rápido (para timeouts largos, p.ej. el sueño de 5 min)
```

**LoRa**
```
lora rx <texto>     inyecta un mensaje entrante (lo cifra con la clave del firmware)
lora rxraw <hex>    inyecta bytes crudos
lora loopback on|off  reenvía lo transmitido como recibido
lora tx <texto>     el FIRMWARE emite ese mensaje por su capa de radio (escuchar antes de hablar, etc.)
lora sent           muestra el último paquete transmitido (y su descifrado)
in <ms> lora <texto>  /  in <ms> chatmsg <emisor> <msgId> <texto>  /  in <ms> chatack <destino> <msgId>
                    el mismo paquete, entregado dentro de <ms> (también durante esperas bloqueantes)
chatmsg <emisor> <msgId> <texto>   inyecta un MENSAJE de chat de un peer (con sobre)
chatack <destino> <msgId>          inyecta un ACK de un peer (confirmación de entrega)
presence <peerId> [epoch] [batt] [gen]   inyecta una BALIZA de presencia de un peer
                                   (gen = generación de ajuste de su hora; -1 = baliza antigua sin ese byte)
in <ms> presence <peerId> [epoch] [batt] [gen]   baliza diferida (durante esperas bloqueantes)
doodle <peerId> [msgId] [heart|frame]   inyecta un DIBUJO 24x24 de un peer: "heart" (por defecto, una masa
                                   en forma de corazón) o "frame" (marco + diagonal); msgId automático si falta
ttt hello <peerId>                 Tres en raya: inyecta el HELLO de emparejamiento de un peer
ttt state <peerId> <9digitos> <fin>   Tres en raya: inyecta un ESTADO del tablero (0/1/2 por casilla)
in <ms> doodle|ttt ...             variantes diferidas (durante el bucle bloqueante de un juego)
in <ms> expect|shot|print|radio ...   una COMPROBACIÓN, captura, nota o ajuste de radio diferido: se ejecuta en ese instante, también
                                   DENTRO de una espera bloqueante (un juego, una pantalla de resultado), donde
                                   un `expect` normal solo podría mirar al terminar el run
```

**Batería / hora / reinicio** (para las features de batería, presencia y reloj)
```
battery <0-100>        fija el nivel de batería simulado (canal ADC mockeado)
in <ms> battery <pct>  cambio de batería diferido (durante una espera bloqueante)
settime <epoch>        fija el reloj de pared (segundos epoch), lo persiste y sube la generación de ajuste
                       (equivale a "alguien puso la hora"; no reajusta las marcas del historial)
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
expect sentdoodle <msgId> <píxeles> [x y]   algún TX reciente es un dibujo con ese msgId y ese nº de píxeles
                       encendidos (y, si se dan, el píxel (x,y) encendido)
expect times <n> <sub>   el log serie contiene <sub> EXACTAMENTE <n> veces
expect beacon gen|batt|epoch|time <v>   la última BALIZA transmitida lleva ese valor (time = HH:MM)
expect panel on|off    el panel OLED está encendido/apagado (la suspensión lo apaga)
expect pixel <x> <y> on|off   estado de un píxel
waitfor serial <min> <max> <texto>   ejecuta el firmware hasta que <texto> aparezca en el log serie (solo
                       lo nuevo) y comprueba que lo hace entre <min> y <max> ms (ritmo de balizas, reintentos)
waitfor clear          olvida las esperas registradas;  expect spread <ms>: entre ellas, max-min >= <ms>
waitfor txend          ejecuta el firmware hasta que la radio termine de emitir (la emisión es asíncrona: tras
                       `waitfor serial ... [Chat] Baliza` la baliza aún está en el aire)
watch pixel <x> <y> on|off <ms>   ejecuta el firmware <ms> y comprueba el píxel tras CADA vuelta de loop()
                       (detecta parpadeos que un expect puntual no ve)
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
  --radio real|ideal  modelo de radio: real (por defecto) = chip SX1276 con tiempo en el aire (la
                    radio solo oye lo que llega mientras escucha, y transmitir bloquea); ideal =
                    siempre escucha y sin tiempo en el aire (comportamiento antiguo)
  --nodes <N>       modo sincronizado (Linux/macOS): los N procesos que comparten --air avanzan
                    juntos el mismo tiempo virtual (ver "Tiempo sincronizado entre procesos")
  --pace <x>        con --keys y sin --nodes: tope de velocidad del tiempo virtual (x ms virtuales
                    por ms real; 0 = sin tope). Con varios procesos en el aire compartido y sin
                    --nodes hace falta: sin tope cada uno corre su tiempo casi instantáneo y deja de
                    coincidir con los demás
```
Sin script y sin `--interactive`, lee comandos por la entrada estándar.
Una opción desconocida o sin su valor, o un guion de teclas que no existe, termina
con código 2. Las carpetas de `--shots` y de `--eeprom` se crean si no existen.
Para dar un id de equipo distinto a cada dispositivo, exporta `SIM_CHIPID` antes de
lanzarlo (`Device_id()` lo deriva de ahí en el simulador, y también siembra su aleatorio:
cada equipo sortea distinto, como la placa con su RNG por hardware). `SIM_SEED=<n>` cambia la secuencia aleatoria
SIN cambiar la identidad: sirve para ejecutar un test con muchas secuencias (retardos de ACK, dispersión de
reintentos y balizas) y descubrir los que solo pasan con una combinación afortunada de tiempos
(`for s in $(seq 1 50); do SIM_SEED=$s ./out/linux/walkie_sim --fresh scripts/x.sim; done`).

## Modelo de radio (chip SX1276 + librería real)

El firmware llama a la librería **LoRa real** (`vendor/lora`, arduino-LoRa 0.8.0, sin modificar) y por
debajo hay un **modelo del chip SX1276 a nivel de registros** (`engine/sx127x.cpp`) enganchado al SPI y a
los pines NSS/RST/DIO0. Reproduce lo que hace el silicio:

- **modos** SLEEP / STDBY / TX / RX continuo / RX único (con su caducidad de 100 símbolos ≈ 102 ms a SF7),
  banderas IRQ que se borran escribiendo 1, FIFO de 256 bytes;
- **transmitir tarda el tiempo en el aire real** (fórmula del datasheet con SF, BW, CR, preámbulo, CRC): 64 B a
  SF7 = 118 ms, 224 B = 348 ms, un ping a SF10 = 698 ms. `endPacket()` de la librería bloquea ese tiempo; el
  firmware emite en modo asíncrono (`endPacket(true)`) y sigue con su bucle. Mientras transmite **no escucha**
  (half-duplex);
- **recibir**: una trama solo se oye si el chip está en un modo de recepción cuando se detecta su preámbulo y
  sigue en él hasta que termina; en STDBY/SLEEP/TX se pierde, con otro SF/BW/frecuencia/sincronismo no se
  demodula, dos tramas solapadas se pierden, con CRC activo una trama corrupta levanta `PayloadCrcError` (sin
  CRC llega alterada) y una trama sin leer la pisa la siguiente;
- **estadísticas**: tiempo emitiendo, destino de cada trama recibida y consumo estimado del chip por modo.

Los comandos `lora rx`, `chatmsg`, `presence`... inyectan una trama que **empieza en ese instante** con los
parámetros del proyecto (`radio peer ...` los cambia). `--radio ideal`
da el comportamiento antiguo: la trama se entrega al instante y siempre, y transmitir no cuesta tiempo.

**Escribir tests con este modelo.** Una trama inyectada ocupa el canal lo que dura en el aire (un mensaje
corto ~120 ms; uno largo ~300 ms; un dibujo ~300 ms) y el equipo contesta con su ACK, que sale entre 10 y 300 ms
después de recibir y dura otros ~120 ms: dos inyecciones a la vez, o una que llegue mientras el equipo
emite su ACK, se pisan y no se oye ninguna, igual que con la radio real. Por eso los guiones separan los
mensajes (≥ 800 ms entre uno y el siguiente, con `in <ms> chatmsg|lora|presence ...` o con `run`) y, antes de inyectar
tras un `ff` o justo tras el arranque, esperan a la baliza propia y a que acabe de emitirse
(`waitfor serial 0 40000 [Chat] Baliza` + `waitfor txend`; justo después el canal queda libre unos 27 s).
Los equipos inyectados son **educados**: no empiezan a emitir mientras el firmware está emitiendo, sino que
esperan a que acabe (`radio peer polite off` lo desactiva). Queda la ventana física que ningún equipo evita:
si el firmware empieza a emitir en los ~3 ms (3 símbolos de preámbulo) que tarda en detectar una trama ajena,
las dos se pisan; es muy improbable y por eso conviene probar los tests con varias semillas (`SIM_SEED`).

```
radio ideal on|off     cambia de modelo durante un guion
radio peer sf|bw|crc|preamble|sync|freq <v>|polite on|off|reset   parámetros con que emiten los "otros equipos" inyectados
radio deepsleep <ms>   ahorro opcional (Lora_setDeepSleepMs): la radio se duerme tras <ms> sin oír nada en suspensión (0 = nunca, el valor por defecto)
radio mark             punto de partida de los contadores (expect rx / expect tx)
radio report           resumen: tramas emitidas y su tiempo en el aire, destino de las recibidas, consumo
radio log              destino de cada trama recibida desde la marca
radio selftest         autotest del chip con la librería real (ver scripts/radio_model.sim)
expect rx heard|lost|crc|standby|sleep|tx|timeout|aborted|collision|mismatch|overrun [=|>=|<=] <n>
expect tx <min> <max>             tramas emitidas desde la marca
expect txstart <min> <max>        la 1a emisión desde la marca empieza entre min y max ms después de ella
expect airtime <max%> <ventanaMs> ocupación del canal por este equipo en la última ventana
expect listening on|off           la radio está en recepción ahora mismo
expect radiomode sleep|stdby|tx|rxcont|rxsingle
```

## Fidelidad respecto al hardware real

| Aspecto | Simulador |
|---|---|
| Lógica del firmware | **idéntica** (compila `../src` sin cambios) |
| Render OLED | **idéntico** (Adafruit_GFX + fuente reales, 128×128, 1 bpp) |
| Cifrado de mensajes | **idéntico** (tiny-AES real, AES-128-CBC + PKCS7) |
| Historial / EEPROM | respaldado en fichero; persiste entre ejecuciones |
| Botones (antirrebote) | reloj virtual; el antirrebote se ejerce de verdad |
| Potenciómetro | valor 0–1023 controlable |
| LoRa (SX1276) | librería real + chip SX1276 simulado (ver "Modelo de radio"); **aire compartido real entre varios procesos** (`--air`) |
| Tiempos | reloj virtual determinista (no en tiempo real, salvo modo interactivo) |

Diferencias: no se simula el RF físico (ruido, alcance), ni el ruido del ADC, ni
la latencia exacta del bus I²C. Para eso haría falta el hardware real.

## Estructura

```
sim/
  arduino/    capa mock del core de Arduino (Arduino.h, Wire, SPI, EEPROM, LoRa,
              Adafruit_SH110X host sobre GFX, String, Print, pgmspace)
  vendor/     librerías reales vendorizadas (Adafruit_GFX, tiny-AES, arduino-LoRa)
  engine/     motor: reloj virtual, eventos de entrada, framebuffer, modelo del chip SX1276, driver/main
  scripts/    scripts de prueba (.sim)
  out/        artefactos de compilación (ignorado por git)
  build.bat / run.bat / run_tests.bat
  linux/      versión Linux (GNOME): build.sh, run.sh, run_tests.sh, net.sh,
              net_test.sh + capa de plataforma (termios). Salida en out/linux/
```

## Cómo añadir un test

1. Crea `scripts/mi_test.sim` con comandos + `expect ...`.
2. `run.bat scripts\mi_test.sim` para depurarlo (usa `screen`/`shot`/`text`).
3. Quedará incluido automáticamente en `run_tests.bat`.
