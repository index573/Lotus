# Third-party notices

## JUCE

Lotus is built with [JUCE](https://juce.com), which is dual-licensed under the AGPLv3 and the commercial JUCE licence. Distributing Lotus binaries means complying with one of those licences.

## clap-juce-extensions and the CLAP SDK

The CLAP build uses [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) and the [CLAP SDK](https://github.com/free-audio/clap), both MIT-licensed and fetched at build time.

## FL Studio SDK

The FL Studio edition is built against `fp_plugclass.h` and `fp_def.h` from Image-Line's FL Studio plugin SDK, in `Source/fl/sdk/` (see the README there for the one Linux-only change). Image-Line provides the SDK for making native FL Studio plugins; the headers remain Image-Line's. Facts about how FL fills in voice levels (pitch relative to the root key, velocity, colours) were checked against the SDK documentation and the [orchard-studio](https://github.com/coderroleggg/orchard-studio) research notes. No code was copied from the latter.

## ares (SNES Gaussian table)

`Source/dsp/Consoles.cpp` generates the S-DSP Gaussian interpolation table with a routine adapted from ares (`ares/sfc/dsp/gaussian.cpp`):

```
Copyright (c) 2004-2025 ares team, Near et al

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
```

The BRR decoder in `Source/dsp/Codecs.cpp` follows the integer steps of ares' `brr.cpp`.

## psx-spx (PS1 tables and reverb)

The SPU Gaussian table, the reverb algorithm, its 39-tap resampling filter, the reverb preset registers and the SPU-ADPCM filter coefficients come from the [psx-spx](https://psx-spx.consoledev.net/) hardware documentation.

## mupen64plus-rsp-hle (N64 resampler table)

The 64×4 RSP resampler coefficient table in `Source/dsp/Consoles.cpp` is the `RESAMPLE_LUT` table from [mupen64plus-rsp-hle](https://github.com/mupen64plus/mupen64plus-rsp-hle) (`src/audio.c`, Copyright (C) 2014 Bobby Smiles, GPL-2.0-or-later). It reproduces constants used by Nintendo's audio microcode.

## N64 effect presets

The five N64 effect presets (section delays, coefficients, chorus rate/depth and filter values) and the effect-bus algorithm follow libultra, as documented by the [ultralib](https://github.com/decompals/ultralib) decompilation (`src/audio/drvrnew.c`, `src/audio/reverb.c`). Lotus contains its own implementation; only the numeric preset values are reproduced.

## Hardware constants

These figures were checked against the following projects and documents. No code was copied from them:

- Amiga Paula clock, minimum period and the A500/A1200/LED filter component values: [pt2-clone](https://github.com/8bitbubsy/pt2-clone) (`pt2_paula.c`).
- YM2612 DAC ladder offsets: [Genesis Plus GX](https://github.com/ekeeke/Genesis-Plus-GX) (`core/sound/ym2612.c`). Mega Drive model 1 low-pass (10 kΩ / 5600 pF, 2842 Hz): [clownmdemu](https://github.com/Clownacy/clownmdemu-core).
- NES DMC rate table and APU output filters: the [NESdev wiki](https://www.nesdev.org/wiki/APU_DMC).
- SCSP/AICA interpolation and pitch format: [MAME](https://github.com/mamedev/mame) (`scsp.cpp`, `aica.cpp`).
- DS timer clock and mixer rate: GBATEK.

## Pitch detection

The tuning check uses the YIN algorithm described in A. de Cheveigné and H. Kawahara, "YIN, a fundamental frequency estimator for speech and music", *Journal of the Acoustical Society of America* 111(4), 2002. Lotus contains its own implementation.
