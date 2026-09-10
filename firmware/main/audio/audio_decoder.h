#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// Codec label attached to a stream. The codec is set either from the API's
// "format" field, or by matching against the first few bytes of the stream.
typedef enum {
    AUDIO_FORMAT_UNKNOWN = 0,
    AUDIO_FORMAT_MP3,
    AUDIO_FORMAT_AAC,
} audio_format_t;

audio_format_t audio_format_from_string(const char *s);
const char    *audio_format_to_string(audio_format_t fmt);

// Look at the first `len` bytes of an encoded stream and guess the format.
// Returns AUDIO_FORMAT_UNKNOWN if nothing in the buffer looks like a valid
// sync word yet -- caller should keep filling and try again.
audio_format_t audio_format_sniff(const uint8_t *buf, size_t len);

typedef enum {
    AUDIO_DECODE_OK,        // pcm_out was filled with `info->samples_per_channel * info->channels` samples
    AUDIO_DECODE_SKIP,      // consumed some bytes (ID3, ADTS filler, etc.) but produced no audio -- keep going
    AUDIO_DECODE_NEED_MORE, // didn't consume anything, buffer is too small to contain a full frame -- feed more input
    AUDIO_DECODE_ERROR,     // buffer contains something that doesn't look like a frame -- caller should drop bytes and resync
} audio_decode_status_t;

typedef struct {
    int    sample_rate_hz;
    int    channels;
    size_t samples_per_channel;
} audio_frame_info_t;

typedef struct audio_decoder audio_decoder_t;

// A decoder is a thin vtable + opaque state pointer. Concrete decoders
// (mp3_decoder, aac_decoder) fill in these slots and manage their own state.
//
// Contract for `decode`:
//   *bytes_consumed is ALWAYS set, regardless of return value (0 is legal).
//   pcm_out is a flat interleaved-int16 buffer of `pcm_out_max_samples`
//   int16_t slots (i.e. for stereo, that's 2 * frames-of-headroom).
//   info->sample_rate_hz / channels / samples_per_channel are only valid
//   when the return is AUDIO_DECODE_OK.
struct audio_decoder {
    const char     *name;
    audio_format_t  format;

    esp_err_t (*init)(audio_decoder_t *self);
    void      (*reset)(audio_decoder_t *self);   // called at every new stream, before the first decode
    audio_decode_status_t (*decode)(audio_decoder_t *self,
                                    const uint8_t *buf, size_t buf_len,
                                    size_t *bytes_consumed,
                                    int16_t *pcm_out, size_t pcm_out_max_samples,
                                    audio_frame_info_t *info);
    void      (*deinit)(audio_decoder_t *self);

    void *state;
};
