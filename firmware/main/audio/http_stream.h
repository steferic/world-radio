#pragma once

#include <stdatomic.h>

extern atomic_uint_fast32_t g_http_bytes_read_total;

// FreeRTOS task: picks a random MP3 station from the world-radio API
// (STATION_API_RANDOM_URL), opens it, and pushes the raw MP3 byte stream
// into the audio_pipe ring buffer. On any drop/error it reconnects with
// exponential backoff, forever, re-picking a new random station each time
// so a dead stream doesn't get retried indefinitely.
void http_stream_task(void *pvParameters);

// Requests that the fetch task tear down its current stream and pick a
// fresh random station on its next iteration. Safe to call from any task,
// this just sets an atomic flag and returns immediately.
void http_stream_shuffle(void);
