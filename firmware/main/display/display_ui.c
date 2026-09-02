#include "display_ui.h"
#include "lcd_driver.h"
#include "font5x7.h"
#include "config.h"

#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define GLYPH_CELL_W (FONT5X7_WIDTH + 1) // +1 for a 1px gap between characters
#define GLYPH_CELL_H (FONT5X7_HEIGHT)

#define MARGIN_X          8
#define FIELD_MAX_WIDTH   (LCD_WIDTH - 2 * MARGIN_X)

// Layout, tuned for a 240x320 portrait panel. Adjust these if you rotate
// the display via LCD's MADCTL setting in config.h/lcd_driver.c.
#define Y_HEADER         8
#define Y_STATION_NAME   30
#define Y_STATION_SUB    62
#define Y_STATION_LANG   80
#define Y_DIVIDER        104
#define Y_TRACK_LABEL    118
#define Y_TRACK_TITLE1   140
#define Y_TRACK_TITLE2   164
#define Y_TRACK_ARTIST   196

// Max characters that fit on one line at 2x scale, used for wrapping the
// track title (see FIELD_MAX_WIDTH / (GLYPH_CELL_W * 2)).
#define TRACK_LINE_MAX_CHARS 18

static SemaphoreHandle_t s_ui_mutex;

// Renders one glyph at (x,y) top-left, at the given integer pixel scale,
// in `color` on `bg`. Builds the scaled bitmap in a small stack buffer and
// blits it in a single SPI transaction.
static void draw_glyph(int x, int y, char ch, uint16_t color, uint16_t bg, int scale)
{
    uint8_t cols[FONT5X7_WIDTH];
    font5x7_lookup(ch, cols);

    int w = FONT5X7_WIDTH * scale;
    int h = FONT5X7_HEIGHT * scale;
    // Sized for the largest scale this module actually uses (2x): 10x14.
    uint16_t buf[FONT5X7_WIDTH * 2 * FONT5X7_HEIGHT * 2];

    for (int row = 0; row < h; row++) {
        int font_row = row / scale;
        for (int col = 0; col < w; col++) {
            int font_col = col / scale;
            int lit = (cols[font_col] >> font_row) & 0x1;
            buf[row * w + col] = lit ? color : bg;
        }
    }

    lcd_draw_bitmap(x, y, w, h, buf);
}

// Clears a fixed-width field to `bg`, then draws `text` (clipped to
// max_width, truncated with ".." if it doesn't fit) at the given scale.
// Always clearing the full field width first is what makes this safe for
// partial updates -- otherwise replacing "Long Previous Title" with "Hi"
// would leave the tail of the old text on screen.
static void draw_field(int x, int y, int max_width, int scale, const char *text, uint16_t color, uint16_t bg)
{
    int cell_w = GLYPH_CELL_W * scale;
    int line_h = GLYPH_CELL_H * scale;

    lcd_fill_rect(x, y, max_width, line_h, bg);

    if (text == NULL || text[0] == '\0') {
        return;
    }

    int max_chars = max_width / cell_w;
    if (max_chars <= 0) {
        return;
    }

    char buf[64];
    size_t len = strlen(text);
    if ((int)len > max_chars) {
        int keep = (max_chars > 2) ? (max_chars - 2) : max_chars;
        snprintf(buf, sizeof(buf), "%.*s..", keep, text);
    } else {
        snprintf(buf, sizeof(buf), "%s", text);
    }

    int cx = x;
    for (const char *p = buf; *p != '\0'; p++) {
        draw_glyph(cx, y, *p, color, bg, scale);
        cx += cell_w;
    }
}

// Splits `s` across two lines of at most `max_chars` each, breaking on the
// last space at or before the limit so words aren't cut mid-word. If `s`
// fits on one line, line2 is left empty. If the remainder after the break
// still doesn't fit on line2, it's truncated there (draw_field would also
// catch this, but doing it here keeps the wrap point sensible).
static void split_two_lines(const char *s, char *l1, size_t l1_cap, char *l2, size_t l2_cap, size_t max_chars)
{
    l1[0] = '\0';
    l2[0] = '\0';

    size_t total = strlen(s);
    if (total <= max_chars) {
        snprintf(l1, l1_cap, "%s", s);
        return;
    }

    size_t split = max_chars;
    while (split > 0 && s[split] != ' ') {
        split--;
    }
    if (split == 0) {
        split = max_chars; // no space to break on -- hard-break instead
    }

    snprintf(l1, l1_cap, "%.*s", (int)split, s);

    const char *rest = s + split;
    while (*rest == ' ') {
        rest++;
    }

    size_t rest_len = strlen(rest);
    if (rest_len <= max_chars) {
        snprintf(l2, l2_cap, "%s", rest);
    } else {
        int keep = (max_chars > 2) ? (int)(max_chars - 2) : (int)max_chars;
        snprintf(l2, l2_cap, "%.*s..", keep, rest);
    }
}

esp_err_t display_ui_init(void)
{
    s_ui_mutex = xSemaphoreCreateMutex();
    if (s_ui_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = lcd_driver_init();
    if (err != ESP_OK) {
        return err;
    }

    xSemaphoreTake(s_ui_mutex, portMAX_DELAY);
    draw_field(MARGIN_X, Y_HEADER, FIELD_MAX_WIDTH, 1, "WORLD RADIO", LCD_COLOR_CYAN, LCD_COLOR_BLACK);
    lcd_fill_rect(MARGIN_X, Y_DIVIDER, FIELD_MAX_WIDTH, 1, LCD_COLOR_GRAY);
    draw_field(MARGIN_X, Y_TRACK_LABEL, FIELD_MAX_WIDTH, 1, "NOW PLAYING:", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
    xSemaphoreGive(s_ui_mutex);

    return ESP_OK;
}

void display_ui_set_station(const char *name, const char *city, const char *country, const char *language)
{
    char sub[64];
    bool has_city = city && city[0] != '\0';
    bool has_country = country && country[0] != '\0';
    if (has_city || has_country) {
        snprintf(sub, sizeof(sub), "%s%s%s",
                 has_city ? city : "",
                 (has_city && has_country) ? ", " : "",
                 has_country ? country : "");
    } else {
        sub[0] = '\0';
    }

    xSemaphoreTake(s_ui_mutex, portMAX_DELAY);
    draw_field(MARGIN_X, Y_STATION_NAME, FIELD_MAX_WIDTH, 2, name, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
    draw_field(MARGIN_X, Y_STATION_SUB, FIELD_MAX_WIDTH, 1, sub, LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
    draw_field(MARGIN_X, Y_STATION_LANG, FIELD_MAX_WIDTH, 1, language, LCD_COLOR_GRAY, LCD_COLOR_BLACK);
    xSemaphoreGive(s_ui_mutex);
}

void display_ui_set_track(const char *title, const char *artist)
{
    char line1[TRACK_LINE_MAX_CHARS + 1];
    char line2[TRACK_LINE_MAX_CHARS + 1];
    split_two_lines(title ? title : "", line1, sizeof(line1), line2, sizeof(line2), TRACK_LINE_MAX_CHARS);

    xSemaphoreTake(s_ui_mutex, portMAX_DELAY);
    draw_field(MARGIN_X, Y_TRACK_TITLE1, FIELD_MAX_WIDTH, 2, line1, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
    draw_field(MARGIN_X, Y_TRACK_TITLE2, FIELD_MAX_WIDTH, 2, line2, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
    draw_field(MARGIN_X, Y_TRACK_ARTIST, FIELD_MAX_WIDTH, 1, artist, LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
    xSemaphoreGive(s_ui_mutex);
}
