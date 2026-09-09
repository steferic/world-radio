#pragma once

#include "esp_err.h"

// Installs GPIO ISRs on ROTARY_ENCODER_GPIO_A / ROTARY_ENCODER_GPIO_B.
// Each completed detent posts a UI_EVT_ROTATE_CW or UI_EVT_ROTATE_CCW
// event onto the UI queue via ui_post_from_isr
esp_err_t rotary_encoder_init(void);
