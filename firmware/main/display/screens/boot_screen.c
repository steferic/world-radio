#include "boot_screen.h"

#include <stdio.h>
#include <stdint.h>

#include "display/ui_draw.h"
#include "display/lcd_driver.h"

// Row positions -- portrait 240x320-rotated-to-320x240, so LCD_HEIGHT=240.
// Two lines vertically centered: the app name at ~1/3, status underneath.
#define Y_TITLE   80
#define Y_STATUS  130

// Dot animation: cycles 0..3 dots on each TICK. UI_EVT_TICK arrives ~10 Hz;
// dividing that by DOT_TICK_STRIDE gives a dot every ~600 ms, which reads
// as "working, not frozen" without being distracting.
#define DOT_TICK_STRIDE 6

static uint32_t s_tick_count = 0;
static int s_dot_count = -1; // force first redraw

static void draw_status(int dots)
{
    char buf[32];
    // Build "Connecting to Wi-Fi..." with a variable number of trailing
    // dots. Padded to 3 trailing spaces so shrinking from 3 dots back to
    // 0 doesn't leave stale "..." behind if ui_draw_field's clear ever
    // slipped -- belt and suspenders.
    switch (dots) {
    case 0:  snprintf(buf, sizeof(buf), "CONNECTING TO WI-FI   "); break;
    case 1:  snprintf(buf, sizeof(buf), "CONNECTING TO WI-FI.  "); break;
    case 2:  snprintf(buf, sizeof(buf), "CONNECTING TO WI-FI.. "); break;
    default: snprintf(buf, sizeof(buf), "CONNECTING TO WI-FI..."); break;
    }
    ui_draw_field_centered(UI_MARGIN_X, Y_STATUS, UI_FIELD_MAX_WIDTH, 1,
                           buf, LCD_COLOR_GRAY, LCD_COLOR_BLACK);
}

static void boot_enter(void)
{
    s_tick_count = 0;
    s_dot_count = -1; // makes the first tick redraw immediately
    ui_draw_field_centered(UI_MARGIN_X, Y_TITLE, UI_FIELD_MAX_WIDTH, 2,
                           "WORLD RADIO", LCD_COLOR_CYAN, LCD_COLOR_BLACK);
    draw_status(0);
    s_dot_count = 0;
}

static void boot_handle(const ui_event_t *evt)
{
    // Boot screen ignores all input, since there's nothing to do until wifi
    // connects, and main.c is what drives the transition. Only TICK
    // matters here.
    if (evt->type != UI_EVT_TICK) return;

    s_tick_count++;
    if (s_tick_count % DOT_TICK_STRIDE != 0) return;

    int next = (s_dot_count + 1) & 0x3; // cycles 0,1,2,3
    if (next != s_dot_count) {
        s_dot_count = next;
        draw_status(next);
    }
}

const screen_t boot_screen = {
    .enter        = boot_enter,
    .exit         = NULL,
    .handle_event = boot_handle,
};
