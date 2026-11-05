# Lotus

A simple, uncluttered SoundFont (`.sf2`) player plugin for making video game music.

![Lotus](docs/screenshot.png)

- **One screen, no menus to dig through.** Load a SoundFont (or drag one onto the window), pick a preset, play.
- **An ADSR that actually does what it says.** Release = 0 means the note stops *now*. A 250 ms release lasts 250 ms. No hidden minimum, and no release tail baked into the file that you can't remove.
- **Cut mode.** One switch makes every note stop the instant its key is released, even if you're using the SoundFont's own envelopes. It works with the sustain pedal too: the cut happens when the pedal comes up.
- **Console resampling modes.** Play samples back the way the original hardware did: SNES Gaussian, DS sample-and-hold, GBA 8-bit, PS1 Gaussian, plus clean modern modes.

Formats: **VST3** and **Standalone** on Windows/macOS/Linux, plus **AU** on macOS and **LV2** on Linux.

---

## Controls

| Control | What it does |
| --- | --- |
| **Load SF2** / drag & drop | Load a `.sf2` file. The plugin remembers the file path in your project. |
| **Preset** `<` `>` | Choose a preset (shown as `bank:program  name`). MIDI program changes (and bank select CC0/CC32) switch presets too; in VST3 hosts these arrive through the host's program parameter. |
| **Attack / Decay / Sustain / Release** | The Lotus envelope. Double-click a knob to reset it, or click its value to type one in (`250 ms`, `1.5 s`, …). |
| **Note off: Release / Cut** | *Release* fades out over the release time. *Cut* silences the note immediately on key-up, with a 1 ms ramp to avoid a click. |
| **Envelope from: Lotus / SoundFont** | *Lotus* uses the knobs. *SoundFont* uses the envelope stored in the file, per sample, as the author intended. Cut mode still applies. |
| **Console** | Resampling/playback model (see below). |
| **Volume**, **Bend** | Output level, and pitch-bend range in semitones. |

MIDI: notes, velocity, pitch bend, sustain pedal (CC64), volume (CC7), pan (CC10), expression (CC11), bank select (CC0/32), program change, all-notes-off/all-sound-off. Lotus plays every MIDI channel (omni). For several instruments, use several instances.

## Console modes

Each mode runs the voices at the console's mixer rate using its interpolation method and its pitch precision. It then reconstructs that signal at your project's sample rate. You get the hardware's character (SNES muffle, DS/GBA aliasing and grit) without a filter or bitcrusher pasted over a clean render.

| Mode | Mixer rate | Interpolation | Pitch | Bit depth |
| --- | --- | --- | --- | --- |
| **Modern** | project rate | 4-point Hermite | exact | float |
| **Linear** | project rate | linear | exact | float |
| **Raw** | project rate | none | exact | float |
| **SNES** | 32 000 Hz | S-DSP 4-tap Gaussian (hardware table) | 14-bit, 4.12 fixed point; max `0x3FFF` (≈ 2 octaves up) | 16-bit |
| **Nintendo DS** | ≈ 32 728 Hz (33.51 MHz / 1024) | none | 16-bit timer (`16756991 / timer`) | 10-bit output |
| **GBA** | 13 379 Hz | linear | exact | 8-bit samples and output |
| **PS1** | 44 100 Hz | SPU 4-tap Gaussian (hardware table) | 4.12 fixed point; max `0x4000` | 16-bit |

How accurate these are:

- **SNES and PS1** use the real Gaussian coefficient tables and the same integer maths and pitch-register limits as the hardware. Notes pitched above the hardware ceiling clamp, just as they did on the console. That's authentic, but it can surprise you on very high notes. Lotus does **not** BRR-encode samples, run the SNES echo/FIR, or emulate the hardware ADSR/GAIN units: your SF2 samples are played as-is.
- **Nintendo DS** follows the documented hardware: sample-and-hold playback, timer-quantised channel rates, and the ~32.7 kHz, 10-bit output stage.
- **GBA** has no hardware sample engine. Games used software mixers, most commonly Nintendo's "m4a"/MusicPlayer2000. This mode models that style of mixer (13 379 Hz, the most common rate; 8-bit samples; 8-bit output). It is an approximation of a typical game, not a bit-exact clone of any one.
- No mode emulates the analogue output stage of the console (DAC filtering, speaker), and none applies console effects such as reverb or echo.

## Building

Requirements: CMake 3.22+, a C++17 compiler (Visual Studio 2022, Xcode 14+, GCC 11+/Clang 14+). JUCE 8 is downloaded automatically.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build --config Release --parallel
```

Plugins end up in `build/Lotus_artefacts/Release/` (`VST3/Lotus.vst3`, `Standalone/`, `AU/`, `LV2/`).

- **Linux** also needs: `libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev`.
- To use a JUCE checkout you already have: `-DLOTUS_JUCE_DIR=/path/to/JUCE`.
- To build only the engine and its tests (no JUCE): `-DLOTUS_BUILD_PLUGIN=OFF`, then run `ctest --test-dir build`.

Every push runs a GitHub Actions build that builds Windows, macOS (universal) and Linux binaries, runs the engine tests, validates the VST3 with [pluginval](https://github.com/Tracktion/pluginval), and uploads zipped plugins as workflow artifacts. Pushing a `v*` tag also attaches them to a GitHub release.

### Installing

Copy `Lotus.vst3` to:

- Windows: `C:\Program Files\Common Files\VST3\`
- macOS: `~/Library/Audio/Plug-Ins/VST3/` (AU: `~/Library/Audio/Plug-Ins/Components/`)
- Linux: `~/.vst3/` (LV2: `~/.lv2/`)

macOS builds from CI are unsigned. If macOS refuses to load them, run `xattr -dr com.apple.quarantine` on the plugin.

## SoundFont support

Supported: SF2 presets/instruments/zones with global zones, key and velocity ranges, preset-level generator offsets, sample start/end/loop offsets, loop modes (none, continuous, loop-until-release), root key / overriding root key, coarse/fine tune and sample pitch correction, scale tuning, initial attenuation, pan, exclusive classes (hi-hat choke), fixed key/velocity, and the volume envelope (delay/attack/hold/decay/sustain/release, with key scaling).

Not supported yet: modulators other than the default velocity curve, the filter, LFOs and the modulation envelope, chorus/reverb sends, 24-bit (`sm24`) sample data (the 16-bit part is used), and compressed SF3 files. Most VGM SoundFonts (game rips and chiptune kits) rarely depend on these, but some General MIDI banks use the filter and will sound brighter than in a full SF2 synth.

Known limitation: in VST3 hosts, the host's own program list shows generic names ("Program 1…128") because VST3 fixes that list when the plugin is created. Selecting programs from the host or by MIDI program change works; the preset menu inside Lotus shows the real names.

## Project layout

```
Source/dsp/      framework-free engine: SF2 parser, envelope, voices, console models
Source/ui/       look & feel and custom widgets
Source/Plugin*   JUCE processor/editor glue
tests/           headless engine tests (build an SF2 in memory; no fixtures)
```

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for attributions.
