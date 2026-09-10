#include "mp3_decoder.h"

#include <string.h>

#include "esp_log.h"
#include "minimp3.h"

static const char *TAG = "mp3_decoder";

static mp3dec_t s_mp3d;

static esp_err_t mp3_init(audio_decoder_t *self)
{
    (void)self;
    mp3dec_init(&s_mp3d);
    return ESP_OK;
}

static void mp3_reset(audio_decoder_t *self)
{
    (void)self;
    // minimp3 keeps a small amount of scratch state between frames; a full
    // re-init is the safe way to drop it when we hop to a new stream.
    mp3dec_init(&s_mp3d);
}

static audio_decode_status_t mp3_decode(audio_decoder_t *self,
                                        const uint8_t *buf, size_t buf_len,
                                        size_t *bytes_consumed,
                                        int16_t *pcm_out, size_t pcm_out_max_samples,
                                        audio_frame_info_t *info)
{
    (void)self;
    *bytes_consumed = 0;

    // minimp3 needs at least the header + frame body, and returns 0 samples
    // (with info.frame_bytes==0) when it can't yet see a full frame.
    mp3dec_frame_info_t mi;
    int samples = mp3dec_decode_frame(&s_mp3d, buf, (int)buf_len, pcm_out, &mi);

    if (mi.frame_bytes > 0) {
        *bytes_consumed = (size_t)mi.frame_bytes;
    } else {
        // No frame boundary found in the buffer -- caller must feed more.
        return AUDIO_DECODE_NEED_MORE;
    }

    if (samples <= 0) {
        // Consumed junk / ID3 padding but no PCM yet. Not an error.
        return AUDIO_DECODE_SKIP;
    }

    // minimp3 hands us `samples` samples PER CHANNEL, interleaved in pcm_out
    // when stereo, packed as mono when mi.channels == 1. audio_output wants
    // stereo, so expand mono to L=R in place -- but only if we have room.
    if (mi.channels == 1) {
        size_t needed = (size_t)samples * 2;
        if (needed > pcm_out_max_samples) {
            ESP_LOGW(TAG, "pcm_out too small for mono->stereo expand (%u > %u)",
                     (unsigned)needed, (unsigned)pcm_out_max_samples);
            return AUDIO_DECODE_ERROR;
        }
        for (int i = samples - 1; i >= 0; i--) {
            int16_t s = pcm_out[i];
            pcm_out[2 * i] = s;
            pcm_out[2 * i + 1] = s;
        }
    }

    info->sample_rate_hz      = mi.hz;
    info->channels            = 2;   // we always hand audio_output stereo
    info->samples_per_channel = (size_t)samples;
    return AUDIO_DECODE_OK;
}

static void mp3_deinit(audio_decoder_t *self)
{
    (void)self;
}

static audio_decoder_t s_mp3_decoder = {
    .name   = "mp3 (minimp3)",
    .format = AUDIO_FORMAT_MP3,
    .init   = mp3_init,
    .reset  = mp3_reset,
    .decode = mp3_decode,
    .deinit = mp3_deinit,
    .state  = NULL,
};

audio_decoder_t *mp3_decoder_get(void)
{
    return &s_mp3_decoder;
}
