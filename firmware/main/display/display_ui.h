#pragma once

#include "esp_err.h"

// Brings up the LCD and draws the static parts of the layout (header,
// divider, labels). Call once at startup.
esp_err_t display_ui_init(void);

// Updates the station block: name, city, country, language. Any argument
// may be NULL or empty to leave that field blank. Safe to call from any
// task -- internally serialized, so simultaneous calls from two tasks
// won't interleave their SPI writes.
void display_ui_set_station(const char *name, const char *city, const char *country, const char *language);

// Updates the track block: title and artist. Either argument may be NULL
// or empty to leave that field blank. A long title wraps onto a second
// line (breaking on a space where possible) and truncates with ".." if it
// still doesn't fit. Safe to call from any task.
void display_ui_set_track(const char *title, const char *artist);
