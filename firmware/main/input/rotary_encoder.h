#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// A signed step counter for keeping track of the rotary encoder's turns. +1
// indicates a clockwise turn, -1 for a counter-clockwise turn. 
typedef int8_t rotary_encoder_step_t;

// Installs GPIO ISRs on ROTARY_ENCODER_GPIO_A / ROTARY_ENCODER_GPIO_B and
// pushes a rotary_encoder_step_t struct (+1 CW, -1 CCW) into event_queue on
// every completed detent.
esp_err_t rotary_encoder_init(QueueHandle_t event_queue);
