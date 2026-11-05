# Third-party notices

## JUCE

Lotus is built with [JUCE](https://juce.com), which is dual-licensed under the
AGPLv3 and the commercial JUCE licence. Distributing Lotus binaries requires
complying with one of those licences.

## clap-juce-extensions / CLAP SDK

The CLAP build uses [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions)
(MIT licence) and the [CLAP SDK](https://github.com/free-audio/clap) (MIT licence),
fetched at build time.

## ares (SNES S-DSP Gaussian table construction)

`Source/dsp/Consoles.cpp` contains the routine that generates the SNES S-DSP
Gaussian interpolation table, adapted from ares (`ares/sfc/dsp/gaussian.cpp`):

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

## psx-spx (PS1 SPU Gaussian table)

The PS1 SPU Gaussian interpolation table values are taken from the
[psx-spx](https://psx-spx.consoledev.net/) hardware documentation
("Sound Processing Unit (SPU)" → "4-Point Gaussian Interpolation"). The PS1
reverb algorithm, its 39-tap resampling filter and the reverb preset register
values are from the same document ("SPU Reverb Formula", "SPU Reverb Examples").
