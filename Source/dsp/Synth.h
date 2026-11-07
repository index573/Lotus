#pragma once

// The sample playback engine. Framework-free; the audio path never allocates or
// locks. The owner must not call setSoundFont()/setCodedPool() while render() runs.

#include "Consoles.h"
#include "Effects.h"
#include "Envelope.h"
#include "SoundFont.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace lotus
{

struct EngineParams
{
    float attack  = 0.0f;   // seconds
    float decay   = 0.0f;
    float sustain = 1.0f;   // 0..1
    float release = 0.0f;

    bool cutOnNoteOff   = false;
    bool useSf2Envelope = false;
    ConsoleId console   = ConsoleId::Modern;
    bool hardwareVoices = false;   // limit polyphony to the console's voice count
    bool nativeFormat   = false;   // play the console-codec version of the samples
    float masterGain    = 1.0f;
    int   bendRange     = 2;       // semitones
    float glide         = 0.0f;    // seconds
    float tuneCents     = 0.0f;    // master tuning relative to A4 = 440 Hz

    // Vibrato: a per-note pitch LFO that starts after `vibDelay` and fades in over `vibAttack`.
    float vibAmount = 0.0f;        // cents (peak deviation)
    float vibSpeed  = 5.5f;        // Hz
    float vibDelay  = 0.0f;        // seconds
    float vibAttack = 0.0f;        // seconds
    int   vibShape  = 0;           // 0 sine, 1 triangle, 2 square

    EffectParams fx;
};

class Synth
{
public:
    static constexpr int maxVoices = 64;

    Synth();

    void prepare (double hostSampleRate, int maxBlockSize);

    void setSoundFont (const SoundFont* sf);
    const SoundFont* getSoundFont() const { return soundFont; }

    /** A pre-decoded copy of the sample pool in a console's native format (or nullptr). */
    void setCodedPool (const std::vector<int16_t>* pool, Codec codec);

    void setPresetIndex (int index);
    int  getPresetIndex() const { return presetIndex.load(); }

    void setParams (const EngineParams& p);

    /** MIDI input. The channel picks the preset slot of the matching note colour. */
    void handleMidi (const uint8_t* data, int size);
    void noteOn (int key, int velocity, int channel = 0);
    void noteOff (int key, int channel = 0);
    void allNotesOff();
    void allSoundOff();

    // --- Note colours ------------------------------------------------------------
    // FL Studio's 16 note colours (MIDI channels 1-16 elsewhere) can each play their own
    // preset. -1 follows the main preset.
    static constexpr int numColours = 16;
    void setColourPreset (int colour, int presetIndex);
    int  getColourPreset (int colour) const;
    int  presetForColour (int colour) const;   // the preset a note of that colour plays

    // --- Host-driven notes -------------------------------------------------------
    // For hosts with their own voice system (FL Studio's native format). Pitch, volume and
    // pan stay under the host's control while the note plays, which is how FL's slides and
    // portamento reach the engine. Ids are never 0.
    uint32_t hostNoteOn (int key, int velocity, int colour, float pitchCents, float gain, float pan);
    void hostNoteLevels (uint32_t id, float pitchCents, float gain, float pan);
    void hostNoteOff (uint32_t id);
    void hostNoteKill (uint32_t id);
    bool isNoteSounding (uint32_t id) const;

    void render (float* left, float* right, int numSamples);

    int getActiveVoiceCount() const { return activeVoices.load (std::memory_order_relaxed); }

    std::atomic<bool> presetChangedByMidi { false };

private:
    struct Voice
    {
        bool active = false;
        const Region* region = nullptr;
        int key = 0;
        int pitchKey = 0;
        int channel = 0;
        bool hostOwned = false;
        float hostCents = 0.0f, hostCentsTarget = 0.0f;   // live pitch offset from the host
        float hostGain = 1.0f, hostGainTarget = 1.0f;
        float hostPan = 0.0f;
        uint32_t noteId = 0;
        uint32_t age = 0;
        bool held = false, sustained = false, released = false, hasLooped = false;
        int64_t pos = 0;     // 32.32 fixed point into the sample pool
        int64_t step = 0;
        float baseGain = 1.0f;
        float pan = 0.0f;
        float glideCents = 0.0f;
        float glideRate = 0.0f;   // cents per engine sample
        double vibTime = 0.0;     // seconds since note-on
        float vibCents = 0.0f;
        Envelope env;
    };

    uint32_t startNote (int key, int velocity, int channel, bool hostOwned);
    Voice& startVoice (const Region& r, int key, int velocity, uint32_t noteId);
    void releaseVoice (Voice& v);
    Voice& allocateVoice();
    void updatePitch (Voice& v) const;
    float vibratoCents (double secondsSinceNoteOn) const;
    void renderEngine (float* outL, float* outR, int n);
    void applyEffectsAndOutputStage (float* outL, float* outR, int n);
    void configureConsole();

    template <Interp interp>
    void renderVoice (Voice& v, float* outL, float* outR, int n, float gainL, float gainR);

    const SoundFont* soundFont = nullptr;
    const std::vector<int16_t>* codedPool = nullptr;
    Codec codedPoolCodec = Codec::None;
    std::atomic<int> presetIndex { 0 };
    std::array<std::atomic<int>, numColours> colourPresets;

    EngineParams params;
    ConsoleId activeConsole = ConsoleId::Modern;
    const ConsoleProfile* profile = nullptr;

    double hostRate = 44100.0;
    double engineRate = 44100.0;
    double engineStep = 1.0;
    double enginePhase = 0.0;
    bool passthrough = true;
    float histL[4] {}, histR[4] {};

    std::vector<float> engineL, engineR;
    std::vector<float> dryL, dryR, wetL, wetR;
    int maxBlock = 512;

    SnesEcho snesEcho;
    Ps1Reverb ps1Reverb;
    N64Reverb n64Reverb;
    AnalogStage analog;
    bool echoWasOn = false, ps1WasOn = false, n64WasOn = false;

    std::array<Voice, maxVoices> voices;
    uint32_t ageCounter = 0, noteCounter = 0;
    std::atomic<int> activeVoices { 0 };
    int lastKey = -1;

    float pitchBend = 0.0f;
    float ccVolume = 1.0f, ccExpression = 1.0f, ccPan = 0.0f;
    bool sustainPedal = false;
    int bankMsb = 0, bankLsb = 0;
};

} // namespace lotus
