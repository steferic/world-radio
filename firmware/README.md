# World Radio firmware (Arduino IDE)

The ESP32-S3 firmware as an Arduino sketch: open `WorldRadio/WorldRadio.ino` in the
Arduino IDE and it compiles + flashes the board for you.

It's the C++ port of the browser prototype's projection core — a color globe on the
2.4" LCD, rotary-encoder tuning with ease-to-station animation, push-to-play internet
radio out the I2S amp. Station + coastline data are baked in (`coastline.h`,
`stations.h`, generated from the repo's data).

## Hardware

| Part | Notes |
|---|---|
| ESP32-S3-DevKitC-1 **N16R8** | 16 MB flash / 8 MB octal PSRAM |
| Waveshare 2.4" LCD (ILI9341, SPI) | pins in `config.h` |
| MAX98357A I2S amp + 4 Ω speaker | pins in `config.h` |
| EC11 rotary encoder w/ push | rotate = tune · push = play/stop |

Full wiring rationale: `../firmware-notes/ESP32-PORTING.md`.

## Arduino IDE setup (once)

1. **Install the IDE:** [arduino.cc/en/software](https://www.arduino.cc/en/software) (IDE 2.x).
2. **ESP32 board support:** *Boards Manager* → search **esp32** → install
   **"esp32 by Espressif Systems"** (3.x).
3. **Libraries:**
   - *Library Manager* → install **LovyanGFX** (by lovyan03).
   - **ESP32-audioI2S** is not in the Library Manager — download the ZIP from
     [github.com/schreibfaul1/ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S)
     (*Code → Download ZIP*, or a release ZIP), then *Sketch → Include Library →
     Add .ZIP Library…*
     - If you hit a compile error mentioning `dsps_biquad…`, your esp32 core and the
       library version disagree — try the latest **release** ZIP instead of master
       (or update the esp32 core to the newest 3.x). The library tracks recent cores.

## Build & flash

1. Open `WorldRadio/WorldRadio.ino`.
2. Edit **`config.h`**: your WiFi SSID/password (and the pin map if you wired differently).
3. *Tools* menu:
   - **Board:** ESP32S3 Dev Module
   - **PSRAM:** OPI PSRAM  ← important (N16R8 is octal PSRAM)
   - **Flash Size:** 16MB
   - **Partition Scheme:** 16M Flash (3MB APP/9.9MB FATFS)  ← important — the
     firmware is ~2 MB, bigger than the default 1.25 MB app partition
   - **USB CDC On Boot:** Enabled (so `Serial` prints over the USB port)
   - **Port:** the board's USB port
4. **Upload** (→ arrow). The IDE compiles the binary and flashes it.
5. *Tools → Serial Monitor* (115200) for logs.

## What you should see

Boot → globe renders (wireframe coastlines + graticule + station markers) → "wifi ok"
in the status line → rotate the knob to swing the globe between stations → press the
knob to play the selected station through the speaker. Stations without a stream URL
show "no stream for this pin" (5 of the 30 curated pins are playable; the live-station
fetch is a later milestone).
