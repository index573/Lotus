#pragma once

// Lotus - console playback profiles.
//
// A profile describes how a particular piece of hardware turns a sample into sound:
// the rate its mixer runs at, how it interpolates between sample points, how finely
// it can represent pitch, and the bit depth of its sample data and output.
//
// Lotus renders voices at the profile's native mixer rate with the native
// interpolator, then reconstructs that stream at the host sample rate. That's what
// gives each mode its character (SNES muffle, DS/GBA grit and aliasing), as
// opposed to just slapping a filter or bitcrusher over a clean render.

#include <array>
#include <cstdint>

namespace lotus
{

enum class Interp : uint8_t
{
    Nearest,      // sample-and-hold, no interpolation
    Linear,
    Hermite,      // 4-point cubic Hermite (clean, modern)
    SnesGauss,    // S-DSP 4-tap Gaussian, 11-bit coefficients
    Ps1Gauss      // SPU 4-tap Gaussian, 15-bit coefficients
};

enum class PitchMode : uint8_t
{
    Free,         // floating-point step (as accurate as possible)
    Fixed4p12,    // SNES / PS1 pitch register: 4.12 fixed point at the mixer rate
    DsTimer       // DS: sample rate = 16756991 / timer (16-bit timer reload)
};

enum class ConsoleId : int
{
    Modern = 0,
    Linear,
    Raw,
    Snes,
    NintendoDs,
    Gba,
    Ps1,
    count
};

struct ConsoleProfile
{
    const char* name;          // shown in the UI
    const char* description;   // tooltip
    double engineRate;         // Hz; 0 = run at the host sample rate
    Interp interp;
    PitchMode pitch;
    uint32_t maxStep4p12;      // pitch register ceiling for Fixed4p12 (0 = none)
    int sampleBits;            // 0 = full precision, otherwise quantise sample data
    int outputBits;            // 0 = float output, otherwise quantise (and clip) the mix
};

const ConsoleProfile& getConsoleProfile (ConsoleId id);
constexpr int numConsoles = (int) ConsoleId::count;

/** S-DSP Gaussian table (512 entries, sums of 4 taps ~2048). */
const std::array<int16_t, 512>& snesGaussTable();

/** PS1 SPU Gaussian table (512 entries, sums of 4 taps ~0x7F80). */
const std::array<int16_t, 512>& ps1GaussTable();

} // namespace lotus
