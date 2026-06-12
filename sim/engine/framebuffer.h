#ifndef SIM_FRAMEBUFFER_H
#define SIM_FRAMEBUFFER_H
// Renderiza el framebuffer del OLED (display global) a terminal y a BMP.
// compact=false: medios bloques (pixeles cuadrados, 128x64 chars).
// compact=true:  braille (compacto, 64x32 chars) para ventanas bajas.
void simRenderTerminal(bool color, bool compact = false);
bool simSaveBMP(const char *path, int scale);
#endif
