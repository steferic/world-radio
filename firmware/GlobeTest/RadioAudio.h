// RadioAudio.h: MAX98357A wrapper using ESP32-audioI2S

#pragma once
#include <Arduino.h>
#include "config.h"

namespace RadioAudio {

void begin(const char *streamUrl, int bclk, int lrc, int dout, int volume = 15);

void loop();

void playUrl(const char *streamUrl);

void setVolume(int volume);
bool isRunning();

// Fires whenever the station name or stream title metadata changes.
// `station` is the station/stream name; `artist` and `title` come from
// splitting the stream's "Artist - Title". If the stream doesn't send a
// " - " separator, `artist` is empty and the whole string lands in `title`.
// Any of the three may be empty depending on what the stream provides.
typedef void (*MetadataCallback)(const char *station, const char *artist, const char *title);
void onMetadataChanged(MetadataCallback cb);

}
