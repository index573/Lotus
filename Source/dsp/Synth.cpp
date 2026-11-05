#include "Synth.h"

#include <algorithm>
#include <cmath>

namespace lotus
{

namespace
{
constexpr int64_t fracOne = int64_t (1) << 32;
constexpr double dsTimerClock = 33513982.0 / 2.0;   // 16.756991 MHz

inline float hermite (float xm1, float x0, float x1, float x2, float t)
{
    const float c = (x1 - xm1) * 0.5f;
    const float v = x0 - x1;
    const float w = c + v;
    const float a = w + v + (x2 - x0) * 0.5f;
    const float b = w + a;
    return ((a * t - b) * t + c) * t + x0;
}
} // namespace

Synth::Synth()
{
    profile = &getConsoleProfile (ConsoleId::Modern);
}

void Synth::prepare (double hostSampleRate, int maxBlockSize)
{
    hostRate = hostSampleRate > 0 ? hostSampleRate : 44100.0;
    maxBlock = std::max (32, maxBlockSize);

    // Enough room for the fastest console mixer relative to the host rate.
    double fastest = hostRate;
    for (int i = 0; i < numConsoles; ++i)
        fastest = std::max (fastest, getConsoleProfile ((ConsoleId) i).engineRate);

    const size_t cap = (size_t) std::ceil (maxBlock * (fastest / hostRate)) + 16;
    engineL.assign (cap, 0.0f);
    engineR.assign (cap, 0.0f);
    for (auto* b : { &engineDryL, &engineDryR, &engineWetL, &engineWetR })
        b->assign (cap, 0.0f);

    // Effect buffers sized for the fastest possible engine rate, so console
    // switches never allocate on the audio thread.
    snesEcho.prepare (std::max (hostRate, 44100.0));
    ps1Reverb.prepare (std::max (hostRate, 44100.0));

    allSoundOff();
    configureConsole();
}

void Synth::configureConsole()
{
    profile = &getConsoleProfile (activeConsole);
    engineRate = profile->engineRate > 0 ? profile->engineRate : hostRate;
    passthrough = profile->engineRate <= 0;
    engineStep = engineRate / hostRate;
    enginePhase = 0.0;
    snesEcho.setRate (engineRate);
    ps1Reverb.setRate (engineRate);
    std::fill (std::begin (histL), std::end (histL), 0.0f);
    std::fill (std::begin (histR), std::end (histR), 0.0f);
}

void Synth::setSoundFont (const SoundFont* sf)
{
    allSoundOff();
    soundFont = sf;
    presetIndex = 0;
}

void Synth::setPresetIndex (int index)
{
    if (soundFont == nullptr || soundFont->presets.empty()) { presetIndex = 0; return; }
    presetIndex = std::clamp (index, 0, (int) soundFont->presets.size() - 1);
}

void Synth::setParams (const EngineParams& p)
{
    params = p;
    if (p.console != activeConsole)
    {
        // Envelope and pitch state is tied to the mixer rate, so a console switch
        // restarts the engine cleanly rather than glitching sounding notes.
        activeConsole = p.console;
        allSoundOff();
        configureConsole();
    }
}

// ---------------------------------------------------------------------------------
// MIDI

void Synth::handleMidi (const uint8_t* d, int size)
{
    if (size < 1) return;
    const int status = d[0] & 0xF0;
    const int d1 = size > 1 ? d[1] & 0x7F : 0;
    const int d2 = size > 2 ? d[2] & 0x7F : 0;

    switch (status)
    {
        case 0x90: if (d2 > 0) noteOn (d1, d2); else noteOff (d1); break;
        case 0x80: noteOff (d1); break;
        case 0xE0: pitchBend = (float) (((d2 << 7) | d1) - 8192) / 8192.0f; break;

        case 0xC0:
            if (soundFont != nullptr)
            {
                // GM convention: bank 128 is percussion; most SF2 drum kits live there.
                int idx = soundFont->findPreset (bankMsb * 128 + bankLsb, d1);
                if (idx < 0) idx = soundFont->findPreset (bankMsb, d1);
                if (idx < 0) idx = soundFont->findPreset (0, d1);
                if (idx >= 0 && idx != presetIndex.load())
                {
                    allNotesOff();
                    presetIndex = idx;
                    presetChangedByMidi = true;
                }
            }
            break;

        case 0xB0:
            switch (d1)
            {
                case 0:  bankMsb = d2; break;
                case 32: bankLsb = d2; break;
                case 7:  ccVolume = (float) (d2 * d2) / (127.0f * 127.0f); break;
                case 11: ccExpression = (float) (d2 * d2) / (127.0f * 127.0f); break;
                case 10: ccPan = std::clamp ((float) (d2 - 64) / 63.0f, -1.0f, 1.0f); break;
                case 64:
                    sustainPedal = d2 >= 64;
                    if (! sustainPedal)
                        for (auto& v : voices)
                            if (v.active && v.sustained) { v.sustained = false; releaseVoice (v); }
                    break;
                case 120: allSoundOff(); break;
                case 121:
                    pitchBend = 0.0f; ccVolume = 1.0f; ccExpression = 1.0f; ccPan = 0.0f;
                    sustainPedal = false;
                    break;
                case 123: allNotesOff(); break;
                default: break;
            }
            break;

        default: break;
    }
}

void Synth::noteOn (int key, int velocity)
{
    if (soundFont == nullptr || soundFont->presets.empty()) return;
    const auto& preset = soundFont->presets[(size_t) std::clamp (presetIndex.load(), 0, (int) soundFont->presets.size() - 1)];

    // Re-striking a key ends whatever that key was already playing.
    for (auto& v : voices)
        if (v.active && v.key == key && ! v.released)
        {
            v.sustained = false;
            releaseVoice (v);
        }

    const uint32_t noteId = ++noteCounter;

    for (const auto& r : preset.regions)
    {
        if (key < r.loKey || key > r.hiKey || velocity < r.loVel || velocity > r.hiVel) continue;

        if (r.exclusiveClass != 0)
            for (auto& v : voices)
                if (v.active && v.noteId != noteId && v.region->exclusiveClass == r.exclusiveClass)
                {
                    v.released = true;
                    v.env.cut();
                }

        startVoice (r, key, velocity, noteId);
    }
}

void Synth::noteOff (int key)
{
    for (auto& v : voices)
    {
        if (! v.active || v.key != key || ! v.held) continue;
        v.held = false;
        if (sustainPedal) v.sustained = true;
        else releaseVoice (v);
    }
}

void Synth::releaseVoice (Voice& v)
{
    if (v.released) return;
    v.released = true;
    v.held = false;

    if (params.cutOnNoteOff) v.env.cut();
    else if (params.useSf2Envelope) v.env.release();
    else v.env.release (params.release);
}

void Synth::allNotesOff()
{
    for (auto& v : voices)
        if (v.active) { v.sustained = false; releaseVoice (v); }
}

void Synth::allSoundOff()
{
    for (auto& v : voices) v.active = false;
    activeVoices = 0;
}

Synth::Voice& Synth::allocateVoice()
{
    for (auto& v : voices)
        if (! v.active) return v;

    // Steal: the quietest releasing voice, otherwise the oldest.
    Voice* best = nullptr;
    for (auto& v : voices)
        if (v.released && (best == nullptr || v.env.getLevel() < best->env.getLevel()))
            best = &v;
    if (best != nullptr) return *best;

    best = &voices[0];
    for (auto& v : voices)
        if (v.age < best->age) best = &v;
    return *best;
}

void Synth::startVoice (const Region& r, int key, int velocity, uint32_t noteId)
{
    Voice& v = allocateVoice();
    v.active = true;
    v.region = &r;
    v.key = key;
    v.pitchKey = r.fixedKey >= 0 ? r.fixedKey : key;
    v.noteId = noteId;
    v.age = ++ageCounter;
    v.held = true;
    v.sustained = false;
    v.released = false;
    v.hasLooped = false;
    v.pos = (int64_t) r.start << 32;

    const int vel = r.fixedVelocity > 0 ? r.fixedVelocity : velocity;
    const float velGain = (float) (vel * vel) / (127.0f * 127.0f);   // ~ SF2 default 40*log10 curve
    v.baseGain = r.gain * velGain;
    v.pan = r.pan;

    Envelope::Params ep;
    if (params.useSf2Envelope)
    {
        const auto& e = r.volEnv;
        const float keyOffset = (float) (60 - v.pitchKey);
        ep.delay = e.delay;
        ep.attack = e.attack;
        ep.hold = e.hold * std::pow (2.0f, e.keyToHold * keyOffset / 1200.0f);
        ep.decay = e.decay * std::pow (2.0f, e.keyToDecay * keyOffset / 1200.0f);
        ep.sustain = e.sustain;
        ep.release = e.release;
        ep.sf2Semantics = true;
    }
    else
    {
        ep.attack = params.attack;
        ep.decay = params.decay;
        ep.sustain = params.sustain;
        ep.release = params.release;
    }
    v.env.start (ep, engineRate);

    updatePitch (v);
}

void Synth::updatePitch (Voice& v) const
{
    const Region& r = *v.region;
    const double cents = (v.pitchKey - r.rootKey) * (double) r.scaleTuning + r.tuneCents
                       + (double) pitchBend * params.bendRange * 100.0;
    const double playbackHz = r.sampleRate * std::pow (2.0, cents / 1200.0);

    double ratio = 0.0;  // source samples per engine sample
    switch (profile->pitch)
    {
        case PitchMode::Free:
            ratio = playbackHz / engineRate;
            break;

        case PitchMode::Fixed4p12:
        {
            int64_t reg = std::llround (playbackHz / engineRate * 4096.0);
            reg = std::clamp<int64_t> (reg, 0, (int64_t) profile->maxStep4p12);
            v.step = reg << 20;   // 4.12 -> 32.32, keeps the exact hardware step
            return;
        }

        case PitchMode::DsTimer:
        {
            const int64_t timer = std::clamp<int64_t> (std::llround (dsTimerClock / std::max (1.0, playbackHz)), 1, 65535);
            ratio = (dsTimerClock / (double) timer) / engineRate;
            break;
        }
    }

    ratio = std::clamp (ratio, 0.0, 256.0);
    v.step = (int64_t) (ratio * (double) fracOne);
}

// ---------------------------------------------------------------------------------
// Rendering

template <Interp interp>
void Synth::renderVoice (Voice& v, float* outL, float* outR, int n, float gainL, float gainR)
{
    const Region& r = *v.region;
    const int16_t* pool = soundFont->samples.data();
    const int64_t start = r.start, end = r.end, loopStart = r.loopStart, loopEnd = r.loopEnd;
    const int64_t loopLen = loopEnd - loopStart;
    const bool eightBit = profile->sampleBits == 8;

    for (int i = 0; i < n; ++i)
    {
        const bool looping = r.loopMode == LoopMode::Continuous
                          || (r.loopMode == LoopMode::UntilRelease && ! v.released);

        auto fetch = [&] (int64_t idx) -> int
        {
            if (looping)
            {
                if (idx >= loopEnd) idx = loopStart + (idx - loopStart) % loopLen;
                else if (v.hasLooped && idx < loopStart) idx += loopLen;   // taps behind the loop point
            }
            if (idx < start || idx >= end) return 0;
            const int s = pool[idx];
            return eightBit ? (s & ~0xFF) : s;
        };

        const int64_t idx = v.pos >> 32;
        const uint32_t frac = (uint32_t) (v.pos & 0xFFFFFFFF);
        float s = 0.0f;

        if constexpr (interp == Interp::Nearest)
        {
            s = (float) fetch (idx) * (1.0f / 32768.0f);
        }
        else if constexpr (interp == Interp::Linear)
        {
            const float t = (float) frac * (1.0f / 4294967296.0f);
            const float a = (float) fetch (idx), b = (float) fetch (idx + 1);
            s = (a + (b - a) * t) * (1.0f / 32768.0f);
        }
        else if constexpr (interp == Interp::Hermite)
        {
            const float t = (float) frac * (1.0f / 4294967296.0f);
            s = hermite ((float) fetch (idx - 1), (float) fetch (idx), (float) fetch (idx + 1), (float) fetch (idx + 2), t)
                * (1.0f / 32768.0f);
        }
        else
        {
            // 4-tap Gaussian, as the hardware does it: bits 4..11 of the 12-bit pitch
            // counter fraction pick the coefficients; integer maths with per-tap shifts.
            const auto& g = interp == Interp::SnesGauss ? snesGaussTable() : ps1GaussTable();
            constexpr int shift = interp == Interp::SnesGauss ? 11 : 15;
            const int gi = (int) (frac >> 24);
            int out = (g[(size_t) (255 - gi)] * fetch (idx - 1)) >> shift;
            out += (g[(size_t) (511 - gi)] * fetch (idx)) >> shift;
            out += (g[(size_t) (256 + gi)] * fetch (idx + 1)) >> shift;
            out += (g[(size_t) gi] * fetch (idx + 2)) >> shift;
            out = std::clamp (out, -32768, 32767);
            if constexpr (interp == Interp::SnesGauss) out &= ~1;
            s = (float) out * (1.0f / 32768.0f);
        }

        const float e = v.env.next();
        outL[i] += s * e * gainL;
        outR[i] += s * e * gainR;

        if (v.env.isDone()) { v.active = false; return; }

        v.pos += v.step;
        if (looping)
        {
            if ((v.pos >> 32) >= loopEnd)
            {
                v.pos -= (((v.pos >> 32) - loopStart) / loopLen) * loopLen << 32;
                v.hasLooped = true;
            }
        }
        else if ((v.pos >> 32) >= end)
        {
            v.active = false;
            return;
        }
    }
}

void Synth::renderEngine (float* outL, float* outR, int n)
{
    std::fill (outL, outL + n, 0.0f);
    std::fill (outR, outR + n, 0.0f);
    if (n <= 0) return;
    if (soundFont == nullptr) { applyEffectsAndOutputStage (outL, outR, n); return; }

    const float channelGain = params.masterGain * ccVolume * ccExpression;
    int count = 0;

    for (auto& v : voices)
    {
        if (! v.active) continue;

        updatePitch (v);   // picks up pitch bend

        // Balance pan law: centre is unity, hard-panned stereo pairs stay at unity.
        const float p = std::clamp (v.pan + ccPan, -1.0f, 1.0f);
        const float g = v.baseGain * channelGain;
        const float gl = g * std::min (1.0f, 1.0f - p);
        const float gr = g * std::min (1.0f, 1.0f + p);

        switch (profile->interp)
        {
            case Interp::Nearest:   renderVoice<Interp::Nearest>   (v, outL, outR, n, gl, gr); break;
            case Interp::Linear:    renderVoice<Interp::Linear>    (v, outL, outR, n, gl, gr); break;
            case Interp::Hermite:   renderVoice<Interp::Hermite>   (v, outL, outR, n, gl, gr); break;
            case Interp::SnesGauss: renderVoice<Interp::SnesGauss> (v, outL, outR, n, gl, gr); break;
            case Interp::Ps1Gauss:  renderVoice<Interp::Ps1Gauss>  (v, outL, outR, n, gl, gr); break;
        }

        if (v.active) ++count;
    }

    activeVoices.store (count, std::memory_order_relaxed);
    applyEffectsAndOutputStage (outL, outR, n);
}

void Synth::applyEffectsAndOutputStage (float* outL, float* outR, int n)
{
    // Console effects are fed from the voice mix and added back to it, as on the
    // hardware (where echo/reverb returns are summed with the dry voices).
    if (params.fx.snesEcho || params.fx.ps1Reverb)
    {
        // Run both from the same dry signal, then sum, so they're parallel sends.
        float* dryL = engineDryL.data();
        float* dryR = engineDryR.data();
        std::copy (outL, outL + n, dryL);
        std::copy (outR, outR + n, dryR);

        if (params.fx.snesEcho)
        {
            if (! echoWasOn) snesEcho.reset();
            snesEcho.process (outL, outR, n, params.fx);
        }
        if (params.fx.ps1Reverb)
        {
            if (! reverbWasOn) ps1Reverb.reset();
            float* wetL = engineWetL.data();
            float* wetR = engineWetR.data();
            std::copy (dryL, dryL + n, wetL);
            std::copy (dryR, dryR + n, wetR);
            ps1Reverb.process (wetL, wetR, n, params.fx);
            for (int i = 0; i < n; ++i) { outL[i] += wetL[i] - dryL[i]; outR[i] += wetR[i] - dryR[i]; }
        }
    }
    echoWasOn = params.fx.snesEcho;
    reverbWasOn = params.fx.ps1Reverb;

    // Hardware output stage: fixed bit depth with hard clipping.
    if (profile->outputBits > 0)
    {
        const float q = (float) (1 << (profile->outputBits - 1));
        for (int i = 0; i < n; ++i)
        {
            outL[i] = std::clamp (std::floor (outL[i] * q + 0.5f), -q, q - 1.0f) / q;
            outR[i] = std::clamp (std::floor (outR[i] * q + 0.5f), -q, q - 1.0f) / q;
        }
    }
}

void Synth::render (float* left, float* right, int numSamples)
{
    while (numSamples > 0)
    {
        const int n = std::min (numSamples, maxBlock);

        if (passthrough)
        {
            renderEngine (left, right, n);
        }
        else
        {
            // How many mixer-rate samples this block needs.
            int needed = 0;
            {
                double ph = enginePhase;
                for (int i = 0; i < n; ++i)
                {
                    ph += engineStep;
                    while (ph >= 1.0) { ph -= 1.0; ++needed; }
                }
            }
            needed = std::min (needed, (int) engineL.size());
            renderEngine (engineL.data(), engineR.data(), needed);

            // Reconstruct at the host rate (4-point Hermite between the two middle points).
            int consumed = 0;
            for (int i = 0; i < n; ++i)
            {
                enginePhase += engineStep;
                while (enginePhase >= 1.0)
                {
                    enginePhase -= 1.0;
                    const float nl = consumed < needed ? engineL[(size_t) consumed] : 0.0f;
                    const float nr = consumed < needed ? engineR[(size_t) consumed] : 0.0f;
                    ++consumed;
                    histL[0] = histL[1]; histL[1] = histL[2]; histL[2] = histL[3]; histL[3] = nl;
                    histR[0] = histR[1]; histR[1] = histR[2]; histR[2] = histR[3]; histR[3] = nr;
                }
                const float t = (float) enginePhase;
                left[i]  = hermite (histL[0], histL[1], histL[2], histL[3], t);
                right[i] = hermite (histR[0], histR[1], histR[2], histR[3], t);
            }
        }

        left += n; right += n; numSamples -= n;
    }
}

} // namespace lotus
