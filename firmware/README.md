# world-radio-mp3

ESP32-S3 + ESP-IDF 5.5: fetches an MP3 stream over HTTP, decodes it with
[minimp3](https://github.com/lieff/minimp3), and plays it out over I2S to a
MAX98357A amp.

## Layout

```
CMakeLists.txt
sdkconfig.defaults
main/
  config.h          <-- edit this: Wi-Fi, stream URL, GPIO pins
  main.c            app_main: bring-up + wires the two tasks together
  wifi_connect.c/h   station-mode Wi-Fi with auto-reconnect
  audio_pipe.c/h     byte ring buffer between the two tasks below
  http_stream.c/h    task: HTTP GET the stream, push bytes into audio_pipe
  mp3_player.c/h     task: pull bytes, decode with minimp3, write I2S
components/minimp3/
  include/minimp3.h  vendored verbatim from lieff/minimp3 (CC0 / public domain)
  minimp3_impl.c      the one TU that instantiates the header's implementation
```

## Before building

Edit `main/config.h`:

- `WIFI_SSID` / `WIFI_PASS`
- `STREAM_URL` — a direct MP3 stream URL (see note below on ICY metadata)
- `I2S_BCLK_GPIO`, `I2S_WS_GPIO`, `I2S_DOUT_GPIO` — wire these to the
  MAX98357A's BCLK, LRC, and DIN pins respectively
- `I2S_SD_GPIO` — optional, drives the amp's SD (shutdown) pin so it starts
  muted and gets enabled by firmware. Set to `-1` if you've tied SD directly
  to a rail on your board instead.

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

- **Core 0**: `http_stream_task` opens the HTTP connection and reads raw MP3
  bytes as fast as the server sends them, pushing each chunk into the ring
  buffer. It reconnects with exponential backoff (1s → 10s) on any drop.
- **Core 1**: `mp3_player`'s decode task pulls bytes out, hands them to
  `mp3dec_decode_frame()`, and writes the resulting PCM to I2S. The
  `i2s_channel_write()` call blocks until the DMA has room, which is what
  paces the whole pipeline to real-time playback speed — the decode loop
  naturally only pulls as much audio as is being played.

The I2S clock is reconfigured on the fly (`i2s_channel_reconfig_std_clock`)
if the stream's actual sample rate differs from the 44.1 kHz default, so it
adapts to whatever the source encodes at. Mono streams get duplicated into
both channels before writing, since the I2S side is always configured for
stereo slots.

## Things worth knowing about

- **ICY metadata**: Icecast/Shoutcast servers only interleave `StreamTitle=`
  metadata blocks into the byte stream if the client sends
  `Icy-MetaData: 1`. This code doesn't send that header on purpose, so a
  normal Icecast mountpoint should hand back clean, uninterrupted MP3 bytes.
  If your specific stream still injects metadata, `http_stream.c` would need
  a stage that parses and strips the `StreamTitle` blocks before they hit
  the ring buffer.
- **Ring buffer size**: `AUDIO_RINGBUF_BYTES` (default 64 KB) is your jitter
  budget — roughly a few seconds of audio at typical internet-radio
  bitrates. Bump it if you're on a flaky link (cellular, etc.) and can
  spare the RAM.
- **Resync on garbage**: if the decoder can't find a valid frame header
  after filling its whole staging buffer, it drops half of it and tries
  again rather than stalling — handles the occasional corrupt/non-MP3 byte
  run without wedging the pipeline.
- **minimp3 license**: CC0 (public domain), vendored verbatim in
  `components/minimp3/include/minimp3.h` — no attribution required, but the
  original LICENSE file is included alongside it anyway.
