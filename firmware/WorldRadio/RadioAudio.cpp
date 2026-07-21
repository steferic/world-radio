// RadioAudio.cpp — see RadioAudio.h for the public API.

#include "RadioAudio.h"
#include <WiFi.h>
#include <Audio.h> // schreibfaul1/ESP32-audioI2S
#include <esp_heap_caps.h>

namespace {

Audio audio;
const char *currentUrl = nullptr;
float currentBitrateBps = 128000.0f;

uint32_t lastCheck = 0;
uint32_t lastConnectAttempt = 0;
const uint32_t RECONNECT_COOLDOWN_MS = 10000;

uint32_t lastFilled = 0;
bool haveLastFilled = false;

void printNetDiagnostics(uint32_t elapsedMs) {
  uint32_t filled = audio.inBufferFilled();
  uint32_t bufSize = audio.inBufferSize();

  if (haveLastFilled) {
    int32_t delta = (int32_t)filled - (int32_t)lastFilled;
    float deltaSec = elapsedMs / 1000.0f;

    const float bytesConsumed = (currentBitrateBps / 8.0f) * deltaSec;
    float bytesArrived = delta + bytesConsumed;
    float incomingRateBps = bytesArrived / deltaSec;

    Serial.printf("[net] rssi=%ddBm heap=%u psram=%u buf=%u/%u (%.0f%%) rate=%.0f B/s (need %.0f B/s)\n",
                  WiFi.RSSI(), ESP.getFreeHeap(), ESP.getFreePsram(),
                  filled, bufSize, 100.0 * filled / bufSize,
                  incomingRateBps, currentBitrateBps / 8.0f);
  }

  lastFilled = filled;
  haveLastFilled = true;
}

}

namespace RadioAudio {

void begin(const char *streamUrl, int bclk, int lrc, int dout, int volume) {
  if (psramFound()) {
    Serial.printf("[RadioAudio] PSRAM found, size=%u bytes\n", ESP.getPsramSize());
  } else {
    Serial.println("[RadioAudio] WARNING: PSRAM NOT detected -- check Tools > PSRAM setting");
  }

  audio.setPinout(bclk, lrc, dout);
  audio.setVolume(volume);
  playUrl(streamUrl);
}

void loop() {
  audio.loop();

  if (millis() - lastCheck > 2000) {
    uint32_t now = millis();
    uint32_t elapsedMs = now - lastCheck;
    lastCheck = now;

    printNetDiagnostics(elapsedMs);

    if (!audio.isRunning() && (millis() - lastConnectAttempt > RECONNECT_COOLDOWN_MS)) {
      Serial.println("[RadioAudio] stream not running, reconnecting...");
      lastConnectAttempt = millis();
      audio.connecttohost(currentUrl);
    }
  }
}

void playUrl(const char *streamUrl) {
  currentUrl = streamUrl;
  haveLastFilled = false;
  Serial.printf("[RadioAudio] connecting to stream: %s\n", streamUrl);
  audio.connecttohost(streamUrl);
}

void setVolume(int volume) { audio.setVolume(volume); }
bool isRunning() { return audio.isRunning(); }

}

void audio_info(const char *info) { Serial.printf("[audio] %s\n", info); }
void audio_showstation(const char *info) { Serial.printf("[station] %s\n", info); }
void audio_showstreamtitle(const char *info) { Serial.printf("[title] %s\n", info); }
void audio_bitrate(const char *info) { Serial.printf("[bitrate] %s\n", info); }
void audio_eof_stream(const char *info) { Serial.printf("[eof] %s\n", info); }
