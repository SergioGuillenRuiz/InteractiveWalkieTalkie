// Driver del simulador: ejecuta el firmware real (setup/loop) sobre el motor
// host, interpreta scripts de prueba con aserciones, y ofrece un modo
// interactivo por teclado.
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <iostream>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <csignal>
#ifdef _WIN32
#include <conio.h>
#include <process.h>
#else
#include "platform_linux.h"   // sim/linux: _kbhit/_getch/_getpid... sobre termios
#endif
#include <thread>
#include <chrono>
#include <filesystem>

#include "sim_state.h"
#include "framebuffer.h"
#include "air_channel.h"
#include "sx127x.h"
#include <LoRa.h>              // la libreria real (sim/vendor/lora): el autotest del chip la usa directamente
#include "MyLora.h"            // parametros de modulacion del proyecto (LORA_*)
#include "EEPROM.h"
#include "SimpleCrypto.h"      // String SimpleCrypto_encrypt/decrypt
#include "Clock.h"             // Clock_set/Clock_now (comando settime)
#include <Adafruit_SH110X.h>   // tipo del display

#undef min
#undef max
#undef abs
#undef constrain

// Firmware
extern void setup();
extern void loop();
extern Adafruit_SH1107 display;   // definido en src/Display.cpp

// Configuracion de consola (Windows): fija fuente/tamano para pixeles cuadrados.
// Devuelve el tamano resultante en caracteres (0 si no aplica, p.ej. en WT).
extern "C" void simSetupConsole(int wantCols, int wantRows, int *outCols, int *outRows);

// ---------------------------------------------------------------------------
// Estado del driver
// ---------------------------------------------------------------------------
static bool g_color = false;
static std::string g_shotsDir = "out";
static int g_scale = 4;
static int g_pass = 0, g_fail = 0;
static int g_shotN = 0;

static const uint32_t TICK = 2;       // ms por iteración de loop() sin delay
static const uint32_t SLACK = 60000;  // margen antibloqueo (ms virtuales)

// Paquetes LoRa programados con "in <ms> chatack|chatmsg ..." y entregados por
// el motor (EV_INJECT) en el instante pedido, incluso durante esperas
// bloqueantes del firmware (p.ej. la pantalla de resultado de envio).
static std::vector<std::string> g_deferredPackets;
static void injectDeferred(int idx) {
    if (idx >= 0 && idx < (int)g_deferredPackets.size()) simLoraInject(g_deferredPackets[idx]);
}
static int deferPacket(const std::string &enc) {
    g_deferredPackets.push_back(enc);
    return (int)g_deferredPackets.size() - 1;
}

// Construye una baliza de presencia cifrada: 0x02 | peer | flags | epoch(4) | batt | gen.
// 'gen' = generacion de ajuste de la hora del peer (0 = nunca fijada; <0 = formato antiguo sin el byte).
static std::string buildBeacon(int peer, long epoch, int batt, int flags, int gen = 0) {
    String p;
    p += (char)0x02;
    p += (char)peer;
    p += (char)flags;
    p += (char)((epoch >> 24) & 0xFF);
    p += (char)((epoch >> 16) & 0xFF);
    p += (char)((epoch >> 8) & 0xFF);
    p += (char)(epoch & 0xFF);
    p += (char)batt;
    if (gen >= 0) p += (char)gen;     // gen < 0: baliza ANTIGUA, sin el byte de generacion
    String enc = SimpleCrypto_encrypt(p);
    return std::string(enc.c_str(), enc.length());
}

// Construye un paquete de Tres en raya cifrado: 0x07 'H'|'S' | peer [ tablero(9) fin ].
static std::string buildTtt(const std::string &sub, int peer, const std::string &cells, int over) {
    String p; p += (char)0x07;
    if (sub == "hello") { p += 'H'; p += (char)peer; }
    else { p += 'S'; p += (char)peer; for (int i = 0; i < 9; i++) p += (char)((i < (int)cells.size()) ? cells[i] - '0' : 0); p += (char)over; }
    String enc = SimpleCrypto_encrypt(p);
    return std::string(enc.c_str(), enc.length());
}

// ---------------------------------------------------------------------------
// Motor de ejecución
// ---------------------------------------------------------------------------
// 'slack' es el margen antibloqueo: cuanto deja correr el reloj MAS ALLA de
// 'target' antes de abortar por "espera bloqueante". Con el valor por defecto
// (SLACK=60 s) un run sobre un bucle bloqueante congela el ULTIMO frame estable.
// Con un slack PEQUENO el run congela el framebuffer en ~target+slack, lo que
// permite capturar pantallas TRANSITORIAS (animaciones, fases de radar que
// dependen de pings periodicos) en un instante elegido. Ver demo_juegos_b.sim.
static void runFor(uint32_t ms, uint32_t slack = SLACK) {
    uint32_t target = sim::now() + ms;
    sim::setDeadline(target + slack);
    bool timeout = false;
    try {
        sim::applyDue();
        while (sim::now() < target) {
            uint32_t before = sim::now();
            loop();
            if (sim::now() == before) {
                uint32_t step = TICK;
                if (before + step > target) step = target - before;
                if (step == 0) step = 1;
                sim::advance(step);
            }
        }
    } catch (sim::Timeout &) { timeout = true; }
    sim::clearDeadline();
    if (timeout) fprintf(stderr, "[sim] aviso: espera bloqueante (deadline alcanzado)\n");
}

static void fastForward(uint32_t ms) {
    uint32_t target = sim::now() + ms;
    sim::setDeadline(target + SLACK);
    try {
        while (sim::now() < target) {
            uint32_t before = sim::now();
            loop();
            if (sim::now() == before) {
                uint32_t step = 200;
                if (before + step > target) step = target - before;
                if (step == 0) step = 1;
                sim::advance(step);
            }
        }
    } catch (sim::Timeout &) {}
    sim::clearDeadline();
}

static void tap(sim::EvKind k, uint32_t durMs) {
    uint32_t t0 = sim::now();
    sim::scheduleAt(t0, k, 1);
    sim::scheduleAt(t0 + durMs, k, 0);
    runFor(durMs + 120);   // pulsación + margen de antirrebote al soltar
}
static void holdRelease(sim::EvKind k, uint32_t durMs) { tap(k, durMs); }

// ---------------------------------------------------------------------------
// Utilidades de texto
// ---------------------------------------------------------------------------
static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
// Devuelve el texto tras saltar `count` tokens separados por espacios.
static std::string restAfter(const std::string &line, int count) {
    std::istringstream is(line);
    std::string tok; int i = 0;
    std::string rest;
    size_t pos = 0;
    while (i < count && is >> tok) { i++; }
    pos = is.tellg() == std::streampos(-1) ? line.size() : (size_t)is.tellg();
    rest = (pos <= line.size()) ? line.substr(pos) : "";
    return trim(rest);
}
static bool contains(const std::string &h, const std::string &n) {
    return h.find(n) != std::string::npos;
}

// ---------------------------------------------------------------------------
// Aserciones
// ---------------------------------------------------------------------------
static void check(bool ok, const std::string &desc) {
    if (ok) { g_pass++; printf("  [PASS] %s\n", desc.c_str()); }
    else    { g_fail++; printf("  [FAIL] %s\n", desc.c_str()); }
}

// Errores del PROPIO guion (comando o comprobacion mal escritos, argumentos que
// faltan...). Cuentan como FAIL: si no, una errata como "expect txt X" no
// comprobaria nada y el test pasaria en silencio.
static int g_lineNo = 0;
static std::string g_curLine;
static size_t g_markRx = 0, g_markTx = 0;       // "radio mark": los contadores de expect rx / expect tx parten de aqui
static uint32_t g_markT = 0;
static std::vector<uint32_t> g_waitSamples;   // instantes (ms) en que "waitfor" vio su texto: para "expect spread" 
static void scriptError(const std::string &msg) {
    g_fail++;
    if (g_lineNo > 0) printf("  [FAIL] linea %d: %s  -> \"%s\"\n", g_lineNo, msg.c_str(), g_curLine.c_str());
    else              printf("  [FAIL] %s  -> \"%s\"\n", msg.c_str(), g_curLine.c_str());
}

// Validadores de argumentos.
static bool parseBtn(const std::string &b, sim::EvKind *k) {
    if (b == "morse")  { *k = sim::EV_MORSE;  return true; }
    if (b == "finish") { *k = sim::EV_FINISH; return true; }
    scriptError("boton desconocido \"" + b + "\" (usa morse|finish)");
    return false;
}
static bool parseUpDown(const std::string &v, bool *down) {
    if (v == "down") { *down = true;  return true; }
    if (v == "up")   { *down = false; return true; }
    scriptError("estado de boton desconocido \"" + v + "\" (usa down|up)");
    return false;
}

// Un paquete que el firmware no puede cifrar (texto > 95 bytes) se inyectaria
// VACIO y no llegaria nada: se avisa como error del guion.
static bool encryptedOk(const String &enc) {
    if (enc.length() > 0) return true;
    scriptError("el paquete supera el limite de 95 bytes que cifra el firmware: no se inyecta");
    return false;
}

// ---------------------------------------------------------------------------
// Autotest del modelo del chip: la libreria LoRa REAL (sin firmware) contra el chip simulado.
// Comprueba la formula del tiempo en el aire con los numeros del roadmap, el bloqueo al transmitir,
// y las reglas de recepcion (escuchar, expirar, sorda en STDBY/SLEEP/TX, colision, parametros, CRC...).
// Deja la radio en un estado cualquiera: despues hay que hacer "reboot" (setup() la reinicializa).
// ---------------------------------------------------------------------------
static void radioSelfTest() {
    const bool wasIdeal = sx::ideal();
    sx::setIdeal(false);
    sx::peerFrame() = AirFrame();                                   // sf7 / 125 kHz / CR4-5 / 8 simbolos / CRC on / sync 0x12
    sx::peerFrame().freq = (long)LORA_FREQUENCY;
    auto radioInit = []() {
        LoRa.setPins(PIN_LORA_NSS, PIN_LORA_RST, PIN_LORA_DIO0);
        LoRa.begin((long)LORA_FREQUENCY);
        LoRa.setSpreadingFactor(7); LoRa.setSignalBandwidth(125E3); LoRa.setTxPower(17);
    };
    auto payload = [](int n, char c) { std::string r; for (int i = 0; i < n; i++) r += (char)(c + (i % 7)); return r; };
    auto txTime = [&](int n, int sf, bool crc) {                    // ms que BLOQUEA transmitir n bytes
        radioInit(); LoRa.setSpreadingFactor(sf); if (crc) LoRa.enableCrc(); else LoRa.disableCrc();
        String p(payload(n, 'A'));
        uint32_t t0 = sim::now(); LoRa.beginPacket(); LoRa.print(p); LoRa.endPacket();
        return (double)(sim::now() - t0);
    };
    // El chip pone al dia su estado de forma perezosa (en cada acceso por SPI/pin): se sincroniza antes de mirar.
    auto lastOutcome = []() { sx::currentMode(); return sx::rxLog().empty() ? sx::RX_PENDING : sx::rxLog().back().outcome; };
    auto near = [](double a, double b, double tol) { return std::fabs(a - b) <= tol; };
    auto adv = [](double ms) { sim::advance((uint32_t)std::ceil(ms)); };

    // --- 1) Tiempo en el aire y bloqueo de la CPU al transmitir (cifras del roadmap, CRC desactivado) ---
    check(near(txTime(64, 7, false), 118.0, 1.5),  "TX de 64 B a SF7/125 kHz bloquea ~118 ms (ACK/mensaje corto en hexadecimal)");
    check(near(txTime(224, 7, false), 348.4, 1.5), "TX de 224 B a SF7 bloquea ~348 ms (mensaje maximo en hexadecimal)");
    check(near(txTime(64, 10, false), 698.4, 1.5), "TX de 64 B a SF10 bloquea ~698 ms (ping de HippoRadar)");
    check(near(txTime(224, 7, true), 353.5, 1.5),  "con CRC activo el mismo paquete tarda ~5 ms mas (2 bytes de CRC)");
    check(near(sx::airtimeMs(32, 7, 125000, 1, 8, true, false), 72.2, 0.5), "formula: 32 B a SF7 con CRC = 72,2 ms");

    // --- 2) Recepcion continua: una trama se oye, con sus bytes y su RSSI ---
    radioInit(); LoRa.receive();
    std::string a = payload(48, 'a');
    sx::inject(a, -63);
    adv(sx::airtimeMs(48, 7, 125000, 1, 8, true, false) + 5);
    check(digitalRead(PIN_LORA_DIO0) == HIGH, "DIO0 sube con RxDone");
    int n = LoRa.parsePacket();
    std::string got; while (LoRa.available()) got += (char)LoRa.read();
    check(n == 48 && got == a, "RX continuo: se recibe la trama completa y sin errores");
    check(LoRa.packetRssi() == -63, "RSSI del paquete = -63 dBm");
    check(digitalRead(PIN_LORA_DIO0) == LOW, "DIO0 baja al borrar la bandera");

    // --- 3) La libreria en modo sondeo (parsePacket repetido): RX unico con caducidad de 100 simbolos ---
    radioInit(); LoRa.parsePacket();                                // arma RX unico (SymbTimeout = 100 simbolos = 102 ms)
    check(sx::currentMode() == 6, "parsePacket() arma el RX unico");
    adv(50);
    sx::inject(a, -70);
    adv(sx::airtimeMs(48, 7, 125000, 1, 8, true, false) + 5);
    check(LoRa.parsePacket() == 48, "RX unico: trama que empieza DENTRO de la ventana de 102 ms: se oye");
    radioInit(); LoRa.parsePacket();
    adv(150);                                                       // la ventana expira: la radio vuelve a STDBY
    check(sx::currentMode() == 1, "RX unico: pasados ~102 ms sin trama la radio vuelve a STDBY");
    sx::inject(a, -70);
    adv(sx::airtimeMs(48, 7, 125000, 1, 8, true, false) + 5);
    bool lostTimeout = (lastOutcome() == sx::RX_LOST_RXTIMEOUT);
    check(lostTimeout && LoRa.parsePacket() == 0, "RX unico expirado: la trama se PIERDE hasta el siguiente parsePacket()");

    // --- 4) idle() entre sondeos deja la radio sorda (lo que hace handleIdle() en cada vuelta) ---
    radioInit(); LoRa.parsePacket(); LoRa.idle();
    sx::inject(a, -70);
    adv(sx::airtimeMs(48, 7, 125000, 1, 8, true, false) + 5);
    bool lostStandby = (lastOutcome() == sx::RX_LOST_STANDBY);
    check(lostStandby && LoRa.parsePacket() == 0, "idle() tras armar la recepcion: la trama se pierde (radio sorda)");
    // salir de recepcion a mitad de una trama la aborta
    radioInit(); LoRa.receive();
    sx::inject(a, -70);
    adv(30); LoRa.idle(); adv(100);
    check(lastOutcome() == sx::RX_LOST_ABORTED, "pasar a idle() a mitad de una trama la aborta");
    // dormida
    radioInit(); LoRa.sleep();
    sx::inject(a, -70);
    adv(sx::airtimeMs(48, 7, 125000, 1, 8, true, false) + 5);
    check(lastOutcome() == sx::RX_LOST_SLEEP, "radio en sleep(): no oye nada");

    // --- 5) Half-duplex: mientras se transmite no se oye ---
    radioInit(); LoRa.beginPacket(); LoRa.print(String(payload(64, 'k'))); LoRa.endPacket(true);   // asincrono: vuelve ya
    sx::inject(a, -70);
    adv(300);
    check(lastOutcome() == sx::RX_LOST_TX, "half-duplex: una trama que llega mientras se transmite se pierde");

    // --- 6) Parametros de modulacion: otro SF no se demodula ---
    radioInit(); LoRa.receive();
    sx::peerFrame().sf = 10;
    sx::inject(a, -70);
    adv(sx::airtimeMs(48, 10, 125000, 1, 8, true, false) + 10);
    check(lastOutcome() == sx::RX_LOST_MISMATCH, "trama a SF10 con el receptor a SF7: no se oye");
    sx::peerFrame().sf = 7;
    sx::peerFrame().sync = 0x34;
    sx::inject(a, -70); adv(200);
    check(lastOutcome() == sx::RX_LOST_MISMATCH, "otra palabra de sincronismo: no se oye");
    sx::peerFrame().sync = 0x12;

    // --- 7) Colision y CRC ---
    radioInit(); LoRa.receive();
    double air48 = sx::airtimeMs(48, 7, 125000, 1, 8, true, false);
    sx::inject(a, -70); adv(20); sx::inject(a, -70);                // la 2a empieza con la 1a a medias
    adv(air48 + 10);
    sx::currentMode();
    check(sx::rxLog()[sx::rxLog().size() - 2].outcome == sx::RX_CRC_ERROR && lastOutcome() == sx::RX_LOST_COLLISION,
          "dos tramas solapadas: la 2a se pierde y la 1a llega con CRC erroneo");
    check(LoRa.parsePacket() == 0, "con CRC erroneo la libreria descarta el paquete (parsePacket = 0)");
    radioInit(); LoRa.receive();
    sx::peerFrame().crc = false;
    sx::inject(a, -70, true);                                       // trama corrupta SIN CRC: nadie se entera
    adv(sx::airtimeMs(48, 7, 125000, 1, 8, false, false) + 5);
    n = LoRa.parsePacket(); got.clear(); while (LoRa.available()) got += (char)LoRa.read();
    check(n == 48 && got != a, "trama corrupta SIN CRC: llega y se entrega con datos alterados (el CRC existe por esto)");
    sx::peerFrame().crc = true;

    // --- 8) Dos tramas seguidas sin leer: la 2a pisa a la 1a ---
    radioInit(); LoRa.receive();
    std::string b = payload(40, 'p');
    sx::inject(a, -70); adv(air48 + 2);
    sx::inject(b, -70); adv(sx::airtimeMs(40, 7, 125000, 1, 8, true, false) + 2);
    sx::currentMode();
    check(sx::rxLog()[sx::rxLog().size() - 2].outcome == sx::RX_OVERRUN, "si el firmware no lee la 1a antes de que acabe la 2a, se pierde");
    n = LoRa.parsePacket(); got.clear(); while (LoRa.available()) got += (char)LoRa.read();
    check(n == 40 && got == b, "y lo que se lee es la ultima trama");

    // --- 9) Tras transmitir hay que volver a escuchar (la libreria deja el chip en STDBY) ---
    radioInit(); LoRa.receive();
    LoRa.beginPacket(); LoRa.print(String(payload(20, 'x'))); LoRa.endPacket();
    check(sx::currentMode() == 1 && !sx::listening(), "tras endPacket() el chip queda en STDBY: no escucha hasta llamar a receive()");
    LoRa.receive();
    check(sx::currentMode() == 5 && sx::listening(), "receive() vuelve al RX continuo");

    sx::setIdeal(wasIdeal);
    printf("  (autotest del chip hecho: la radio ha quedado en un estado cualquiera; usa \"reboot\" para continuar con el firmware)\n");
}

// ---------------------------------------------------------------------------
// Intérprete de comandos
// ---------------------------------------------------------------------------

static void doShot(const std::string &nameArg) {
    char path[512];
    if (nameArg.empty())
        snprintf(path, sizeof(path), "%s/shot_%03d.bmp", g_shotsDir.c_str(), ++g_shotN);
    else
        snprintf(path, sizeof(path), "%s/%s%s", g_shotsDir.c_str(), nameArg.c_str(),
                 contains(nameArg, ".bmp") ? "" : ".bmp");
    if (simSaveBMP(path, g_scale)) printf("  captura -> %s\n", path);
    else scriptError(std::string("no se pudo guardar la captura ") + path);
}

static void execLine(const std::string &raw) {
    std::string line = trim(raw);
    if (line.empty() || line[0] == '#') return;
    g_curLine = line;
    // Comentarios en linea: cortar a partir de " #" SEGUIDO DE ESPACIO (o de fin de linea). Un "#" pegado
    // a un texto ("ACK a #50", "De #137") es parte del argumento: antes se cortaba ahi y esas
    // aserciones solo comprobaban "ACK a" o "De".
    size_t cpos = std::string::npos;
    for (size_t q = line.find(" #"); q != std::string::npos; q = line.find(" #", q + 1)) {
        if (q + 2 >= line.size() || line[q + 2] == ' ' || line[q + 2] == '\t') { cpos = q; break; }
    }
    if (cpos != std::string::npos) line = trim(line.substr(0, cpos));
    if (line.empty()) return;

    std::istringstream is(line);
    std::string cmd; is >> cmd;

    if (cmd == "pot") { int v; if (!(is >> v)) { scriptError("pot necesita un valor 0-1023"); return; } sim::setPot(v); }
    else if (cmd == "pot%") { int p; if (!(is >> p)) { scriptError("pot% necesita un valor 0-100"); return; } sim::setPot(p * 1023 / 100); }
    else if (cmd == "morse" || cmd == "finish") {
        std::string v; is >> v; bool down;
        if (!parseUpDown(v, &down)) return;
        if (cmd == "morse") sim::setMorse(down); else sim::setFinish(down);
    }
    else if (cmd == "tap") {
        std::string b; uint32_t ms = 150; is >> b; sim::EvKind k;
        if (!parseBtn(b, &k)) return;
        if (!(is >> ms)) ms = 150;
        tap(k, ms);
    }
    else if (cmd == "hold") {
        std::string b; uint32_t ms = 0; is >> b; sim::EvKind k;
        if (!parseBtn(b, &k)) return;
        if (!(is >> ms)) { scriptError("hold necesita la duracion en ms"); return; }
        holdRelease(k, ms);
    }
    else if (cmd == "wait" || cmd == "run") {
        uint32_t ms = 0;
        if (!(is >> ms)) { scriptError(cmd + " necesita los ms a esperar"); return; }
        uint32_t slack = SLACK;            // por defecto, margen antibloqueo largo (60 s)
        uint32_t s; if (is >> s) slack = s; // opcional: "run <ms> <slack>" congela el frame en ~now+ms+slack
        runFor(ms, slack);
    }
    else if (cmd == "watch") {   // watch pixel <x> <y> on|off <ms>: el pixel debe mantenerse TODO ese tiempo
        // Ejecuta el firmware como "run" y comprueba el pixel tras CADA vuelta de loop(): detecta
        // parpadeos que un expect puntual no ve (p.ej. algo que borra y repinta en la misma vuelta).
        std::string what; is >> what;
        int x = 0, y = 0; std::string st; uint32_t ms = 0;
        if (what != "pixel" || !(is >> x >> y >> st >> ms) || (st != "on" && st != "off")) {
            scriptError("watch necesita: pixel <x> <y> on|off <ms>"); return;
        }
        bool want = (st == "on");
        uint32_t target = sim::now() + ms;
        sim::setDeadline(target + SLACK);
        long samples = 0, bad = 0; uint32_t firstBad = 0;
        try {
            sim::applyDue();
            while (sim::now() < target) {
                uint32_t before = sim::now();
                loop();
                samples++;
                if (display.simPixel(x, y) != want) { if (!bad) firstBad = sim::now() - (target - ms); bad++; }
                if (sim::now() == before) {
                    uint32_t step = TICK;
                    if (before + step > target) step = target - before;
                    if (step == 0) step = 1;
                    sim::advance(step);
                }
            }
        } catch (sim::Timeout &) { fprintf(stderr, "[sim] aviso: espera bloqueante (deadline alcanzado)\n"); }
        sim::clearDeadline();
        char b[160];
        if (bad == 0) snprintf(b, sizeof(b), "pixel(%d,%d)=%s durante %u ms (%ld vueltas)", x, y, st.c_str(), (unsigned)ms, samples);
        else          snprintf(b, sizeof(b), "pixel(%d,%d)=%s durante %u ms: falla en %ld de %ld vueltas (la 1a a los %u ms)", x, y, st.c_str(), (unsigned)ms, bad, samples, (unsigned)firstBad);
        check(bad == 0, b);
    }
    else if (cmd == "radio") {   // radio ideal on|off | peer <campo> <valor> | mark | report
        std::string sub; is >> sub;
        if (sub == "ideal") {
            std::string v; is >> v;
            if (v != "on" && v != "off") { scriptError("radio ideal necesita on|off"); return; }
            sx::setIdeal(v == "on");
            printf("  radio %s\n", v == "on" ? "IDEAL (siempre escucha, sin tiempo en el aire)" : "REALISTA (chip SX1276 + tiempo en el aire)");
        } else if (sub == "peer") {      // parametros con que emiten los "otros equipos" que inyecta el guion
            std::string f, v; is >> f >> v;
            AirFrame &pf = sx::peerFrame();
            if (f == "sf" && !v.empty()) pf.sf = atoi(v.c_str());
            else if (f == "bw" && !v.empty()) pf.bw = (long)(atof(v.c_str()) * 1000.0);        // en kHz
            else if (f == "crc" && (v == "on" || v == "off")) pf.crc = (v == "on");
            else if (f == "preamble" && !v.empty()) pf.preamble = atoi(v.c_str());
            else if (f == "sync" && !v.empty()) pf.sync = (int)strtol(v.c_str(), nullptr, 0);
            else if (f == "freq" && !v.empty()) pf.freq = (long)(atof(v.c_str()) * 1e6);     // en MHz
            else if (f == "reset") { pf.freq = (long)LORA_FREQUENCY; pf.sf = LORA_SPREADING; pf.bw = (long)LORA_BANDWIDTH; pf.cr = 1; pf.preamble = 8; pf.crc = true; pf.sync = 0x12; }
            else { scriptError("radio peer necesita: sf <n> | bw <kHz> | crc on|off | preamble <n> | sync <hex> | freq <MHz> | reset"); return; }
        } else if (sub == "deepsleep") {   // radio deepsleep <ms>: ahorro opcional (Lora_setDeepSleepMs); 0 = no dormir nunca
            long ms = -1;
            if (!(is >> ms) || ms < 0) { scriptError("radio deepsleep necesita los ms (0 = nunca)"); return; }
            Lora_setDeepSleepMs((uint32_t)ms);
            printf("  la radio se dormira tras %ld ms sin oir nada en suspension%s\n", ms, ms == 0 ? " (nunca)" : "");
        } else if (sub == "mark") { g_markRx = sx::rxLog().size(); g_markTx = sx::txLog().size(); g_markT = sim::now(); }
        else if (sub == "report") { sx::sync(); printf("%s", sx::report().c_str()); }
        else if (sub == "selftest") { radioSelfTest(); }
        else if (sub == "kick") {        // la radio cae de recepcion sin que el firmware lo pida (brown-out, reinicio del chip...)
            std::string m; is >> m;
            if (m == "stdby") sx::forceMode(1); else if (m == "sleep") sx::forceMode(0);
            else { scriptError("radio kick necesita stdby|sleep"); return; }
            printf("  radio forzada a %s\n", m.c_str());
        }
        else if (sub == "log") {         // lista el destino de cada trama recibida desde la marca
            sx::sync();
            auto &rl = sx::rxLog();
            for (size_t i = g_markRx; i < rl.size(); i++)
                printf("  rx t=%.0f..%.0f ms  %zu B  %s\n", rl[i].start, rl[i].end, rl[i].len, sx::outcomeName(rl[i].outcome));
        }
        else scriptError("subcomando radio desconocido \"" + sub + "\" (ideal|peer|deepsleep|mark|report|log|selftest|kick)");
    }
    else if (cmd == "waitfor") {   // waitfor serial <minMs> <maxMs> <texto>: aparece en el log serie ENTRE minMs y maxMs
        // Ejecuta el firmware hasta que el texto aparezca en el log (solo lo nuevo, desde ahora) y
        // comprueba el instante en que lo hace. Sirve para fijar el ritmo de eventos periodicos
        // (balizas, reintentos) con cotas por ambos lados.
        std::string what; uint32_t mn = 0, mx = 0;
        is >> what;
        if (what == "clear") { g_waitSamples.clear(); return; }     // waitfor clear: olvida las muestras
        if (!(is >> mn >> mx) || what != "serial") { scriptError("waitfor necesita: serial <minMs> <maxMs> <texto>  (o: clear)"); return; }
        std::string txt = restAfter(line, 4);
        if (txt.empty()) { scriptError("waitfor necesita el texto a esperar"); return; }
        size_t startLen = sim::serialLog().size();
        uint32_t t0 = sim::now(), limit = t0 + mx + 2000;
        sim::setDeadline(limit + SLACK);
        bool found = false; uint32_t tFound = 0;
        try {
            sim::applyDue();
            while (sim::now() < limit) {
                uint32_t before = sim::now();
                loop();
                if (sim::now() == before) sim::advance(25);       // pasos de 25 ms: basta para cotas en segundos
                if (sim::serialLog().find(txt, startLen) != std::string::npos) { found = true; tFound = sim::now() - t0; break; }
            }
        } catch (sim::Timeout &) { fprintf(stderr, "[sim] aviso: espera bloqueante (deadline alcanzado)\n"); }
        sim::clearDeadline();
        char b[200];
        if (!found) snprintf(b, sizeof(b), "\"%s\" no aparece en %u ms (se esperaba entre %u y %u)", txt.c_str(), (unsigned)(limit - t0), (unsigned)mn, (unsigned)mx);
        else        snprintf(b, sizeof(b), "\"%s\" aparece a los %u ms (cota %u..%u)", txt.c_str(), (unsigned)tFound, (unsigned)mn, (unsigned)mx);
        check(found && tFound >= mn && tFound <= mx, b);
        if (found) g_waitSamples.push_back(tFound);
    }
    else if (cmd == "blind") {   // blind <ms>: el reloj avanza pero el firmware NO corre (esta ocupado en otra cosa)
        uint32_t ms = 0;
        if (!(is >> ms)) { scriptError("blind necesita los ms a avanzar"); return; }
        sim::advance(ms);
    }
    else if (cmd == "ff") { uint32_t ms = 0; if (!(is >> ms)) { scriptError("ff necesita los ms a avanzar"); return; } fastForward(ms); }
    else if (cmd == "in") {
        uint32_t off = 0; std::string what;
        if (!(is >> off >> what)) { scriptError("in necesita: in <ms> <accion> ..."); return; }
        uint32_t t = sim::now() + off;
        if (what == "morse" || what == "finish") {
            std::string val; is >> val; bool down;
            if (!parseUpDown(val, &down)) return;
            sim::scheduleAt(t, what == "morse" ? sim::EV_MORSE : sim::EV_FINISH, down ? 1 : 0);
        }
        else if (what == "pot") { int v; if (!(is >> v)) { scriptError("in <ms> pot necesita un valor"); return; } sim::scheduleAt(t, sim::EV_POT, v); }
        else if (what == "lora") {   // paquete LoRa crudo diferido: in <ms> lora <texto>
            // Como "lora rx" pero entregado en el instante pedido, incluso mientras el
            // firmware esta dentro de un bucle de juego (p.ej. los pings "HR" de HippoRadar,
            // que su rxTick lee pero backgroundTick descartaria si llegaran fuera del juego).
            std::string txt = restAfter(line, 3);
            String enc = SimpleCrypto_encrypt(String(txt.c_str()));
            if (!encryptedOk(enc)) return;
            sim::scheduleAt(t, sim::EV_INJECT, deferPacket(std::string(enc.c_str(), enc.length())));
        }
        else if (what == "chatack") {   // ACK diferido: in <ms> chatack <destino> <msgId>
            int target = 0, mid = 0; is >> target >> mid;
            String p; p += (char)0x06; p += (char)target; p += (char)mid;
            String enc = SimpleCrypto_encrypt(p);
            sim::scheduleAt(t, sim::EV_INJECT, deferPacket(std::string(enc.c_str(), enc.length())));
        }
        else if (what == "chatmsg") {   // MENSAJE diferido: in <ms> chatmsg <emisor> <msgId> <texto>
            int sender = 0, mid = 0; is >> sender >> mid;
            std::string txt = restAfter(line, 5);
            String p; p += (char)0x01; p += (char)sender; p += (char)mid; p += String(txt.c_str());
            String enc = SimpleCrypto_encrypt(p);
            if (!encryptedOk(enc)) return;
            sim::scheduleAt(t, sim::EV_INJECT, deferPacket(std::string(enc.c_str(), enc.length())));
        }
        else if (what == "battery") {   // bateria diferida: in <ms> battery <pct>
            int pct = 100; is >> pct; sim::scheduleAt(t, sim::EV_BATTERY, pct * 1023 / 100);
        }
        else if (what == "presence") {  // baliza diferida: in <ms> presence <peer> [epoch] [batt] [gen]
            int peer = 0, batt = 100, gen = 0; long ep = 0; is >> peer >> ep >> batt >> gen;
            int flags = (batt <= 15) ? 1 : 0;
            sim::scheduleAt(t, sim::EV_INJECT, deferPacket(buildBeacon(peer, ep, batt, flags, gen)));
        }
        else if (what == "ttt") {        // tres en raya diferido: in <ms> ttt hello|state ...
            std::string sub; is >> sub;
            int peer = 0, ov = 0; std::string cells = "000000000";
            if (sub == "hello") is >> peer; else is >> peer >> cells >> ov;
            sim::scheduleAt(t, sim::EV_INJECT, deferPacket(buildTtt(sub, peer, cells, ov)));
        }
        else scriptError("accion desconocida en in: \"" + what + "\"");
    }
    else if (cmd == "reboot") { setup(); }
    else if (cmd == "reboot-cold") { sim::resetClock(); setup(); printf("  reboot en frio (millis=0)\n"); }
    else if (cmd == "battery") { int pct; if (!(is >> pct)) { scriptError("battery necesita un % 0-100"); return; } sim::setBatteryRaw(pct * 1023 / 100); printf("  bateria = %d%%\n", pct); }
    else if (cmd == "settime") { long ep = 0; if (!(is >> ep)) { scriptError("settime necesita segundos epoch"); return; } Clock_set((uint32_t)ep); printf("  reloj fijado a %ld (gen %d)\n", ep, (int)Clock_gen()); }
    else if (cmd == "presence") {   // baliza de un peer: presence <peerId> [epoch] [batt] [gen]
        int peer = 0, batt = 100, gen = 0; long ep = 0; is >> peer >> ep >> batt >> gen;
        int flags = (batt <= 15) ? 1 : 0;   // BEACON_FLAG_LOWBATT
        simLoraInject(buildBeacon(peer, ep, batt, flags, gen));
        printf("  baliza de #%d inyectada (epoch %ld, bat %d%%, gen %d)\n", peer, ep, batt, gen);
    }
    else if (cmd == "doodle") {      // dibujo de un peer: doodle <peerId> (24x24, un corazon)
        int peer = 0; is >> peer;
        String p; p += (char)0x04; p += (char)peer;
        for (int cy = 0; cy < 24; cy++) {
            unsigned char b[3] = {0, 0, 0};
            for (int cx = 0; cx < 24; cx++) {
                double x = (cx - 11.5) / 10.0, y = (9.5 - cy) / 10.0;   // curva del corazon
                double t = x * x + y * y - 1.0;
                if (t * t * t - x * x * y * y * y < 0.0) b[cx / 8] |= (1 << (7 - (cx % 8)));
            }
            p += (char)b[0]; p += (char)b[1]; p += (char)b[2];
        }
        String e = SimpleCrypto_encrypt(p);
        simLoraInject(std::string(e.c_str(), e.length()));
        printf("  dibujo de #%d inyectado\n", peer);
    }
    else if (cmd == "ttt") {         // tres en raya: ttt hello <peer> | ttt state <peer> <9digitos> <fin>
        std::string sub; is >> sub;
        int peer = 0, ov = 0; std::string cells = "000000000";
        if (sub == "hello") is >> peer; else is >> peer >> cells >> ov;
        simLoraInject(buildTtt(sub, peer, cells, ov));
        printf("  ttt %s de #%d\n", sub.c_str(), peer);
    }
    else if (cmd == "lora") {
        std::string sub; is >> sub;
        if (sub == "rx") {
            std::string txt = restAfter(line, 2);
            String enc = SimpleCrypto_encrypt(String(txt.c_str()));
            if (!encryptedOk(enc)) return;
            simLoraInject(std::string(enc.c_str(), enc.length()));
            printf("  LoRa RX inyectado: \"%s\"\n", txt.c_str());
        } else if (sub == "rxbad") {      // como rx, pero la trama llega CORRUPTA (ruido en el canal)
            std::string txt = restAfter(line, 2);
            String enc = SimpleCrypto_encrypt(String(txt.c_str()));
            if (!encryptedOk(enc)) return;
            sx::inject(std::string(enc.c_str(), enc.length()), -42, true);
            printf("  LoRa RX CORRUPTO inyectado: \"%s\"\n", txt.c_str());
        } else if (sub == "rxraw") {
            std::string hex;
            if (!(is >> hex)) { scriptError("lora rxraw necesita los bytes en hex"); return; }
            simLoraInject(hex);
        } else if (sub == "loopback") {
            std::string s; is >> s;
            if (s != "on" && s != "off") { scriptError("lora loopback necesita on|off"); return; }
            simLoraSetLoopback(s == "on");
        } else if (sub == "tx") {        // el FIRMWARE emite este mensaje por su capa de radio (escucha antes de hablar, etc.)
            std::string txt = restAfter(line, 2);
            if (txt.empty()) { scriptError("lora tx necesita el texto a emitir"); return; }
            bool ok = Lora_send(String(txt.c_str()));
            printf("  LoRa TX del firmware: \"%s\" (%s)\n", txt.c_str(), ok ? "emitido" : "no emitido");
        } else if (sub == "sent") {
            std::string last = simLoraLastSent();
            String dec = SimpleCrypto_decrypt(String(last.c_str()));
            printf("  LoRa TX (hex)=%s  descifrado=\"%s\"\n", last.c_str(), dec.c_str());
        }
        else scriptError("subcomando lora desconocido \"" + sub + "\" (rx|rxbad|rxraw|loopback|tx|sent)");
    }
    else if (cmd == "chatmsg") {   // inyecta un MENSAJE de chat de un peer: chatmsg <emisor> <msgId> <texto>
        int sender = 0, mid = 0; is >> sender >> mid;
        std::string txt = restAfter(line, 3);
        String p; p += (char)0x01; p += (char)sender; p += (char)mid; p += String(txt.c_str());
        String enc = SimpleCrypto_encrypt(p);
        if (!encryptedOk(enc)) return;
        simLoraInject(std::string(enc.c_str(), enc.length()));
        printf("  Chat MSG inyectado de #%d (msg %d): \"%s\"\n", sender, mid, txt.c_str());
    }
    else if (cmd == "chatack") {   // inyecta un ACK de un peer: chatack <destino> <msgId>
        int target = 0, mid = 0; is >> target >> mid;
        String p; p += (char)0x06; p += (char)target; p += (char)mid;
        String enc = SimpleCrypto_encrypt(p);
        simLoraInject(std::string(enc.c_str(), enc.length()));
        printf("  Chat ACK inyectado para #%d (msg %d)\n", target, mid);
    }
    else if (cmd == "screen") { simRenderTerminal(g_color); }
    else if (cmd == "text") { printf("  [texto pantalla] \"%s\"\n", display.simText().c_str()); }
    else if (cmd == "serial") { printf("%s", sim::serialLog().c_str()); }
    else if (cmd == "shot") { std::string n; is >> n; doShot(n); }
    else if (cmd == "print") { printf("  %s\n", restAfter(line, 1).c_str()); }
    else if (cmd == "reset-eeprom") { EEPROM.begin(2048); for (int i = 0; i < 2048; i++) EEPROM.write(i, 0xFF); EEPROM.commit(); setup(); }
    else if (cmd == "expect") {
        std::string sub; is >> sub;
        if ((sub == "text" || sub == "notext" || sub == "serial" || sub == "sent") && restAfter(line, 2).empty()) {
            scriptError("expect " + sub + " necesita el texto a buscar"); return;
        }
        if (sub == "text") { std::string n = restAfter(line, 2); check(contains(display.simText(), n), "pantalla contiene \"" + n + "\""); }
        else if (sub == "notext") { std::string n = restAfter(line, 2); check(!contains(display.simText(), n), "pantalla NO contiene \"" + n + "\""); }
        else if (sub == "serial") { std::string n = restAfter(line, 2); check(contains(sim::serialLog(), n), "serial contiene \"" + n + "\""); }
        else if (sub == "sent") {
            // Busca el texto en CUALQUIERA de los ultimos TX (no solo el ultimo): asi
            // una baliza de presencia transmitida despues no oculta el mensaje enviado.
            std::string n = restAfter(line, 2);
            auto ring = simLoraSentRing();
            bool found = false;
            for (auto it = ring.rbegin(); it != ring.rend() && !found; ++it) {
                String dec = SimpleCrypto_decrypt(String(it->c_str()));
                if (contains(std::string(dec.c_str(), dec.length()), n)) found = true;
            }
            check(found, "ultimo TX descifra y contiene \"" + n + "\"");
        }
        else if (sub == "rx") {       // expect rx heard|lost|crc|<motivo> [=|>=|<=] <n>   (desde "radio mark")
            std::string what, op = "=", nstr; is >> what >> nstr;
            if (nstr == "=" || nstr == ">=" || nstr == "<=") { op = nstr; is >> nstr; }
            if (what.empty() || nstr.empty()) { scriptError("expect rx necesita: heard|lost|crc|standby|sleep|tx|timeout|aborted|collision|mismatch|overrun [=|>=|<=] <n>"); return; }
            int want = atoi(nstr.c_str());
            sx::sync();                    // las tramas se resuelven al acceder al chip: ponerlo al dia
            auto &rl = sx::rxLog(); int n = 0;
            for (size_t i = g_markRx; i < rl.size(); i++) {
                sx::RxOutcome o = rl[i].outcome; bool hit = false;
                if (what == "heard") hit = (o == sx::RX_HEARD);
                else if (what == "crc") hit = (o == sx::RX_CRC_ERROR);
                else if (what == "lost") hit = (o >= sx::RX_LOST_STANDBY);
                else if (what == "standby") hit = (o == sx::RX_LOST_STANDBY);
                else if (what == "sleep") hit = (o == sx::RX_LOST_SLEEP);
                else if (what == "tx") hit = (o == sx::RX_LOST_TX);
                else if (what == "timeout") hit = (o == sx::RX_LOST_RXTIMEOUT);
                else if (what == "aborted") hit = (o == sx::RX_LOST_ABORTED);
                else if (what == "collision") hit = (o == sx::RX_LOST_COLLISION);
                else if (what == "mismatch") hit = (o == sx::RX_LOST_MISMATCH);
                else if (what == "overrun") hit = (o == sx::RX_OVERRUN);
                else { scriptError("expect rx: motivo desconocido \"" + what + "\""); return; }
                if (hit) n++;
            }
            bool ok = (op == "=") ? (n == want) : (op == ">=") ? (n >= want) : (n <= want);
            check(ok, "radio: tramas " + what + " desde la marca " + op + " " + std::to_string(want) + " (son " + std::to_string(n) + ")");
        }
        else if (sub == "tx") {        // expect tx <min> <max>: tramas emitidas desde "radio mark"
            int mn = 0, mx = 0;
            if (!(is >> mn >> mx)) { scriptError("expect tx necesita: <min> <max>"); return; }
            int n = (int)(sx::txLog().size() - g_markTx);
            check(n >= mn && n <= mx, "radio: tramas emitidas desde la marca entre " + std::to_string(mn) + " y " + std::to_string(mx) + " (son " + std::to_string(n) + ")");
        }
        else if (sub == "txstart") {   // expect txstart <min> <max>: la 1a emision desde "radio mark" empieza entre min y max ms despues de la marca
            int mn = 0, mx = 0;
            if (!(is >> mn >> mx)) { scriptError("expect txstart necesita: <min> <max>"); return; }
            if (sx::txLog().size() <= g_markTx) { check(false, "radio: ninguna emision desde la marca (se esperaba una entre " + std::to_string(mn) + " y " + std::to_string(mx) + " ms)"); return; }
            double dt = sx::txLog()[g_markTx].start - (double)g_markT;
            char b[160]; snprintf(b, sizeof(b), "radio: la 1a emision empieza %.0f ms despues de la marca (cota %d..%d)", dt, mn, mx);
            check(dt >= mn && dt <= mx, b);
        }
        else if (sub == "airtime") {   // expect airtime <maxPct> <ventanaMs>: ocupacion del canal por ESTE equipo en la ultima ventana
            double maxPct = 0; uint32_t win = 0;
            if (!(is >> maxPct >> win) || win == 0) { scriptError("expect airtime necesita: <maxPorcentaje> <ventanaMs>"); return; }
            double t1 = (double)sim::now(), t0 = t1 - (double)win;
            double pct = 100.0 * sx::txAirtimeBetween(t0, t1) / (double)win;
            char b[160]; snprintf(b, sizeof(b), "ocupacion del canal por este equipo en los ultimos %u ms: %.1f %% (maximo %.1f %%)", (unsigned)win, pct, maxPct);
            check(pct <= maxPct, b);
        }
        else if (sub == "listening") { // expect listening on|off: la radio esta en recepcion ahora mismo
            std::string st; is >> st;
            if (st != "on" && st != "off") { scriptError("expect listening necesita on|off"); return; }
            check(sx::listening() == (st == "on"), std::string("radio escuchando = ") + st);
        }
        else if (sub == "radiomode") { // expect radiomode sleep|stdby|tx|rxcont|rxsingle
            std::string m; is >> m; int want = -1;
            if (m == "sleep") want = 0; else if (m == "stdby") want = 1; else if (m == "tx") want = 3; else if (m == "rxcont") want = 5; else if (m == "rxsingle") want = 6;
            if (want < 0) { scriptError("expect radiomode necesita sleep|stdby|tx|rxcont|rxsingle"); return; }
            check(sx::currentMode() == want, "modo de la radio = " + m);
        }
        else if (sub == "spread") {   // expect spread <ms>: entre las esperas "waitfor" registradas, max-min >= ms
            uint32_t mn = 0;
            if (!(is >> mn)) { scriptError("expect spread necesita los ms"); return; }
            if (g_waitSamples.size() < 2) { scriptError("expect spread necesita al menos 2 esperas waitfor registradas"); return; }
            uint32_t lo = g_waitSamples[0], hi = g_waitSamples[0];
            for (uint32_t v : g_waitSamples) { if (v < lo) lo = v; if (v > hi) hi = v; }
            char b[160]; snprintf(b, sizeof(b), "las %u esperas varian %u ms (entre %u y %u; se pedian >= %u)", (unsigned)g_waitSamples.size(), (unsigned)(hi - lo), (unsigned)lo, (unsigned)hi, (unsigned)mn);
            check(hi - lo >= mn, b);
        }
        else if (sub == "panel") {    // expect panel on|off: el panel OLED esta encendido/apagado
            std::string st; is >> st;
            if (st != "on" && st != "off") { scriptError("expect panel necesita on|off"); return; }
            check(display.simPanelOn() == (st == "on"), "panel OLED " + st);
        }
        else if (sub == "beacon") {   // ultima BALIZA transmitida: expect beacon gen|batt|epoch|time <valor>
            std::string field, val; is >> field >> val;
            if (field.empty() || val.empty() || (field != "gen" && field != "batt" && field != "epoch" && field != "time")) {
                scriptError("expect beacon necesita: gen|batt|epoch|time <valor>"); return;
            }
            std::string got; bool found = false;
            auto ring = simLoraSentRing();
            for (auto it = ring.rbegin(); it != ring.rend() && !found; ++it) {
                String dec = SimpleCrypto_decrypt(String(*it));
                if (dec.length() < 7 || dec[0] != 0x02) continue;      // no es una baliza
                uint32_t ep = ((uint32_t)(uint8_t)dec[3] << 24) | ((uint32_t)(uint8_t)dec[4] << 16) |
                              ((uint32_t)(uint8_t)dec[5] << 8)  |  (uint32_t)(uint8_t)dec[6];
                int batt = dec.length() >= 8 ? (uint8_t)dec[7] : -1;
                int gen  = dec.length() >= 9 ? (uint8_t)dec[8] : 0;
                char b[32];
                if (field == "gen") snprintf(b, sizeof(b), "%d", gen);
                else if (field == "batt") snprintf(b, sizeof(b), "%d", batt);
                else if (field == "epoch") snprintf(b, sizeof(b), "%u", (unsigned)ep);
                else snprintf(b, sizeof(b), "%02u:%02u", (unsigned)((ep % 86400u) / 3600u), (unsigned)((ep % 3600u) / 60u));
                got = b; found = true;
            }
            if (!found) check(false, "hay una baliza transmitida (para comprobar " + field + ")");
            else check(got == val, "ultima baliza TX: " + field + " = " + val + " (es " + got + ")");
        }
        else if (sub == "pixel") {
            int x, y; std::string st;
            if (!(is >> x >> y >> st) || (st != "on" && st != "off")) { scriptError("expect pixel necesita: <x> <y> on|off"); return; }
            bool on = display.simPixel(x, y);
            check(on == (st == "on"), "pixel(" + std::to_string(x) + "," + std::to_string(y) + ")=" + st);
        }
        else scriptError("comprobacion desconocida \"expect " + sub + "\" (text|notext|serial|sent|beacon|panel|rx|tx|airtime|listening|radiomode|spread|pixel)");
    }
    else scriptError("comando desconocido \"" + cmd + "\"");
}

// ---------------------------------------------------------------------------
// Modo interactivo
//
// El firmware se ejecuta de forma continua, sin "deadline". El teclado se
// sondea desde dentro de cada delay() (mediante el pump-hook del motor), de modo
// que las esperas bloqueantes de los juegos/menus reciben las pulsaciones en
// directo. El render y el ritmo en tiempo real tambien viven en el pump.
// ---------------------------------------------------------------------------
// Sube la resolucion del temporizador de Windows a 1 ms (por defecto ~15 ms),
// para que los sleep cortos del ritmo en tiempo real no se pasen de largo.
#ifdef _WIN32
extern "C" __declspec(dllimport) unsigned int __stdcall timeBeginPeriod(unsigned int);
extern "C" __declspec(dllimport) unsigned int __stdcall timeEndPeriod(unsigned int);
#pragma comment(lib, "winmm.lib")
#endif

static int  g_iPot = 512;
static bool g_iRunning = true;
static std::chrono::steady_clock::time_point g_iLastRender;
static std::chrono::steady_clock::time_point g_iRealEpoch;   // ancla de tiempo real
static uint32_t g_iVirtEpoch = 0;                            // ancla de tiempo virtual

// --- Encuadre: alto/ancho REALES del terminal ---
// La API de consola da valores erroneos bajo Windows Terminal, asi que medimos
// con la secuencia DSR: mover el cursor al fondo-derecha y preguntar su posicion.
static int g_termRows = 0, g_termCols = 0;
static std::chrono::steady_clock::time_point g_lastProbe;

static void queryTermSize(int timeoutMs) {
    fputs("\x1b[9999;9999H\x1b[6n", stdout);   // ir al fondo-derecha y pedir posicion
    fflush(stdout);
    char buf[40]; int n = 0;
    auto t0 = std::chrono::steady_clock::now();
    while (n < 39) {
        if (_kbhit()) {
            int c = _getch();
            if (n == 0 && c != 0x1b) { _ungetch(c); break; }   // era una tecla, no la respuesta
            buf[n++] = (char)c;
            if (c == 'R') break;
        } else if (std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - t0).count() > timeoutMs) {
            break;   // sin respuesta (timeout)
        }
    }
    buf[n] = 0;
    const char *p = strchr(buf, '[');
    int r = 0, c = 0;
    if (p && sscanf(p + 1, "%d;%d", &r, &c) == 2 && r > 0) { g_termRows = r; g_termCols = c; }
}

// El cuadrado necesita 128 columnas y 68 filas (128x64 + estado + holgura). Si la
// ventana no llega, usamos braille para que NO se corte la parte de arriba. Si aun
// no se ha medido, cuadrado.
static bool needCompact() {
    if (g_termRows <= 0) return false;
    return g_termRows < 68 || g_termCols < 128;
}

// A tiempo real "de pared" el sim iba mas rapido que la placa (que tiene su
// propio coste de E/S por vuelta). Este factor calibra el ritmo contra el
// hardware real: ms virtuales que avanzan por cada ms real.
static const double VIRT_PER_REAL = 0.6;

static long long iRealMs() {
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - g_iRealEpoch).count();
}

// Reproduccion de teclas con guion (para pruebas/demos sin teclado): lista de
// (msVirtuales desde el arranque, accion). Accion = una tecla o "pot <valor>".
static std::vector<std::pair<uint32_t, std::string>> g_keyScript;
static size_t   g_ksIdx = 0;
static uint32_t g_iStart = 0;
static bool     g_keysScripted = false;
static double   g_pace = 0.0;        // --pace: ms virtuales por ms real como MAXIMO en modo --keys (0 = sin limite)
static bool     g_ksArmed = false;   // deadline de fin de guion ya armado (una sola vez)

// Programa una pulsacion (sin runFor: la aplica el avance del reloj del firmware).
static void scheduleTap(sim::EvKind k, uint32_t durMs) {
    uint32_t t0 = sim::now();
    sim::scheduleAt(t0, k, 1);
    sim::scheduleAt(t0 + durMs, k, 0);
}

static void pressKey(int c) {
    switch (c) {
        case 'm': scheduleTap(sim::EV_MORSE, 150); break;
        case 'n': scheduleTap(sim::EV_FINISH, 150); break;
        case 'M': scheduleTap(sim::EV_MORSE, 1700); break;   // pulsacion larga
        case 'N': scheduleTap(sim::EV_FINISH, 1700); break;
        case 'r': { String e = SimpleCrypto_encrypt(String("happy")); simLoraInject(std::string(e.c_str(), e.length())); } break;
        case 'q':
            g_iRunning = false;
            // Salir YA aunque el firmware este en una espera bloqueante (pantallas
            // "Enviado"/"Entregado", juegos...): el siguiente delay() supera el
            // deadline, lanza sim::Timeout y interactive() termina limpio.
            sim::setDeadline(sim::now());
            break;
    }
}

static void iHandleKeys() {
    while (_kbhit()) {
        int c = _getch();
        if (c == 0 || c == 0xE0) {           // teclas especiales (flechas)
            int k = _getch();
            if (k == 72) { g_iPot += 40; if (g_iPot > 1023) g_iPot = 1023; sim::setPot(g_iPot); }  // arriba
            else if (k == 80) { g_iPot -= 40; if (g_iPot < 0) g_iPot = 0; sim::setPot(g_iPot); }    // abajo
            continue;
        }
        pressKey(c);
    }
}

// Dispara las acciones del guion cuyo instante ya ha llegado. Al agotarse, arma
// un deadline para forzar la salida (incluso si el firmware esta bloqueado).
static void fireScriptedKeys() {
    while (g_ksIdx < g_keyScript.size() &&
           sim::now() - g_iStart >= g_keyScript[g_ksIdx].first) {
        std::string act = g_keyScript[g_ksIdx++].second;
        if (act.rfind("pot", 0) == 0) {
            int v = atoi(trim(act.substr(3)).c_str());
            g_iPot = v < 0 ? 0 : (v > 1023 ? 1023 : v);
            sim::setPot(g_iPot);
        } else if (act.rfind("shot", 0) == 0) {
            doShot(trim(act.substr(4)));            // captura BMP del frame actual
        } else if (act == "mdown") { sim::setMorse(true); }    // pulsacion mantenida (control fino)
        else if (act == "mup")     { sim::setMorse(false); }
        else if (act == "fdown")   { sim::setFinish(true); }
        else if (act == "fup")     { sim::setFinish(false); }
        else if (act.rfind("hr", 0) == 0) {                    // ping del companero con RSSI dado
            int rssi = atoi(trim(act.substr(2)).c_str());
            String p = String("HR"); p += (char)0xAB;          // id de companero ficticio
            String e = SimpleCrypto_encrypt(p);
            simLoraInject(std::string(e.c_str(), e.length()), rssi);
        }
        else if (act.rfind("cmsg", 0) == 0) {                  // mensaje de chat de un peer: cmsg <emisor> <msgId> <texto>
            std::istringstream as(act.substr(4));
            int sender = 0, mid = 0; as >> sender >> mid;
            std::string txt; std::getline(as, txt); txt = trim(txt);
            String p; p += (char)0x01; p += (char)sender; p += (char)mid; p += String(txt.c_str());
            String e = SimpleCrypto_encrypt(p);
            simLoraInject(std::string(e.c_str(), e.length()));
        }
        else if (act.rfind("cack", 0) == 0) {                  // ACK de un peer: cack <destino> <msgId>
            std::istringstream as(act.substr(4));
            int target = 0, mid = 0; as >> target >> mid;
            String p; p += (char)0x06; p += (char)target; p += (char)mid;
            String e = SimpleCrypto_encrypt(p);
            simLoraInject(std::string(e.c_str(), e.length()));
        }
        else if (!act.empty()) {
            pressKey((unsigned char)act[0]);
        }
    }
    if (g_ksIdx >= g_keyScript.size() && !g_ksArmed) {
        g_ksArmed = true;
        sim::setDeadline(sim::now() + 50);   // una sola vez: fuerza la salida del bloqueo
    }
}

static void iRender() {
    // Re-medir el terminal cada ~700 ms para detectar redimensionados. La primera
    // medida se hace en interactive() ANTES del primer frame, asi que aqui no hay
    // que medir en la primera vuelta (no hay cambio de modo al arrancar).
    static int lastCompact = -1;
    auto nowR = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(nowR - g_lastProbe).count() > 700) {
        g_lastProbe = nowR;
        queryTermSize(30);
    }
    bool compact = needCompact();
    if ((int)compact != lastCompact) {   // al cambiar de modo, limpiar restos del anterior
        printf("\x1b[2J");
        lastCompact = (int)compact;
    }
    printf("\x1b[H");   // cursor arriba (redibujado en el sitio)
    simRenderTerminal(g_color, compact);
    printf("  t=%lus   pot=%d        \n", (unsigned long)(sim::now() / 1000), sim::getPot());
    printf("  [m] Morse   [n] Finish   [Shift+M / Shift+N] pulsacion larga      \n");
    printf("  [Flecha arriba/abajo] potenciometro   [r] recibir LoRa   [q] salir\n");
    if (compact) printf("  (ventana baja: vista compacta; agrandala o reduce la fuente para pixeles cuadrados)\n");
    fflush(stdout);
}

// Llamado por el motor desde cada delay()/advance() (y por el bucle exterior en
// las iteraciones sin delay). Sondea teclado, redibuja a ~30 fps y marca el
// ritmo en tiempo real anclando el reloj virtual al reloj de pared.
static void interactivePump(uint32_t ms) {
    (void)ms;
    iHandleKeys();
    if (g_keysScripted) {                                  // modo prueba: rapido
        fireScriptedKeys();
        if (g_pace > 0.0 && !air_lockstep()) {
            // Con varios procesos en el "aire" compartido el tiempo virtual de cada uno debe
            // avanzar a un ritmo comparable: en modo rapido uno arranca unas decenas de ms
            // despues y ya ha pasado "toda su vida" (y la radio apagada no oye lo emitido).
            double ahead = (double)(sim::now() - g_iVirtEpoch) - g_pace * (double)iRealMs();
            if (ahead > 0.0) {
                double sl = ahead / g_pace; if (sl > 20.0) sl = 20.0;
                if (sl >= 1.0) std::this_thread::sleep_for(std::chrono::milliseconds((long long)sl));
            }
        }
        return;
    }

    auto nowR = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(nowR - g_iLastRender).count() >= 33) {
        g_iLastRender = nowR;
        iRender();
    }

    // El tiempo virtual sigue al real segun el factor de calibracion: el objetivo
    // es virt = VIRT_PER_REAL * real. Si el virtual va por delante, dormimos lo
    // justo para que el real lo alcance. Es auto-correctivo: si una espera del SO
    // se pasa de largo, la siguiente vuelta no dormira (sin acumular retraso).
    double virt  = (double)(sim::now() - g_iVirtEpoch);
    double real  = (double)iRealMs();
    double ahead = virt - VIRT_PER_REAL * real;
    if (ahead > 0.0) {
        double s = ahead / VIRT_PER_REAL;    // tiempo real que falta para alcanzar el objetivo
        if (s > 33.0) s = 33.0;              // nunca dormir mucho de golpe: teclado siempre vivo
        std::this_thread::sleep_for(std::chrono::milliseconds((long long)s));
        iHandleKeys();
    }
}

// Restaura el cursor si se sale con Ctrl+C (lo ocultamos durante el render).
static void iRestoreOnSignal(int) { fputs("\x1b[?25h", stdout); fflush(stdout); _exit(0); }

static void interactive() {
#ifdef _WIN32
    system("chcp 65001 > nul");
#endif
    if (!g_keysScripted) { printf("\x1b[?25l"); signal(SIGINT, iRestoreOnSignal); }   // ocultar cursor (evita parpadeos del DSR)
    printf("\x1b[2J");
    g_iPot = 512;
    sim::setPot(g_iPot);
    g_iRunning = true;
    g_iLastRender = std::chrono::steady_clock::now() - std::chrono::milliseconds(100);
    g_iStart = sim::now();
    g_ksIdx = 0;
    g_ksArmed = false;
    g_iRealEpoch = std::chrono::steady_clock::now();
    g_iVirtEpoch = sim::now();
    // Ajustar la consola (consola clasica): fija fuente y tamano para que quepa
    // el render cuadrado (128x68) sin tocar el zoom a mano. En Windows Terminal
    // es no-op y caemos a medir por DSR. Luego pintamos YA el primer frame para
    // que no se quede en negro hasta pulsar una tecla.
    g_lastProbe = std::chrono::steady_clock::now();
    if (!g_keysScripted) {
        int cc = 0, rr = 0;
        simSetupConsole(130, 70, &cc, &rr);
        if (cc >= 128 && rr >= 68) { g_termCols = cc; g_termRows = rr; }
        else {
            fputs("\x1b[8;70;132t", stdout); fflush(stdout);   // pedir tamano a Windows Terminal
            queryTermSize(150);
        }
        printf("\x1b[2J");
        iRender();          // primer frame inmediato
    }
    if (!g_keysScripted) timeBeginPeriod(1);   // temporizador fino (solo en interactivo real)
    sim::clearDeadline();                 // sin deadline: el firmware puede bloquearse esperando al usuario
    sim::setPumpHook(interactivePump);
    while (g_iRunning) {
        try {
            uint32_t before = sim::now();
            loop();
            if (sim::now() == before) {
                // El firmware no ha avanzado el reloj (idle). Hay que empujarlo.
                if (g_keysScripted) {
                    sim::advance(TICK);                     // modo prueba: rapido
                } else {
                    // Tiempo real: avanzar el virtual hasta el objetivo VIRT_PER_REAL*real,
                    // absorbiendo el coste de dibujar/computar (que consume real sin
                    // avanzar virtual). Asi las animaciones van a su velocidad natural.
                    double behind = VIRT_PER_REAL * (double)iRealMs() - (double)(sim::now() - g_iVirtEpoch);
                    if (behind < 3.0) {                     // al dia: siesta corta (no quemar CPU)
                        std::this_thread::sleep_for(std::chrono::milliseconds(3));
                        behind = VIRT_PER_REAL * (double)iRealMs() - (double)(sim::now() - g_iVirtEpoch);
                    }
                    if (behind < 1.0)  behind = 1.0;
                    if (behind > 50.0) behind = 50.0;       // no saltar mas de 50 ms de golpe
                    sim::advance((uint32_t)behind);
                }
            }
        } catch (sim::Timeout &) { break; }   // fin del guion (--keys) o tecla [q]: salir limpio
    }
    sim::setPumpHook(nullptr);
    sim::clearDeadline();
    if (!g_keysScripted) timeEndPeriod(1);
    if (!g_keysScripted) printf("\x1b[?25h");   // restaurar cursor
    if (g_keysScripted) {
        printf("\n[serial]\n%s\n", sim::serialLog().c_str());
        // Radio: que emitio este equipo y que le llego (y por que se perdio lo que no oyo). Es lo primero que
        // hay que mirar cuando una prueba con varios equipos falla.
        sx::sync();
        printf("\n[radio]\n%s", sx::report().c_str());
        for (auto &t : sx::txLog()) printf("  tx t=%.0f..%.0f ms  %zu B\n", t.start, t.end, t.payload.size());
        for (auto &r : sx::rxLog()) printf("  rx t=%.0f..%.0f ms  %zu B  %s\n", r.start, r.end, r.len, sx::outcomeName(r.outcome));
    }
    printf("\n");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
#ifdef _WIN32
    system("chcp 65001 > nul");   // consola en UTF-8 para los bloques del render
#endif

    // Directorio del ejecutable (sim/out) -> base para EEPROM y capturas.
    std::string base = ".";
    {
        char *pgm = nullptr;
        if (_get_pgmptr(&pgm) == 0 && pgm) {
            std::string p(pgm);
            size_t s = p.find_last_of("\\/");
            if (s != std::string::npos) base = p.substr(0, s);
        }
    }
    g_shotsDir = base;

    std::string scriptPath;
#ifdef _WIN32
    std::string eepromPath = base + "\\eeprom.bin";
#else
    std::string eepromPath = base + "/eeprom.bin";
#endif
    bool interactiveMode = false, fresh = false;
    std::string airDir, airNode;
    int airRssi = -50;
    int airNodes = 0;           // --nodes N: modo sincronizado con N procesos

    // Opciones que llevan valor: si falta, es un error de uso.
    static const char *withValue[] = { "--keys", "--eeprom", "--shots", "--scale", "--air", "--node", "--rssi", "--pace", "--radio", "--nodes" };
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        for (const char *o : withValue)
            if (a == o && i + 1 >= argc) { fprintf(stderr, "[sim] falta el valor de %s\n", o); return 2; }
        if (a == "--interactive") interactiveMode = true;
        else if (a == "--keys") {   // reproducir teclas con guion (pruebas/demos)
            std::ifstream kf(argv[++i]);
            if (!kf) { fprintf(stderr, "[sim] no puedo abrir el guion de teclas: %s\n", argv[i]); return 2; }
            std::string ln;
            while (std::getline(kf, ln)) {
                ln = trim(ln);
                if (ln.empty() || ln[0] == '#') continue;
                std::istringstream is(ln);
                uint32_t ms = 0; is >> ms;
                std::string rest; std::getline(is, rest);
                g_keyScript.push_back({ms, trim(rest)});
            }
            g_keysScripted = true;
            interactiveMode = true;
        }
        else if (a == "--color") g_color = true;
        else if (a == "--fresh") fresh = true;
        else if (a == "--eeprom") eepromPath = argv[++i];
        else if (a == "--shots") g_shotsDir = argv[++i];
        else if (a == "--scale") g_scale = atoi(argv[++i]);
        else if (a == "--air") airDir = argv[++i];     // directorio del "aire" compartido
        else if (a == "--node") airNode = argv[++i];   // etiqueta unica del dispositivo
        else if (a == "--rssi") airRssi = atoi(argv[++i]); // dBm con que oyen los demas
        else if (a == "--pace") g_pace = atof(argv[++i]);  // tope de velocidad en modo --keys (multi-dispositivo)
        else if (a == "--nodes") airNodes = atoi(argv[++i]);   // modo sincronizado: numero de procesos que comparten el tiempo
        else if (a == "--radio") {                          // modelo de radio: ideal (siempre escucha, sin tiempo en el aire) o realista
            std::string m = argv[++i];
            if (m == "ideal") sx::setIdeal(true);
            else if (m == "real") sx::setIdeal(false);
            else { fprintf(stderr, "[sim] --radio admite real|ideal (no %s)\n", m.c_str()); return 2; }
        }
        else if (!a.empty() && a[0] != '-') {
            if (!scriptPath.empty()) { fprintf(stderr, "[sim] solo se admite un script (%s y %s)\n", scriptPath.c_str(), a.c_str()); return 2; }
            scriptPath = a;
        }
        else { fprintf(stderr, "[sim] opcion desconocida: %s\n", a.c_str()); return 2; }
    }

    // Crear las carpetas de capturas y de la EEPROM si no existen: si no, las
    // capturas fallarian y la EEPROM no se guardaria (sin persistencia).
    {
        std::error_code ec;
        std::filesystem::create_directories(g_shotsDir, ec);
        std::filesystem::path ep(eepromPath);
        if (ep.has_parent_path()) std::filesystem::create_directories(ep.parent_path(), ec);
    }

    // Aire compartido entre procesos (comunicacion real multi-dispositivo).
    if (!airDir.empty()) {
        if (airNode.empty()) airNode = "p" + std::to_string(_getpid());
        air_init(airDir.c_str(), airNode.c_str(), airRssi);
        if (airNodes > 0) {
            if (!air_lockstep_enable(airNodes)) {
                fprintf(stderr, "[sim] --nodes %d: el modo sincronizado no esta disponible (solo Linux/macOS, 1..8 procesos)\n", airNodes);
                return 2;
            }
            atexit(air_lockstep_bye);                  // al terminar, los demas dejan de esperarnos
        }
    } else if (airNodes > 0) { fprintf(stderr, "[sim] --nodes necesita --air <directorio>\n"); return 2; }

    EEPROM.setBackingFile(eepromPath.c_str());
    if (fresh) { remove(eepromPath.c_str()); }

    sim::setInjectHook(injectDeferred);   // habilita "in <ms> chatack|chatmsg ..."
    {   // los "otros equipos" que inyectan los guiones emiten con los mismos parametros que el proyecto
        AirFrame &pf = sx::peerFrame();
        pf.freq = (long)LORA_FREQUENCY; pf.sf = LORA_SPREADING; pf.bw = (long)LORA_BANDWIDTH;
        pf.cr = 1; pf.preamble = 8; pf.crc = true; pf.sync = 0x12;
    }

    // Arranque del firmware
    setup();

    if (interactiveMode) { interactive(); return 0; }

    if (!scriptPath.empty()) {
        std::ifstream f(scriptPath);
        if (!f) { fprintf(stderr, "[sim] no puedo abrir script: %s\n", scriptPath.c_str()); return 2; }
        printf("=== Ejecutando script: %s ===\n", scriptPath.c_str());
        std::string line;
        while (std::getline(f, line)) { g_lineNo++; execLine(line); }
    } else {
        std::string line;
        while (std::getline(std::cin, line)) { g_lineNo++; execLine(line); }
    }

    printf("\n=== Resultado: %d PASS, %d FAIL ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
