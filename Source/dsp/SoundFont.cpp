#include "SoundFont.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>

namespace lotus
{

namespace
{

// ---------------------------------------------------------------------------------
// Generator operators (SF2.04 spec, section 8.1.2). Only the ones we use are named.
enum Gen : uint16_t
{
    startAddrsOffset = 0,
    endAddrsOffset = 1,
    startloopAddrsOffset = 2,
    endloopAddrsOffset = 3,
    startAddrsCoarseOffset = 4,
    endAddrsCoarseOffset = 12,
    pan = 17,
    delayVolEnv = 33,
    attackVolEnv = 34,
    holdVolEnv = 35,
    decayVolEnv = 36,
    sustainVolEnv = 37,
    releaseVolEnv = 38,
    keynumToVolEnvHold = 39,
    keynumToVolEnvDecay = 40,
    instrument = 41,
    keyRange = 43,
    velRange = 44,
    startloopAddrsCoarseOffset = 45,
    keynum = 46,
    velocity = 47,
    initialAttenuation = 48,
    endloopAddrsCoarseOffset = 50,
    coarseTune = 51,
    fineTune = 52,
    sampleID = 53,
    sampleModes = 54,
    scaleTuning = 56,
    exclusiveClass = 57,
    overridingRootKey = 58,
    numGenerators = 61
};

struct GenList
{
    std::array<int32_t, numGenerators> value {};
    std::array<bool, numGenerators> set {};

    void put (uint16_t op, int32_t amount)
    {
        if (op < numGenerators) { value[op] = amount; set[op] = true; }
    }
};

/** Instrument-level defaults (spec 8.1.3). */
GenList instrumentDefaults()
{
    GenList g;
    g.value.fill (0);
    g.value[8] = 13500;                  // initialFilterFc
    for (int i : { 21, 23, 25, 26, 27, 28, 30, 33, 34, 35, 36, 38 })
        g.value[(size_t) i] = -12000;    // LFO delays and envelope times
    g.value[keyRange] = 0x7F00;          // lo=0, hi=127 (lo in low byte)
    g.value[velRange] = 0x7F00;
    g.value[keynum] = -1;
    g.value[velocity] = -1;
    g.value[scaleTuning] = 100;
    g.value[overridingRootKey] = -1;
    return g;
}

int rangeLo (int32_t v) { return v & 0xFF; }
int rangeHi (int32_t v) { return (v >> 8) & 0xFF; }

float timecentsToSeconds (int32_t tc)
{
    if (tc <= -32768) return 0.0f;
    tc = std::clamp (tc, -12000, 8000);  // spec says ~1ms .. 100s
    return (float) std::pow (2.0, tc / 1200.0);
}

// ---------------------------------------------------------------------------------
// Little RIFF reader with bounds checking.
struct Reader
{
    const uint8_t* p;
    size_t size;

    uint32_t u32 (size_t at) const { return (uint32_t) p[at] | ((uint32_t) p[at + 1] << 8) | ((uint32_t) p[at + 2] << 16) | ((uint32_t) p[at + 3] << 24); }
    uint16_t u16 (size_t at) const { return (uint16_t) (p[at] | (p[at + 1] << 8)); }
    int16_t  s16 (size_t at) const { return (int16_t) u16 (at); }
    bool fourcc (size_t at, const char* id) const { return std::memcmp (p + at, id, 4) == 0; }

    std::string str (size_t at, size_t maxLen) const
    {
        std::string s;
        for (size_t i = 0; i < maxLen && p[at + i] != 0; ++i)
            s += (char) p[at + i];
        while (! s.empty() && s.back() == ' ') s.pop_back();
        return s;
    }
};

struct Chunk { size_t offset = 0; size_t size = 0; bool found = false; };

struct PhdrRec { std::string name; int program, bank, bagIndex; };
struct InstRec { std::string name; int bagIndex; };
struct BagRec  { int genIndex; };
struct GenRec  { uint16_t op; int32_t amount; };   // amount: signed 16 or range word
struct ShdrRec { std::string name; uint32_t start, end, loopStart, loopEnd, rate; int originalPitch; int pitchCorrection; uint16_t type; };

} // namespace

// ---------------------------------------------------------------------------------
std::unique_ptr<SoundFont> SoundFont::loadFromFile (const std::string& path, std::string& error)
{
    std::ifstream in (path, std::ios::binary | std::ios::ate);
    if (! in) { error = "Could not open file."; return nullptr; }

    const auto len = (size_t) in.tellg();
    std::vector<uint8_t> buf (len);
    in.seekg (0);
    if (! in.read ((char*) buf.data(), (std::streamsize) len)) { error = "Could not read file."; return nullptr; }

    return loadFromMemory (buf.data(), buf.size(), error);
}

std::unique_ptr<SoundFont> SoundFont::loadFromMemory (const void* data, size_t size, std::string& error)
{
    const Reader r { static_cast<const uint8_t*> (data), size };

    if (size < 12 || ! r.fourcc (0, "RIFF") || ! r.fourcc (8, "sfbk"))
    {
        error = "Not a SoundFont 2 file.";
        return nullptr;
    }

    const size_t riffEnd = std::min (size, (size_t) r.u32 (4) + 8);

    // Sub-chunks we care about.
    Chunk inam, smpl, phdr, pbag, pgen, inst, ibag, igen, shdr;

    auto scanList = [&] (size_t from, size_t to, auto&& onChunk)
    {
        size_t pos = from;
        while (pos + 8 <= to)
        {
            const size_t csize = r.u32 (pos + 4);
            const size_t body = pos + 8;
            if (body + csize > to) break;  // truncated, stop here
            onChunk (pos, body, csize);
            pos = body + csize + (csize & 1);
        }
    };

    scanList (12, riffEnd, [&] (size_t pos, size_t body, size_t csize)
    {
        if (! r.fourcc (pos, "LIST") || csize < 4) return;

        scanList (body + 4, body + csize, [&] (size_t sp, size_t sbody, size_t ssize)
        {
            auto grab = [&] (const char* id, Chunk& c)
            {
                if (r.fourcc (sp, id)) c = { sbody, ssize, true };
            };
            grab ("INAM", inam); grab ("smpl", smpl);
            grab ("phdr", phdr); grab ("pbag", pbag); grab ("pgen", pgen);
            grab ("inst", inst); grab ("ibag", ibag); grab ("igen", igen);
            grab ("shdr", shdr);
        });
    });

    if (! (phdr.found && pbag.found && pgen.found && inst.found && ibag.found && igen.found && shdr.found))
    {
        error = "SoundFont is missing required preset data.";
        return nullptr;
    }
    if (! smpl.found)
    {
        error = "SoundFont has no sample data.";
        return nullptr;
    }

    auto sf = std::make_unique<SoundFont>();
    if (inam.found) sf->name = r.str (inam.offset, inam.size);

    // --- sample pool -----------------------------------------------------------
    const size_t numSamples = smpl.size / 2;
    sf->samples.resize (numSamples);
    for (size_t i = 0; i < numSamples; ++i)
        sf->samples[i] = r.s16 (smpl.offset + i * 2);

    // --- hydra records ---------------------------------------------------------
    std::vector<PhdrRec> phdrs;
    for (size_t i = 0; i + 38 <= phdr.size; i += 38)
    {
        const size_t o = phdr.offset + i;
        phdrs.push_back ({ r.str (o, 20), r.u16 (o + 20), r.u16 (o + 22), r.u16 (o + 24) });
    }

    std::vector<InstRec> insts;
    for (size_t i = 0; i + 22 <= inst.size; i += 22)
    {
        const size_t o = inst.offset + i;
        insts.push_back ({ r.str (o, 20), r.u16 (o + 20) });
    }

    auto readBags = [&] (const Chunk& c)
    {
        std::vector<BagRec> v;
        for (size_t i = 0; i + 4 <= c.size; i += 4)
            v.push_back ({ r.u16 (c.offset + i) });
        return v;
    };
    auto readGens = [&] (const Chunk& c)
    {
        std::vector<GenRec> v;
        for (size_t i = 0; i + 4 <= c.size; i += 4)
        {
            const uint16_t op = r.u16 (c.offset + i);
            const bool isRange = (op == keyRange || op == velRange);
            v.push_back ({ op, isRange ? (int32_t) r.u16 (c.offset + i + 2) : (int32_t) r.s16 (c.offset + i + 2) });
        }
        return v;
    };

    const auto pbags = readBags (pbag);
    const auto ibags = readBags (ibag);
    const auto pgens = readGens (pgen);
    const auto igens = readGens (igen);

    std::vector<ShdrRec> shdrs;
    for (size_t i = 0; i + 46 <= shdr.size; i += 46)
    {
        const size_t o = shdr.offset + i;
        shdrs.push_back ({ r.str (o, 20), r.u32 (o + 20), r.u32 (o + 24), r.u32 (o + 28), r.u32 (o + 32), r.u32 (o + 36),
                           r.p[o + 40], (int) (int8_t) r.p[o + 41], r.u16 (o + 44) });
    }

    if (phdrs.size() < 2 || insts.size() < 2)
    {
        error = "SoundFont contains no presets.";
        return nullptr;
    }

    // Collect the generators of bag `b` (bags are terminated by the next bag's index).
    auto zoneGens = [] (const std::vector<BagRec>& bags, const std::vector<GenRec>& gens, size_t b)
    {
        std::vector<GenRec> out;
        if (b + 1 >= bags.size()) return out;
        const size_t from = (size_t) bags[b].genIndex;
        const size_t to = std::min ((size_t) bags[b + 1].genIndex, gens.size());
        for (size_t g = from; g < to; ++g) out.push_back (gens[g]);
        return out;
    };

    // --- flatten every preset ----------------------------------------------------
    for (size_t pi = 0; pi + 1 < phdrs.size(); ++pi)   // last record is the EOP terminator
    {
        Preset preset;
        preset.name = phdrs[pi].name;
        preset.program = phdrs[pi].program;
        preset.bank = phdrs[pi].bank;

        const size_t pbFrom = (size_t) phdrs[pi].bagIndex;
        const size_t pbTo = std::min ((size_t) phdrs[pi + 1].bagIndex, pbags.size() > 0 ? pbags.size() - 1 : 0);

        GenList presetGlobal;  // preset generators are offsets, so "unset" means 0
        bool firstZone = true;

        for (size_t pb = pbFrom; pb < pbTo; ++pb)
        {
            const auto gens = zoneGens (pbags, pgens, pb);
            const bool hasInstrument = ! gens.empty() && gens.back().op == instrument;

            if (! hasInstrument)
            {
                if (firstZone)  // global preset zone
                    for (auto& g : gens) presetGlobal.put (g.op, g.amount);
                firstZone = false;
                continue;
            }
            firstZone = false;

            GenList pz = presetGlobal;
            for (auto& g : gens) pz.put (g.op, g.amount);

            const int pKeyLo = pz.set[keyRange] ? rangeLo (pz.value[keyRange]) : 0;
            const int pKeyHi = pz.set[keyRange] ? rangeHi (pz.value[keyRange]) : 127;
            const int pVelLo = pz.set[velRange] ? rangeLo (pz.value[velRange]) : 0;
            const int pVelHi = pz.set[velRange] ? rangeHi (pz.value[velRange]) : 127;

            const size_t instIndex = (size_t) gens.back().amount;
            if (instIndex + 1 >= insts.size()) continue;

            const size_t ibFrom = (size_t) insts[instIndex].bagIndex;
            const size_t ibTo = std::min ((size_t) insts[instIndex + 1].bagIndex, ibags.size() > 0 ? ibags.size() - 1 : 0);

            GenList instGlobal = instrumentDefaults();
            bool firstIZone = true;

            for (size_t ib = ibFrom; ib < ibTo; ++ib)
            {
                const auto igs = zoneGens (ibags, igens, ib);
                const bool hasSample = ! igs.empty() && igs.back().op == sampleID;

                if (! hasSample)
                {
                    if (firstIZone)
                        for (auto& g : igs) instGlobal.put (g.op, g.amount);
                    firstIZone = false;
                    continue;
                }
                firstIZone = false;

                GenList z = instGlobal;
                for (auto& g : igs) z.put (g.op, g.amount);

                const size_t sIndex = (size_t) (uint16_t) igs.back().amount;
                if (sIndex >= shdrs.size()) continue;
                const auto& s = shdrs[sIndex];
                if (s.type & 0x8000) continue;  // ROM sample, not available

                // Key / velocity: intersection of preset and instrument ranges.
                const int keyLo = std::max (pKeyLo, rangeLo (z.value[keyRange]));
                const int keyHi = std::min (pKeyHi, rangeHi (z.value[keyRange]));
                const int velLo = std::max (pVelLo, rangeLo (z.value[velRange]));
                const int velHi = std::min (pVelHi, rangeHi (z.value[velRange]));
                if (keyLo > keyHi || velLo > velHi) continue;

                // Preset generators add to the instrument ones (spec 9.4), except for
                // the non-additive / instrument-only generators.
                auto v = [&] (Gen g) -> int32_t
                {
                    int32_t value = z.value[g];
                    switch (g)
                    {
                        case startAddrsOffset: case endAddrsOffset: case startloopAddrsOffset: case endloopAddrsOffset:
                        case startAddrsCoarseOffset: case endAddrsCoarseOffset: case startloopAddrsCoarseOffset:
                        case endloopAddrsCoarseOffset: case keynum: case velocity: case sampleModes:
                        case exclusiveClass: case overridingRootKey: case sampleID: case instrument:
                        case keyRange: case velRange:
                            break;
                        default:
                            if (pz.set[g]) value += pz.value[g];
                            break;
                    }
                    return value;
                };

                Region rg;
                rg.loKey = (uint8_t) keyLo; rg.hiKey = (uint8_t) keyHi;
                rg.loVel = (uint8_t) velLo; rg.hiVel = (uint8_t) velHi;

                const int64_t poolEnd = (int64_t) sf->samples.size();
                auto addr = [&] (uint32_t base, Gen fine, Gen coarse)
                {
                    const int64_t a = (int64_t) base + v (fine) + 32768LL * v (coarse);
                    return (uint32_t) std::clamp<int64_t> (a, 0, poolEnd);
                };

                rg.start = addr (s.start, startAddrsOffset, startAddrsCoarseOffset);
                rg.end = addr (s.end, endAddrsOffset, endAddrsCoarseOffset);
                rg.loopStart = addr (s.loopStart, startloopAddrsOffset, startloopAddrsCoarseOffset);
                rg.loopEnd = addr (s.loopEnd, endloopAddrsOffset, endloopAddrsCoarseOffset);
                if (rg.end <= rg.start) continue;

                rg.loopStart = std::clamp (rg.loopStart, rg.start, rg.end);
                rg.loopEnd = std::clamp (rg.loopEnd, rg.start, rg.end);

                const int mode = v (sampleModes) & 3;
                rg.loopMode = mode == 1 ? LoopMode::Continuous : mode == 3 ? LoopMode::UntilRelease : LoopMode::None;
                if (rg.loopEnd <= rg.loopStart + 1) rg.loopMode = LoopMode::None;   // degenerate loop

                const int rootOverride = v (overridingRootKey);
                rg.rootKey = (rootOverride >= 0 && rootOverride <= 127) ? rootOverride
                           : (s.originalPitch <= 127 ? s.originalPitch : 60);
                rg.tuneCents = (float) (v (coarseTune) * 100 + v (fineTune) + s.pitchCorrection);
                rg.scaleTuning = (float) v (scaleTuning);
                rg.sampleRate = s.rate > 0 ? (float) s.rate : 44100.0f;

                // Attenuation in centibels. Like FluidSynth and EMU hardware we scale by
                // 0.4: most SoundFonts were voiced on hardware that applied this factor.
                const float attenCb = (float) std::clamp (v (initialAttenuation), 0, 1440) * 0.4f;
                rg.gain = std::pow (10.0f, -attenCb / 200.0f);
                rg.pan = std::clamp ((float) v (pan) / 500.0f, -1.0f, 1.0f);
                rg.exclusiveClass = v (exclusiveClass);
                rg.fixedKey = (v (keynum) >= 0 && v (keynum) <= 127) ? v (keynum) : -1;
                rg.fixedVelocity = (v (velocity) >= 1 && v (velocity) <= 127) ? v (velocity) : -1;

                rg.volEnv.delay = timecentsToSeconds (v (delayVolEnv));
                rg.volEnv.attack = timecentsToSeconds (v (attackVolEnv));
                rg.volEnv.hold = timecentsToSeconds (v (holdVolEnv));
                rg.volEnv.decay = timecentsToSeconds (v (decayVolEnv));
                rg.volEnv.release = timecentsToSeconds (v (releaseVolEnv));
                rg.volEnv.sustain = std::pow (10.0f, -(float) std::clamp (v (sustainVolEnv), 0, 1440) / 200.0f);
                rg.volEnv.keyToHold = (float) v (keynumToVolEnvHold);
                rg.volEnv.keyToDecay = (float) v (keynumToVolEnvDecay);

                preset.regions.push_back (rg);
            }
        }

        sf->presets.push_back (std::move (preset));
    }

    std::stable_sort (sf->presets.begin(), sf->presets.end(), [] (const Preset& a, const Preset& b)
    {
        return a.bank != b.bank ? a.bank < b.bank : a.program < b.program;
    });

    if (sf->presets.empty())
    {
        error = "SoundFont contains no presets.";
        return nullptr;
    }

    return sf;
}

int SoundFont::findPreset (int bank, int program) const
{
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].bank == bank && presets[i].program == program)
            return (int) i;
    return -1;
}

} // namespace lotus
