#pragma once

#include "sdkconfig.h"
#include "config/gpio.h"

// ---------------------------------------------------------------------------
// Wi-Fi
// ---------------------------------------------------------------------------
#define WIFI_SSID               "SM-G970U72a"
#define WIFI_PASS               "0000005958"
#define WIFI_CONNECT_MAX_RETRY  10

// ---------------------------------------------------------------------------
// Stream
// ---------------------------------------------------------------------------
// STREAM_USE_API = 1 -> the fetch task picks a random MP3 station from the
// world-radio API on every (re)connect and shuffle. = 0 -> it ignores the
// API and pins to STREAM_URL below (useful for isolating stream/TLS bugs
// from API bugs). station_api.c and the shuffle plumbing are always built
// so flipping this is a rebuild-only change.
#define STREAM_USE_API          1
// SomaFM Groove Salad -- verified to send both header-mode ICY fields and
// interleaved metadata (icy-metaint: 45000, StreamTitle='Artist - Title').
// Good for exercising the ICY demuxer + now-playing UI path end-to-end.
#define STREAM_URL              "http://ice1.somafm.com/groovesalad-128-mp3"
// #define STREAM_URL              "https://italiandancenetwork.com/stream.mp3"

// Base URL of the world-radio API (see api/README.md). Only used when
// STREAM_USE_API is 1. No trailing slash.
#define STATION_API_BASE_URL    "https://world-radio-v370.onrender.com"
#define STATION_API_RANDOM_URL  STATION_API_BASE_URL "/api/stations/random"

// The API can hand back MP3, AAC, or other formats (OGG, HLS, etc.). The
// firmware decodes MP3 (minimp3 in audio/mp3_decoder.c) and AAC (libhelix
// via audio/aac_decoder.c, if the helix-aac component's sources are
// vendored -- see components/helix-aac/README.md). If /random hands back a
// format we can't decode, we throw it away and ask for another one, up to
// this many times before giving up on this cycle and letting the outer
// backoff retry the whole thing.
#define STATION_API_MAX_ATTEMPTS 20

// Size of the byte ring buffer sitting between the HTTP fetch task and the
// MP3 decode/I2S task. Bigger = more resilience to network jitter/stalls,
// at the cost of RAM and added latency.
#define AUDIO_RINGBUF_BYTES     (512 * 1024)

// ---------------------------------------------------------------------------
// Volume potentiometer
// ---------------------------------------------------------------------------
// Set to 1 or 0 to determine the direction of the potentiometer. This is
// a firmware fix in case the hardware wiring is accidentally inverted.
#define VOLUME_POT_INVERT        0

// How often the pot is sampled
#define VOLUME_POLL_INTERVAL_MS  30

// Low-pass filter coefficient for smoothing raw ADC noise (0-1). Higher =
// more responsive to knob movement but noisier; lower = smoother but laggier.
#define VOLUME_SMOOTHING_ALPHA   0.2f

// ---------------------------------------------------------------------------
// Pushbutton (shuffle / next-random)
// ---------------------------------------------------------------------------
// Anything shorter than this between edges is treated as switch bounce and
// dropped in the ISR. 30 ms is comfortably longer than any mechanical bounce
// on a normal tact switch, without feeling laggy on a real press.
#define DEBOUNCE_US             (30 * 1000)

// ---------------------------------------------------------------------------
// Rotary encoder (station select)
// ---------------------------------------------------------------------------
// Invert the direction the R.E. turns, in case the hardware wiring in inverted.
#define ROTARY_ENCODER_INVERT     0

#define ROTARY_ENCODER_POLL_INTERVAL_MS 30

// A hold on the rotary's built-in switch longer than this is classified as
// a LONG press. Used as an "escape hatch" gesture from deep menus back to
// the root screen.
#define LONG_PRESS_US             (600 * 1000)

// ---------------------------------------------------------------------------
// LCD (240x320 SPI TFT)
// ---------------------------------------------------------------------------
// Which controller chip is on the panel. The init sequence in lcd_driver.c 
// branches on this.
#define LCD_CONTROLLER_ST7789    0
#define LCD_CONTROLLER_ILI9341   1
#define LCD_CONTROLLER           LCD_CONTROLLER_ILI9341

// Panel dimensions as seen by software -- these must match the current
// MADCTL rotation below, not the panel's native portrait dimensions. A 90/
// 270 degree rotation swaps width and height; 0/180 keeps them as-is.
#define LCD_WIDTH           320
#define LCD_HEIGHT          240

// ST7789 tolerates 40 MHz on short wires; ILI9341 is datasheet-rated at
// 10 MHz for writes and typically works up to ~26 MHz in practice. If you
// see snow or misaligned pixels after switching to ILI9341, drop this.
#define LCD_SPI_CLOCK_HZ    (40 * 1000 * 1000)

// MADCTL (command 0x36) sets rotation/mirroring. Bit 7=MY (row order),
// bit 6=MX (column order), bit 5=MV (row/column exchange). Same encoding
// on ST7789 and ILI9341, but the two panel families are wired to their
// respective driver chips differently, so the SAME visual rotation needs
// DIFFERENT MADCTL values on each. Choose properly from the table below.
//
//                            portrait   90 CW      180        90 CCW
//                            240x320    320x240    240x320    320x240
//   ST7789                   0x00       0x60       0xC0       0xA0
//   ILI9341                  0x00       0x20       0x80       0xE0
//
// Changing MADCTL WITHOUT updating LCD_WIDTH/LCD_HEIGHT to match will
// send pixel data outside the panel's addressable area in that orientation.
#if LCD_CONTROLLER == LCD_CONTROLLER_ST7789
    #define LCD_MADCTL       0x60
#elif LCD_CONTROLLER == LCD_CONTROLLER_ILI9341
    #define LCD_MADCTL       0x20
#endif
