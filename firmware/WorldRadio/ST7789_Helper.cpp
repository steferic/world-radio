#include "ST7789_Helper.h"

// Using an explicit instance rather than the default SPI object lets
// us assign our own SCLK/MOSI pins before Adafruit_ST7789 uses it.
SPIClass hspi(FSPI);

Adafruit_ST7789 tft = Adafruit_ST7789(&hspi, LCD_PIN_CS, LCD_PIN_DC, LCD_PIN_RST);

void lcdInit(uint8_t rotation) {
  // MISO isn't used by the display, so pass -1 for that pin.
  hspi.begin(LCD_PIN_SCLK, -1, LCD_PIN_MOSI, LCD_PIN_CS);

  tft.init(LCD_WIDTH, LCD_HEIGHT);
  tft.setSPISpeed(40000000); // 40 MHz
  tft.setRotation(rotation);
  lcdClear();
}

void lcdClear(uint16_t color) {
  tft.fillScreen(color);
}

void lcdDrawRGB565(const uint16_t *pixels, int16_t w, int16_t h,
                    int16_t x, int16_t y) {
  // drawRGBBitmap expects a non-const pointer in some library versions.
  tft.drawRGBBitmap(x, y, const_cast<uint16_t *>(pixels), w, h);
}

void lcdDrawBitmap1bpp(const uint8_t *bitmap, int16_t w, int16_t h,
                        uint16_t fgColor, uint16_t bgColor,
                        int16_t x, int16_t y) {
  // We use the Adafruit_GFX built-in helper for this format.
  tft.drawBitmap(x, y, bitmap, w, h, fgColor, bgColor);
}

void lcdShowStatus(const char *message, uint16_t color) {
  lcdClear(ST77XX_BLACK);
  tft.setTextWrap(true);
  tft.setTextSize(2);
  tft.setTextColor(color);

  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(message, 0, 0, &x1, &y1, &w, &h);
  int16_t cx = (tft.width() - (int16_t)w) / 2;
  int16_t cy = (tft.height() - (int16_t)h) / 2;
  if (cx < 4) cx = 4;
  if (cy < 4) cy = 4;
  tft.setCursor(cx, cy);
  tft.print(message);
}

void lcdShowNowPlaying(const char *station, const char *artist, const char *title) {
  lcdClear(ST77XX_BLACK);
  tft.setTextWrap(true);

  // Station name
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(4, 4);
  tft.print(station[0] ? station : "(no station)");

  // Track title
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(4, tft.height() * 0.30);
  tft.print(title[0] ? title : "(no title)");

  // Artist
  if (artist[0]) {
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(4, tft.height() * 0.55);
    tft.print(artist);
  }
}
