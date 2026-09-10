#include "aac_decoder.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "aac_decoder";

// HELIX_AAC_AVAILABLE is defined by components/helix-aac/CMakeLists.txt when
// the vendored Helix AAC sources are present. When it's not, we compile a
// stub decoder so the firmware still links and the routing plumbing can be
// exercised -- AAC streams just get rejected instead of being decoded.
#ifdef HELIX_AAC_AVAILABLE

#include "aacdec.h"

// One shared decoder handle for the lifetime of the program. Reset (freed +
// re-alloced) at every new stream so we don't inherit an old psychoacoustic
// state from a different station.
static HAACDecoder s_dec = NULL;

static esp_err_t aac_init(audio_decoder_t *self)
{
    (void)self;
    if (s_dec != NULL) {
        AACFreeDecoder(s_dec);
        s_dec = NULL;
    }
    s_dec = AACInitDecoder();
    if (s_dec == NULL) {
        ESP_LOGE(TAG, "AACInitDecoder failed (OOM?)");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void aac_reset(audio_decoder_t *self)
{
    (void)self;
    // AACFlushCodec keeps the handle alive but drops per-frame state that
    // would otherwise carry ADTS parsing offsets, gain history, etc. across
    // the stream boundary and corrupt the first frame of the new one.
    if (s_dec != NULL) {
        AACFlushCodec(s_dec);
    }
}

static audio_decode_status_t aac_decode(audio_decoder_t *self,
                                        const uint8_t *buf, size_t buf_len,
                                        size_t *bytes_consumed,
                                        int16_t *pcm_out, size_t pcm_out_max_samples,
                                        audio_frame_info_t *info)
{
    (void)self;
    *bytes_consumed = 0;

    if (s_dec == NULL) {
        return AUDIO_DECODE_ERROR;
    }

    // Find the ADTS sync word first: Helix will happily start decoding at
    // whatever offset we hand it, so filler / partial frames at the start
    // of the buffer would cause it to trip.
    int offset = AACFindSyncWord((unsigned char *)buf, (int)buf_len);
    if (offset < 0) {
        // No sync word visible yet. If the buffer isn't nearly full there's
        // just not enough data; if it IS nearly full this looks like garbage
        // and the caller will drop bytes to resync.
        return AUDIO_DECODE_NEED_MORE;
    }
    if (offset > 0) {
        *bytes_consumed = (size_t)offset;
        return AUDIO_DECODE_SKIP;
    }

    // AACDecode advances *inptr and decrements *bytesLeft by the number of
    // bytes it consumed. On success it writes up to AAC_MAX_NCHANS *
    // AAC_MAX_NSAMPS int16 samples into outbuf.
    unsigned char *inptr = (unsigned char *)buf;
    int bytesLeft = (int)buf_len;
    int err = AACDecode(s_dec, &inptr, &bytesLeft, pcm_out);

    size_t consumed_now = (size_t)(inptr - buf);
    if (consumed_now == 0) {
        // Helix didn't advance -- the frame is truncated. Ask for more.
        return AUDIO_DECODE_NEED_MORE;
    }
    *bytes_consumed = consumed_now;

    if (err != 0) {
        // Any non-zero code from Helix means the frame was rejected. Let the
        // caller drop this input and resync at the next ADTS boundary.
        ESP_LOGD(TAG, "AACDecode error %d, consumed %u bytes", err, (unsigned)consumed_now);
        return AUDIO_DECODE_ERROR;
    }

    AACFrameInfo fi;
    AACGetLastFrameInfo(s_dec, &fi);

    if (fi.outputSamps <= 0) {
        return AUDIO_DECODE_SKIP;
    }
    size_t total_samples = (size_t)fi.outputSamps;
    if (total_samples > pcm_out_max_samples) {
        ESP_LOGW(TAG, "AAC frame produced %u samples, buffer only %u -- dropping",
                 (unsigned)total_samples, (unsigned)pcm_out_max_samples);
        return AUDIO_DECODE_ERROR;
    }

    int channels = fi.nChans > 0 ? fi.nChans : 1;
    size_t samples_per_channel = total_samples / (size_t)channels;

    // audio_output wants interleaved stereo -- expand mono in place. Helix
    // writes mono packed (not L=R), so the same fanout we do in mp3_decoder.
    if (channels == 1) {
        size_t needed = samples_per_channel * 2;
        if (needed > pcm_out_max_samples) {
            ESP_LOGW(TAG, "pcm_out too small for AAC mono->stereo expand");
            return AUDIO_DECODE_ERROR;
        }
        for (int i = (int)samples_per_channel - 1; i >= 0; i--) {
            int16_t s = pcm_out[i];
            pcm_out[2 * i] = s;
            pcm_out[2 * i + 1] = s;
        }
    }

    // sampRateOut already accounts for SBR (HE-AAC's spectral band replication
    // doubles the output rate vs sampRateCore); trust it.
    info->sample_rate_hz      = fi.sampRateOut > 0 ? fi.sampRateOut : fi.sampRateCore;
    info->channels            = 2;
    info->samples_per_channel = samples_per_channel;
    return AUDIO_DECODE_OK;
}

static void aac_deinit(audio_decoder_t *self)
{
    (void)self;
    if (s_dec != NULL) {
        AACFreeDecoder(s_dec);
        s_dec = NULL;
    }
}

#else // !HELIX_AAC_AVAILABLE

static bool s_warned = false;

static esp_err_t aac_init(audio_decoder_t *self)
{
    (void)self;
    return ESP_OK;
}

static void aac_reset(audio_decoder_t *self)
{
    (void)self;
    if (!s_warned) {
        ESP_LOGW(TAG,
                 "AAC decoder not compiled in -- vendor libhelix-aac into "
                 "components/helix-aac/ and rebuild. See its README.md.");
        s_warned = true;
    }
}

static audio_decode_status_t aac_decode(audio_decoder_t *self,
                                        const uint8_t *buf, size_t buf_len,
                                        size_t *bytes_consumed,
                                        int16_t *pcm_out, size_t pcm_out_max_samples,
                                        audio_frame_info_t *info)
{
    (void)self; (void)buf; (void)buf_len; (void)pcm_out;
    (void)pcm_out_max_samples; (void)info;
    // Drop everything the caller hands us -- there's no decoder to feed.
    *bytes_consumed = buf_len;
    return AUDIO_DECODE_SKIP;
}

static void aac_deinit(audio_decoder_t *self)
{
    (void)self;
}

#endif // HELIX_AAC_AVAILABLE

static audio_decoder_t s_aac_decoder = {
#ifdef HELIX_AAC_AVAILABLE
    .name   = "aac (libhelix)",
#else
    .name   = "aac (stub, library not present)",
#endif
    .format = AUDIO_FORMAT_AAC,
    .init   = aac_init,
    .reset  = aac_reset,
    .decode = aac_decode,
    .deinit = aac_deinit,
    .state  = NULL,
};

audio_decoder_t *aac_decoder_get(void)
{
    return &s_aac_decoder;
}
