#include "GraphicsHelper.h"

void graphicsInit() {

}

void draw() {

}
 
void drawThinCircle(int16_t x, int16_t y, int16_t radius, uint16_t color) {
  tft.drawCircle(x, y, radius, color);
}
 
void drawCenteredCircleDemo() {
  lcdClear(ST77XX_BLACK);
 
  int16_t cx = tft.width() / 2;
  int16_t cy = tft.height() / 2;
 
  // Margin keeps the outline from touching the panel edge.
  const int16_t margin = 8;
  int16_t maxRadius = (min(tft.width(), tft.height()) / 2) - margin;
 
  drawThinCircle(cx, cy, maxRadius, ST77XX_WHITE);
}