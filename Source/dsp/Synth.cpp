#include "Synth.h"

#include <algorithm>
#include <cmath>

namespace lotus
{

namespace
{
constexpr int64_t fracOne = int64_t (1) << 32;
constexpr double dsTimerClock = 33513982.0 / 2.0;

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
    for (auto* b : { &dryL, &dryR, &wetL, &wetR })
        b->assign (cap, 0.0f);

    // Sized for the fastest engine rate so console switches never allocate.
    snesEcho.prepare (fastest);
    ps1Reverb.prepare (fastest);
    n64Reverb.prepare (fastest);

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
    n64Reverb.setRate (engineRate);
    analog.configure (hostRate, profile->lowpassHz, profile->highpassHz[0], profile->highpassHz[1]);
    std::fill (std::begin (histL), std::end (histL), 0.0f);
    std::fill (std::begin (histR), std::end (histR), 0.0f);
}

void Synth::setSoundFont (const SoundFont* sf)
{
    allSoundOff();
    soundFont = sf;
    presetIndex = 0;
}

void Synth::setCodedPool (const std::vector<int16_t>* pool, Codec codec)
{
    codedPool = pool;
    codedPoolCodec = pool != nullptr ? codec : Codec::None;
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
        // Envelope and pitch state is tied to the mixer rate, so restart cleanly.
        activeConsole = p.console;
        allSoundOff();
        configureConsole();
    }
}

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
    lastKey = key;
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
    // With the hardware limit on, a console with N voices steals once N are sounding.
    const int limit = (params.hardwareVoices && profile->voiceLimit > 0) ? std::min (profile->voiceLimit, maxVoices) : maxVoices;
    int sounding = 0;
    for (auto& v : voices) sounding += v.active ? 1 : 0;

    if (sounding < limit)
        for (auto& v : voices)
            if (! v.active) return v;

    // Steal the quietest releasing voice, otherwise the oldest.
    Voice* best = nullptr;
    for (auto& v : voices)
        if (v.active && v.released && (best == nullptr || v.env.getLevel() < best->env.getLevel()))
            best = &v;
    if (best != nullptr) return *best;

    for (auto& v : voices)
        if (v.active && (best == nullptr || v.age < best->age)) best = &v;
    return best != nullptr ? *best : voices[0];
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
    const float velGain = (float) (vel * vel) / (127.0f * 127.0f);   // ~ the SF2 default velocity curve
    v.baseGain = r.gain * velGain;
    v.pan = r.pan;

    v.glideCents = 0.0f;
    v.glideRate = 0.0f;
    v.vibTime = 0.0;
    v.vibCents = 0.0f;
    if (params.glide > 0.0f && lastKey >= 0 && lastKey != key)
    {
        v.glideCents = (float) (lastKey - key) * 100.0f;
        v.glideRate = std::abs (v.glideCents) / (float) (params.glide * engineRate);
    }

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
                       + (double) pitchBend * params.bendRange * 100.0 + v.glideCents
                       + v.vibCents + params.tuneCents;
    const double rel = std::pow (2.0, cents / 1200.0);
    const double playbackHz = r.sampleRate * rel;

    double ratio = 0.0;  // source samples per engine sample
    switch (profile->pitch)
    {
        case PitchMode::Free:
            ratio = playbackHz / engineRate;
            break;

        case PitchMode::Fixed4p12:
        {
            // Pitch ceilings apply to the sample as a developer would have converted it
            // (to the mixer rate), i.e. relative to the root note.
            const double capped = std::min (rel, profile->maxStep4p12 / 4096.0);
            const int64_t reg = std::llround (capped * r.sampleRate / engineRate * 4096.0);
            v.step = std::max<int64_t> (0, reg) << 20;
            return;
        }

        case PitchMode::DsTimer:
        {
            const int64_t timer = std::clamp<int64_t> (std::llround (dsTimerClock / std::max (1.0, playbackHz)), 1, 65535);
            ratio = (dsTimerClock / (double) timer) / engineRate;
            break;
        }

        case PitchMode::N64Q15:
        {
            // libultra caps the ratio just under 2.0 and truncates it to Q1.15.
            const double capped = std::min (rel, 1.99996) * r.sampleRate / engineRate;
            ratio = std::floor (capped * 32768.0) / 32768.0;
            break;
        }

        case PitchMode::AmigaPeriod:
        {
            // Pitch snaps to integer periods. Above Paula's fastest rate the pitch is
            // kept and the sample effectively plays decimated.
            const int64_t period = std::clamp<int64_t> (std::llround (amigaPalClock / std::max (1.0, playbackHz)), 113, 65535);
            const double hz = period == 113 ? playbackHz : amigaPalClock / (double) period;
            ratio = hz / engineRate;
            break;
        }

        case PitchMode::NesDmc:
        {
            // The sample is treated as already converted to the fastest DMC rate; notes
            // snap to whichever of the 16 rates is nearest.
            const double fastest = nesCpuClock / nesDmcPeriods[15];
            const double target = fastest * rel;
            int best = 15;
            for (int k = 0; k < 16; ++k)
                if (std::abs (std::log (nesCpuClock / nesDmcPeriods[k] / target))
                    < std::abs (std::log (nesCpuClock / nesDmcPeriods[best] / target)))
                    best = k;
            ratio = r.sampleRate * (nesCpuClock / nesDmcPeriods[best] / fastest) / engineRate;
            break;
        }

        case PitchMode::ScspOctFns:
        {
            const double want = std::max (1e-6, playbackHz / 44100.0);
            int oct = (int) std::floor (std::log2 (want));
            int fns = (int) std::lround ((want / std::ldexp (1.0, oct) - 1.0) * 1024.0);
            if (fns >= 1024) { fns = 0; ++oct; }
            if (oct < -8) { oct = -8; fns = 0; }
            if (oct > 7)  { oct = 7; fns = 1023; }
            ratio = std::ldexp (1.0 + fns / 1024.0, oct) * 44100.0 / engineRate;
            break;
        }
    }

    ratio = std::clamp (ratio, 0.0, 256.0);
    v.step = (int64_t) (ratio * (double) fracOne);
}

float Synth::vibratoCents (double t) const
{
    if (params.vibAmount <= 0.0f || t < params.vibDelay) return 0.0f;
    const double since = t - params.vibDelay;
    const double fade = params.vibAttack > 0.0f ? std::min (1.0, since / params.vibAttack) : 1.0;
    const double phase = since * params.vibSpeed;   // cycles
    const double s = std::sin (2.0 * 3.14159265358979323846 * phase);
    double shape = s;
    if (params.vibShape == 1)      shape = (2.0 / 3.14159265358979323846) * std::asin (s);   // triangle
    else if (params.vibShape == 2) shape = s >= 0.0 ? 1.0 : -1.0;                           // square
    return (float) (params.vibAmount * fade * shape);
}

template <Interp interp>
void Synth::renderVoice (Voice& v, float* outL, float* outR, int n, float gainL, float gainR)
{
    const Region& r = *v.region;
    const bool coded = codedPool != nullptr && codedPoolCodec == profile->codec && params.nativeFormat;
    const int16_t* pool = coded ? codedPool->data() : soundFont->samples().data();
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
        else if constexpr (interp == Interp::N64Poly)
        {
            // Top 6 bits of the fraction pick one of 64 phases.
            const int16_t* c = n64ResampleTable().data() + ((frac >> 26) << 2);
            const int out = (fetch (idx - 1) * c[0] + fetch (idx) * c[1] + fetch (idx + 1) * c[2] + fetch (idx + 2) * c[3]) >> 15;
            s = (float) std::clamp (out, -32768, 32767) * (1.0f / 32768.0f);
        }
        else
        {
            // Bits 4..11 of the 12-bit pitch fraction pick the Gaussian coefficients.
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
    constexpr int chunk = 32;   // pitch (bend, glide) is updated this often
    int count = 0;

    for (auto& v : voices)
    {
        if (! v.active) continue;

        const float p = std::clamp (v.pan + ccPan, -1.0f, 1.0f);
        const float g = v.baseGain * channelGain;
        const float gl = g * std::min (1.0f, 1.0f - p);   // balance law: centre = unity
        const float gr = g * std::min (1.0f, 1.0f + p);

        for (int done = 0; done < n && v.active; done += chunk)
        {
            const int len = std::min (chunk, n - done);
            v.vibCents = vibratoCents (v.vibTime);
            updatePitch (v);

            switch (profile->interp)
            {
                case Interp::Nearest:   renderVoice<Interp::Nearest>   (v, outL + done, outR + done, len, gl, gr); break;
                case Interp::Linear:    renderVoice<Interp::Linear>    (v, outL + done, outR + done, len, gl, gr); break;
                case Interp::Hermite:   renderVoice<Interp::Hermite>   (v, outL + done, outR + done, len, gl, gr); break;
                case Interp::SnesGauss: renderVoice<Interp::SnesGauss> (v, outL + done, outR + done, len, gl, gr); break;
                case Interp::Ps1Gauss:  renderVoice<Interp::Ps1Gauss>  (v, outL + done, outR + done, len, gl, gr); break;
                case Interp::N64Poly:   renderVoice<Interp::N64Poly>   (v, outL + done, outR + done, len, gl, gr); break;
            }

            v.vibTime += (double) len / engineRate;

            if (v.glideCents != 0.0f)
            {
                const float stepCents = v.glideRate * (float) len;
                v.glideCents = v.glideCents > 0.0f ? std::max (0.0f, v.glideCents - stepCents)
                                                   : std::min (0.0f, v.glideCents + stepCents);
            }
        }

        if (v.active) ++count;
    }

    activeVoices.store (count, std::memory_order_relaxed);
    applyEffectsAndOutputStage (outL, outR, n);
}

void Synth::applyEffectsAndOutputStage (float* outL, float* outR, int n)
{
    const auto& fx = params.fx;

    // Effects are parallel sends fed from the dry voice mix, summed back in.
    if (fx.snesEcho || fx.ps1Reverb || fx.n64Reverb)
    {
        std::copy (outL, outL + n, dryL.data());
        std::copy (outR, outR + n, dryR.data());

        if (fx.snesEcho)
        {
            if (! echoWasOn) snesEcho.reset();
            snesEcho.process (outL, outR, n, fx);
        }

        auto addSend = [&] (auto& effect, bool& wasOn)
        {
            if (! wasOn) effect.reset();
            std::copy (dryL.data(), dryL.data() + n, wetL.data());
            std::copy (dryR.data(), dryR.data() + n, wetR.data());
            effect.process (wetL.data(), wetR.data(), n, fx);
            for (int i = 0; i < n; ++i)
            {
                outL[i] += wetL[(size_t) i] - dryL[(size_t) i];
                outR[i] += wetR[(size_t) i] - dryR[(size_t) i];
            }
        };
        if (fx.ps1Reverb) addSend (ps1Reverb, ps1WasOn);
        if (fx.n64Reverb) addSend (n64Reverb, n64WasOn);
    }
    echoWasOn = fx.snesEcho;
    ps1WasOn = fx.ps1Reverb;
    n64WasOn = fx.n64Reverb;

    // Output stage: fixed bit depth with hard clipping.
    if (profile->outputBits > 0)
    {
        const float q = (float) (1 << (profile->outputBits - 1));
        for (int i = 0; i < n; ++i)
        {
            outL[i] = std::clamp (std::floor (outL[i] * q + 0.5f), -q, q - 1.0f) / q;
            outR[i] = std::clamp (std::floor (outR[i] * q + 0.5f), -q, q - 1.0f) / q;
        }
    }

    // Discrete YM2612: the 9-bit DAC steps by +4 at or above zero and -3 below it,
    // a dead zone around zero crossings. The constant +4 at rest is removed.
    if (profile->ym2612Ladder)
    {
        constexpr float below = -7.0f / 256.0f;
        for (int i = 0; i < n; ++i)
        {
            if (outL[i] < 0.0f) outL[i] += below;
            if (outR[i] < 0.0f) outR[i] += below;
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

            // Reconstruct at the host rate: sample-and-hold, or Hermite between the middle points.
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
                if (profile->holdOutput)
                {
                    left[i]  = histL[3];
                    right[i] = histR[3];
                }
                else
                {
                    const float t = (float) enginePhase;
                    left[i]  = hermite (histL[0], histL[1], histL[2], histL[3], t);
                    right[i] = hermite (histR[0], histR[1], histR[2], histR[3], t);
                }
            }
        }

        analog.process (left, right, n, params.fx.ledFilter);

        left += n; right += n; numSamples -= n;
    }
}

} // namespace lotus
