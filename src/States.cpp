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
    
    // Gestionar animaciones (se ejecuta después del menú y cursor)
    updateHippoAnimation();

    if (Lora_hasMessage()) {
        String msg = Lora_readMessage();
        History_addMessage(msg);
        lastTimeReceived = millis();
        // Disparar animación cuando se recibe mensaje
        triggerAnimation(ANIM_CHASING_HEART);
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
    int sel = getPotValue(1);
    if (sel < 0) sel = 0;
    if (sel > 1) sel = 1;

    if (sel != lastSel) {
        // SOLO redibujar las líneas que cambian, no toda la pantalla
        if (lastSel != -1) {
            // Borrar solo las dos líneas de opciones (no toda la pantalla)
            display.fillRect(0, 16, 128, 32, SH110X_BLACK);
        } else {
            // Primera vez: dibujar título completo
            Display_clear();
            display.setCursor(0,0);
            display.setTextSize(1);
            display.setTextColor(SH110X_WHITE);
            display.println("Selecciona modo:");
            display.println();
        }
        
        // Dibujar las dos opciones
        display.setCursor(0, 16);
        display.print((sel==0) ? "> " : "  ");
        display.println("Morse");
        
        display.setCursor(0, 26);
        display.print((sel==1) ? "> " : "  ");
        display.println("Instant");
        
        display.display();
        lastSel = sel;
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
                        triggerAnimation(ANIM_GIVING_HEART);
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
                    int seleccion = getPotValue(7);
                    if (seleccion < 0) seleccion = 0;
                    if (seleccion > 7) seleccion = 7;

                    String mensaje = nombresMensajes[seleccion];

                    if (Lora_send(mensaje)) {
                        triggerAnimation(ANIM_GIVING_HEART);
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

// ==================== STATE_HISTORY_MENU ====================
bool handleHistoryMenu() {
    static int topIndex = 0;          // índice del primer elemento a mostrar (0 = más reciente)
    static int selected = 0;          // índice seleccionado (0 = más reciente)
    static int prevTop = -1;
    static int prevSelected = -1;

    // Debounce / estabilidad para la entrada analógica
    static int candidateSel = -1;
    static unsigned long candidateSince = 0;
    const unsigned long SEL_DEBOUNCE_MS = 150;

    const int LINES_PER_PAGE = 4;

    int total = History_count();

    // Si no hay mensajes, mostrar aviso (sin test messages)
    if (total == 0) {
        if (prevTop != -2) {
            prevTop = -2; prevSelected = -2;
            Display_clear();
            display.setCursor(0,0);
            display.setTextSize(1);
            display.setTextColor(SH110X_WHITE);
            display.println("Historial");
            display.println();
            display.println("Sin mensajes");
            display.display();
        }

        if (isFinishPressed()) {
            delay(50);
            if (isFinishPressed()) {
                mainState = STATE_IDLE;
                menuTransitionDelay();
                return true;
            }
        }
        return true;
    }

    // Leer y mapear la entrada analógica a un índice
    int mapped = getPotValue(max(0, total - 1));

    // Debounce: aceptar el nuevo valor sólo si se mantiene estable
    if (mapped != candidateSel) {
        candidateSel = mapped;
        candidateSince = millis();
    }
    if ((millis() - candidateSince) >= SEL_DEBOUNCE_MS) {
        selected = candidateSel;
    }

    // Asegurar visibilidad en ventana
    if (selected < topIndex) topIndex = selected;
    if (selected >= topIndex + LINES_PER_PAGE) topIndex = selected - LINES_PER_PAGE + 1;

    // Decide si hay que redibujar:
    bool needRedraw = (topIndex != prevTop) || (selected != prevSelected);

    // Mantener cache de minutos de las líneas visibles para evitar redraw por segundos
    static int prevMinutes[LINES_PER_PAGE] = { -1, -1, -1, -1 };

    // Comprobar si alguno de los minutos visibles ha cambiado
    for (int line = 0; line < LINES_PER_PAGE; ++line) {
        int idx = topIndex + line;
        if (idx >= total) {
            if (prevMinutes[line] != -1) { prevMinutes[line] = -1; needRedraw = true; }
            continue;
        }
        unsigned long ts = History_getTimestamp(idx);
        unsigned long ageMin = (ts == 0) ? 0 : ((millis() - ts) / 60000UL);
        int ageMinInt = (int)ageMin;
        if (prevMinutes[line] != ageMinInt) {
            needRedraw = true;
            // no break: queremos actualizar prevMinutes de todas las líneas al redibujar
        }
    }

    if (needRedraw) {
        prevTop = topIndex;
        prevSelected = selected;

        Display_clear();
        display.setCursor(0,0);
        display.setTextSize(1);
        display.setTextColor(SH110X_WHITE);

        for (int line = 0; line < LINES_PER_PAGE; ++line) {
            int idx = topIndex + line;
            if (idx >= total) {
                prevMinutes[line] = -1;
                break;
            }

            String msg = History_getMessage(idx);
            unsigned long ts = History_getTimestamp(idx);
            unsigned long ageMin = (ts == 0) ? 0 : ((millis() - ts) / 60000UL);

            // Mostrar solo minutos: "0m", "1m", "23m", ...
            String ageStr = String((unsigned long)ageMin) + "m";

            String lineText = ((idx == selected) ? "> " : "  ") + ageStr + " " + msg;
            display.println(lineText);

            prevMinutes[line] = (int)ageMin;
        }
        display.display();
    }

    // Salir del menú
    if (isFinishPressed()) {
        delay(50);
        if (isFinishPressed()) {
            mainState = STATE_IDLE;
            menuTransitionDelay();
            display.clearDisplay();
            return true;
        }
    }

    return true;
}

// ==================== STATE_GAMES_MENU ====================
bool handleGamesMenu() {
    // Muestra el submenú de juegos (iconos 4x2) y permite seleccionar con el potenciómetro A0.
    drawGamesMenu();

    // Si se pulsa MORSE -> "arrancar" juego (placeholder)
    if (isMorsePressed()) {
        delay(50);
        if (isMorsePressed()) {
            int seleccion = getPotValue(7);
            if (seleccion < 0) seleccion = 0;
            if (seleccion > 7) seleccion = 7;

            // Mostrar pantalla de arranque / placeholder
            String name = String(nombresJuegos[seleccion]);
            Display_clear();
            display.setCursor(0,20);
            display.setTextSize(1);
            display.setTextColor(SH110X_WHITE);
            display.println("Arrancando:");
            display.println(name);
            display.display();

            // Simular tentativa de arranque y mostrar que no está implementado aún
            delay(800);
            Display_clear();
            display.setCursor(0,20);
            display.setTextSize(1);
            display.setTextColor(SH110X_WHITE);
            display.println("Proximamente");
            display.display();

            // Esperar hasta que el usuario pulse un botón; MORSE -> volver al submenú, FINISH -> salir a idle
            while (!isMorsePressed() && !isFinishPressed()) {
                Lora_update();
                Display_update();
                yield();
                delay(10);
            }

            if (isMorsePressed()) {
                menuTransitionDelay();
                Display_clear();
                return true; // volver a STATE_GAMES_MENU (se redibujará)
            } else { // isFinishPressed()
                menuTransitionDelay();
                mainState = STATE_IDLE;
                Display_clear();
                return true;
            }
        }
    }

    // Si se pulsa FINISH -> volver al menú principal
    if (isFinishPressed()) {
        delay(50);
        if (isFinishPressed()) {
            mainState = STATE_IDLE;
            menuTransitionDelay();
            Display_clear();
            return true;
        }
    }

    return true;
}