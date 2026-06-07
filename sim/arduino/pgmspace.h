// Host mock of <pgmspace.h>: on a PC there is no separate program memory,
// so every "read from flash" is just a normal memory access.
#ifndef SIM_PGMSPACE_H
#define SIM_PGMSPACE_H

#include <cstdint>
#include <cstring>

#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef PGM_P
#define PGM_P const char *
#endif
#ifndef PSTR
#define PSTR(s) (s)
#endif

// Lecturas "desde flash" -> simple desreferencia
#define pgm_read_byte(addr)    (*(const uint8_t  *)(addr))
#define pgm_read_byte_near(a)  pgm_read_byte(a)
#define pgm_read_byte_far(a)   pgm_read_byte(a)
#define pgm_read_word(addr)    (*(const uint16_t *)(addr))
#define pgm_read_word_near(a)  pgm_read_word(a)
#define pgm_read_dword(addr)   (*(const uint32_t *)(addr))

// Lectura de puntero almacenado "en flash" (tamaño de puntero real: 64 bits en PC)
#define pgm_read_pointer(addr) (*(void *const *)(addr))

static inline void *memcpy_P(void *dst, const void *src, size_t n) { return memcpy(dst, src, n); }
static inline size_t strlen_P(const char *s) { return strlen(s); }
static inline char *strcpy_P(char *d, const char *s) { return strcpy(d, s); }
static inline int strcmp_P(const char *a, const char *b) { return strcmp(a, b); }

#endif // SIM_PGMSPACE_H
