#include "stations_screen.h"
#include "now_playing_screen.h"

#include <stdbool.h>

#include "display/ui_draw.h"
#include "display/ui_task.h"
#include "display/lcd_driver.h"

#define Y_TITLE       10
#define Y_LIST_START  46
#define ROW_HEIGHT    22
#define VISIBLE_ROWS  8

// Hardcoded station catalogue -- the real thing will come from a JSON
// file on flash (or the API), but the UI code below doesn't care what
// backs the list. Adding a "load from file" step later means only
// replacing s_stations and its length.
typedef struct {
    const char *name;
    const char *city;
    const char *country;
    const char *language;
    const char *url; // unused today; wired in when stream-swap lands
} station_t;

static const station_t s_stations[] = {
    { "ITALIAN DANCE NETWORK", "MILAN",      "ITALY",       "ITALIAN",   "https://italiandancenetwork.com/stream.mp3" },
    { "RADIO PARADISE",        "AUBURN",     "USA",         "ENGLISH",   "http://stream.radioparadise.com/mp3-192" },
    { "FIP",                   "PARIS",      "FRANCE",      "FRENCH",    "http://icecast.radiofrance.fr/fip-hifi.aac" },
    { "BBC WORLD SERVICE",     "LONDON",     "UK",          "ENGLISH",   "http://stream.live.vc.bbcmedia.co.uk/bbc_world_service" },
    { "NHK RADIO 1",           "TOKYO",      "JAPAN",       "JAPANESE",  "http://nhkworld.example/stream.mp3" },
    { "RADIO NACIONAL",        "BUENOS AIRES","ARGENTINA",  "SPANISH",   "http://radionacional.example/stream.mp3" },
    { "SOMA GROOVE SALAD",     "SAN FRANCISCO","USA",       "ENGLISH",   "http://ice1.somafm.com/groovesalad-128-mp3" },
    { "RADIO SWISS JAZZ",      "ZURICH",     "SWITZERLAND", "GERMAN",    "http://stream.srg-ssr.ch/m/rsj/mp3_128" },
    { "KEXP",                  "SEATTLE",    "USA",         "ENGLISH",   "http://kexp.example/stream.mp3" },
    { "RADIO KRISHNALOKA",     "MAYAPUR",    "INDIA",       "SANSKRIT",  "http://krishnaloka.example/stream.mp3" },
};
#define STATION_COUNT ((int)(sizeof(s_stations) / sizeof(s_stations[0])))

static int s_selected = 0; // index into s_stations
static int s_scroll_top = 0; // index of first visible row

static void draw_row(int visual_row, int station_index, bool selected)
{
    int y = Y_LIST_START + visual_row * ROW_HEIGHT;
    uint16_t fg = selected ? LCD_COLOR_BLACK : LCD_COLOR_WHITE;
    uint16_t bg = selected ? LCD_COLOR_CYAN  : LCD_COLOR_BLACK;
    lcd_fill_rect(0, y - 3, LCD_WIDTH, ROW_HEIGHT, bg);
    ui_draw_field(UI_MARGIN_X, y, UI_FIELD_MAX_WIDTH, 1,
                  s_stations[station_index].name, fg, bg);
}

static void redraw_visible(void)
{
    for (int row = 0; row < VISIBLE_ROWS; row++) {
        int idx = s_scroll_top + row;
        if (idx >= STATION_COUNT) {
            // Blank rows past the end so a smaller list doesn't leave
            // stale entries from a previous longer list. (Not possible
            // today with a static array, but cheap and future-proof.)
            int y = Y_LIST_START + row * ROW_HEIGHT;
            lcd_fill_rect(0, y - 3, LCD_WIDTH, ROW_HEIGHT, LCD_COLOR_BLACK);
            continue;
        }
        draw_row(row, idx, idx == s_selected);
    }
}

static void stations_enter(void)
{
    // Clamp scroll_top so the selected row is visible; guards against a
    // list that shrank between visits (again, not possible today).
    if (s_selected < s_scroll_top) s_scroll_top = s_selected;
    if (s_selected >= s_scroll_top + VISIBLE_ROWS) {
        s_scroll_top = s_selected - VISIBLE_ROWS + 1;
    }

    ui_draw_field_centered(UI_MARGIN_X, Y_TITLE, UI_FIELD_MAX_WIDTH, 2,
                           "STATIONS", LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
    redraw_visible();
}

// Moves selection by `delta`. If it goes off-screen, adjust the scroll
// window and redraw everything visible; otherwise just repaint the two
// affected rows for a flicker-free move.
static void move_selection(int delta)
{
    int prev = s_selected;
    int next = s_selected + delta;
    if (next < 0) next = STATION_COUNT - 1;
    if (next >= STATION_COUNT) next = 0;
    s_selected = next;

    // Bring the newly selected row into the visible window if needed.
    if (s_selected < s_scroll_top) {
        s_scroll_top = s_selected;
        redraw_visible();
        return;
    }
    if (s_selected >= s_scroll_top + VISIBLE_ROWS) {
        s_scroll_top = s_selected - VISIBLE_ROWS + 1;
        redraw_visible();
        return;
    }

    // No scroll: just repaint the two rows that flipped highlight state.
    int prev_row = prev - s_scroll_top;
    int cur_row  = s_selected - s_scroll_top;
    draw_row(prev_row, prev, false);
    draw_row(cur_row,  s_selected, true);
}

static void activate_selection(void)
{
    const station_t *s = &s_stations[s_selected];
    // Wire the new station to the display. Real stream-switching (stop
    // http_stream, reset audio_pipe, restart with s->url) is a follow-up
    // -- everything below the UI layer needs a controlled restart path
    // first, and that's a larger change than the UI work here.
    now_playing_set_station(s->name, s->city, s->country, s->language);
    now_playing_set_track("UNKNOWN TITLE", "UNKNOWN ARTIST");
    // REPLACE (not two POPs) so we don't flash the settings menu on the
    // way through. REPLACE tears down the whole stack in one step and
    // reinstalls now_playing as the sole entry.
    ui_screen_replace(&now_playing_screen);
}

static void stations_handle(const ui_event_t *evt)
{
    switch (evt->type) {
    case UI_EVT_ROTATE_CW:    move_selection(+1); break;
    case UI_EVT_ROTATE_CCW:   move_selection(-1); break;
    case UI_EVT_BUTTON_SHORT: activate_selection(); break;
    case UI_EVT_BUTTON_LONG:  ui_screen_pop(); break; // back to menu
    default: break;
    }
}

const screen_t stations_screen = {
    .enter        = stations_enter,
    .exit         = NULL,
    .handle_event = stations_handle,
};
