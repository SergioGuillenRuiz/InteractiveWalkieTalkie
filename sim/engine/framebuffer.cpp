#include <vector>
#include <string>
#include <cstdio>
#include <cstdint>

#include "framebuffer.h"
#include "Display.h"   // extern Adafruit_SH1107 display;

// Render con MEDIOS BLOQUES: cada caracter pinta 2 pixeles verticales RELLENOS
// (mitad de arriba / mitad de abajo), asi los pixeles salen CUADRADOS y solidos,
// igual que el OLED. La pantalla 128x128 ocupa 128x64 caracteres.
//
//   ' ' = ambos apagados   '▀' U+2580 = arriba   '▄' U+2584 = abajo   '█' U+2588 = ambos
static void renderHalfBlocks(std::string &out, bool color) {
    const int W = Adafruit_SH1107::W;
    const int H = Adafruit_SH1107::H;
    if (color) out += "\x1b[38;2;210;255;225m";   // color "encendido" parecido al OLED
    for (int cy = 0; cy < H; cy += 2) {
        for (int x = 0; x < W; x++) {
            bool top = display.simPixel(x, cy);
            bool bot = display.simPixel(x, cy + 1);
            if (top && bot) { out += (char)0xE2; out += (char)0x96; out += (char)0x88; }      // █
            else if (top)   { out += (char)0xE2; out += (char)0x96; out += (char)0x80; }      // ▀
            else if (bot)   { out += (char)0xE2; out += (char)0x96; out += (char)0x84; }      // ▄
            else            out += ' ';                                                       // apagado = fondo
        }
        out += "\n";
    }
    if (color) out += "\x1b[0m";
}

// Render compacto con BRAILLE: 2x4 pixeles por celda -> 64x32 caracteres. Cabe en
// ventanas bajas (los puntos se ven redondos). Solo se usa cuando la ventana no
// tiene alto para el cuadrado, para que NO se corte la parte de arriba.
static void renderBraille(std::string &out, bool color) {
    const int W = Adafruit_SH1107::W;
    const int H = Adafruit_SH1107::H;
    if (color) out += "\x1b[38;2;210;255;225m";
    for (int cy = 0; cy < H; cy += 4) {
        for (int cx = 0; cx < W; cx += 2) {
            unsigned char b = 0;
            if (display.simPixel(cx,     cy))     b |= 0x01;
            if (display.simPixel(cx,     cy + 1)) b |= 0x02;
            if (display.simPixel(cx,     cy + 2)) b |= 0x04;
            if (display.simPixel(cx + 1, cy))     b |= 0x08;
            if (display.simPixel(cx + 1, cy + 1)) b |= 0x10;
            if (display.simPixel(cx + 1, cy + 2)) b |= 0x20;
            if (display.simPixel(cx,     cy + 3)) b |= 0x40;
            if (display.simPixel(cx + 1, cy + 3)) b |= 0x80;
            out += (char)0xE2;
            out += (char)(0xA0 + (b >> 6));
            out += (char)(0x80 + (b & 0x3F));
        }
        out += "\n";
    }
    if (color) out += "\x1b[0m";
}

void simRenderTerminal(bool color, bool compact) {
    const int W = Adafruit_SH1107::W;
    const int H = Adafruit_SH1107::H;
    std::string out;
    out.reserve((size_t)(W * 3 + 8) * (H / 2 + 1));
    if (compact) renderBraille(out, color);
    else         renderHalfBlocks(out, color);
    fputs(out.c_str(), stdout);
    fflush(stdout);
}

static void putLE32(FILE *f, uint32_t v) {
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    fwrite(b, 1, 4, f);
}
static void putLE16(FILE *f, uint16_t v) {
    uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
    fwrite(b, 1, 2, f);
}

bool simSaveBMP(const char *path, int scale) {
    if (scale < 1) scale = 1;
    const int W = Adafruit_SH1107::W;
    const int H = Adafruit_SH1107::H;
    const int ow = W * scale, oh = H * scale;
    const int rowSize = (ow * 3 + 3) & ~3;
    const uint32_t imgSize = (uint32_t)rowSize * oh;

    FILE *f = fopen(path, "wb");
    if (!f) return false;

    // BITMAPFILEHEADER (14) + BITMAPINFOHEADER (40)
    fputc('B', f); fputc('M', f);
    putLE32(f, 54 + imgSize);   // file size
    putLE32(f, 0);              // reserved
    putLE32(f, 54);             // pixel data offset
    putLE32(f, 40);             // DIB header size
    putLE32(f, (uint32_t)ow);
    putLE32(f, (uint32_t)oh);
    putLE16(f, 1);              // planes
    putLE16(f, 24);            // bpp
    putLE32(f, 0);             // compression
    putLE32(f, imgSize);
    putLE32(f, 2835); putLE32(f, 2835); // ~72 DPI
    putLE32(f, 0); putLE32(f, 0);

    std::vector<uint8_t> row(rowSize, 0);
    for (int oy = 0; oy < oh; oy++) {
        int srcY = (oh - 1 - oy) / scale;        // BMP es bottom-up
        for (int ox = 0; ox < ow; ox++) {
            int srcX = ox / scale;
            bool on = display.simPixel(srcX, srcY);
            uint8_t r = on ? 210 : 8;
            uint8_t g = on ? 255 : 14;
            uint8_t b = on ? 225 : 22;
            row[ox * 3 + 0] = b;
            row[ox * 3 + 1] = g;
            row[ox * 3 + 2] = r;
        }
        fwrite(row.data(), 1, rowSize, f);
    }
    fclose(f);
    return true;
}
