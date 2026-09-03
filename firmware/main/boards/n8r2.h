// Board-specific configs for the ESP32-S3-N8R2 (8MB Flash + 2 MB PSRAM in quad mode).
#pragma once

#include "sdkconfig.h"

#if !CONFIG_SPIRAM_MODE_QUAD
    #error "n8r2.h included but CONFIG_SPIRAM_MODE_QUAD is not set. Include via config.h only."
#endif

// --- MAX98357A I2S DAC/amp ---------------------------------------------------
// Since this board had external PSRAM in Quad mode, GPIOs 33-37 are exposed for
// use. On a board with Octal PSRAM, those GPIOs are reserved and must be remapped.
#define I2S_BCLK_GPIO           GPIO_NUM_36
#define I2S_WS_GPIO             GPIO_NUM_37
#define I2S_DOUT_GPIO           GPIO_NUM_35