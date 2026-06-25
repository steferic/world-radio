# Porting the globe to ESP32-S3 (2.4" color SPI build)

The browser prototype was written so the rendering is a straight port. This doc bridges
`src/projection.js` to firmware. **Hardware direction (chosen): a small 2.4" color SPI
LCD driven by a separate ESP32-S3 dev board** — a cheap, simple-to-wire appliance with a
smooth color globe you spin between stations using a rotary knob.

> Earlier directions (1-bit B&W e-ink, then a 5–7" RGB all-in-one) are archived in git
> history. The projection core is identical across all of them — only the framebuffer
> and the display driver change.

## Bill of materials

| Part | Recommendation | Notes |
|---|---|---|
| **MCU board** | **ESP32-S3-DevKitC-1 N16R8** (16 MB flash / 8 MB PSRAM, USB-C) | Separate from the screen. Its high pin count is why display + audio + a knob all fit. |
| **Display** | **Waveshare 2.4" LCD Module** — ILI9341, 240×320, SPI, no touch | Screen-only; wires to the S3 over SPI. |
| **Audio amp** | **MAX98357A** (I2S, mono, 3 W, DAC built in) | Trivial here — SPI leaves plenty of free GPIO. |
| **Speaker** | **3" full-range 4 Ω driver in a small sealed enclosure** (Dayton Audio CE/ND) | The enclosure matters more than the chip. |
| **Input** | **Rotary encoder (EC11, with push)** | Tune between stations + push to play/stop. This panel has no touch. |
| **Power** | **USB-C 5 V** (on the DevKitC) | The amp can pull ~0.5–1 A at volume; don't undersize. |

## Wiring — no GPIO contention (the upside of SPI)

| Bus | Pins | Count |
|---|---|---|
| SPI LCD | SCK, MOSI, CS, DC, RST, BL (+ 3V3/GND) — MISO unused (display-only) | ~6 |
| I2S audio | BCLK, LRC, DIN | 3 |
| Encoder | A, B, SW | 3 |

~12 GPIO of the DevKitC's 30+ → fits comfortably. (This is exactly the pin pressure that
made the big RGB-parallel panel awkward; SPI sidesteps it.)

## Software stack

- **Framework:** Arduino-ESP32 (or ESP-IDF).
- **Display driver:** [`LovyanGFX`](https://github.com/lovyan03/LovyanGFX) or
  [`TFT_eSPI`](https://github.com/Bodmer/TFT_eSPI) for the ILI9341 (DMA SPI). Rust:
  [`mipidsi`](https://crates.io/crates/mipidsi).
- **Audio streaming:** [`ESP32-audioI2S`](https://github.com/schreibfaul1/ESP32-audioI2S)
  — `audio.connecttohost("http://stream...")` → I2S. Internet-radio-on-ESP32 is well-trodden.
- **JSON (live stations):** `ArduinoJson` for radio-browser.info.

## How the prototype maps to the chip

### Framebuffer (`projection.js` → C)
- Target is an **RGB565 framebuffer, 240×320 (~150 KB)** — small enough for internal RAM,
  pushed to the panel via **DMA SPI**. `setPixel` writes a `uint16_t`.
- `project()` / `makeCenter()` and the line/circle rasterizers **port verbatim**; colors
  come from your palette (filled land + coastlines + colored markers — like the
  `terminal-radio` color mode).

### Geo data — coarse on-device
- Bake **110m** coastline (~5 k points) to `coastline.h` (~20 KB). The 50m set is overkill
  for a 240 px globe and too heavy to redraw per frame on the S3.

### Display loop
- Globe as a ~**220 px circle** centered (or left, with a slim station strip). At this size
  the S3 can **ease the rotation smoothly** toward the selected station, then hold static —
  freeing the CPU for audio while a station plays.

### Audio
- WiFi → `audio.connecttohost(station.url)` → I2S → MAX98357A → speaker.

### Stations
- Ship a curated table in flash; optionally fetch radio-browser.info over HTTPS at boot
  (ArduinoJson), cache to **NVS**. (Reuse the verify idea from `terminal-radio`; a simple
  HTTP/byte probe on-device, since ffprobe isn't available.)

### Input
- Encoder ISR increments/decrements `selectedStation`; push toggles play/stop.

## Memory budget (8 MB PSRAM)

| Item | Size |
|---|---|
| RGB565 framebuffer (240×320) | ~150 KB |
| Coastline (110m) table | ~20 KB |
| Station table | < 10 KB |
| Audio decode + ring buffers | hundreds of KB → PSRAM |

## Suggested bring-up order

1. **LovyanGFX/TFT_eSPI "hello world"** on the 2.4" LCD — confirm SPI wiring.
2. Wire the **MAX98357A** and stream one hard-coded station (`ESP32-audioI2S`).
3. Port `projection.c` + `coastline.h`; draw **one static color globe frame**.
4. Add eased rotation + **encoder** navigation.
5. radio-browser fetch + NVS caching.
6. Enclosure + speaker box.
