# World Radio — e-ink globe

A hardware "world radio": a black-and-white e-ink globe you spin between internet
radio stations, in the spirit of [Radio Garden](https://radio.garden). This repo
starts with the **visualization** — a browser prototype of the globe + station
markers + snap navigation — written so the render code ports almost line-for-line
to an **ESP32-S3** driving a B&W e-ink panel.

> A terminal spin-off grew out of this prototype — a braille-rendered globe you can
> run in your shell — and now lives in its own repo, **terminal-radio**.

## Why a browser prototype first

The globe is just math writing into a 1-bit framebuffer. That framebuffer is
identical whether it lives in a `<canvas>` or in the ESP32's RAM. So we prove the
*look and feel* at web-iteration speed, then port the ~200 lines of pure render
code to C. See [`firmware-notes/ESP32-PORTING.md`](firmware-notes/ESP32-PORTING.md).

## Run it

```bash
cd world-radio
npm run dev          # or: node server.mjs
# open http://localhost:4173
```

No build step, no dependencies. The coastline data is vendored (`public/coastline.geojson`,
Natural Earth 110m).

## Controls

| Action | Input |
|---|---|
| Next / previous station | `→` / `←` (or `[` `]`, or the Prev/Next buttons) |
| Select a station | click its marker on the globe, or a row in the sidebar |
| Play / stop stream | `Space` or the ▶/■ button |
| Full refresh (clear ghosting) | `F` or the **Full refresh** button |
| Change panel size | the **Panel** dropdown (4.2″, 1.54″ square, 2.9″, 7.5″) |
| Render style | the **Render style** dropdown (see below) |
| Graticule | checkbox |
| **Simulate e-ink refresh** | checkbox — snap navigation + partial/full refresh + **ghosting** |
| Load real geolocated stations | **Load live stations** (pulls from radio-browser.info) |

## Render styles (and why they matter for e-ink)

A B&W e-ink panel can only do black and white, and **dense dithered regions ghost
badly under partial refresh.** The render styles let you weigh look vs. e-ink
behavior — turn on *Simulate e-ink refresh* and navigate a few times to feel it:

| Style | Look | E-ink behavior |
|---|---|---|
| **Filled land** (default) | Solid black continents, white ocean | Best — ocean stays clean, minimal ghosting |
| **Gray land** | 50% dithered continents | Good — dither only over land, ocean clean |
| **Wireframe** | Coastline outlines only | Cleanest — fewest black pixels |
| **Shaded sphere** | Lit 3D sphere via full-disc dither | Prettiest, but **ghosts heavily** — full-refresh only |

The land fill uses an **inverse-orthographic projection + point-in-polygon** test
per disc pixel (`fillLand` / `unproject` in `projection.js`) — it ports to the chip
unchanged and clips at the limb for free.

## The e-ink simulation (what to look for)

With *Simulate e-ink refresh* on, the prototype models how the real panel behaves so
you can make design calls **before buying hardware**:

- **Partial refresh** (most moves): fast (~0.3s), no flash — but leaves a faint ghost
  where pixels went black→white. Ghosts accumulate.
- **Full refresh** (every 6th move, or press `F`): the black/white flash (~2s) that
  clears all ghosting.

Try **Shaded** vs **Filled** in e-ink mode and watch the difference — this is the #1
thing to validate on real glass, and it strongly favors the filled/wire styles.

## Design decisions

- **1-bit, pixel-faithful.** Everything renders at the panel's *native* resolution
  (e.g. 400×300), is hard-thresholded to pure black/white (even the text), then
  upscaled nearest-neighbor so what you see on screen is what the e-ink shows.
- **Discrete snap navigation.** E-ink can't do smooth rotation; instead you snap
  station-to-station, one partial refresh per move — a vintage shortwave-dial feel
  that *matches* the hardware's strengths and frees the CPU for audio during playback.
- **Sphere shading via ordered dither.** A 4×4 Bayer matrix fakes a lit sphere on a
  panel that only has two colors.

## Project layout

```
world-radio/
├── index.html              # device shell + controls
├── styles.css
├── server.mjs              # tiny dependency-free static server
├── public/
│   ├── coastline.geojson   # vendored Natural Earth 110m coastlines (source)
│   ├── coastline.js        # inlined coastlines (window.COASTLINE)
│   ├── land.geojson        # vendored Natural Earth 110m land polygons (source)
│   └── land.js             # inlined landmasses + bboxes (window.LAND)
├── src/
│   ├── projection.js       # PORTABLE renderer — DOM-free, maps to ESP32 C
│   ├── stations.js         # curated global station table
│   └── app.js              # browser glue: canvas, animation, audio, input, e-ink sim
└── firmware-notes/
    └── ESP32-PORTING.md    # bill of materials + the port plan
```

The one file that matters for hardware is [`src/projection.js`](src/projection.js):
keep it DOM-free and dependency-free. Everything browser-specific lives in `app.js`
and gets replaced on the chip by the e-ink driver, a rotary encoder, and I2S audio.

## Data sources

- **Geography:** [Natural Earth](https://www.naturalearthdata.com/) 110m coastlines.
- **Stations + streams:** [radio-browser.info](https://www.radio-browser.info) — free,
  open, ~50k stations, many with lat/long and direct stream URLs.

## Roadmap

1. ✅ Browser prototype of the globe (this).
2. Bring up e-ink: render one static globe frame from the ported `projection.c`.
3. Rotary-encoder navigation + partial refresh between stations.
4. WiFi + internet-radio streaming (ESP32-audioI2S → I2S DAC → speaker).
5. radio-browser fetch + on-device station cache.
6. Enclosure + battery.
