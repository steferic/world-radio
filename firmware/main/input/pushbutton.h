#pragma once

#include <stdint.h>
#include <stdatomic.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Diagnostic counters populated by the ISR. 'raw_edges' increments on every
// interrupt regardless of debounce or level check, useful for diagnosing
// whether the ISR is firing at all. 'accepted_presses' increments only for
// presses that made it past debounce and the active-low level check.
extern atomic_uint_fast32_t g_pushbutton_raw_edges;
extern atomic_uint_fast32_t g_pushbutton_accepted_presses;

// For now this can be left a simple bool, but in the future we may want
// to differentiate between e.g. PRESS_SHORT and PRESS_LONG.
typedef bool pushbutton_event_t;

esp_err_t pushbutton_init(QueueHandle_t event_queue);