#pragma once

// Shim so the RealNetworks Helix sources (patched by the ESP8266Audio fork
// they came from to include <Arduino.h>) build unmodified under bare
// ESP-IDF. Nothing in the AAC decoder actually needs Arduino runtime -- it
// only reaches for stdint/stdlib types via this header. The component's
// CMakeLists also defines ARDUINO=1, which selects the generic-C fallback
// in assembly.h and the plain <stdlib.h> path in buffers.c / sbr.c.

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
