#include "audio_player.h"
#include "audio_pipe.h"
#include "audio_output.h"
#include "mp3_decoder.h"
#include "aac_decoder.h"

#include <string.h>
#include <stdatomic.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "audio_player";

// How many encoded bytes we keep staged in front of the decoder. Needs to
// comfortably fit more than one frame from the largest codec we support.
// MP3 tops out around 1441 B (320 kbps @ 32 kHz), but AAC's ADTS
// frame_length is a 13-bit field so the spec allows up to 8191 B per frame.
// 12 KiB leaves ~4 KiB of headroom above that so we can always keep at
// least one full worst-case frame plus room to keep appending.
#define DECODE_STAGING_BYTES 12288

// PCM scratch: sized for the LARGEST possible frame we might get from any
// decoder. MP3 tops out at 1152 samples/ch (2304 stereo int16), but HE-AAC
// with SBR runs at nChans * AAC_MAX_NSAMPS * 2 = 2 * 1024 * 2 = 4096 stereo
// int16, so Helix will happily write past a smaller buffer.
#define PCM_SCRATCH_SAMPLES (2 * 1024 * 2)

typedef enum {
    ROUTE_STATE_WAITING,     // no decoder selected yet -- sniff incoming bytes to pick one
    ROUTE_STATE_DECODING,    // a decoder is active; keep feeding it
} route_state_t;

// New-stream hand-off from http_stream to the decode task. The task checks
// s_stream_generation each iteration; when it moves, it drops its staged
// bytes and re-arms the router with s_pending_format.
static atomic_int      s_stream_generation = 0;
static atomic_int      s_pending_format    = AUDIO_FORMAT_UNKNOWN;

void audio_player_new_stream(audio_format_t fmt)
{
    atomic_store(&s_pending_format, (int)fmt);
    atomic_fetch_add(&s_stream_generation, 1);
}

// Pick a decoder for a format, or NULL if we don't have one.
static audio_decoder_t *decoder_for(audio_format_t fmt)
{
    switch (fmt) {
        case AUDIO_FORMAT_MP3: return mp3_decoder_get();
        case AUDIO_FORMAT_AAC: return aac_decoder_get();
        default:               return NULL;
    }
}

static void decode_task(void *pvParameters)
{
    (void)pvParameters;

    // Initialize every decoder once up front rather than at first-use, so
    // OOM (Helix's allocation for AAC state, etc.) shows up at boot instead
    // of the first time an AAC station is picked hours later.
    ESP_ERROR_CHECK(mp3_decoder_get()->init(mp3_decoder_get()));
    ESP_ERROR_CHECK(aac_decoder_get()->init(aac_decoder_get()));

    static uint8_t stage[DECODE_STAGING_BYTES];
    size_t stage_len = 0;

    static int16_t pcm[PCM_SCRATCH_SAMPLES];

    route_state_t   route_state = ROUTE_STATE_WAITING;
    audio_decoder_t *active     = NULL;
    int             last_generation = atomic_load(&s_stream_generation);

    while (1) {
        // Pick up any new-stream hand-off from http_stream. We drop the
        // stage so leftover bytes from the previous codec don't get fed
        // into the newly-selected decoder.
        int gen = atomic_load(&s_stream_generation);
        if (gen != last_generation) {
            last_generation = gen;
            stage_len = 0;
            audio_format_t hint = (audio_format_t)atomic_load(&s_pending_format);
            if (hint != AUDIO_FORMAT_UNKNOWN) {
                active = decoder_for(hint);
                if (active != NULL) {
                    active->reset(active);
                    ESP_LOGI(TAG, "new stream: format=%s -> %s",
                             audio_format_to_string(hint), active->name);
                    route_state = ROUTE_STATE_DECODING;
                } else {
                    ESP_LOGW(TAG, "new stream: format=%s has no decoder, will sniff",
                             audio_format_to_string(hint));
                    route_state = ROUTE_STATE_WAITING;
                }
            } else {
                ESP_LOGI(TAG, "new stream: format=unknown, will sniff bytes");
                active = NULL;
                route_state = ROUTE_STATE_WAITING;
            }
        }

        if (stage_len < sizeof(stage)) {
            size_t n = audio_pipe_read(stage + stage_len,
                                       sizeof(stage) - stage_len,
                                       pdMS_TO_TICKS(10));
            if (n == 0) {
                atomic_fetch_add(&g_audio_pipe_read_underruns, 1);
            }
            stage_len += n;
        }
        if (stage_len == 0) {
            audio_output_write_starvation_noise();
            continue;
        }

        // If we still don't know what format we're looking at, sniff the
        // stage buffer. Keep pulling more bytes until sniffing succeeds or
        // the buffer's full (at which point we fall back to MP3 -- the
        // overwhelmingly-common case -- rather than getting stuck).
        if (route_state == ROUTE_STATE_WAITING) {
            audio_format_t sniffed = audio_format_sniff(stage, stage_len);
            if (sniffed == AUDIO_FORMAT_UNKNOWN) {
                if (stage_len < sizeof(stage)) {
                    continue; // fill more, try again next iteration
                }
                ESP_LOGW(TAG, "sniff failed after %u bytes, defaulting to MP3",
                         (unsigned)stage_len);
                sniffed = AUDIO_FORMAT_MP3;
            }
            active = decoder_for(sniffed);
            if (active == NULL) {
                ESP_LOGW(TAG, "sniffed format %s has no decoder, dropping stage",
                         audio_format_to_string(sniffed));
                stage_len = 0;
                continue;
            }
            active->reset(active);
            ESP_LOGI(TAG, "sniffed %s -> %s",
                     audio_format_to_string(sniffed), active->name);
            route_state = ROUTE_STATE_DECODING;
        }

        size_t consumed = 0;
        audio_frame_info_t info = {0};
        audio_decode_status_t status = active->decode(active,
                                                       stage, stage_len,
                                                       &consumed,
                                                       pcm, PCM_SCRATCH_SAMPLES,
                                                       &info);

        if (consumed > 0) {
            if (consumed > stage_len) consumed = stage_len; // paranoia
            memmove(stage, stage + consumed, stage_len - consumed);
            stage_len -= consumed;
        }

        switch (status) {
        case AUDIO_DECODE_OK:
            audio_output_write(pcm,
                               info.samples_per_channel * (size_t)info.channels,
                               info.sample_rate_hz);
            break;

        case AUDIO_DECODE_SKIP:
            // consumed >0 already handled above; nothing else to do.
            break;

        case AUDIO_DECODE_NEED_MORE:
            // If the stage is full and the decoder still says "not enough",
            // it isn't a truncated-frame issue -- the bytes are garbage.
            // Drop half and resync at the next frame boundary the decoder
            // finds. Same recovery the old mp3_player.c did.
            if (stage_len == sizeof(stage)) {
                size_t drop = stage_len / 2;
                memmove(stage, stage + drop, stage_len - drop);
                stage_len -= drop;
                atomic_fetch_add(&g_audio_pipe_resyncs, 1);
                ESP_LOGW(TAG, "%s: no frame sync in %u B, dropped %u",
                         active->name, (unsigned)sizeof(stage), (unsigned)drop);
            }
            break;

        case AUDIO_DECODE_ERROR:
            // Decoder rejected the bytes at the head of the stage. If it
            // consumed some, we've already moved past them; otherwise drop
            // one byte and let it try to resync on the next iteration.
            if (consumed == 0 && stage_len > 0) {
                memmove(stage, stage + 1, stage_len - 1);
                stage_len -= 1;
            }
            atomic_fetch_add(&g_audio_pipe_resyncs, 1);
            break;
        }
    }
}

esp_err_t audio_player_init(void)
{
    ESP_ERROR_CHECK(audio_output_init());

    BaseType_t ok = xTaskCreatePinnedToCore(decode_task, "audio_decode",
                                            24576, NULL, 6, NULL, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create decode task");
        return ESP_FAIL;
    }
    return ESP_OK;
}
