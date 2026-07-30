#include <Arduino.h>
#include <WiFi.h>
#include "RadioAudio.h"
#include "config.h"


// static const char *STREAM_URL = "https://streams.kqed.org/kqedradio";
static const char *STREAM_URL = "http://ice1.somafm.com/groovesalad-128-mp3";

static const int PIN_VOLUME_POT = 1; // GPIO1
uint32_t lastPotPoll = 0;
int lastVolume = -1;

void pollVolumePot() {
  if (millis() - lastPotPoll < 100) return;
  lastPotPoll = millis();

  int raw = analogRead(PIN_VOLUME_POT);   // 0-4095
  int vol = map(raw, 0, 4095, 0, 21);     // Audio lib volume is 0-21

  if (vol != lastVolume) {
    lastVolume = vol;
    RadioAudio::setVolume(vol);
    Serial.printf("[AudioTest] volume -> %d\n", vol);
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[AudioTest] boot (S3)");

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_VOLUME_POT, ADC_11db); // full 0-3.3V range

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
  pollVolumePot();
}
