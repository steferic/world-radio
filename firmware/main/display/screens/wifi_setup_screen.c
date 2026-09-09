#include "wifi_setup_screen.h"
#include "config.h"

#include "display/ui_draw.h"
#include "display/ui_task.h"
#include "display/lcd_driver.h"

#define Y_TITLE     12
#define Y_SSID_LBL  70
#define Y_SSID_VAL  92
#define Y_NOTE_1    140
#define Y_NOTE_2    160
#define Y_HINT      210

static void wifi_setup_enter(void)
{
    ui_draw_field_centered(UI_MARGIN_X, Y_TITLE, UI_FIELD_MAX_WIDTH, 2,
                           "WI-FI SETUP", LCD_COLOR_YELLOW, LCD_COLOR_BLACK);

    ui_draw_field(UI_MARGIN_X, Y_SSID_LBL, UI_FIELD_MAX_WIDTH, 1,
                  "CURRENT SSID:", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
    ui_draw_field(UI_MARGIN_X, Y_SSID_VAL, UI_FIELD_MAX_WIDTH, 2,
                  WIFI_SSID, LCD_COLOR_WHITE, LCD_COLOR_BLACK);

    ui_draw_field_centered(UI_MARGIN_X, Y_NOTE_1, UI_FIELD_MAX_WIDTH, 1,
                           "CREDENTIALS ARE CURRENTLY", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
    ui_draw_field_centered(UI_MARGIN_X, Y_NOTE_2, UI_FIELD_MAX_WIDTH, 1,
                           "COMPILED IN FROM CONFIG.H", LCD_COLOR_GRAY, LCD_COLOR_BLACK);

    ui_draw_field_centered(UI_MARGIN_X, Y_HINT, UI_FIELD_MAX_WIDTH, 1,
                           "PRESS BUTTON TO GO BACK", LCD_COLOR_CYAN, LCD_COLOR_BLACK);
}

static void wifi_setup_handle(const ui_event_t *evt)
{
    switch (evt->type) {
    case UI_EVT_BUTTON_SHORT:
    case UI_EVT_BUTTON_LONG:
        ui_screen_pop();
        break;
    default:
        break;
    }
}

const screen_t wifi_setup_screen = {
    .enter        = wifi_setup_enter,
    .exit         = NULL,
    .handle_event = wifi_setup_handle,
};
