#ifndef GRAPHICS_HELPER_H
#define GRAPHICS_HELPER_H

#include <Arduino.h>
#include "ST7789_Helper.h"

void graphicsInit();

void draw();

void drawThinCircle(int16_t x, int16_t y, int16_t radius, uint16_t color = ST77XX_WHITE);

void drawCenteredCircleDemo();

#endif