# HTTPS sanity check

Minimal ESP-IDF project that answers one question: **does HTTPS work on this board at all?**

Boots, connects to Wi-Fi (credentials copied from MVP config), issues a single
HTTPS GET to `https://httpbin.org/get`, logs the result, and exits `app_main`.

## Why this exists

When HTTPS is hanging or crashing in the main firmware, it's easy to lose an
afternoon guessing whether the fault is TLS, the server, mbedTLS config, a
task stack, or something specific to the app. This project isolates the
network stack: if it prints `PASS`, HTTPS works and the fault lives in MVP.
If it prints `FAIL` or hangs, HTTPS is broken at the board / config level
and the MVP-side debugging is pointless.

`httpbin.org` sits behind Cloudflare, same as the Render-hosted station API,
so this exercises the same TLS profile.

## Build and run

From `firmware/C/SanityChecks/HTTPS/`:

```
idf.py set-target esp32s3
idf.py build flash monitor
```

## What success looks like

```
I (nnnn) https_sanity: connecting to '...'
I (nnnn) https_sanity: got ip: 192.168.x.y
I (nnnn) https_sanity: GET https://httpbin.org/get
I (nnnn) https_sanity: status=200 content-length=...
I (nnnn) https_sanity: PASS: HTTPS works on this board.
I (nnnn) https_sanity: done. app_main returning.
```

## Notes

- `sdkconfig.defaults` assumes an N16R8 (Octal PSRAM). For an N8R2, change
  `CONFIG_SPIRAM_MODE_OCT=y` to `CONFIG_SPIRAM_MODE_QUAD=y` (and adjust the
  flash size in menuconfig if you care about the extra space).
- Wi-Fi credentials are hardcoded to match MVP's `config.h`. If those change,
  edit `main/main.c`.
- No PSRAM? `CONFIG_SPIRAM_IGNORE_NOTFOUND=y` means the app boots anyway.
