#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// For now this can be left a simple bool, but in the future we may want
// to differentiate between e.g. PRESS_SHORT and PRESS_LONG.
typedef bool pushbutton_event_t;

esp_err_t pushbutton_init(QueueHandle_t event_queue);