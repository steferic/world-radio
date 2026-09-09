#include "http_stream.h"
#include "config.h"
#include "audio_pipe.h"
#include "network/station_api.h"
#include "display/screens/now_playing_screen.h"

#include <stdlib.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "http_stream";

atomic_uint_fast32_t g_http_bytes_read_total = 0;

// Set by http_stream_shuffle() and consumed at the top of each stream_once()
// iteration (also polled between reads) so a button press causes the current
// stream to tear down and a fresh random station to be picked. Cleared by
// the consumer once acknowledged.
static atomic_bool s_shuffle_pending = false;

#define READ_CHUNK_SIZE 4096
#define BACKOFF_MIN_MS  1000
#define BACKOFF_MAX_MS  10000

// A stream that hangs up in less than this window without ever delivering
// audio is treated as "unhealthy" -- we skip the backoff and immediately
// re-hit /random for a different station rather than wasting time retrying
// the same dead URL. Tune upward if legitimate slow servers get misclassified.
#define UNHEALTHY_WINDOW_MS 3000

// Max number of Location: redirects we'll chase before giving up. Real
// streaming redirects (playerservices.streamtheworld.com "livestream-redirect"
// endpoints, load balancers, etc.) are usually a single hop; three leaves
// headroom for chained CDNs without risking a redirect-loop lockup.
#define MAX_REDIRECT_HOPS 3

typedef enum {
    STREAM_ENDED_CLEAN, // server closed after sending everything
    STREAM_DROPPED,     // dropped mid-stream after real playback
    STREAM_UNHEALTHY,   // failed fast with no audio delivered, re-shuffle now
    STREAM_SHUFFLED,    // user pressed the button, re-shuffle now
} stream_result_t;

void http_stream_shuffle(void)
{
    atomic_store(&s_shuffle_pending, true);
}

// Opens `url`, follows up to MAX_REDIRECT_HOPS of 3xx redirects, and (on a
// 200) reads audio into the ring buffer until the stream ends, drops, or a
// shuffle is requested. See stream_result_t for the return semantics.
static stream_result_t stream_once(const char *url)
{
    // Own buffer for the current URL so we can rewrite it across redirect
    // hops without holding onto memory owned by the previous client handle
    // (esp_http_client_get_header returns a pointer into internal state that
    // dies at cleanup). 512 bytes is plenty for real-world stream URLs.
    char url_buf[512];
    strncpy(url_buf, url, sizeof(url_buf) - 1);
    url_buf[sizeof(url_buf) - 1] = '\0';

    for (int hop = 0; hop <= MAX_REDIRECT_HOPS; hop++) {
        esp_http_client_config_t config = {
            .url = url_buf,
            .timeout_ms = 10000,
            .buffer_size = 2048,
            .crt_bundle_attach = esp_crt_bundle_attach, // only used if URL is https
            // We follow redirects manually below rather than relying on
            // esp_http_client's built-in follower: the built-in path assumes
            // esp_http_client_perform(), but we use the open/fetch/read
            // pattern for streaming and it doesn't kick in there.
            .disable_auto_redirect = true,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL) {
            ESP_LOGE(TAG, "esp_http_client_init failed");
            return STREAM_UNHEALTHY;
        }

        // Older Shoutcast-family servers (e.g. Gloman) accept the HTTP
        // request with a non-browser UA and return 200 but then don't push
        // audio bytes -- observed against radiostreaming.ert.gr with our own
        // "esp32-world-radio/1.0" string. Pretend to be Firefox for the
        // audio fetch only; station_api.c keeps the honest UA for API calls.
        esp_http_client_set_header(client, "User-Agent",
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) Gecko/20100101 Firefox/128.0");
        // Deliberately not sending "Icy-MetaData: 1" -- see the note in
        // config.h. If your server injects ICY metadata anyway, that will
        // corrupt the MP3 stream and this player will need a metadata-
        // stripping stage.

        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "open failed: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            return STREAM_UNHEALTHY;
        }

        int64_t content_length = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "connected, status=%d content_length=%lld", status, (long long)content_length);

        // 3xx: follow the Location, up to MAX_REDIRECT_HOPS. We copy the
        // value out immediately because it lives in the client handle's
        // internal buffer and disappears at cleanup.
        if (status >= 300 && status < 400) {
            if (hop == MAX_REDIRECT_HOPS) {
                ESP_LOGW(TAG, "too many redirects (%d), giving up", hop);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return STREAM_UNHEALTHY;
            }
            char *loc = NULL;
            esp_http_client_get_header(client, "Location", &loc);
            if (loc == NULL || loc[0] == '\0') {
                ESP_LOGW(TAG, "%d without a Location header, giving up", status);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return STREAM_UNHEALTHY;
            }
            ESP_LOGI(TAG, "following redirect (%d) -> %s", status, loc);
            strncpy(url_buf, loc, sizeof(url_buf) - 1);
            url_buf[sizeof(url_buf) - 1] = '\0';
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            continue; // next hop
        }

        if (status != 200) {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return STREAM_UNHEALTHY;
        }

        uint8_t *buf = malloc(READ_CHUNK_SIZE);
        if (buf == NULL) {
            ESP_LOGE(TAG, "OOM allocating read buffer");
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return STREAM_UNHEALTHY;
        }

        int64_t open_us = esp_timer_get_time();
        size_t bytes_this_connection = 0;
        stream_result_t result = STREAM_ENDED_CLEAN;

        while (1) {
            // Check for shuffle before each read so a button press during
            // playback rips down the connection promptly. Read chunks tend
            // to return within tens of ms on a live stream, so worst-case
            // latency is one chunk (plus TLS shutdown) -- well under a second.
            if (atomic_load(&s_shuffle_pending)) {
                ESP_LOGI(TAG, "shuffle requested, dropping current stream");
                result = STREAM_SHUFFLED;
                break;
            }

            int n = esp_http_client_read(client, (char *)buf, READ_CHUNK_SIZE);
            if (n < 0) {
                // The mbedtls tag (when its debug is enabled) will have
                // already printed the actual TLS-layer cause on the lines
                // just above -- esp_err_to_name() doesn't know mbedtls
                // codes, so trying to name it from here just prints "ERROR".
                ESP_LOGW(TAG, "read error");
                result = STREAM_DROPPED;
                break;
            }
            if (n == 0) {
                if (esp_http_client_is_complete_data_received(client)) {
                    ESP_LOGI(TAG, "stream ended cleanly");
                    result = STREAM_ENDED_CLEAN;
                } else {
                    ESP_LOGW(TAG, "read returned 0, connection likely dropped");
                    result = STREAM_DROPPED;
                }
                break;
            }

            // Block (with a timeout) so a full ring buffer applies
            // backpressure instead of silently corrupting the stream. If
            // the decode side has truly stalled for 2s something else is
            // wrong, so just drop this chunk and keep the connection
            // alive rather than wedging forever.
            if (!audio_pipe_write(buf, (size_t)n, pdMS_TO_TICKS(2000))) {
                atomic_fetch_add(&g_audio_pipe_write_drops, 1);
                ESP_LOGW(TAG, "ring buffer full, dropping %d bytes", n);
            }

            atomic_fetch_add(&g_http_bytes_read_total, (uint32_t)n);
            bytes_this_connection += (size_t)n;
        }

        free(buf);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        // Reclassify: a drop that arrived within a few seconds without ever
        // producing audio is almost always an origin that's rejecting us
        // silently (peer-close after 200, geoblock, UA gate, etc.). Don't
        // waste a backoff sitting on it -- tell the caller to re-shuffle.
        if (result == STREAM_DROPPED) {
            int64_t elapsed_ms = (esp_timer_get_time() - open_us) / 1000;
            if (bytes_this_connection == 0 && elapsed_ms < UNHEALTHY_WINDOW_MS) {
                ESP_LOGW(TAG, "no audio in %lld ms, treating as unhealthy stream",
                         (long long)elapsed_ms);
                result = STREAM_UNHEALTHY;
            }
        }

        return result;
    }

    // Unreachable -- the redirect loop always returns from inside.
    return STREAM_UNHEALTHY;
}

void http_stream_task(void *pvParameters)
{
    (void)pvParameters;
    uint32_t backoff_ms = BACKOFF_MIN_MS;

    // One scratch buffer, reused for every /random request. Sized generously
    // over the ~1 KB responses the API returns so we don't have to worry
    // about truncation of long station names / genre tags. Lives in BSS
    // (via `static`), not on the task's stack, so a fragmented heap can't
    // silently downgrade us later.
    static char api_scratch[4096];

    while (1) {
        // A shuffle request that arrived during a backoff phase is satisfied
        // simply by the fact that we're about to (re)connect -- clear the
        // flag now so it doesn't cause the next stream to be immediately
        // torn down.
        atomic_store(&s_shuffle_pending, false);

        const char *url = NULL;
#if STREAM_USE_API
        station_info_t station;
        esp_err_t api_err = station_api_get_random_mp3(&station, api_scratch, sizeof(api_scratch));
        if (api_err != ESP_OK) {
            ESP_LOGW(TAG, "no station from API, backing off %u ms", (unsigned)backoff_ms);
            vTaskDelay(pdMS_TO_TICKS(backoff_ms));
            backoff_ms *= 2;
            if (backoff_ms > BACKOFF_MAX_MS) backoff_ms = BACKOFF_MAX_MS;
            continue;
        }
        now_playing_set_station(station.name, station.region, station.country, station.genre);
        now_playing_set_track("UNKNOWN TITLE", "UNKNOWN ARTIST");
        url = station.stream_url;
#else
        // TLS-bypass mode: same URL every cycle, so a shuffle press just
        // reconnects to the pinned Italian Dance Network stream. Keeps the
        // display update so a press still visibly "does something".
        (void)api_scratch;
        now_playing_set_station("ITALIAN DANCE NETWORK", "MILAN", "ITALY", "ITALIAN");
        now_playing_set_track("UNKNOWN TITLE", "UNKNOWN ARTIST");
        url = STREAM_URL;
#endif

        // Drop any bytes still queued from the previous stream so we don't
        // play a fraction of a second of the old stream before the new one
        // kicks in.
        audio_pipe_reset();

        ESP_LOGI(TAG, "connecting to %s", url);
        stream_result_t result = stream_once(url);

        switch (result) {
        case STREAM_SHUFFLED:
        case STREAM_UNHEALTHY:
            // Either the user asked, or the stream failed fast with no
            // audio. Either way, don't sit on a backoff -- re-hit /random
            // right now and take whatever the API hands us next.
            backoff_ms = BACKOFF_MIN_MS;
            break;

        case STREAM_ENDED_CLEAN:
            backoff_ms = BACKOFF_MIN_MS;
            break;

        case STREAM_DROPPED:
            // Stream had been producing audio and then dropped -- likely a
            // transient network hiccup on an otherwise-good origin. Back off
            // (with exponential growth) before reconnecting.
            ESP_LOGW(TAG, "reconnecting in %u ms", (unsigned)backoff_ms);
            vTaskDelay(pdMS_TO_TICKS(backoff_ms));
            backoff_ms *= 2;
            if (backoff_ms > BACKOFF_MAX_MS) {
                backoff_ms = BACKOFF_MAX_MS;
            }
            break;
        }
    }
}
