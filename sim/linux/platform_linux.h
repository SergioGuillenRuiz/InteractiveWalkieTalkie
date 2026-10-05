// Capa de compatibilidad Linux (GNOME Terminal / cualquier terminal VT) para el
// simulador. Proporciona lo que en Windows dan <conio.h>, <process.h> y winmm:
//
//   _kbhit / _getch / _ungetch  -> teclado sin eco ni buffer de linea (termios)
//   _getpid / _get_pgmptr       -> pid y ruta del ejecutable (/proc/self/exe)
//   timeBeginPeriod / End       -> no-op (el temporizador de Linux ya es fino)
//
// Las flechas llegan como secuencias VT (ESC [ A / ESC [ B). _getch las traduce
// al formato de conio (0xE0 seguido de 72/80) para que el motor del simulador
// funcione igual que en Windows. Cualquier otra secuencia ESC (p.ej. la
// respuesta DSR "ESC [ fila ; col R" que usa el simulador para medir el
// terminal) se entrega intacta, byte a byte.
//
// Solo lo incluye sim_main.cpp cuando NO es Windows (ver sim/linux/build.sh).
#pragma once

#include <cstdlib>
#include <cstring>
#include <deque>
#include <climits>
#include <csignal>
#include <unistd.h>
#include <poll.h>
#include <termios.h>
#include <sys/ioctl.h>

namespace simlinux {

inline std::deque<int> &pending() { static std::deque<int> q; return q; }
inline bool &stdinEof() { static bool e = false; return e; }

inline struct termios &savedTermios() { static struct termios t; return t; }
inline bool &rawActive() { static bool a = false; return a; }

inline void restoreTerminal() {
    if (rawActive()) {
        tcsetattr(STDIN_FILENO, TCSANOW, &savedTermios());
        rawActive() = false;
    }
}

// Ctrl+C / cierre: el manejador de SIGINT del motor sale con _exit() (que no
// ejecuta atexit), asi que envolvemos el manejador previo: primero se restaura
// el terminal (tcsetattr es async-signal-safe) y luego se delega en el.
inline struct sigaction *prevActions() { static struct sigaction a[32]; return a; }

inline void onFatalSignal(int sig) {
    restoreTerminal();
    struct sigaction &prev = prevActions()[sig];
    if (!(prev.sa_flags & SA_SIGINFO) && prev.sa_handler != SIG_DFL && prev.sa_handler != SIG_IGN) {
        prev.sa_handler(sig);
        return;
    }
    signal(sig, SIG_DFL);   // sin manejador propio: comportamiento por defecto
    raise(sig);
}

inline void hookSignals() {
    const int sigs[] = { SIGINT, SIGTERM, SIGHUP, SIGQUIT };
    for (int s : sigs) {
        struct sigaction cur;
        if (sigaction(s, nullptr, &cur) != 0 || cur.sa_handler == SIG_IGN) continue;
        prevActions()[s] = cur;
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = onFatalSignal;
        sigemptyset(&sa.sa_mask);
        sigaction(s, &sa, nullptr);
    }
}

// Modo "crudo" (sin eco, sin esperar a Enter) solo si stdin es un terminal.
// ISIG se conserva: Ctrl+C sigue funcionando. Se restaura al salir.
inline void ensureRaw() {
    static bool tried = false;
    if (tried) return;
    tried = true;
    if (!isatty(STDIN_FILENO)) return;
    if (tcgetattr(STDIN_FILENO, &savedTermios()) != 0) return;
    struct termios raw = savedTermios();
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
        rawActive() = true;
        atexit(restoreTerminal);
        hookSignals();
    }
}

// Espera hasta timeoutMs a que haya un byte en stdin.
inline bool byteReady(int timeoutMs) {
    if (stdinEof()) return false;
    struct pollfd p;
    p.fd = STDIN_FILENO; p.events = POLLIN; p.revents = 0;
    if (poll(&p, 1, timeoutMs) <= 0) return false;
    return (p.revents & (POLLIN | POLLHUP)) != 0;
}

// Lee un byte crudo (-1 si EOF / error).
inline int readByte() {
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n <= 0) { stdinEof() = true; return -1; }
    return (int)c;
}

} // namespace simlinux

inline int _kbhit() {
    simlinux::ensureRaw();
    if (!simlinux::pending().empty()) return 1;
    if (!simlinux::byteReady(0)) return 0;
    // Hay algo: comprobar que no sea un EOF (stdin redirigido desde /dev/null).
    int c = simlinux::readByte();
    if (c < 0) return 0;
    simlinux::pending().push_back(c);
    return 1;
}

inline int _getch() {
    simlinux::ensureRaw();
    auto &q = simlinux::pending();
    int c;
    if (!q.empty()) { c = q.front(); q.pop_front(); }
    else {
        c = simlinux::readByte();
        if (c < 0) return 0x1b;   // EOF: devolver algo inocuo (ninguna accion)
    }
    if (c != 0x1b) return c;

    // ESC: puede ser una flecha (ESC [ A, ESC O A...). Mirar lo que sigue.
    auto next = [&](int &out) -> bool {
        if (!q.empty()) { out = q.front(); q.pop_front(); return true; }
        if (!simlinux::byteReady(15)) return false;
        out = simlinux::readByte();
        return out >= 0;
    };
    int b1, b2;
    if (!next(b1)) return 0x1b;
    if (b1 != '[' && b1 != 'O') { q.push_front(b1); return 0x1b; }
    if (!next(b2)) { q.push_front(b1); return 0x1b; }
    int code = 0;
    switch (b2) {
        case 'A': code = 72; break;   // arriba
        case 'B': code = 80; break;   // abajo
        case 'C': code = 77; break;   // derecha
        case 'D': code = 75; break;   // izquierda
    }
    if (code) { q.push_front(code); return 0xE0; }
    // No es una flecha (p.ej. respuesta DSR): devolver ESC y conservar el resto.
    q.push_front(b2);
    q.push_front(b1);
    return 0x1b;
}

inline int _ungetch(int c) { simlinux::pending().push_front(c); return c; }

inline int _getpid() { return (int)getpid(); }

inline int _get_pgmptr(char **out) {
    static char path[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n <= 0) { *out = nullptr; return 1; }
    path[n] = 0;
    *out = path;
    return 0;
}

inline unsigned int timeBeginPeriod(unsigned int) { return 0; }
inline unsigned int timeEndPeriod(unsigned int)   { return 0; }
