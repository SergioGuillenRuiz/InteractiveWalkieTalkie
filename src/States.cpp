#include <Arduino.h>
#include "States.h"
#include "Display.h"
#include "MyLora.h"
#include "Morse.h"
#include "Inputs.h"
#include "Historial.h"
#include "playChoose4Me.h"
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
        if (now - firstPressTime > 45000) {
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
    static bool firstDrawComplete = false;  // Nueva variable para controlar si ya se dibujó el título
    
    int sel = getPotValue(1);
    if (sel < 0) sel = 0;
    if (sel > 1) sel = 1;

    if (!firstDrawComplete || sel != lastSel) {
        // Si es la primera vez que entramos O cambió la selección
        
        if (firstDrawComplete && lastSel != -1) {
            // No es la primera vez: solo borrar las líneas de opciones
            display.fillRect(0, 16, 128, 32, SH110X_BLACK);
        } else {
            // Primera vez o reset: dibujar título completo
            Display_clear();
            display.setCursor(0,0);
            display.setTextSize(1);
            display.setTextColor(SH110X_WHITE);
            display.println("Selecciona modo:");
            display.println();
            firstDrawComplete = true;  // Marcar que ya dibujamos el título
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
                // Resetear para la próxima vez que entremos
                lastSel = -1;
                firstDrawComplete = false;
                return true;
            } else {
                sendSubState = SEND_INSTANT_MSG;
                Display_clear();
                menuTransitionDelay();
                // Resetear para la próxima vez que entremos
                lastSel = -1;
                firstDrawComplete = false;
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
            // Resetear para la próxima vez que entremos
            lastSel = -1;
            firstDrawComplete = false;
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
    static int topIndex = 0;
    static int selected = 0;
    static int prevTop = -1;
    static int prevSelected = -1;
    static bool forceRedraw = true;
    static bool firstTime = true;
    static bool justEntered = true;

    static int candidateSel = -1;
    static unsigned long candidateSince = 0;
    const unsigned long SEL_DEBOUNCE_MS = 150;

    const int LINES_PER_PAGE = 4;

    int total = History_count();

    if (total == 0) {
        if (prevTop != -2 || firstTime) {
            prevTop = -2;
            prevSelected = -2;
            firstTime = false;
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
                display.clearDisplay();
                justEntered = true;
                return true;
            }
        }
        return true;
    }

    if (justEntered) {
        justEntered = false;
        firstTime = true;
        forceRedraw = true;
        prevTop = -1;
        prevSelected = -1;
    }

    if (firstTime || forceRedraw) {
        firstTime = false;
        
        Display_clear();
        display.setCursor(0,0);
        display.setTextSize(1);
        display.setTextColor(SH110X_WHITE);
        display.println("Historial");
        
        for (int line = 0; line < LINES_PER_PAGE; ++line) {
            int idx = line;
            if (idx >= total) break;
            
            String msg = History_getMessage(idx);
            unsigned long ts = History_getTimestamp(idx);
            unsigned long ageMin = (ts == 0) ? 0 : ((millis() - ts) / 60000UL);
            String ageStr = String((unsigned long)ageMin) + "m";
            
            if (msg.length() > 18) {
                msg = msg.substring(0, 15) + "...";
            }
            
            String lineText = ((idx == selected) ? "> " : "  ") + ageStr + " " + msg;
            display.setCursor(0, 10 + (line * 10));
            display.println(lineText);
        }
        display.display();
        
        prevTop = 0;
        prevSelected = selected;
        forceRedraw = false;
        return true;
    }

    int mapped = getPotValue(max(0, total - 1));

    if (mapped != candidateSel) {
        candidateSel = mapped;
        candidateSince = millis();
    }
    if ((millis() - candidateSince) >= SEL_DEBOUNCE_MS) {
        if (candidateSel != selected) {
            selected = candidateSel;
            forceRedraw = true;
        }
    }

    if (selected < topIndex) {
        topIndex = selected;
        forceRedraw = true;
    }
    if (selected >= topIndex + LINES_PER_PAGE) {
        topIndex = selected - LINES_PER_PAGE + 1;
        forceRedraw = true;
    }

    if (isMorsePressed()) {
        delay(50);
        if (isMorsePressed()) {
            unsigned long buttonHoldStart = millis();
            while (isMorsePressed()) {
                if (millis() - buttonHoldStart > 1000) {
                    menuTransitionDelay();
                    return true;
                }
                delay(10);
            }
            delay(150);
            
            Display_clear();
            display.setCursor(0, 0);
            display.setTextSize(1);
            display.setTextColor(SH110X_WHITE);
            display.println("Borrar mensaje?");
            display.println();
            
            String msgToDelete = History_getMessage(selected);
            if (msgToDelete.length() > 20) {
                msgToDelete = msgToDelete.substring(0, 17) + "...";
            }
            display.println(msgToDelete);
            display.println();
            display.println();
            display.println("A: SI  B: NO");
            display.display();
            
            while (Serial.available()) Serial.read();
            delay(100);
            
            bool confirmed = false;
            bool cancelled = false;
            unsigned long confirmStart = millis();
            
            static bool morseAlreadyProcessed = false;
            static bool finishAlreadyProcessed = false;
            morseAlreadyProcessed = false;
            finishAlreadyProcessed = false;
            
            while (!confirmed && !cancelled && (millis() - confirmStart < 5000)) {
                if (isMorsePressed() && !morseAlreadyProcessed) {
                    delay(80);
                    if (isMorsePressed()) {
                        while (isMorsePressed()) { delay(10); }
                        delay(80);
                        confirmed = true;
                        morseAlreadyProcessed = true;
                        break;
                    }
                }
                
                if (isFinishPressed() && !finishAlreadyProcessed) {
                    delay(80);
                    if (isFinishPressed()) {
                        while (isFinishPressed()) { delay(10); }
                        delay(80);
                        cancelled = true;
                        finishAlreadyProcessed = true;
                        break;
                    }
                }
                
                delay(20);
            }
            
            if (confirmed) {
                History_deleteMessage(selected);
                
                Display_clear();
                display.setCursor(0, 0);
                display.setTextSize(1);
                display.setTextColor(SH110X_WHITE);
                display.println("Mensaje borrado");
                display.display();
                delay(1200);
                
                total = History_count();
                
                if (selected >= total && total > 0) {
                    selected = total - 1;
                }
                if (selected < 0 && total > 0) {
                    selected = 0;
                }
                
                prevTop = -1;
                prevSelected = -1;
                forceRedraw = true;
                firstTime = true;
                justEntered = true;
                
                while (Serial.available()) Serial.read();
                delay(50);
                
                menuTransitionDelay();
                return true;
            }
            
            if (cancelled) {
                Display_clear();
                display.setCursor(0, 0);
                display.println("Cancelado");
                display.display();
                delay(500);
            }
            
            prevTop = -1;
            prevSelected = -1;
            forceRedraw = true;
            firstTime = true;
            justEntered = true;
            
            menuTransitionDelay();
            return true;
        }
    }

    bool needRedraw = forceRedraw || (topIndex != prevTop) || (selected != prevSelected);
    
    static unsigned long lastMinuteCheck = 0;
    static int cachedMinutes[LINES_PER_PAGE] = { -1, -1, -1, -1 };
    
    if (millis() - lastMinuteCheck > 500) {
        lastMinuteCheck = millis();
        
        for (int line = 0; line < LINES_PER_PAGE; ++line) {
            int idx = topIndex + line;
            if (idx >= total) {
                if (cachedMinutes[line] != -1) {
                    cachedMinutes[line] = -1;
                    needRedraw = true;
                }
                continue;
            }
            
            unsigned long ts = History_getTimestamp(idx);
            unsigned long ageMin = (ts == 0) ? 0 : ((millis() - ts) / 60000UL);
            int ageMinInt = (int)ageMin;
            
            if (cachedMinutes[line] != ageMinInt) {
                cachedMinutes[line] = ageMinInt;
                needRedraw = true;
            }
        }
    }

    if (needRedraw) {
        display.fillRect(0, 0, 128, 32, SH110X_BLACK);
        display.fillRect(0, 8, 128, 120, SH110X_BLACK);
        
        display.setCursor(0,0);
        display.setTextSize(1);
        display.setTextColor(SH110X_WHITE);
        display.println("Historial");
        
        for (int line = 0; line < LINES_PER_PAGE; ++line) {
            int idx = topIndex + line;
            if (idx >= total) {
                cachedMinutes[line] = -1;
                break;
            }

            String msg = History_getMessage(idx);
            unsigned long ts = History_getTimestamp(idx);
            unsigned long ageMin = (ts == 0) ? 0 : ((millis() - ts) / 60000UL);
            String ageStr = String((unsigned long)ageMin) + "m";

            if (msg.length() > 18) {
                msg = msg.substring(0, 15) + "...";
            }
            
            String lineText = ((idx == selected) ? "> " : "  ") + ageStr + " " + msg;
            display.setCursor(0, 10 + (line * 10));
            display.println(lineText);

            cachedMinutes[line] = (int)ageMin;
        }
        
        display.display();
        
        prevTop = topIndex;
        prevSelected = selected;
        forceRedraw = false;
    }

    if (isFinishPressed()) {
        delay(80);
        if (isFinishPressed()) {
            while (isFinishPressed()) { delay(10); }
            delay(50);
            
            mainState = STATE_IDLE;
            menuTransitionDelay();
            display.clearDisplay();
            justEntered = true;
            return true;
        }
    }

    return true;
}
// ==================== STATE_GAMES_MENU ====================
bool handleGamesMenu() {
    // Muestra el submenú de juegos (iconos 4x2) y permite seleccionar con el potenciómetro A0.
    drawGamesMenu();

    // Si se pulsa MORSE -> "arrancar" juego
    if (isMorsePressed()) {
        delay(50);
        if (isMorsePressed()) {
            int seleccion = getPotValue(5);
            if (seleccion < 0) seleccion = 0;
            if (seleccion > 4) seleccion = 4;

            String name = String(nombresJuegos[seleccion]);
            
            // Arrancar el juego según la selección
            switch (seleccion) {
                case 0: // Tetris2v2
                    Display_clear();
                    display.setCursor(0,20);
                    display.setTextSize(1);
                    display.setTextColor(SH110X_WHITE);
                    display.println("Proximamente:");
                    display.println(name);
                    display.display();
                    delay(800);
                    break;
                    
                case 1: // Poker
                    Display_clear();
                    display.setCursor(0,20);
                    display.setTextSize(1);
                    display.setTextColor(SH110X_WHITE);
                    display.println("Proximamente:");
                    display.println(name);
                    display.display();
                    delay(800);
                    break;
                    
                case 2: // RefillGame
                    Display_clear();
                    display.setCursor(0,20);
                    display.setTextSize(1);
                    display.setTextColor(SH110X_WHITE);
                    display.println("Proximamente:");
                    display.println(name);
                    display.display();
                    delay(800);
                    break;
                    
                case 3: // Choose4Me - ¡ESTE SÍ ESTÁ IMPLEMENTADO!
                    // Arrancar el juego Choose4Me
                    startChoose4Me();
                    // startChoose4Me() maneja su propia salida, así que retornamos
                    return true;
                    
                case 4: // HippoRadar
                    Display_clear();
                    display.setCursor(0,20);
                    display.setTextSize(1);
                    display.setTextColor(SH110X_WHITE);
                    display.println("Proximamente:");
                    display.println(name);
                    display.display();
                    delay(800);
                    break;
                    
                default:
                    break;
            }
            
            // Para juegos no implementados (todos excepto Choose4Me), mostrar mensaje
            if (seleccion != 3) {
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