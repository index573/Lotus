#pragma once

// Lotus - amplitude envelope.
//
// Two flavours share one implementation:
//
//  * Lotus ADSR (the knobs). Times are exact: a 250 ms release takes 250 ms from
//    wherever the level is to silence, and 0 means *instant*. No hidden minimum
//    release, no "tail" the user can't get rid of.
//
//  * SoundFont envelope (the file's own DAHDSR). Uses the SF2 semantics, where
//    decay/release times describe a full 96 dB fall, so a partially decayed note
//    releases proportionally faster.
//
// Attack is linear in amplitude; decay and release are linear in dB (i.e.
// exponential in amplitude), which is how both the SF2 spec and most console
// hardware envelopes behave. Everything ends at -96 dB, at which point the voice is
// freed - so release really does end.

#include <algorithm>
#include <cmath>

namespace lotus
{

class Envelope
{
public:
    enum class Stage { Delay, Attack, Hold, Decay, Sustain, Release, Cut, Done };

    struct Params
    {
        float delay = 0.0f, attack = 0.0f, hold = 0.0f, decay = 0.0f;
        float sustain = 1.0f;   // linear 0..1
        float release = 0.0f;
        bool  sf2Semantics = false;
    };

    static constexpr float floorLevel = 1.5848932e-5f;   // -96 dB
    static constexpr float cutSeconds = 0.001f;          // anti-click ramp for hard cuts

    void start (const Params& p, double sampleRate)
    {
        params = p;
        rate = sampleRate;
        level = 0.0f;
        if (p.delay > 0.0f) enter (Stage::Delay, samples (p.delay));
        else enterAttack();
    }

    /** Note-off. `releaseSeconds` < 0 keeps the release time given at start(). */
    void release (float releaseSeconds = -1.0f)
    {
        if (stage == Stage::Done || stage == Stage::Release || stage == Stage::Cut) return;
        if (releaseSeconds >= 0.0f) params.release = releaseSeconds;
        if (stage == Stage::Delay || level <= floorLevel) { finish(); return; }

        double n = samples (params.release);
        if (params.sf2Semantics)
            n *= (20.0 * std::log10 (level / floorLevel)) / 96.0;

        const int count = (int) std::lround (n);
        if (count <= 0) { finish(); return; }

        mul = (float) std::pow ((double) floorLevel / level, 1.0 / count);
        enter (Stage::Release, count);
    }

    /** Hard stop with a ~1 ms ramp so it doesn't click. */
    void cut()
    {
        if (stage == Stage::Done || stage == Stage::Cut) return;
        if (stage == Stage::Delay || level <= floorLevel) { finish(); return; }
        const int count = std::max (1, (int) std::lround (cutSeconds * rate));
        inc = -level / (float) count;
        enter (Stage::Cut, count);
    }

    /** Returns the current gain and advances one sample. */
    inline float next()
    {
        const float out = level;

        switch (stage)
        {
            case Stage::Attack:  level += inc; break;
            case Stage::Decay:
            case Stage::Release: level *= mul; break;
            case Stage::Cut:     level = std::max (0.0f, level + inc); break;
            case Stage::Done:    return 0.0f;
            case Stage::Delay:
            case Stage::Hold:
            case Stage::Sustain: break;
        }

        if (stage != Stage::Sustain && --remaining <= 0)
            advance();

        return out;
    }

    bool isDone() const       { return stage == Stage::Done; }
    bool isReleasing() const  { return stage == Stage::Release || stage == Stage::Cut; }
    Stage getStage() const    { return stage; }
    float getLevel() const    { return level; }

private:
    int samples (float seconds) const { return (int) std::lround (std::max (0.0f, seconds) * rate); }

    void enter (Stage s, int count) { stage = s; remaining = count; }
    void finish() { stage = Stage::Done; level = 0.0f; remaining = 0; }

    void enterAttack()
    {
        const int n = samples (params.attack);
        if (n <= 0) { level = 1.0f; enterHold(); return; }
        level = 0.0f;
        inc = 1.0f / (float) n;
        enter (Stage::Attack, n);
    }

    void enterHold()
    {
        level = 1.0f;
        const int n = samples (params.hold);
        if (n <= 0) { enterDecay(); return; }
        enter (Stage::Hold, n);
    }

    void enterDecay()
    {
        level = 1.0f;
        const float target = std::max (params.sustain, floorLevel);
        if (target >= 1.0f) { enter (Stage::Sustain, 0); return; }

        double n = samples (params.decay);
        if (params.sf2Semantics)
            n *= (-20.0 * std::log10 (target)) / 96.0;

        const int count = (int) std::lround (n);
        if (count <= 0) { enterSustain(); return; }

        mul = (float) std::pow ((double) target, 1.0 / count);
        enter (Stage::Decay, count);
    }

    void enterSustain()
    {
        level = params.sustain;
        if (level <= floorLevel) finish();
        else enter (Stage::Sustain, 0);
    }

    void advance()
    {
        switch (stage)
        {
            case Stage::Delay:   enterAttack(); break;
            case Stage::Attack:  enterHold(); break;
            case Stage::Hold:    enterDecay(); break;
            case Stage::Decay:   enterSustain(); break;
            case Stage::Release:
            case Stage::Cut:     finish(); break;
            case Stage::Sustain:
            case Stage::Done:    break;
        }
    }

    Params params;
    double rate = 44100.0;
    Stage stage = Stage::Done;
    float level = 0.0f, inc = 0.0f, mul = 1.0f;
    int remaining = 0;
};

} // namespace lotus
