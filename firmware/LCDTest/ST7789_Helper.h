/*
 * ST7789_Helper.h
 * ----------------
 * Small wrapper around Adafruit_GFX + Adafruit_ST7789 for driving an
 * ST7789 TFT LCD from an ESP32-S3, using the hardware SPI (FSPI/SPI2)
 * pin mapping:
 *
 *      LCD Pin      ESP32-S3 GPIO
 *      SCL (SCLK)   12
 *      SDA (MOSI)   11
 *      CS           10
 *      DC           13
 *      RST          14
 *
 * Requires these libraries (install via Arduino Library Manager):
 *   - Adafruit GFX Library
 *   - Adafruit ST7735 and ST7789 Library
 *
 * Your main sketch just needs to:
 *   #include "ST7789_Helper.h"
 *   lcdInit();
 *   lcdDrawRGB565(myImageArray, width, height);   // full-color bitmap
 *   lcdDrawBitmap1bpp(myMonoArray, width, height, ST77XX_WHITE, ST77XX_BLACK);
 */

#ifndef ST7789_HELPER_H
#define ST7789_HELPER_H

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// ---- Pin configuration (matches the schematic wiring) ----
#define LCD_PIN_SCLK 12
#define LCD_PIN_MOSI 11
#define LCD_PIN_CS   10
#define LCD_PIN_DC   13
#define LCD_PIN_RST  14

// Confirmed panel: 240 (native width) x 320 (native height), viewed in
// landscape (320x240) via rotation in lcdInit().
#define LCD_WIDTH  240
#define LCD_HEIGHT 320

// Global display object, available to the main sketch if it needs
// direct access to Adafruit_GFX drawing calls (text, shapes, etc.)
extern Adafruit_ST7789 tft;

// Call once in setup(). Initializes SPI on the chosen pins and the panel.
// `rotation` is 0-3, each step turning the image 90 degrees clockwise —
// if your image looks sideways, try the next value.
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

// Convenience: clear the whole screen to a solid color (default black).
void lcdClear(uint16_t color = ST77XX_BLACK);

// Fill the entire screen with a gradient between two RGB565 colors.
// `horizontal` = true blends left->right, false blends top->bottom.
// Draws one line per step (not a per-pixel buffer), so it's fast and
// uses negligible RAM regardless of panel resolution.
void lcdFillGradient(uint16_t colorStart, uint16_t colorEnd, bool horizontal = true);

#endif // ST7789_HELPER_H
