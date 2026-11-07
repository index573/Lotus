# Lotus

Lotus is a SoundFont (`.sf2`) player for making video game music. Release at 0 means the note stops, and you can play your samples through models of the sound hardware in the SNES, N64, PlayStation, Mega Drive, Amiga and other machines.

![Lotus](docs/screenshot.png)

Formats: **CLAP**, **VST3** and **Standalone** on Windows, macOS and Linux, plus **AU** on macOS and **LV2** on Linux.

## What it does

- **ADSR.** A 250 ms release lasts 250 ms and 0 stops the note dead. The SoundFont's own envelopes only apply if you switch to them.
- **Cut mode.** One switch makes notes stop the moment you let go of the key, even with the SoundFont's envelopes. With the sustain pedal down, the cut happens when the pedal comes up.
- **Console playback.** Pick a console and Lotus copies its mixer rate, interpolation, pitch precision, bit depth and output filtering.
- **Native sample formats.** Optionally hear your samples after conversion to the console's own format (BRR on SNES, ADPCM on PS1/PS2, IMA-ADPCM on DS, DPCM on NES).
- **Hardware voice limits.** Optionally cap polyphony at the console's voice count, so the SNES gets 8, the Amiga 4 and the NES DMC 1. Extra notes steal the oldest.
- **Song-wide voice light.** Every Lotus in your project set to the same console adds up its voices, and a light in the Console card turns red when the song needs more than the hardware has. Click it to find out how many consoles your song would take.
- **Console effects.** SNES echo, N64 reverb, PS1 reverb and the Amiga LED filter, on their own tab.
- **Vibrato.** A per-note pitch LFO with amount, speed, delay and fade-in, like the pitch LFO in FL Studio's channel settings.
- **Tuning.** Master tuning you can type in cents or as an A4 frequency (432 Hz, 415 Hz and so on).
- **Tuning repair.** Drag in a SoundFont with wrong root notes and Lotus offers to fix it. It measures every preset, lets you hear each fix before you keep it, and saves a `_FIXED.sf2` copy or keeps the fixes in your project.
- **English and Japanese.** Switch with the globe button.
- **Glide**, pitch-bend range, MIDI program changes, drag and drop, a scalable window.

## Consoles

| Console | Mixer rate | Interpolation | Pitch | Output |
| --- | --- | --- | --- | --- |
| Modern / Linear / Raw | project rate | Hermite / linear / none | exact | float |
| NES | project rate | none (DMC) | 16 fixed DMC rates | 7-bit, 90 Hz + 440 Hz high-pass, 14 kHz low-pass |
| SNES | 32 kHz | 4-tap Gaussian (S-DSP table) | 4.12 fixed point, about 2 octaves up | 16-bit |
| N64 | 32 kHz | 4-tap, 64-phase polyphase (RSP table) | Q1.15, at most one octave up | 16-bit |
| GBA | 13.379 kHz | linear | exact | 8-bit, held |
| Nintendo DS | 32.73 kHz | none | 16-bit timer | 10-bit, held |
| PS1 | 44.1 kHz | 4-tap Gaussian (SPU table) | 4.12 fixed point | 16-bit |
| PS2 | 48 kHz | 4-tap Gaussian (SPU table) | 4.12 fixed point | 16-bit |
| Mega Drive | 14 kHz | none | exact | 8-bit, held, YM2612 ladder effect, 2.84 kHz RC low-pass (model 1) |
| Saturn / Dreamcast | 44.1 kHz | linear | octave + 10-bit F-number | 16-bit |
| Amiga 500 | Paula | none | 3546895 / period | 8-bit samples, 4.42 kHz RC low-pass |
| Amiga 1200 | Paula | none | 3546895 / period | 8-bit samples, no low-pass |

Where the numbers come from, and where they're approximations:

- **SNES, PS1, PS2, N64** use the real interpolation tables and integer maths.
- **Pitch ceilings (SNES, N64, NES).** Game developers converted samples to the console's mixer rate before using them, so Lotus applies each console's pitch ceiling relative to the sample's root note. That way a normal 44.1 kHz SoundFont plays in tune, and only notes far above the root hit the limit, as they would have on the hardware.
- **NES.** The DMC can only play at 16 rates, so notes snap to whichever is closest and drift out of tune the way they did on the console. Notes above the root can't go any higher.
- **Amiga.** Pitch snaps to integer Paula periods. Above Paula's top rate (about 31 kHz) Lotus keeps the pitch and lets the sample play decimated rather than detuning it.
- **GBA and Mega Drive** had no hardware sample engine; games mixed samples in software. These modes model the common drivers: Nintendo's m4a on GBA (13.379 kHz) and a 14 kHz Z80 mixer like SGDK's XGM on Mega Drive. Individual games varied.
- **Saturn and Dreamcast** use the same pitch scheme and linear interpolation, so they share a mode.
- Not modelled: the N64's VADPCM sample format, which needs per-sample codebooks, and the GameCube/Wii, whose resampling filter only exists in Nintendo's DSP ROM.

## Effects

![Effects](docs/effects.png)

Each effect takes the dry voice mix and adds its return back in, as the hardware did, and you can run several at once.

**SNES Echo.** The S-DSP echo unit with its own integer maths and quirks.

| Control | Register | Range |
| --- | --- | --- |
| Delay | EDL | 0-240 ms in 16 ms steps. A new delay takes effect when the buffer wraps. |
| Feedback | EFB | -100% to +99%. Negative feedback flips the polarity of each repeat. |
| Volume | EVOL | 0-99% |
| Filter | FIR | The four presets from Nintendo's standard SNES sound driver: Flat, Low, High, Band |

The FIR adds 7 samples of delay, and extreme settings can overflow and distort the same way the chip does.

**N64 Reverb.** libultra's effect bus with its five presets: Small Room, Big Room, Echo, Chorus and Flange. One delay line runs through feed-forward/feedback sections, with a one-pole low-pass in the loop and triangle-modulated taps for chorus and flange.

**PS1 Reverb.** The SPU reverb: same-side and cross-side reflections, four combs and two all-pass stages on a 16-bit work area, running at half rate with the hardware's 39-tap resampling filter. It comes with Sony's nine presets (Room, three Studio sizes, Hall, Half Echo, Space Echo, Chaos Echo, Delay). As on the hardware presets, the reverb input is phase-inverted.

**Amiga LED Filter.** The Amiga's switchable 12 dB/oct low-pass at about 3.1 kHz (a Sallen-Key stage with the real component values). It works on any console.

The effects run at the console's mixer rate. With the matching console selected they behave like the hardware; with any other console, delay times stay right but the filters shift with the sample rate. The Effects tab tells you when that's the case.

## Pitch tab

![Pitch](docs/pitch.png)

**Vibrato** starts after the delay, fades in over the attack, then swings the pitch by the amount (in cents; 100 cents is a semitone) at the speed you set. Each note starts its own vibrato, so a held note gets the classic delayed wobble while quick notes stay clean. The shape can be sine, triangle or square. Square gives a trill-like jump between two pitches. On console modes the vibrato goes through the console's own pitch precision, so a SNES vibrato steps the way the S-DSP's pitch register would.

**Tuning** shifts everything by up to a semitone either way. Switch between cents and Hz, then click the value to type either one: `-31.8`, `432 Hz` and `A4 = 415` all work. Double-click the knob to go back to A4 = 440 Hz.

## Tuning repair

![Tuning results](docs/tuning.png)

SoundFonts ripped from games often have the wrong root note on some samples, so a preset plays a fifth or an octave away from the key you press. When you load a SoundFont by dragging it in or with **Load SF2**, Lotus does a quick check in the background. If it finds the patterns of a broken bank, it asks: *"Strange tuning and incorrect root notes detected. Attempt to adjust to C5/A=440Hz?"* You can also run the check yourself from the Pitch tab with **Check tuning...**.

Saying yes runs a full analysis with a progress bar. For every preset, Lotus takes the sample that plays at C5 (middle C, MIDI 60, in FL Studio's note naming) and measures its pitch with the YIN algorithm, then compares what the preset plays against A4 = 440 Hz. The results list shows:

- **To adjust:** each preset with what's wrong ("Root note 7 semitones too high", "18 cents flat"), the change in cents, a Fix/Keep switch, and **Before**/**After** buttons that play C5 both ways.
- **Left alone:** everything it didn't touch and why. That covers drum kits, sound effects, sounds with no clear pitch, pitch that drifts too much to measure, and presets that are already in tune.

Then **Save fixed version?** gives four choices:

- **Yes, save:** writes `name_FIXED.sf2` next to the original and switches to it.
- **Yes, save as...:** lets you pick where.
- **No:** keeps the fixes in your project only. The .sf2 file isn't touched, and the fixes come back every time the project loads. The Pitch tab shows how many are active, with a button to clear them.
- **Cancel:** throws the fixes away.

How it decides, and its limits:

- Fixes are written as preset-level tuning, so instruments shared with other presets (often drum kits) don't change. The rest of the file, samples included, is copied byte for byte.
- A preset that's off by exactly an octave is listed but not selected, because organ footages, harmonics and some pads really do sound an octave away. Listen and switch it to Fix if it's wrong. The exception is when most of the bank is off by the same octave, which is usually a C4/C5 naming mix-up; then those are selected.
- Patches within 25 cents count as in tune, because ensemble patches (accordion, choir) are often detuned on purpose.
- If a preset layers a voice a fifth or an octave above, Lotus measures the layer with the least transposition.
- The automatic prompt only appears for broken-looking banks: many wrong roots, the whole bank shifted by one octave, or the whole bank detuned. A healthy General MIDI bank with a couple of odd presets doesn't trigger it; you can still check it by hand. On FluidR3_GM and TimGM6mb, two well-tuned GM banks, it stays quiet. FluidR3's "Sine Wave" preset really does play a tritone off, and the manual check catches it.
- Pitch detection can be fooled by bells and other inharmonic sounds. That's why every fix can be auditioned before you keep it.

## Japanese

![Japanese](docs/japanese.png)

Click the globe button and pick 日本語. Every label, tooltip and dialog switches at once, and the choice is saved for all Lotus instances. With nothing saved, Lotus follows your system language. Console names use their Japanese release names (スーパーファミコン, メガドライブ and so on). Note names stay in FL Studio's numbering, where middle C is C5.

## The voice light

The light at the top right of the Console card shows how many voices are playing right now across every Lotus in the project set to the same console, against that console's limit (for example `9 / 8` on SNES). It turns amber at the limit and red over it, and stays red for two seconds so a brief overload doesn't slip past.

Click it after playing your song through and Lotus tells you how many of that console the song would need, based on the peak. **Reset peak** starts the count again.

![Verdict](docs/verdict.png)

If the tracks in your project target different consoles, the light goes dark (it reads "mixed"), because one console's limit can't be judged against another's. Clicking it then lists the consoles in use. A track only counts once it has played, so freshly loaded tracks don't trigger this.

![Console mismatch](docs/mismatch.png)

Instances find each other through shared memory, so this works when your DAW loads plugins in the same process, which is the usual setup. Instances it can't see are those running in a separate plugin sandbox or bridge, or a VST3 and a CLAP copy mixed in one project. It's a warning only: each instance still steals voices on its own when the Voices chip is on.

## Using it

- **Load SF2**, or drag a `.sf2` onto the window. Lotus remembers the file's location in your project.
- Pick a preset from the menu or with the arrows. MIDI program changes and bank select (CC0/CC32) work too. In VST3 hosts they arrive through the host's program list, which shows "Program 1-128" rather than preset names because VST3 fixes that list at load.
- Double-click a knob to reset it, or click its value to type one (`250 ms`, `1.5 s`).
- The Sound tab holds the instrument, the Pitch tab holds vibrato, tuning and tuning repair, and the Effects tab holds the console effects.
- Hover over a control to see what it does. Hovering the console menu shows what that console mode emulates.
- MIDI: notes, velocity, pitch bend, sustain (CC64), volume (CC7), pan (CC10), expression (CC11), all-notes-off and all-sound-off. Lotus listens on every channel; use one instance per instrument.

## Building

You need CMake 3.22+ and a C++17 compiler (Visual Studio 2022, Xcode 14+, or GCC 11+/Clang 14+). JUCE 8 and clap-juce-extensions are downloaded during configuration.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build --config Release --parallel
```

The plugins land in `build/Lotus_artefacts/Release/`.

On Linux, install these first: `libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev`.

Options:

- `-DLOTUS_JUCE_DIR=...` and `-DLOTUS_CLAP_JUCE_EXTENSIONS_DIR=...` use local checkouts instead of downloading.
- `-DLOTUS_BUILD_CLAP=OFF` skips the CLAP.
- `-DLOTUS_BUILD_PLUGIN=OFF` builds only the engine and its tests (no JUCE needed); run them with `ctest --test-dir build`.

GitHub Actions builds every push on Windows, macOS (universal) and Linux, runs the tests, validates the VST3 with [pluginval](https://github.com/Tracktion/pluginval), and uploads zipped plugins as build artifacts. Pushing a `v*` tag also attaches them to a GitHub release.

### Installing

| | CLAP | VST3 |
| --- | --- | --- |
| Windows | `C:\Program Files\Common Files\CLAP\` | `C:\Program Files\Common Files\VST3\` |
| macOS | `~/Library/Audio/Plug-Ins/CLAP/` | `~/Library/Audio/Plug-Ins/VST3/` |
| Linux | `~/.clap/` | `~/.vst3/` |

AU goes in `~/Library/Audio/Plug-Ins/Components/` and LV2 in `~/.lv2/`. The macOS builds aren't signed; if macOS won't load them, run `xattr -dr com.apple.quarantine` on the plugin.

## SoundFont support

Lotus reads presets, instruments and zones (including global zones), key and velocity ranges, preset-level generator offsets, sample and loop offsets, all three loop modes, root key overrides, tuning, scale tuning, attenuation, pan, exclusive classes (hi-hat choke), fixed key and velocity, and the volume envelope with key scaling.

It doesn't yet support modulators beyond the default velocity curve, the SF2 filter, LFOs, the modulation envelope, chorus/reverb sends, 24-bit sample data (the 16-bit part plays), or SF3 files. Game rips and chiptune kits rarely use these. Some General MIDI banks lean on the filter and will sound brighter than they would in a full SF2 synth.

## Source layout

```
Source/dsp/   engine: SF2 parser, envelope, voices, consoles, codecs, effects, tuning analysis
Source/ui/    look and feel, widgets, tuning dialog, translations (StringsJa.inc)
Source/       plugin processor and editor
tests/        engine tests (they build an SF2 in memory, so there are no fixtures)
tools/        check_translations.py: fails if a UI string has no Japanese entry
```

Sources and licences for the hardware tables and algorithms are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
