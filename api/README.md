# World Radio API

A tiny, dependency-free proxy + cache in front of [radio-browser.info](https://www.radio-browser.info), presenting the ESP32 with an **indexed list of stations** it can surf linearly (`random` → `next_id` → `next_id` → …) and enriching each station with a best-effort **now-playing** track scrape.

## Model

- The station corpus is fetched once every 10 minutes from radio-browser, deduped by rounded lon/lat, and sorted by UUID so integer IDs stay as stable as possible across refreshes.
- Each station has an integer `id` (its position in the list) plus a `uuid` (stable across the whole radio-browser universe). Either can be used to address it.
- Every single-station response includes `next_id` and `prev_id`, wrapping at the ends. The ESP just walks those; it never needs to know the total length or handle wraparound itself.
- Track name / artist come from **ICY metadata** scraped by opening the stream, reading past its first metadata block, and closing. Cached briefly per station. Streams without ICY (HLS, most OGG mounts) simply return `now_playing: null` — never blocks a station response for more than ~3.5s.

## Endpoints

| Route | Purpose |
|---|---|
| `GET /api/stations` | Full indexed list, slim rows (no stream URL, no now-playing). For browser prototype / debugging. Returns `{ total, stations: [...] }`. |
| `GET /api/stations?near=lon,lat&limit=20` | Nearest N slim stations to a point. `limit` clamped 1-100. |
| `GET /api/stations/random` | Random station, full shape (stream URL + nav + now-playing). This is how the ESP boots. |
| `GET /api/stations/:id` | Same shape as `/random`, but for a specific `id` (integer index) or `uuid`. This is how the ESP walks `next_id` / `prev_id`. |
| `GET /api/stations/:id/now-playing` | Just the track metadata for polling while a stream is already playing. |
| `GET /api/health` | `200` once the cache is warm, `503` before the first successful refresh. Reports count, age, last error. |

### Single-station response shape

```json
{
  "id": 42,
  "uuid": "9617a958-...",
  "name": "KQED",
  "location": "California, US",
  "country": "US",
  "region": "California",
  "lon": -122.42,
  "lat": 37.77,
  "genre": "public",
  "codec": "MP3",
  "bitrate": 128,
  "stream_url": "https://streams.kqed.org/kqedradio",
  "homepage": "https://www.kqed.org/",
  "prev_id": 41,
  "next_id": 43,
  "now_playing": {
    "title": "Morning Edition",
    "artist": "NPR"
  }
}
```

`now_playing` is `null` when the stream doesn't expose ICY metadata or the scrape timed out. `title` and/or `artist` individually may be `null` when the stream sent a `StreamTitle` that didn't follow the "Artist - Title" convention.

### Typical ESP32 surf loop

1. Boot: `GET /api/stations/random` → hold on to the response, start playing `stream_url`.
2. User rotates encoder forward: `GET /api/stations/{next_id}` → replace the held response.
3. User rotates encoder backward: `GET /api/stations/{prev_id}` → same.
4. While playing, poll `GET /api/stations/{id}/now-playing` every ~15s to update the "now playing" line on the LCD without re-fetching the station.

The ESP holds no station list, just the current response — the API drives navigation.

## Local development

```bash
cd api
npm start           # or: npm run dev for auto-restart on file change
```

Node 18.11+ required (uses `--watch`, `AbortSignal.timeout`, and Web Streams).

## Deployment (Render)

TLS terminates at Render, so the ESP32 can talk plain HTTPS to a stable hostname. Point the firmware at the deployed URL:

```c
#define STATION_API_URL_FMT "https://world-radio-api.onrender.com/api/stations/%d"
#define STATION_RANDOM_URL  "https://world-radio-api.onrender.com/api/stations/random"
```

If freeing TLS RAM on the ESP32 ever matters, a plain-HTTP subdomain is straightforward to add — the API itself has nothing sensitive to protect in transit.

## Design notes

- **Single-flight refresh.** Concurrent requests to a stale cache share one in-flight upstream fetch instead of stampeding radio-browser.
- **Upstream failover.** Cycles through `de1`/`at1`/`nl1` mirrors on connection failure (all three are canonical radio-browser hosts).
- **Serve stale on error.** If a refresh fails but we already have a cache, keep serving it; only propagate the error when the cache is empty.
- **Stable-ish IDs.** Stations are sorted by UUID before indexing, so a routine refresh where the top-clickcount ordering shifts doesn't shuffle every ESP client's cached `next_id` out from under it. Adding or removing a station shifts a small neighborhood, not the whole list.
- **No codec filter.** All radio-browser streams pass through — MP3, AAC, OGG, HLS — so the firmware can grow into more of them without an API change.
