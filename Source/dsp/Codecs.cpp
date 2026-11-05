#include "Codecs.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace lotus
{

namespace
{
inline int clamp16 (int v) { return std::clamp (v, -32768, 32767); }

bool cancelled (const std::atomic<bool>* c) { return c != nullptr && c->load (std::memory_order_relaxed); }

// SNES BRR: 16-sample blocks, 4-bit nibbles, shift 0-12, filters 0-3.
// Decoder follows the S-DSP (ares brr.cpp): history is kept doubled (16-bit).

int brrPredict (int filter, int p1, int p2Doubled)
{
    const int p2 = p2Doubled >> 1;
    int s = 0;
    switch (filter)
    {
        case 1: s += p1 >> 1; s += (-p1) >> 5; break;
        case 2: s += p1; s -= p2; s += p2 >> 4; s += (p1 * -3) >> 6; break;
        case 3: s += p1; s -= p2; s += (p1 * -13) >> 7; s += (p2 * 3) >> 4; break;
        default: break;
    }
    return s;
}

inline int brrOutput (int nibble, int shift, int pred)
{
    return (int) (int16_t) (clamp16 (((nibble * (1 << shift)) >> 1) + pred) * 2);
}

std::vector<int16_t> brr (const std::vector<int16_t>& in, const std::atomic<bool>* cancel)
{
    std::vector<int16_t> out (in.size());
    int h1 = 0, h2 = 0;   // decoded history (doubled form, as stored by the DSP)

    for (size_t blk = 0; blk < in.size(); blk += 16)
    {
        if ((blk & 0xFFFF) == 0 && cancelled (cancel)) return {};
        const size_t n = std::min<size_t> (16, in.size() - blk);

        int64_t bestErr = std::numeric_limits<int64_t>::max();
        int bestOut[16] {}, bestH1 = 0, bestH2 = 0;

        for (int filter = 0; filter < 4; ++filter)
        {
            for (int shift = 0; shift <= 12; ++shift)
            {
                int p1 = h1, p2 = h2, dec[16];
                int64_t err = 0;
                for (size_t i = 0; i < n; ++i)
                {
                    // Solve for the nibble, then check its neighbours.
                    const int target = in[blk + i];
                    const int pred = brrPredict (filter, p1, p2);
                    const int guess = (int) std::lround ((double) ((target >> 1) - pred) * 2.0 / (double) (1 << shift));
                    int bestVal = 0;
                    int64_t bestE = std::numeric_limits<int64_t>::max();
                    for (int nib = std::max (-8, guess - 1); nib <= std::min (7, guess + 1); ++nib)
                    {
                        const int v = brrOutput (nib, shift, pred);
                        const int64_t e = (int64_t) (v - target) * (v - target);
                        if (e < bestE) { bestE = e; bestVal = v; }
                    }
                    if (bestE == std::numeric_limits<int64_t>::max())   // guess far outside the range
                    {
                        const int nib = guess < -8 ? -8 : 7;
                        bestVal = brrOutput (nib, shift, pred);
                        bestE = (int64_t) (bestVal - target) * (bestVal - target);
                    }
                    err += bestE;
                    dec[i] = bestVal;
                    p2 = p1;
                    p1 = bestVal;
                    if (err >= bestErr) break;
                }
                if (err < bestErr)
                {
                    bestErr = err;
                    std::copy (dec, dec + n, bestOut);
                    bestH1 = p1;
                    bestH2 = p2;
                }
            }
        }

        for (size_t i = 0; i < n; ++i) out[blk + i] = (int16_t) bestOut[i];
        h1 = bestH1;
        h2 = bestH2;
    }
    return out;
}

// PS1/PS2 SPU-ADPCM: 28-sample blocks, shift 0-12, filters 0-4 (psx-spx).

constexpr int psPos[5] = { 0, 60, 115, 98, 122 };
constexpr int psNeg[5] = { 0, 0, -52, -55, -60 };

std::vector<int16_t> psAdpcm (const std::vector<int16_t>& in, const std::atomic<bool>* cancel)
{
    std::vector<int16_t> out (in.size());
    int old = 0, older = 0;

    for (size_t blk = 0; blk < in.size(); blk += 28)
    {
        if ((blk % (28 * 4096)) == 0 && cancelled (cancel)) return {};
        const size_t n = std::min<size_t> (28, in.size() - blk);

        int64_t bestErr = std::numeric_limits<int64_t>::max();
        int bestOut[28] {}, bestOld = 0, bestOlder = 0;

        for (int filter = 0; filter < 5; ++filter)
        {
            for (int range = 0; range <= 12; ++range)
            {
                int o = old, oo = older, dec[28];
                int64_t err = 0;
                for (size_t i = 0; i < n; ++i)
                {
                    const int pred = (o * psPos[filter] + oo * psNeg[filter] + 32) >> 6;
                    const int target = in[blk + i];
                    // Nibble t contributes t << (12 - range).
                    int t = (int) std::lround ((double) (target - pred) * (1 << range) / 4096.0);
                    t = std::clamp (t, -8, 7);
                    int best = clamp16 (((t * 4096) >> range) + pred);
                    for (int dt : { -1, 1 })
                    {
                        const int t2 = std::clamp (t + dt, -8, 7);
                        const int v = clamp16 (((t2 * 4096) >> range) + pred);
                        if (std::abs (v - target) < std::abs (best - target)) best = v;
                    }
                    err += (int64_t) (best - target) * (best - target);
                    dec[i] = best;
                    oo = o;
                    o = best;
                    if (err >= bestErr) break;
                }
                if (err < bestErr)
                {
                    bestErr = err;
                    std::copy (dec, dec + n, bestOut);
                    bestOld = o;
                    bestOlder = oo;
                }
            }
        }

        for (size_t i = 0; i < n; ++i) out[blk + i] = (int16_t) bestOut[i];
        old = bestOld;
        older = bestOlder;
    }
    return out;
}

// DS IMA-ADPCM (standard IMA step and index tables).

constexpr int imaSteps[89] =
{
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
    73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
    2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493,
    10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};
constexpr int imaIndex[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

std::vector<int16_t> imaAdpcm (const std::vector<int16_t>& in, const std::atomic<bool>* cancel)
{
    std::vector<int16_t> out (in.size());
    int pred = 0, index = 0;

    for (size_t i = 0; i < in.size(); ++i)
    {
        if ((i & 0xFFFFF) == 0 && cancelled (cancel)) return {};
        const int step = imaSteps[index];
        int diff = in[i] - pred;
        int code = 0;
        if (diff < 0) { code = 8; diff = -diff; }
        int delta = step >> 3;
        if (diff >= step)        { code |= 4; diff -= step;        delta += step; }
        if (diff >= (step >> 1)) { code |= 2; diff -= step >> 1;   delta += step >> 1; }
        if (diff >= (step >> 2)) { code |= 1;                      delta += step >> 2; }

        // The DS clamps to -0x7FFF..0x7FFF.
        pred = std::clamp ((code & 8) ? pred - delta : pred + delta, -0x7FFF, 0x7FFF);
        index = std::clamp (index + imaIndex[code & 7], 0, 88);
        out[i] = (int16_t) pred;
    }
    return out;
}

// NES DMC: 1-bit delta on a 7-bit counter, +/-2 per bit, no change at the rails.

std::vector<int16_t> dpcm (const std::vector<int16_t>& in, const std::atomic<bool>* cancel)
{
    std::vector<int16_t> out (in.size());
    int counter = 64;

    for (size_t i = 0; i < in.size(); ++i)
    {
        if ((i & 0xFFFFF) == 0 && cancelled (cancel)) return {};
        const int target = (in[i] + 32768) >> 9;   // 0..127
        if (target > counter) { if (counter <= 125) counter += 2; }
        else if (target < counter) { if (counter >= 2) counter -= 2; }
        out[i] = (int16_t) ((counter - 64) * 512);
    }
    return out;
}
} // namespace

std::vector<int16_t> applyCodec (const std::vector<int16_t>& pool, Codec codec, const std::atomic<bool>* cancel)
{
    switch (codec)
    {
        case Codec::Brr:      return brr (pool, cancel);
        case Codec::PsAdpcm:  return psAdpcm (pool, cancel);
        case Codec::ImaAdpcm: return imaAdpcm (pool, cancel);
        case Codec::Dpcm:     return dpcm (pool, cancel);
        case Codec::None:     break;
    }
    return pool;
}

} // namespace lotus
