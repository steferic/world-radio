#pragma once

#include <stddef.h>
#include "esp_err.h"

// Metadata for a single station chosen by the API
typedef struct {
    char *stream_url;   // required; the audio URL to open
    char *name;         // station display name
    char *region;       // e.g. "California"
    char *country;      // ISO country code, e.g. "US"
    char *genre;        // first tag from radio-browser
} station_info_t;

#define STATION_API_SCRATCH_MIN_BYTES 4096

// Queries STATION_API_RANDOM_URL, parses the JSON, and (if it's an MP3 stream)
// fills `info` with pointers into the `scratch` buffer. Retries internally up to
// STATION_API_MAX_ATTEMPTS times.
//
// The raw HTTP body is written into 'scratch', and the 'info' struct points to
// the stream metadata wherever they happen to be inside 'scratch'. Those pointers
// remain valid in memory until the next time this function is called.
esp_err_t station_api_get_random_mp3(station_info_t *info, char *scratch, size_t scratch_len);
