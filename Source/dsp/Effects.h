#pragma once

// Console effects, modelled on the hardware that shipped in each machine: the SNES
// S-DSP echo, the PS1 SPU reverb, libultra's N64 effect bus, and the analogue filters
// on a console's output (including the Amiga's switchable "LED" filter). They run at
// the engine's mixer rate, so they're closest to the hardware when Console matches.

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
    int  ps1Preset = 0;
    float ps1Depth = 0.5f;     // EVOL as 0..1

    bool n64Reverb = false;
    int  n64Preset = 1;        // Big Room, libultra's default
    float n64Mix = 0.5f;

    bool ledFilter = false;    // Amiga "LED" low-pass
};

constexpr int numSnesFirPresets = 4;
const char* snesFirPresetName (int index);

constexpr int numPs1ReverbPresets = 9;
const char* ps1ReverbPresetName (int index);

constexpr int numN64FxPresets = 5;
const char* n64FxPresetName (int index);

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

class N64Reverb
{
public:
    void prepare (double maxRate);
    void setRate (double engineRate);
    void reset();

    void process (float* l, float* r, int n, const EffectParams& p);

private:
    struct Section
    {
        int input = 0, output = 0;          // delays in samples
        int fbcoef = 0, ffcoef = 0, gain = 0;
        bool chorus = false;
        double rsinc = 0.0, rsgain = 0.0, rsval = 1.0, rsdelta = 0.0;
        int lpCoef = 0, lpGain = 0, lpState = 0;
        bool lowpass = false;
    };

    void loadPreset (int index);
    int16_t& at (int delay);
    float readFrac (double delay);

    std::vector<int16_t> line;
    int length = 1, pos = 0;
    int currentPreset = -1;
    double rateScale = 1.0;      // engine rate / 32000
    double engineRate = 32000.0;
    std::array<Section, 4> sections {};
    int numSections = 0;
};

/** First-order analogue filters on a console's output, plus the Amiga "LED" filter. */
class AnalogStage
{
public:
    void configure (double sampleRate, float lowpassHz, float highpass1Hz, float highpass2Hz);
    void process (float* l, float* r, int n, bool ledFilter);

private:
    struct OnePole { float a = 0.0f; float zl = 0.0f, zr = 0.0f; bool on = false; };
    struct Biquad { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; float l1 = 0, l2 = 0, r1 = 0, r2 = 0; };

    OnePole lp, hp1, hp2;
    Biquad led;
    bool ledWasOn = false;
};

} // namespace lotus
