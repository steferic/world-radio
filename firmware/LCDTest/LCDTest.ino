/*
 * LCDTest.ino
 * ------------------
 * Self-contained bring-up test for the ST7789Helper wrapper.
 * No external image files needed — all test data below is generated
 * either at compile time (the box/X logo) or on the fly (the gradient).
 *
 * Put ST7789_Helper.h and ST7789_Helper.cpp in this same sketch folder.
 *
 * What you should see, in order:
 *   1. Five color bars (red/green/blue/white/black) across the screen
 *      -- confirms backlight, wiring, and driver/rotation are correct
 *   2. A full-screen blue->magenta gradient
 *      -- confirms lcdFillGradient()
 *   3. A 32x32 black/white box-and-X pattern in the top-left corner
 *      -- confirms lcdDrawBitmap1bpp() / 1-bit bitmap data, over the gradient
 *   4. White "LCD ready" text below the logo
 *      -- confirms direct Adafruit_GFX calls (tft.print, etc.) still work
 *   5. A small square in the bottom-right that blinks every second
 *      -- confirms the board is alive and still running in loop()
 */

#include "ST7789_Helper.h"

// If the image looks rotated 90 degrees, try 1, 2, or 3 here.
// 0 = normal, 1 = 90 CW, 2 = 180, 3 = 270 CW
const uint8_t LCD_ROTATION = 1;

// --- 1bpp test logo: 32x32 box outline with a diagonal X ---
// Generated in advance (MSB-first, byte-padded rows); not hand-typed.
const int16_t LOGO_W = 32;
const int16_t LOGO_H = 32;
const uint8_t testLogoBitmap[] PROGMEM = {
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x00, 0x00, 0x0F,
  0xF8, 0x00, 0x00, 0x1F, 0xDC, 0x00, 0x00, 0x3B, 0xCE, 0x00, 0x00, 0x73,
  0xC7, 0x00, 0x00, 0xE3, 0xC3, 0x80, 0x01, 0xC3, 0xC1, 0xC0, 0x03, 0x83,
  0xC0, 0xE0, 0x07, 0x03, 0xC0, 0x70, 0x0E, 0x03, 0xC0, 0x38, 0x1C, 0x03,
  0xC0, 0x1C, 0x38, 0x03, 0xC0, 0x0E, 0x70, 0x03, 0xC0, 0x07, 0xE0, 0x03,
  0xC0, 0x03, 0xC0, 0x03, 0xC0, 0x03, 0xC0, 0x03, 0xC0, 0x07, 0xE0, 0x03,
  0xC0, 0x0E, 0x70, 0x03, 0xC0, 0x1C, 0x38, 0x03, 0xC0, 0x38, 0x1C, 0x03,
  0xC0, 0x70, 0x0E, 0x03, 0xC0, 0xE0, 0x07, 0x03, 0xC1, 0xC0, 0x03, 0x83,
  0xC3, 0x80, 0x01, 0xC3, 0xC7, 0x00, 0x00, 0xE3, 0xCE, 0x00, 0x00, 0x73,
  0xDC, 0x00, 0x00, 0x3B, 0xF8, 0x00, 0x00, 0x1F, 0xF0, 0x00, 0x00, 0x0F,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[LCDTest] starting");

  lcdInit(LCD_ROTATION);

  // 1. Color bars -- confirms wiring/driver/rotation before anything fancy
  const uint16_t bars[] = {ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE, ST77XX_WHITE, ST77XX_BLACK};
  for (int i = 0; i < 5; i++) {
    tft.fillRect(0, i * (tft.height() / 5), tft.width(), tft.height() / 5, bars[i]);
  }
  delay(1200);
  lcdClear();

  // 2. Full-screen gradient -- blue (left) to magenta (right)
  lcdFillGradient(ST77XX_BLUE, ST77XX_MAGENTA, true);
  delay(2000);

  // 3. 1bpp box/X logo, white on black, drawn over the gradient
  lcdDrawBitmap1bpp(testLogoBitmap, LOGO_W, LOGO_H,
                     ST77XX_WHITE, ST77XX_BLACK,
                     10, 10);

  // 4. Text via direct Adafruit_GFX calls
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, LOGO_H + 20);
  tft.print("LCD ready");

  Serial.printf("[LCDTest] panel %dx%d drawing\n", tft.width(), tft.height());
}

void loop() {
  // 5. Heartbeat square, bottom-right corner, toggles every second so you
  // can tell at a glance the board hasn't hung or reset.
  static bool on = false;
  static uint32_t lastToggle = 0;
  if (millis() - lastToggle >= 1000) {
    lastToggle = millis();
    on = !on;
    uint16_t color = on ? ST77XX_YELLOW : ST77XX_BLACK;
    tft.fillRect(tft.width() - 16, tft.height() - 16, 12, 12, color);
  }
}
