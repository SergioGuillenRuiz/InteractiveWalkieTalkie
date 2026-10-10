#include "air_channel.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <chrono>

namespace fs = std::filesystem;

// Un paquete solo "esta en el aire" mientras se transmite. Pasado este tiempo
// (de reloj real) ya nadie puede oirlo: se borra del directorio, asi este no
// crece sin limite en sesiones largas (cada nodo lo recorre en cada sondeo).
static const uint64_t AIR_TTL_MS      = 10000;
static const uint64_t AIR_CLEANUP_MS  = 1000;    // barrido como mucho 1 vez/s

static bool        g_enabled = false;
static std::string g_dir;
static std::string g_node;
static int         g_rssi = -50;
static uint64_t    g_counter = 0;
static uint64_t    g_initMs = 0;       // instante de "encendido" de esta radio
static uint64_t    g_lastCleanup = 0;
static std::set<std::string> g_seen;   // ficheros de paquete ya consumidos/propios

static uint64_t nowMs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

// La etiqueta va en el nombre del fichero y en su contenido (separado por
// espacios): sin espacios ni separadores de ruta.
static std::string sanitizeNode(const char *node) {
    std::string s = (node && *node) ? node : "n";
    for (char &c : s)
        if (c == ' ' || c == '\t' || c == '/' || c == '\\' || c == '_' || c == ':') c = '-';
    return s;
}

// Instante de emision codificado en el nombre: pkt_<ms 20 digitos>_<nodo>_<n>.txt
static bool packetTime(const std::string &fn, uint64_t *ms) {
    if (fn.size() < 4 + 20 || fn.compare(0, 4, "pkt_") != 0) return false;
    *ms = std::strtoull(fn.substr(4, 20).c_str(), nullptr, 10);
    return true;
}

void air_init(const char *dir, const char *node, int rssi) {
    g_dir = dir ? dir : "";
    g_node = sanitizeNode(node);
    g_rssi = rssi;
    g_enabled = !g_dir.empty();
    g_initMs = nowMs();
    if (g_enabled) {
        std::error_code ec;
        fs::create_directories(g_dir, ec);
    }
}

bool air_enabled() { return g_enabled; }

static std::string toHex(const std::string &b) {
    static const char *H = "0123456789abcdef";
    std::string o; o.reserve(b.size() * 2);
    for (unsigned char c : b) { o += H[c >> 4]; o += H[c & 15]; }
    return o;
}
static bool fromHex(const std::string &h, std::string &out) {
    if (h.size() % 2) return false;
    out.clear();
    for (size_t i = 0; i < h.size(); i += 2) {
        auto v = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1); };
        int a = v(h[i]), b = v(h[i + 1]);
        if (a < 0 || b < 0) return false;
        out += (char)(a * 16 + b);
    }
    return true;
}

void air_publish(const AirFrame &fr) {
    if (!g_enabled) return;

    // Nombre unico y ordenable por tiempo: pkt_<ms>_<node>_<contador>.txt
    char name[256];
    snprintf(name, sizeof(name), "pkt_%020llu_%s_%06llu.txt",
             (unsigned long long)nowMs(), g_node.c_str(),
             (unsigned long long)g_counter++);

    fs::path tmp = fs::path(g_dir) / (std::string(name) + ".tmp");
    fs::path fin = fs::path(g_dir) / name;
    {
        std::ofstream f(tmp, std::ios::binary);
        // nodo rssi freq sf bw cr preambulo crc sync payload(hex)
        f << g_node << " " << g_rssi << " " << fr.freq << " " << fr.sf << " " << fr.bw << " " << fr.cr << " "
          << fr.preamble << " " << (fr.crc ? 1 : 0) << " " << fr.sync << " " << toHex(fr.payload) << "\n";
    }
    std::error_code ec;
    fs::rename(tmp, fin, ec);   // rename atomico: nadie ve un fichero a medias
    g_seen.insert(name);        // no recibir lo propio
}

// Borra los paquetes (y .tmp huerfanos) que ya han salido del aire y olvida los
// nombres vistos que ya no pueden volver a aparecer.
static void cleanup(uint64_t now) {
    if (now - g_lastCleanup < AIR_CLEANUP_MS) return;
    g_lastCleanup = now;
    std::error_code ec;
    for (auto &e : fs::directory_iterator(g_dir, ec)) {
        if (ec) break;
        std::string fn = e.path().filename().string();
        uint64_t t;
        if (packetTime(fn, &t) && t + AIR_TTL_MS < now) {
            std::error_code rec;
            fs::remove(e.path(), rec);   // si otro nodo ya lo borro, da igual
        }
    }
    for (auto it = g_seen.begin(); it != g_seen.end();) {
        uint64_t t;
        if (packetTime(*it, &t) && t + 2 * AIR_TTL_MS < now) it = g_seen.erase(it);
        else ++it;
    }
}

void air_poll(std::vector<AirFrame> &out) {
    if (!g_enabled) return;

    uint64_t now = nowMs();
    cleanup(now);

    std::error_code ec;
    std::vector<std::string> names;
    for (auto &e : fs::directory_iterator(g_dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec)) continue;
        std::string fn = e.path().filename().string();
        if (fn.size() < 4 || fn.compare(0, 4, "pkt_") != 0) continue;
        if (fn.size() >= 4 && fn.substr(fn.size() - 4) == ".tmp") continue;
        if (g_seen.count(fn)) continue;
        uint64_t t;
        if (!packetTime(fn, &t) || t < g_initMs || t + AIR_TTL_MS < now) {
            // Emitido antes de encender esta radio (o ya fuera del aire): una
            // radio real no puede oirlo. Se ignora (y no se vuelve a mirar).
            g_seen.insert(fn);
            continue;
        }
        names.push_back(fn);
    }
    std::sort(names.begin(), names.end());   // por nombre == por tiempo de emision

    for (auto &fn : names) {
        g_seen.insert(fn);
        std::ifstream f(fs::path(g_dir) / fn, std::ios::binary);
        std::string node, hex; int rssi = -50, crc = 1;
        AirFrame fr;
        if (f >> node >> rssi >> fr.freq >> fr.sf >> fr.bw >> fr.cr >> fr.preamble >> crc >> fr.sync >> hex) {
            if (node == g_node) continue;    // half-duplex: no oigo lo que yo emito
            if (!fromHex(hex, fr.payload)) continue;
            fr.rssi = rssi; fr.crc = (crc != 0);
            out.push_back(fr);
        }
    }
}
