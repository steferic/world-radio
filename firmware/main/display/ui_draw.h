// ui_draw is essentially a wrapper around lcd_driver, exposing a number
// of helper files that make drawing shapes and text easier.
//
// IMPORTANT: every function in this module must be called only from the
// UI task. That's the only task that touches the LCD, which is why there
// is no mutex around any of this. If some future subsystem needs to draw,
// it should post a UI event and let the UI task do the drawing instead.

#pragma once

#include <stdint.h>
#include <stddef.h>

// config.h holds the panel dimensions LCD_WIDTH & LCD_HEIGHT that
// UI_FIELD_MAX_WIDTH is derived from, so it must be pulled in
#include "config.h"
#include "lcd_driver.h"
#include "font5x7.h"

// Shared layout constants. Screens use these so their fields line up
// vertically across screens.
#define UI_MARGIN_X         8
#define UI_FIELD_MAX_WIDTH  (LCD_WIDTH - 2 * UI_MARGIN_X)

// One glyph cell is FONT5X7_WIDTH+1 wide (+1 for inter-character gap) 
// and FONT5X7_HEIGHT tall, multiplied by the integer scale factor.
#define UI_CELL_W(scale) ((FONT5X7_WIDTH + 1) * (scale))
#define UI_CELL_H(scale) (FONT5X7_HEIGHT * (scale))

// Draws one glyph at (x, y) top-left, in `color` on `bg`, at the given
// integer pixel scale. Scale must be >= 1.
void ui_draw_glyph(int x, int y, char ch, uint16_t color, uint16_t bg, int scale);

// Clears a fixed-width horizontal field to 'bg' and then draws 'text'
// left-aligned in it, truncating with ".." if it doesn't fit.
void ui_draw_field(int x, int y, int max_width, int scale,
                   const char *text, uint16_t color, uint16_t bg);

// Same as ui_draw_field but centers the text horizontally within
// [x, x + max_width). Also clears the full field first.
void ui_draw_field_centered(int x, int y, int max_width, int scale,
                            const char *text, uint16_t color, uint16_t bg);

// Splits 's' across two lines of at most 'max_chars' each, breaking at
// the last space at or before the limit so words don't get chopped in
// half. If 's' fits on one line, second line is left "".
void ui_split_two_lines(const char *s,
                        char *l1, size_t l1_cap,
                        char *l2, size_t l2_cap,
                        size_t max_chars);

// Clears the entire panel to black. Called by ui_task on every screen
// swap so screens can rely on a blank canvas in enter().
void ui_draw_clear_screen(void);
