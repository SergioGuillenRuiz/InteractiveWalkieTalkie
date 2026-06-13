#include <Arduino.h>
#include "States.h"
#include "Display.h"
#include "MyLora.h"
#include "Morse.h"
#include "Inputs.h"
#include "Historial.h"
#include "Chat.h"
#include "Identity.h"
#include "playChoose4Me.h"
#include "Poker.h"
#include "RefillGame.h"
#include "TetrisCoop.h"
#include "HippoRadar.h"
#include <LoRa.h>

#if defined(ESP8266)
extern "C" {
  #include "user_interface.h"   // wifi_fpm_* (light sleep)
  #include "gpio.h"             // gpio_pin_wakeup_enable
}
#endif

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
// SERVICIO DE FONDO
//=============================================================

// Atiende la radio y refresca la pantalla. Es el ÚNICO punto donde se reciben
// mensajes, así que basta con llamarlo desde el bucle principal y desde
// cualquier espera bloqueante (menús, juegos...) para poder recibir en todo
// momento, esté donde esté el usuario.
void backgroundTick() {
    Lora_update();

    if (Lora_hasMessage()) {
        String raw = Lora_readMessage();
        String text; uint8_t sender = 0, msgId = 0;
        ChatKind kind = Chat_parse(raw, text, sender, msgId);

        if (kind == CHAT_ACK) {
            // Confirmacion de entrega de un mensaje que enviamos: no es un mensaje
            // nuevo, solo actualiza el estado de "Entregado".
            Chat_noteAck(sender, msgId);
        } else if (kind == CHAT_MSG) {
            if (sender != Device_id()) {        // ignorar el eco de nuestro propio mensaje
                History_addIncoming(text, sender);
                Chat_sendAck(sender, msgId);    // confirmar recepcion al emisor
                lastTimeReceived = millis();
                if (mainState == STATE_IDLE) triggerAnimation(ANIM_CHASING_HEART);
            }
        } else {
            // Mensaje plano/legado (sin sobre): comportamiento anterior.
            History_addIncoming(raw, 0);
            lastTimeReceived = millis();
            if (mainState == STATE_IDLE) triggerAnimation(ANIM_CHASING_HEART);
        }
    }

    Display_update();
}

//=============================================================
// AYUDANTES INTERNOS
//=============================================================

// --- Pantalla de resultado de envío, con confirmación de entrega (ACK) ---
// Tras enviar mostramos "Enviado / esperando confirm..."; si llega el ACK del
// receptor pasa a "Entregado!"; si no llega en unos segundos, "(sin confirmar)".
static String        g_resMsg;
static bool          g_resIsSend = false;   // el último resultado fue un envío correcto
static unsigned long g_resAt = 0;
static int           g_resPhase = 0;        // 0=esperando ACK, 1=entregado, 2=sin confirmar

static void drawSendResult(int phase) {
    Display_clear();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.println(phase == 1 ? "Entregado!" : "Enviado:");
    display.println(g_resMsg);
    display.println();
    if (phase == 0)      display.println("esperando confirm...");
    else if (phase == 2) display.println("(sin confirmar)");
    else                 display.println("visto por el otro");
    display.display();
}

// Refresca la pantalla de resultado si cambia el estado de entrega. No hace nada
// si el último resultado no fue un envío (p.ej. "Cancelado").
static void updateSendResultScreen() {
    if (!g_resIsSend || g_resPhase != 0) return;
    if (Chat_awaitingAck() && Chat_delivered()) { g_resPhase = 1; drawSendResult(1); }
    else if (millis() - g_resAt > 3000)         { g_resPhase = 2; drawSendResult(2); }
}

// Espera (sin bloquear la recepción) hasta que se pulse un botón.
// Devuelve true si se pulsó MORSE (repetir), false si FINISH (salir).
static bool waitButtonMorseOrFinish() {
    while (!isMorsePressed() && !isFinishPressed()) {
        backgroundTick();
        updateSendResultScreen();
        yield();
        delay(10);
    }
    return isMorsePressed();
}

// Envía un mensaje de chat y muestra el resultado. El mensaje se guarda también
// en el historial como ENVIADO (Chat_send) y se arma la espera del ACK.
static void sendAndShowResult(const String &mensaje) {
    Chat_resetPending();
    bool ok = Chat_send(mensaje);
    if (ok) triggerAnimation(ANIM_GIVING_HEART);

    g_resMsg = mensaje;
    g_resIsSend = ok;
    g_resAt = millis();
    g_resPhase = 0;

    if (ok) {
        drawSendResult(0);
    } else {
        Display_clear();
        display.setCursor(0, 0);
        display.setTextSize(1);
        display.setTextColor(SH110X_WHITE);
        display.println("Error al enviar");
        display.display();
    }
}

// Reinicia las variables de creación del mensaje Morse.
static void resetMorseState() {
    mensajeAEnviar = "";
    morseCode = "";
    morsePrefix = "";
    lastMorsePrefix = "";
    morseScrollOffset = 0;
}

// Tras mostrar un resultado (Enviado/Error/Cancelado): suelta el botón que lo
// disparó, garantiza un tiempo mínimo en pantalla y luego espera la siguiente
// pulsación. Devuelve true si MORSE (repetir), false si FINISH (salir).
static bool waitAfterResult() {
    unsigned long shownAt = millis();
    while (isMorsePressed() || isFinishPressed()) {   // soltar el botón
        backgroundTick(); updateSendResultScreen(); yield(); delay(10);
    }
    while (millis() - shownAt < RESULT_MIN_MS) {       // tiempo mínimo visible
        backgroundTick(); updateSendResultScreen(); yield(); delay(10);
    }
    bool repeat = waitButtonMorseOrFinish();
    g_resIsSend = false;   // salimos de la pantalla de resultado
    return repeat;
}

// Dibuja un texto largo con salto de línea por palabras (fuente 6 px -> ~21
// caracteres por línea en los 128 px de ancho).
static void drawWrappedMessage(const String &msg, int startY) {
    const int MAX_CHARS = 21;
    const int LINE_H = 10;
    int y = startY;
    String remaining = msg;
    while (remaining.length() > 0 && y <= 105) {
        int cut = remaining.length();
        if ((int)remaining.length() > MAX_CHARS) {
            cut = MAX_CHARS;
            for (int i = cut; i > 0; i--) {
                if (remaining.charAt(i) == ' ') { cut = i; break; }
            }
        }
        String line = remaining.substring(0, cut);
        remaining = remaining.substring(cut);
        if (remaining.length() > 0 && remaining.charAt(0) == ' ')
            remaining = remaining.substring(1);
        display.setCursor(0, y);
        display.println(line);
        y += LINE_H;
    }
}

// Antigüedad legible de un mensaje del historial. Si viene de una sesión anterior
// (millis() se reinició al apagar), no es fiable -> "--".
static String histAgeStr(int idx) {
    if (!History_isFromThisBoot(idx)) return "--";
    unsigned long ts = History_getTimestamp(idx);
    unsigned long now = millis();
    unsigned long ageMin = (now >= ts) ? ((now - ts) / 60000UL) : 0;
    return String(ageMin) + "m";
}

// Edad en minutos para el cacheo de refresco (-2 = fija, no cambia: msg antiguo).
static int histAgeMinForCache(int idx) {
    if (!History_isFromThisBoot(idx)) return -2;
    unsigned long ts = History_getTimestamp(idx);
    unsigned long now = millis();
    return (int)((now >= ts) ? ((now - ts) / 60000UL) : 0);
}

// Una línea de la lista del historial: cursor + antigüedad + (Tu: si es enviado) + texto.
static String histListLine(int idx, bool selected) {
    String body = History_getMessage(idx);
    if (History_isOutgoing(idx)) body = "Tu:" + body;
    if (body.length() > 18) body = body.substring(0, 15) + "...";
    return (selected ? "> " : "  ") + histAgeStr(idx) + " " + body;
}

//=============================================================
// FUNCIONES DE GESTIÓN DE ESTADOS
//=============================================================

// ==================== STATE_IDLE ====================
bool handleIdle() {
    static bool justEnteredIdle = true;

    LoRa.idle();

    // El menú se redibuja en el buffer cada iteración (así se repara lo que
    // pinten las animaciones), pero el volcado a pantalla está limitado por
    // Display_update()/moveCursor(), no se hace en cada vuelta.
    drawMenu();
    if (justEnteredIdle) {
        Display_resetMenuCursor();   // forzar repintado del cursor al entrar
        justEnteredIdle = false;
    }
    moveCursor();
    updateHippoAnimation();
    // La recepción de mensajes la gestiona backgroundTick() (bucle principal).

    if (isMorsePressed()) {
        delay(50);
        if (isMorsePressed()) {
            if (cursorPos == 0)      mainState = STATE_SEND_MENU;
            else if (cursorPos == 1) mainState = STATE_HISTORY_MENU;
            else if (cursorPos == 2) mainState = STATE_GAMES_MENU;
            lastInteraction = millis();
            justEnteredIdle = true;     // saldremos del IDLE
            Display_clear();
            menuTransitionDelay();
            return true;
        }
    }

    if (millis() - lastInteraction > SLEEP_TIMEOUT) {
        mainState = STATE_SLEEP;
        justEnteredIdle = true;
        Display_clear();
        Display_setPower(false);   // apagar el panel OLED para ahorrar batería
        LoRa.idle();
        menuTransitionDelay();
        return true;
    }

    return false;
}

#if defined(ESP8266) && ENABLE_CPU_LIGHT_SLEEP
// Duerme la CPU (light sleep) ~1 s o hasta que se pulse un botón (despertar por
// GPIO en nivel bajo, los botones son activos en bajo). El temporizador finito
// es una red de seguridad: aunque el despertar por GPIO fallara, la CPU vuelve
// sola tras ~1 s, así que NUNCA puede quedarse colgada en la siesta.
//   Consumo esperado: ~70 mA -> ~1-2 mA mientras duerme.
//   (El consumo real y el despertar por botón hay que medirlos en la placa.)
static void cpuLightSleep() {
    wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);
    wifi_fpm_open();
    gpio_pin_wakeup_enable(GPIO_ID_PIN(PIN_MORSE_BUTTON),  GPIO_PIN_INTR_LOLEVEL);
    gpio_pin_wakeup_enable(GPIO_ID_PIN(PIN_FINISH_BUTTON), GPIO_PIN_INTR_LOLEVEL);
    wifi_fpm_do_sleep(1000 * 1000);   // 1 s en microsegundos (o hasta GPIO)
    delay(1001);                       // imprescindible para que la siesta ocurra
    wifi_fpm_close();
}
#endif

// ==================== STATE_SLEEP ====================
bool handleSleep() {

    if (millis() - lastTimeReceived > LORA_DEEP_SLEEP) {
        LoRa.sleep();
    }

    // Para despertar hacen falta WAKE_PRESS_COUNT pulsaciones DISTINTAS dentro
    // de WAKE_WINDOW_MS. Se detecta el flanco de subida para no contar la misma
    // pulsación muchas veces mientras el botón permanece presionado.
    static bool prevPressed = false;
    bool pressed = isMorsePressed() || isFinishPressed();

    if (pressed && !prevPressed) {
        unsigned long now = millis();
        if (buttonPressCount == 0 || (now - firstPressTime) > WAKE_WINDOW_MS) {
            firstPressTime = now;
            buttonPressCount = 1;
        } else {
            buttonPressCount++;
            if (buttonPressCount >= WAKE_PRESS_COUNT) {
                buttonPressCount = 0;
                mainState = STATE_IDLE;
                lastInteraction = now;
                LoRa.idle();              // reactivar la radio
                Display_setPower(true);   // reactivar el panel OLED
                Display_clear();
                menuTransitionDelay();
                prevPressed = false;
                return true;
            }
        }
    }
    prevPressed = pressed;

#if defined(ESP8266) && ENABLE_CPU_LIGHT_SLEEP
    // Si no hay ningún botón pulsado, dormir la CPU hasta el próximo evento.
    if (!pressed) cpuLightSleep();
#endif

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
            // OJO: no tocar 'primeraVezMenu' aquí. createMorseMessage() lo usa
            // para inicializar su temporizador de inactividad la primera vez.
            dentroMenuEnviar = true;
            mensajeCancelado = false;
            mensajeEnviado = false;

            MorseResult res = createMorseMessage();

            if (res == MORSE_SENT || res == MORSE_CANCELLED) {
                if (res == MORSE_SENT) {
                    sendAndShowResult(mensajeAEnviar);
                } else {
                    Display_clear();
                    display.setCursor(0, 0);
                    display.setTextSize(1);
                    display.setTextColor(SH110X_WHITE);
                    display.println("Cancelado");
                    display.display();
                }

                bool repeat = waitAfterResult();
                menuTransitionDelay();
                resetMorseState();

                if (repeat) {
                    // Volver a la creación de un nuevo mensaje Morse
                    primeraVezMenu = true;
                    sendSubState = SEND_MORSE;
                } else {
                    // Volver al menú principal
                    dentroMenuEnviar = false;
                    sendSubState = SEND_WAIT;
                    mainState = STATE_IDLE;
                }
                Display_clear();
                return true;
            }

            return true;
        }

        // ---------------------------------------------------
        // ------------------ SEND_INSTANT_MSG ---------------
        // ---------------------------------------------------
        case SEND_INSTANT_MSG:
        {
            static bool needRedraw = true;

            int seleccion = getPotValue(7);           // 8 mensajes: índices 0..7
            drawInstantMessagesMenu(seleccion, needRedraw);
            needRedraw = false;

            if (isMorsePressed()) {
                delay(50);
                if (isMorsePressed()) {
                    sendAndShowResult(nombresMensajes[seleccion]);

                    bool repeat = waitAfterResult();
                    menuTransitionDelay();
                    needRedraw = true;

                    // Tras enviar siempre se vuelve a la selección de modo;
                    // con FINISH además se sale al menú principal.
                    sendSubState = SEND_WAIT;
                    if (!repeat) mainState = STATE_IDLE;
                    Display_clear();
                    return true;
                }
            }

            if (isFinishPressed()) {
                delay(50);
                if (isFinishPressed()) {
                    needRedraw = true;
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
        selected = 0;       // empezar siempre en el mensaje más reciente
        topIndex = 0;
    }

    if (firstTime || forceRedraw) {
        firstTime = false;
        
        Display_clear();
        display.setCursor(0,0);
        display.setTextSize(1);
        display.setTextColor(SH110X_WHITE);
        display.println("Historial");

        for (int line = 0; line < LINES_PER_PAGE; ++line) {
            int idx = topIndex + line;
            if (idx >= total) break;

            display.setCursor(0, 10 + (line * 10));
            display.println(histListLine(idx, idx == selected));
        }
        display.display();

        prevTop = topIndex;
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
            while (isMorsePressed()) { delay(10); }   // esperar a soltar
            delay(120);

            // ---- VISTA DEL MENSAJE COMPLETO ----
            {
                String full = History_getMessage(selected);

                Display_clear();
                display.setTextSize(1);
                display.setTextColor(SH110X_WHITE);
                display.setCursor(0, 0);
                if (History_isOutgoing(selected)) {
                    display.print("Enviado");
                } else {
                    display.print("De ");
                    uint8_t s = History_getSender(selected);
                    if (s) { display.print("#"); display.print(s); }
                    else   { display.print("?"); }
                }
                if (History_isFromThisBoot(selected)) {
                    display.print("  hace ");
                    display.print(histAgeStr(selected));
                }
                display.println();
                drawWrappedMessage(full, 16);
                display.setCursor(0, 118);
                display.print("A: Borrar  B: Volver");
                display.display();

                // Esperar accion: MORSE (A) = borrar, FINISH (B) = volver
                while (Serial.available()) Serial.read();
                delay(120);
                bool wantDelete = false, goBack = false;
                while (!wantDelete && !goBack) {
                    if (isMorsePressed()) { delay(60); if (isMorsePressed()) { while (isMorsePressed()) delay(10); wantDelete = true; } }
                    else if (isFinishPressed()) { delay(60); if (isFinishPressed()) { while (isFinishPressed()) delay(10); goBack = true; } }
                    backgroundTick(); yield(); delay(15);
                }
                if (goBack) {
                    prevTop = -1; prevSelected = -1;
                    forceRedraw = true; firstTime = true; justEntered = true;
                    menuTransitionDelay();
                    return true;
                }
            }
            delay(120);
            
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

                backgroundTick();
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
            
            int ageMinInt = histAgeMinForCache(idx);

            if (cachedMinutes[line] != ageMinInt) {
                cachedMinutes[line] = ageMinInt;
                needRedraw = true;
            }
        }
    }

    if (needRedraw) {
        display.fillRect(0, 0, 128, 128, SH110X_BLACK);

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

            display.setCursor(0, 10 + (line * 10));
            display.println(histListLine(idx, idx == selected));

            cachedMinutes[line] = histAgeMinForCache(idx);
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
    static bool needRedraw = true;

    // Submenú de iconos; selección con el potenciómetro (5 juegos: 0..4).
    int seleccion = getPotValue(4);
    drawGamesMenu(seleccion, needRedraw);
    needRedraw = false;

    // Si se pulsa MORSE -> arrancar el juego seleccionado
    if (isMorsePressed()) {
        delay(50);
        if (isMorsePressed()) {
            menuTransitionDelay();
            needRedraw = true;   // al volver, refrescar el submenú

            switch (seleccion) {
                case 0: // Tetris Coop (implementado): gestiona su propia salida a IDLE
                    startTetrisCoop();
                    return true;

                case 1: // Poker (implementado): gestiona su propia salida a IDLE
                    startPoker();
                    return true;

                case 2: // RefillGame (implementado): gestiona su propia salida a IDLE
                    startRefillGame();
                    return true;

                case 3: // Choose4Me (implementado): deja mainState = STATE_IDLE
                    startChoose4Me();
                    return true;

                case 4: // HippoRadar (utilidad): gestiona su propia salida a IDLE
                    startHippoRadar();
                    return true;

                default: { // Sin implementar
                    Display_clear();
                    display.setCursor(0, 20);
                    display.setTextSize(1);
                    display.setTextColor(SH110X_WHITE);
                    display.println("Proximamente:");
                    display.println(nombresJuegos[seleccion]);
                    display.display();

                    // MORSE -> volver al submenú; FINISH -> salir a IDLE
                    bool repeat = waitButtonMorseOrFinish();
                    menuTransitionDelay();
                    Display_clear();
                    if (!repeat) mainState = STATE_IDLE;
                    return true;
                }
            }
        }
    }

    // Si se pulsa FINISH -> volver al menú principal
    if (isFinishPressed()) {
        delay(50);
        if (isFinishPressed()) {
            needRedraw = true;
            mainState = STATE_IDLE;
            menuTransitionDelay();
            Display_clear();
            return true;
        }
    }

    return true;
}