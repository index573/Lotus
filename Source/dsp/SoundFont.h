#pragma once

// SoundFont 2 loader. Flattens presets -> instruments -> samples into a list of
// playable regions per preset, so the audio thread never walks the hierarchy.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lotus
{

/** SF2 volume envelope, converted to seconds / linear units. */
struct Sf2VolEnv
{
    float delay   = 0.0f;   // seconds
    float attack  = 0.0f;   // seconds
    float hold    = 0.0f;   // seconds
    float decay   = 0.0f;   // seconds (time for a full 96 dB fall)
    float sustain = 1.0f;   // linear gain (0..1)
    float release = 0.0f;   // seconds (time for a full 96 dB fall)
    float keyToHold  = 0.0f;  // timecents per key (relative to key 60)
    float keyToDecay = 0.0f;  // timecents per key (relative to key 60)
};

enum class LoopMode : uint8_t
{
    None = 0,
    Continuous = 1,
    UntilRelease = 3   // loop while held, play through to the end after note-off
};

/** One playable zone: a sample plus everything needed to pitch, place and shape it. */
struct Region
{
    uint8_t loKey = 0, hiKey = 127;
    uint8_t loVel = 0, hiVel = 127;

    // Absolute indices into SoundFont::samples
    uint32_t start = 0, end = 0;          // [start, end)
    uint32_t loopStart = 0, loopEnd = 0;  // [loopStart, loopEnd)
    LoopMode loopMode = LoopMode::None;

    int   rootKey = 60;
    float tuneCents = 0.0f;       // coarse * 100 + fine + sample pitch correction
    float scaleTuning = 100.0f;   // cents per key
    float sampleRate = 44100.0f;
    float gain = 1.0f;            // linear, from initialAttenuation
    float pan = 0.0f;             // -1 (left) .. +1 (right)
    int   exclusiveClass = 0;
    int   fixedKey = -1;          // keynum generator override
    int   fixedVelocity = -1;     // velocity generator override

    Sf2VolEnv volEnv;
    std::string sampleName;
};

struct Preset
{
    std::string name;
    int bank = 0;
    int program = 0;
    std::vector<Region> regions;
};

class SoundFont
{
public:
    /** Parses an SF2 image. Returns nullptr and fills `error` on failure. */
    static std::unique_ptr<SoundFont> loadFromMemory (const void* data, size_t size, std::string& error);
    static std::unique_ptr<SoundFont> loadFromFile (const std::string& path, std::string& error);

    std::string name;                 // INFO/INAM
    std::shared_ptr<const std::vector<int16_t>> pool;   // whole 16-bit sample pool (smpl chunk), shared by copies
    std::vector<Preset> presets;      // sorted by bank, then program

    const std::vector<int16_t>& samples() const { return *pool; }

    /** Index of the preset with this bank/program, or -1. */
    int findPreset (int bank, int program) const;
};

} // namespace lotus
