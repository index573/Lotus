#pragma once

// Lotus - the sample playback engine.
//
// Framework-free. The host wrapper (PluginProcessor) owns one Synth, feeds it MIDI
// and asks it to render. The audio path is allocation-free and lock-free; the owner
// is responsible for not calling setSoundFont() concurrently with render().

#include "Consoles.h"
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
    // Lotus ADSR
    float attack  = 0.0f;   // seconds
    float decay   = 0.0f;   // seconds
    float sustain = 1.0f;   // linear 0..1
    float release = 0.0f;   // seconds

    bool cutOnNoteOff   = false;  // note-off silences immediately (1 ms de-click)
    bool useSf2Envelope = false;  // use the file's own envelope instead of the knobs
    ConsoleId console   = ConsoleId::Modern;
    float masterGain    = 1.0f;   // linear
    int   bendRange     = 2;      // semitones
};

class Synth
{
public:
    static constexpr int maxVoices = 64;

    Synth();

    void prepare (double hostSampleRate, int maxBlockSize);

    /** Swap the SoundFont. Must not be called while render() is running. Kills all voices. */
    void setSoundFont (const SoundFont* sf);
    const SoundFont* getSoundFont() const { return soundFont; }

    void setPresetIndex (int index);
    int  getPresetIndex() const { return presetIndex.load(); }

    /** Call once per block before rendering. */
    void setParams (const EngineParams& p);

    /** Raw MIDI message. Omni: the channel is ignored. */
    void handleMidi (const uint8_t* data, int size);

    void noteOn (int key, int velocity);
    void noteOff (int key);
    void allNotesOff();
    void allSoundOff();

    /** Renders (overwrites) numSamples into left/right. */
    void render (float* left, float* right, int numSamples);

    int getActiveVoiceCount() const { return activeVoices.load (std::memory_order_relaxed); }

    /** Set when a MIDI program change moved to a different preset; the UI clears it. */
    std::atomic<bool> presetChangedByMidi { false };

private:
    struct Voice
    {
        bool active = false;
        const Region* region = nullptr;
        int key = 0;          // the key that was pressed (for note-off matching)
        int pitchKey = 0;     // the key used for pitch (may be overridden by the region)
        uint32_t noteId = 0;  // voices started by the same note-on share an id
        uint32_t age = 0;
        bool held = false, sustained = false, released = false, hasLooped = false;
        int64_t pos = 0;      // 32.32 fixed point, absolute index into the sample pool
        int64_t step = 0;
        float baseGain = 1.0f;
        float pan = 0.0f;
        Envelope env;
    };

    void startVoice (const Region& r, int key, int velocity, uint32_t noteId);
    void releaseVoice (Voice& v);
    Voice& allocateVoice();
    void updatePitch (Voice& v) const;
    void renderEngine (float* outL, float* outR, int numEngineSamples);
    void configureConsole();

    template <Interp interp>
    void renderVoice (Voice& v, float* outL, float* outR, int n, float gainL, float gainR);

    const SoundFont* soundFont = nullptr;
    std::atomic<int> presetIndex { 0 };

    EngineParams params;
    ConsoleId activeConsole = ConsoleId::Modern;
    const ConsoleProfile* profile = nullptr;

    double hostRate = 44100.0;
    double engineRate = 44100.0;
    double engineStep = 1.0;      // engine samples per host sample
    double enginePhase = 0.0;
    bool passthrough = true;
    float histL[4] {}, histR[4] {};

    std::vector<float> engineL, engineR;
    int maxBlock = 512;

    std::array<Voice, maxVoices> voices;
    uint32_t ageCounter = 0, noteCounter = 0;
    std::atomic<int> activeVoices { 0 };

    // MIDI controller state
    float pitchBend = 0.0f;  // -1..1
    float ccVolume = 1.0f, ccExpression = 1.0f, ccPan = 0.0f;
    bool sustainPedal = false;
    int bankMsb = 0, bankLsb = 0;
};

} // namespace lotus
