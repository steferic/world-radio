#pragma once

#include "ui_event.h"

// A "screen" is a full-panel view -- boot spinner, now playing, main menu,
// etc. The abstraction is deliberately tiny: three optional function
// pointers. No per-instance data, no class hierarchy -- each screen is a
// single global `const screen_t` populated at file scope in that screen's
// .c file, and any state the screen needs is kept as file-static variables
// in the same file. This keeps allocation out of it (important on an
// MCU) and makes each screen readable on its own without chasing
// vtables.
//
// Lifetime:
//   enter()  -- called once when this screen becomes active. That happens on
//               a fresh PUSH, on REPLACE, or on POP-return (the screen
//               beneath is re-entered from scratch, so any covered redraw
//               is trivial). The LCD has just been cleared to black by the
//               UI task, so draw everything from scratch here.
//   exit()   -- called once when the screen is leaving the top of the stack
//               (a PUSH is covering it, or a POP/REPLACE is removing it).
//               Use for stopping timers or freeing tick state. May be NULL
//               if there's nothing to tear down.
//   handle_event() -- called for every event delivered while this screen is
//               on top. Input events, TICKs, and METADATA_CHANGED all
//               arrive here. May be NULL if the screen is static.
struct screen {
    void (*enter)(void);
    void (*exit)(void);
    void (*handle_event)(const ui_event_t *evt);
};
