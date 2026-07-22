# World Radio API

A tiny, dependency-free proxy + cache in front of [radio-browser.info](https://www.radio-browser.info), serving a small fixed-schema JSON that's easy to consume from both the browser prototype and the ESP32-S3 firmware.

## Why this exists

We need a custom wrapper around whatever sources we use to pull radio data (in this case, radio-browser), both for the sake of the ESP32 to easily pull data and streams *knowing* that the stream is healthy and payload is small, and so we can eventually customize the behaviors of streams as needed later.

## Endpoints

Some basic initial endpoints:

| Route | Description |
|---|---|
| `GET /api/stations` | Full cached station list (c. 150 stations, deduped by rounded lon/lat) |
| `GET /api/stations?near=lon,lat&limit=20` | Nearest N stations to a point |
| `GET /api/stations/:id` | Single station by `stationuuid` |
| `GET /api/health` | Cache status, reporting count, age, last error |

Example response shape (one station):

```json
{
  "id": "9617a958-...",
  "name": "KQED",
  "city": "San Francisco",
  "country": "US",
  "lon": -122.42,
  "lat": 37.77,
  "genre": "public",
  "url": "https://streams.kqed.org/kqedradio",
  "bitrate": 128,
  "codec": "MP3",
  "ok": true
}
```

## Local development

```bash
cd api
npm install
npm start       # or: npm run dev for it to auto-restart on file change
```

## Deploying to Render

My intention is to eventually deploy this API to Render. Right now it is just for local experimentation.

## Wiring up the ESP32 firmware

We will have to point the firmware at our deployed Render URL:

```cpp
http.begin("https://world-radio-api.onrender.com/api/stations");
```

Render terminates TLS, so the ESP32 talks plain HTTPS to a stable hostname. Use `WiFiClientSecure` with `setInsecure()` or a pinned root CA if we want to be stricter. It may be fruitful later to create a naked HTTP endpoint for fetching stations so that the ESP32 can free up RAM (since it doesn't need TLS anymore).