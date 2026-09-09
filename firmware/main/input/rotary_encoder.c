#include "rotary_encoder.h"
#include "config.h"
#include "display/ui_event.h"
#include "display/ui_task.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "rotary_encoder";

// Half-step quadrature decode table (Ben Buxton). Index is
// (prev_state << 2) | new_state, where each 2-bit state is (A << 1) | B.
// +1 accumulates toward CW, -1 toward CCW; a full detent equals 4 counts
// on a standard EC11, which filters out contact bounce for free.
static const int8_t s_table[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0,
};

static volatile uint8_t s_prev_state = 0;
static volatile int8_t  s_accum = 0;

static void IRAM_ATTR encoder_isr(void *arg)
{
    (void)arg;
    uint8_t a = gpio_get_level(ROTARY_ENCODER_GPIO_A);
    uint8_t b = gpio_get_level(ROTARY_ENCODER_GPIO_B);
    uint8_t new_state = (uint8_t)((a << 1) | b);
    uint8_t idx = (uint8_t)((s_prev_state << 2) | new_state);
    s_prev_state = new_state;
    s_accum += s_table[idx];

    // Raw-edge diagnostic. If nothing prints when we turn the knob, the
    // ISR isn't firing at all. If only one of a/b ever changes across a
    // full detent, the encoder's other line isn't reaching the pin, a
    // hardware problem. A healthy detent a/b walking through 
    // 11 -> 01 -> 00 -> 10 -> 11 (or the reverse).
    esp_rom_printf("[rotary_encoder] edge A=%d B=%d state=%d idx=%d accum=%d\n",
                   (int)a, (int)b, (int)new_state, (int)idx, (int)s_accum);

    ui_event_type_t type;
    if (s_accum >= 4) {
        s_accum = 0;
        type = ROTARY_ENCODER_INVERT ? UI_EVT_ROTATE_CCW : UI_EVT_ROTATE_CW;
    } else if (s_accum <= -4) {
        s_accum = 0;
        type = ROTARY_ENCODER_INVERT ? UI_EVT_ROTATE_CW : UI_EVT_ROTATE_CCW;
    } else {
        return; // detent not yet reached, nothing to emit
    }

    esp_rom_printf("[rotary_encoder] detent %s\n",
                   type == UI_EVT_ROTATE_CW ? "CW" : "CCW");

    ui_event_t evt = { .type = type, .screen = NULL };
    BaseType_t hp_woken = pdFALSE;
    ui_post_from_isr(&evt, &hp_woken);
    if (hp_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

esp_err_t rotary_encoder_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << ROTARY_ENCODER_GPIO_A) | (1ULL << ROTARY_ENCODER_GPIO_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }

    // Seed the state machine with the pins' current levels so the first
    // real edge doesn't decode against a false "00" prior state and emit
    // a phantom click on boot.
    s_prev_state = (uint8_t)((gpio_get_level(ROTARY_ENCODER_GPIO_A) << 1)
                           | gpio_get_level(ROTARY_ENCODER_GPIO_B));

    // gpio_install_isr_service is a per-app singleton. If another module
    // already installed it, we get ESP_ERR_INVALID_STATE, which is fine.
    err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = gpio_isr_handler_add(ROTARY_ENCODER_GPIO_A, encoder_isr, NULL);
    if (err != ESP_OK) return err;
    err = gpio_isr_handler_add(ROTARY_ENCODER_GPIO_B, encoder_isr, NULL);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "Rotary encoder ready on GPIO%d/GPIO%d",
             ROTARY_ENCODER_GPIO_A, ROTARY_ENCODER_GPIO_B);
    return ESP_OK;
}
