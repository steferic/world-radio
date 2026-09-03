#pragma once

#include <stdatomic.h>

extern atomic_uint_fast32_t g_http_bytes_read_total;

// FreeRTOS task: connects to STREAM_URL, reads the
// raw MP3 byte stream, and pushes it into the audio_pipe ring buffer.
// Reconnects with exponential backoff on any drop/error, forever.
void http_stream_task(void *pvParameters);
