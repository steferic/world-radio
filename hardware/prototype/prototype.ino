/*
  Wireframe Globe — ST7789 TFT
  -----------------------------------------------------------
  Spinning wireframe globe on a 240×320 ST7789 panel.
  WiFi and audio streaming have been removed.

  Required libraries (install via Arduino Library Manager):
    - Adafruit GFX Library
    - Adafruit ST7735 and ST7789 Libraries

  TFT wiring:
    TFT CS  → GPIO 5
    TFT DC  → GPIO 2
    TFT RST → GPIO 4
*/

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// TFT pins
#define TFT_CS   5
#define TFT_DC   2
#define TFT_RST  4

// Display setup
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

#define PANEL_WIDTH  240
#define PANEL_HEIGHT 320
#define HALF_HEIGHT  (PANEL_HEIGHT / 2)

// Sphere appearance
#define SPHERE_RADIUS_PX   95
#define CENTER_X           120
#define CENTER_Y           160
#define NUM_LAT_RINGS      7
#define NUM_LON_LINES      8
#define SEGMENTS_PER_LINE  28
#define TILT_DEG           20.0
#define ROTATION_STEP_DEG  4.0
#define FRAME_DELAY_MS     5

#define RGB565(r, g, b) ((uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)))
#define COLOR_FRONT  ST77XX_CYAN
#define COLOR_BACK   RGB565(50, 50, 50)
#define COLOR_LIGHT_MODE false

// Graphics canvas
GFXcanvas16 *halfCanvas = nullptr;

struct Point3D { float x, y, z; };

Point3D rotateY(Point3D p, float rad) {
  float c = cos(rad), s = sin(rad);
  return { p.x * c + p.z * s, p.y, -p.x * s + p.z * c };
}

Point3D rotateX(Point3D p, float rad) {
  float c = cos(rad), s = sin(rad);
  return { p.x, p.y * c - p.z * s, p.y * s + p.z * c };
}

Point3D sphereToCartesian(float latDeg, float lonDeg) {
  float lat = radians(latDeg), lon = radians(lonDeg);
  return { cos(lat) * cos(lon), sin(lat), cos(lat) * sin(lon) };
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

float currentSpin = 0;

void setup() {
  Serial.begin(115200);
  delay(200);

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

void loop() {
  for (int half = 0; half < 2; half++) {
    int yOffset = half * HALF_HEIGHT;
    halfCanvas->fillScreen(ST77XX_BLACK);
    drawSphereFrame(*halfCanvas, currentSpin, yOffset);
    tft.drawRGBBitmap(0, yOffset, halfCanvas->getBuffer(), PANEL_WIDTH, HALF_HEIGHT);
  }

  currentSpin += ROTATION_STEP_DEG;
  if (currentSpin >= 360.0f) currentSpin -= 360.0f;

  delay(FRAME_DELAY_MS);
}
