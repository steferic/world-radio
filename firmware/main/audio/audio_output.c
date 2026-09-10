#include "audio_output.h"
#include "audio_pipe.h"
#include "config.h"
#include "input/volume_control.h"

#include <string.h>

#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "audio_output";

#define DEFAULT_SAMPLE_RATE 44100

static i2s_chan_handle_t s_tx_handle;
static int               s_current_rate = 0;

// Noise-fade curve: when the ring buffer has more than NOISE_FADE_START_BYTES
// of audio staged, the noise is silent; as it drains toward zero, the noise
// gain ramps linearly up to full. Tuned so short (<1 s) hiccups only nudge
// noise in a bit rather than dropping the listener straight to full static.
static const size_t NOISE_FADE_START_BYTES = 16384;
static const size_t NOISE_FADE_END_BYTES   = 0;
static const float  NOISE_MAX_AMPLITUDE    = 1.0f;
static const float  NOISE_FADE_ALPHA       = 0.0008f;

static uint32_t s_rng_state = 0xACE1u;
static int32_t  s_noise_lp  = 0;
static float    s_noise_gain_smoothed = 0.0f;

// One MP3 frame's worth of interleaved stereo silence; the starvation writer
// mixes noise into this in place. AAC frames are smaller (1024 vs 1152
// samples/ch) so the same buffer covers both codecs.
#define STARVATION_BLOCK_SAMPLES (1152 * 2)
static int16_t s_starvation_block[STARVATION_BLOCK_SAMPLES];

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
    // Warm hiss instead of harsh white -- try shifting by 2 for brighter or
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

        // Crossfade signal with noise so a quiet decoder buffer sounds like
        // radio static, then apply the volume level.
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

esp_err_t audio_output_init(void)
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

void audio_output_write(int16_t *pcm, size_t num_samples, int sample_rate_hz)
{
    reconfigure_rate_if_needed(sample_rate_hz);
    mix_and_write(pcm, num_samples);
}

void audio_output_write_starvation_noise(void)
{
    memset(s_starvation_block, 0, sizeof(s_starvation_block));
    mix_and_write(s_starvation_block, STARVATION_BLOCK_SAMPLES);
}
