#include "pushbutton.h"
#include "config.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "pushbutton";

static QueueHandle_t s_event_queue = NULL;

static volatile int64_t s_last_edge_us = 0;

static void IRAM_ATTR pushbutton_isr(void *arg) {
    
    (void)arg;

    // If the button is pushed, it must also be debounced. Therefore,
    // we need to set a timeout value and reject transient changes that
    // don't persist a sufficiently long time.

    int64_t now = esp_timer_get_time();
    if (now - s_last_edge_us < DEBOUNCE_US) {
        return; // Button is bouncing, so ignore it.
    }
    s_last_edge_us = now;


    // Read GPIO level. The button is active LOW, using the internal pullup.
    // This also rejects falling-edge readings. We fire this code on press,
    // not on release.
    if (gpio_get_level(PUSHBUTTON_GPIO) != 0) {
        return;
    }

    if (e_event_queue != null) {
        // Here we create a pushbutton event indicating a press, and send it
        // to the queue.
        pushbutton_event_t event = true;
        BaseType_t hp_woken = pdFALSE;
        xQueueSendFromISR(s_event_queue, &event, &hp_woken);
        if (hp_woken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}

esp_err_t pushbutton_init(QueueHandle_t event_queue) {
    if (event_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_event_queue = event_queue;

    // Set up GPIO pin for pushbutton use.
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PUSHBUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }

    // Now we install the ESP32 ISR service. It is possible
    // that the rotary encoder already installed the service,
    // which would be fine but would throw an error. In that
    // case, we just move on to install our GPIO handler
    // alongside the rotary encoder.
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