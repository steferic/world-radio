#pragma once

#include <stdint.h>

// Each glyph occupies a 5-wide x 7-tall cell (columns 0-2 hold the actual
// strokes, columns 3-4 are always blank and serve as inter-character
// spacing). Only uppercase letters, digits, and basic punctuation exist --
// font5x7_lookup() upper-cases its input, so lowercase text still renders,
// just without a distinct lowercase shape.
#define FONT5X7_WIDTH  5
#define FONT5X7_HEIGHT 7

// Fills out_cols[5] with the glyph's column bytes (bit0 = top row of the
// cell, bit6 = bottom row). Unknown characters map to a blank glyph rather
// than failing, so a stray symbol just renders as a gap.
void font5x7_lookup(char ch, uint8_t out_cols[5]);
