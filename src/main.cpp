#include <Arduino.h>
#include "Config.h"
#include "Display.h"
#include "MyLora.h"
#include "Morse.h"
#include "Inputs.h"
#include "Historial.h"

/// ============================================================
/// TESTEO ELMINAR DESPUÉS Y ENCAPSULAR EN OTRO LADO
/// ============================================================
#include <LoRa.h>

extern Adafruit_SH1107 display;
extern const char* nombresMensajes[];

MainState mainState = STATE_IDLE;
SendSubState sendSubState = SEND_WAIT;


// ============================================================
// VARIABLES DE CONTROL
// ============================================================

unsigned long lastInteraction = 0;        
unsigned long lastTimeReceived = 0;           
int cursorPos = 0;                        
int buttonPressCount = 0;                 
unsigned long firstPressTime = 0;         

// ============================================================
// setup
// ============================================================
void setup() {

  Serial.begin(115200);

  setupPins();

  Display_begin();

  lastInteraction = millis();

  Lora_begin();

}

// ============================================================
// maquina de estados principal
// ============================================================
void loop() {

  Lora_update();
  Display_update();
  
  // ==================== STATE_IDLE ====================
    if (mainState == STATE_IDLE) {

        LoRa.idle();
        drawMenu();
        moveCursor();

        if (Lora_hasMessage()) {
            String msg = Lora_readMessage();
            History_addMessage(msg);
            lastTimeReceived = millis();
        }

        if (isMorsePressed()) {
            delay(50);
            if (isMorsePressed()) {
                if (cursorPos == 0) mainState = STATE_SEND_MENU;
                if (cursorPos == 1) mainState = STATE_HISTORY_MENU;
                if (cursorPos == 2) mainState = STATE_GAMES_MENU;
                lastInteraction = millis();
                Display_clear();
                menuTransitionDelay();
                return;
            }
        }

        if (millis() - lastInteraction > 300000) {
            mainState = STATE_SLEEP;
            Display_clear();
            LoRa.idle();
            menuTransitionDelay();
            return;
        }
    }

    // ==================== STATE_SLEEP ====================
    if (mainState == STATE_SLEEP) {

        if (millis() - lastTimeReceived > 900000) {
            LoRa.sleep();
        }

        if (isMorsePressed() || isFinishPressed()) {
            unsigned long now = millis();
            if (buttonPressCount == 0) {
                firstPressTime = now;
                buttonPressCount = 1;
                return;
            }
            if (now - firstPressTime > 5000) {
                buttonPressCount = 0;
                return;
            }
            buttonPressCount++;
            if (buttonPressCount >= 3) {
                buttonPressCount = 0;
                Display_clear();
                mainState = STATE_IDLE;
                lastInteraction = millis();
                menuTransitionDelay();
                return;
            }
        }

        return;
    }

    // ==================== STATE_HISTORY_MENU ====================
    if (mainState == STATE_HISTORY_MENU) {

        Display_centerText("HISTORY MENU");

        if (isFinishPressed()) {
            delay(50);
            if (isFinishPressed()) {
                mainState = STATE_IDLE;
                menuTransitionDelay();
                return;
            }
        }

        return;
    }

    // ==================== STATE_GAMES_MENU ====================
    if (mainState == STATE_GAMES_MENU) {

        Display_centerText("GAMES MENU");
        
        if (isFinishPressed()) {
            delay(50);
            if (isFinishPressed()) {
                mainState = STATE_IDLE;
                menuTransitionDelay();
                return;
            }
        }

        return;
    }

    // ==================== STATE_SEND_MENU ====================
    if(mainState == STATE_SEND_MENU) {

    switch (sendSubState) {

        // ---------------------------------------------------
        // ---------------------- SEND_WAIT ------------------
        // ---------------------------------------------------
        case SEND_WAIT:
        {
            static int lastSel = -1;
            static unsigned long lastDraw = 0;
            int sel = map(analogRead(A0), 0, 1023, 0, 1);
            if (sel < 0) sel = 0;
            if (sel > 1) sel = 1;

            // Redibujar solo cuando cambia la selección o cada 500ms (por si hay animaciones)
            if (sel != lastSel) {
                lastSel = sel;
                lastDraw = millis();
                Display_clear();
                display.setCursor(0,0);
                display.setTextSize(1);
                display.setTextColor(SH110X_WHITE);
                display.println("Selecciona modo:");
                display.println();
                display.print((sel==0) ? "> " : "  ");
                display.println("Morse");
                display.print((sel==1) ? "> " : "  ");
                display.println("Instant");
                display.display();
            }

            if (isMorsePressed()) {
                delay(50);
                if (isMorsePressed()) {
                    if (sel == 0) {
                        dentroMenuEnviar = true;
                        primeraVezMenu = true;
                        sendSubState = SEND_MORSE;
                        Display_clear();
                        menuTransitionDelay();
                        return;
                    } else {
                        sendSubState = SEND_INSTANT_MSG;
                        Display_clear();
                        menuTransitionDelay();
                        return;
                    }
                }
            }

            if (isFinishPressed()) {
                delay(50);
                if (isFinishPressed()) {
                    sendSubState = SEND_WAIT;
                    mainState = STATE_IDLE;
                    Display_clear();
                    menuTransitionDelay();
                    return;
                }
            }

            return;
        }

        // ---------------------------------------------------
        // ---------------------- SEND_MORSE -----------------
        // ---------------------------------------------------

        case SEND_MORSE:
        {
            dentroMenuEnviar = true;
            primeraVezMenu = false;
            mensajeCancelado = false;
            mensajeEnviado = false;

            {
                MorseResult res = createMorseMessage();
                if (res == MORSE_SENT) {
                    if (Lora_send(mensajeAEnviar)) {
                        Display_clear();
                        display.setCursor(0,0);
                        display.setTextSize(1);
                        display.setTextColor(SH110X_WHITE);
                        display.println("Enviado:");
                        display.println(mensajeAEnviar);
                        display.display();
                    } else {
                        Display_clear();
                        display.setCursor(0,0);
                        display.setTextSize(1);
                        display.setTextColor(SH110X_WHITE);
                        display.println("Error al enviar");
                        display.display();
                    }

                    // Esperar hasta que el usuario pulse un botón:
                    while (!isMorsePressed() && !isFinishPressed()) {
                        Lora_update();
                        Display_update();
                        yield();
                        delay(10);
                    }

                    if (isMorsePressed()) {
                        menuTransitionDelay(); // consumir/release
                        // volver a creación morse
                        mensajeAEnviar = "";
                        morseCode = "";
                        morsePrefix = "";
                        lastMorsePrefix = "";
                        morseScrollOffset = 0;
                        dentroMenuEnviar = true;
                        primeraVezMenu = true;
                        sendSubState = SEND_MORSE;
                        Display_clear();
                        return;
                    } else { // isFinishPressed()
                        menuTransitionDelay();
                        // volver al menú principal
                        dentroMenuEnviar = false;
                        mensajeAEnviar = "";
                        morseCode = "";
                        morsePrefix = "";
                        lastMorsePrefix = "";
                        morseScrollOffset = 0;
                        sendSubState = SEND_WAIT;
                        mainState = STATE_IDLE;
                        Display_clear();
                        return;
                    }
                }
                if (res == MORSE_CANCELLED) {
                    Display_clear();
                    display.setCursor(0,0);
                    display.setTextSize(1);
                    display.setTextColor(SH110X_WHITE);
                    display.println("Cancelado");
                    display.display();

                    // Esperar hasta que el usuario pulse un botón:
                    while (!isMorsePressed() && !isFinishPressed()) {
                        Lora_update();
                        Display_update();
                        yield();
                        delay(10);
                    }

                    if (isMorsePressed()) {
                        menuTransitionDelay(); // consumir/release
                        // volver a creación morse
                        mensajeAEnviar = "";
                        morseCode = "";
                        morsePrefix = "";
                        lastMorsePrefix = "";
                        morseScrollOffset = 0;
                        dentroMenuEnviar = true;
                        primeraVezMenu = true;
                        sendSubState = SEND_MORSE;
                        Display_clear();
                        return;
                    } else { // isFinishPressed()
                        menuTransitionDelay();
                        // volver al menú principal
                        dentroMenuEnviar = false;
                        mensajeAEnviar = "";
                        morseCode = "";
                        morsePrefix = "";
                        lastMorsePrefix = "";
                        morseScrollOffset = 0;
                        sendSubState = SEND_WAIT;
                        mainState = STATE_IDLE;
                        Display_clear();
                        return;
                    }
                }
            }

            return;
        }

        // ---------------------------------------------------
        // ------------------ SEND_INSTANT_MSG ---------------
        // ---------------------------------------------------
       case SEND_INSTANT_MSG:
        {

            drawInstantMessagesMenu();

            if (isMorsePressed()) {
                delay(50);
                if (isMorsePressed()) {
                    int seleccion = map(analogRead(A0), 0, 1023, 0, 7);
                    if (seleccion < 0) seleccion = 0;
                    if (seleccion > 7) seleccion = 7;

                    String mensaje = nombresMensajes[seleccion];

                    if (Lora_send(mensaje)) {
                        Display_clear();
                        display.setCursor(0,0);
                        display.setTextSize(1);
                        display.setTextColor(SH110X_WHITE);
                        display.println("Enviado:");
                        display.println(mensaje);
                        display.display();
                    } else {
                        Display_clear();
                        display.setCursor(0,0);
                        display.setTextSize(1);
                        display.setTextColor(SH110X_WHITE);
                        display.println("Error al enviar");
                        display.display();
                    }

                    // Esperar hasta que el usuario pulse un botón:
                    while (!isMorsePressed() && !isFinishPressed()) {
                        Lora_update();
                        Display_update();
                        yield();
                        delay(10);
                    }

                    if (isMorsePressed()) {
                        menuTransitionDelay();
                        // volver al submenú SEND_WAIT (seguir en enviar)
                        sendSubState = SEND_WAIT;
                        Display_clear();
                        return;
                    } else { // isFinishPressed()
                        menuTransitionDelay();
                        // volver al menú principal
                        sendSubState = SEND_WAIT;
                        mainState = STATE_IDLE;
                        Display_clear();
                        return;
                    }
                }
            }

            if (isFinishPressed()) {
                delay(50);
                if (isFinishPressed()) {
                    sendSubState = SEND_WAIT;
                    mainState = STATE_IDLE;
                    Display_clear();
                    menuTransitionDelay();
                    return;
                }
            }

            return;
        }
    }
}
}
