// RadioAudio.h: MAX98357A wrapper using ESP32-audioI2S.
#pragma once
#include <Arduino.h>

namespace RadioAudio {

void begin(const char *streamUrl, int bclk, int lrc, int dout, int volume = 15);

void loop();

void playUrl(const char *streamUrl);

void setVolume(int volume); // 0..21
bool isRunning();

typedef void (*MetadataCallback)(const char *station, const char *artist, const char *title);
void onMetadataChanged(MetadataCallback cb);

}