#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

// A byte-oriented ring buffer that decouples the HTTP fetch task from the
// MP3 decode/I2S task, so a slow network read never directly stalls (or a
// slow I2S write never directly backs up) the other side.

// Debug/monitoring counters, incremented at the call sites that hit each
// failure mode (http_stream.c on a full-buffer drop, mp3_player.c on a
// zero-byte read). Read with atomic_load(); a monitor task can diff
// successive readings to get a per-interval rate. Not reset automatically.
extern atomic_uint_fast32_t g_audio_pipe_write_drops;
extern atomic_uint_fast32_t g_audio_pipe_read_underruns;
extern atomic_uint_fast32_t g_audio_pipe_resyncs;
extern atomic_uint_fast32_t g_audio_pipe_rate_changes;

esp_err_t audio_pipe_init(size_t capacity_bytes);

// Pushes `len` bytes into the pipe, blocking up to `timeout` ticks if it's
// full (this is the backpressure mechanism that keeps the HTTP task from
// running away with memory it doesn't have). Returns true on success.
bool audio_pipe_write(const uint8_t *data, size_t len, TickType_t timeout);

// Pulls up to `max_len` bytes out of the pipe into `out`, blocking up to
// `timeout` ticks for at least one byte to become available. Returns the
// number of bytes actually copied (0 on timeout).
size_t audio_pipe_read(uint8_t *out, size_t max_len, TickType_t timeout);

// Drops everything currently buffered. Used when (re)starting the stream so
// stale audio doesn't play after a reconnect.
void audio_pipe_reset(void);

// Current number of bytes buffered and the pipe's total capacity, for
// monitoring/debugging (e.g. logging fill percentage). This is a snapshot,
// not atomic with concurrent read/write, so don't rely on it for anything
// exact -- it's meant for trend-watching, not synchronization.
size_t audio_pipe_get_fill_bytes(void);
size_t audio_pipe_get_capacity_bytes(void);
