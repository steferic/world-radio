#include "pushbutton.h"
#include "config.h"

#include <stdatomic.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

static const char *TAG = "pushbutton";

static QueueHandle_t s_event_queue = NULL;

// Debounce state. 's_last_edge_us' is the timestamp of the most recent
// state-change edge; anything closer than DEBOUNCE_US is ignored as bounce.
// 's_pressed_state' remembers whether the button is currently held down.
static volatile int64_t s_last_edge_us = 0;
static volatile bool s_pressed_state = false;

// Diagnostic counters. See pushbutton.h.
atomic_uint_fast32_t g_pushbutton_raw_edges = 0;
atomic_uint_fast32_t g_pushbutton_accepted_presses = 0;

static void IRAM_ATTR pushbutton_isr(void *arg)
{
    (void)arg;

    atomic_fetch_add(&g_pushbutton_raw_edges, 1);
    int level = gpio_get_level(PUSHBUTTON_GPIO);
    
    // esp_rom_printf is the only print call that's safe in ISR. ESP_LOG CANNOT 
    // run from ISR. There is a risk that esp_rom_printf can interleave with regular
    // ESP_LOG lines under heavy logging but that's fine for a diagnostic.
    esp_rom_printf("[pushbutton] ISR edge, level=%d\n", level);

    // Debounce logic
    int64_t now = esp_timer_get_time();
    if (now - s_last_edge_us < DEBOUNCE_US) {
        return;
    }

    // Only act on a genuine level change
    bool now_pressed = (level == 0);
    if (now_pressed == s_pressed_state) {
        return;
    }
    s_pressed_state = now_pressed;
    s_last_edge_us = now;

    // This precludes firing an event on the release edge. We fire on
    // the press-edge only.
    if (!now_pressed) {
        return;
    }

    atomic_fetch_add(&g_pushbutton_accepted_presses, 1);
    esp_rom_printf("[pushbutton] press accepted -> queuing\n");

    if (s_event_queue != NULL) {
        pushbutton_event_t event = true;
        BaseType_t hp_woken = pdFALSE;
        BaseType_t ok = xQueueSendFromISR(s_event_queue, &event, &hp_woken);
        if (ok != pdTRUE) {
            // Queue was full. The shuffle_task is presumably stuck, or a
            // burst of presses landed faster than it can drain. Not fatal,
            // the press just won't fire a shuffle. Log and move on.
            esp_rom_printf("[pushbutton] queue full, press dropped\n");
        }
        if (hp_woken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    } else {
        esp_rom_printf("[pushbutton] queue is NULL, press dropped\n");
    }
}

esp_err_t pushbutton_init(QueueHandle_t event_queue)
{
    if (event_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_event_queue = event_queue;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PUSHBUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE, // ANYEDGE, not NEGEDGE, so both press and release
                                        // refresh the debounce timestamp, so a bouncy
                                        // release isn't misread as a new press later.
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }

    // The ISR service may already be installed by the rotary encoder driver.
    // ESP_ERR_INVALID_STATE just means some service did so first, which is
    // fine, in that case we still install our handler.
    err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = gpio_isr_handler_add(PUSHBUTTON_GPIO, pushbutton_isr, NULL);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Pushbutton ready on GPIO%d", PUSHBUTTON_GPIO);
    return ESP_OK;
}
