/*
 * A small wrapper around Adafruit_GFX & Adafruit_ST7789 for driving an
 * ST7789 TFT LCD using an ESP32-S3. Requires these libraries:
 * Adafruit GFX Library, Adafruit ST7735 and ST7789 Library
 */

#ifndef ST7789_HELPER_H
#define ST7789_HELPER_H

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

extern Adafruit_ST7789 tft;

// Call once in setup() in main script. Rotation is between 0-3.
void lcdInit(uint8_t rotation = 0);

// Draw a full-color image. The array `pixels` must be a flat array
// of RGB565 values, row-major, of length w*h.
void lcdDrawRGB565(const uint16_t *pixels, int16_t w, int16_t h,
                    int16_t x = 0, int16_t y = 0);

// Draw a 1-bit-per-pixel bitmap (e.g. exported from an image editor as
// a monochrome/bitmap array), MSB-first, row-major, padded to whole
// bytes per row (same layout Adafruit_GFX::drawBitmap expects).
void lcdDrawBitmap1bpp(const uint8_t *bitmap, int16_t w, int16_t h,
                        uint16_t fgColor, uint16_t bgColor,
                        int16_t x = 0, int16_t y = 0);

// Clear the whole screen to a solid color (default black).
void lcdClear(uint16_t color = ST77XX_BLACK);

// Show a single centered text line (e.g. "Connecting to WiFi...").
// Clears the screen first, so don't mix with other drawing calls.
void lcdShowStatus(const char *message, uint16_t color = ST77XX_WHITE);

// Show now-playing metadata: station name (small, top), title (large,
// middle), artist (small, below title). I know it's ugly but it works
// for now.
void lcdShowNowPlaying(const char *station, const char *artist, const char *title);

#endif