/*
  Wireframe Globe — ST7789 TFT
  -----------------------------------------------------------
  Spinning wireframe globe on a 240×320 ST7789 panel.

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
#include "config.h"
#include "geometry.h"
#include "display.h"

float currentSpin = 0;

void setup() {
  Serial.begin(115200);
  delay(200);
  displayInit();
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
