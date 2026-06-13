// Configuracion de la consola en Windows para el modo interactivo.
//
// El render "cuadrado" (medios bloques) necesita 128x64 caracteres + estado
// (~68 filas). Con la fuente por defecto la ventana no llega y el simulador cae
// al modo braille (puntos redondos). Aqui fijamos una fuente lo bastante pequena
// (segun el monitor) y el tamano de ventana para que SIEMPRE quepa el cuadrado,
// sin que el usuario tenga que reducir el zoom a mano.
//
// IMPORTANTE: Windows Terminal (conpty) IGNORA la API de fuente de consola, asi
// que esto solo surte efecto en la consola clasica (conhost). run.bat lanza el
// simulador a traves de conhost para que funcione. Si detectamos que la fuente
// NO se aplico (estamos en WT), devolvemos 0 y el simulador mide el tamano real
// por DSR y se adapta (sin romper nada).
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "user32.lib")

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

// Devuelve en outCols/outRows el tamano resultante (0 si no se pudo / es Windows
// Terminal). wantCols/wantRows = tamano objetivo en caracteres.
extern "C" void simSetupConsole(int wantCols, int wantRows, int *outCols, int *outRows) {
    *outCols = 0; *outRows = 0;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == NULL || h == INVALID_HANDLE_VALUE) return;

    // Asegurar secuencias VT en la salida (medios bloques, posicionado, etc.)
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    // Area de trabajo del monitor (sin barra de tareas).
    RECT wa;
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0)) { wa.left = 0; wa.top = 0; wa.right = 1920; wa.bottom = 1080; }
    int screenW = (int)(wa.right - wa.left);
    int screenH = (int)(wa.bottom - wa.top);
    if (screenW < 640) screenW = 1920;
    if (screenH < 480) screenH = 1080;

    // Celda de Consolas: ancho ~= alto/2. Elegir la altura mas grande que permita
    // meter wantCols x wantRows en pantalla, con un margen para el marco/titulo.
    int hByW = (int)(((double)screenW * 0.92) * 2.0 / (double)wantCols);
    int hByH = (int)(((double)screenH * 0.88) / (double)wantRows);
    int fh = (hByW < hByH) ? hByW : hByH;
    if (fh > 22) fh = 22;
    if (fh < 8)  fh = 8;

    CONSOLE_FONT_INFOEX cf;
    ZeroMemory(&cf, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.nFont = 0;
    cf.dwFontSize.X = 0;            // ancho derivado de la altura (TrueType)
    cf.dwFontSize.Y = (SHORT)fh;
    cf.FontFamily = FF_DONTCARE;
    cf.FontWeight = FW_NORMAL;
    lstrcpyW(cf.FaceName, L"Consolas");
    SetCurrentConsoleFontEx(h, FALSE, &cf);

    // Comprobar si la fuente se aplico de verdad. En Windows Terminal es no-op:
    // en ese caso salimos y dejamos que el DSR mida el tamano real.
    CONSOLE_FONT_INFOEX chk;
    ZeroMemory(&chk, sizeof(chk));
    chk.cbSize = sizeof(chk);
    if (!GetCurrentConsoleFontEx(h, FALSE, &chk) || chk.dwFontSize.Y != (SHORT)fh) return;

    // Encajar ventana + buffer: ventana minima -> buffer objetivo -> ventana objetivo.
    SMALL_RECT minWin = {0, 0, 1, 1};
    SetConsoleWindowInfo(h, TRUE, &minWin);
    COORD buf;
    buf.X = (SHORT)wantCols; buf.Y = (SHORT)wantRows;
    SetConsoleScreenBufferSize(h, buf);
    SMALL_RECT win = {0, 0, (SHORT)(wantCols - 1), (SHORT)(wantRows - 1)};
    SetConsoleWindowInfo(h, TRUE, &win);

    // Centrar la ventana en el monitor (mejor presentacion).
    HWND hwnd = GetConsoleWindow();
    if (hwnd) {
        RECT r;
        if (GetWindowRect(hwnd, &r)) {
            int ww = r.right - r.left, wh = r.bottom - r.top;
            int x = wa.left + (screenW - ww) / 2;
            int y = wa.top + (screenH - wh) / 2;
            if (x < wa.left) x = wa.left;
            if (y < wa.top)  y = wa.top;
            SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    // Tamano real resultante.
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(h, &csbi)) {
        *outCols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        *outRows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
}
