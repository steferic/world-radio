# Porting the globe to ESP32-S3

The browser prototype was written so the rendering is a straight port. This doc is
the bridge from `src/projection.js` to firmware: the bill of materials, the library
choices, and exactly how each piece moves onto the chip.

## Bill of materials

| Part | Recommendation | Notes |
|---|---|---|
| **MCU** | ESP32-S3 with **PSRAM** — e.g. `ESP32-S3-DevKitC-1 N16R8` (16 MB flash / 8 MB PSRAM), or Seeed XIAO ESP32-S3 | PSRAM is non-negotiable: audio decode buffers + framebuffer + WiFi stack. The S3's FPU makes the projection math cheap. |
| **Display** | Waveshare **4.2″ B&W e-ink**, 400×300, SPI (SSD1683/UC8176) | Square 1.54″ 200×200 is the most globe-like if you want compact. Both do partial refresh. |
| **Audio DAC/amp** | **MAX98357A** (I2S, mono, built-in 3W amp) → small speaker | Or PCM5102 for line-out. Dead-simple wiring, 3 signal pins. |
| **Input** | Rotary encoder with push (e.g. EC11) | Rotate = tune between stations; push = play/stop. Optional 1–2 buttons. |
| **Power** | LiPo + TP4056 charger (if portable) | E-ink holds its image at **zero power**, so idle/standby battery life is excellent. |

Everything else (WiFi antenna) is on the S3 module.

## Software stack

- **Framework:** Arduino-ESP32 (fastest path — the libraries below are first-class
  there). ESP-IDF works too if you prefer.
- **E-ink driver:** [`GxEPD2`](https://github.com/ZinggJM/GxEPD2) — supports the
  Waveshare panels and, crucially, **partial refresh**.
- **Audio streaming:** [`ESP32-audioI2S`](https://github.com/schreibfaul1/ESP32-audioI2S)
  — `Audio.connecttohost("http://stream...")` handles Icecast/Shoutcast MP3/AAC and
  pushes straight to I2S. Internet-radio-on-ESP32 is a solved, well-documented problem.
- **JSON (for live stations):** `ArduinoJson` to parse radio-browser.info responses.

## How each prototype piece maps to the chip

### Framebuffer (`projection.js` → `projection.c`)
- JS uses one byte per pixel (`Uint8Array`). On the chip, use GxEPD2's native
  **packed 1bpp buffer** (`width*height/8` bytes; 400×300 = **15 KB**).
- `setPixel(x,y,color)` becomes a bit set/clear in that packed buffer (or just call
  `display.drawPixel(x,y, color ? GxEPD_BLACK : GxEPD_WHITE)`).
- `drawLine`, `fillCircle`, `strokeCircle`, the Bayer dither, and **`project()` /
  `makeCenter()` / `renderGlobe()` port verbatim** — they're already pure number
  crunching. Use `float` (the S3 has hardware FP) or fixed-point if you want to shave
  cycles; it's not the bottleneck.

### Coastline + land data
- Convert `public/coastline.geojson` → a C header of scaled integers once, at build
  time. ~5,128 points × (lon,lat) as `int16_t` (degrees × 100) ≈ **20 KB** in flash —
  trivial. A 15-line Node script emits `coastline.h` as `const int16_t[]` with
  per-polyline length prefixes. (`public/coastline.js` already does this transform to
  JS; the C header is the same data.)
- For the **filled** styles, also bake `public/land.js` → `land.h` (landmasses +
  per-landmass bbox, another ~20 KB). `fillLand` / `unproject` / `pointInFeature` in
  `projection.js` port unchanged — the bbox reject keeps the per-pixel point-in-polygon
  cheap. **Prefer the filled or wireframe styles on real hardware**: the prototype's
  e-ink simulation shows the shaded (full-disc dither) style ghosts badly under partial
  refresh, while solid fills keep the ocean clean and refresh cleanly.

### Display loop (the snap model pays off here)
- Render the globe into the 1bpp buffer, then `display.display(true)` for a **partial
  refresh** (~0.3–0.5 s on the 4.2″). Do a **full refresh** (~2 s) every N moves to
  clear ghosting.
- Because navigation is discrete snaps, the panel only refreshes *when you turn the
  encoder* — the rest of the time the CPU is free for audio.

### Audio
- WiFi connect → `audio.connecttohost(station.url)` → I2S → MAX98357A → speaker.
- Runs continuously while the e-ink sits static. This synergy (slow display + steady
  audio) is exactly why ESP32-S3 + e-ink is a good fit.

### Stations
- Ship the curated table (`stations.js`) as a C struct array in flash.
- Optionally fetch radio-browser.info over HTTPS at boot, parse with ArduinoJson,
  cache to **NVS** so you're not hitting the network every power-on.

### Input
- Rotary encoder ISR increments/decrements `selectedStation`; ease `currentCenter`
  toward the target lon/lat over a few partial refreshes (or snap in one). Encoder
  push toggles `audio.pauseResume()`.

## Memory budget (sanity check)

| Item | Size |
|---|---|
| 1bpp framebuffer (400×300) | 15 KB |
| Coastline table | ~20 KB |
| Station table | < 5 KB |
| Audio decode + ring buffers | hundreds of KB → **PSRAM** |

Comfortable on an S3 with 8 MB PSRAM. This is why the BOM insists on the `R8` part.

## Suggested bring-up order

1. `GxEPD2` "hello world" on the panel — confirm wiring + refresh.
2. Drop in ported `projection.c` + `coastline.h`; render **one static globe frame**.
   This single step proves the entire visual port.
3. Add the rotary encoder + partial-refresh navigation between stations.
4. Add WiFi + `ESP32-audioI2S` streaming of the selected station.
5. Add radio-browser fetch + NVS caching.
6. Enclosure + battery.
