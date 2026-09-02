#include "config.h"
#include "network/wifi_connect.h"
#include "audio/audio_pipe.h"
#include "audio/mp3_player.h"
#include "audio/http_stream.h"
#include "input/volume_control.h"
#include "display/display_ui.h"

#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

// Logs ring buffer fill level and per-second rates of each failure mode
// (write drops, read underruns, frame resyncs, sample rate changes) so a
// glitch can be correlated against what the pipeline was doing at the time.
// Low priority, unpinned -- this is not timing-critical.
static void monitor_task(void *pvParameters)
{
    (void)pvParameters;
    uint32_t last_drops = 0, last_underruns = 0, last_resyncs = 0, last_rate_changes = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        size_t fill = audio_pipe_get_fill_bytes();
        size_t cap = audio_pipe_get_capacity_bytes();
        uint32_t drops = atomic_load(&g_audio_pipe_write_drops);
        uint32_t underruns = atomic_load(&g_audio_pipe_read_underruns);
        uint32_t resyncs = atomic_load(&g_audio_pipe_resyncs);
        uint32_t rate_changes = atomic_load(&g_audio_pipe_rate_changes);

        ESP_LOGI(TAG, "ringbuf %u%% (%u/%u B) | drops +%lu | underruns +%lu | resyncs +%lu | rate changes +%lu",
                 cap ? (unsigned)(100 * fill / cap) : 0, (unsigned)fill, (unsigned)cap,
                 (unsigned long)(drops - last_drops),
                 (unsigned long)(underruns - last_underruns),
                 (unsigned long)(resyncs - last_resyncs),
                 (unsigned long)(rate_changes - last_rate_changes));

        last_drops = drops;
        last_underruns = underruns;
        last_resyncs = resyncs;
        last_rate_changes = rate_changes;

        static size_t s_monitor_min_watermark = SIZE_MAX;
        size_t watermark = uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t);
        if (watermark < s_monitor_min_watermark) {
            s_monitor_min_watermark = watermark;
            ESP_LOGI(TAG, "Monitor task new low stack watermark: %u bytes", (unsigned)watermark);
        }
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(wifi_connect_start());
    ESP_LOGI(TAG, "Wifi up");

    ESP_ERROR_CHECK(audio_pipe_init(AUDIO_RINGBUF_BYTES));
    ESP_ERROR_CHECK(mp3_player_init());
    ESP_ERROR_CHECK(volume_control_init());

    ESP_ERROR_CHECK(display_ui_init());
    // Placeholder content so the display can be verified on its own before
    // real track/station metadata is wired up (that needs either ICY
    // metadata parsing or the station API work, both separate follow-ups).
    display_ui_set_station("ITALIAN DANCE NETWORK", "MILAN", "ITALY", "ITALIAN");
    display_ui_set_track("UNKNOWN TITLE", "UNKNOWN ARTIST");

    // Fetch task on core 0 (alongside Wi-Fi/lwIP), decode+I2S task on core 1
    // (set in mp3_player.c) so network jitter doesn't compete with audio
    // timing on the same core.
    xTaskCreatePinnedToCore(http_stream_task, "http_stream", 8192, NULL, 5, NULL, 0);
    xTaskCreate(monitor_task, "monitor", 4096, NULL, 1, NULL);

    ESP_LOGI(TAG, "Streaming %s", STREAM_URL);
}
