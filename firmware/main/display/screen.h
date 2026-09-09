#pragma once

#include "ui_event.h"

// This struct represents a common UI pattern that all display screens can
// fit into, so that ui_task.c can manipulate all screens. All screens follow
// this lifecycle:
//      - enter: called once when the screen becomes active, i.e. on a PUSH,
//              REPLACE, or when the screen above it is POPPED. First defines
//              what the screen draws
//      - handle_event: called every time the given screen is on top of stack,
//              and an input event or tick event is consumed. Each screen can
//              define what happens for each event. If the screen is static,
//              then this can be NULL.
//      - exit: called once when the screen is removed from the top of the stack.
//              Only necessary to free up memory allocated by the screen in its
//              lifecycle. If there's nothing to tear down, can be NULL.
struct screen {
    void (*enter)(void);
    void (*exit)(void);
    void (*handle_event)(const ui_event_t *evt);
};
