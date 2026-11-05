// Lotus - headless engine tests.
//
// Builds a SoundFont in memory (so no binary fixtures are needed), then checks the
// parser and the behaviours Lotus promises: notes that really stop, ADSR times
// that mean what they say, and console modes that render sensibly.

#include "../Source/dsp/Codecs.h"
#include "../Source/dsp/Synth.h"

#include <cmath>
#include <cstdio>
#include <atomic>
#include <cstring>
#include <string>
#include <vector>

using namespace lotus;

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { std::printf ("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

// ---------------------------------------------------------------------------------
// Minimal SF2 writer

struct Bytes
{
    std::vector<uint8_t> d;
    void u8 (int v) { d.push_back ((uint8_t) v); }
    void u16 (int v) { u8 (v & 0xFF); u8 ((v >> 8) & 0xFF); }
    void u32 (uint32_t v) { u16 ((int) (v & 0xFFFF)); u16 ((int) (v >> 16)); }
    void str (const char* s, size_t n) { for (size_t i = 0; i < n; ++i) u8 (i < std::strlen (s) ? s[i] : 0); }
    void id (const char* s) { str (s, 4); }
    void append (const Bytes& b) { d.insert (d.end(), b.d.begin(), b.d.end()); }
};

static Bytes chunk (const char* id, const Bytes& body)
{
    Bytes b; b.id (id); b.u32 ((uint32_t) body.d.size()); b.append (body);
    if (body.d.size() & 1) b.u8 (0);
    return b;
}

static Bytes list (const char* type, const std::vector<Bytes>& chunks)
{
    Bytes body; body.id (type);
    for (auto& c : chunks) body.append (c);
    return chunk ("LIST", body);
}

struct TestFont
{
    std::vector<uint8_t> data;
    uint32_t sampleLen = 0;
};

/** One preset ("Square", bank 0 prog 0) and a drum-ish preset (bank 0 prog 1) with
    an exclusive class. Sample: a looped 100-sample square wave, root key 69, 44.1 kHz. */
static TestFont makeTestFont (int releaseTimecents = 2400 /* ~4 s: the "annoying" long release */)
{
    const uint32_t len = 1000, loopS = 100, loopE = 900;
    Bytes smpl;
    for (uint32_t i = 0; i < len + 46; ++i)
        smpl.u16 (i < len ? (((i / 50) % 2) ? 16000 : -16000) & 0xFFFF : 0);

    // phdr: Square, Kick, EOP
    Bytes phdr;
    phdr.str ("Square", 20); phdr.u16 (0); phdr.u16 (0); phdr.u16 (0); phdr.u32 (0); phdr.u32 (0); phdr.u32 (0);
    phdr.str ("Kit", 20);    phdr.u16 (1); phdr.u16 (0); phdr.u16 (1); phdr.u32 (0); phdr.u32 (0); phdr.u32 (0);
    phdr.str ("EOP", 20);    phdr.u16 (0); phdr.u16 (0); phdr.u16 (2); phdr.u32 (0); phdr.u32 (0); phdr.u32 (0);

    Bytes pbag; pbag.u16 (0); pbag.u16 (0); pbag.u16 (1); pbag.u16 (0); pbag.u16 (2); pbag.u16 (0);
    Bytes pmod; for (int i = 0; i < 10; ++i) pmod.u8 (0);
    Bytes pgen;
    pgen.u16 (41); pgen.u16 (0);   // preset 0 -> instrument 0
    pgen.u16 (41); pgen.u16 (1);   // preset 1 -> instrument 1
    pgen.u16 (0);  pgen.u16 (0);   // terminator

    Bytes inst;
    inst.str ("SquareInst", 20); inst.u16 (0);
    inst.str ("KitInst", 20);    inst.u16 (1);
    inst.str ("EOI", 20);        inst.u16 (3);

    // ibag 0: square zone; ibag 1, 2: two kit zones (keys 42 & 46, exclusive class 1)
    Bytes ibag; int g = 0;
    ibag.u16 (g); ibag.u16 (0); g += 3;
    ibag.u16 (g); ibag.u16 (0); g += 3;
    ibag.u16 (g); ibag.u16 (0); g += 3;
    ibag.u16 (g); ibag.u16 (0);
    Bytes imod; for (int i = 0; i < 10; ++i) imod.u8 (0);

    Bytes igen;
    igen.u16 (38); igen.u16 (releaseTimecents & 0xFFFF);  // releaseVolEnv
    igen.u16 (54); igen.u16 (1);                          // sampleModes: loop
    igen.u16 (53); igen.u16 (0);                          // sampleID
    igen.u16 (43); igen.u16 (42 | (42 << 8));             // keyRange 42
    igen.u16 (57); igen.u16 (1);                          // exclusiveClass 1
    igen.u16 (53); igen.u16 (0);
    igen.u16 (43); igen.u16 (46 | (46 << 8));             // keyRange 46
    igen.u16 (57); igen.u16 (1);
    igen.u16 (53); igen.u16 (0);
    igen.u16 (0);  igen.u16 (0);

    Bytes shdr;
    shdr.str ("square", 20); shdr.u32 (0); shdr.u32 (len); shdr.u32 (loopS); shdr.u32 (loopE); shdr.u32 (44100);
    shdr.u8 (69); shdr.u8 (0); shdr.u16 (0); shdr.u16 (1);
    shdr.str ("EOS", 20); for (int i = 0; i < 26; ++i) shdr.u8 (0);

    Bytes ifil; ifil.u16 (2); ifil.u16 (1);
    Bytes inam; inam.str ("Lotus Test", 12);

    Bytes body; body.id ("sfbk");
    body.append (list ("INFO", { chunk ("ifil", ifil), chunk ("INAM", inam) }));
    body.append (list ("sdta", { chunk ("smpl", smpl) }));
    body.append (list ("pdta", { chunk ("phdr", phdr), chunk ("pbag", pbag), chunk ("pmod", pmod), chunk ("pgen", pgen),
                                 chunk ("inst", inst), chunk ("ibag", ibag), chunk ("imod", imod), chunk ("igen", igen),
                                 chunk ("shdr", shdr) }));
    Bytes riff = chunk ("RIFF", body);
    return { riff.d, len };
}

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
    std::vector<int16_t> silent (rig.sf->samples.size(), 0);
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
    testConsoles();
    testSnesEcho();
    testPs1Reverb();
    testEffectsInEngine();
    testCodecs();
    testNativeFormatInEngine();
    testN64Reverb();
    testVoiceLimitAndGlide();
    testAnalogStage();

    if (failures == 0) std::printf ("\nAll tests passed.\n");
    else std::printf ("\n%d check(s) failed.\n", failures);
    return failures == 0 ? 0 : 1;
}
