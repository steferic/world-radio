#include "rotary_button.h"
#include "config.h"
#include "display/ui_event.h"
#include "display/ui_task.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "rotary_button";

// Timestamp of the last accepted edge, used to debounce.
static volatile int64_t s_last_edge_us = 0;
static volatile int64_t s_press_us = 0;
static volatile bool s_pressed = false;

static void IRAM_ATTR rotary_button_isr(void *arg) {
    (void)arg;

    // Debounce logic
    int64_t now = esp_timer_get_time();
    if (now - s_last_edge_us < DEBOUNCE_US) {
        return;
    }
    s_last_edge_us = now;

    // Button is active LOW, with internal GPIO pullup.
    int level = gpio_get_level(ROTARY_ENCODER_PUSHBUTTON);
    if (level == 0) {
        // Pressed
        s_press_us = now;
        s_pressed = true;
        return;
    }

    // Released
    if (!s_pressed) {
        return;
    }
    s_pressed = false;

    int64_t held_us = now - s_press_us;
    ui_event_type_t type = (held_us >= LONG_PRESS_US)
        ? UI_EVT_BUTTON_LONG
        : UI_EVT_BUTTON_SHORT;

    ui_event_t evt = { .type = type, .screen = NULL };
    BaseType_t hp_woken = pdFALSE;
    ui_post_from_isr(&evt, &hp_woken);

    // esp_rom_printf is the only print call that's safe in ISR. ESP_LOG CANNOT 
    // run from ISR. There is a risk that esp_rom_printf can interleave with regular
    // ESP_LOG lines under heavy logging but that's fine for a diagnostic.
    esp_rom_printf("[rotary_button] press %s\n",
                   type == UI_EVT_BUTTON_LONG ? "LONG" : "SHORT");
    if (hp_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

esp_err_t rotary_button_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << ROTARY_ENCODER_PUSHBUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = gpio_isr_handler_add(ROTARY_ENCODER_PUSHBUTTON, rotary_button_isr, NULL);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Rotary encoder switch ready on GPIO%d", ROTARY_ENCODER_PUSHBUTTON);
    return ESP_OK;
}
