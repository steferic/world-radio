#pragma once

#include <stdint.h>

// Forward declaration only -- screen_t is defined in screen.h, but events
// need to carry a screen pointer for navigation. Including screen.h here
// would create a cycle (screens include ui_event.h to know what they
// receive). The forward decl breaks the cycle.
struct screen;
typedef struct screen screen_t;

// The single event type flowing through the UI queue. Producers are:
//   - rotary_encoder ISR (ROTATE_CW, ROTATE_CCW)
//   - pushbutton ISR    (BUTTON_SHORT, BUTTON_LONG)
//   - any task that wants to swap screens (PUSH/POP/REPLACE)
//   - any task that updates now-playing state (METADATA_CHANGED)
//   - the UI task itself, synthesizing TICK when the queue is idle
//
// Consumers are always the currently active screen's handle_event().
typedef enum {
    // --- Input events ---
    UI_EVT_ROTATE_CW,
    UI_EVT_ROTATE_CCW,
    UI_EVT_BUTTON_SHORT,
    UI_EVT_BUTTON_LONG,

    // --- Navigation requests ---
    // REPLACE: discard the whole stack, install `screen` as the sole entry.
    //   Use when moving through app lifecycle (boot -> now_playing) where
    //   going "back" to the previous view would make no sense.
    // PUSH:    put `screen` on top; the covered screen stays on the stack
    //   and its state is preserved for when we POP back to it.
    // POP:     remove the top screen and re-enter the one beneath it.
    UI_EVT_REPLACE_SCREEN,
    UI_EVT_PUSH_SCREEN,
    UI_EVT_POP_SCREEN,

    // --- Data notifications ---
    // Something in shared state changed (e.g. new ICY track). Screens that
    // display that state redraw from it; screens that don't, ignore.
    UI_EVT_METADATA_CHANGED,

    // --- Timing ---
    // Fired by the UI task itself whenever the event queue has been idle
    // for UI_TICK_INTERVAL_MS. Screens use it to drive small animations
    // (spinner dots on the boot screen, blink cursors, etc.) without
    // needing their own timer tasks.
    UI_EVT_TICK,
} ui_event_type_t;

typedef struct {
    ui_event_type_t type;
    // Only meaningful for PUSH_SCREEN and REPLACE_SCREEN; ignored otherwise.
    // A pointer-to-const because screens are static, immutable descriptors
    // (function-pointer tables), not per-instance objects.
    const screen_t *screen;
} ui_event_t;
