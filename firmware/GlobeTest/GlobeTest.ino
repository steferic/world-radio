// WorldRadioLCD.ino
// -----------------
// Combines RadioAudio (I2S streaming) with the ST7789 LCD helper: whenever
// the stream sends new station/title metadata, it's shown on screen.
//
// Files needed in this same sketch folder:
//   RadioAudio.h / RadioAudio.cpp   (I2S audio driver)
//   ST7789_Helper.h / ST7789_Helper.cpp   (LCD driver)
//   config.h

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "RadioAudio.h"
#include "ST7789_Helper.h"
#include "GraphicsHelper.h"
#include "config.h"

static const uint32_t STATION_FETCH_INTERVAL_MS = 30000; // 30 seconds
uint32_t lastStationFetch = 0;

static const int PIN_VOLUME_POT = 1; // GPIO1
uint32_t lastPotPoll = 0;
int lastVolume = -1;
int currentVolume = 15; // kept in sync so station switches preserve volume

void pollVolumePot() {
  if (millis() - lastPotPoll < 100) return;
  lastPotPoll = millis();

  int raw = analogRead(PIN_VOLUME_POT);   // 0-4095
  int vol = map(raw, 0, 4095, 0, 21);     // Audio lib volume is 0-21

  if (vol != lastVolume) {
    lastVolume = vol;
    currentVolume = 18; // matches the hardcoded RadioAudio::setVolume(21) call below
    RadioAudio::setVolume(18);//vol);
    Serial.printf("[WorldRadio] volume -> %d\n", vol);
  }
}

void onMetadataChanged(const char *station, const char *artist, const char *title) {
  Serial.printf("[WorldRadio] now playing: station=\"%s\" artist=\"%s\" title=\"%s\"\n",
                station, artist, title);
}

// Switches playback to a new stream URL without re-running I2S setup.
void playStreamUrl(const char *url, const char *displayName) {
  Serial.printf("[WorldRadio] switching to: %s (%s)\n", displayName ? displayName : "?", url);
  RadioAudio::playUrl(url);
}

// Fetches a random station from the API and switches to it.
void fetchAndPlayRandomStation() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WorldRadio] skip station fetch: wifi not connected");
    return;
  }

  HTTPClient http;
  http.setTimeout(5000);
  if (!http.begin(String(API_URL) + "/stations/random")) {
    Serial.println("[WorldRadio] http.begin() failed");
    return;
  }

  int code = http.GET();
  if (code != 200) {
    Serial.printf("[WorldRadio] station fetch failed, http code=%d\n", code);
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[WorldRadio] JSON parse failed: %s\n", err.c_str());
    return;
  }

  const char *url = doc["url"] | "";
  const char *name = doc["name"] | "";
  if (!url || url[0] == '\0') {
    Serial.println("[WorldRadio] station JSON had no usable url");
    return;
  }

  playStreamUrl(url, name);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[WorldRadio] boot (S3)");

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_VOLUME_POT, ADC_11db); // full 0-3.3V range

  lcdInit(LCD_ROTATION);
  drawCenteredCircleDemo();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  WiFi.setSleep(false);
  Serial.printf("[WorldRadio] connecting to %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.printf("\n[WorldRadio] wifi ok, ip=%s\n", WiFi.localIP().toString().c_str());

  RadioAudio::onMetadataChanged(onMetadataChanged);
  RadioAudio::begin(TEST_STREAM_URL, PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DOUT, /*volume=*/currentVolume);

  lastStationFetch = millis();
}

void loop() {
  RadioAudio::loop();
  pollVolumePot();

  if (millis() - lastStationFetch >= STATION_FETCH_INTERVAL_MS) {
    lastStationFetch = millis();
    fetchAndPlayRandomStation();
  }
}
