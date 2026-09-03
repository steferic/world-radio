#pragma once

#include "display/screen.h"

// "Connecting to Wi-Fi..." with an animated dot count. This is the root
// screen at startup; ui_task_start(&boot_screen) is what makes it appear
// before wifi_connect_start blocks the main task. Once wifi is up, main
// swaps to now_playing via ui_screen_replace().
extern const screen_t boot_screen;
