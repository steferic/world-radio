#pragma once

#include "display/screen.h"

// A vertical list of hard-coded settings entries: "Stations", "Wi-Fi
// Setup", "Back". Rotate to move the highlight; short-press to activate
// the selected item; long-press = back (same as picking "Back").
//
// Pushed from now_playing_screen on button short-press.
extern const screen_t menu_screen;
