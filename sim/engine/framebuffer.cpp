#include <vector>
#include <string>
#include <cstdio>
#include <cstdint>

#include "framebuffer.h"
#include "Display.h"   // extern Adafruit_SH1107 display;

// Bloques Unicode en UTF-8 explícito (independiente del charset del compilador)
static const char *BLK_FULL = "\xE2\x96\x88"; // █
static const char *BLK_TOP  = "\xE2\x96\x80"; // ▀
static const char *BLK_BOT  = "\xE2\x96\x84"; // ▄

void simRenderTerminal(bool color) {
    const int W = Adafruit_SH1107::W;
    const int H = Adafruit_SH1107::H;
    std::string out;
    out.reserve((W + 1) * (H / 2 + 4));

    std::string border(W, '-');
    out += "+" + border + "+\n";
    for (int y = 0; y < H; y += 2) {
        out += "|";
        for (int x = 0; x < W; x++) {
            bool t = display.simPixel(x, y);
            bool b = (y + 1 < H) ? display.simPixel(x, y + 1) : false;
            if (t && b)      out += BLK_FULL;
            else if (t)      out += BLK_TOP;
            else if (b)      out += BLK_BOT;
            else             out += " ";
        }
        out += "|\n";
    }
    out += "+" + border + "+\n";

    if (color) fputs("\x1b[38;5;48m", stdout);
    fputs(out.c_str(), stdout);
    if (color) fputs("\x1b[0m", stdout);
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
