#include "icy_demuxer.h"
#include "audio_pipe.h"
#include "display/screens/now_playing_screen.h"

#include <stdbool.h>
#include <string.h>
#include <stdatomic.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "icy";

// Match the timeout the http_stream read loop was using before the demuxer
// slotted in, so backpressure behavior is unchanged.
#define AUDIO_WRITE_TIMEOUT_MS 2000

void icy_demuxer_init(icy_demuxer_t *d, size_t metaint)
{
    memset(d, 0, sizeof(*d));
    d->metaint = metaint;
    d->state = ICY_STATE_AUDIO;
    d->audio_remaining = metaint;
}

static void forward_audio(const uint8_t *buf, size_t len)
{
    if (len == 0) return;
    if (!audio_pipe_write(buf, len, pdMS_TO_TICKS(AUDIO_WRITE_TIMEOUT_MS))) {
        atomic_fetch_add(&g_audio_pipe_write_drops, 1);
    }
}

// Copy up to `dst_cap-1` chars from [src, src+src_len), stripping non-printable
// and non-ASCII bytes down to '?'. Result is NUL-terminated. Returns strlen.
static size_t copy_ascii_scrubbed(char *dst, size_t dst_cap,
                                  const char *src, size_t src_len)
{
    if (dst_cap == 0) return 0;
    size_t o = 0;
    for (size_t i = 0; i < src_len && o + 1 < dst_cap; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == 0) break;
        // Keep printable ASCII plus space; replace anything else (control
        // chars, high-bit Latin-1/UTF-8 bytes) with a '?' so the display
        // doesn't render mojibake.
        if (c >= 0x20 && c < 0x7F) {
            dst[o++] = (char)c;
        } else {
            dst[o++] = '?';
        }
    }
    dst[o] = '\0';
    // Trim trailing spaces/? which are usually padding artifacts.
    while (o > 0 && (dst[o - 1] == ' ' || dst[o - 1] == '?')) {
        dst[--o] = '\0';
    }
    return o;
}

// Parse the accumulated d->meta_buf (d->meta_fill bytes). Extracts the value
// of StreamTitle=... (accepts ', ", or unquoted), and dispatches to
// now_playing_set_track. If the value contains " - " it's split into
// Artist / Title; otherwise the whole string is shown as the title and the
// artist line is left blank. Dedups against d->last_title so identical
// repeated updates don't churn the UI.
static void parse_and_dispatch(icy_demuxer_t *d)
{
    if (d->meta_fill == 0) return;

    // One-time raw dump of the first non-empty metadata block, so you can
    // see on the serial monitor exactly what the server is sending -- keys,
    // quoting, separators, character encoding. Guarded by a module-level
    // static so it survives icy_demuxer_init()'s memset of the struct and
    // fires only once per boot rather than once per reconnect.
    static bool s_first_meta_dumped = false;
    if (!s_first_meta_dumped) {
        s_first_meta_dumped = true;
        ESP_LOGI(TAG, "=== first ICY metadata block (%u bytes) ===",
                 (unsigned)d->meta_fill);
        ESP_LOG_BUFFER_HEXDUMP(TAG, d->meta_buf, d->meta_fill, ESP_LOG_INFO);
        // Also print as a single printable-ASCII line (non-printables shown
        // as '.') so the eye can find the interesting bytes without scanning
        // hex. Bounded copy: the meta block can be up to ~4 KB but for the
        // one-shot debug view a page is plenty.
        char printable[512];
        size_t n = d->meta_fill < sizeof(printable) - 1
                     ? d->meta_fill : sizeof(printable) - 1;
        for (size_t j = 0; j < n; j++) {
            unsigned char c = d->meta_buf[j];
            printable[j] = (c >= 0x20 && c < 0x7F) ? (char)c : '.';
        }
        printable[n] = '\0';
        ESP_LOGI(TAG, "=== as ASCII: %s ===", printable);
    }

    // Find "StreamTitle=" -- case-sensitive because every server we've seen
    // sends it exactly like this. The value's quote char varies (', ", or
    // occasionally absent), so we grab the key without a trailing quote and
    // detect the quoting style below.
    static const char KEY[] = "StreamTitle=";
    const size_t key_len = sizeof(KEY) - 1;
    if (d->meta_fill < key_len) return;

    const uint8_t *p = d->meta_buf;
    size_t remaining = d->meta_fill;
    const uint8_t *found = NULL;
    while (remaining >= key_len) {
        if (memcmp(p, KEY, key_len) == 0) { found = p; break; }
        p++;
        remaining--;
    }
    if (found == NULL) return;

    const char *val      = (const char *)(found + key_len);
    const char *scan_end = (const char *)(d->meta_buf + d->meta_fill);

    // Detect the quoting style. Canonical Shoutcast is `'...'`; a handful of
    // Icecast forks emit `"..."`; a few non-standard servers send bare
    // values that end at the next `;`.
    char quote = 0;
    if (val < scan_end && (*val == '\'' || *val == '"')) {
        quote = *val;
        val++;
    }

    // Terminator scan. Quoted values end at `quote` + `;` (or `quote` at
    // end-of-buffer for malformed blocks that got padded with NULs before
    // the `;`). Unquoted values end at the next `;`. Titles do occasionally
    // contain a bare quote character, so a lone `'` or `"` without a
    // following `;` doesn't count as a terminator.
    const char *end = val;
    while (end < scan_end) {
        if (quote) {
            if ((uint8_t)*end == (uint8_t)quote) {
                if (end + 1 >= scan_end || end[1] == ';') break;
            }
        } else {
            if (*end == ';') break;
        }
        end++;
    }
    size_t title_len = (size_t)(end - val);

    // Scrub into a working buffer, then dedup against last_title.
    char title_raw[128];
    copy_ascii_scrubbed(title_raw, sizeof(title_raw), val, title_len);
    if (title_raw[0] == '\0') {
        // Empty StreamTitle -- common between tracks and on stations that
        // don't publish track info. Leave whatever's already on the display
        // instead of clobbering it with placeholder text.
        return;
    }

    if (strcmp(title_raw, d->last_title) == 0) return;
    snprintf(d->last_title, sizeof(d->last_title), "%s", title_raw);

    // Try to split on " - " into "Artist - Title". Shoutcast convention is
    // artist-first; a handful of stations reverse it, but there's no
    // reliable signal, so pick one and live with the occasional swap. If
    // there's no separator (station-name-only, DJ-set titles, etc.) show
    // the whole string as the title with "-" on the artist line -- matches
    // the placeholder shown before any StreamTitle arrives, so the display
    // reads consistently rather than flipping between a blank and a dash.
    const char *sep = strstr(title_raw, " - ");
    const char *artist = "-";
    const char *title  = title_raw;
    char artist_buf[128];
    if (sep != NULL) {
        size_t alen = (size_t)(sep - title_raw);
        if (alen >= sizeof(artist_buf)) alen = sizeof(artist_buf) - 1;
        memcpy(artist_buf, title_raw, alen);
        artist_buf[alen] = '\0';
        artist = artist_buf;
        title  = sep + 3; // skip " - "
    }

    ESP_LOGI(TAG, "track: '%s' - '%s'", artist, title);
    now_playing_set_track(title, artist);
}

void icy_demuxer_feed(icy_demuxer_t *d, const uint8_t *in, size_t len)
{
    // Passthrough: no interleave, just push everything to the audio pipe.
    if (d->metaint == 0) {
        forward_audio(in, len);
        return;
    }

    size_t i = 0;
    while (i < len) {
        switch (d->state) {

        case ICY_STATE_AUDIO: {
            size_t chunk = len - i;
            if (chunk > d->audio_remaining) chunk = d->audio_remaining;
            forward_audio(in + i, chunk);
            i += chunk;
            d->audio_remaining -= chunk;
            if (d->audio_remaining == 0) {
                d->state = ICY_STATE_META_LEN;
            }
            break;
        }

        case ICY_STATE_META_LEN: {
            uint8_t L = in[i++];
            // One-shot log for the first metadata-length byte the demuxer
            // sees: this confirms bytes are actually being fed and that the
            // metaint window closed correctly. If you never see this line,
            // the demuxer is either not being fed at all or is stuck in
            // passthrough (metaint == 0).
            static bool s_first_L_logged = false;
            if (!s_first_L_logged) {
                s_first_L_logged = true;
                ESP_LOGI(TAG, "first ICY length byte: L=%u (%u bytes of metadata)",
                         (unsigned)L, (unsigned)(L * 16));
            }
            d->meta_remaining = (size_t)L * 16;
            d->meta_fill = 0;
            if (d->meta_remaining == 0) {
                // No metadata this cycle. Straight back to audio.
                d->audio_remaining = d->metaint;
                d->state = ICY_STATE_AUDIO;
            } else {
                d->state = ICY_STATE_META_BODY;
            }
            break;
        }

        case ICY_STATE_META_BODY: {
            size_t chunk = len - i;
            if (chunk > d->meta_remaining) chunk = d->meta_remaining;

            // meta_buf is sized for the maximum possible (255*16) block so
            // this bound check is defensive: a corrupt L byte can't ever
            // overflow the buffer, but we keep it explicit.
            size_t space = sizeof(d->meta_buf) - d->meta_fill;
            size_t copy  = chunk < space ? chunk : space;
            if (copy > 0) {
                memcpy(d->meta_buf + d->meta_fill, in + i, copy);
                d->meta_fill += copy;
            }
            i += chunk;
            d->meta_remaining -= chunk;

            if (d->meta_remaining == 0) {
                parse_and_dispatch(d);
                d->audio_remaining = d->metaint;
                d->state = ICY_STATE_AUDIO;
            }
            break;
        }
        }
    }
}
