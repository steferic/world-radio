#pragma once

#include "esp_err.h"

// Sets up ADC1 on VOLUME_POT_GPIO (config.h) and starts a low-priority
// background task that samples the potentiometer, smooths out ADC noise,
// and applies a square-law taper so it feels natural to a human ear (which
// perceives loudness roughly logarithmically, not linearly with voltage).
esp_err_t volume_control_init(void);

// Current volume gain: 0.0 (silent) to 1.0 (full scale, unmodified audio).
// Safe to call from any task -- backed by an atomic relaxed-order read, and
// a briefly stale value for a single audio frame is inaudible.
float volume_control_get_gain(void);
