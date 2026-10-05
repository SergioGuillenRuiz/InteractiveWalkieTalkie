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

// Construye una baliza de presencia cifrada: 0x02 | peer | flags | epoch(4) | batt.
static std::string buildBeacon(int peer, long epoch, int batt, int flags) {
    String p;
    p += (char)0x02;
    p += (char)peer;
    p += (char)flags;
    p += (char)((epoch >> 24) & 0xFF);
    p += (char)((epoch >> 16) & 0xFF);
    p += (char)((epoch >> 8) & 0xFF);
    p += (char)(epoch & 0xFF);
    p += (char)batt;
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
    // Comentarios en linea: cortar a partir de " #"
    size_t cpos = line.find(" #");
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
        else if (what == "presence") {  // baliza diferida: in <ms> presence <peer> [epoch] [batt]
            int peer = 0, batt = 100; long ep = 0; is >> peer >> ep >> batt;
            int flags = (batt <= 15) ? 1 : 0;
            sim::scheduleAt(t, sim::EV_INJECT, deferPacket(buildBeacon(peer, ep, batt, flags)));
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
    else if (cmd == "settime") { long ep = 0; if (!(is >> ep)) { scriptError("settime necesita segundos epoch"); return; } Clock_set((uint32_t)ep); printf("  reloj fijado a %ld\n", ep); }
    else if (cmd == "presence") {   // baliza de un peer: presence <peerId> [epoch] [batt]
        int peer = 0, batt = 100; long ep = 0; is >> peer >> ep >> batt;
        int flags = (batt <= 15) ? 1 : 0;   // BEACON_FLAG_LOWBATT
        simLoraInject(buildBeacon(peer, ep, batt, flags));
        printf("  baliza de #%d inyectada (epoch %ld, bat %d%%)\n", peer, ep, batt);
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
        } else if (sub == "rxraw") {
            std::string hex;
            if (!(is >> hex)) { scriptError("lora rxraw necesita los bytes en hex"); return; }
            simLoraInject(hex);
        } else if (sub == "loopback") {
            std::string s; is >> s;
            if (s != "on" && s != "off") { scriptError("lora loopback necesita on|off"); return; }
            simLoraSetLoopback(s == "on");
        } else if (sub == "sent") {
            std::string last = simLoraLastSent();
            String dec = SimpleCrypto_decrypt(String(last.c_str()));
            printf("  LoRa TX (hex)=%s  descifrado=\"%s\"\n", last.c_str(), dec.c_str());
        }
        else scriptError("subcomando lora desconocido \"" + sub + "\" (rx|rxraw|loopback|sent)");
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
        else if (sub == "pixel") {
            int x, y; std::string st;
            if (!(is >> x >> y >> st) || (st != "on" && st != "off")) { scriptError("expect pixel necesita: <x> <y> on|off"); return; }
            bool on = display.simPixel(x, y);
            check(on == (st == "on"), "pixel(" + std::to_string(x) + "," + std::to_string(y) + ")=" + st);
        }
        else scriptError("comprobacion desconocida \"expect " + sub + "\" (text|notext|serial|sent|pixel)");
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
    if (g_keysScripted) { fireScriptedKeys(); return; }   // modo prueba: rapido

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
    if (g_keysScripted) printf("\n[serial]\n%s\n", sim::serialLog().c_str());
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

    // Opciones que llevan valor: si falta, es un error de uso.
    static const char *withValue[] = { "--keys", "--eeprom", "--shots", "--scale", "--air", "--node", "--rssi" };
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
    }

    EEPROM.setBackingFile(eepromPath.c_str());
    if (fresh) { remove(eepromPath.c_str()); }

    sim::setInjectHook(injectDeferred);   // habilita "in <ms> chatack|chatmsg ..."

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
