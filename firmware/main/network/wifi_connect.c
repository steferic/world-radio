#include "wifi_connect.h"
#include "config.h"

#include <string.h>

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "wifi_connect";

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_count = 0;

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    // Here we account for three possible scenarios: 1) the device just booted and is not yet
    // connected to wifi, 2) device was connected but then suddenly disconnected, and 3) device
    // successfully connects and is provisioned an IP address.
    // In 1), we simply call esp_wifi_connect() and wait to connect. In 2), we retry connecting
    // until we hit WIFI_CONNECT_MAX_RETRY, after which we reboot. In 3), we mark the success
    // and move on.

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < WIFI_CONNECT_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGW(TAG, "Disconnected, retrying (%d/%d)", s_retry_count, WIFI_CONNECT_MAX_RETRY);
        } else if (s_wifi_event_group != NULL) {
            // Still in the initial connect attempt and out of retries.
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        } else {
            // We were already connected once before; keep retrying forever
            // in the background so the stream can recover after a Wi-Fi
            // outage without a full reboot.
            ESP_LOGW(TAG, "disconnected after initial connect, retrying...");
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        if (s_wifi_event_group != NULL) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

esp_err_t wifi_connect_start(void)
{
    s_wifi_event_group = xEventGroupCreate();

    // esp_netif_init() and esp_event_loop_create_default() are expected to
    // have already been called once by app_main() before this.
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // By default, the modem will sleep between RX cycles to save power.
    // However, this application streams audio and is highly latency-
    // sensitive, and it is wall-powered too, so we don't need the modem
    // to sleep. Therefore disable 'periodic stall'.
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_LOGI(TAG, "connecting to '%s'...", WIFI_SSID);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE, pdFALSE, portMAX_DELAY);

    // From here on the event handler falls through to the "retry forever"
    // branch on disconnect instead of touching the (now retired) event group.
    EventGroupHandle_t eg = s_wifi_event_group;
    s_wifi_event_group = NULL;
    vEventGroupDelete(eg);

    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }

    ESP_LOGE(TAG, "failed to connect after %d attempts, rebooting", WIFI_CONNECT_MAX_RETRY);
    esp_restart();
    return ESP_FAIL;
}
