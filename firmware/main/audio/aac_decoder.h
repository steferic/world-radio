#pragma once

#include "audio_decoder.h"

// Returns a singleton audio_decoder_t backed by libhelix-aac. audio_player
// owns the pointer for the lifetime of the program; nothing frees it.
//
// When the helix-aac component's source hasn't been vendored yet (see
// components/helix-aac/README.md), this decoder is compiled as a stub that
// logs "AAC not available" at reset time. That lets the rest of the firmware
// build without an AAC library present -- the routing/plumbing all still
// works, and AAC-labelled streams simply get rejected until the library is
// dropped in.
audio_decoder_t *aac_decoder_get(void);
