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

static esp_err_t i2s_setup(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
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
            size_t n = audio_pipe_read(stage + stage_len, sizeof(stage) - stage_len, pdMS_TO_TICKS(200));
            if (n == 0) {
                atomic_fetch_add(&g_audio_pipe_read_underruns, 1);
            }
            stage_len += n;
        }
        if (stage_len == 0) {
            continue; // nothing buffered yet
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

        // Apply the potentiometer's gain in place. Skip the multiply
        // entirely at full volume (the common case if the knob is maxed)
        // to avoid burning cycles on every single sample for nothing.
        float gain = volume_control_get_gain();
        if (gain < 0.999f) {
            int16_t *mutable_out = (int16_t *)out;
            size_t total_samples = (size_t)samples * 2; // stereo out
            for (size_t i = 0; i < total_samples; i++) {
                mutable_out[i] = (int16_t)((float)mutable_out[i] * gain);
            }
        }

        size_t bytes_to_write = (size_t)samples * 2 /* channels out */ * sizeof(int16_t);
        size_t bytes_written = 0;
        // Blocking write: this is what paces the whole pipeline to real time
        // -- the decode loop only pulls new bytes as fast as I2S drains them.
        esp_err_t err = i2s_channel_write(s_tx_handle, out, bytes_to_write, &bytes_written, portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "i2s_channel_write: %s", esp_err_to_name(err));
        }
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
