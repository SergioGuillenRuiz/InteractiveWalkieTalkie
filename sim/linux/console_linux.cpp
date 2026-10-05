// Configuracion de la consola en Linux para el modo interactivo (equivalente a
// engine/console_win.cpp).
//
// En Linux no hay API para cambiar la fuente del terminal desde el programa: el
// tamano de los "pixeles" lo fija run.sh al abrir la ventana de GNOME Terminal
// (--geometry y --zoom). Aqui solo pedimos al terminal el tamano objetivo
// (secuencia xterm, la ignoran los que no la soportan) y devolvemos el tamano
// real medido con TIOCGWINSZ. Si no llega para el render cuadrado, el simulador
// mide por DSR y cae a la vista compacta (braille), igual que en Windows.
#include <cstdio>
#include <unistd.h>
#include <sys/ioctl.h>

extern "C" void simSetupConsole(int wantCols, int wantRows, int *outCols, int *outRows) {
    *outCols = 0; *outRows = 0;
    if (!isatty(STDOUT_FILENO)) return;

    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 &&
        (ws.ws_col < wantCols || ws.ws_row < wantRows)) {
        printf("\x1b[8;%d;%dt", wantRows, wantCols);   // pedir redimensionado
        fflush(stdout);
        usleep(100 * 1000);                            // dar tiempo a aplicarlo
    }
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        *outCols = ws.ws_col;
        *outRows = ws.ws_row;
    }
}
