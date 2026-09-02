#pragma once

#include <stdint.h>
#include "esp_err.h"

// Brings up the ST7789 over SPI (pins/dimensions in config.h), runs its
// init sequence, and clears the screen to black. Call once at startup.
esp_err_t lcd_driver_init(void);

// Fills a rectangle with a single RGB565 color. Coordinates are clipped to
// the panel; a rectangle fully off-screen is a silent no-op.
void lcd_fill_rect(int x, int y, int w, int h, uint16_t color);

// Blits a caller-supplied RGB565 pixel buffer (w*h pixels, row-major,
// native byte order) into the rectangle starting at (x, y). The full
// rectangle must be on-screen -- out-of-bounds calls are logged and
// dropped rather than clipped, since a caller asking to draw a bitmap
// partly off-screen is almost always a layout bug worth noticing.
void lcd_draw_bitmap(int x, int y, int w, int h, const uint16_t *pixels);

// Common RGB565 colors.
#define LCD_COLOR_BLACK   0x0000
#define LCD_COLOR_WHITE   0xFFFF
#define LCD_COLOR_RED     0xF800
#define LCD_COLOR_GREEN   0x07E0
#define LCD_COLOR_BLUE    0x001F
#define LCD_COLOR_YELLOW  0xFFE0
#define LCD_COLOR_CYAN    0x07FF
#define LCD_COLOR_GRAY    0x8410
