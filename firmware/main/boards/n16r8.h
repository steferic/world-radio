// Board-specific configs for the ESP32-S3-N16R8 (16MB Flash + 8 MB PSRAM in octal mode)
#pragma once

#include "sdkconfig.h"

#if !CONFIG_SPIRAM_MODE_OCT
    #error "n16r8.h included but CONFIG_SPIRAM_MODE_OCT is not set. Include via config.h only."
#endif

// --- MAX98357A I2S DAC/amp ---------------------------------------------------
// This ESP32-S3 variant has external PSRAM wired in Octal mode, meaning pins 33-
// 37 are reserved for communicating to PSRAM. Therefore, the DAC pins must move.
#define I2S_BCLK_GPIO           GPIO_NUM_5
#define I2S_WS_GPIO             GPIO_NUM_6
#define I2S_DOUT_GPIO           GPIO_NUM_7