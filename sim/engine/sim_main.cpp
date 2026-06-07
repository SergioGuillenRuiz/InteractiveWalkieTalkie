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
#include <conio.h>
#include <thread>
#include <chrono>

#include "sim_state.h"
#include "framebuffer.h"
#include "EEPROM.h"
#include "SimpleCrypto.h"      // String SimpleCrypto_encrypt/decrypt
#include <Adafruit_SH110X.h>   // tipo del display

#undef min
#undef max
#undef abs
#undef constrain

// Firmware
extern void setup();
extern void loop();
extern Adafruit_SH1107 display;   // definido en src/Display.cpp

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

// ---------------------------------------------------------------------------
// Motor de ejecución
// ---------------------------------------------------------------------------
static void runFor(uint32_t ms) {
    uint32_t target = sim::now() + ms;
    sim::setDeadline(target + SLACK);
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

// ---------------------------------------------------------------------------
// Intérprete de comandos
// ---------------------------------------------------------------------------
static sim::EvKind btnKind(const std::string &b) {
    return (b == "finish") ? sim::EV_FINISH : sim::EV_MORSE;
}

static void doShot(const std::string &nameArg) {
    char path[512];
    if (nameArg.empty())
        snprintf(path, sizeof(path), "%s/shot_%03d.bmp", g_shotsDir.c_str(), ++g_shotN);
    else
        snprintf(path, sizeof(path), "%s/%s%s", g_shotsDir.c_str(), nameArg.c_str(),
                 contains(nameArg, ".bmp") ? "" : ".bmp");
    if (simSaveBMP(path, g_scale)) printf("  captura -> %s\n", path);
    else printf("  ERROR guardando captura %s\n", path);
}

static void execLine(const std::string &raw) {
    std::string line = trim(raw);
    if (line.empty() || line[0] == '#') return;
    // Comentarios en linea: cortar a partir de " #"
    size_t cpos = line.find(" #");
    if (cpos != std::string::npos) line = trim(line.substr(0, cpos));
    if (line.empty()) return;

    std::istringstream is(line);
    std::string cmd; is >> cmd;

    if (cmd == "pot") { int v; is >> v; sim::setPot(v); }
    else if (cmd == "pot%") { int p; is >> p; sim::setPot(p * 1023 / 100); }
    else if (cmd == "morse") { std::string s; is >> s; sim::setMorse(s == "down"); }
    else if (cmd == "finish") { std::string s; is >> s; sim::setFinish(s == "down"); }
    else if (cmd == "tap") { std::string b; uint32_t ms = 150; is >> b; if (!(is >> ms)) ms = 150; tap(btnKind(b), ms); }
    else if (cmd == "hold") { std::string b; uint32_t ms = 1000; is >> b >> ms; holdRelease(btnKind(b), ms); }
    else if (cmd == "wait" || cmd == "run") { uint32_t ms = 0; is >> ms; runFor(ms); }
    else if (cmd == "ff") { uint32_t ms = 0; is >> ms; fastForward(ms); }
    else if (cmd == "in") {
        uint32_t off = 0; std::string what, val; is >> off >> what >> val;
        uint32_t t = sim::now() + off;
        if (what == "morse")  sim::scheduleAt(t, sim::EV_MORSE, val == "down" ? 1 : 0);
        else if (what == "finish") sim::scheduleAt(t, sim::EV_FINISH, val == "down" ? 1 : 0);
        else if (what == "pot") sim::scheduleAt(t, sim::EV_POT, atoi(val.c_str()));
    }
    else if (cmd == "reboot") { setup(); }
    else if (cmd == "lora") {
        std::string sub; is >> sub;
        if (sub == "rx") {
            std::string txt = restAfter(line, 2);
            String enc = SimpleCrypto_encrypt(String(txt.c_str()));
            simLoraInject(std::string(enc.c_str(), enc.length()));
            printf("  LoRa RX inyectado: \"%s\"\n", txt.c_str());
        } else if (sub == "rxraw") {
            std::string hex; is >> hex; simLoraInject(hex);
        } else if (sub == "loopback") {
            std::string s; is >> s; simLoraSetLoopback(s == "on");
        } else if (sub == "sent") {
            std::string last = simLoraLastSent();
            String dec = SimpleCrypto_decrypt(String(last.c_str()));
            printf("  LoRa TX (hex)=%s  descifrado=\"%s\"\n", last.c_str(), dec.c_str());
        }
    }
    else if (cmd == "screen") { simRenderTerminal(g_color); }
    else if (cmd == "text") { printf("  [texto pantalla] \"%s\"\n", display.simText().c_str()); }
    else if (cmd == "serial") { printf("%s", sim::serialLog().c_str()); }
    else if (cmd == "shot") { std::string n; is >> n; doShot(n); }
    else if (cmd == "print") { printf("  %s\n", restAfter(line, 1).c_str()); }
    else if (cmd == "reset-eeprom") { EEPROM.begin(2048); for (int i = 0; i < 2048; i++) EEPROM.write(i, 0xFF); EEPROM.commit(); setup(); }
    else if (cmd == "expect") {
        std::string sub; is >> sub;
        if (sub == "text") { std::string n = restAfter(line, 2); check(contains(display.simText(), n), "pantalla contiene \"" + n + "\""); }
        else if (sub == "notext") { std::string n = restAfter(line, 2); check(!contains(display.simText(), n), "pantalla NO contiene \"" + n + "\""); }
        else if (sub == "serial") { std::string n = restAfter(line, 2); check(contains(sim::serialLog(), n), "serial contiene \"" + n + "\""); }
        else if (sub == "sent") { std::string n = restAfter(line, 2); String dec = SimpleCrypto_decrypt(String(simLoraLastSent().c_str())); check(contains(std::string(dec.c_str()), n), "ultimo TX descifra y contiene \"" + n + "\""); }
        else if (sub == "pixel") { int x, y; std::string st; is >> x >> y >> st; bool on = display.simPixel(x, y); check(on == (st == "on"), "pixel(" + std::to_string(x) + "," + std::to_string(y) + ")=" + st); }
    }
    else { fprintf(stderr, "[sim] comando desconocido: %s\n", cmd.c_str()); }
}

// ---------------------------------------------------------------------------
// Modo interactivo
// ---------------------------------------------------------------------------
static void interactive() {
    system("chcp 65001 > nul");
    printf("\x1b[2J");
    int pot = 512;
    bool running = true;
    printf("Controles: [m]=tap Morse  [n]=tap Finish  [M]/[N]=mantener  "
           "[j/k]=pot -/+  [r]=LoRa RX  [q]=salir\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    while (running) {
        while (_kbhit()) {
            int c = _getch();
            switch (c) {
                case 'm': tap(sim::EV_MORSE, 150); break;
                case 'n': tap(sim::EV_FINISH, 150); break;
                case 'M': tap(sim::EV_MORSE, 1700); break;   // pulsación larga
                case 'N': tap(sim::EV_FINISH, 1700); break;
                case 'j': pot = pot > 30 ? pot - 30 : 0; sim::setPot(pot); break;
                case 'k': pot = pot < 993 ? pot + 30 : 1023; sim::setPot(pot); break;
                case 'r': { String e = SimpleCrypto_encrypt(String("happy")); simLoraInject(std::string(e.c_str(), e.length())); } break;
                case 'q': running = false; break;
            }
        }
        runFor(30);
        printf("\x1b[H");   // cursor arriba
        simRenderTerminal(g_color);
        printf(" t=%lus  pot=%d   (q para salir)      \n", (unsigned long)(sim::now() / 1000), sim::getPot());
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    system("chcp 65001 > nul");   // consola en UTF-8 para los bloques del render

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
    std::string eepromPath = base + "\\eeprom.bin";
    bool interactiveMode = false, fresh = false;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--interactive") interactiveMode = true;
        else if (a == "--color") g_color = true;
        else if (a == "--fresh") fresh = true;
        else if (a == "--eeprom" && i + 1 < argc) eepromPath = argv[++i];
        else if (a == "--shots" && i + 1 < argc) g_shotsDir = argv[++i];
        else if (a == "--scale" && i + 1 < argc) g_scale = atoi(argv[++i]);
        else if (!a.empty() && a[0] != '-') scriptPath = a;
    }

    EEPROM.setBackingFile(eepromPath.c_str());
    if (fresh) { remove(eepromPath.c_str()); }

    // Arranque del firmware
    setup();

    if (interactiveMode) { interactive(); return 0; }

    if (!scriptPath.empty()) {
        std::ifstream f(scriptPath);
        if (!f) { fprintf(stderr, "[sim] no puedo abrir script: %s\n", scriptPath.c_str()); return 2; }
        printf("=== Ejecutando script: %s ===\n", scriptPath.c_str());
        std::string line;
        while (std::getline(f, line)) execLine(line);
    } else {
        std::string line;
        while (std::getline(std::cin, line)) execLine(line);
    }

    printf("\n=== Resultado: %d PASS, %d FAIL ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
