#include <Arduino.h>
#include "States.h"
#include "Display.h"
#include "MyLora.h"
#include "Morse.h"
#include "Inputs.h"
#include "Historial.h"
#include <LoRa.h>

//=============================================================
// INICIALIZACIÓN DE VARIABLES
//=============================================================

unsigned long lastInteraction = 0;
unsigned long lastTimeReceived = 0;
int cursorPos = 0;
int buttonPressCount = 0;
unsigned long firstPressTime = 0;

MainState mainState = STATE_IDLE;
SendSubState sendSubState = SEND_WAIT;

//=============================================================
// FUNCIONES DE GESTIÓN DE ESTADOS  
//=============================================================

// ==================== STATE_IDLE ====================
bool handleIdle() {

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
            return true;
        }
    }

    if (millis() - lastInteraction > 300000) {
        mainState = STATE_SLEEP;
        Display_clear();
        LoRa.idle();
        menuTransitionDelay();
        return true;
    }

    return false; 
}

// ==================== STATE_SLEEP ====================
bool handleSleep() {

    if (millis() - lastTimeReceived > 900000) {
        LoRa.sleep();
    }

    if (isMorsePressed() || isFinishPressed()) {
        unsigned long now = millis();
        if (buttonPressCount == 0) {
            firstPressTime = now;
            buttonPressCount = 1;
            return true;
        }
        if (now - firstPressTime > 5000) {
            buttonPressCount = 0;
            return true;
        }
        buttonPressCount++;
        if (buttonPressCount >= 3) {
            buttonPressCount = 0;
            Display_clear();
            mainState = STATE_IDLE;
            lastInteraction = millis();
            menuTransitionDelay();
            return true;
        }
    }

    return true; 
}

// ==================== STATE_SEND_MENU ====================
bool handleSendMenu() {

    switch (sendSubState) {

        // ---------------------------------------------------
        // ---------------------- SEND_WAIT ------------------
        // ---------------------------------------------------
        case SEND_WAIT:
        {
            static int lastSel = -1;
            int sel = map(analogRead(A0), 0, 1023, 0, 1);
            if (sel < 0) sel = 0;
            if (sel > 1) sel = 1;

            if (sel != lastSel) {
                lastSel = sel;
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
                        return true;
                    } else {
                        sendSubState = SEND_INSTANT_MSG;
                        Display_clear();
                        menuTransitionDelay();
                        return true;
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
                    return true;
                }
            }

            return true;
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
                        return true;
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
                        return true;
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
                        return true;
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
                        return true;
                    }
                }
            }

            return true;
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
                        return true;
                    } else { // isFinishPressed()
                        menuTransitionDelay();
                        // volver al menú principal
                        sendSubState = SEND_WAIT;
                        mainState = STATE_IDLE;
                        Display_clear();
                        return true;
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
                    return true;
                }
            }

            return true;
        }
    }

    return true;
}