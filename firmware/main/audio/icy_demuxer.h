#pragma once

#include <stddef.h>
#include <stdint.h>

// Shoutcast/Icecast ICY metadata demuxer.
//
// If a stream was opened with "Icy-MetaData: 1" AND the server responded with
// an "icy-metaint: N" header, the response body is not pure audio -- every N
// audio bytes are followed by a single length byte L (0..255) and, if L > 0,
// L*16 bytes of ASCII metadata like:
//     StreamTitle='Artist - Title';StreamUrl='...';\0\0...
// padded with NULs to the next 16-byte boundary. Length byte L=0 means
// "no update this cycle".
//
// This demuxer walks a raw byte stream from the HTTP client, forwards the
// audio-only bytes to audio_pipe_write(), and dispatches complete metadata
// blocks to now_playing_set_track() after parsing StreamTitle.
//
// Initialize with metaint == 0 to disable interleave handling entirely -- the
// demuxer then becomes a thin passthrough that just forwards bytes to the
// audio pipe. That's the right mode when the server didn't send icy-metaint.

typedef enum {
    ICY_STATE_AUDIO,     // emitting the current audio block
    ICY_STATE_META_LEN,  // next byte is the length prefix
    ICY_STATE_META_BODY, // accumulating a metadata payload
} icy_state_t;

typedef struct {
    size_t      metaint;         // 0 => passthrough mode (no interleave)
    icy_state_t state;
    size_t      audio_remaining; // bytes of audio still to emit before next length byte
    size_t      meta_remaining;  // bytes of metadata still to consume
    size_t      meta_fill;       // bytes accumulated in meta_buf so far

    // L is a single byte, so the largest possible metadata block is 255*16 = 4080 bytes.
    uint8_t     meta_buf[16 * 255];

    // Last StreamTitle we dispatched, so we don't churn the display when the
    // server re-sends the same title on every interval. NUL-terminated.
    char        last_title[128];
} icy_demuxer_t;

// Prepare the demuxer for a new connection. Pass 0 as metaint when the server
// did not send an icy-metaint header (or the value was invalid).
void icy_demuxer_init(icy_demuxer_t *d, size_t metaint);

// Consume `len` bytes from the HTTP client. Audio bytes are forwarded to
// audio_pipe_write() (with the same 2s backpressure timeout the rest of the
// code uses); metadata blocks are parsed and dispatched to now_playing.
void icy_demuxer_feed(icy_demuxer_t *d, const uint8_t *in, size_t len);
