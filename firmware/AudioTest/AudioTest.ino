#include <Arduino.h>
#include <WiFi.h>
#include "RadioAudio.h"


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
