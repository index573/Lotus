#include "Effects.h"

#include <algorithm>
#include <cmath>

namespace lotus
{

namespace
{
inline int clamp16 (int v) { return std::clamp (v, -32768, 32767); }
inline int toInt16 (float v) { return clamp16 ((int) std::lround (v * 32768.0f)); }
inline int wrap16 (int v) { return (int) (int16_t) (uint16_t) (v & 0xFFFF); }

// N-SPC (Nintendo's standard SNES sound driver) FIR filter presets.
constexpr int8_t snesFir[numSnesFirPresets][8] =
{
    { 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },                                   // flat
    { 0x0C, 0x21, 0x2B, 0x2B, 0x13, (int8_t) 0xFE, (int8_t) 0xF3, (int8_t) 0xF9 },        // low-pass
    { 0x58, (int8_t) 0xBF, (int8_t) 0xDB, (int8_t) 0xF0, (int8_t) 0xFE, 0x07, 0x0C, 0x0C }, // high-pass
    { 0x34, 0x33, 0x00, (int8_t) 0xD9, (int8_t) 0xE5, 0x01, (int8_t) 0xFC, (int8_t) 0xEB }, // band-pass
};
const char* const snesFirNames[numSnesFirPresets] = { "Flat", "Low-pass", "High-pass", "Band-pass" };

// PS1 SPU reverb presets (Sony libsnd), from psx-spx "SPU Reverb Examples".
// Register order: dAPF1 dAPF2 vIIR vCOMB1 vCOMB2 vCOMB3 vCOMB4 vWALL vAPF1 vAPF2
// mLSAME mRSAME mLCOMB1 mRCOMB1 mLCOMB2 mRCOMB2 dLSAME dRSAME mLDIFF mRDIFF
// mLCOMB3 mRCOMB3 mLCOMB4 mRCOMB4 dLDIFF dRDIFF mLAPF1 mRAPF1 mLAPF2 mRAPF2 vLIN vRIN
struct Ps1Preset { const char* name; uint32_t sizeBytes; uint16_t regs[32]; };
const Ps1Preset ps1Presets[numPs1ReverbPresets] =
{
    { "Room", 0x26C0, { 0x007D,0x005B,0x6D80,0x54B8,0xBED0,0x0000,0x0000,0xBA80, 0x5800,0x5300,0x04D6,0x0333,0x03F0,0x0227,0x0374,0x01EF,
                        0x0334,0x01B5,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000, 0x0000,0x0000,0x01B4,0x0136,0x00B8,0x005C,0x8000,0x8000 } },
    { "Studio Small", 0x1F40, { 0x0033,0x0025,0x70F0,0x4FA8,0xBCE0,0x4410,0xC0F0,0x9C00, 0x5280,0x4EC0,0x03E4,0x031B,0x03A4,0x02AF,0x0372,0x0266,
                        0x031C,0x025D,0x025C,0x018E,0x022F,0x0135,0x01D2,0x00B7, 0x018F,0x00B5,0x00B4,0x0080,0x004C,0x0026,0x8000,0x8000 } },
    { "Studio Medium", 0x4840, { 0x00B1,0x007F,0x70F0,0x4FA8,0xBCE0,0x4510,0xBEF0,0xB4C0, 0x5280,0x4EC0,0x0904,0x076B,0x0824,0x065F,0x07A2,0x0616,
                        0x076C,0x05ED,0x05EC,0x042E,0x050F,0x0305,0x0462,0x02B7, 0x042F,0x0265,0x0264,0x01B2,0x0100,0x0080,0x8000,0x8000 } },
    { "Studio Large", 0x6FE0, { 0x00E3,0x00A9,0x6F60,0x4FA8,0xBCE0,0x4510,0xBEF0,0xA680, 0x5680,0x52C0,0x0DFB,0x0B58,0x0D09,0x0A3C,0x0BD9,0x0973,
                        0x0B59,0x08DA,0x08D9,0x05E9,0x07EC,0x04B0,0x06EF,0x03D2, 0x05EA,0x031D,0x031C,0x0238,0x0154,0x00AA,0x8000,0x8000 } },
    { "Hall", 0xADE0, { 0x01A5,0x0139,0x6000,0x5000,0x4C00,0xB800,0xBC00,0xC000, 0x6000,0x5C00,0x15BA,0x11BB,0x14C2,0x10BD,0x11BC,0x0DC1,
                        0x11C0,0x0DC3,0x0DC0,0x09C1,0x0BC4,0x07C1,0x0A00,0x06CD, 0x09C2,0x05C1,0x05C0,0x041A,0x0274,0x013A,0x8000,0x8000 } },
    { "Half Echo", 0x3C00, { 0x0017,0x0013,0x70F0,0x4FA8,0xBCE0,0x4510,0xBEF0,0x8500, 0x5F80,0x54C0,0x0371,0x02AF,0x02E5,0x01DF,0x02B0,0x01D7,
                        0x0358,0x026A,0x01D6,0x011E,0x012D,0x00B1,0x011F,0x0059, 0x01A0,0x00E3,0x0058,0x0040,0x0028,0x0014,0x8000,0x8000 } },
    { "Space Echo", 0xF6C0, { 0x033D,0x0231,0x7E00,0x5000,0xB400,0xB000,0x4C00,0xB000, 0x6000,0x5400,0x1ED6,0x1A31,0x1D14,0x183B,0x1BC2,0x16B2,
                        0x1A32,0x15EF,0x15EE,0x1055,0x1334,0x0F2D,0x11F6,0x0C5D, 0x1056,0x0AE1,0x0AE0,0x07A2,0x0464,0x0232,0x8000,0x8000 } },
    { "Chaos Echo", 0x18040, { 0x0001,0x0001,0x7FFF,0x7FFF,0x0000,0x0000,0x0000,0x8100, 0x0000,0x0000,0x1FFF,0x0FFF,0x1005,0x0005,0x0000,0x0000,
                        0x1005,0x0005,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000, 0x0000,0x0000,0x1004,0x1002,0x0004,0x0002,0x8000,0x8000 } },
    { "Delay", 0x18040, { 0x0001,0x0001,0x7FFF,0x7FFF,0x0000,0x0000,0x0000,0x0000, 0x0000,0x0000,0x1FFF,0x0FFF,0x1005,0x0005,0x0000,0x0000,
                        0x1005,0x0005,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000, 0x0000,0x0000,0x1004,0x1002,0x0004,0x0002,0x8000,0x8000 } },
};

// 39-tap half-band FIR used by the SPU to resample reverb input/output (psx-spx).
constexpr int ps1Fir[39] =
{
    -0x0001, 0, 0x0002, 0, -0x000A, 0, 0x0023, 0, -0x0067, 0, 0x010A, 0, -0x0268, 0, 0x0534, 0,
    -0x0B90, 0, 0x2806, 0x4000, 0x2806, 0, -0x0B90, 0, 0x0534, 0, -0x0268, 0, 0x010A, 0, -0x0067, 0,
    0x0023, 0, -0x000A, 0, 0x0002, 0, -0x0001,
};

constexpr uint32_t ps1MaxSizeBytes = 0x18040;
} // namespace

const char* snesFirPresetName (int i)   { return snesFirNames[std::clamp (i, 0, numSnesFirPresets - 1)]; }
const char* ps1ReverbPresetName (int i) { return ps1Presets[std::clamp (i, 0, numPs1ReverbPresets - 1)].name; }

// ---------------------------------------------------------------------------------
// SNES echo

void SnesEcho::prepare (double maxRate)
{
    const size_t cap = (size_t) std::ceil (15 * 512 * std::max (1.0, maxRate / 32000.0)) + 2;
    bufL.assign (cap, 0);
    bufR.assign (cap, 0);
    reset();
}

void SnesEcho::setRate (double engineRate)
{
    rateScale = engineRate / 32000.0;
    reset();
}

void SnesEcho::reset()
{
    std::fill (bufL.begin(), bufL.end(), (int16_t) 0);
    std::fill (bufR.begin(), bufR.end(), (int16_t) 0);
    histL.fill (0);
    histR.fill (0);
    offset = 0;
    length = 0;
    histPos = 0;
}

void SnesEcho::process (float* l, float* r, int n, const EffectParams& p)
{
    if (bufL.empty()) return;
    const auto& fir = snesFir[std::clamp (p.snesFilter, 0, numSnesFirPresets - 1)];
    const int efb = std::clamp (p.snesFeedback, -128, 127);
    const int evol = std::clamp (p.snesVolume, -128, 127);

    for (int i = 0; i < n; ++i)
    {
        // Like the chip, a new EDL only takes effect when the buffer wraps.
        if (offset == 0)
            length = std::min ((int) bufL.size(),
                               std::max (1, (int) std::lround (std::clamp (p.snesDelay, 0, 15) * 512 * rateScale)));

        // Read the echo buffer into the FIR history (the chip halves it here).
        histPos = (histPos + 1) & 7;
        histL[(size_t) histPos] = bufL[(size_t) offset] >> 1;
        histR[(size_t) histPos] = bufR[(size_t) offset] >> 1;

        // 8-tap FIR, oldest tap first, with the chip's 16-bit wrap before the last tap.
        auto fir8 = [&] (const std::array<int, 8>& h)
        {
            int acc = 0;
            for (int t = 0; t < 7; ++t)
                acc += (h[(size_t) ((histPos + t + 1) & 7)] * fir[t]) >> 6;
            acc = wrap16 (acc);
            acc += wrap16 ((h[(size_t) histPos] * fir[7]) >> 6);
            return clamp16 (acc) & ~1;
        };
        const int firL = fir8 (histL);
        const int firR = fir8 (histR);

        const int dryL = toInt16 (l[i]);
        const int dryR = toInt16 (r[i]);

        // Feedback write: voice input + FIR output * EFB.
        bufL[(size_t) offset] = (int16_t) (clamp16 (dryL + wrap16 ((firL * efb) >> 7)) & ~1);
        bufR[(size_t) offset] = (int16_t) (clamp16 (dryR + wrap16 ((firR * efb) >> 7)) & ~1);

        // Echo return into the main output.
        l[i] += (float) wrap16 ((firL * evol) >> 7) * (1.0f / 32768.0f);
        r[i] += (float) wrap16 ((firR * evol) >> 7) * (1.0f / 32768.0f);

        if (++offset >= length) offset = 0;
    }
}

// ---------------------------------------------------------------------------------
// PS1 reverb

void Ps1Reverb::prepare (double maxRate)
{
    const size_t cap = (size_t) std::ceil ((ps1MaxSizeBytes / 2) * std::max (1.0, maxRate / 44100.0)) + 8;
    work.assign (cap, 0);
    currentPreset = -1;
    reset();
}

void Ps1Reverb::setRate (double engineRate)
{
    rateScale = engineRate / 44100.0;
    currentPreset = -1;   // offsets must be rescaled
    reset();
}

void Ps1Reverb::reset()
{
    std::fill (work.begin(), work.end(), (int16_t) 0);
    inHistL.fill (0); inHistR.fill (0); outHistL.fill (0); outHistR.fill (0);
    histPos = 0;
    bufferAddress = 0;
    phase = false;
}

void Ps1Reverb::loadPreset (int index)
{
    const auto& pr = ps1Presets[std::clamp (index, 0, numPs1ReverbPresets - 1)];
    const auto* g = pr.regs;
    auto vol = [] (uint16_t v) { return (int) (int16_t) v; };
    // Addresses are in 8-byte units = 4 halfwords, scaled for the engine rate.
    auto adr = [this] (uint16_t v) { return v == 0 ? 0 : std::max (1, (int) std::lround (v * 4 * rateScale)); };

    dAPF1 = adr (g[0]);  dAPF2 = adr (g[1]);
    vIIR = vol (g[2]); vCOMB1 = vol (g[3]); vCOMB2 = vol (g[4]); vCOMB3 = vol (g[5]); vCOMB4 = vol (g[6]);
    vWALL = vol (g[7]); vAPF1 = vol (g[8]); vAPF2 = vol (g[9]);
    mLSAME = adr (g[10]); mRSAME = adr (g[11]); mLCOMB1 = adr (g[12]); mRCOMB1 = adr (g[13]);
    mLCOMB2 = adr (g[14]); mRCOMB2 = adr (g[15]); dLSAME = adr (g[16]); dRSAME = adr (g[17]);
    mLDIFF = adr (g[18]); mRDIFF = adr (g[19]); mLCOMB3 = adr (g[20]); mRCOMB3 = adr (g[21]);
    mLCOMB4 = adr (g[22]); mRCOMB4 = adr (g[23]); dLDIFF = adr (g[24]); dRDIFF = adr (g[25]);
    mLAPF1 = adr (g[26]); mRAPF1 = adr (g[27]); mLAPF2 = adr (g[28]); mRAPF2 = adr (g[29]);
    vLIN = vol (g[30]); vRIN = vol (g[31]);

    workSize = std::min ((int) work.size(), std::max (16, (int) std::lround ((pr.sizeBytes / 2) * rateScale)));
    currentPreset = index;
    reset();   // games zero the work area when changing presets
}

int16_t& Ps1Reverb::mem (int addr)
{
    int idx = (bufferAddress + addr) % workSize;
    if (idx < 0) idx += workSize;
    return work[(size_t) idx];
}

void Ps1Reverb::tick (int lin, int rin, int& lout, int& rout)
{
    auto mul = [] (int a, int b) { return (a * b) >> 15; };

    const int Lin = mul (vLIN, lin);
    const int Rin = mul (vRIN, rin);

    // Same-side reflections
    {
        const int prevL = mem (mLSAME - 1), prevR = mem (mRSAME - 1);
        const int l = clamp16 (mul (Lin + mul (mem (dLSAME), vWALL) - prevL, vIIR) + prevL);
        const int r = clamp16 (mul (Rin + mul (mem (dRSAME), vWALL) - prevR, vIIR) + prevR);
        mem (mLSAME) = (int16_t) l;
        mem (mRSAME) = (int16_t) r;
    }
    // Different-side reflections
    {
        const int prevL = mem (mLDIFF - 1), prevR = mem (mRDIFF - 1);
        const int l = clamp16 (mul (Lin + mul (mem (dRDIFF), vWALL) - prevL, vIIR) + prevL);
        const int r = clamp16 (mul (Rin + mul (mem (dLDIFF), vWALL) - prevR, vIIR) + prevR);
        mem (mLDIFF) = (int16_t) l;
        mem (mRDIFF) = (int16_t) r;
    }

    // Early echo (combs)
    int L = mul (vCOMB1, mem (mLCOMB1)) + mul (vCOMB2, mem (mLCOMB2)) + mul (vCOMB3, mem (mLCOMB3)) + mul (vCOMB4, mem (mLCOMB4));
    int R = mul (vCOMB1, mem (mRCOMB1)) + mul (vCOMB2, mem (mRCOMB2)) + mul (vCOMB3, mem (mRCOMB3)) + mul (vCOMB4, mem (mRCOMB4));
    L = clamp16 (L);
    R = clamp16 (R);

    // Late reverb: two all-pass stages
    auto apf = [&] (int x, int mAddr, int d, int v)
    {
        const int delayed = mem (mAddr - d);
        x = clamp16 (x - mul (v, delayed));
        mem (mAddr) = (int16_t) x;
        return clamp16 (mul (x, v) + delayed);
    };
    L = apf (L, mLAPF1, dAPF1, vAPF1);
    R = apf (R, mRAPF1, dAPF1, vAPF1);
    L = apf (L, mLAPF2, dAPF2, vAPF2);
    R = apf (R, mRAPF2, dAPF2, vAPF2);

    lout = L;
    rout = R;
    bufferAddress = (bufferAddress + 1) % workSize;
}

void Ps1Reverb::process (float* l, float* r, int n, const EffectParams& p)
{
    if (work.empty()) return;
    if (p.ps1Preset != currentPreset) loadPreset (p.ps1Preset);

    const int evol = std::clamp ((int) std::lround (p.ps1Depth * 32767.0f), 0, 32767);
    const int H = (int) inHistL.size();   // power of two, >= 39

    for (int i = 0; i < n; ++i)
    {
        histPos = (histPos + 1) & (H - 1);
        inHistL[(size_t) histPos] = toInt16 (l[i]);
        inHistR[(size_t) histPos] = toInt16 (r[i]);

        auto fir = [&] (const std::array<int, 64>& h)
        {
            int64_t acc = 0;
            for (int k = 0; k < 39; ++k)
                acc += (int64_t) ps1Fir[k] * h[(size_t) ((histPos - k) & (H - 1))];
            return acc;
        };

        // The reverb core runs at half rate: decimate in, zero-stuff out.
        int coreL = 0, coreR = 0;
        phase = ! phase;
        if (phase)
        {
            const int inL = clamp16 ((int) (fir (inHistL) >> 15));
            const int inR = clamp16 ((int) (fir (inHistR) >> 15));
            tick (inL, inR, coreL, coreR);
        }
        outHistL[(size_t) histPos] = coreL;
        outHistR[(size_t) histPos] = coreR;

        const int wetL = clamp16 ((int) (fir (outHistL) >> 14));   // x2 for the zero-stuffing
        const int wetR = clamp16 ((int) (fir (outHistR) >> 14));

        l[i] += (float) ((wetL * evol) >> 15) * (1.0f / 32768.0f);
        r[i] += (float) ((wetR * evol) >> 15) * (1.0f / 32768.0f);
    }
}

} // namespace lotus
