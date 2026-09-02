// HTTPS sanity check.
//
// Boots, connects to Wi-Fi, makes one HTTPS GET to a well-known endpoint, logs
// the HTTP status and a byte count from the body, and returns. Nothing else --
// no display, no audio, no cJSON. If this fails, the network stack is broken.
// If this passes, the network stack is fine and any other HTTPS failures
// belong to the caller.

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"

// Copied from firmware/C/MVP/main/config.h so this project stands alone.
#define WIFI_SSID   "SM-G970U72a"
#define WIFI_PASS   "0000005958"

// A stable, well-known HTTPS endpoint that returns a small JSON body.
// httpbin is behind Cloudflare -- same TLS profile as our real API host,
// which is deliberate: if this works and our API doesn't, the fault is
// on our API side. Swap for anything else you like.
#define TEST_URL    "https://httpbin.org/get"

static const char *TAG = "https_sanity";

static EventGroupHandle_t s_wifi_events;
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAILED_BIT     BIT1

static int s_retry_count = 0;
#define WIFI_MAX_RETRIES    10

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < WIFI_MAX_RETRIES) {
            s_retry_count++;
            ESP_LOGW(TAG, "disconnected, retry %d/%d", s_retry_count, WIFI_MAX_RETRIES);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_events, WIFI_FAILED_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&evt->ip_info.ip));
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_up(void)
{
    s_wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_cfg = { 0 };
    strlcpy((char *)wifi_cfg.sta.ssid,     WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, WIFI_PASS, sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "connecting to '%s'...", WIFI_SSID);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                           WIFI_CONNECTED_BIT | WIFI_FAILED_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);
    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_FAIL;
}

static esp_err_t do_https_get(void)
{
    esp_http_client_config_t cfg = {
        .url = TEST_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 30000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    ESP_LOGI(TAG, "GET %s", TEST_URL);
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "perform failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int status = esp_http_client_get_status_code(client);
    int64_t len = esp_http_client_get_content_length(client);
    ESP_LOGI(TAG, "status=%d content-length=%lld", status, (long long)len);

    esp_http_client_cleanup(client);
    return (status >= 200 && status < 300) ? ESP_OK : ESP_FAIL;
}

void app_main(void)
{
    // NVS is required by the Wi-Fi driver for calibration data. Erase and
    // retry if the partition is a leftover from an incompatible build.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    if (wifi_up() != ESP_OK) {
        ESP_LOGE(TAG, "wifi failed to come up -- aborting");
        return;
    }

    esp_err_t result = do_https_get();

    if (result == ESP_OK) {
        ESP_LOGI(TAG, "PASS: HTTPS works on this board.");
    } else {
        ESP_LOGE(TAG, "FAIL: HTTPS request did not complete successfully.");
    }
    ESP_LOGI(TAG, "done. app_main returning.");
}
