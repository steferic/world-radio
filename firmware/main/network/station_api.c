#include "station_api.h"
#include "config.h"

#include <string.h>
#include <stdlib.h>

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "station_api";

// Split the scratch buffer: the first half receives the raw response
// body, the second half holds the NUL-terminated copies of individual JSON
// string fields that station_info_t points to. Keeping them in separate
// halves means we can free the cJSON tree without invalidating the pointers
// we handed back to the caller.
static size_t body_capacity(size_t scratch_len) {
    return scratch_len / 2;
}

// Bump-allocate a NUL-terminated copy of `src` into the second half of
// scratch, returning a pointer to it (or an empty string if `src` is
// missing or there's no room left). Never returns NULL.
static char *dup_into(const char *src, char *scratch, size_t scratch_len, size_t *cursor) {
    static const char empty[] = "";
    size_t fields_offset = body_capacity(scratch_len);
    size_t fields_room = scratch_len - fields_offset;
    if (src == NULL) {
        return (char *)empty;
    }
    size_t n = strlen(src);
    if (*cursor + n + 1 > fields_room) {
        return (char *)empty;
    }
    char *dst = scratch + fields_offset + *cursor;
    memcpy(dst, src, n);
    dst[n] = '\0';
    *cursor += n + 1;
    return dst;
}

// GET /api/stations/random into `body`, returning the number of bytes
// read into it (or 0 on any error). Body is NUL-terminated on success.
static size_t fetch_random(char *body, size_t body_cap)
{
    esp_http_client_config_t config = {
        .url = STATION_API_RANDOM_URL,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "http_client_init failed");
        return 0;
    }
    esp_http_client_set_header(client, "User-Agent", "esp32-world-radio/1.0");
    esp_http_client_set_header(client, "Accept", "application/json");

    size_t out_len = 0;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "open failed: %s", esp_err_to_name(err));
        goto done;
    }
    int64_t content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGW(TAG, "random returned status %d (content_length=%lld)",
                 status, (long long)content_length);
        goto done;
    }

    // Reserve one byte for the NUL terminator. Drain the connection even if
    // the body is bigger than our buffer so the socket ends up in a clean
    // state, otherwise the next request can inherit stale bytes.
    while (out_len + 1 < body_cap) {
        int n = esp_http_client_read(client, body + out_len, (int)(body_cap - 1 - out_len));
        if (n <= 0) break;
        out_len += (size_t)n;
    }
    body[out_len] = '\0';

    if (out_len + 1 >= body_cap) {
        ESP_LOGW(TAG, "response body >= %u bytes, likely truncated", (unsigned)body_cap);
        out_len = 0;
    }

done:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return out_len;
}

esp_err_t station_api_get_random_mp3(station_info_t *info, char *scratch, size_t scratch_len) {
    if (info == NULL || scratch == NULL || scratch_len < STATION_API_SCRATCH_MIN_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(info, 0, sizeof(*info));

    size_t body_cap = body_capacity(scratch_len);

    for (int attempt = 1; attempt <= STATION_API_MAX_ATTEMPTS; attempt++) {
        size_t body_len = fetch_random(scratch, body_cap);
        if (body_len == 0) {
            // Network / HTTP failure
            return ESP_FAIL;
        }

        cJSON *root = cJSON_ParseWithLength(scratch, body_len);
        if (root == NULL) {
            ESP_LOGW(TAG, "attempt %d: JSON parse failed", attempt);
            continue;
        }

        const cJSON *format = cJSON_GetObjectItemCaseSensitive(root, "format");
        const cJSON *stream_url = cJSON_GetObjectItemCaseSensitive(root, "stream_url");

        bool is_mp3 = cJSON_IsString(format) && format->valuestring
                      && strcmp(format->valuestring, "mp3") == 0;
        bool has_url = cJSON_IsString(stream_url) && stream_url->valuestring
                       && stream_url->valuestring[0] != '\0';

        if (!is_mp3 || !has_url) {
            const char *got = cJSON_IsString(format) ? format->valuestring : "(missing)";
            ESP_LOGI(TAG, "attempt %d: skipping non-MP3 stream (format=%s)", attempt, got);
            cJSON_Delete(root);
            continue;
        }

        const cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
        const cJSON *region = cJSON_GetObjectItemCaseSensitive(root, "region");
        const cJSON *country = cJSON_GetObjectItemCaseSensitive(root, "country");
        const cJSON *genre = cJSON_GetObjectItemCaseSensitive(root, "genre");

        size_t cursor = 0;
        info->stream_url = dup_into(stream_url->valuestring, scratch, scratch_len, &cursor);
        info->name = dup_into(cJSON_IsString(name) ? name->valuestring : NULL,
                              scratch, scratch_len, &cursor);
        info->region = dup_into(cJSON_IsString(region) ? region->valuestring : NULL,
                                scratch, scratch_len, &cursor);
        info->country = dup_into(cJSON_IsString(country) ? country->valuestring : NULL,
                                 scratch, scratch_len, &cursor);
        info->genre = dup_into(cJSON_IsString(genre) ? genre->valuestring : NULL,
                               scratch, scratch_len, &cursor);

        // A stream_url longer than our field-half capacity is a failure.
        if (info->stream_url[0] == '\0') {
            ESP_LOGW(TAG, "attempt %d: stream_url too long for scratch buffer", attempt);
            cJSON_Delete(root);
            continue;
        }

        ESP_LOGI(TAG, "picked MP3 station \"%s\" (%s) after %d attempt(s)",
                 info->name, info->country, attempt);
        cJSON_Delete(root);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "gave up after %d attempts without an MP3 stream",
             STATION_API_MAX_ATTEMPTS);
    return ESP_FAIL;
}
