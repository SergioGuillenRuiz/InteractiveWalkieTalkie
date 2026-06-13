#include "air_channel.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <chrono>

namespace fs = std::filesystem;

static bool        g_enabled = false;
static std::string g_dir;
static std::string g_node;
static int         g_rssi = -50;
static uint64_t    g_counter = 0;
static std::set<std::string> g_seen;   // ficheros de paquete ya consumidos/propios

void air_init(const char *dir, const char *node, int rssi) {
    g_dir = dir ? dir : "";
    g_node = node ? node : "n";
    g_rssi = rssi;
    g_enabled = !g_dir.empty();
    if (g_enabled) {
        std::error_code ec;
        fs::create_directories(g_dir, ec);
    }
}

bool air_enabled() { return g_enabled; }

static uint64_t nowMs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

void air_publish(const std::string &payloadHex) {
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
        f << g_node << " " << g_rssi << " " << payloadHex << "\n";
    }
    std::error_code ec;
    fs::rename(tmp, fin, ec);   // rename atomico: nadie ve un fichero a medias
    g_seen.insert(name);        // no recibir lo propio
}

void air_poll(std::vector<std::pair<std::string, int>> &out) {
    if (!g_enabled) return;

    std::error_code ec;
    std::vector<std::string> names;
    for (auto &e : fs::directory_iterator(g_dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec)) continue;
        std::string fn = e.path().filename().string();
        if (fn.size() < 4 || fn.compare(0, 4, "pkt_") != 0) continue;
        if (fn.size() >= 4 && fn.substr(fn.size() - 4) == ".tmp") continue;
        if (g_seen.count(fn)) continue;
        names.push_back(fn);
    }
    std::sort(names.begin(), names.end());   // por nombre == por tiempo de emision

    for (auto &fn : names) {
        g_seen.insert(fn);
        std::ifstream f(fs::path(g_dir) / fn, std::ios::binary);
        std::string node, hex; int rssi = -50;
        if (f >> node >> rssi >> hex) {
            if (node == g_node) continue;    // half-duplex: no oigo lo que yo emito
            out.push_back({hex, rssi});
        }
    }
}
