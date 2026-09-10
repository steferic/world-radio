#include "http_stream.h"
#include "config.h"
#include "audio_pipe.h"
#include "audio_player.h"
#include "audio_decoder.h"
#include "icy_demuxer.h"
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

// Captured by the HTTP event handler as response headers arrive, then read
// after esp_http_client_fetch_headers() returns. We do it this way rather
// than via esp_http_client_get_header() because some IDF versions don't
// expose non-standard headers (like icy-metaint) through that API even when
// they were on the wire. The event handler sees every header the parser
// sees, so this is bulletproof. Reset to 0 at the top of stream_once().
static size_t s_captured_metaint = 0;

// Log every response header so we can see exactly what the server sends.
// Also captures icy-metaint into s_captured_metaint for the demuxer to use.
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_HEADER) {
        // Print every header. This is a lot on the first connect, but it's
        // invaluable for debugging ICY / redirect / content-type issues and
        // the traffic drops to zero once the stream is running.
        ESP_LOGI(TAG, "hdr: %s: %s",
                 evt->header_key   ? evt->header_key   : "(null)",
                 evt->header_value ? evt->header_value : "(null)");

        if (evt->header_key && evt->header_value
            && strcasecmp(evt->header_key, "icy-metaint") == 0) {
            char *endp = NULL;
            unsigned long v = strtoul(evt->header_value, &endp, 10);
            // Same sanity bounds as before: reject 0 and anything absurdly
            // large that would let a corrupt header stall the demuxer.
            if (v > 0 && v <= (1UL << 20) && endp != evt->header_value) {
                s_captured_metaint = (size_t)v;
            } else {
                ESP_LOGW(TAG, "ignoring bogus icy-metaint '%s'", evt->header_value);
            }
        }
    }
    return ESP_OK;
}

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
        // Clear before opening the client so headers from a prior hop /
        // connection don't leak into this one's demuxer init.
        s_captured_metaint = 0;

        esp_http_client_config_t config = {
            .url = url_buf,
            .timeout_ms = 10000,
            .buffer_size = 2048,
            .crt_bundle_attach = esp_crt_bundle_attach, // only used if URL is https
            .event_handler = http_event_handler,        // logs headers + captures icy-metaint
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
        // Ask Shoutcast/Icecast servers to interleave "StreamTitle=..." blocks
        // into the byte stream. If the server responds with an icy-metaint
        // header we route reads through icy_demuxer to strip the metadata
        // before it reaches the audio pipe; if it doesn't, this header is a
        // no-op and playback is unchanged.
        esp_http_client_set_header(client, "Icy-MetaData", "1");

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

        // Detect ICY interleave. The value was captured by http_event_handler
        // as headers came off the wire (bypasses ESP-IDF's get_header, which
        // has been observed to hide non-standard response headers in some
        // versions). If nothing was captured, log so it's obvious in the
        // monitor -- silent passthrough was the bug that made "unknown title
        // / unknown artist" so hard to debug.
        // Static because the state buffer is ~4 KB and we don't want to spend
        // that on the http_stream task stack; there's only one connection in
        // flight at a time so a static shared instance is safe.
        static icy_demuxer_t demux;
        size_t metaint = s_captured_metaint;
        if (metaint == 0) {
            ESP_LOGW(TAG, "no valid icy-metaint response header -- "
                          "demuxer will run in passthrough (no ICY metadata will be parsed). "
                          "Check the 'hdr:' lines above to see what headers the server actually sent.");
        } else {
            ESP_LOGI(TAG, "icy-metaint=%u -- demuxer will emit metadata every %u audio bytes",
                     (unsigned)metaint, (unsigned)metaint);
        }
        icy_demuxer_init(&demux, metaint);

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

            // Route through the ICY demuxer: audio bytes are forwarded to
            // the ring buffer (with the same 2s backpressure semantics as
            // before), and any interleaved metadata blocks are parsed and
            // dispatched to now_playing. If metaint==0 this is a straight
            // passthrough.
            icy_demuxer_feed(&demux, buf, (size_t)n);

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
        audio_format_t fmt_hint = AUDIO_FORMAT_UNKNOWN;
#if STREAM_USE_API
        station_info_t station;
        esp_err_t api_err = station_api_get_random(&station, api_scratch, sizeof(api_scratch));
        if (api_err != ESP_OK) {
            ESP_LOGW(TAG, "no station from API, backing off %u ms", (unsigned)backoff_ms);
            vTaskDelay(pdMS_TO_TICKS(backoff_ms));
            backoff_ms *= 2;
            if (backoff_ms > BACKOFF_MAX_MS) backoff_ms = BACKOFF_MAX_MS;
            continue;
        }
        now_playing_set_station(station.name, station.region, station.country, station.genre);
        // Placeholder shown until the ICY demuxer dispatches a real
        // StreamTitle. A single "-" reads as intentional ("nothing to show
        // here") rather than as broken metadata ("UNKNOWN TITLE"), which
        // matters for talk/news streams that legitimately never populate
        // per-track info.
        now_playing_set_track("-", "-");
        url = station.stream_url;
        fmt_hint = audio_format_from_string(station.format);
#else
        // Pinned-URL mode: same URL every cycle, so a shuffle press just
        // reconnects to the pinned stream. Keeps the display update so a press
        // still visibly "does something". The ICY demuxer will overwrite the
        // "UNKNOWN" track fields with real StreamTitle metadata as soon as
        // the first metadata block arrives.
        (void)api_scratch;
        now_playing_set_station("SOMAFM GROOVE SALAD", "SAN FRANCISCO", "USA", "AMBIENT CHILL");
        // Placeholder shown until the ICY demuxer dispatches a real
        // StreamTitle. A single "-" reads as intentional ("nothing to show
        // here") rather than as broken metadata ("UNKNOWN TITLE"), which
        // matters for talk/news streams that legitimately never populate
        // per-track info.
        now_playing_set_track("-", "-");
        url = STREAM_URL;
        fmt_hint = AUDIO_FORMAT_MP3;
#endif

        // Drop any bytes still queued from the previous stream so we don't
        // play a fraction of a second of the old stream before the new one
        // kicks in, and hand the format hint to the decoder task so it can
        // swap decoders (or fall back to byte-sniffing on UNKNOWN).
        audio_pipe_reset();
        audio_player_new_stream(fmt_hint);

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
