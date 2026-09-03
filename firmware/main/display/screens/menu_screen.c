#include "menu_screen.h"
#include "stations_screen.h"
#include "wifi_setup_screen.h"

#include <stdbool.h>

#include "display/ui_draw.h"
#include "display/ui_task.h"
#include "display/lcd_driver.h"

#define Y_TITLE       12
#define Y_ITEMS_START 60
#define ITEM_HEIGHT   32

// A menu item is a display name plus an action closure. Actions are
// plain function pointers (no user_data) because everything they need is
// global (a screen singleton, a ui_screen_* call). If actions ever grow
// to need arguments, add a `void *ctx` here.
typedef struct {
    const char *label;
    void (*action)(void);
} menu_item_t;

static void action_open_stations(void)  { ui_screen_push(&stations_screen); }
static void action_open_wifi(void)      { ui_screen_push(&wifi_setup_screen); }
static void action_back(void)           { ui_screen_pop(); }

static const menu_item_t s_items[] = {
    { "STATIONS",   action_open_stations },
    { "WI-FI SETUP", action_open_wifi },
    { "BACK",       action_back },
};
#define ITEM_COUNT ((int)(sizeof(s_items) / sizeof(s_items[0])))

// Highlight index. Persisted across enter/exit within one power cycle
// because s_selected is file-static; whether that's the right UX is
// debatable (some users want the menu to always open on item 0) --
// change to reset in enter() if you prefer that.
static int s_selected = 0;

// Draws one item at its slot. If `selected`, the row is drawn white-on-
// gray to give a bar-like highlight; otherwise gray-on-black.
static void draw_item(int index, bool selected)
{
    int y = Y_ITEMS_START + index * ITEM_HEIGHT;
    uint16_t fg = selected ? LCD_COLOR_BLACK : LCD_COLOR_WHITE;
    uint16_t bg = selected ? LCD_COLOR_CYAN  : LCD_COLOR_BLACK;
    // Full-width band so the highlight extends to the margins.
    lcd_fill_rect(0, y - 4, LCD_WIDTH, ITEM_HEIGHT, bg);
    ui_draw_field_centered(UI_MARGIN_X, y, UI_FIELD_MAX_WIDTH, 2,
                           s_items[index].label, fg, bg);
}

static void redraw_all_items(void)
{
    for (int i = 0; i < ITEM_COUNT; i++) {
        draw_item(i, i == s_selected);
    }
}

static void menu_enter(void)
{
    ui_draw_field_centered(UI_MARGIN_X, Y_TITLE, UI_FIELD_MAX_WIDTH, 2,
                           "SETTINGS", LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
    redraw_all_items();
}

static void move_selection(int delta)
{
    int prev = s_selected;
    s_selected += delta;
    if (s_selected < 0) s_selected = ITEM_COUNT - 1;         // wrap
    if (s_selected >= ITEM_COUNT) s_selected = 0;
    // Only redraw the two rows that changed -- cheaper and no perceptible
    // flicker on the previously highlighted row.
    draw_item(prev, false);
    draw_item(s_selected, true);
}

static void menu_handle(const ui_event_t *evt)
{
    switch (evt->type) {
    case UI_EVT_ROTATE_CW:    move_selection(+1); break;
    case UI_EVT_ROTATE_CCW:   move_selection(-1); break;
    case UI_EVT_BUTTON_SHORT: s_items[s_selected].action(); break;
    case UI_EVT_BUTTON_LONG:  ui_screen_pop(); break;
    default: break;
    }
}

const screen_t menu_screen = {
    .enter        = menu_enter,
    .exit         = NULL,
    .handle_event = menu_handle,
};
