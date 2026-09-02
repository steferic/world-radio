#pragma once

#include "esp_err.h"

// Sets up ADC1 on VOLUME_POT_GPIO and starts a low-priority
// background task that samples the potentiometer, smooths out ADC noise,
// and applies a square-law taper so it feels natural to a human ear.
esp_err_t volume_control_init(void);

// Get current volume gain, a value between 0.0 (silent) - 1.0 (full, undamped audio).
float volume_control_get_gain(void);
