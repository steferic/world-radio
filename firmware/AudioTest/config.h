// config.h — WiFi credentials and the pin map.
//
// Edit WIFI_SSID / WIFI_PASS before flashing. The pin choices avoid the S3's
// strapping pins (0, 3, 45, 46) and the pins reserved by octal PSRAM on R8
// modules (35–37).

#pragma once

// ---- WiFi ----
#define WIFI_SSID "YOUR WIFI SSID"
#define WIFI_PASS "YOUR WIFI PASSWORD"

// ---- 2.4" ILI9341 SPI LCD (Waveshare 2.4inch LCD Module) ----
#define PIN_LCD_SCK 12
#define PIN_LCD_MOSI 11
#define PIN_LCD_CS 10
#define PIN_LCD_DC 8
#define PIN_LCD_RST 9
#define PIN_LCD_BL 14

// ---- MAX98357A I2S amp pins ----
#define PIN_I2S_BCLK 36
#define PIN_I2S_LRC 35
#define PIN_I2S_DOUT 37

// ---- Rotary encoder (EC11 w/ push) ----
// (A moved 15 -> 18: GPIO 15 is now the amp's LRC)
#define PIN_ENC_A 18
#define PIN_ENC_B 16
#define PIN_ENC_SW 17
