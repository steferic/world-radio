#pragma once

#include "esp_err.h"
#include "audio_decoder.h"

// Brings up audio_output and starts the decode task on core 1. The task
// pulls encoded bytes out of the audio_pipe ring buffer, routes them through
// whichever decoder matches the current stream's format, and hands PCM to
// audio_output for playback.
esp_err_t audio_player_init(void);

// Called at every new stream (http_stream picks a new station, or a shuffle
// forces a reconnect). `fmt` is the codec label from the station API. If
// it's AUDIO_FORMAT_UNKNOWN the decode task will sniff the first bytes and
// pick a decoder on its own.
//
// This is a hand-off, not a synchronous switch: it flags "a new stream is
// coming" and the decode task drops any staged bytes at the next iteration.
// Safe to call from any task.
void audio_player_new_stream(audio_format_t fmt);
