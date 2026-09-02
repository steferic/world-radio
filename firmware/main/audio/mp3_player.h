#pragma once

#include "esp_err.h"

// Sets up the I2S TX channel for the MAX98357A and
// starts the decode task, which pulls encoded bytes out of the audio_pipe
// ring buffer, decodes them with minimp3, and writes PCM out over I2S.
// The I2S clock is reconfigured on the fly if the stream's sample rate
// changes between frames.
esp_err_t mp3_player_init(void);
