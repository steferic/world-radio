#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// Owns the I2S TX channel that talks to the MAX98357A amp, along with the
// analog-radio-style noise mixer that fades in whenever the audio_pipe runs
// dry. Decoder-agnostic: any of mp3_decoder / aac_decoder feed PCM here
// through audio_output_write().
esp_err_t audio_output_init(void);

// Emit one block of interleaved-stereo int16 samples at `sample_rate_hz`.
// If the sample rate changed since the last call the I2S clock is
// reconfigured in place, at the cost of a brief audible gap.
//
// `pcm` is mixed with fill-level-driven noise and multiplied by the current
// volume knob position IN PLACE before it goes to I2S, so the caller must
// not care about the buffer's contents after this returns. Blocks until the
// block has been handed to the DMA layer.
void audio_output_write(int16_t *pcm, size_t num_samples, int sample_rate_hz);

// Emit one block of pure noise at the last-known sample rate. Called by
// the decode task when the pipe is empty so I2S never starves and the user
// hears radio-static instead of silence during a stall.
void audio_output_write_starvation_noise(void);
