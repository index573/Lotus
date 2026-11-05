#pragma once

// Lotus - console-native effects.
//
// These are models of the effects hardware that actually shipped in the consoles,
// not generic delay/reverb plugins:
//
//  * SnesEcho  - the S-DSP echo unit: a 16-bit echo buffer of EDL x 16 ms, an
//                8-tap FIR filter on the echo path, signed feedback (EFB) and echo
//                volume (EVOL), using the chip's own integer maths.
//  * Ps1Reverb - the PlayStation SPU reverb: same-side/different-side reflections,
//                four combs and two all-pass stages running at 22.05 kHz on a 16-bit
//                work area, with the 39-tap FIR used to resample in and out, driven
//                by the register presets from Sony's libraries.
//
// Both run at the engine's mixer rate. When that matches the hardware (Console set to
// SNES for the echo, PS1 for the reverb) they are sample-exact models; at any other
// rate the delay times are scaled to stay correct, but the FIR responses scale with
// the sample rate.

#include <array>
#include <cstdint>
#include <vector>

namespace lotus
{

struct EffectParams
{
    bool snesEcho = false;
    int  snesDelay = 5;        // EDL 0..15 (x 16 ms)
    int  snesFeedback = 64;    // EFB -128..127
    int  snesVolume = 48;      // EVOL 0..127
    int  snesFilter = 0;       // FIR preset index

    bool ps1Reverb = false;
    int  ps1Preset = 0;        // index into ps1ReverbPresetNames
    float ps1Depth = 0.5f;     // EVOL as 0..1
};

constexpr int numSnesFirPresets = 4;
const char* snesFirPresetName (int index);

constexpr int numPs1ReverbPresets = 9;
const char* ps1ReverbPresetName (int index);

class SnesEcho
{
public:
    void prepare (double maxRate);
    void setRate (double engineRate);
    void reset();

    /** Adds the echo return to l/r. Input to the echo is the dry l/r. */
    void process (float* l, float* r, int n, const EffectParams& p);

private:
    std::vector<int16_t> bufL, bufR;
    double rateScale = 1.0;      // engine rate / 32000
    int offset = 0, length = 0;  // in samples
    std::array<int, 8> histL {}, histR {};
    int histPos = 0;
};

class Ps1Reverb
{
public:
    void prepare (double maxRate);
    void setRate (double engineRate);
    void reset();

    void process (float* l, float* r, int n, const EffectParams& p);

private:
    void loadPreset (int index);
    int16_t& mem (int addr);
    void tick (int lin, int rin, int& lout, int& rout);

    std::vector<int16_t> work;   // reverb work area (halfwords)
    int workSize = 0;            // active size in halfwords
    int bufferAddress = 0;
    int currentPreset = -1;
    double rateScale = 1.0;      // engine rate / 44100

    // Registers (offsets already converted to halfwords and scaled)
    int dAPF1 = 0, dAPF2 = 0;
    int vIIR = 0, vCOMB1 = 0, vCOMB2 = 0, vCOMB3 = 0, vCOMB4 = 0, vWALL = 0, vAPF1 = 0, vAPF2 = 0;
    int mLSAME = 0, mRSAME = 0, mLCOMB1 = 0, mRCOMB1 = 0, mLCOMB2 = 0, mRCOMB2 = 0;
    int dLSAME = 0, dRSAME = 0, mLDIFF = 0, mRDIFF = 0, mLCOMB3 = 0, mRCOMB3 = 0, mLCOMB4 = 0, mRCOMB4 = 0;
    int dLDIFF = 0, dRDIFF = 0, mLAPF1 = 0, mRAPF1 = 0, mLAPF2 = 0, mRAPF2 = 0, vLIN = 0, vRIN = 0;

    // 39-tap resampling filter state (input history at the full rate)
    std::array<int, 64> inHistL {}, inHistR {};
    std::array<int, 64> outHistL {}, outHistR {};
    int histPos = 0;
    bool phase = false;
};

} // namespace lotus
