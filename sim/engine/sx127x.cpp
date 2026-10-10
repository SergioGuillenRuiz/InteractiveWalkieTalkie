// Modelo del chip LoRa SX1276 a nivel de registros (ver sx127x.h).
#include "sx127x.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "air_channel.h"
#include "sim_state.h"

namespace {

// ---------------- Registros (los que toca la libreria o importan al modelo) ----------------
enum {
    R_FIFO = 0x00, R_OPMODE = 0x01, R_FRF_MSB = 0x06, R_FRF_MID = 0x07, R_FRF_LSB = 0x08,
    R_PA_CONFIG = 0x09, R_OCP = 0x0B, R_LNA = 0x0C, R_FIFO_ADDR_PTR = 0x0D, R_FIFO_TX_BASE = 0x0E,
    R_FIFO_RX_BASE = 0x0F, R_FIFO_RX_CURRENT = 0x10, R_IRQ_MASK = 0x11, R_IRQ_FLAGS = 0x12,
    R_RX_NB_BYTES = 0x13, R_PKT_SNR = 0x19, R_PKT_RSSI = 0x1A, R_RSSI = 0x1B, R_MC1 = 0x1D,
    R_MC2 = 0x1E, R_SYMB_TIMEOUT_LSB = 0x1F, R_PREAMBLE_MSB = 0x20, R_PREAMBLE_LSB = 0x21,
    R_PAYLOAD_LENGTH = 0x22, R_MC3 = 0x26, R_SYNC_WORD = 0x39, R_DIO_MAPPING1 = 0x40,
    R_VERSION = 0x42, R_PA_DAC = 0x4D
};
const int MODE_SLEEP = 0, MODE_STDBY = 1, MODE_TX = 3, MODE_RXCONT = 5, MODE_RXSINGLE = 6;
const uint8_t IRQ_RXTIMEOUT = 0x80, IRQ_RXDONE = 0x40, IRQ_CRCERR = 0x20, IRQ_VALIDHDR = 0x10, IRQ_TXDONE = 0x08;

// Simbolos de preambulo que el receptor necesita ver para engancharse (deteccion de preambulo).
const int RX_LOCK_SYMS = 6;
// RegModemStat "senal detectada" (lo que usa el firmware para escuchar antes de hablar) se activa antes que el
// enganche completo: a los pocos simbolos de preambulo, como el CAD del chip. Antes de eso nadie ve que otro emite.
const int RX_DETECT_SYMS = 3;

// Corrientes tipicas del datasheet (mA) para la estimacion de consumo.
const double MA_SLEEP = 0.0002, MA_STDBY = 1.6, MA_TX17 = 87.0, MA_TX20 = 120.0, MA_RX = 11.5;

struct Frame {
    AirFrame p;
    double start = 0, lockAt = 0, end = 0;
    bool corrupt = false;
    bool done = false;        // ya tratada (oida o perdida)
    size_t rec = 0;           // indice en rxLog
};

struct Chip {
    uint8_t reg[128];
    uint8_t fifo[256];
    uint8_t fifoRx[256];             // solo modo ideal: lo recibido va aparte (el chip real comparte FIFO con TX)
    // SPI
    bool nssLow = false;
    int  spiPhase = 0;
    uint8_t spiAddr = 0;
    bool spiWrite = false;
    // estado
    double cur = 0;                  // instante hasta el que se ha procesado el modelo
    int  mode = MODE_STDBY;
    bool lora = false;
    uint8_t irq = 0;
    // TX
    double txStart = 0, txEnd = 0;
    // RX
    double rxDeadline = 1e300;       // RX unico: expira a esta hora si no se engancha ninguna trama
    int  rxPtr = 0;
    Frame *locked = nullptr;
    bool stdbyByTimeout = false;     // el ultimo paso a STDBY fue por expirar el RX unico
    size_t unreadRec = (size_t)-1;   // trama entregada cuyo RxDone aun no se ha borrado (por si la pisa otra)
    // contabilidad por modo
    double modeSince = 0;
    double modeMs[8] = {0};
    double txPowerMsHigh = 0;        // ms emitiendo con +20 dBm (PA_DAC=0x87), para la estimacion
};

Chip c;
std::deque<Frame> frames;
std::vector<sx::TxRecord> g_tx;
std::vector<sx::RxRecord> g_rx;
bool g_ideal = false;            // por defecto el modelo realista; --radio ideal / "radio ideal on" dan el comportamiento antiguo
bool g_loopback = false;
bool g_polite = true;            // ver sx::setPolite()
AirFrame g_peer;                 // parametros por defecto de las tramas inyectadas
double g_lastAirPoll = -1;

double nowMs() { return (double)sim::now(); }

// ---------------- Parametros leidos de los registros ----------------
long bwFromReg(int v) {
    static const long T[10] = { 7800, 10400, 15600, 20800, 31250, 41700, 62500, 125000, 250000, 500000 };
    return (v >= 0 && v < 10) ? T[v] : 125000;
}
int  curSf()   { return c.reg[R_MC2] >> 4; }
long curBw()   { return bwFromReg(c.reg[R_MC1] >> 4); }
int  curCr()   { return (c.reg[R_MC1] >> 1) & 7; }
bool curCrc()  { return (c.reg[R_MC2] & 0x04) != 0; }
bool curImplicit() { return (c.reg[R_MC1] & 1) != 0; }
int  curPreamble() { return (c.reg[R_PREAMBLE_MSB] << 8) | c.reg[R_PREAMBLE_LSB]; }
long curFreq() {
    uint64_t frf = ((uint64_t)c.reg[R_FRF_MSB] << 16) | ((uint64_t)c.reg[R_FRF_MID] << 8) | c.reg[R_FRF_LSB];
    return (long)((frf * 32000000ULL) >> 19);
}
double symMs(int sf, long bw) { return (double)(1 << sf) / (double)bw * 1000.0; }

// ---------------- Tramas ----------------
Frame mkFrame(const AirFrame &p, double start, bool corrupt) {
    Frame f;
    f.p = p;
    f.start = start;
    f.corrupt = corrupt;
    double tSym = symMs(p.sf, p.bw);
    if (g_ideal) { f.lockAt = start; f.end = start; }
    else {
        f.lockAt = start + std::min(p.preamble, RX_LOCK_SYMS) * tSym;
        f.end = start + sx::airtimeMs((int)p.payload.size(), p.sf, p.bw, p.cr, p.preamble, p.crc, false);
    }
    sx::RxRecord r; r.start = start; r.end = f.end; r.len = p.payload.size(); r.outcome = sx::RX_PENDING; r.p = p;
    g_rx.push_back(r);
    f.rec = g_rx.size() - 1;
    return f;
}

void finish(Frame &f, sx::RxOutcome o) { f.done = true; g_rx[f.rec].outcome = o; }

bool paramsMatch(const AirFrame &p) {
    if (g_ideal) return true;
    if (p.sf != curSf() || p.bw != curBw() || p.sync != c.reg[R_SYNC_WORD]) return false;
    long df = p.freq - curFreq(); if (df < 0) df = -df;
    return df <= (long)(curBw() / 4);
}

bool listeningAt(double t) {
    if (c.mode == MODE_RXCONT) return true;
    if (c.mode == MODE_RXSINGLE) return t <= c.rxDeadline;
    return false;
}

// ---------------- Cambio de modo ----------------
void account(double now) {
    c.modeMs[c.mode & 7] += now - c.modeSince;
    c.modeSince = now;
}

void setStdby(double now, bool byTimeout) {
    account(now);
    c.mode = MODE_STDBY;
    c.stdbyByTimeout = byTimeout;
}

void startTx(double now) {
    int len = c.reg[R_PAYLOAD_LENGTH];
    std::string payload;
    int a = c.reg[R_FIFO_TX_BASE];
    for (int i = 0; i < len; i++) payload += (char)c.fifo[(a + i) & 0xFF];

    AirFrame f;
    f.payload = payload;
    f.freq = curFreq(); f.sf = curSf(); f.bw = curBw(); f.cr = curCr(); f.preamble = curPreamble();
    f.crc = curCrc(); f.sync = c.reg[R_SYNC_WORD];
    double air = g_ideal ? 0.0 : sx::airtimeMs(len, f.sf, f.bw, f.cr, f.preamble, f.crc, curImplicit());
    f.startMs = air_lockstep() ? (double)sim::lockMs() : -1.0;   // modo sincronizado: instante comun de inicio
    c.txStart = now;
    c.txEnd = now + air;
    sx::TxRecord t; t.start = now; t.end = c.txEnd; t.payload = payload; t.p = f;
    g_tx.push_back(t);
    if (c.reg[R_PA_DAC] == 0x87) c.txPowerMsHigh += air;
    if (air_enabled()) air_publish(f);                   // difusion al resto de procesos
    if (g_loopback) {                                    // eco: se oye a si mismo al terminar de emitir
        AirFrame e = f; e.rssi = -42;
        frames.push_back(mkFrame(e, c.txEnd, false));
    }
}

void setMode(int nm, bool lora, double now) {
    c.lora = lora;
    int old = c.mode;
    if (nm == old) return;
    // salir de RX con una trama enganchada: se aborta (llegara corrupta/perdida)
    if ((old == MODE_RXCONT || old == MODE_RXSINGLE) && c.locked) {
        finish(*c.locked, sx::RX_LOST_ABORTED);
        c.locked = nullptr;
    }
    account(now);
    c.mode = nm;
    c.stdbyByTimeout = false;
    if (old == MODE_TX && nm != MODE_TX) c.txEnd = std::min(c.txEnd, now);     // emision cortada
    if (nm == MODE_RXCONT || nm == MODE_RXSINGLE) {
        c.rxPtr = c.reg[R_FIFO_RX_BASE];
        if (nm == MODE_RXSINGLE) {
            int symbTimeout = ((c.reg[R_MC2] & 3) << 8) | c.reg[R_SYMB_TIMEOUT_LSB];
            c.rxDeadline = now + symbTimeout * symMs(curSf(), curBw());
        } else c.rxDeadline = 1e300;
    }
    if (nm == MODE_TX && c.lora) startTx(now);
}

// ---------------- Avance del modelo en el tiempo ----------------
void deliver(Frame &f, double now) {
    // datos al FIFO a partir del puntero de RX; RxDone (+CRC error si la trama esta corrupta)
    std::string data = f.p.payload;
    if (f.corrupt && !f.p.crc && !data.empty()) data[data.size() / 2] ^= 0x5A;   // sin CRC nadie se entera
    int start = c.rxPtr & 0xFF;
    uint8_t *dst = g_ideal ? c.fifoRx : c.fifo;     // ideal: sin pisar lo que el firmware este preparando para TX
    for (size_t i = 0; i < data.size(); i++) dst[(c.rxPtr++) & 0xFF] = (uint8_t)data[i];
    c.reg[R_FIFO_RX_CURRENT] = (uint8_t)start;
    c.reg[R_RX_NB_BYTES] = (uint8_t)std::min<size_t>(data.size(), 255);
    int rs = f.p.rssi + 157; if (rs < 0) rs = 0; if (rs > 255) rs = 255;
    c.reg[R_PKT_RSSI] = (uint8_t)rs;
    c.reg[R_PKT_SNR] = (uint8_t)(int8_t)(9 * 4);
    if ((c.irq & IRQ_RXDONE) && c.unreadRec != (size_t)-1 && c.unreadRec < g_rx.size() &&
        g_rx[c.unreadRec].outcome == sx::RX_HEARD)
        g_rx[c.unreadRec].outcome = sx::RX_OVERRUN;     // el firmware aun no habia leido la anterior
    c.irq |= IRQ_RXDONE | IRQ_VALIDHDR;
    if (f.corrupt && f.p.crc) { c.irq |= IRQ_CRCERR; finish(f, sx::RX_CRC_ERROR); c.unreadRec = (size_t)-1; }
    else { finish(f, sx::RX_HEARD); c.unreadRec = f.rec; }
    if (c.mode == MODE_RXSINGLE) setStdby(now, false);   // el RX unico vuelve a STDBY al recibir
}

enum Ev { EV_NONE, EV_TXEND, EV_RXEND, EV_TIMEOUT, EV_LOCK };

void pollAir(double now) {
    if (!air_enabled() || now == g_lastAirPoll) return;
    g_lastAirPoll = now;
    std::vector<AirFrame> in;
    air_poll(in);
    for (auto &a : in) {
        // Modo libre: llega "ahora" (cada proceso lleva su reloj). Modo sincronizado: con su instante de
        // inicio exacto (como mucho 1 ms antes de ahora, porque los procesos avanzan a la par).
        double st = now;
        if (a.startMs >= 0) st = std::min(now, now - ((double)sim::lockMs() - a.startMs));
        frames.push_back(mkFrame(a, st, false));
    }
}

// Modo ideal: el firmware recibe UN paquete por consulta de las banderas (como el mock antiguo,
// que servia uno por parsePacket()), en orden de llegada y siempre que no tenga uno sin leer.
void idealDeliver(double now) {
    if (!g_ideal || (c.irq & IRQ_RXDONE)) return;
    for (auto &f : frames)
        if (!f.done && f.start <= now) { deliver(f, now); return; }
}

void advanceTo(double t) {
    pollAir(t);
    if (t < c.cur) t = c.cur;
    while (true) {
        double tn = 1e300; Ev kind = EV_NONE; Frame *fr = nullptr;
        if (c.mode == MODE_TX && c.txEnd <= t) { tn = c.txEnd; kind = EV_TXEND; }
        if (c.locked && c.locked->end <= t && c.locked->end < tn) { tn = c.locked->end; kind = EV_RXEND; }
        if (c.mode == MODE_RXSINGLE && !c.locked && c.rxDeadline <= t && c.rxDeadline < tn) { tn = c.rxDeadline; kind = EV_TIMEOUT; }
        // tramas que se detectan (en ideal las entrega idealDeliver(), una por consulta del firmware)
        if (!g_ideal) {
            for (auto &f : frames)
                if (!f.done && f.lockAt <= t && f.lockAt < tn && &f != c.locked) { tn = f.lockAt; kind = EV_LOCK; fr = &f; }
        }
        if (kind == EV_NONE) break;
        if (tn < c.cur) tn = c.cur;
        c.cur = tn;
        switch (kind) {
            case EV_TXEND:
                c.irq |= IRQ_TXDONE;
                setStdby(tn, false);           // en modo LoRa el chip vuelve solo a STDBY al acabar de emitir
                break;
            case EV_TIMEOUT:
                c.irq |= IRQ_RXTIMEOUT;
                setStdby(tn, true);
                break;
            case EV_RXEND: {
                Frame *f = c.locked; c.locked = nullptr;
                deliver(*f, tn);
                break;
            }
            case EV_LOCK: {
                if (c.locked) {                                              // dos tramas a la vez: colision
                    c.locked->corrupt = true;
                    finish(*fr, sx::RX_LOST_COLLISION);
                    break;
                }
                if (!listeningAt(tn)) {
                    sx::RxOutcome o = sx::RX_LOST_STANDBY;
                    if (c.mode == MODE_SLEEP) o = sx::RX_LOST_SLEEP;
                    else if (c.mode == MODE_TX) o = sx::RX_LOST_TX;
                    else if (c.stdbyByTimeout) o = sx::RX_LOST_RXTIMEOUT;
                    finish(*fr, o);
                    break;
                }
                if (!paramsMatch(fr->p)) { finish(*fr, sx::RX_LOST_MISMATCH); break; }
                c.locked = fr;
                break;
            }
            default: break;
        }
    }
    c.cur = t;
    // olvidar tramas ya tratadas hace tiempo (los Frame* solo se usan mientras no estan hechas)
    while (!frames.empty() && frames.front().done && frames.front().end < t - 2000 && &frames.front() != c.locked)
        frames.pop_front();
}

// ---------------- Registros ----------------
uint8_t readReg(uint8_t a) {
    double now = nowMs();
    advanceTo(now);
    if (a == R_IRQ_FLAGS && c.mode == MODE_TX && c.txEnd > now) {
        // El firmware (endPacket) hace polling de TxDone: la CPU queda ocupada todo el tiempo en el aire.
        uint32_t rem = (uint32_t)std::ceil(c.txEnd - now);
        sim::advance(rem);
        advanceTo(nowMs());
    }
    switch (a) {
        case R_FIFO: { uint8_t v = (g_ideal ? c.fifoRx : c.fifo)[c.reg[R_FIFO_ADDR_PTR]]; c.reg[R_FIFO_ADDR_PTR]++; return v; }
        case R_OPMODE: return (uint8_t)((c.lora ? 0x80 : 0x00) | c.mode);
        case R_IRQ_FLAGS: idealDeliver(nowMs()); return c.irq;
        case 0x18: {                                    // RegModemStat: recibiendo una trama (senal+sincronismo+RX en curso+cabecera) / modem libre
            if (c.locked) return 0x0F;
            if (listeningAt(now))                       // preambulo ya detectable (pero aun sin enganchar): "senal detectada"
                for (auto &f : frames)
                    if (!f.done && &f != c.locked && f.start + RX_DETECT_SYMS * symMs(f.p.sf, f.p.bw) <= now && now < f.end &&
                        (g_ideal || paramsMatch(f.p)))
                        return 0x01;
            return 0x10;
        }
        case R_VERSION: return 0x12;
        case R_RSSI: return (uint8_t)(-110 + 157);
        default: return c.reg[a];
    }
}

void writeReg(uint8_t a, uint8_t v) {
    double now = nowMs();
    advanceTo(now);
    switch (a) {
        case R_FIFO: c.fifo[c.reg[R_FIFO_ADDR_PTR]] = v; c.reg[R_FIFO_ADDR_PTR]++; break;
        case R_OPMODE: c.reg[R_OPMODE] = v; setMode(v & 7, (v & 0x80) != 0, now); break;
        case R_IRQ_FLAGS: c.irq &= (uint8_t)~v; break;            // se borra escribiendo 1
        case R_VERSION: break;                                     // solo lectura
        default: c.reg[a] = v; break;
    }
}

void hardReset() {
    memset(c.reg, 0, sizeof(c.reg));
    memset(c.fifo, 0, sizeof(c.fifo));
    memset(c.fifoRx, 0, sizeof(c.fifoRx));
    c.reg[R_OPMODE] = 0x09;
    c.reg[R_FIFO_TX_BASE] = 0x80; c.reg[R_FIFO_RX_BASE] = 0x00;
    c.reg[R_LNA] = 0x20; c.reg[R_MC1] = 0x72; c.reg[R_MC2] = 0x70; c.reg[R_SYMB_TIMEOUT_LSB] = 0x64;
    c.reg[R_PREAMBLE_LSB] = 0x08; c.reg[R_PAYLOAD_LENGTH] = 0x01; c.reg[0x23] = 0xFF; c.reg[R_MC3] = 0x00;
    c.reg[R_SYNC_WORD] = 0x12; c.reg[0x31] = 0x03; c.reg[0x33] = 0x27; c.reg[0x37] = 0x0A;
    c.reg[R_PA_CONFIG] = 0x4F; c.reg[R_PA_DAC] = 0x84;
    c.reg[R_FRF_MSB] = 0x6C; c.reg[R_FRF_MID] = 0x80; c.reg[R_FRF_LSB] = 0x00;   // 434 MHz
    double now = nowMs();
    account(now);
    c.lora = false; c.mode = MODE_STDBY; c.irq = 0; c.locked = nullptr;
    c.rxDeadline = 1e300; c.stdbyByTimeout = false; c.unreadRec = (size_t)-1;
    c.cur = std::max(c.cur, now);
}

struct Init { Init() {
    g_peer.freq = 868000000; g_peer.sf = 7; g_peer.bw = 125000; g_peer.cr = 1; g_peer.preamble = 8; g_peer.crc = true; g_peer.sync = 0x12;
    hardReset();
} } g_init;

}  // namespace

// =====================================================================
//  API publica
// =====================================================================
namespace sx {

double airtimeMs(int payloadBytes, int sf, long bw, int cr, int preamble, bool crc, bool implicitHeader) {
    double tSym = (double)(1 << sf) / (double)bw * 1000.0;
    bool de = tSym > 16.0;                                  // optimizacion de baja tasa (la fija la libreria)
    double tPre = (preamble + 4.25) * tSym;
    double num = 8.0 * payloadBytes - 4.0 * sf + 28.0 + (crc ? 16.0 : 0.0) - (implicitHeader ? 20.0 : 0.0);
    double den = 4.0 * (sf - (de ? 2 : 0));
    double nPay = 8.0 + std::max(std::ceil(num / den) * (cr + 4), 0.0);
    return tPre + nPay * tSym;
}

void nssWrite(bool level) {
    c.nssLow = !level;
    if (c.nssLow) c.spiPhase = 0;
}

void rstWrite(bool level) {
    static bool prev = true;
    if (level && !prev) hardReset();                        // flanco de subida: reinicio
    prev = level;
}

uint8_t spiTransfer(uint8_t b) {
    if (!c.nssLow) return 0xFF;
    if (c.spiPhase == 0) {                                  // byte de direccion (bit 7 = escritura)
        c.spiWrite = (b & 0x80) != 0;
        c.spiAddr = b & 0x7F;
        c.spiPhase = 1;
        return 0;
    }
    uint8_t r = 0;
    if (c.spiWrite) writeReg(c.spiAddr, b);
    else r = readReg(c.spiAddr);
    if (c.spiAddr != R_FIFO) c.spiAddr = (c.spiAddr + 1) & 0x7F;     // rafaga: la direccion se autoincrementa (menos la FIFO)
    return r;
}

bool dio0Level() {
    advanceTo(nowMs());
    idealDeliver(nowMs());
    int map = c.reg[R_DIO_MAPPING1] >> 6;
    if (map == 0) return (c.irq & IRQ_RXDONE) != 0;
    if (map == 1) return (c.irq & IRQ_TXDONE) != 0;
    return false;
}

void setIdeal(bool on) { g_ideal = on; }
bool ideal() { return g_ideal; }
void setLoopback(bool on) { g_loopback = on; }
AirFrame &peerFrame() { return g_peer; }

void setPolite(bool on) { g_polite = on; }
bool polite() { return g_polite; }

void inject(const std::string &payload, int rssi, bool corrupt) {
    AirFrame a = g_peer;
    a.payload = payload;
    a.rssi = rssi;
    double start = nowMs();
    if (g_polite && !g_ideal) {
        advanceTo(start);                                          // poner el chip al dia: (esta emitiendo ahora?)
        if (c.mode == MODE_TX && c.txEnd > start) start = c.txEnd + 1.0;   // espera a que el firmware termine de emitir
    }
    frames.push_back(mkFrame(a, start, corrupt));
}

void resetWorld() {
    frames.clear();
    c.locked = nullptr;
    c.cur = 0; c.modeSince = 0; c.txEnd = 0; c.txStart = 0;
    c.unreadRec = (size_t)-1;
    g_lastAirPoll = -1;
}

const std::vector<TxRecord> &txLog() { return g_tx; }
const std::vector<RxRecord> &rxLog() { return g_rx; }

const char *outcomeName(RxOutcome o) {
    switch (o) {
        case RX_PENDING: return "en curso";
        case RX_HEARD: return "oida";
        case RX_CRC_ERROR: return "CRC erroneo";
        case RX_LOST_STANDBY: return "PERDIDA: radio en reposo (sorda)";
        case RX_LOST_SLEEP: return "PERDIDA: radio dormida";
        case RX_LOST_TX: return "PERDIDA: la radio estaba transmitiendo";
        case RX_LOST_RXTIMEOUT: return "PERDIDA: la recepcion unica ya habia expirado";
        case RX_LOST_ABORTED: return "PERDIDA: se salio de recepcion a mitad";
        case RX_LOST_COLLISION: return "PERDIDA: colision con otra trama";
        case RX_LOST_MISMATCH: return "PERDIDA: otros parametros de modulacion";
        case RX_OVERRUN: return "PERDIDA: pisada por otra antes de leerla";
    }
    return "?";
}

double txAirtimeBetween(double t0, double t1) {
    double sum = 0;
    for (auto &t : g_tx) {
        double a = std::max(t.start, t0), b = std::min(t.end, t1);
        if (b > a) sum += b - a;
    }
    return sum;
}

std::string lastSent() { return g_tx.empty() ? std::string() : g_tx.back().payload; }

std::vector<std::string> sentRing(size_t n) {
    std::vector<std::string> out;
    size_t from = g_tx.size() > n ? g_tx.size() - n : 0;
    for (size_t i = from; i < g_tx.size(); i++) out.push_back(g_tx[i].payload);
    return out;
}

void sync() { advanceTo(nowMs()); }

void forceMode(int mode) {
    advanceTo(nowMs());
    setMode(mode & 7, c.lora, nowMs());
}

int currentMode() { advanceTo(nowMs()); return c.mode; }
bool listening() { advanceTo(nowMs()); return listeningAt(nowMs()); }

std::string report() {
    advanceTo(nowMs());
    account(nowMs());
    char b[256];
    std::string r;
    snprintf(b, sizeof(b), "  radio: %zu tramas emitidas, %.0f ms en el aire en total\n", g_tx.size(), txAirtimeBetween(0, 1e300)); r += b;
    int cnt[16] = {0};
    for (auto &x : g_rx) cnt[(int)x.outcome]++;
    snprintf(b, sizeof(b), "  radio: %zu tramas recibidas por el aire: %d oidas, %d con CRC malo\n", g_rx.size(), cnt[RX_HEARD], cnt[RX_CRC_ERROR]); r += b;
    for (int o = RX_LOST_STANDBY; o <= RX_OVERRUN; o++)
        if (cnt[o]) { snprintf(b, sizeof(b), "  radio:   %d %s\n", cnt[o], outcomeName((RxOutcome)o)); r += b; }
    double tot = c.modeMs[0] + c.modeMs[1] + c.modeMs[3] + c.modeMs[5] + c.modeMs[6];
    snprintf(b, sizeof(b), "  radio: tiempo por modo (ms): sleep %.0f  stdby %.0f  tx %.0f  rx-cont %.0f  rx-unico %.0f\n",
             c.modeMs[0], c.modeMs[1], c.modeMs[3], c.modeMs[5], c.modeMs[6]); r += b;
    double txLow = c.modeMs[3] - c.txPowerMsHigh;
    double mAh = (c.modeMs[0] * MA_SLEEP + c.modeMs[1] * MA_STDBY + txLow * MA_TX17 + c.txPowerMsHigh * MA_TX20 +
                  (c.modeMs[5] + c.modeMs[6]) * MA_RX) / 3.6e6;
    double avg = tot > 0 ? mAh * 3.6e6 / tot : 0;
    snprintf(b, sizeof(b), "  radio: consumo estimado del chip %.4f mAh (media %.2f mA; corrientes tipicas del datasheet)\n", mAh, avg); r += b;
    return r;
}

}  // namespace sx

// =====================================================================
//  API historica del simulador (la usaban el driver y los comandos de guion)
// =====================================================================
void simLoraInject(const std::string &packet, int rssi) { sx::inject(packet, rssi); }
std::string simLoraLastSent() { return sx::lastSent(); }
std::vector<std::string> simLoraSentRing() { return sx::sentRing(16); }
void simLoraSetLoopback(bool on) { sx::setLoopback(on); }
