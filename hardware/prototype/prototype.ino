/*
  Wireframe Globe — ST7789 TFT
  -----------------------------------------------------------
  Spinning wireframe globe on a 240×320 ST7789 panel.
*/

#include <Arduino.h>
#include <SPI.h>
#include "config.h"
#include "geometry.h"
#include "display.h"
#include "network.h"

float currentSpin = 0;

void setup() {
  Serial.begin(115200);
  delay(200);
  wifiInit();
  displayInit();
}

void loop() {
  wifiUpdate();
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
