#include "ST7789_Helper.h"

// SPIClass instance bound to the ESP32-S3's FSPI (SPI2) peripheral.
// Using an explicit instance (rather than the default SPI object) lets
// us assign our own SCLK/MOSI pins before Adafruit_ST7789 uses it.
SPIClass hspi(FSPI);

// Adafruit_ST7789 constructor: (SPIClass*, CS, DC, RST)
Adafruit_ST7789 tft = Adafruit_ST7789(&hspi, LCD_PIN_CS, LCD_PIN_DC, LCD_PIN_RST);

void lcdInit(uint8_t rotation) {
  // MISO isn't used by the display, so pass -1 for that pin.
  hspi.begin(LCD_PIN_SCK, -1, LCD_PIN_MOSI, LCD_PIN_CS);

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
  // drawRGBBitmap expects a non-const pointer in some library versions;
  // the cast is safe since it only reads pixel data.
  tft.drawRGBBitmap(x, y, const_cast<uint16_t *>(pixels), w, h);
}

void lcdDrawBitmap1bpp(const uint8_t *bitmap, int16_t w, int16_t h,
                        uint16_t fgColor, uint16_t bgColor,
                        int16_t x, int16_t y) {
  // Adafruit_GFX has a built-in helper for exactly this format:
  // 1 bit per pixel, MSB first, each row padded to a whole byte.
  tft.drawBitmap(x, y, bitmap, w, h, fgColor, bgColor);
}

void lcdFillGradient(uint16_t colorStart, uint16_t colorEnd, bool horizontal) {
  // Unpack RGB565 -> separate 5/6/5 bit channels so we can interpolate
  // each one independently.
  uint8_t r0 = (colorStart >> 11) & 0x1F, g0 = (colorStart >> 5) & 0x3F, b0 = colorStart & 0x1F;
  uint8_t r1 = (colorEnd   >> 11) & 0x1F, g1 = (colorEnd   >> 5) & 0x3F, b1 = colorEnd   & 0x1F;

  int16_t steps = horizontal ? tft.width() : tft.height();
  for (int16_t i = 0; i < steps; i++) {
    uint8_t r = map(i, 0, steps - 1, r0, r1);
    uint8_t g = map(i, 0, steps - 1, g0, g1);
    uint8_t b = map(i, 0, steps - 1, b0, b1);
    uint16_t color = (r << 11) | (g << 5) | b;
    if (horizontal) {
      tft.drawFastVLine(i, 0, tft.height(), color);
    } else {
      tft.drawFastHLine(0, i, tft.width(), color);
    }
  }
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

  // Station name -- small, top-left
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(4, 4);
  tft.print(station[0] ? station : "(no station)");

  // Track title -- larger, roughly a third of the way down
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(4, tft.height() * 0.30);
  tft.print(title[0] ? title : "(no title)");

  // Artist -- small, below the title
  if (artist[0]) {
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(4, tft.height() * 0.55);
    tft.print(artist);
  }
}
