#include "now_playing_screen.h"
#include "menu_screen.h"

#include "display/ui_draw.h"
#include "display/ui_task.h"
#include "display/lcd_driver.h"

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Layout, tuned for a 240 (h) x 320 (w) landscape panel. Keep the
// vertical arrangement roughly the same as the previous display_ui to
// preserve muscle memory for anyone glancing at the hardware.
#define Y_HEADER        8
#define Y_STATION_NAME  30
#define Y_STATION_SUB   62
#define Y_STATION_LANG  80
#define Y_DIVIDER       104
#define Y_TRACK_LABEL   118
#define Y_TRACK_TITLE1  140
#define Y_TRACK_TITLE2  164
#define Y_TRACK_ARTIST  196

#define TRACK_LINE_MAX_CHARS 18

// Shared state guarded by s_state_mutex. Setters copy into these buffers
// on their calling task; the UI task reads them (also under the mutex)
// when redrawing. This is the only shared-state locking in the whole UI
// system -- everything else runs on the UI task.
static SemaphoreHandle_t s_state_mutex;
static char s_station_name[48];
static char s_station_city[32];
static char s_station_country[32];
static char s_station_language[32];
static char s_track_title[64];
static char s_track_artist[48];

// Redraws the station block from the current state. Called from the UI
// task only; takes the mutex briefly to copy into stack buffers, then
// releases before doing SPI (which is slower than the copies).
static void draw_station_block(void)
{
    char name[sizeof(s_station_name)];
    char city[sizeof(s_station_city)];
    char country[sizeof(s_station_country)];
    char lang[sizeof(s_station_language)];

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    memcpy(name, s_station_name, sizeof(name));
    memcpy(city, s_station_city, sizeof(city));
    memcpy(country, s_station_country, sizeof(country));
    memcpy(lang, s_station_language, sizeof(lang));
    xSemaphoreGive(s_state_mutex);

    char sub[80];
    bool has_city = city[0] != '\0';
    bool has_country = country[0] != '\0';
    if (has_city || has_country) {
        snprintf(sub, sizeof(sub), "%s%s%s",
                 has_city ? city : "",
                 (has_city && has_country) ? ", " : "",
                 has_country ? country : "");
    } else {
        sub[0] = '\0';
    }

    ui_draw_field(UI_MARGIN_X, Y_STATION_NAME, UI_FIELD_MAX_WIDTH, 2,
                  name, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
    ui_draw_field(UI_MARGIN_X, Y_STATION_SUB, UI_FIELD_MAX_WIDTH, 1,
                  sub, LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
    ui_draw_field(UI_MARGIN_X, Y_STATION_LANG, UI_FIELD_MAX_WIDTH, 1,
                  lang, LCD_COLOR_GRAY, LCD_COLOR_BLACK);
}

static void draw_track_block(void)
{
    char title[sizeof(s_track_title)];
    char artist[sizeof(s_track_artist)];

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    memcpy(title, s_track_title, sizeof(title));
    memcpy(artist, s_track_artist, sizeof(artist));
    xSemaphoreGive(s_state_mutex);

    char line1[TRACK_LINE_MAX_CHARS + 1];
    char line2[TRACK_LINE_MAX_CHARS + 1];
    ui_split_two_lines(title, line1, sizeof(line1), line2, sizeof(line2),
                       TRACK_LINE_MAX_CHARS);

    ui_draw_field(UI_MARGIN_X, Y_TRACK_TITLE1, UI_FIELD_MAX_WIDTH, 2,
                  line1, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
    ui_draw_field(UI_MARGIN_X, Y_TRACK_TITLE2, UI_FIELD_MAX_WIDTH, 2,
                  line2, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
    ui_draw_field(UI_MARGIN_X, Y_TRACK_ARTIST, UI_FIELD_MAX_WIDTH, 1,
                  artist, LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
}

static void now_playing_enter(void)
{
    // The mutex is lazily created here rather than in main because the
    // screen module has no init function of its own -- and it's safe:
    // enter() runs on the UI task before any input handler can arrive.
    if (s_state_mutex == NULL) {
        s_state_mutex = xSemaphoreCreateMutex();
    }

    // Static chrome.
    ui_draw_field(UI_MARGIN_X, Y_HEADER, UI_FIELD_MAX_WIDTH, 1,
                  "WORLD RADIO", LCD_COLOR_CYAN, LCD_COLOR_BLACK);
    lcd_fill_rect(UI_MARGIN_X, Y_DIVIDER, UI_FIELD_MAX_WIDTH, 1, LCD_COLOR_GRAY);
    ui_draw_field(UI_MARGIN_X, Y_TRACK_LABEL, UI_FIELD_MAX_WIDTH, 1,
                  "NOW PLAYING:", LCD_COLOR_GRAY, LCD_COLOR_BLACK);

    // Dynamic blocks from stored state.
    draw_station_block();
    draw_track_block();
}

static void now_playing_handle(const ui_event_t *evt)
{
    switch (evt->type) {
    case UI_EVT_METADATA_CHANGED:
        // Something updated the station or track. We could get fancy and
        // track which changed, but redrawing both is <100 SPI bytes and
        // metadata events are rare.
        draw_station_block();
        draw_track_block();
        break;

    case UI_EVT_BUTTON_SHORT:
        // Enter the menu. PUSH (not REPLACE) so the menu can pop back
        // here without us having to re-fetch anything.
        ui_screen_push(&menu_screen);
        break;

    default:
        break;
    }
}

const screen_t now_playing_screen = {
    .enter        = now_playing_enter,
    .exit         = NULL,
    .handle_event = now_playing_handle,
};

// --- Public setters ---

void now_playing_set_station(const char *name, const char *city,
                             const char *country, const char *language)
{
    if (s_state_mutex == NULL) {
        // enter() creates it; if a setter fires before the screen has
        // been entered once, allocate here too. Two-mutex-create race
        // is fine because both paths only fire pre-multitasking (during
        // app_main setup).
        s_state_mutex = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    snprintf(s_station_name,     sizeof(s_station_name),     "%s", name     ? name     : "");
    snprintf(s_station_city,     sizeof(s_station_city),     "%s", city     ? city     : "");
    snprintf(s_station_country,  sizeof(s_station_country),  "%s", country  ? country  : "");
    snprintf(s_station_language, sizeof(s_station_language), "%s", language ? language : "");
    xSemaphoreGive(s_state_mutex);

    ui_event_t evt = { .type = UI_EVT_METADATA_CHANGED, .screen = NULL };
    ui_post(&evt);
}

void now_playing_set_track(const char *title, const char *artist)
{
    if (s_state_mutex == NULL) {
        s_state_mutex = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    snprintf(s_track_title,  sizeof(s_track_title),  "%s", title  ? title  : "");
    snprintf(s_track_artist, sizeof(s_track_artist), "%s", artist ? artist : "");
    xSemaphoreGive(s_state_mutex);

    ui_event_t evt = { .type = UI_EVT_METADATA_CHANGED, .screen = NULL };
    ui_post(&evt);
}
