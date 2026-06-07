#include <vector>
#include <string>
#include <cstdio>
#include <cstdint>

#include "framebuffer.h"
#include "Display.h"   // extern Adafruit_SH1107 display;

// Render con caracteres BRAILLE: cada celda agrupa 2x4 pixeles, asi la pantalla
// 128x128 ocupa 64x32 caracteres (mitad de alto) y los pixeles quedan cuadrados,
// muy parecido al tamano real del OLED.
//
//   distribucion de puntos braille en la celda 2x4 (col x, fila y):
//      (0,0)=0x01  (1,0)=0x08
//      (0,1)=0x02  (1,1)=0x10
//      (0,2)=0x04  (1,2)=0x20
//      (0,3)=0x40  (1,3)=0x80
void simRenderTerminal(bool color) {
    const int W = Adafruit_SH1107::W;
    const int H = Adafruit_SH1107::H;
    std::string out;
    out.reserve((W / 2 * 3 + 1) * (H / 4 + 1));

    if (color) out += "\x1b[38;5;48m";
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
            // U+2800 + b en UTF-8 (3 bytes)
            out += (char)0xE2;
            out += (char)(0xA0 + (b >> 6));
            out += (char)(0x80 + (b & 0x3F));
        }
        out += "\n";
    }
    if (color) out += "\x1b[0m";

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
