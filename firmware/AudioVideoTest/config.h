#pragma once

// ---- WiFi ----
#define WIFI_SSID "SM-G970U72a"
#define WIFI_PASS "0000005958"
//#define WIFI_SSID "Kelly Highway"
//#define WIFI_PASS "w@yn3_1c3_v@ult"

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
