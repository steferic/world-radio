#pragma once

#include "sdkconfig.h"

// ---------------------------------------------------------------------------
// Wi-Fi
// ---------------------------------------------------------------------------
#define WIFI_SSID               "SM-G970U72a"
#define WIFI_PASS               "0000005958"
#define WIFI_CONNECT_MAX_RETRY  10

// ---------------------------------------------------------------------------
// Stream
// ---------------------------------------------------------------------------
#define STREAM_URL              "https://italiandancenetwork.com/stream.mp3"

// Size of the byte ring buffer sitting between the HTTP fetch task and the
// MP3 decode/I2S task. Bigger = more resilience to network jitter/stalls,
// at the cost of RAM and added latency.
#define AUDIO_RINGBUF_BYTES     (512 * 1024)

// ---------------------------------------------------------------------------
// GPIO —> MAX98357A I2S DAC/amp wiring
// ---------------------------------------------------------------------------
#if CONFIG_SPIRAM_MODE_OCT
    #include "boards/n16r8.h"
#elif CONFIG_SPIRAM_MODE_QUAD
    #include "boards/n8r2.h"
#else
    #error "No board detected: set CONFIG_SPIRAM_MODE_QUAD or _OCT in the sdkconfig fragment for the target board."
#endif

// ---------------------------------------------------------------------------
// Volume potentiometer
// ---------------------------------------------------------------------------
#define VOLUME_POT_GPIO          GPIO_NUM_4

// Set to 1 or 0 to determine the direction of the potentiometer. This is
// a firmware fix in case the hardware wiring is accidentally inverted.
#define VOLUME_POT_INVERT        1

// How often the pot is sampled
#define VOLUME_POLL_INTERVAL_MS  30

// Low-pass filter coefficient for smoothing raw ADC noise (0-1). Higher =
// more responsive to knob movement but noisier; lower = smoother but laggier.
#define VOLUME_SMOOTHING_ALPHA   0.2f

// ---------------------------------------------------------------------------
// Rotary encoder (station select) (not yet implemented)
// ---------------------------------------------------------------------------
#define ROTARY_ENCODER_GPIO_A     GPIO_NUM_15
#define ROTARY_ENCODER_GPIO_B     GPIO_NUM_16

// Invert the direction the R.E. turns, in case the hardware wiring in inverted.
#define ROTARY_ENCODER_INVERT     0

#define ROTARY_ENCODER_POLL_INTERVAL_MS 30

// ---------------------------------------------------------------------------
// LCD (ST7789, 240x320 SPI TFT)
// ---------------------------------------------------------------------------
#define LCD_SPI_HOST        SPI2_HOST
#define LCD_SCK_GPIO        GPIO_NUM_2
#define LCD_MOSI_GPIO       GPIO_NUM_42
#define LCD_CS_GPIO         GPIO_NUM_39
#define LCD_DC_GPIO         GPIO_NUM_40
#define LCD_RESET_GPIO      GPIO_NUM_41

// Panel dimensions as seen by software -- these must match the current
// MADCTL rotation below, not the panel's native portrait dimensions. A 90/
// 270 degree rotation swaps width and height; 0/180 keeps them as-is.
#define LCD_WIDTH           320
#define LCD_HEIGHT          240
#define LCD_SPI_CLOCK_HZ    (40 * 1000 * 1000)

// MADCTL (ST7789 command 0x36) sets rotation/mirroring. Bit 7=MY (row
// order), bit 6=MX (column order), bit 5=MV (row/column exchange).
// Common values for this panel:
//   0x00 -- native portrait,        240 wide x 320 tall
//   0x60 -- rotated 90 deg CW,      320 wide x 240 tall  <- current
//   0xC0 -- rotated 180 deg,        240 wide x 320 tall
//   0xA0 -- rotated 90 deg CCW,     320 wide x 240 tall
// Changing this WITHOUT updating LCD_WIDTH/LCD_HEIGHT to match will send
// pixel data outside the panel's actual addressable area in that orientation.
#define LCD_MADCTL           0x60
