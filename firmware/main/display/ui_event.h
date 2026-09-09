#pragma once

#include <stdint.h>

// Forward declaration. screen_t is defined in screen.h, but events
// need to carry a screen pointer for navigation.
struct screen;
typedef struct screen screen_t;

// This is the single event type flowing throughout the UI queue. Producers are:
//   - rotary_encoder ISR (ROTATE_CW, ROTATE_CCW)
//   - pushbutton ISR  (BUTTON_SHORT, BUTTON_LONG)
//   - any task that wants to swap screens (PUSH/POP/REPLACE)
//   - any task that updates now-playing state (METADATA_CHANGED)
//   - the UI task itself, synthesizing TICK when the queue is idle
//
// Consumers are always the currently active screen's handle_event().
typedef enum {
    // --- Physical input events ---
    UI_EVT_ROTATE_CW,
    UI_EVT_ROTATE_CCW,
    UI_EVT_BUTTON_SHORT,
    UI_EVT_BUTTON_LONG,

    // --- Navigation requests ---
    // REPLACE: discard the whole stack, install `screen` as the sole entry.
    //   Used when moving through the app lifecycle (e.g. boot -> now_playing)
    //   where going back to the previous screen would make no sense.
    // PUSH:    put the pushed screen on top; the covered screen stays on the stack
    //   and its state is preserved for when we POP back to it.
    // POP:     remove the top screen and re-enter the one beneath it.
    UI_EVT_REPLACE_SCREEN,
    UI_EVT_PUSH_SCREEN,
    UI_EVT_POP_SCREEN,

    // --- Data change events ---
    // Something in shared state changed (e.g. new ICY track metadata)
    UI_EVT_METADATA_CHANGED,

    // --- Timing events ---
    // Fired by the UI task itself whenever the event queue has been idle
    // for UI_TICK_INTERVAL_MS milliseconds. Used to manage screens that drive
    // small animations without needing their own timer tasks.
    UI_EVT_TICK,
} ui_event_type_t;

typedef struct {
    ui_event_type_t type;
    // A struct used only for PUSH_SCREEN and REPLACE_SCREEN; ignored otherwise.
    // We need a pointer-to-const because screens are static, immutable descriptors
    // (function-pointer tables), not per-instance objects.
    const screen_t *screen;
} ui_event_t;
