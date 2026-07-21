// AudioTest — internet-radio audio bring-up, ESP32-S3 + PSRAM.
//
// Board (Tools):  Your specific S3 dev board (e.g. "ESP32S3 Dev Module")
//                 PSRAM: "OPI PSRAM" or "QSPI PSRAM" -- MUST match your board's
//                        PSRAM chip type, check your board's product page/silkscreen.
//                        Get this wrong and PSRAM silently won't be used.
//                 Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)" (or similar)
//                 USB CDC On Boot: "Enabled" if you want Serial over the native
//                        USB port on boards without a separate USB-UART chip.
//
// Wiring (avoid GPIO19/20 (native USB D-/D+), GPIO0/3/45/46 (strapping pins),
// and whatever pins your board uses for PSRAM if it's an octal-PSRAM module):
//   MAX98357A BCLK -> GPIO 36
//   MAX98357A LRC  -> GPIO 35
//   MAX98357A DIN  -> GPIO 37

#include <Arduino.h>
#include <WiFi.h>
#include "RadioAudio.h"

// ---- WiFi ----
#define WIFI_SSID "******"
#define WIFI_PASS "******"

// ---- MAX98357A pins ----
#define PIN_I2S_BCLK 36
#define PIN_I2S_LRC 35
#define PIN_I2S_DOUT 37

// static const char *STREAM_URL = "https://streams.kqed.org/kqedradio";
static const char *STREAM_URL = "http://ice1.somafm.com/groovesalad-128-mp3";

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[AudioTest] boot (S3)");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  WiFi.setSleep(false);
  Serial.printf("[AudioTest] connecting to %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.printf("\n[AudioTest] wifi ok, ip=%s\n", WiFi.localIP().toString().c_str());

  RadioAudio::begin(STREAM_URL, PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DOUT, /*volume=*/15);
}

void loop() {
  RadioAudio::loop();
}
