#include "air_channel.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <thread>

#ifndef _WIN32
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

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

#ifndef _WIN32
// =====================================================================
//  Modo sincronizado: tablero en memoria compartida (fichero con mmap) con el reloj de cada proceso y un
//  anillo de tramas. Ver la cabecera.
// =====================================================================
namespace {
const uint32_t LS_MAGIC = 0x4C53544Bu;       // "LSTK"
const int      LS_MAXN  = 8;                 // procesos
const int      LS_RING  = 1024;              // tramas en vuelo (los procesos van a 1 ms unos de otros: sobra)

struct alignas(64) LsSlot {
    std::atomic<long long> t;                // ms del reloj comun COMPLETADOS por el proceso
    std::atomic<int>       state;            // 0 libre, 1 activo, 2 terminado
    int                    pid;
    char                   node[32];
};
struct LsEntry {
    std::atomic<unsigned long long> seq;     // indice+1 cuando la entrada esta completa
    int    from;
    int    rssi, sf, cr, preamble, crc, sync;
    long   freq, bw;
    double startMs;
    unsigned len;
    unsigned char data[256];
};
struct LsBoard {
    std::atomic<unsigned> magic;
    std::atomic<unsigned long long> nframes;
    LsSlot  slot[LS_MAXN];
    LsEntry ring[LS_RING];
};

LsBoard *g_ls = nullptr;
int      g_lsMe = -1;
unsigned long long g_lsCursor = 0;           // proxima trama del anillo por leer
std::vector<int> g_lsPeers;                  // ranuras de los demas

bool pidAlive(int pid) { return pid > 0 && (kill(pid, 0) == 0 || errno == EPERM); }
}  // namespace

bool air_lockstep() { return g_ls != nullptr; }

bool air_lockstep_enable(int nodes) {
    if (!g_enabled || nodes < 1 || nodes > LS_MAXN) return false;
    std::string path = g_dir + "/board.bin";
    int fd = open(path.c_str(), O_RDWR | O_CREAT, 0666);
    if (fd < 0) return false;
    flock(fd, LOCK_EX);                       // el registro de ranuras es exclusivo (varios procesos arrancan a la vez)
    struct stat st;
    fstat(fd, &st);
    if ((size_t)st.st_size < sizeof(LsBoard) && ftruncate(fd, sizeof(LsBoard)) != 0) { flock(fd, LOCK_UN); close(fd); return false; }
    void *m = mmap(nullptr, sizeof(LsBoard), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { flock(fd, LOCK_UN); close(fd); return false; }
    LsBoard *b = (LsBoard *)m;
    // Tablero de una ejecucion anterior (ninguna ranura viva): se reinicia entero.
    bool anyAlive = false;
    for (int i = 0; i < LS_MAXN; i++)
        if (b->slot[i].state.load() == 1 && pidAlive(b->slot[i].pid)) anyAlive = true;
    if (b->magic.load() != LS_MAGIC || !anyAlive) {
        memset((void *)b, 0, sizeof(LsBoard));
        b->magic.store(LS_MAGIC);
    }
    int me = -1;
    for (int i = 0; i < LS_MAXN && me < 0; i++)
        if (b->slot[i].state.load() == 0) me = i;
    if (me < 0) { flock(fd, LOCK_UN); close(fd); munmap(m, sizeof(LsBoard)); return false; }
    LsSlot &s = b->slot[me];
    s.t.store(0); s.pid = (int)getpid();
    snprintf(s.node, sizeof(s.node), "%s", g_node.c_str());
    s.state.store(1);
    flock(fd, LOCK_UN);
    close(fd);                                // el mapeo sigue valido
    g_ls = b; g_lsMe = me;
    g_lsCursor = b->nframes.load();           // solo lo emitido a partir de ahora
    // Esperar a que arranquen los N procesos (hasta 20 s de reloj real).
    auto t0 = std::chrono::steady_clock::now();
    while (true) {
        int n = 0;
        for (int i = 0; i < LS_MAXN; i++) if (b->slot[i].state.load() != 0) n++;
        if (n >= nodes) break;
        if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(20)) {
            fprintf(stderr, "[air] modo sincronizado: solo %d de %d procesos han arrancado\n", n, nodes);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    g_lsPeers.clear();
    for (int i = 0; i < LS_MAXN; i++) if (i != me && b->slot[i].state.load() != 0) g_lsPeers.push_back(i);
    return true;
}

void air_lockstep_tick(long long doneMs) {
    if (!g_ls) return;
    LsSlot &me = g_ls->slot[g_lsMe];
    me.t.store(doneMs, std::memory_order_release);
    for (int j : g_lsPeers) {
        LsSlot &o = g_ls->slot[j];
        unsigned spins = 0;
        auto t0 = std::chrono::steady_clock::now();
        while (o.t.load(std::memory_order_acquire) < doneMs && o.state.load(std::memory_order_acquire) == 1) {
            if (++spins >= 256) sched_yield();       // (primero espera activa breve, luego cede la CPU)
            if ((spins & 0xFFFF) == 0) {
                // El otro proceso murio sin avisar (kill) o esta colgado: no bloquear a todos para siempre.
                if (!pidAlive(o.pid)) { o.state.store(2); break; }
                if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(60)) {
                    fprintf(stderr, "[air] el proceso '%s' no avanza desde hace 60 s: se deja de esperarle\n", o.node);
                    o.state.store(2);
                    break;
                }
            }
        }
    }
}

void air_lockstep_bye() {
    if (!g_ls) return;
    g_ls->slot[g_lsMe].state.store(2, std::memory_order_release);
}

static void lsPublish(const AirFrame &fr) {
    unsigned long long idx = g_ls->nframes.fetch_add(1);
    LsEntry &e = g_ls->ring[idx % LS_RING];
    e.seq.store(0, std::memory_order_release);        // entrada en escritura (si el anillo da la vuelta)
    e.from = g_lsMe; e.rssi = g_rssi; e.sf = fr.sf; e.cr = fr.cr; e.preamble = fr.preamble;
    e.crc = fr.crc ? 1 : 0; e.sync = fr.sync; e.freq = fr.freq; e.bw = fr.bw; e.startMs = fr.startMs;
    e.len = (unsigned)std::min<size_t>(fr.payload.size(), sizeof(e.data));
    memcpy(e.data, fr.payload.data(), e.len);
    e.seq.store(idx + 1, std::memory_order_release);  // completa
}

static void lsPoll(std::vector<AirFrame> &out) {
    unsigned long long n = g_ls->nframes.load(std::memory_order_acquire);
    if (n - g_lsCursor > (unsigned long long)LS_RING) g_lsCursor = n - LS_RING;   // el anillo dio la vuelta: se perdieron las mas viejas
    std::vector<AirFrame> got;
    std::vector<int> from;
    while (g_lsCursor < n) {
        LsEntry &e = g_ls->ring[g_lsCursor % LS_RING];
        if (e.seq.load(std::memory_order_acquire) != g_lsCursor + 1) break;       // aun escribiendose: se vera en la siguiente consulta
        if (e.from != g_lsMe) {
            AirFrame f;
            f.payload.assign((const char *)e.data, e.len);
            f.rssi = e.rssi; f.sf = e.sf; f.cr = e.cr; f.preamble = e.preamble; f.crc = e.crc != 0;
            f.sync = e.sync; f.freq = e.freq; f.bw = e.bw; f.startMs = e.startMs;
            got.push_back(f); from.push_back(e.from);
        }
        g_lsCursor++;
    }
    // Orden determinista (los procesos escriben en el anillo en un orden que depende de la planificacion del SO)
    std::vector<size_t> idx(got.size());
    for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        if (got[a].startMs != got[b].startMs) return got[a].startMs < got[b].startMs;
        return from[a] < from[b];
    });
    for (size_t i : idx) out.push_back(got[i]);
}
#else
bool air_lockstep_enable(int) { return false; }
bool air_lockstep() { return false; }
void air_lockstep_tick(long long) {}
void air_lockstep_bye() {}
#endif

void air_publish(const AirFrame &fr) {
    if (!g_enabled) return;
#ifndef _WIN32
    if (g_ls) { lsPublish(fr); return; }
#endif

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
#ifndef _WIN32
    if (g_ls) { lsPoll(out); return; }
#endif

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
