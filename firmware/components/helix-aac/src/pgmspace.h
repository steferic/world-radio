#pragma once

// Shim for AVR's <pgmspace.h>. On ESP32 (unlike an AVR) `const` data is
// already placed in flash by the linker and reachable via normal loads, so
// PROGMEM and its pgm_read_* accessors collapse to plain memory reads. The
// Helix AAC sources only reach for the byte and word variants; nothing else
// needs to exist here.

#include <stdint.h>

#ifndef PROGMEM
#define PROGMEM
#endif

#ifndef PGM_P
#define PGM_P const char *
#endif

#ifndef pgm_read_byte
#define pgm_read_byte(addr)  (*(const uint8_t  *)(addr))
#endif

#ifndef pgm_read_word
#define pgm_read_word(addr)  (*(const uint16_t *)(addr))
#endif

#ifndef pgm_read_dword
#define pgm_read_dword(addr) (*(const uint32_t *)(addr))
#endif
