#ifndef SIM_FRAMEBUFFER_H
#define SIM_FRAMEBUFFER_H
// Renderiza el framebuffer del OLED (display global) a terminal y a BMP.
void simRenderTerminal(bool color);
bool simSaveBMP(const char *path, int scale);
#endif
