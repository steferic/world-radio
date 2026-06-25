# Porting the globe to ESP32-S3 (color IPS build)

The browser prototype was written so the rendering is a straight port. This doc is the
bridge from `src/projection.js` to firmware. **Hardware direction (chosen): a color
RGB IPS panel, iPhone-ish size (5–7"), on an all-in-one ESP32-S3 board** — a low-power,
instant-on appliance that shows the globe in color and eases (snaps + short animation)
between stations.

> Earlier this targeted a 1-bit B&W e-ink panel; that path is archived in git history.
> The projection core is identical — only the framebuffer (1bpp → RGB565) and the
> display driver change.

## Bill of materials

| Part | Recommendation | Notes |
|---|---|---|
| **MCU + display** | All-in-one **ESP32-S3 + 5–7″ 800×480 RGB IPS** board — e.g. Waveshare **ESP32-S3-Touch-LCD-5** (5″) or **-7** (7″); Elecrow **CrowPanel 5″/7″** | Integrates the RGB panel + ESP32-S3 (8 MB PSRAM / 16 MB flash) + capacitive touch on one board. 800×480 is the S3's practical color ceiling. |
| **Audio amp** | **MAX98357A** (I2S, mono, 3 W) → speaker — *or* a board with **onboard audio** | ⚠️ See the GPIO note below: RGB panels eat most GPIOs. Prefer a board with onboard I2S audio, or confirm ≥3 free I2S-capable pins. |
| **Speaker** | **3″ full-range 4 Ω driver in a small sealed enclosure** (Dayton Audio CE/ND) | The enclosure matters more than the chip. Bigger driver suits a 5–7″ device. |
| **Input** | **Capacitive touch** (built into the board) and/or a **rotary encoder** (EC11) | Touch the globe/list to navigate; an encoder is the "radio knob" feel. Encoder needs 2–3 free GPIOs. |
| **Power** | **USB-C 5 V** | RGB panel + backlight draws hundreds of mA, so this is a plugged-in appliance. Battery is possible but short-lived vs the old e-ink build. |

### ⚠️ The GPIO gotcha
A parallel-RGB panel consumes ~**16 data + 5 control GPIOs**, leaving few free on the
S3. Before buying, make sure the board either has **onboard I2S audio** (many HMI boards
do) or **breaks out ≥3 free GPIOs** for the MAX98357A (BCLK/LRCLK/DIN) — plus a couple
more if you add a rotary encoder. This is the #1 thing that trips up RGB-board audio.

## Software stack

- **Framework:** Arduino-ESP32 (these boards ship Arduino/ESP-IDF + LVGL examples and a
  board-support package). ESP-IDF works too.
- **Display + UI:** the board's **LVGL** BSP. Render the globe into an LVGL canvas / an
  RGB565 buffer; let LVGL handle the chrome (station list, now-playing, labels).
- **Audio streaming:** [`ESP32-audioI2S`](https://github.com/schreibfaul1/ESP32-audioI2S)
  — `audio.connecttohost("http://stream...")` handles Icecast/Shoutcast MP3/AAC straight
  to I2S. Internet-radio-on-ESP32 is well-trodden.
- **JSON (live stations):** `ArduinoJson` to parse radio-browser.info.

## How the prototype maps to the chip

### Framebuffer (`projection.js` → C)
- The render target is now an **RGB565 framebuffer in PSRAM** (800×480×2 = **768 KB**;
  double-buffer if you want tear-free updates → ~1.5 MB). `setPixel` writes a `uint16_t`.
- `project()` / `makeCenter()` and the line/circle rasterizers **port verbatim** (pure
  math; the S3 has hardware float). Colors come straight from your palette
  (coast/border/land/marker), same as the terminal app's color mode.

### Geo data — use the *coarse* set on-device
- Bake **110m** coastline (~5 k points) to a C header (`coastline.h`, ~20 KB), **not**
  the 50m set. At ~440 px the S3 can't redraw 60 k segments per frame; 110m keeps
  per-frame line drawing affordable.
- Optional filled land via a baked `land.h` + the `is_land` point-in-polygon test, or
  texture-map an equirectangular earth bitmap (per-pixel lookup) for photographic
  continents.

### Display loop (be realistic about framerate)
- Layout (landscape 800×480): globe as a ~**440 px circle on the left**, station list +
  now-playing + controls in the right ~340 px column — same as the browser/terminal.
- A full-screen procedural globe redraw is heavy on the S3, so **ease then rest**: animate
  the rotation toward the selected station over ~0.5 s at ~10–15 fps, then hold static.
  Keep the spinning render modest (smaller internal size / coarse data) and it feels good
  without needing 60 fps. CPU is then free for audio while resting.

### Audio
- WiFi → `audio.connecttohost(station.url)` → I2S → MAX98357A (or onboard amp) → speaker.
- Runs continuously while the globe is static (between navigations).

### Stations
- Ship a curated table in flash; optionally fetch radio-browser.info over HTTPS at boot
  (ArduinoJson) and cache to **NVS**. Reuse the verify idea from `terminal-radio` if you
  want only-working streams (an HTTP/byte probe; full ffprobe isn't available on-device).

## Memory budget (8 MB PSRAM)

| Item | Size |
|---|---|
| RGB565 framebuffer (800×480) | 768 KB (×2 if double-buffered) |
| Coastline (110m) table | ~20 KB |
| Station table | < 10 KB |
| Audio decode + ring buffers | hundreds of KB |

Comfortable on an 8 MB-PSRAM S3 — which the all-in-one boards have.

## Suggested bring-up order

1. Flash the board's **LVGL "hello world"** — confirm the panel + touch work.
2. Wire/confirm **audio** (onboard or MAX98357A) and stream one hard-coded station with
   `ESP32-audioI2S`. (Do this early — the GPIO gotcha is easier to catch now.)
3. Port `projection.c` + `coastline.h`; draw **one static color globe frame** into the
   RGB565 buffer. Proves the visual port.
4. Add eased rotation + station selection (touch and/or encoder).
5. Add radio-browser fetch + NVS caching.
6. Enclosure + speaker box.

## Open sub-choices
- **5″ vs 7″** panel (both 800×480 — same firmware, just enclosure size).
- **Touch vs rotary encoder vs both** for navigation.
