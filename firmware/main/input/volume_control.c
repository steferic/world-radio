#include "volume_control.h"
#include "config.h"

#include <stdatomic.h>

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "volume_control";

static adc_oneshot_unit_handle_t s_adc_handle;
static adc_channel_t s_adc_channel;
static _Atomic float s_gain = 1.0f; // full volume until the first real sample lands

static void volume_task(void *pvParameters)
{
    (void)pvParameters;

    // Seed the filter at full-scale so we don't briefly play at zero volume
    // while waiting for the smoothing filter to catch up to the first read.
    float smoothed = 4095.0f;

    while (1) {
        int raw = 0;
        esp_err_t err = adc_oneshot_read(s_adc_handle, s_adc_channel, &raw);
        if (err == ESP_OK) {
            smoothed += VOLUME_SMOOTHING_ALPHA * ((float)raw - smoothed);

            float normalized = smoothed / 4095.0f; // 12-bit ADC range
            if (normalized < 0.0f) normalized = 0.0f;
            if (normalized > 1.0f) normalized = 1.0f;

#if VOLUME_POT_INVERT
            normalized = 1.0f - normalized;
#endif

            // Square-law taper: matches perceived loudness better than a
            // straight linear multiply, so the knob doesn't feel like all
            // the useful range is crammed into the first 10% of rotation.
            float gain = normalized * normalized;
            atomic_store_explicit(&s_gain, gain, memory_order_relaxed);
        } else {
            ESP_LOGW(TAG, "adc_oneshot_read failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(VOLUME_POLL_INTERVAL_MS));
    }
}

esp_err_t volume_control_init(void)
{
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t err = adc_oneshot_new_unit(&init_cfg, &s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(err));
        return err;
    }

    // Resolve the GPIO to its ADC unit/channel rather than hardcoding the
    // channel number, so this stays correct if VOLUME_POT_GPIO ever changes.
    // This also doubles as validation: it fails loudly if someone points
    // VOLUME_POT_GPIO at a non-ADC1 pin instead of silently reading garbage.
    adc_unit_t resolved_unit;
    err = adc_oneshot_io_to_channel(VOLUME_POT_GPIO, &resolved_unit, &s_adc_channel);
    if (err != ESP_OK || resolved_unit != ADC_UNIT_1) {
        ESP_LOGE(TAG, "GPIO%d is not a valid ADC1 pin", VOLUME_POT_GPIO);
        return (err != ESP_OK) ? err : ESP_ERR_INVALID_ARG;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12, // full 0-3.3V input range
    };
    err = adc_oneshot_config_channel(s_adc_handle, s_adc_channel, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    BaseType_t ok = xTaskCreate(volume_task, "volume_ctrl", 3072, NULL, 2, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create volume_ctrl task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "volume potentiometer on GPIO%d ready", VOLUME_POT_GPIO);
    return ESP_OK;
}

float volume_control_get_gain(void)
{
    return atomic_load_explicit(&s_gain, memory_order_relaxed);
}
