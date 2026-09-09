# MVP inputs sanity check

Minimal ESP-IDF project that answers one question: **is each MVP input wired
and configured correctly?**

Boots, arms the standalone pushbutton, the rotary encoder, the rotary
encoder's built-in pushbutton, and the volume potentiometer, and prints a
confirmation line every time it accepts an event. No display, no audio, no
networking.

## Why this exists

When the main firmware ignores a knob or a button, it's easy to lose an
afternoon guessing whether the fault is wiring, GPIO config, an ISR, a
FreeRTOS queue, or the UI task that owns the event. This project isolates the
input layer: if every input prints its confirmation here, the wiring and GPIO
config are fine and the bug lives in the MVP firmware. If an input stays
silent, the fault is at the board level and MVP-side debugging is pointless.

## Pin map (must match `firmware/main/config/gpio.h`)

| Input                       | GPIO   |
| --------------------------- | ------ |
| Standalone pushbutton       | GPIO21 |
| Rotary encoder A            | GPIO15 |
| Rotary encoder B            | GPIO16 |
| Rotary encoder pushbutton   | GPIO47 |
| Volume potentiometer        | GPIO4  (ADC1) |

All buttons are wired active-low against the internal pull-up. The pot's
wiper goes to GPIO4 with the outer legs on 3V3 and GND.

## Build and run

From `firmware-sanity-tests/Inputs/`:

```
idf.py set-target esp32s3
idf.py build flash monitor
```

## What success looks like

```
======================================================
           MVP INPUTS SANITY TEST
======================================================
  standalone pushbutton   : GPIO21
  rotary encoder A / B    : GPIO15 / GPIO16
  rotary encoder button   : GPIO47
  volume potentiometer    : GPIO4 (ADC1)
------------------------------------------------------
  ...
======================================================

I (nnnn) inputs_sanity: all inputs armed. Waiting for events...
[pushbutton]      GPIO21 PRESS
[pushbutton]      GPIO21 release
[rotary_encoder]  detent CW  (A=GPIO15 B=GPIO16)
[rotary_encoder]  detent CW  (A=GPIO15 B=GPIO16)
[rotary_encoder]  detent CCW (A=GPIO15 B=GPIO16)
[rotary_button]   GPIO47 PRESS
[rotary_button]   GPIO47 release
I (nnnn) inputs_sanity: [potentiometer]   GPIO4 raw=2048  (~50%)
I (nnnn) inputs_sanity: [potentiometer]   GPIO4 raw=3400  (~83%)
```

## Notes

- `sdkconfig.defaults` assumes an N16R8 (Octal PSRAM). For an N8R2, change
  `CONFIG_SPIRAM_MODE_OCT=y` to `CONFIG_SPIRAM_MODE_QUAD=y`. PSRAM isn't
  actually used by the test; it's enabled only so this project flashes onto
  the same boards MVP targets without menuconfig changes.
- The potentiometer only prints when the smoothed reading changes by more
  than `POT_LOG_DELTA` (out of 4095). A still knob is silent; sweep it to
  see log lines.
- The rotary encoder announces one line per detent (4 quadrature counts on
  a standard EC11). If a full turn produces no lines, one of A/B isn't
  reaching the pin -- check solder joints and the common leg.
- The pin map is copied from `firmware/main/config/gpio.h`. If those change,
  edit the `#define`s at the top of `main/main.c`.
