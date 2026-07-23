// ScreenTest — LCD bring-up. No WiFi, no audio: just light the panel and draw,
// so you can confirm power + wiring + driver before touching the rest.
//
// Works on either board; the pin map is chosen per chip, because the two are
// NOT interchangeable:
//   ESP32-S3    -> the project pin map from WorldRadio/config.h
//   classic ESP32 -> VSPI pins below. (config.h's LCD pins 8-11 are the SPI
//                    FLASH on a classic ESP32 — wiring a screen there breaks
//                    boot, so never use the S3 map on that chip.)
//
// Power, either board:
//   VCC -> 3V3   (module has its own regulator/level shifter, but 3V3 is the
//   GND -> GND    safe default next to 3.3V logic)
//   BL  -> the BL pin below, or straight to 3V3 for an always-on backlight.

#include <Arduino.h>
#include <LovyanGFX.hpp>

#if CONFIG_IDF_TARGET_ESP32S3
  #include "../WorldRadio/config.h" // PIN_LCD_* — the real project wiring
  #define LCD_SPI_HOST SPI2_HOST
#else
  // ---- classic ESP32 (ESP32-D0WD / WROOM-32) ----
  // Avoids GPIO 6-11 (flash), 12 (strapping), 34-39 (input-only) and the
  // MAX98357A pins.
  #define PIN_LCD_SCK 18
  #define PIN_LCD_MOSI 23
  #define PIN_LCD_CS 5
  #define PIN_LCD_DC 21
  #define PIN_LCD_RST 22
  #define PIN_LCD_BL 19
  #define LCD_SPI_HOST VSPI_HOST
#endif

class LGFX : public lgfx::LGFX_Device {
  // Swap for Panel_ST7789 / Panel_ST7735S if your module uses a different driver.
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;

public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = LCD_SPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 27000000; // conservative for breadboard jumpers
      cfg.pin_sclk = PIN_LCD_SCK;
      cfg.pin_mosi = PIN_LCD_MOSI;
      cfg.pin_miso = -1;
      cfg.pin_dc = PIN_LCD_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = PIN_LCD_CS;
      cfg.pin_rst = PIN_LCD_RST;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.invert = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = PIN_LCD_BL;
      cfg.invert = false;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};

static LGFX lcd;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n[ScreenTest] sck=%d mosi=%d cs=%d dc=%d rst=%d bl=%d\n",
                PIN_LCD_SCK, PIN_LCD_MOSI, PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST, PIN_LCD_BL);

  lcd.init();
  lcd.setRotation(0);
  lcd.setBrightness(255);

  // Color bars first: if the backlight works but these are wrong (colors
  // swapped, mirrored, inverted), that's the panel class / invert / rotation,
  // not the wiring.
  const uint16_t bars[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE, TFT_BLACK};
  for (int i = 0; i < 5; i++) {
    lcd.fillRect(0, i * (lcd.height() / 5), lcd.width(), lcd.height() / 5, bars[i]);
  }
  delay(1200);

  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setTextDatum(lgfx::middle_center);
  lcd.drawString("WORLD RADIO", lcd.width() / 2, lcd.height() / 2 - 20, &fonts::Font4);
  lcd.drawString("screen OK", lcd.width() / 2, lcd.height() / 2 + 10, &fonts::Font2);
  lcd.drawRect(4, 4, lcd.width() - 8, lcd.height() - 8, TFT_WHITE);

  Serial.printf("[ScreenTest] panel %dx%d drawing\n", lcd.width(), lcd.height());
}

void loop() {
  // Sweep the backlight so it's obvious the BL pin is under our control.
  static int b = 40;
  static int dir = 8;
  b += dir;
  if (b >= 255 || b <= 40) dir = -dir;
  lcd.setBrightness(b);
  delay(40);
}
