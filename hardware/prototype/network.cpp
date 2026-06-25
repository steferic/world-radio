#include "network.h"
#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <HTTPClient.h>

static unsigned long lastPrintMs = 0;

void wifiInit() {
  Serial.print(F("Connecting to WiFi"));
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }

  Serial.println();
  Serial.print(F("Connected. IP: "));
  Serial.println(WiFi.localIP());

  // Sync time via NTP (pool, UTC offset in seconds, DST offset in seconds)
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  Serial.print(F("Waiting for NTP sync"));
  struct tm timeInfo;
  while (!getLocalTime(&timeInfo)) {
    delay(500);
    Serial.print('.');
  }
  Serial.println(F(" done."));
}

void wifiUpdate() {
  if (millis() - lastPrintMs < 10000) return;
  lastPrintMs = millis();

  HTTPClient http;
  http.begin("https://kexp.streamguys1.com/kexp160.aac");
  int code = http.GET();

  if (code > 0) {
    Serial.print(F("Stream status: "));
    Serial.println(code);
  } else {
    Serial.print(F("Request failed: "));
    Serial.println(http.errorToString(code));
  }

  http.end();
}