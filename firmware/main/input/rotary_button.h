#pragma once

#include "esp_err.h"

// Installs a GPIO ISR on ROTARY_ENCODER_PUSHBUTTON (the button on the
// EC11 rotary encoder) that posts UI_EVT_BUTTON_SHORT or UI_EVT_BUTTON_LONG
// onto the UI event queue. The press-vs-hold decision is made on release.
esp_err_t rotary_button_init(void);
