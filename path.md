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
| Historial | 10 mensajes guardados en EEPROM, enviados marcados con "Tu:", vista completa y borrado con confirmación |
| Presencia | Baliza cada 30 s y corazón en pantalla cuando el compañero está en alcance |
| Suspensión | A los 5 min: apaga la pantalla, WiFi apagado; se despierta con 3 pulsaciones |
| Juegos | Tetris Coop (contra CPU y 2 jugadores), Poker contra CPU, RefillGame, Choose4Me, HippoRadar y Tres en raya por LoRa. No queda ningún "Próximamente" accesible |
| Simulador | Windows (`sim/`) + Linux (`sim/linux/`), 27 tests, multi-dispositivo y demos |

---

## 🟡 A medias

- [ ] **Hora compartida.** No hay forma de poner la hora en la placa: `Clock_set()` solo lo llama el simulador (comando `settime`). Los equipos arrancan en 00:00 y se sincronizan entre ellos, así que la hora de la barra de título es falsa. Tampoco hay zona horaria (`Clock_hhmm()` usa UTC).
- [ ] **Batería.**
  - En la placa, la lectura siempre devuelve 100 % (`src/Battery.cpp:13`), porque el único ADC del ESP8266 lo usa el potenciómetro. Hace falta hardware.
  - La batería del compañero llega en la baliza, pero `Chat_peerBatt()` no se usa en ninguna pantalla.
- [ ] **Dibujos.**
  - Solo se guarda el último recibido, y solo en RAM (`src/Doodle.cpp:119`). Tras reiniciar sale "(ya no guardado)".
  - Los dibujos enviados no aparecen en el historial.
  - No tienen confirmación ni reintentos, a diferencia del texto.
- [ ] **"Recibir en todo momento" no se cumple en tres modos.**
  - Tres en raya y Tetris 2 jugadores descartan cualquier paquete que no sea del juego (`src/TresEnRaya.cpp:61`, `src/TetrisCoop.cpp:349`).
  - HippoRadar guarda en el historial los paquetes en crudo (`src/HippoRadar.cpp:106`), así que confirmaciones y balizas aparecerían como texto basura.
  - Durante esas partidas no se envían balizas ni reintentos.
- [ ] **Aviso de mensaje nuevo.** Solo hay una animación si estás en el menú principal. En suspensión no se avisa, y al despertar no hay indicador de "no leído".
- [ ] **Varios equipos.** El firmware solo conoce un "compañero" (el último que oyó). Con 3 o más equipos, la presencia salta de uno a otro.

---

## ⚪ Sin implementar

- [ ] Ajuste de la hora (por menú o por puerto serie).
- [ ] Medición real de la batería (necesita hardware).
- [ ] Emparejamiento y seguridad:
  - La clave AES está fija en el código y es la misma para todos los equipos.
  - No hay autenticación de mensajes (cualquiera con el firmware podría suplantar a otro equipo).
  - No hay protección contra reenvíos de paquetes antiguos.
- [ ] Indicador de mensajes no leídos.
- [ ] Dibujos guardados en EEPROM.
- [ ] Manual de usuario: el README solo tiene la lista de componentes y el simulador.
- [ ] Zumbador o vibración (opcional; no está en la lista de componentes).

---

## 🔴 Riesgos sin verificar en la placa

- [ ] **La radio puede no escuchar en el menú principal.**
  - `handleIdle()` llama a `LoRa.idle()` en cada vuelta (`src/States.cpp:320`).
  - La librería LoRa solo recibe mientras está en modo "recepción única", que activa `parsePacket()`. Si cada vuelta la pasa a reposo, la radio estaría casi siempre sorda y podría perder mensajes. Deducido del código de la librería; no comprobado.
  - En el simulador `idle()` no hace nada, así que no lo detecta.
  - Por la misma razón, `LoRa.sleep()` en suspensión probablemente se deshace al instante, y el ahorro de energía (light sleep, despertar por GPIO, consumo) también está sin medir.
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

## Orden propuesto

1. [ ] **Probar en la placa** la recepción en el menú principal, la suspensión y el consumo, y los dos modos de la tabla. Opcional: que el simulador modele el tiempo en el aire para detectar esto sin placa.
2. [ ] **Arreglar la radio:**
   - escuchar de forma continua;
   - enviar los paquetes en binario;
   - bajar el ritmo de Tetris 2J y HippoRadar a algo que quepa en el canal.
3. [ ] **Ajuste de la hora** y la zona horaria.
4. [ ] **Batería real** (hardware + calibración) y mostrar la del compañero.
5. [ ] **Recepción dentro de los juegos** y el indicador de no leídos.
6. [ ] **Dibujos:** persistentes, en el historial y con confirmación.
7. [ ] **Seguridad:** emparejamiento y clave por pareja.
8. [ ] **Pulido:** un manual de usuario.

---

## Limitaciones conocidas del simulador (no son tareas pendientes)

- Cada vuelta de `loop()` cuesta mucho menos que en la placa (no se cuenta el envío de cada imagen a la pantalla por I²C): las animaciones que avanzan por vuelta, como las "Zzz", van más rápido.
- `expect text` comprueba el texto dibujado desde el último borrado completo de la pantalla, aunque parte ya no se vea. Ningún test actual pasa por ese motivo.
- No se modela el comportamiento real de la radio (modos de recepción, tiempo en el aire, colisiones).
