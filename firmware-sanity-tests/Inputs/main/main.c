// MVP inputs sanity test.
//
// Configures every user-facing input the MVP firmware exposes and prints one
// confirmation line per event received. No display, no audio, no networking --
// if a press or a turn doesn't show up here, the fault is wiring or GPIO
// config, not something further up the stack.
//
// Inputs, matching firmware/main/config/gpio.h:
//   - Standalone pushbutton      : GPIO21 (active-low, internal pull-up)
//   - Rotary encoder A / B       : GPIO15 / GPIO16 (quadrature, pull-ups)
//   - Rotary encoder pushbutton  : GPIO47 (active-low, internal pull-up)
//   - Volume potentiometer       : GPIO4  (ADC1, 12-bit, 0-3.3V atten)
//
// The pushbuttons and rotary use edge-triggered ISRs. The pot is polled in a
// FreeRTOS task and only prints when the smoothed reading crosses a threshold
// so a still knob doesn't spam the monitor.

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ----------------------------------------------------------------------------
// GPIO / ADC assignments (copied from firmware/main/config/gpio.h so this
// project stands alone -- keep in sync if the main firmware ever moves them).
// ----------------------------------------------------------------------------
#define PUSHBUTTON_GPIO           GPIO_NUM_21
#define ROTARY_ENCODER_GPIO_A     GPIO_NUM_15
#define ROTARY_ENCODER_GPIO_B     GPIO_NUM_16
#define ROTARY_ENCODER_PUSHBUTTON GPIO_NUM_47
#define VOLUME_POT_GPIO           GPIO_NUM_4

// Debounce window for the two mechanical buttons. 30 ms is comfortably longer
// than any bounce on a normal tact switch without feeling laggy.
#define DEBOUNCE_US               (30 * 1000)

// Anything above this much change (out of 4095) in the smoothed pot reading
// gets logged as a real turn -- keeps a still knob from spamming the console.
#define POT_LOG_DELTA             80

// Poll interval for the pot task.
#define POT_POLL_INTERVAL_MS      30

static const char *TAG = "inputs_sanity";

// ----------------------------------------------------------------------------
// Standalone pushbutton
// ----------------------------------------------------------------------------
static volatile int64_t s_pb_last_edge_us = 0;
static volatile bool    s_pb_pressed      = false;

static void IRAM_ATTR pushbutton_isr(void *arg)
{
    (void)arg;

    int64_t now = esp_timer_get_time();
    if (now - s_pb_last_edge_us < DEBOUNCE_US) {
        return;
    }

    int level = gpio_get_level(PUSHBUTTON_GPIO);
    bool now_pressed = (level == 0);
    if (now_pressed == s_pb_pressed) {
        return;
    }
    s_pb_pressed = now_pressed;
    s_pb_last_edge_us = now;

    // esp_rom_printf is ISR-safe; ESP_LOG_* is not.
    esp_rom_printf("[pushbutton]      GPIO%d %s\n",
                   PUSHBUTTON_GPIO,
                   now_pressed ? "PRESS" : "release");
}

// ----------------------------------------------------------------------------
// Rotary encoder (quadrature) -- half-step Buxton table
// ----------------------------------------------------------------------------
static const int8_t s_rotary_table[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0,
};
static volatile uint8_t s_rotary_prev_state = 0;
static volatile int8_t  s_rotary_accum      = 0;

static void IRAM_ATTR rotary_encoder_isr(void *arg)
{
    (void)arg;
    uint8_t a = gpio_get_level(ROTARY_ENCODER_GPIO_A);
    uint8_t b = gpio_get_level(ROTARY_ENCODER_GPIO_B);
    uint8_t new_state = (uint8_t)((a << 1) | b);
    uint8_t idx = (uint8_t)((s_rotary_prev_state << 2) | new_state);
    s_rotary_prev_state = new_state;
    s_rotary_accum += s_rotary_table[idx];

    // A standard EC11 puts out 4 quadrature counts per detent; only announce
    // once we've collected a whole detent's worth.
    if (s_rotary_accum >= 4) {
        s_rotary_accum = 0;
        esp_rom_printf("[rotary_encoder]  detent CW  (A=GPIO%d B=GPIO%d)\n",
                       ROTARY_ENCODER_GPIO_A, ROTARY_ENCODER_GPIO_B);
    } else if (s_rotary_accum <= -4) {
        s_rotary_accum = 0;
        esp_rom_printf("[rotary_encoder]  detent CCW (A=GPIO%d B=GPIO%d)\n",
                       ROTARY_ENCODER_GPIO_A, ROTARY_ENCODER_GPIO_B);
    }
}

// ----------------------------------------------------------------------------
// Rotary encoder built-in pushbutton
// ----------------------------------------------------------------------------
static volatile int64_t s_rb_last_edge_us = 0;
static volatile bool    s_rb_pressed      = false;

static void IRAM_ATTR rotary_button_isr(void *arg)
{
    (void)arg;

    int64_t now = esp_timer_get_time();
    if (now - s_rb_last_edge_us < DEBOUNCE_US) {
        return;
    }

    int level = gpio_get_level(ROTARY_ENCODER_PUSHBUTTON);
    bool now_pressed = (level == 0);
    if (now_pressed == s_rb_pressed) {
        return;
    }
    s_rb_pressed = now_pressed;
    s_rb_last_edge_us = now;

    esp_rom_printf("[rotary_button]   GPIO%d %s\n",
                   ROTARY_ENCODER_PUSHBUTTON,
                   now_pressed ? "PRESS" : "release");
}

// ----------------------------------------------------------------------------
// GPIO init
// ----------------------------------------------------------------------------
static esp_err_t init_gpio_inputs(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PUSHBUTTON_GPIO)
                      | (1ULL << ROTARY_ENCODER_GPIO_A)
                      | (1ULL << ROTARY_ENCODER_GPIO_B)
                      | (1ULL << ROTARY_ENCODER_PUSHBUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }

    // Seed the rotary state so the very first real edge doesn't decode
    // against a bogus prior state and emit a phantom click at boot.
    s_rotary_prev_state = (uint8_t)((gpio_get_level(ROTARY_ENCODER_GPIO_A) << 1)
                                 |  gpio_get_level(ROTARY_ENCODER_GPIO_B));

    err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(err));
        return err;
    }

    err  = gpio_isr_handler_add(PUSHBUTTON_GPIO,           pushbutton_isr,     NULL);
    err |= gpio_isr_handler_add(ROTARY_ENCODER_GPIO_A,     rotary_encoder_isr, NULL);
    err |= gpio_isr_handler_add(ROTARY_ENCODER_GPIO_B,     rotary_encoder_isr, NULL);
    err |= gpio_isr_handler_add(ROTARY_ENCODER_PUSHBUTTON, rotary_button_isr,  NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_isr_handler_add failed");
        return err;
    }
    return ESP_OK;
}

// ----------------------------------------------------------------------------
// Volume potentiometer (ADC1 one-shot, polled in a task)
// ----------------------------------------------------------------------------
static adc_oneshot_unit_handle_t s_adc_handle;
static adc_channel_t             s_adc_channel;

static void volume_task(void *pvParameters)
{
    (void)pvParameters;

    // Match the MVP smoothing: light IIR so a jittery ADC line doesn't fire
    // a log line every poll. Seed at midscale so the first announce isn't
    // a huge jump from zero.
    float smoothed = 2048.0f;
    int   last_logged = -POT_LOG_DELTA * 2; // guarantee the first sample logs

    while (1) {
        int raw = 0;
        esp_err_t err = adc_oneshot_read(s_adc_handle, s_adc_channel, &raw);
        if (err == ESP_OK) {
            smoothed += 0.2f * ((float)raw - smoothed);
            int cur = (int)smoothed;
            if (abs(cur - last_logged) >= POT_LOG_DELTA) {
                float pct = (cur / 4095.0f) * 100.0f;
                ESP_LOGI(TAG,
                         "[potentiometer]   GPIO%d raw=%4d  (~%.0f%%)",
                         VOLUME_POT_GPIO, cur, pct);
                last_logged = cur;
            }
        } else {
            ESP_LOGW(TAG, "adc_oneshot_read failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(POT_POLL_INTERVAL_MS));
    }
}

static esp_err_t init_potentiometer(void)
{
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t err = adc_oneshot_new_unit(&init_cfg, &s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(err));
        return err;
    }

    // Resolve GPIO -> ADC channel instead of hardcoding the channel number,
    // so this stays honest if VOLUME_POT_GPIO ever changes. Also validates
    // that the chosen pin actually lives on ADC1.
    adc_unit_t resolved_unit;
    err = adc_oneshot_io_to_channel(VOLUME_POT_GPIO, &resolved_unit, &s_adc_channel);
    if (err != ESP_OK || resolved_unit != ADC_UNIT_1) {
        ESP_LOGE(TAG, "GPIO%d is not a valid ADC1 pin", VOLUME_POT_GPIO);
        return (err != ESP_OK) ? err : ESP_ERR_INVALID_ARG;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten    = ADC_ATTEN_DB_12,
    };
    err = adc_oneshot_config_channel(s_adc_handle, s_adc_channel, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    BaseType_t ok = xTaskCreate(volume_task, "pot_poll", 3072, NULL, 2, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create pot_poll task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void app_main(void)
{
    // Let the boot log flush before the banner.
    vTaskDelay(pdMS_TO_TICKS(200));

    printf("\n\n");
    printf("======================================================\n");
    printf("           MVP INPUTS SANITY TEST\n");
    printf("======================================================\n");
    printf("  standalone pushbutton   : GPIO%d\n", PUSHBUTTON_GPIO);
    printf("  rotary encoder A / B    : GPIO%d / GPIO%d\n",
           ROTARY_ENCODER_GPIO_A, ROTARY_ENCODER_GPIO_B);
    printf("  rotary encoder button   : GPIO%d\n", ROTARY_ENCODER_PUSHBUTTON);
    printf("  volume potentiometer    : GPIO%d (ADC1)\n", VOLUME_POT_GPIO);
    printf("------------------------------------------------------\n");
    printf("  Turn / press each input. A confirmation line prints\n");
    printf("  below every time an event is accepted. Silence for\n");
    printf("  an input under real actuation = wiring / config bug\n");
    printf("  on that input, not further up the stack.\n");
    printf("======================================================\n\n");

    if (init_gpio_inputs() != ESP_OK) {
        ESP_LOGE(TAG, "GPIO init failed -- aborting");
        return;
    }
    if (init_potentiometer() != ESP_OK) {
        ESP_LOGE(TAG, "potentiometer init failed -- aborting");
        return;
    }

    ESP_LOGI(TAG, "all inputs armed. Waiting for events...");

    // Idle forever; the ISRs and the pot task do all the work.
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
