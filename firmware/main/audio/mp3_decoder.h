#pragma once

#include "audio_decoder.h"

// Returns a singleton audio_decoder_t backed by minimp3. audio_player owns
// the pointer for the lifetime of the program; nothing frees it.
audio_decoder_t *mp3_decoder_get(void);
