/* Small wrapper around Adafruit_GFX & Adafruit_ST7789 for driving an
 * ST7789 TFT LCD from the ESP32-S3 using SPI.
 */

#ifndef ST7789_HELPER_H
#define ST7789_HELPER_H

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "config.h"

#define LCD_WIDTH  240
#define LCD_HEIGHT 320

extern Adafruit_ST7789 tft;

void lcdInit(uint8_t rotation = 0);

// Draw a full-color image. `pixels` must be a flat array of RGB565
// values, row-major, of length w*h. This is the native ST7789 pixel
// format, so it is pushed straight to the panel with no conversion.
void lcdDrawRGB565(const uint16_t *pixels, int16_t w, int16_t h,
                    int16_t x = 0, int16_t y = 0);

// Draw a 1-bit-per-pixel bitmap (e.g. exported from an image editor as
// a monochrome/bitmap array), MSB-first, row-major, padded to whole
// bytes per row (same layout Adafruit_GFX::drawBitmap expects).
// `fgColor`/`bgColor` are RGB565 colors substituted for 1 and 0 bits.
void lcdDrawBitmap1bpp(const uint8_t *bitmap, int16_t w, int16_t h,
                        uint16_t fgColor, uint16_t bgColor,
                        int16_t x = 0, int16_t y = 0);

// Convenience: clear the whole screen to a solid color
void lcdClear(uint16_t color = ST77XX_BLACK);

#endif
