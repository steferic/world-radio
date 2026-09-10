# world-radio

ESP32-S3 + ESP-IDF 5.5: fetches an internet radio stream over HTTP, decodes
it (MP3 via [minimp3](https://github.com/lieff/minimp3), AAC via
[libhelix-aac](https://github.com/ultraembedded/libhelix-aac)), and plays it
out over I2S to a MAX98357A amp.

## Layout

```
CMakeLists.txt
sdkconfig.defaults
main/
  config.h              <-- edit this: Wi-Fi, stream URL, GPIO pins
  main.c                app_main: bring-up + wires the two tasks together
  network/
    wifi_connect.c/h    station-mode Wi-Fi with auto-reconnect
    station_api.c/h     hits the world-radio API for a random station
  audio/
    audio_pipe.c/h      byte ring buffer between the two tasks below
    http_stream.c/h     task: HTTP GET the stream, push bytes into audio_pipe
    audio_player.c/h    task: pull bytes, pick a decoder, feed audio_output
    audio_decoder.c/h   abstract decoder interface + format sniffer
    mp3_decoder.c/h     decoder impl: minimp3
    aac_decoder.c/h     decoder impl: libhelix-aac (stub if not vendored)
    audio_output.c/h    I2S setup, sample-rate reconfig, noise mixer
components/
  minimp3/              vendored, CC0 (public domain)
  helix-aac/            wrapper for libhelix-aac -- see its README.md
```

## Before building

Edit `main/config.h`:

- `WIFI_SSID` / `WIFI_PASS`
- `STREAM_URL` — a direct audio stream URL (see note below on ICY metadata)
- `I2S_BCLK_GPIO`, `I2S_WS_GPIO`, `I2S_DOUT_GPIO` — wire these to the
  MAX98357A's BCLK, LRC, and DIN pins respectively
- `I2S_SD_GPIO` — optional, drives the amp's SD (shutdown) pin so it starts
  muted and gets enabled by firmware. Set to `-1` if you've tied SD directly
  to a rail on your board instead.

For AAC support, vendor the Helix AAC sources under
`components/helix-aac/` as its README.md describes. Without them the build
still succeeds and MP3 works normally; AAC-labelled streams are just
rejected at runtime.

## Build & flash

```
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## How it's wired together

Two FreeRTOS tasks, pinned to separate cores, talk through a byte ring
buffer (`audio_pipe`):

- **Core 0**: `http_stream_task` opens the HTTP connection and reads raw
  audio bytes as fast as the server sends them, pushing each chunk into the
  ring buffer. On every new stream it also tells `audio_player` the format
  (from the API's `format` field) so the right decoder is picked up front.
  It reconnects with exponential backoff (1s → 10s) on any drop.
- **Core 1**: `audio_player`'s decode task pulls bytes out, hands them to
  the active decoder (mp3 or aac), and passes the resulting PCM to
  `audio_output` which writes to I2S. If the format hint is missing or the
  stream turns out to be something else, the task sniffs the first bytes
  for an MPEG audio sync word / ADTS marker and picks a decoder from that.
  The `i2s_channel_write()` call blocks until the DMA has room, which is
  what paces the whole pipeline to real-time playback speed — the decode
  loop naturally only pulls as much audio as is being played.

The I2S clock is reconfigured on the fly (`i2s_channel_reconfig_std_clock`)
if the stream's actual sample rate differs from the 44.1 kHz default, so it
adapts to whatever the source encodes at. Mono streams get duplicated into
both channels before writing, since the I2S side is always configured for
stereo slots.

## Adding a new codec

The decoder interface lives in `main/audio/audio_decoder.h`. Any new codec
plugs in with three things:

1. Implement the vtable in `main/audio/<codec>_decoder.c` (see
   `mp3_decoder.c` for the minimum shape).
2. Add its label to `audio_format_t` and to `audio_format_from_string()` /
   `audio_format_to_string()`.
3. Add a case in `audio_player.c`'s `decoder_for()`.

If the codec has a distinctive sync word, extend `audio_format_sniff()` so
streams without a format hint still land on the right decoder. Otherwise
just rely on the API's `format` label.

## Things worth knowing about

- **ICY metadata**: Icecast/Shoutcast servers only interleave `StreamTitle=`
  metadata blocks into the byte stream if the client sends
  `Icy-MetaData: 1`. This code doesn't send that header on purpose, so a
  normal Icecast mountpoint should hand back clean, uninterrupted audio.
  If your specific stream still injects metadata, `http_stream.c` would need
  a stage that parses and strips the `StreamTitle` blocks before they hit
  the ring buffer.
- **Ring buffer size**: `AUDIO_RINGBUF_BYTES` (default 512 KB) is your
  jitter budget — roughly a few seconds of audio at typical internet-radio
  bitrates. Bump it if you're on a flaky link (cellular, etc.) and can
  spare the RAM.
- **Resync on garbage**: if the decoder can't find a valid frame header
  after filling its whole staging buffer, it drops half of it and tries
  again rather than stalling — handles the occasional corrupt/non-audio
  byte run without wedging the pipeline. `AUDIO_DECODE_ERROR` from a
  decoder drops one byte and lets it search for the next sync boundary.
- **minimp3 license**: CC0 (public domain), vendored verbatim in
  `components/minimp3/include/minimp3.h` — no attribution required, but the
  original LICENSE file is included alongside it anyway.
- **libhelix-aac license**: RCSL / RPSL / GPL tri-license from RealNetworks.
  Not vendored in this repo -- see `components/helix-aac/README.md` for
  where to fetch a snapshot.
