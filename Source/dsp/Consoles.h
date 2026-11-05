#pragma once

// A console profile describes how a piece of hardware turns a sample into sound:
// mixer rate, interpolation, pitch precision, sample/output bit depth, native sample
// codec, voice count, and the analogue filtering on its output. Lotus renders voices
// at the profile's mixer rate with its interpolator and then reconstructs that stream
// at the host rate, so the character comes from the playback model itself.

#include <array>
#include <cstdint>

namespace lotus
{

enum class Interp : uint8_t
{
    Nearest,
    Linear,
    Hermite,
    SnesGauss,   // S-DSP 4-tap Gaussian, 11-bit coefficients
    Ps1Gauss,    // SPU/SPU2 4-tap Gaussian, 15-bit coefficients
    N64Poly      // RSP 4-tap, 64-phase polyphase filter
};

enum class PitchMode : uint8_t
{
    Free,
    Fixed4p12,     // SNES / PS1 / PS2 pitch register (4.12 fixed point)
    DsTimer,       // 16756991 / timer
    N64Q15,        // libultra: Q1.15 ratio, capped just under 2.0
    AmigaPeriod,   // 3546895 / period, period >= 113
    NesDmc,        // one of the 16 DMC rates
    ScspOctFns     // 44100 * 2^oct * (1 + fns / 1024)
};

enum class Codec : uint8_t
{
    None,
    Brr,        // SNES
    PsAdpcm,    // PS1 / PS2
    ImaAdpcm,   // DS
    Dpcm        // NES DMC
};

enum class ConsoleId : int   // menu order, grouped by maker
{
    Modern = 0,
    Linear,
    Raw,
    Nes,
    Snes,
    N64,
    Gba,
    NintendoDs,
    Ps1,
    Ps2,
    MegaDrive,
    Saturn,
    Amiga500,
    Amiga1200,
    count
};

struct ConsoleProfile
{
    const char* name;
    const char* description;
    const char* group;          // heading in the console menu
    double engineRate;          // Hz; 0 = host rate
    Interp interp;
    PitchMode pitch;
    uint32_t maxStep4p12;       // Fixed4p12 ceiling
    int sampleBits;             // 0 = full precision
    int outputBits;             // 0 = float output
    Codec codec;                // native sample format (optional emulation)
    int voiceLimit;             // hardware voices (0 = no fixed limit)
    bool holdOutput;            // reconstruct with sample-and-hold instead of smoothly
    float lowpassHz;            // first-order analogue low-pass (0 = none)
    float highpassHz[2];        // first-order analogue high-passes (0 = none)
    bool ym2612Ladder;          // discrete YM2612 DAC crossover distortion
};

const ConsoleProfile& getConsoleProfile (ConsoleId id);
constexpr int numConsoles = (int) ConsoleId::count;

const char* codecName (Codec c);

const std::array<int16_t, 512>& snesGaussTable();
const std::array<int16_t, 512>& ps1GaussTable();
const std::array<int16_t, 256>& n64ResampleTable();

constexpr double amigaPalClock = 28375160.0 / 8.0;   // 3546895 Hz
constexpr double nesCpuClock = 1789773.0;
extern const int nesDmcPeriods[16];

} // namespace lotus
