#include "http_stream.h"
#include "config.h"
#include "audio_pipe.h"

#include <stdlib.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "http_stream";

atomic_uint_fast32_t g_http_bytes_read_total = 0;

#define READ_CHUNK_SIZE 4096
#define BACKOFF_MIN_MS  1000
#define BACKOFF_MAX_MS  10000

// Runs one connect-read-until-it-ends cycle. Returns true if the stream
// ended "cleanly" (server closed after sending everything it had), false if
// it looks like a drop that's worth backing off before retrying.
static bool stream_once(void)
{
    esp_http_client_config_t config = {
        .url = STREAM_URL,
        .timeout_ms = 10000,
        .buffer_size = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach, // only used if URL is https
        .disable_auto_redirect = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return false;
    }

    esp_http_client_set_header(client, "User-Agent", "esp32-world-radio/1.0");
    // Deliberately not sending "Icy-MetaData: 1" -- see the note in config.h.
    // If your server injects ICY metadata anyway, that will corrupt the MP3
    // stream and this player will need a metadata-stripping stage.

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int64_t content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "connected, status=%d content_length=%lld", status, (long long)content_length);

    if (status != 200) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    uint8_t *buf = malloc(READ_CHUNK_SIZE);
    if (buf == NULL) {
        ESP_LOGE(TAG, "OOM allocating read buffer");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    bool clean_end = true;
    while (1) {
        int n = esp_http_client_read(client, (char *)buf, READ_CHUNK_SIZE);
        if (n < 0) {
            ESP_LOGW(TAG, "read error");
            clean_end = false;
            break;
        }
        if (n == 0) {
            if (esp_http_client_is_complete_data_received(client)) {
                ESP_LOGI(TAG, "stream ended cleanly");
            } else {
                ESP_LOGW(TAG, "read returned 0, connection likely dropped");
                clean_end = false;
            }
            break;
        }

        // Block (with a timeout) so a full ring buffer applies backpressure
        // instead of silently corrupting the stream. If the decode side has
        // truly stalled for 2s something else is wrong, so just drop this
        // chunk and keep the connection alive rather than wedging forever.
        if (!audio_pipe_write(buf, (size_t)n, pdMS_TO_TICKS(2000))) {
            atomic_fetch_add(&g_audio_pipe_write_drops, 1);
            ESP_LOGW(TAG, "ring buffer full, dropping %d bytes", n);
        }

        atomic_fetch_add(&g_http_bytes_read_total, (uint32_t)n);
    }

    free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return clean_end;
}

void http_stream_task(void *pvParameters)
{
    (void)pvParameters;
    uint32_t backoff_ms = BACKOFF_MIN_MS;

    while (1) {
        ESP_LOGI(TAG, "connecting to %s", STREAM_URL);
        bool ok = stream_once();

        if (ok) {
            backoff_ms = BACKOFF_MIN_MS;
        } else {
            ESP_LOGW(TAG, "reconnecting in %u ms", (unsigned)backoff_ms);
            vTaskDelay(pdMS_TO_TICKS(backoff_ms));
            backoff_ms *= 2;
            if (backoff_ms > BACKOFF_MAX_MS) {
                backoff_ms = BACKOFF_MAX_MS;
            }
        }
    }
}
