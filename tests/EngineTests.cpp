// Lotus - headless engine tests.
//
// Builds a SoundFont in memory (so no binary fixtures are needed), then checks the
// parser and the behaviours Lotus promises: notes that really stop, ADSR times
// that mean what they say, and console modes that render sensibly.

#include "../Source/dsp/Codecs.h"
#include "../Source/dsp/Synth.h"
#include "../Source/dsp/TuningAnalysis.h"
#include "../Source/dsp/VoiceRegistry.h"
#include "TestFont.h"

#include <cmath>
#include <cstdio>
#include <atomic>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace lotus;

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { std::printf ("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

// ---------------------------------------------------------------------------------

static float peak (const std::vector<float>& x, size_t from, size_t to)
{
    float p = 0.0f;
    for (size_t i = from; i < std::min (to, x.size()); ++i) p = std::max (p, std::fabs (x[i]));
    return p;
}

struct Rig
{
    std::unique_ptr<SoundFont> sf;
    Synth synth;
    double rate;

    explicit Rig (EngineParams p, double sampleRate = 48000.0, int releaseTc = 2400) : rate (sampleRate)
    {
        auto tf = makeTestFont (releaseTc);
        std::string err;
        sf = SoundFont::loadFromMemory (tf.data.data(), tf.data.size(), err);
        if (! sf) { std::printf ("  load failed: %s\n", err.c_str()); std::exit (1); }
        synth.prepare (sampleRate, 512);
        synth.setSoundFont (sf.get());
        synth.setParams (p);
    }

    std::vector<float> run (double seconds, std::vector<float>* right = nullptr)
    {
        const int n = (int) (seconds * rate);
        std::vector<float> l ((size_t) n), r ((size_t) n);
        for (int i = 0; i < n; i += 256)
            synth.render (l.data() + i, r.data() + i, std::min (256, n - i));
        if (right) *right = r;
        return l;
    }
};

static void testParser()
{
    std::printf ("parser\n");
    auto tf = makeTestFont();
    std::string err;
    auto sf = SoundFont::loadFromMemory (tf.data.data(), tf.data.size(), err);
    CHECK (sf != nullptr);
    if (! sf) return;
    CHECK (sf->name == "Lotus Test");
    CHECK (sf->presets.size() == 2);
    CHECK (sf->presets[0].name == "Square");
    CHECK (sf->presets[0].regions.size() == 1);
    CHECK (sf->presets[1].regions.size() == 2);
    const auto& r = sf->presets[0].regions[0];
    CHECK (r.rootKey == 69);
    CHECK (r.loopMode == LoopMode::Continuous);
    CHECK (r.loopStart == 100 && r.loopEnd == 900);
    CHECK (std::fabs (r.volEnv.release - 4.0f) < 0.01f);
    CHECK (sf->presets[1].regions[0].exclusiveClass == 1);

    std::string err2;
    const char junk[] = "RIFF....WAVEfmt ";
    CHECK (SoundFont::loadFromMemory (junk, sizeof (junk), err2) == nullptr);
    CHECK (! err2.empty());

    // Truncated file must fail gracefully, not crash.
    for (size_t cut : { (size_t) 20, tf.data.size() / 2, tf.data.size() - 10 })
    {
        std::string e;
        auto t = SoundFont::loadFromMemory (tf.data.data(), cut, e);
        (void) t;
    }
}

static void testCutOnNoteOff()
{
    std::printf ("cut on note-off\n");
    EngineParams p;
    p.cutOnNoteOff = true;
    p.useSf2Envelope = true;   // even with the file's 4 s release...
    Rig rig (p);
    rig.synth.noteOn (69, 127);
    auto a = rig.run (0.2);
    CHECK (peak (a, 0, a.size()) > 0.1f);
    rig.synth.noteOff (69);
    auto b = rig.run (0.1);
    const size_t ms2 = (size_t) (0.002 * rig.rate);
    CHECK (peak (b, ms2, b.size()) == 0.0f);   // ...silent within 2 ms
    CHECK (rig.synth.getActiveVoiceCount() == 0);
}

static void testSf2ReleaseIsLong()
{
    std::printf ("sf2 envelope keeps the file's release when not cutting\n");
    EngineParams p;
    p.useSf2Envelope = true;
    Rig rig (p);
    rig.synth.noteOn (69, 127);
    rig.run (0.2);
    rig.synth.noteOff (69);
    auto b = rig.run (0.5);
    CHECK (peak (b, b.size() - 100, b.size()) > 0.01f);  // still ringing after 0.5 s
}

static void testZeroReleaseIsInstant()
{
    std::printf ("Lotus ADSR: release 0 is instant, release time is exact\n");
    {
        EngineParams p; p.release = 0.0f;
        Rig rig (p);
        rig.synth.noteOn (69, 127);
        rig.run (0.1);
        rig.synth.noteOff (69);
        auto b = rig.run (0.05);
        CHECK (peak (b, 1, b.size()) == 0.0f);
        CHECK (rig.synth.getActiveVoiceCount() == 0);
    }
    {
        EngineParams p; p.release = 0.25f;
        Rig rig (p);
        rig.synth.noteOn (69, 127);
        rig.run (0.1);
        rig.synth.noteOff (69);
        auto b = rig.run (0.4);
        const size_t at200 = (size_t) (0.20 * rig.rate), at255 = (size_t) (0.255 * rig.rate);
        CHECK (peak (b, at200 - 200, at200) > 0.0f);      // still audible before 250 ms
        CHECK (peak (b, at255, b.size()) == 0.0f);        // gone after 250 ms
        CHECK (rig.synth.getActiveVoiceCount() == 0);
    }
}

static void testAttackDecaySustain()
{
    std::printf ("Lotus ADSR: attack ramps, decay settles at sustain\n");
    EngineParams p; p.attack = 0.1f; p.decay = 0.1f; p.sustain = 0.25f;
    Rig rig (p);
    rig.synth.noteOn (69, 127);
    auto a = rig.run (0.5);
    const auto ms = [&] (double t) { return (size_t) (t * rig.rate); };
    const float early = peak (a, 0, ms (0.01));
    const float top = peak (a, ms (0.09), ms (0.11));
    const float sus = peak (a, ms (0.4), ms (0.5));
    CHECK (early < top * 0.2f);
    CHECK (top > 0.3f);
    CHECK (std::fabs (sus / top - 0.25f) < 0.05f);
}

static void testSustainPedal()
{
    std::printf ("sustain pedal defers the note-off\n");
    EngineParams p; p.cutOnNoteOff = true;
    Rig rig (p);
    const uint8_t pedalDown[] = { 0xB0, 64, 127 }, pedalUp[] = { 0xB0, 64, 0 };
    rig.synth.handleMidi (pedalDown, 3);
    rig.synth.noteOn (69, 127);
    rig.run (0.05);
    rig.synth.noteOff (69);
    auto held = rig.run (0.05);
    CHECK (peak (held, held.size() - 100, held.size()) > 0.1f);
    rig.synth.handleMidi (pedalUp, 3);
    auto after = rig.run (0.05);
    CHECK (peak (after, (size_t) (0.002 * rig.rate), after.size()) == 0.0f);
}

static void testExclusiveClass()
{
    std::printf ("exclusive class chokes (open/closed hi-hat)\n");
    EngineParams p;
    Rig rig (p);
    rig.synth.setPresetIndex (1);
    rig.synth.noteOn (46, 127);
    rig.run (0.05);
    CHECK (rig.synth.getActiveVoiceCount() == 1);
    rig.synth.noteOn (42, 127);
    rig.run (0.05);
    CHECK (rig.synth.getActiveVoiceCount() == 1);
}

static double zeroCrossFreq (const std::vector<float>& x, double rate)
{
    int crossings = 0;
    for (size_t i = 1; i < x.size(); ++i)
        if ((x[i - 1] < 0.0f) != (x[i] < 0.0f)) ++crossings;
    return crossings / 2.0 / (x.size() / rate);
}

static void testConsoles()
{
    std::printf ("console modes render at the right pitch with no NaNs\n");
    for (int c = 0; c < numConsoles; ++c)
    {
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            EngineParams p; p.console = (ConsoleId) c;
            Rig rig (p, rate);
            rig.synth.noteOn (69, 127);   // root key -> 441 Hz square (100-sample period at 44.1k)
            auto a = rig.run (0.5);

            bool finite = true;
            for (float s : a) finite &= std::isfinite (s);
            CHECK (finite);
            CHECK (peak (a, 0, a.size()) > 0.1f);
            CHECK (peak (a, 0, a.size()) <= 1.0f);

            std::vector<float> tail (a.begin() + (long) (0.1 * rate), a.end());
            const double f = zeroCrossFreq (tail, rate);
            if (std::fabs (f - 441.0) > 441.0 * 0.01)
            {
                std::printf ("  %s @ %.0f Hz: measured %.1f Hz\n", getConsoleProfile ((ConsoleId) c).name, rate, f);
                ++failures;
            }

            // A note well above the hardware pitch ceiling must still be safe.
            rig.synth.noteOn (127, 127);
            auto hi = rig.run (0.1);
            bool ok = true;
            for (float s : hi) ok &= std::isfinite (s) && std::fabs (s) <= 2.0f;
            CHECK (ok);
        }
    }

    // SNES pitch ceiling: 0x3FFF ~= 4x the root pitch. Three octaves up must clamp to that.
    {
        EngineParams p; p.console = ConsoleId::Snes;
        Rig rig (p, 48000.0);
        rig.synth.noteOn (69 + 36, 127);
        auto a = rig.run (0.5);
        std::vector<float> tail (a.begin() + 4800, a.end());
        const double expected = 441.0 * 0x3FFF / 4096.0;
        const double f = zeroCrossFreq (tail, 48000.0);
        CHECK (std::fabs (f - expected) < expected * 0.02);
    }

    // N64: at most one octave above the root.
    {
        EngineParams p; p.console = ConsoleId::N64;
        Rig rig (p, 48000.0);
        rig.synth.noteOn (69 + 24, 127);
        auto a = rig.run (0.5);
        std::vector<float> tail (a.begin() + 4800, a.end());
        const double f = zeroCrossFreq (tail, 48000.0);
        CHECK (std::fabs (f - 882.0) < 882.0 * 0.02);
    }

    // NES: notes snap to the 16 DMC rates. A semitone up from the root can't go higher
    // than the fastest rate, so it stays at the root pitch.
    {
        EngineParams p; p.console = ConsoleId::Nes;
        Rig rig (p, 48000.0);
        rig.synth.noteOn (70, 127);
        auto a = rig.run (0.5);
        std::vector<float> tail (a.begin() + 4800, a.end());
        const double f = zeroCrossFreq (tail, 48000.0);
        CHECK (std::fabs (f - 441.0) < 441.0 * 0.02);
    }
}

static void testTables()
{
    std::printf ("gaussian tables\n");
    const auto& s = snesGaussTable();
    CHECK (s[511] == 1305);   // well-known S-DSP centre coefficient (0x519)
    CHECK (s[0] == 0);
    for (int i = 0; i < 256; ++i)
    {
        const int sum = s[(size_t) i] + s[(size_t) (255 - i)] + s[(size_t) (256 + i)] + s[(size_t) (511 - i)];
        CHECK (sum >= 2046 && sum <= 2050);
        const auto& p = ps1GaussTable();
        const int ps = p[(size_t) i] + p[(size_t) (255 - i)] + p[(size_t) (256 + i)] + p[(size_t) (511 - i)];
        CHECK (ps >= 0x7F7F && ps <= 0x7F81);
    }
}

static void testProgramChange()
{
    std::printf ("program change selects the matching preset\n");
    EngineParams p;
    Rig rig (p);
    const uint8_t pc[] = { 0xC0, 1 };
    rig.synth.handleMidi (pc, 2);
    CHECK (rig.synth.getPresetIndex() == 1);
    CHECK (rig.synth.presetChangedByMidi.load());
}


static void testSnesEcho()
{
    std::printf ("SNES echo: delay = EDL x 16 ms, feedback repeats, EDL latches at wrap\n");
    SnesEcho echo;
    echo.prepare (48000.0);
    echo.setRate (32000.0);
    EffectParams p; p.snesEcho = true; p.snesDelay = 5; p.snesFeedback = 0; p.snesVolume = 127; p.snesFilter = 0;

    const int n = 32000;
    std::vector<float> l ((size_t) n, 0.0f), r ((size_t) n, 0.0f);
    l[0] = r[0] = 0.5f;
    echo.process (l.data(), r.data(), n, p);

    // 80 ms at 32 kHz, plus 7 samples: FIR0 (the only tap in the flat preset)
    // applies to the oldest of the 8 history samples, exactly as on the S-DSP.
    const size_t delay = 5 * 512 + 7;
    CHECK (peak (l, 1, delay) == 0.0f);
    CHECK (std::fabs (l[delay] - 0.5f) < 0.02f);            // flat FIR, EVOL 127: ~unity
    CHECK (peak (l, delay + 1, n) == 0.0f);                 // no feedback: exactly one repeat

    SnesEcho fb;
    fb.prepare (48000.0);
    fb.setRate (32000.0);
    p.snesFeedback = 64;
    std::vector<float> a ((size_t) n, 0.0f), b ((size_t) n, 0.0f);
    a[0] = b[0] = 0.5f;
    fb.process (a.data(), b.data(), n, p);
    CHECK (std::fabs (a[delay * 2] - 0.25f) < 0.02f);   // each pass adds the 7-sample FIR delay       // EFB 64 = half each repeat
    CHECK (std::fabs (a[delay * 3] - 0.125f) < 0.02f);

    // Negative feedback flips the polarity of each repeat.
    SnesEcho neg;
    neg.prepare (48000.0);
    neg.setRate (32000.0);
    p.snesFeedback = -64;
    std::vector<float> c ((size_t) n, 0.0f), d ((size_t) n, 0.0f);
    c[0] = d[0] = 0.5f;
    neg.process (c.data(), d.data(), n, p);
    CHECK (c[delay * 2] < -0.2f);

    // Time scales with the engine rate.
    SnesEcho fast;
    fast.prepare (48000.0);
    fast.setRate (48000.0);
    p.snesFeedback = 0;
    std::vector<float> e ((size_t) n, 0.0f), f ((size_t) n, 0.0f);
    e[0] = f[0] = 0.5f;
    fast.process (e.data(), f.data(), n, p);
    CHECK (std::fabs (e[3840 + 7] - 0.5f) < 0.02f);         // 80 ms at 48 kHz
}

static void testPs1Reverb()
{
    std::printf ("PS1 reverb: every preset produces a bounded, decaying tail\n");
    for (int preset = 0; preset < numPs1ReverbPresets; ++preset)
    {
        Ps1Reverb rev;
        rev.prepare (48000.0);
        rev.setRate (44100.0);
        EffectParams p; p.ps1Reverb = true; p.ps1Preset = preset; p.ps1Depth = 0.5f;

        const int n = 44100 * 8;
        std::vector<float> l ((size_t) n, 0.0f), r ((size_t) n, 0.0f);
        for (int i = 0; i < 441; ++i) l[(size_t) i] = r[(size_t) i] = (i % 40 < 20) ? 0.5f : -0.5f;  // 10 ms burst
        rev.process (l.data(), r.data(), n, p);

        bool finite = true;
        for (float x : l) finite &= std::isfinite (x) && std::fabs (x) <= 2.0f;
        const float tail = peak (l, 4410, 44100);             // 0.1 .. 1 s
        const float late = peak (l, (size_t) n - 4410, (size_t) n);
        std::printf ("  %-14s tail %.4f  after 8 s %.5f\n", ps1ReverbPresetName (preset), tail, late);
        CHECK (finite);
        CHECK (tail > 0.001f);
        if (std::string (ps1ReverbPresetName (preset)) != "Chaos Echo")   // "almost infinite" by design
            CHECK (late < tail);
    }
}

static void testEffectsInEngine()
{
    std::printf ("effects run in the engine, including with no SoundFont\n");
    {
        Synth s;
        s.prepare (48000.0, 512);
        EngineParams p; p.fx.snesEcho = true; p.fx.ps1Reverb = true;
        s.setParams (p);
        std::vector<float> l (4800), r (4800);
        s.render (l.data(), r.data(), 4800);
        CHECK (peak (l, 0, l.size()) == 0.0f);
    }
    {
        EngineParams p; p.console = ConsoleId::Snes; p.cutOnNoteOff = true;
        p.fx.snesEcho = true; p.fx.snesDelay = 4; p.fx.snesFeedback = 0; p.fx.snesVolume = 100;
        Rig rig (p);
        rig.synth.noteOn (69, 127);
        rig.run (0.03);
        rig.synth.noteOff (69);
        auto a = rig.run (0.2);
        // Dry note is cut, but the echo of it arrives ~64 ms later.
        CHECK (peak (a, (size_t) (0.01 * rig.rate), (size_t) (0.025 * rig.rate)) == 0.0f);
        CHECK (peak (a, (size_t) (0.04 * rig.rate), (size_t) (0.09 * rig.rate)) > 0.05f);
    }
}

static double snrDb (const std::vector<int16_t>& ref, const std::vector<int16_t>& test)
{
    double sig = 0, err = 0;
    for (size_t i = 0; i < ref.size(); ++i)
    {
        sig += (double) ref[i] * ref[i];
        err += (double) (ref[i] - test[i]) * (ref[i] - test[i]);
    }
    return 10.0 * std::log10 (sig / std::max (err, 1.0));
}

static void testCodecs()
{
    std::printf ("native sample formats: decode close to the source, with each format's character\n");
    std::vector<int16_t> sine (44100);
    for (size_t i = 0; i < sine.size(); ++i)
        sine[i] = (int16_t) (12000.0 * std::sin (2.0 * 3.14159265358979 * 440.0 * (double) i / 44100.0));

    struct C { Codec c; double minSnr, maxSnr; };
    for (auto [codec, lo, hi] : { C { Codec::Brr, 20.0, 80.0 }, C { Codec::PsAdpcm, 20.0, 80.0 },
                                  C { Codec::ImaAdpcm, 15.0, 80.0 }, C { Codec::Dpcm, 0.0, 30.0 } })
    {
        const auto out = applyCodec (sine, codec);
        CHECK (out.size() == sine.size());
        const double snr = snrDb (sine, out);
        std::printf ("  %-10s SNR %.1f dB\n", codecName (codec), snr);
        CHECK (snr > lo && snr < hi);   // lossy, but recognisably the same signal
    }

    // DPCM can only move 2 steps of 128 per sample: a full-scale jump takes ~64 samples.
    std::vector<int16_t> step (200, 0);
    for (size_t i = 100; i < step.size(); ++i) step[i] = 30000;
    const auto d = applyCodec (step, Codec::Dpcm);
    CHECK (d[105] < 10000);
    CHECK (d[199] > 25000);

    std::atomic<bool> cancel { true };
    std::vector<int16_t> big (1 << 21, 0);
    CHECK (applyCodec (big, Codec::Brr, &cancel).empty());
}

static void testNativeFormatInEngine()
{
    std::printf ("engine plays the coded pool only when the format is enabled and matches\n");
    EngineParams p; p.console = ConsoleId::Snes;
    Rig rig (p);
    std::vector<int16_t> silent (rig.sf->samples().size(), 0);
    rig.synth.setCodedPool (&silent, Codec::Brr);
    rig.synth.noteOn (69, 127);
    CHECK (peak (rig.run (0.1), 0, 4800) > 0.1f);   // nativeFormat off: original samples

    p.nativeFormat = true;
    rig.synth.setParams (p);
    rig.synth.noteOn (69, 127);
    auto a = rig.run (0.1);
    CHECK (peak (a, 2400, a.size()) == 0.0f);         // now the (silent) coded pool

    rig.synth.setCodedPool (&silent, Codec::PsAdpcm); // wrong codec for SNES: ignored
    rig.synth.noteOn (69, 127);
    CHECK (peak (rig.run (0.1), 0, 4800) > 0.1f);
}

static void testN64Reverb()
{
    std::printf ("N64 reverb: every preset produces a bounded tail that dies away\n");
    for (int preset = 0; preset < numN64FxPresets; ++preset)
    {
        N64Reverb rev;
        rev.prepare (48000.0);
        rev.setRate (32000.0);
        EffectParams p; p.n64Reverb = true; p.n64Preset = preset; p.n64Mix = 1.0f;
        const int n = 32000 * 6;
        std::vector<float> l ((size_t) n, 0.0f), r ((size_t) n, 0.0f);
        for (int i = 0; i < 320; ++i) l[(size_t) i] = r[(size_t) i] = (i % 32 < 16) ? 0.5f : -0.5f;
        rev.process (l.data(), r.data(), n, p);

        bool finite = true;
        for (float x : l) finite &= std::isfinite (x) && std::fabs (x) <= 2.0f;
        const float tail = peak (l, 400, 32000);
        const float late = peak (l, (size_t) n - 3200, (size_t) n);
        std::printf ("  %-10s tail %.4f  after 6 s %.5f\n", n64FxPresetName (preset), tail, late);
        CHECK (finite);
        CHECK (tail > 0.001f);
        CHECK (late < tail * 0.05f);
    }

    // Echo: the first repeat lands at 179 "ms" = 7160 samples.
    N64Reverb echo;
    echo.prepare (48000.0);
    echo.setRate (32000.0);
    EffectParams p; p.n64Reverb = true; p.n64Preset = 2; p.n64Mix = 1.0f;
    std::vector<float> l (16000, 0.0f), r (16000, 0.0f);
    l[0] = r[0] = 0.5f;
    echo.process (l.data(), r.data(), 16000, p);
    CHECK (peak (l, 1, 7150) == 0.0f);
    CHECK (peak (l, 7155, 7170) > 0.2f);
}

static void testVoiceLimitAndGlide()
{
    std::printf ("hardware voice limit and glide\n");
    {
        EngineParams p; p.console = ConsoleId::Snes; p.hardwareVoices = true;
        Rig rig (p);
        for (int k = 0; k < 12; ++k) rig.synth.noteOn (48 + k, 100);
        rig.run (0.05);
        CHECK (rig.synth.getActiveVoiceCount() == 8);

        p.hardwareVoices = false;
        rig.synth.setParams (p);
        for (int k = 0; k < 12; ++k) rig.synth.noteOn (60 + k, 100);
        rig.run (0.05);
        CHECK (rig.synth.getActiveVoiceCount() > 8);
    }
    {
        EngineParams p; p.glide = 0.2f; p.cutOnNoteOff = true;
        Rig rig (p, 48000.0);
        rig.synth.noteOn (57, 127);
        rig.run (0.1);
        rig.synth.noteOff (57);
        rig.synth.noteOn (69, 127);                      // glide up an octave over 200 ms
        auto a = rig.run (0.5);
        std::vector<float> early (a.begin(), a.begin() + 2400), late (a.begin() + 14400, a.end());
        const double fEarly = zeroCrossFreq (early, 48000.0), fLate = zeroCrossFreq (late, 48000.0);
        CHECK (fEarly < 300.0);
        CHECK (std::fabs (fLate - 441.0) < 441.0 * 0.02);
    }
}

static void testAnalogStage()
{
    std::printf ("analogue output: Amiga LED filter, Mega Drive ladder\n");
    AnalogStage a;
    a.configure (48000.0, 0.0f, 0.0f, 0.0f);
    auto rms = [] (const std::vector<float>& x)
    {
        double s = 0; for (size_t i = 2400; i < x.size(); ++i) s += x[i] * x[i];
        return std::sqrt (s / (double) (x.size() - 2400));
    };
    std::vector<float> l (9600), r (9600);
    for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = (float) std::sin (2.0 * 3.14159265358979 * 10000.0 * (double) i / 48000.0);
    auto off = l;
    a.process (l.data(), r.data(), (int) l.size(), true);
    CHECK (rms (l) < rms (off) * 0.2);                // 10 kHz is well past the 3.1 kHz LED corner

    EngineParams p; p.console = ConsoleId::MegaDrive;
    Rig rig (p, 48000.0);
    rig.synth.noteOn (69, 127);
    auto x = rig.run (0.3);
    float minNeg = 0.0f;
    for (float v : x) minNeg = std::min (minNeg, v);
    CHECK (minNeg < -0.3f);
}

static void testVoiceRegistry()
{
    std::printf ("voice registry: song-wide totals per console, peak and reset\n");
    auto& reg = VoiceRegistry::get();
    const int a = reg.join(), b = reg.join(), c = reg.join();
    CHECK (a >= 0 && b >= 0 && c >= 0 && a != b && b != c);

    reg.resetPeak (ConsoleId::Snes);
    CHECK (reg.publish (a, ConsoleId::Snes, 5) == 5);
    CHECK (reg.publish (b, ConsoleId::Snes, 6) == 11);
    CHECK (reg.publish (c, ConsoleId::Ps1, 20) == 20);        // other console: not counted with SNES
    CHECK (reg.currentTotal (ConsoleId::Snes) == 11);
    CHECK (reg.instancesOn (ConsoleId::Snes) == 2);

    reg.publish (a, ConsoleId::Snes, 0);
    CHECK (reg.currentTotal (ConsoleId::Snes) == 6);
    CHECK (reg.peakTotal (ConsoleId::Snes) == 11);            // peak is held
    reg.resetPeak (ConsoleId::Snes);
    CHECK (reg.peakTotal (ConsoleId::Snes) == 6);

    {
        const auto inUse = reg.consolesInUse();
        CHECK (inUse[(size_t) ConsoleId::Snes] && inUse[(size_t) ConsoleId::Ps1]);   // a mismatch
        CHECK (! inUse[(size_t) ConsoleId::Modern]);
        const int fresh = reg.join();                          // hasn't played yet: not counted
        CHECK (! reg.consolesInUse()[(size_t) ConsoleId::Modern]);
        reg.leave (fresh);
    }

    reg.leave (b);
    CHECK (reg.currentTotal (ConsoleId::Snes) == 0);          // leaving drops its voices
    reg.leave (a);
    reg.leave (c);
    CHECK (reg.instancesOn (ConsoleId::Snes) == 0);
    for (bool used : reg.consolesInUse()) CHECK (! used);
}

static void testVibratoAndTuning()
{
    std::printf ("vibrato (delay, depth, speed) and master tuning\n");
    auto pitchAt = [] (const std::vector<float>& a, double rate, double t0, double t1)
    {
        std::vector<float> w (a.begin() + (long) (t0 * rate), a.begin() + (long) (t1 * rate));
        return zeroCrossFreq (w, rate);
    };
    {
        EngineParams p; p.tuneCents = 1200.0f * std::log2 (432.0f / 440.0f);   // A4 = 432 Hz
        Rig rig (p, 48000.0);
        rig.synth.noteOn (69, 127);
        auto a = rig.run (0.5);
        CHECK (std::fabs (pitchAt (a, 48000.0, 0.1, 0.5) - 441.0 * 432.0 / 440.0) < 2.0);
    }
    {
        EngineParams p; p.vibAmount = 100.0f; p.vibSpeed = 2.0f; p.vibDelay = 0.3f; p.vibAttack = 0.0f; p.vibShape = 2;
        Rig rig (p, 48000.0);
        rig.synth.noteOn (69, 127);
        auto a = rig.run (1.2);
        // Before the delay: steady. Square vibrato at 2 Hz then alternates +100 / -100 cents every 250 ms.
        CHECK (std::fabs (pitchAt (a, 48000.0, 0.05, 0.28) - 441.0) < 3.0);
        const double up = pitchAt (a, 48000.0, 0.32, 0.53), down = pitchAt (a, 48000.0, 0.57, 0.78);
        CHECK (std::fabs (up - 441.0 * std::pow (2.0, 100.0 / 1200.0)) < 4.0);
        CHECK (std::fabs (down - 441.0 * std::pow (2.0, -100.0 / 1200.0)) < 4.0);
    }
    {
        EngineParams p; p.vibAmount = 50.0f; p.vibSpeed = 6.0f; p.vibAttack = 0.5f;
        Rig rig (p, 48000.0);
        rig.synth.noteOn (69, 127);
        auto a = rig.run (1.0);
        // Fade-in: the first 50 ms barely moves; later the pitch swings.
        double lo = 1e9, hi = 0;
        for (double t = 0.6; t < 0.95; t += 0.02) { const double f = pitchAt (a, 48000.0, t, t + 0.02); lo = std::min (lo, f); hi = std::max (hi, f); }
        CHECK (hi / lo > std::pow (2.0, 40.0 / 1200.0));
        CHECK (std::fabs (pitchAt (a, 48000.0, 0.0, 0.05) - 441.0) < 6.0);
    }
}

// ---------------------------------------------------------------------------------
// Tuning analysis

struct TestPreset
{
    std::string name;
    int bank = 0, program = 0;
    std::vector<int16_t> sample;
    int rootKey = 60;
    int fineTune = 0;          // instrument zone
    int presetGlobalFine = 0;  // preset global zone (0 = no global zone)
    bool loop = true;
};

static std::vector<int16_t> tone (double hz, int n, double rate = 44100.0, bool square = false)
{
    std::vector<int16_t> v ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double s = std::sin (2.0 * 3.14159265358979 * hz * i / rate);
        // a touch of 2nd/3rd harmonic so it isn't a pure sine
        const double x = square ? (s >= 0 ? 1.0 : -1.0) : s + 0.3 * std::sin (2.0 * 3.14159265358979 * 2 * hz * i / rate)
                                                            + 0.2 * std::sin (2.0 * 3.14159265358979 * 3 * hz * i / rate);
        v[(size_t) i] = (int16_t) (x * 9000.0);
    }
    return v;
}

static std::vector<int16_t> noise (int n)
{
    std::vector<int16_t> v ((size_t) n);
    uint32_t seed = 12345;
    for (auto& s : v) { seed = seed * 1664525u + 1013904223u; s = (int16_t) ((int) (seed >> 16) - 32768) / 3; }
    return v;
}

static std::vector<uint8_t> buildFont (const std::vector<TestPreset>& presets)
{
    Bytes smpl, phdr, pbag, pgen, inst, ibag, igen, shdr;
    std::vector<uint32_t> starts;
    for (auto& p : presets)
    {
        starts.push_back ((uint32_t) (smpl.d.size() / 2));
        for (auto s : p.sample) smpl.u16 (s & 0xFFFF);
        for (int i = 0; i < 46; ++i) smpl.u16 (0);
    }

    int pbagIdx = 0, pgenIdx = 0, ibagIdx = 0, igenIdx = 0;
    for (size_t i = 0; i < presets.size(); ++i)
    {
        const auto& p = presets[i];
        phdr.str (p.name.c_str(), 20); phdr.u16 (p.program); phdr.u16 (p.bank); phdr.u16 (pbagIdx); phdr.u32 (0); phdr.u32 (0); phdr.u32 (0);
        if (p.presetGlobalFine != 0)
        {
            pbag.u16 (pgenIdx); pbag.u16 (0); ++pbagIdx;
            pgen.u16 (52); pgen.u16 (p.presetGlobalFine & 0xFFFF); ++pgenIdx;
        }
        pbag.u16 (pgenIdx); pbag.u16 (0); ++pbagIdx;
        pgen.u16 (41); pgen.u16 ((int) i); ++pgenIdx;

        inst.str ((p.name + "I").c_str(), 20); inst.u16 (ibagIdx);
        ibag.u16 (igenIdx); ibag.u16 (0); ++ibagIdx;
        if (p.fineTune != 0) { igen.u16 (52); igen.u16 (p.fineTune & 0xFFFF); ++igenIdx; }
        if (p.loop)          { igen.u16 (54); igen.u16 (1); ++igenIdx; }
        igen.u16 (53); igen.u16 ((int) i); ++igenIdx;

        const uint32_t st = starts[i], en = st + (uint32_t) p.sample.size();
        shdr.str ((p.name + "S").c_str(), 20); shdr.u32 (st); shdr.u32 (en);
        shdr.u32 (st + (uint32_t) p.sample.size() / 4); shdr.u32 (en - 8); shdr.u32 (44100);
        shdr.u8 (p.rootKey); shdr.u8 (0); shdr.u16 (0); shdr.u16 (1);
    }
    phdr.str ("EOP", 20); phdr.u16 (0); phdr.u16 (0); phdr.u16 (pbagIdx); phdr.u32 (0); phdr.u32 (0); phdr.u32 (0);
    pbag.u16 (pgenIdx); pbag.u16 (0);
    pgen.u16 (0); pgen.u16 (0);
    inst.str ("EOI", 20); inst.u16 (ibagIdx);
    ibag.u16 (igenIdx); ibag.u16 (0);
    igen.u16 (0); igen.u16 (0);
    shdr.str ("EOS", 20); for (int i = 0; i < 26; ++i) shdr.u8 (0);
    Bytes pmod, imod; for (int i = 0; i < 10; ++i) { pmod.u8 (0); imod.u8 (0); }
    Bytes ifil; ifil.u16 (2); ifil.u16 (1);

    Bytes body; body.id ("sfbk");
    body.append (list ("INFO", { chunk ("ifil", ifil) }));
    body.append (list ("sdta", { chunk ("smpl", smpl) }));
    body.append (list ("pdta", { chunk ("phdr", phdr), chunk ("pbag", pbag), chunk ("pmod", pmod), chunk ("pgen", pgen),
                                 chunk ("inst", inst), chunk ("ibag", ibag), chunk ("imod", imod), chunk ("igen", igen),
                                 chunk ("shdr", shdr) }));
    return chunk ("RIFF", body).d;
}

static const PresetTuning* findTuning (const std::vector<PresetTuning>& r, const std::string& name)
{
    for (auto& t : r) if (t.name == name) return &t;
    return nullptr;
}

static void testPitchDetection()
{
    std::printf ("pitch detection: accurate on tones, low clarity on noise\n");
    for (double hz : { 55.0, 130.81, 261.63, 440.0, 1046.5, 3000.0 })
    {
        std::vector<float> x (8192);
        const auto t = tone (hz, 8192);
        for (size_t i = 0; i < x.size(); ++i) x[i] = t[i] / 32768.0f;
        const auto e = detectPitch (x.data(), (int) x.size(), 44100.0);
        const double err = 1200.0 * std::log2 (e.hz / hz);
        if (std::fabs (err) > 3.0 || e.clarity < 0.9)
        {
            std::printf ("  %.2f Hz -> %.2f Hz (%.1f cents, clarity %.2f)\n", hz, e.hz, err, e.clarity);
            ++failures;
        }
    }
    std::vector<float> x (8192);
    const auto nz = noise (8192);
    for (size_t i = 0; i < x.size(); ++i) x[i] = nz[i] / 32768.0f;
    CHECK (detectPitch (x.data(), (int) x.size(), 44100.0).clarity < 0.6);
}

static void testTuningAnalysis()
{
    std::printf ("tuning analysis: finds wrong roots and detuning, skips kits and noise, fixes round-trip\n");
    const double c5 = 261.6256;
    std::vector<TestPreset> ps;
    ps.push_back ({ "InTune", 0, 0, tone (c5, 22050), 60 });
    ps.push_back ({ "OctaveUp", 0, 1, tone (c5, 22050), 48 });                 // root says C4, sample is C5: plays an octave high
    ps.push_back ({ "Detuned", 0, 2, tone (c5, 22050), 60, 40 });             // +40 cents in the instrument
    ps.push_back ({ "GlobalDetune", 0, 3, tone (c5, 22050, 44100.0, true), 60, 0, -35 });   // -35 cents in a preset global zone
    ps.push_back ({ "Wash", 0, 4, noise (22050), 60, 0, 0, false });
    ps.push_back ({ "Standard", 128, 0, tone (200.0, 22050), 60 });           // bank 128: drum kit
    ps.push_back ({ "FifthLow", 0, 5, tone (c5 * std::pow (2.0, -7.0 / 12.0), 22050), 60 });   // sample is F4, root C5

    const auto bytes = buildFont (ps);
    std::string err;
    auto sf = SoundFont::loadFromMemory (bytes.data(), bytes.size(), err);
    CHECK (sf != nullptr);
    if (! sf) return;

    int calls = 0;
    const auto res = analyzeTuning (*sf, {}, [&] (int, int) { ++calls; return true; });
    CHECK (calls >= (int) ps.size());
    auto status = [&] (const char* n) { auto* t = findTuning (res, n); return t ? t->status : TuneStatus::NoZone; };
    auto corr = [&] (const char* n) { auto* t = findTuning (res, n); return t ? t->correctionCents : 9999; };

    CHECK (status ("InTune") == TuneStatus::InTune);
    CHECK (status ("OctaveUp") == TuneStatus::Adjust);
    CHECK (std::abs (corr ("OctaveUp") + 1200) <= 3);
    CHECK (status ("Detuned") == TuneStatus::Adjust);
    CHECK (std::abs (corr ("Detuned") + 40) <= 3);
    CHECK (status ("GlobalDetune") == TuneStatus::Adjust);
    CHECK (std::abs (corr ("GlobalDetune") - 35) <= 3);
    CHECK (status ("Wash") == TuneStatus::Atonal);
    CHECK (status ("Standard") == TuneStatus::DrumKit);
    CHECK (status ("FifthLow") == TuneStatus::Adjust);
    CHECK (std::abs (corr ("FifthLow") - 700) <= 3);
    CHECK (looksMistuned (res));

    for (auto& t : res)
        if (t.status == TuneStatus::Adjust || t.status == TuneStatus::InTune)
            std::printf ("  %-12s %+8.1f cents\n", t.name.c_str(), t.errorCents);

    // Cancelling returns nothing.
    CHECK (analyzeTuning (*sf, {}, [] (int done, int) { return done < 2; }).empty());

    TuningCorrections fix;
    for (auto& t : res) if (t.status == TuneStatus::Adjust) fix[{ t.bank, t.program }] = t.correctionCents;

    // In memory
    auto fixed = withCorrections (*sf, fix);
    CHECK (&fixed->samples() == &sf->samples());   // shares the pool
    for (auto& t : analyzeTuning (*fixed, {}))
        if (t.status == TuneStatus::Adjust) { std::printf ("  still off after fix: %s %+.1f\n", t.name.c_str(), t.errorCents); ++failures; }

    // On disk
    const std::string src = "lotus_tuning_src.sf2", dst = "lotus_tuning_dst.sf2";
    { std::ofstream f (src, std::ios::binary); f.write ((const char*) bytes.data(), (std::streamsize) bytes.size()); }
    CHECK (writeCorrectedSoundFont (src, dst, fix, err));
    auto reloaded = SoundFont::loadFromFile (dst, err);
    CHECK (reloaded != nullptr);
    if (reloaded)
    {
        CHECK (reloaded->presets.size() == sf->presets.size());
        CHECK (reloaded->samples() == sf->samples());
        for (size_t i = 0; i < sf->presets.size(); ++i)
        {
            const auto& a = sf->presets[i];
            const auto& b = reloaded->presets[i];
            const auto it = fix.find ({ a.bank, a.program });
            const float expect = a.regions[0].tuneCents + (it != fix.end() ? (float) it->second : 0.0f);
            if (std::fabs (b.regions[0].tuneCents - expect) > 0.01f)
            {
                std::printf ("  %s: tune %.1f, expected %.1f\n", a.name.c_str(), b.regions[0].tuneCents, expect);
                ++failures;
            }
        }
        for (auto& t : analyzeTuning (*reloaded, {}))
            CHECK (t.status != TuneStatus::Adjust);
    }
    std::remove (src.c_str());
    std::remove (dst.c_str());

    // Rip-like banks trigger the prompt: many wrong roots, a bank-wide octave shift, a detuned bank.
    {
        std::vector<TestPreset> rip;
        const int roots[] = { 55, 63, 60, 52, 67, 60, 58, 60 };
        for (int i = 0; i < 8; ++i) rip.push_back ({ "R" + std::to_string (i), 0, i, tone (c5, 16000), roots[i] });
        const auto rb = buildFont (rip);
        auto rsf = SoundFont::loadFromMemory (rb.data(), rb.size(), err);
        CHECK (rsf != nullptr && looksMistuned (analyzeTuning (*rsf, { false, 25.0 })));

        std::vector<TestPreset> oct;
        for (int i = 0; i < 6; ++i) oct.push_back ({ "O" + std::to_string (i), 0, i, tone (c5 * std::pow (2.0, i / 12.0), 16000), 72 + i });
        const auto ob = buildFont (oct);
        auto osf = SoundFont::loadFromMemory (ob.data(), ob.size(), err);
        CHECK (osf != nullptr);
        if (osf)
        {
            const auto r = analyzeTuning (*osf, {});
            CHECK (looksMistuned (r));
            for (auto& t : r) CHECK (t.status == TuneStatus::Adjust && t.kind == AdjustKind::Octave && t.recommended);
        }

        std::vector<TestPreset> flat;
        for (int i = 0; i < 5; ++i) flat.push_back ({ "F" + std::to_string (i), 0, i, tone (c5 * std::pow (2.0, (i - 32.0) / 1200.0), 16000), 60 });
        const auto fb = buildFont (flat);
        auto fsf = SoundFont::loadFromMemory (fb.data(), fb.size(), err);
        CHECK (fsf != nullptr && looksMistuned (analyzeTuning (*fsf, { false, 25.0 })));
    }

    // A lone octave offset (an organ footage, say) is offered but not selected.
    {
        std::vector<TestPreset> org { { "Piano", 0, 0, tone (c5, 16000), 60 }, { "Strings", 0, 1, tone (c5, 16000), 60 },
                                      { "Organ", 0, 16, tone (c5 / 2, 16000), 60 } };
        const auto b = buildFont (org);
        auto osf = SoundFont::loadFromMemory (b.data(), b.size(), err);
        CHECK (osf != nullptr);
        if (osf)
        {
            const auto r = analyzeTuning (*osf, {});
            auto* t = findTuning (r, "Organ");
            CHECK (t != nullptr && t->status == TuneStatus::Adjust && t->kind == AdjustKind::Octave && ! t->recommended);
            CHECK (! looksMistuned (r));
        }
    }

    // A healthy font doesn't trigger the prompt.
    std::vector<TestPreset> good { { "A", 0, 0, tone (c5, 22050), 60 }, { "B", 0, 1, tone (c5 * 2, 22050), 72 },
                                   { "Kit", 128, 0, noise (8000), 60, 0, 0, false } };
    const auto gb = buildFont (good);
    auto gsf = SoundFont::loadFromMemory (gb.data(), gb.size(), err);
    CHECK (gsf != nullptr);
    if (gsf) CHECK (! looksMistuned (analyzeTuning (*gsf, { false, 15.0 })));
}

static void testNoteColours()
{
    std::printf ("note colours / MIDI channels pick their own preset\n");
    EngineParams p;
    p.cutOnNoteOff = true;
    Rig rig (p);
    CHECK (rig.synth.presetForColour (2) == 0);   // follows the main preset by default
    rig.synth.setColourPreset (2, 1);            // colour 3 plays the kit
    CHECK (rig.synth.presetForColour (2) == 1);
    rig.synth.setColourPreset (5, 99);           // out of range falls back to the main preset
    CHECK (rig.synth.presetForColour (5) == 0);

    rig.synth.noteOn (69, 127, 2);               // the kit has nothing on key 69
    rig.run (0.01);
    CHECK (rig.synth.getActiveVoiceCount() == 0);
    const uint8_t kitOn[] = { 0x92, 42, 127 };   // MIDI channel 3 = colour 3
    rig.synth.handleMidi (kitOn, 3);
    rig.run (0.01);
    CHECK (rig.synth.getActiveVoiceCount() == 1);

    // Same key on two channels: a note-off only ends its own channel.
    rig.synth.allSoundOff();
    rig.synth.noteOn (69, 127, 0);
    rig.synth.noteOn (69, 127, 3);
    rig.run (0.01);
    CHECK (rig.synth.getActiveVoiceCount() == 2);
    rig.synth.noteOff (69, 3);
    rig.run (0.02);
    CHECK (rig.synth.getActiveVoiceCount() == 1);

    // A program change on a channel with its own slot only changes that slot.
    const uint8_t pc[] = { 0xC2, 0 };
    rig.synth.handleMidi (pc, 2);
    CHECK (rig.synth.getColourPreset (2) == 0);
    CHECK (rig.synth.getPresetIndex() == 0);
}

static void testHostNotes()
{
    std::printf ("host-driven notes follow live pitch and level changes (FL slides)\n");
    EngineParams p;
    p.cutOnNoteOff = true;
    Rig rig (p);
    const uint32_t id = rig.synth.hostNoteOn (69, 100, 0, 0.0f, 1.0f, 0.0f);
    CHECK (id != 0);
    rig.run (0.05);
    auto a = rig.run (0.2);
    CHECK (std::fabs (zeroCrossFreq (a, rig.rate) - 441.0) < 5.0);

    rig.synth.hostNoteLevels (id, 1200.0f, 1.0f, 0.0f);   // slide up an octave
    rig.run (0.05);
    auto b = rig.run (0.2);
    CHECK (std::fabs (zeroCrossFreq (b, rig.rate) - 882.0) < 8.0);

    rig.synth.hostNoteLevels (id, 1200.0f, 0.5f, 0.0f);   // host volume halves the level
    rig.run (0.05);
    auto c = rig.run (0.1);
    CHECK (std::fabs (peak (c, 0, c.size()) / peak (b, 0, b.size()) - 0.5f) < 0.05f);

    rig.synth.noteOff (69);                    // MIDI note-offs don't touch host notes
    rig.run (0.01);
    CHECK (rig.synth.isNoteSounding (id));
    rig.synth.hostNoteOff (id);
    rig.run (0.02);
    CHECK (! rig.synth.isNoteSounding (id));

    // Kill fades out quickly even with a long release.
    EngineParams q;
    q.release = 2.0f;
    Rig rig2 (q);
    const uint32_t id2 = rig2.synth.hostNoteOn (69, 100, 0, 0.0f, 1.0f, 0.0f);
    rig2.run (0.05);
    rig2.synth.hostNoteOff (id2);
    rig2.run (0.05);
    CHECK (rig2.synth.isNoteSounding (id2));   // still in its release
    rig2.synth.hostNoteKill (id2);
    rig2.run (0.02);
    CHECK (! rig2.synth.isNoteSounding (id2));
}

int main()
{
    testParser();
    testTables();
    testCutOnNoteOff();
    testSf2ReleaseIsLong();
    testZeroReleaseIsInstant();
    testAttackDecaySustain();
    testSustainPedal();
    testExclusiveClass();
    testProgramChange();
    testNoteColours();
    testHostNotes();
    testConsoles();
    testSnesEcho();
    testPs1Reverb();
    testEffectsInEngine();
    testCodecs();
    testNativeFormatInEngine();
    testN64Reverb();
    testVoiceLimitAndGlide();
    testAnalogStage();
    testVoiceRegistry();
    testVibratoAndTuning();
    testPitchDetection();
    testTuningAnalysis();

    if (failures == 0) std::printf ("\nAll tests passed.\n");
    else std::printf ("\n%d check(s) failed.\n", failures);
    return failures == 0 ? 0 : 1;
}
