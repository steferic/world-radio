#include "ui_draw.h"

#include <string.h>
#include <stdio.h>

void ui_draw_glyph(int x, int y, char ch, uint16_t color, uint16_t bg, int scale)
{
    uint8_t cols[FONT5X7_WIDTH];
    font5x7_lookup(ch, cols);

    int w = FONT5X7_WIDTH * scale;
    int h = FONT5X7_HEIGHT * scale;

    // Sized for the largest scale the app actually uses (2x): 10x14.
    // Bumping to 3x would need FONT5X7_WIDTH*3 * FONT5X7_HEIGHT*3 = 315 words.
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

// Renders `text` glyph-by-glyph starting at (x, y), no clearing. Used by
// ui_draw_field / ui_draw_field_centered after they've cleared the field.
static void draw_text_run(int x, int y, const char *text,
                          uint16_t color, uint16_t bg, int scale)
{
    int cell_w = UI_CELL_W(scale);
    int cx = x;
    for (const char *p = text; *p != '\0'; p++) {
        ui_draw_glyph(cx, y, *p, color, bg, scale);
        cx += cell_w;
    }
}

// Truncates `text` to fit `max_chars` cells, appending ".." if truncated.
// Returns a pointer into `out`, which must be at least max_chars+1 bytes.
static const char *truncated(const char *text, char *out, size_t out_cap, int max_chars)
{
    size_t len = strlen(text);
    if ((int)len <= max_chars) {
        snprintf(out, out_cap, "%s", text);
    } else {
        int keep = (max_chars > 2) ? (max_chars - 2) : max_chars;
        snprintf(out, out_cap, "%.*s..", keep, text);
    }
    return out;
}

void ui_draw_field(int x, int y, int max_width, int scale,
                   const char *text, uint16_t color, uint16_t bg)
{
    int cell_w = UI_CELL_W(scale);
    int line_h = UI_CELL_H(scale);

    lcd_fill_rect(x, y, max_width, line_h, bg);

    if (text == NULL || text[0] == '\0') {
        return;
    }

    int max_chars = max_width / cell_w;
    if (max_chars <= 0) {
        return;
    }

    char buf[64];
    const char *s = truncated(text, buf, sizeof(buf), max_chars);
    draw_text_run(x, y, s, color, bg, scale);
}

void ui_draw_field_centered(int x, int y, int max_width, int scale,
                            const char *text, uint16_t color, uint16_t bg)
{
    int cell_w = UI_CELL_W(scale);
    int line_h = UI_CELL_H(scale);

    lcd_fill_rect(x, y, max_width, line_h, bg);

    if (text == NULL || text[0] == '\0') {
        return;
    }

    int max_chars = max_width / cell_w;
    if (max_chars <= 0) {
        return;
    }

    char buf[64];
    const char *s = truncated(text, buf, sizeof(buf), max_chars);

    // Center on the actual drawn width (which is one glyph_width per char
    // plus the trailing inter-char gap of the last cell -- we can just use
    // cell_w * len since draw_text_run advances by cell_w each glyph).
    int len = (int)strlen(s);
    int drawn_w = len * cell_w;
    int offset = (max_width - drawn_w) / 2;
    if (offset < 0) offset = 0;

    draw_text_run(x + offset, y, s, color, bg, scale);
}

void ui_split_two_lines(const char *s,
                        char *l1, size_t l1_cap,
                        char *l2, size_t l2_cap,
                        size_t max_chars)
{
    l1[0] = '\0';
    l2[0] = '\0';

    size_t total = strlen(s);
    if (total <= max_chars) {
        snprintf(l1, l1_cap, "%s", s);
        return;
    }

    // Walk back from max_chars to the last space so we break on a word.
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

void ui_draw_clear_screen(void)
{
    lcd_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, LCD_COLOR_BLACK);
}
