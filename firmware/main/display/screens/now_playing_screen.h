#pragma once

#include "display/screen.h"

// The "home" screen once wifi is up: header, station block, and track
// block. Short-press of the encoder button opens the settings menu.
//
// The screen holds its own copy of the strings so producers (main.c,
// future ICY parser, station-select code) can update state at any time,
// on any task, and it will render whenever it's visible. If the menu is
// covering it when an update arrives, the state is stored anyway and
// gets drawn the next time the screen is entered.
extern const screen_t now_playing_screen;

// Setters -- callable from any task. They mutate shared state under a
// mutex, then post UI_EVT_METADATA_CHANGED so the UI task redraws (if
// this screen is currently active).
void now_playing_set_station(const char *name, const char *city,
                             const char *country, const char *language);
void now_playing_set_track(const char *title, const char *artist);
