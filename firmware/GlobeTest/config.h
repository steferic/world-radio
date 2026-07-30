#pragma once

// ---- WiFi ----
//#define WIFI_SSID "SM-G970U72a"
//#define WIFI_PASS "0000005958"
//#define WIFI_SSID "Kelly Highway"
//#define WIFI_PASS "w@yn3_1c3_v@ult"
#define WIFI_SSID "Verizon_4FBPQP"
#define WIFI_PASS "gut4-nil-blithe"

// ---- 2.4" ILI9341 SPI LCD (Waveshare 2.4inch LCD Module) ----
#define LCD_PIN_SCK  12
#define LCD_PIN_MOSI 11
#define LCD_PIN_CS   10
#define LCD_PIN_RST  9
#define LCD_PIN_DC   13
#define LCD_PIN_RST  14
#define LCD_ROTATION 3

// ---- MAX98357A I2S amp pins ----
#define PIN_I2S_LRC  35
#define PIN_I2S_BCLK 36
#define PIN_I2S_DOUT 37

// ---- Rotary encoder (EC11 w/ push) ----
#define PIN_ENC_A  18
#define PIN_ENC_B  16
#define PIN_ENC_SW 17

// ---- API & STREAM ----
#define API_URL "https://world-radio-v370.onrender.com/api"
#define TEST_STREAM_URL "http://ice1.somafm.com/groovesalad-128-mp3"
