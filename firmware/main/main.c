#include "config.h"
#include "network/wifi_connect.h"
#include "audio/audio_pipe.h"
#include "audio/mp3_player.h"
#include "audio/http_stream.h"
#include "input/volume_control.h"
#include "input/pushbutton.h"
#include "input/rotary_encoder.h"
#include "input/rotary_button.h"

#include "display/ui_task.h"
#include "display/screens/boot_screen.h"
#include "display/screens/now_playing_screen.h"

#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "mbedtls/debug.h"

#include "network/mem_probe.h"

static const char *TAG = "main";

// Logs ring buffer fill level and per-second rates of each failure mode
// (write drops, read underruns, frame resyncs, sample rate changes) so a
// glitch can be correlated against what the pipeline was doing at the time.
// Low priority, unpinned -- this is not timing-critical.
static void monitor_task(void *pvParameters)
{
    (void)pvParameters;
    uint32_t last_drops = 0, last_underruns = 0, last_resyncs = 0, last_rate_changes = 0;
    uint32_t last_bytes = atomic_load(&g_http_bytes_read_total);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        size_t fill = audio_pipe_get_fill_bytes();
        size_t cap = audio_pipe_get_capacity_bytes();
        uint32_t drops = atomic_load(&g_audio_pipe_write_drops);
        uint32_t underruns = atomic_load(&g_audio_pipe_read_underruns);
        uint32_t resyncs = atomic_load(&g_audio_pipe_resyncs);
        uint32_t rate_changes = atomic_load(&g_audio_pipe_rate_changes);
        uint32_t bytes = atomic_load(&g_http_bytes_read_total);
        uint32_t bytes_per_sec = bytes - last_bytes;

        ESP_LOGI(TAG,
                 "ringbuf %u%% (%u/%u B) | net %u B/s | drops +%lu | underruns +%lu | resyncs +%lu | rate changes +%lu",
                 cap ? (unsigned)(100 * fill / cap) : 0, (unsigned)fill, (unsigned)cap,
                 (unsigned)bytes_per_sec,
                 (unsigned long)(drops - last_drops),
                 (unsigned long)(underruns - last_underruns),
                 (unsigned long)(resyncs - last_resyncs),
                 (unsigned long)(rate_changes - last_rate_changes));

        last_drops = drops;
        last_underruns = underruns;
        last_resyncs = resyncs;
        last_rate_changes = rate_changes;
        last_bytes = bytes;

        static size_t s_monitor_min_watermark = SIZE_MAX;
        size_t watermark = uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t);
        if (watermark < s_monitor_min_watermark) {
            s_monitor_min_watermark = watermark;
            ESP_LOGI(TAG, "Monitor task new low stack watermark: %u bytes", (unsigned)watermark);
        }
    }
}

// Consumes pushbutton_event_t items produced by the standalone shuffle
// pushbutton's ISR and asks the streaming task to reshuffle. Runs at low
// priority -- the ISR already captured the press; getting from "queued"
// to "reshuffled" a few ms later is invisible to a user. Distinct from
// the rotary encoder's built-in switch, which drives the UI menu system
// via input/rotary_button.c.
static void shuffle_task(void *pvParameters)
{
    QueueHandle_t q = (QueueHandle_t)pvParameters;
    pushbutton_event_t ev;
    while (1) {
        if (xQueueReceive(q, &ev, portMAX_DELAY) == pdTRUE) {
            uint32_t accepted = atomic_load(&g_pushbutton_accepted_presses);
            uint32_t raw = atomic_load(&g_pushbutton_raw_edges);
            ESP_LOGI(TAG, "shuffle consumer: press #%lu drained (raw edges seen: %lu) -> requesting new random station",
                     (unsigned long)accepted, (unsigned long)raw);
            http_stream_shuffle();
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

    // Route mbedtls's internal debug messages through esp_log. Only linkable
    // when CONFIG_MBEDTLS_DEBUG is on (that's what compiles MBEDTLS_DEBUG_C
    // into the library); with it off, this function is undefined at link
    // time. Level 4 = verbose (handshakes, records, alerts).
#if CONFIG_MBEDTLS_DEBUG
    mbedtls_debug_set_threshold(4);
#endif

    // Bring the UI up FIRST so the user sees "Connecting to Wi-Fi..."
    // while wifi_connect_start's blocking wait runs below. The UI task
    // is on its own FreeRTOS task, so app_main blocking here doesn't
    // freeze the spinner.
    ESP_ERROR_CHECK(ui_task_start(&boot_screen));

    // Rotary encoder + its built-in switch feed the UI event queue.
    // The standalone shuffle pushbutton is a *separate* physical button
    // wired to PUSHBUTTON_GPIO and is initialized further down against
    // its own queue -- it does not participate in the UI system.
    ESP_ERROR_CHECK(rotary_encoder_init());
    ESP_ERROR_CHECK(rotary_button_init());

    ESP_ERROR_CHECK(wifi_connect_start());
    ESP_LOGI(TAG, "Wifi up");

    ESP_ERROR_CHECK(audio_pipe_init(AUDIO_RINGBUF_BYTES));
    ESP_ERROR_CHECK(mp3_player_init());
    ESP_ERROR_CHECK(volume_control_init());

    // Seed the now-playing screen's state so the swap from boot lands on
    // something readable, then swap. http_stream_task will overwrite the
    // station block as soon as it picks the first random station (~1-2 s
    // later); this placeholder just fills the LCD in the interim.
    now_playing_set_station("CONNECTING", "", "", "");
    now_playing_set_track("UNKNOWN TITLE", "UNKNOWN ARTIST");
    ui_screen_replace(&now_playing_screen);

    // Shuffle-button queue and consumer task. Independent of the UI queue.
    // Length 4 so a rapid burst of clicks doesn't get dropped by the ISR,
    // but not so long that a queue backlog defers a shuffle by seconds.
    static StaticQueue_t s_pb_queue_storage;
    static uint8_t s_pb_queue_buf[4 * sizeof(pushbutton_event_t)];
    QueueHandle_t pb_queue = xQueueCreateStatic(4, sizeof(pushbutton_event_t),
                                                s_pb_queue_buf, &s_pb_queue_storage);
    configASSERT(pb_queue != NULL);
    ESP_ERROR_CHECK(pushbutton_init(pb_queue));
    // 4 KiB stack, not 2 KiB: each drained press calls ESP_LOGI which uses
    // vsnprintf (~1 KiB of transient stack). Under a burst of queued events
    // the peak crosses a 2 KiB canary and FreeRTOS panics. Steady-state
    // usage is tiny; the headroom is just for the logging.
    xTaskCreate(shuffle_task, "shuffle", 4096, pb_queue, 2, NULL);

    // Fetch task on core 0 (alongside Wi-Fi/lwIP), decode+I2S task on
    // core 1 (set in mp3_player.c) so network jitter doesn't compete
    // with audio timing on the same core. Stack bumped to 12 KiB because
    // station_api's HTTPS GET + cJSON parse push http_stream_task's peak
    // usage above what the old 8 KiB left room for.
    xTaskCreatePinnedToCore(http_stream_task, "http_stream", 12288, NULL, 5, NULL, 0);
    xTaskCreate(monitor_task, "monitor", 4096, NULL, 1, NULL);

    ESP_LOGI(TAG, "Streaming random stations from %s", STATION_API_RANDOM_URL);
}
