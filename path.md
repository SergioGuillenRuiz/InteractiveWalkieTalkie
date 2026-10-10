# Hoja de ruta del proyecto

Estado de las funcionalidades a fecha **2026-10-06** (commit `017463c`).

> Todo lo "terminado" está verificado **solo en el simulador**: desde junio no hay
> pruebas en la placa real. El simulador no modela dos cosas que en la placa pueden
> fallar: cómo escucha la radio y cuánto tarda cada paquete en el aire (ver Riesgos).

Leyenda: ✅ terminado · 🟡 a medias · ⚪ sin implementar · 🔴 riesgo sin verificar en la placa

---

## ✅ Terminado (funciona y tiene tests en el simulador)

| Área | Funcionalidad |
|---|---|
| Menú | Menú principal (Enviar / Historial / Juegos) con el hipopótamo animado y dormido por inactividad; el cursor se repinta en cada vuelta (no desaparece tras dormir/despertar ni tras las animaciones) y las etiquetas del menú de juegos no se cortan |
| Enviar | **Morse**, **Instant** (8 iconos), **Rueda** de letras (hasta 60 caracteres), **Frase** (3 categorías + piezas propias guardadas en EEPROM), **Dibujar** (lienzo 24×24) |
| Mensajería | Identificador por equipo, cifrado AES-128 con IV aleatorio, confirmación "Entregado", 3 reintentos, cola de pendientes persistente y descarte de duplicados |
| Historial | 10 mensajes guardados en EEPROM, enviados marcados con "Tu:", vista completa y borrado con confirmación. Los recibidos entran como **no leídos** (bit persistente): insignia con el recuento sobre el icono "Hist" (9+), punto de "nuevo" en la fila y se marcan como leídos al abrirlos o al salir de la lista si su fila llegó a verse |
| Aviso | Mensaje o dibujo nuevo: el hipopótamo persigue el corazón en el menú principal; con la pantalla apagada (suspensión) se enciende 8 s con vista previa (**A** = leer, abre el Historial; **B** = cerrar; sin pulsar se apaga sola) y esas pulsaciones no cuentan para el despertar de 3 pulsaciones |
| Hora | Se pone a mano (mantener **B** 1,5 s en el menú principal → "Poner la hora": horas / decenas / unidades de minuto con el pote), se muestra en el menú principal y en el Historial, se difunde y es **autoritativa** entre equipos (generación de ajuste en la baliza: la última hora puesta gana aunque se corrija hacia atrás), persiste en EEPROM y las antigüedades del historial no cambian al saltar el reloj. Es la hora local (sin zona horaria: al cambiar la hora de verano/invierno hay que volver a ponerla). No se puede por puerto serie: el SCL del OLED va al pin RX |
| Presencia | Baliza cada 30 s ±3 s (aleatoria, para que dos equipos encendidos a la vez no se tapen) y corazón en pantalla cuando hay algún compañero en alcance. Recuerda hasta 4 compañeros **por separado** (id, último oído, batería); a la izquierda del corazón se muestra el nivel de batería **más bajo** de los que están en alcance (y "!" si alguno avisa de batería baja), no el del último que emitió |
| Suspensión | A los 5 min: apaga la pantalla, WiFi apagado; se despierta con 3 pulsaciones |
| Juegos | Tetris Coop (contra CPU y 2 jugadores), Poker contra CPU, RefillGame, Choose4Me, HippoRadar y Tres en raya por LoRa. No queda ningún "Próximamente" accesible |
| Radio | La radio **escucha siempre** en recepción continua (menús, juegos, esperas bloqueantes y tras cada emisión), con vigilancia que la re-arma si el chip cae. La FIFO se lee por SPI sin salir de recepción (leer una trama no corta la siguiente) y una trama ya recibida no se pierde al emitir (cola de recepción). **Escuchar antes de hablar**: si entra una trama se espera a que acabe y a una pausa aleatoria. Los **ACK** salen tras un retardo aleatorio de 10-300 ms (varios equipos no se pisan) y los **reintentos** se reparten ±1,5 s. Las balizas esperan si el canal está ocupado. CRC activado en el paquete |
| Simulador | Windows (`sim/`) + Linux (`sim/linux/`), 37 tests, multi-dispositivo y demos. **Modelo de radio realista** (por defecto): ejecuta la librería LoRa real sobre un chip SX1276 simulado a nivel de registros (modos, IRQ, FIFO, tiempo en el aire real, transmitir bloquea, solo se oye lo que llega mientras se escucha, colisiones, CRC); autotest en `scripts/radio_model.sim`. **Varios equipos con el mismo tiempo** (`--nodes N`, Linux): los procesos avanzan juntos ms a ms, así las colisiones entre equipos son las reales y la prueba (`net_test.sh`, 5 escenas) es determinista y tarda ~2 s |

---

## 🟡 A medias

- [ ] **Batería.** En la placa, la lectura siempre devuelve 100 % (`src/Battery.cpp:13`), porque el único ADC del ESP8266 lo usa el potenciómetro. Hace falta hardware (y, por tanto, también el nivel que envía la baliza es siempre 100 %).
- [ ] **Dibujos.**
  - Solo se guarda el último recibido, y solo en RAM (`src/Doodle.cpp:119`). Tras reiniciar sale "(ya no guardado)".
  - Los dibujos enviados no aparecen en el historial.
  - No tienen confirmación ni reintentos, a diferencia del texto.
- [ ] **"Recibir en todo momento" no se cumple en tres modos.**
  - Tres en raya y Tetris 2 jugadores descartan cualquier paquete que no sea del juego (`src/TresEnRaya.cpp:61`, `src/TetrisCoop.cpp:349`).
  - HippoRadar guarda en el historial los paquetes en crudo (`src/HippoRadar.cpp:106`), así que confirmaciones y balizas aparecerían como texto basura.
  - Durante esas partidas no se envían balizas ni reintentos.

---

## ⚪ Sin implementar

- [ ] Medición real de la batería (necesita hardware).
- [ ] Emparejamiento y seguridad:
  - La clave AES está fija en el código y es la misma para todos los equipos.
  - No hay autenticación de mensajes (cualquiera con el firmware podría suplantar a otro equipo).
  - No hay protección contra reenvíos de paquetes antiguos.
- [ ] Dibujos guardados en EEPROM.
- [ ] Manual de usuario: el README solo tiene la lista de componentes y el simulador.
- [ ] Zumbador o vibración (opcional; no está en la lista de componentes).

---

## 🔴 Riesgos sin verificar en la placa

- [ ] **Radio: comportamiento verificado solo en el simulador.** El simulador ejecuta la librería real sobre un modelo del chip, pero el modelo se escribió a partir del datasheet: en la placa hay que confirmar que (a) se oyen los mensajes en el menú principal y en las esperas, (b) `Lora_busy()` (registro `RegModemStat`) detecta de verdad una trama entrante para escuchar antes de hablar, (c) leer la FIFO por SPI sin salir de recepción no corrompe la trama siguiente, y (d) el consumo en suspensión (light sleep del ESP8266, radio siempre encendida ≈ 11 mA) es el esperado.
- [ ] **Algunos modos ocupan la radio más del 100 % del tiempo** (SF7/125 kHz salvo que se indique; tiempo en el aire calculado):

  | Paquete | Tiempo en el aire | Se envía cada | Ocupación |
  |---|---|---|---|
  | Tetris 2J, estado del anfitrión | 302 ms | 280 ms | **108 %** |
  | Tetris 2J, controles del invitado | 118 ms | 200 ms | 59 % |
  | HippoRadar, ping (SF10) | 698 ms | ~340 ms | **205 %** |
  | Mensaje de chat máximo | 348 ms | puntual | — |

  - Es físicamente imposible. Además, la radio no puede escuchar mientras transmite, así que en la placa esos modos probablemente no funcionen.
  - En la UE, la banda de 868,0 MHz tiene un límite legal del 1 % de ocupación.
  - Los paquetes van en hexadecimal, lo que duplica su tamaño. Enviarlos en binario reduciría el tiempo a la mitad.

---

## ⏸ En espera de tu decisión

Nada de esto se ha cambiado hasta que lo decidas (el código está preparado en los dos casos):

- [ ] **Ahorro de energía de la radio en suspensión.** Hoy la radio **no se duerme nunca**: el equipo está siempre alcanzable y puede avisar con la pantalla apagada, a cambio de ~11 mA continuos. La alternativa está implementada y probada pero desactivada (`LORA_DEEP_SLEEP` en `include/Config.h`, o `Lora_setDeepSleepMs()` en marcha): tras N minutos sin oír nada en suspensión la radio se duerme del todo, el equipo dura más y deja de oír (y de avisar) hasta que lo despiertes con 3 pulsaciones. ¿Siempre alcanzable, dormir tras N minutos (¿cuántos?) o un ajuste en el menú?

---

## Orden propuesto

1. [ ] **Probar en la placa** la recepción en el menú principal, la suspensión y el consumo, y los dos modos de la tabla. El simulador ya modela la radio (`--radio real`) y reproduce estos riesgos sin placa; falta confirmarlos en la placa.
2. [ ] **Arreglar la radio** (la escucha continua, el acceso al canal y los ACK/reintentos ya están hechos):
   - enviar los paquetes en binario;
   - bajar el ritmo de Tetris 2J y HippoRadar a algo que quepa en el canal.
3. [ ] **Batería real** (hardware + calibración) y mostrar la del compañero.
4. [ ] **Recepción dentro de los juegos.**
5. [ ] **Dibujos:** persistentes, en el historial y con confirmación.
6. [ ] **Seguridad:** emparejamiento y clave por pareja.
7. [ ] **Pulido:** un manual de usuario.

---

## Limitaciones conocidas del simulador (no son tareas pendientes)

- Cada vuelta de `loop()` cuesta mucho menos que en la placa (no se cuenta el envío de cada imagen a la pantalla por I²C): las animaciones que avanzan por vuelta, como las "Zzz", van más rápido.
- `expect text` comprueba el texto dibujado desde el último borrado completo de la pantalla, aunque parte ya no se vea. Ningún test actual pasa por ese motivo.
- El modelo de radio realista enfrenta UN equipo a tramas inyectadas por el guion; con varios procesos (`--air`) hay colisiones reales entre equipos solo si se usa `--nodes N` (Linux/macOS, ver `sim/README.md`): sin él cada proceso lleva su propio reloj y el resultado depende del azar del arranque. En Windows `net_test.ps1` usa `--radio ideal` (sin tiempo en el aire). No se simula el RF físico (ruido, alcance, captura de la trama más fuerte) ni el tiempo real de los accesos SPI/I²C.
- `net_test.ps1` y `build.bat` (Windows) no se han probado desde que se añadió el modelo de radio realista: el entorno de desarrollo es Linux.
