#pragma once

#include "esp_err.h"

// Brings up Wi-Fi in station mode using WIFI_SSID/WIFI_PASS from config.h.
// Blocks until an IP address is obtained (or reboots the device after
// WIFI_CONNECT_MAX_RETRY failed attempts). Also installs a background
// handler that reconnects automatically if the AP is lost later on.
esp_err_t wifi_connect_start(void);
