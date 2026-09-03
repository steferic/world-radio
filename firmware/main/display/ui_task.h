#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "ui_event.h"
#include "screen.h"

// Brings up the LCD, creates the UI event queue, launches the UI task,
// and installs `initial` as the sole entry on the screen stack.
// Call once, early in app_main (before wifi/audio bringup) so the boot
// screen is visible while the rest of the system is still initializing.
esp_err_t ui_task_start(const screen_t *initial);

// Posts an event to the UI queue from any task. Non-blocking, drops on
// full queue (which shouldn't happen -- the queue is sized to comfortably
// absorb the worst-case burst from a hard rotary spin).
void ui_post(const ui_event_t *evt);

// Same as ui_post but callable from inside an ISR. Both rotary_encoder
// and pushbutton use this from their GPIO handlers.
void ui_post_from_isr(const ui_event_t *evt, BaseType_t *higher_prio_task_woken);

// --- Navigation helpers, thin wrappers around ui_post ---
// See ui_event.h for what REPLACE/PUSH/POP mean.
void ui_screen_replace(const screen_t *s);
void ui_screen_push(const screen_t *s);
void ui_screen_pop(void);
