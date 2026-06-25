#include "display.h"
#include <Arduino.h>

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16 *halfCanvas = nullptr;

void displayInit() {
  tft.init(PANEL_WIDTH, PANEL_HEIGHT);
  tft.invertDisplay(COLOR_LIGHT_MODE);
  tft.fillScreen(ST77XX_BLACK);

  Serial.print(F("Free heap before canvas alloc: "));
  Serial.println(ESP.getFreeHeap());

  halfCanvas = new GFXcanvas16(PANEL_WIDTH, HALF_HEIGHT);
  if (!halfCanvas || !halfCanvas->getBuffer()) {
    Serial.println(F("ERROR: canvas alloc failed — insufficient RAM."));
    while (1) { delay(1000); }
  }

  Serial.print(F("Free heap after canvas alloc: "));
  Serial.println(ESP.getFreeHeap());
}

void project(Point3D p, float spinRad, int &outX, int &outY, float &outZ) {
  Point3D spun   = rotateY(p, spinRad);
  Point3D tilted = rotateX(spun, radians(TILT_DEG));
  outX = CENTER_X + (int)(tilted.x * SPHERE_RADIUS_PX);
  outY = CENTER_Y - (int)(tilted.y * SPHERE_RADIUS_PX);
  outZ = tilted.z;
}

void drawSegment(Adafruit_GFX &gfx, int x0, int y0, int x1, int y1, float zAvg, int yOffset) {
  uint16_t color = (zAvg >= 0) ? COLOR_FRONT : COLOR_BACK;
  gfx.drawLine(x0, y0 - yOffset, x1, y1 - yOffset, color);
}

void drawSphereFrame(Adafruit_GFX &gfx, float spinDeg, int yOffset) {
  float spinRad = radians(spinDeg);

  for (int ring = 0; ring < NUM_LAT_RINGS; ring++) {
    float lat = -75.0f + ring * (150.0f / (NUM_LAT_RINGS - 1));
    int prevX = 0, prevY = 0; float prevZ = 0; bool havePrev = false;
    for (int seg = 0; seg <= SEGMENTS_PER_LINE; seg++) {
      float lon = seg * (360.0f / SEGMENTS_PER_LINE);
      Point3D p = sphereToCartesian(lat, lon);
      int sx, sy; float sz;
      project(p, spinRad, sx, sy, sz);
      if (havePrev) drawSegment(gfx, prevX, prevY, sx, sy, (prevZ + sz) / 2.0f, yOffset);
      prevX = sx; prevY = sy; prevZ = sz; havePrev = true;
    }
  }

  for (int m = 0; m < NUM_LON_LINES; m++) {
    float lon = m * (360.0f / NUM_LON_LINES);
    int prevX = 0, prevY = 0; float prevZ = 0; bool havePrev = false;
    for (int seg = 0; seg <= SEGMENTS_PER_LINE; seg++) {
      float lat = -90.0f + seg * (180.0f / SEGMENTS_PER_LINE);
      Point3D p = sphereToCartesian(lat, lon);
      int sx, sy; float sz;
      project(p, spinRad, sx, sy, sz);
      if (havePrev) drawSegment(gfx, prevX, prevY, sx, sy, (prevZ + sz) / 2.0f, yOffset);
      prevX = sx; prevY = sy; prevZ = sz; havePrev = true;
    }
  }
}