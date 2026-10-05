

                           BILL OF MATERIALS 


 REF    | COMPONENTES      | ESPECIFICACIONES           | QTY 
--------|------------------|----------------------------|------------
 ES     | Microcontrolador | ESP8266 (Generic)          | 1
 LO     | LoRa Module      | SX1276 (868 MHz)           | 1
 OLD    | OLED Display     | Monochromatic I2C (4-pin)  | 1
 PR     | Push Buttons     | Generic Tactile            | 2
 PTR    | Potentiometer    | Generic Analog             | 1
 PWR    | Power Source     | 3.2V LiPo Battery          | 1



 ![Vista previa de la carcasa](planos/CarcasaPreview.png)

* Nota: El proyecto utiliza placas genéricas para el microcontrolador y la pantalla,
  usar otra placa diferente no debería suponer un problema, en caso de error,
  consulta el manual y especificaciones de la placa, probablemente se deberá a una distinta
  distribución de pines.


---

## Simulador y pruebas

El directorio [`sim/`](sim/) contiene un **simulador nativo de PC** que compila y
ejecuta el firmware real (`src/`) sin necesidad de hardware: renderiza la pantalla
OLED 128×128, simula botones, potenciómetro y radio LoRa (con cifrado AES real), y
permite tanto un **modo interactivo por teclado** como **tests automatizados**.

```bat
cd sim
run_tests.bat            :: compila y ejecuta toda la batería de tests
run.bat                  :: modo interactivo (teclado)
```

Requiere *Visual Studio Build Tools* (C++). Más detalles en [`sim/README.md`](sim/README.md).

En **Linux (GNOME)** hay una versión equivalente y separada en [`sim/linux/`](sim/linux/):

```bash
cd sim/linux
./run_tests.sh           # compila y ejecuta toda la batería de tests
./run.sh                 # modo interactivo (abre una ventana de GNOME Terminal)
```

Requiere `g++`/`gcc`. Más detalles en [`sim/linux/README.md`](sim/linux/README.md).

