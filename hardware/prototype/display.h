#ifndef DISPLAY_H
#define DISPLAY_H

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "config.h"
#include "geometry.h"

extern Adafruit_ST7789 tft;
extern GFXcanvas16 *halfCanvas;

void displayInit();
void project(Point3D p, float spinRad, int &outX, int &outY, float &outZ);
void drawSegment(Adafruit_GFX &gfx, int x0, int y0, int x1, int y1, float zAvg, int yOffset);
void drawSphereFrame(Adafruit_GFX &gfx, float spinDeg, int yOffset);

#endif