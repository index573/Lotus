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

    void handleMidi (const uint8_t* data, int size);
    void noteOn (int key, int velocity);
    void noteOff (int key);
    void allNotesOff();
    void allSoundOff();

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
        uint32_t noteId = 0;
        uint32_t age = 0;
        bool held = false, sustained = false, released = false, hasLooped = false;
        int64_t pos = 0;     // 32.32 fixed point into the sample pool
        int64_t step = 0;
        float baseGain = 1.0f;
        float pan = 0.0f;
        float glideCents = 0.0f;
        float glideRate = 0.0f;   // cents per engine sample
        Envelope env;
    };

    void startVoice (const Region& r, int key, int velocity, uint32_t noteId);
    void releaseVoice (Voice& v);
    Voice& allocateVoice();
    void updatePitch (Voice& v) const;
    void renderEngine (float* outL, float* outR, int n);
    void applyEffectsAndOutputStage (float* outL, float* outR, int n);
    void configureConsole();

    template <Interp interp>
    void renderVoice (Voice& v, float* outL, float* outR, int n, float gainL, float gainR);

    const SoundFont* soundFont = nullptr;
    const std::vector<int16_t>* codedPool = nullptr;
    Codec codedPoolCodec = Codec::None;
    std::atomic<int> presetIndex { 0 };

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
