#include "mp3_player.h"
#include "config.h"
#include "audio_pipe.h"
#include "input/volume_control.h"

#include <string.h>

#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "minimp3.h"

static const char *TAG = "mp3_player";

// How many encoded bytes we keep staged in front of the decoder. Needs to
// comfortably fit more than one MP3 frame (max ~1441 bytes at 320kbps/32kHz)
// so mp3dec_decode_frame() always has enough to work with.
#define DECODE_STAGING_BYTES 4096
#define DEFAULT_SAMPLE_RATE  44100

static i2s_chan_handle_t s_tx_handle;
static int s_current_rate = 0;

static const size_t NOISE_FADE_START_BYTES = 16384;  // 16 kB, starts fading in ~1.3s from empty
static const size_t NOISE_FADE_END_BYTES   = 0;      // full noise at empty
static const float  NOISE_MAX_AMPLITUDE    = 1.0f;  // -14 dBFS at peak
static const float  NOISE_FADE_ALPHA       = 0.0008f;// ~250 ms glide at 48 kHz

// PRNG + LPF + gain smoother state.
static uint32_t s_rng_state = 0xACE1u;
static int32_t  s_noise_lp = 0;
static float    s_noise_gain_smoothed = 0.0f;

// A single MP3 frame's worth of interleaved stereo samples, reused whenever
// the decoder is starved and we need to feed I2S a synthetic block instead.
#define NOISE_FALLBACK_SAMPLES (1152 * 2)
static int16_t s_noise_block[NOISE_FALLBACK_SAMPLES];

static inline int16_t noise_sample_s16(void)
{
    uint32_t x = s_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng_state = x;
    return (int16_t)(x & 0xFFFF);
}

static inline int16_t noise_colored_s16(void)
{
    int32_t white = (int32_t)noise_sample_s16();
    // Warm hiss instead of harsh white — try shifting by 2 for brighter or
    // 5 for darker/browner if this default isn't to your taste.
    s_noise_lp += (white - s_noise_lp) >> 3;
    return (int16_t)s_noise_lp;
}

static float compute_target_gain(size_t fill_bytes)
{
    if (fill_bytes >= NOISE_FADE_START_BYTES) return 0.0f;
    if (fill_bytes <= NOISE_FADE_END_BYTES)   return 1.0f;
    return 1.0f - (float)(fill_bytes - NOISE_FADE_END_BYTES) /
                  (float)(NOISE_FADE_START_BYTES - NOISE_FADE_END_BYTES);
}

// Mix noise into pcm[] in place, then write the result to I2S
static void mix_and_write(int16_t *pcm, size_t num_samples)
{
    size_t fill_bytes = audio_pipe_get_fill_bytes();
    float target = compute_target_gain(fill_bytes);
    float vol = volume_control_get_gain();

    for (size_t i = 0; i < num_samples; i++) {
        s_noise_gain_smoothed += NOISE_FADE_ALPHA * (target - s_noise_gain_smoothed);
        float g = s_noise_gain_smoothed;

        float signal = (float)pcm[i];
        float noise  = (float)noise_colored_s16() * NOISE_MAX_AMPLITUDE;

        // Crossfade signal with noise, so that when signal is small (i.e. quiet),
        // noise is applied as if it was radio static. Then apply the volume level.
        float mixed = (signal * (1.0f - g) + noise * g) * vol;

        int32_t out_i = (int32_t)mixed;
        if (out_i >  32767) out_i =  32767;
        if (out_i < -32768) out_i = -32768;
        pcm[i] = (int16_t)out_i;
    }

    size_t written = 0;
    esp_err_t err = i2s_channel_write(s_tx_handle, pcm,
                                      num_samples * sizeof(int16_t),
                                      &written, portMAX_DELAY);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2s_channel_write: %s", esp_err_to_name(err));
    }
}

static esp_err_t i2s_setup(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.auto_clear_after_cb = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx_handle, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(DEFAULT_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCLK_GPIO,
            .ws = I2S_WS_GPIO,
            .dout = I2S_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx_handle));
    s_current_rate = DEFAULT_SAMPLE_RATE;

    return ESP_OK;
}

static void reconfigure_rate_if_needed(int hz)
{
    if (hz <= 0 || hz == s_current_rate) {
        return;
    }
    ESP_LOGI(TAG, "stream sample rate changed: %d -> %d Hz", s_current_rate, hz);
    atomic_fetch_add(&g_audio_pipe_rate_changes, 1);
    ESP_ERROR_CHECK(i2s_channel_disable(s_tx_handle));
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(hz);
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(s_tx_handle, &clk_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx_handle));
    s_current_rate = hz;
}

static void decode_task(void *pvParameters)
{
    (void)pvParameters;

    static mp3dec_t mp3d;
    mp3dec_init(&mp3d);

    static uint8_t stage[DECODE_STAGING_BYTES];
    size_t stage_len = 0;

    // MINIMP3_MAX_SAMPLES_PER_FRAME is (1152*2), i.e. already sized for a
    // full stereo frame's worth of interleaved int16 samples.
    static int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    static int16_t pcm_stereo[MINIMP3_MAX_SAMPLES_PER_FRAME];

    while (1) {
        if (stage_len < sizeof(stage)) {
            size_t n = audio_pipe_read(stage + stage_len, sizeof(stage) - stage_len, pdMS_TO_TICKS(10));
            if (n == 0) {
                atomic_fetch_add(&g_audio_pipe_read_underruns, 1);
            }
            stage_len += n;
        }
        if (stage_len == 0) {
            // Add in white noise if no data is buffered in this interval.
            memset(s_noise_block, 0, sizeof(s_noise_block));
            mix_and_write(s_noise_block, NOISE_FALLBACK_SAMPLES);
            continue;
        }

        mp3dec_frame_info_t info;
        int samples = mp3dec_decode_frame(&mp3d, stage, (int)stage_len, pcm, &info);

        if (info.frame_bytes > 0) {
            memmove(stage, stage + info.frame_bytes, stage_len - info.frame_bytes);
            stage_len -= info.frame_bytes;
        } else if (stage_len == sizeof(stage)) {
            // Buffer's full and minimp3 still couldn't find a frame boundary
            // in it -- this is corrupt/non-MP3 data. Drop half and resync
            // instead of spinning forever on the same bytes.
            size_t drop = stage_len / 2;
            memmove(stage, stage + drop, stage_len - drop);
            stage_len -= drop;
            atomic_fetch_add(&g_audio_pipe_resyncs, 1);
            ESP_LOGW(TAG, "no frame sync found, dropped %u bytes", (unsigned)drop);
        }

        if (samples <= 0) {
            continue; // ID3/garbage skipped, or not enough data buffered yet
        }

        reconfigure_rate_if_needed(info.hz);

        const int16_t *out = pcm;
        if (info.channels == 1) {
            for (int i = 0; i < samples; i++) {
                pcm_stereo[2 * i] = pcm[i];
                pcm_stereo[2 * i + 1] = pcm[i];
            }
            out = pcm_stereo;
        }

        mix_and_write((int16_t *)out, (size_t)samples * 2);
    }
}

esp_err_t mp3_player_init(void)
{
    ESP_ERROR_CHECK(i2s_setup());

    BaseType_t ok = xTaskCreatePinnedToCore(decode_task, "mp3_decode", 24576, NULL, 6, NULL, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create decode task");
        return ESP_FAIL;
    }
    return ESP_OK;
}
