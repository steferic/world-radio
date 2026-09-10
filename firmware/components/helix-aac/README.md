# helix-aac

Vendored copy of the [RealNetworks Helix][helix-original] fixed-point HE-AAC
decoder, taken from [earlephilhower/ESP8266Audio][esp8266audio] which
tracks the Helix source with the minimum edits needed to compile in a
modern embedded-GCC environment.

## What's here

```
components/helix-aac/
├── CMakeLists.txt          -- registers the component, sets ARDUINO=1
├── README.md               -- this file
└── src/
    ├── aacdec.h            -- public API used by main/audio/aac_decoder.c
    ├── statname.h          -- (also public; internal name-mangling table)
    ├── aaccommon.h, coder.h, bitstream.h, sbr.h, assembly.h
    ├── {aacdec,aactabs,bitstream,buffers,dct4,decelmnt,dequant,fft,
    │    filefmt,huffmanaac,hufftabs,imdct,noiseless,pns,
    │    sbr,sbrfft,sbrfreq,sbrhfadj,sbrhfgen,sbrhuff,sbrimdct,
    │    sbrmath,sbrqmf,sbrside,sbrtabs,stproc,tns,trigtabs}.c
    ├── Arduino.h           -- shim (see below)
    └── pgmspace.h          -- shim (see below)
```

## Why the two shim headers

The ESP8266Audio fork inserted `#include <Arduino.h>` and
`#include <pgmspace.h>` into `aaccommon.h`. Nothing in the AAC decoder
actually uses Arduino runtime -- those includes just pull in stdint types
and the `PROGMEM` / `pgm_read_*` macros. Rather than patch the RealNetworks
source, we ship two tiny drop-in shims:

* `Arduino.h`   -- `#include`s stdint / stddef / stdlib / string, nothing else.
* `pgmspace.h`  -- collapses `PROGMEM` to nothing and `pgm_read_byte/word/dword`
                   to plain dereferences (safe on ESP32 -- `const` data lands
                   in flash and is reachable via normal loads).

Plus `-DARDUINO=1` in `CMakeLists.txt`, which:

* picks the generic-C 64-bit multiply/CLZ fallbacks in `assembly.h`
  (no Xtensa asm exists upstream, so this is the only path that works),
* lets `aacdec.h` accept the target platform without erroring out, and
* routes `buffers.c` / `sbr.c` through the standard `<stdlib.h>` /
  `<stdio.h>` instead of Helix's private `hlxclib/` variants.

## Updating

The upstream is at
`https://github.com/earlephilhower/ESP8266Audio/tree/master/src/libhelix-aac`.
To refresh, replace the contents of `src/` (except `Arduino.h` and
`pgmspace.h`, which are ours) with the current tree there.

## License

RealNetworks Public Source License (RPSL) / RealNetworks Community Source
License (RCSL). The per-file copyright block at the top of every `.c` and
`.h` in `src/` is authoritative. Full text of the RPSL is at
https://helixcommunity.org/content/rpsl.

By vendoring these files into this repository, the `helix-aac` component
carries the RPSL; the rest of the firmware is unaffected (RPSL is a
per-file license, and this component is the only place Helix code lives).

[helix-original]: https://helixcommunity.org/
[esp8266audio]:   https://github.com/earlephilhower/ESP8266Audio
