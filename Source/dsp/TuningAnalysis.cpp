#include "TuningAnalysis.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>

namespace lotus
{

namespace
{
constexpr double yinThreshold = 0.12;

double centsBetween (double a, double b) { return 1200.0 * std::log2 (a / b); }
double keyHz (int key) { return 440.0 * std::pow (2.0, (key - 69) / 12.0); }
} // namespace

// ---------------------------------------------------------------------------------
// YIN (de Cheveigné & Kawahara, 2002): cumulative-mean-normalised difference, first
// dip under the threshold, parabolic interpolation.

PitchEstimate detectPitch (const float* x, int n, double sr, double minHz, double maxHz)
{
    PitchEstimate result;
    const int minTau = std::max (2, (int) std::floor (sr / maxHz));
    int maxTau = (int) std::ceil (sr / minHz);
    maxTau = std::min (maxTau, n / 2);
    const int w = std::min (n - maxTau - 2, std::max (maxTau, 1024));
    if (maxTau <= minTau + 2 || w < 64) return result;

    double energy = 0.0;
    for (int j = 0; j < w + maxTau && j < n; ++j) energy += (double) x[j] * x[j];
    if (energy < 1e-6 * (w + maxTau)) return result;   // silence

    std::vector<double> d ((size_t) maxTau + 2, 0.0), dn ((size_t) maxTau + 2, 1.0);
    for (int tau = 1; tau <= maxTau + 1; ++tau)
    {
        double sum = 0.0;
        const float* a = x;
        const float* b = x + tau;
        for (int j = 0; j < w; ++j)
        {
            const double diff = (double) a[j] - b[j];
            sum += diff * diff;
        }
        d[(size_t) tau] = sum;
    }
    double running = 0.0;
    for (int tau = 1; tau <= maxTau + 1; ++tau)
    {
        running += d[(size_t) tau];
        dn[(size_t) tau] = running > 0.0 ? d[(size_t) tau] * tau / running : 1.0;
    }

    auto localMin = [&] (int t)
    {
        while (t + 1 <= maxTau && dn[(size_t) t + 1] < dn[(size_t) t]) ++t;
        return t;
    };

    int tau = -1;
    for (int t = minTau; t <= maxTau; ++t)
        if (dn[(size_t) t] < yinThreshold) { tau = localMin (t); break; }
    if (tau < 0)
    {
        tau = minTau;
        for (int t = minTau; t <= maxTau; ++t)
            if (dn[(size_t) t] < dn[(size_t) tau]) tau = t;
    }

    // Octave guard: if a multiple of the period is a clearly better dip, the waveform
    // was only roughly periodic at the shorter one.
    for (int m : { 2, 3 })
    {
        const int cand = tau * m;
        if (cand + 1 > maxTau) break;
        const int best = localMin (std::max (minTau, cand - 2));
        if (dn[(size_t) tau] > 0.03 && dn[(size_t) best] < 0.5 * dn[(size_t) tau])
            tau = best;
    }

    double refined = tau;
    if (tau > 1 && tau < maxTau)
    {
        const double a = dn[(size_t) tau - 1], b = dn[(size_t) tau], c = dn[(size_t) tau + 1];
        const double denom = a - 2.0 * b + c;
        if (std::abs (denom) > 1e-12) refined = tau + 0.5 * (a - c) / denom;
    }

    result.hz = sr / refined;
    result.clarity = std::clamp (1.0 - dn[(size_t) tau], 0.0, 1.0);
    return result;
}

// ---------------------------------------------------------------------------------

namespace
{
std::string lower (std::string s)
{
    for (auto& c : s) c = (char) std::tolower ((unsigned char) c);
    return s;
}

bool isSoundEffect (const Preset& p)
{
    const auto n = " " + lower (p.name) + " ";
    for (const char* word : { "sfx", " fx", "fx ", "noise", "fret", "breath", "seashore", "bird", "phone", "helicopter",
                              "applause", "gunshot", " gun", "explosion", "laser", "thunder", " rain", "wind ", "scratch",
                              "reverse", "taiko", "agogo", "woodblock", "wood block", "whistle" })
        if (n.find (word) != std::string::npos) return true;
    return false;
}


/** Up to `count` samples of the region from `offset`, following its loop. */
std::vector<float> extract (const SoundFont& sf, const Region& r, size_t offset, size_t count)
{
    const auto& pool = sf.samples();
    std::vector<float> out;
    out.reserve (count);
    const bool loops = r.loopMode != LoopMode::None && r.loopEnd > r.loopStart + 16;
    size_t idx = r.start + offset;
    while (out.size() < count)
    {
        if (loops && idx >= r.loopEnd) idx = r.loopStart + (idx - r.loopStart) % (r.loopEnd - r.loopStart);
        if (idx >= r.end || idx >= pool.size()) break;
        out.push_back (pool[idx] * (1.0f / 32768.0f));
        ++idx;
    }
    return out;
}

const Region* regionFor (const Preset& p, int& playKey)
{
    // Of the layers sounding at C5, measure the one with the least intentional
    // transposition (pads often stack a fifth or an octave on top), then the loudest.
    const Region* best = nullptr;
    auto semis = [] (const Region& r) { return std::abs (std::lround (r.tuneCents / 100.0f)); };
    for (const auto& r : p.regions)
        if (tuningReferenceKey >= r.loKey && tuningReferenceKey <= r.hiKey && 100 >= r.loVel && 100 <= r.hiVel)
            if (best == nullptr || semis (r) < semis (*best) || (semis (r) == semis (*best) && r.gain > best->gain))
                best = &r;
    if (best != nullptr) { playKey = tuningReferenceKey; return best; }

    // Nothing at C5: the zone nearest to it.
    int bestDist = 1 << 20;
    for (const auto& r : p.regions)
    {
        const int dist = r.loKey > tuningReferenceKey ? r.loKey - tuningReferenceKey : tuningReferenceKey - r.hiKey;
        if (dist < bestDist) { bestDist = dist; best = &r; }
    }
    if (best != nullptr) playKey = std::clamp (tuningReferenceKey, (int) best->loKey, (int) best->hiKey);
    return best;
}

/** The sample's own fundamental (at its native rate), or 0 if there isn't a trustworthy one. */
double sampleFundamental (const SoundFont& sf, const Region& r, bool thorough, double& clarity)
{
    const double sr = r.sampleRate;
    const size_t len = r.end - r.start;
    const bool loops = r.loopMode != LoopMode::None && r.loopEnd > r.loopStart + 16;
    const size_t want = (size_t) std::ceil (sr / 30.0) + std::max<size_t> (1024, (size_t) std::ceil (sr / 30.0)) + 4;

    std::vector<size_t> offsets;
    if (loops) offsets.push_back (r.loopStart - r.start);
    if (thorough || ! loops)
    {
        offsets.push_back ((size_t) std::min ((double) len * 0.15, 0.06 * sr));
        if (thorough && len > want * 2) offsets.push_back (len * 2 / 5);
    }

    std::vector<PitchEstimate> found;
    double claritySum = 0.0;
    int tried = 0;
    for (size_t off : offsets)
    {
        const auto buf = extract (sf, r, off, want);
        if (buf.size() < 256) continue;
        const auto e = detectPitch (buf.data(), (int) buf.size(), sr);
        ++tried;
        claritySum += e.clarity;
        if (e.hz > 0.0 && e.clarity >= 0.8) found.push_back (e);
    }

    clarity = tried > 0 ? claritySum / tried : 0.0;
    if (found.empty()) return 0.0;

    std::sort (found.begin(), found.end(), [] (const PitchEstimate& a, const PitchEstimate& b) { return a.hz < b.hz; });
    const double median = found[found.size() / 2].hz;
    for (const auto& e : found)
        if (std::abs (centsBetween (e.hz, median)) > 35.0) return -1.0;   // windows disagree
    return median;
}
} // namespace

bool isDrumKit (const Preset& p)
{
    if (p.bank == 128) return true;   // GM percussion bank

    const auto n = " " + lower (p.name) + " ";
    for (const char* word : { "drum", "kit", "perc", "cymbal", "snare", "kick", "hihat", "hi-hat", " tom " })
        if (n.find (word) != std::string::npos) return true;

    // Lots of single-key zones with different samples: a mapped kit.
    if (p.regions.size() >= 8)
    {
        int single = 0;
        std::vector<uint32_t> starts;
        for (const auto& r : p.regions)
            if (r.loKey == r.hiKey) { ++single; starts.push_back (r.start); }
        std::sort (starts.begin(), starts.end());
        const auto distinct = std::unique (starts.begin(), starts.end()) - starts.begin();
        if (single * 10 >= (int) p.regions.size() * 7 && distinct >= 4) return true;
    }
    return false;
}

std::vector<PresetTuning> analyzeTuning (const SoundFont& sf, const TuningOptions& opt,
                                         const std::function<bool (int, int)>& progress)
{
    std::vector<PresetTuning> out;
    const int total = (int) sf.presets.size();

    for (int i = 0; i < total; ++i)
    {
        if (progress && ! progress (i, total)) return {};

        const auto& p = sf.presets[(size_t) i];
        PresetTuning t;
        t.presetIndex = i;
        t.bank = p.bank;
        t.program = p.program;
        t.name = p.name;

        int playKey = tuningReferenceKey;
        const Region* r = regionFor (p, playKey);
        t.playKey = playKey;

        if (isDrumKit (p))          t.status = TuneStatus::DrumKit;
        else if (isSoundEffect (p)) t.status = TuneStatus::SoundEffect;
        else if (r == nullptr)      t.status = TuneStatus::NoZone;
        else
        {
            double clarity = 0.0;
            const double f0 = sampleFundamental (sf, *r, opt.thorough, clarity);
            if (f0 == 0.0)
                t.status = clarity < 0.6 ? TuneStatus::Atonal : TuneStatus::Unclear;
            else if (f0 < 0.0)
                t.status = TuneStatus::Unclear;
            else
            {
                const int key = r->fixedKey >= 0 ? r->fixedKey : playKey;
                const double cents = (key - r->rootKey) * (double) r->scaleTuning + r->tuneCents;
                t.playedHz = f0 * std::pow (2.0, cents / 1200.0);
                t.errorCents = centsBetween (t.playedHz, keyHz (playKey));
                if (std::abs (t.errorCents) <= opt.toleranceCents)
                    t.status = TuneStatus::InTune;
                else
                {
                    t.status = TuneStatus::Adjust;
                    t.correctionCents = (int) -std::lround (t.errorCents);
                    const double octaves = std::round (t.errorCents / 1200.0);
                    const double semis = std::round (t.errorCents / 100.0);
                    if (octaves != 0.0 && std::abs (t.errorCents - octaves * 1200.0) <= 30.0)
                        t.kind = AdjustKind::Octave;
                    else if (semis != 0.0 && std::abs (t.errorCents - semis * 100.0) <= 25.0)
                        t.kind = AdjustKind::RootNote;
                    else
                        t.kind = std::abs (t.errorCents) < 100.0 ? AdjustKind::Detune : AdjustKind::RootNote;
                }
            }
        }
        out.push_back (t);
    }

    // Octave offsets are recommended only when most of the bank shares the same one.
    int tonal = 0;
    std::map<int, int> octaveVotes;
    for (const auto& t : out)
    {
        if (t.status == TuneStatus::InTune) { ++tonal; ++octaveVotes[0]; }
        if (t.status == TuneStatus::Adjust)
        {
            ++tonal;
            ++octaveVotes[t.kind == AdjustKind::Octave ? (int) std::round (t.errorCents / 1200.0) : 99];
        }
    }
    int sharedOctave = 0;
    for (const auto& [oct, votes] : octaveVotes)
        if (oct != 0 && oct != 99 && votes * 2 > tonal) sharedOctave = oct;

    for (auto& t : out)
        if (t.status == TuneStatus::Adjust)
            t.recommended = t.kind != AdjustKind::Octave || (int) std::round (t.errorCents / 1200.0) == sharedOctave;

    if (progress) progress (total, total);
    return out;
}

bool looksMistuned (const std::vector<PresetTuning>& results)
{
    std::vector<double> errors;   // tonal presets only
    int rootErrors = 0;
    std::map<int, int> octaveVotes;
    for (const auto& r : results)
    {
        if (r.status != TuneStatus::InTune && r.status != TuneStatus::Adjust) continue;
        errors.push_back (r.errorCents);
        if (r.status == TuneStatus::Adjust && r.kind != AdjustKind::Octave) ++rootErrors;
        const int oct = (int) std::round (r.errorCents / 1200.0);
        if (std::abs (r.errorCents - oct * 1200.0) <= 30.0) ++octaveVotes[oct];
    }
    const int tonal = (int) errors.size();
    if (tonal == 0) return false;

    // Many wrong root notes or detuned presets.
    if (rootErrors >= 3 && rootErrors * 100 >= tonal * 10) return true;

    // The whole bank shifted by the same octave (a C4/C5 naming mix-up, typically).
    for (const auto& [oct, votes] : octaveVotes)
        if (oct != 0 && votes * 2 > tonal) return true;

    // The whole bank detuned.
    std::sort (errors.begin(), errors.end());
    const double median = errors[errors.size() / 2];
    return tonal >= 3 && std::abs (median) >= 20.0 && std::abs (median) <= 100.0;
}

std::shared_ptr<const SoundFont> withCorrections (const SoundFont& base, const TuningCorrections& corrections)
{
    auto copy = std::make_shared<SoundFont> (base);   // shares the sample pool
    for (auto& p : copy->presets)
    {
        const auto it = corrections.find ({ p.bank, p.program });
        if (it == corrections.end() || it->second == 0) continue;
        for (auto& r : p.regions) r.tuneCents += (float) it->second;
    }
    return copy;
}

// ---------------------------------------------------------------------------------
// Writer: copies the file, rebuilding only pbag/pgen in the pdta list.

namespace
{
uint32_t rd32 (const std::vector<uint8_t>& b, size_t at) { return (uint32_t) b[at] | ((uint32_t) b[at + 1] << 8) | ((uint32_t) b[at + 2] << 16) | ((uint32_t) b[at + 3] << 24); }
uint16_t rd16 (const std::vector<uint8_t>& b, size_t at) { return (uint16_t) (b[at] | (b[at + 1] << 8)); }
void wr32 (std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((uint8_t) (v >> (8 * i))); }
void wr16 (std::vector<uint8_t>& b, uint16_t v) { b.push_back ((uint8_t) v); b.push_back ((uint8_t) (v >> 8)); }
bool isId (const std::vector<uint8_t>& b, size_t at, const char* id) { return std::memcmp (b.data() + at, id, 4) == 0; }

struct GenRec { uint16_t op; uint16_t amount; };
constexpr uint16_t genInstrument = 41, genCoarse = 51, genFine = 52;

void appendChunk (std::vector<uint8_t>& out, const char* id, const std::vector<uint8_t>& body)
{
    out.insert (out.end(), id, id + 4);
    wr32 (out, (uint32_t) body.size());
    out.insert (out.end(), body.begin(), body.end());
    if (body.size() & 1) out.push_back (0);
}
} // namespace

bool writeCorrectedSoundFont (const std::string& srcPath, const std::string& dstPath,
                              const TuningCorrections& corrections, std::string& error)
{
    std::vector<uint8_t> in;
    {
        std::ifstream f (srcPath, std::ios::binary | std::ios::ate);
        if (! f) { error = "Could not open the original SoundFont."; return false; }
        in.resize ((size_t) f.tellg());
        f.seekg (0);
        if (! f.read ((char*) in.data(), (std::streamsize) in.size())) { error = "Could not read the original SoundFont."; return false; }
    }
    if (in.size() < 12 || ! isId (in, 0, "RIFF") || ! isId (in, 8, "sfbk")) { error = "Not a SoundFont 2 file."; return false; }

    const size_t riffEnd = std::min (in.size(), (size_t) rd32 (in, 4) + 8);
    std::vector<uint8_t> body;   // everything after "RIFF" size "sfbk"
    bool donePdta = false;

    size_t pos = 12;
    while (pos + 8 <= riffEnd)
    {
        const size_t size = rd32 (in, pos + 4);
        const size_t start = pos + 8;
        if (start + size > riffEnd) { error = "The SoundFont is truncated."; return false; }
        const size_t padded = size + (size & 1);

        if (isId (in, pos, "LIST") && size >= 4 && isId (in, start, "pdta"))
        {
            // Collect the sub-chunks, keeping their order.
            std::vector<std::pair<std::string, std::vector<uint8_t>>> subs;
            size_t sp = start + 4;
            while (sp + 8 <= start + size)
            {
                const size_t ss = rd32 (in, sp + 4);
                if (sp + 8 + ss > start + size) break;
                subs.push_back ({ std::string ((const char*) in.data() + sp, 4),
                                  std::vector<uint8_t> (in.begin() + (long) (sp + 8), in.begin() + (long) (sp + 8 + ss)) });
                sp += 8 + ss + (ss & 1);
            }

            auto find = [&] (const char* id) -> std::vector<uint8_t>*
            {
                for (auto& s : subs) if (s.first == id) return &s.second;
                return nullptr;
            };
            auto* phdr = find ("phdr");
            auto* pbag = find ("pbag");
            auto* pgen = find ("pgen");
            if (! phdr || ! pbag || ! pgen) { error = "The SoundFont is missing preset data."; return false; }

            const size_t nPresets = phdr->size() / 38, nBags = pbag->size() / 4, nGens = pgen->size() / 4;
            if (nPresets < 2 || nBags < 1) { error = "The SoundFont has no presets."; return false; }

            std::vector<GenRec> gens (nGens);
            for (size_t i = 0; i < nGens; ++i) gens[i] = { rd16 (*pgen, i * 4), rd16 (*pgen, i * 4 + 2) };
            std::vector<uint16_t> bagGen (nBags), bagMod (nBags);
            for (size_t i = 0; i < nBags; ++i) { bagGen[i] = rd16 (*pbag, i * 4); bagMod[i] = rd16 (*pbag, i * 4 + 2); }

            std::vector<std::vector<GenRec>> zones (nBags - 1);
            for (size_t z = 0; z + 1 < nBags; ++z)
                for (size_t g = bagGen[z]; g < std::min<size_t> (bagGen[z + 1], nGens); ++g)
                    zones[z].push_back (gens[g]);
            const std::vector<GenRec> tail (gens.begin() + std::min<size_t> (bagGen[nBags - 1], nGens), gens.end());

            auto get = [] (const std::vector<GenRec>& zone, uint16_t op, bool& found) -> int
            {
                for (const auto& g : zone) if (g.op == op) { found = true; return (int) (int16_t) g.amount; }
                found = false;
                return 0;
            };
            auto set = [] (std::vector<GenRec>& zone, uint16_t op, int value)
            {
                for (auto& g : zone) if (g.op == op) { g.amount = (uint16_t) (int16_t) value; return; }
                zone.insert (zone.end() - 1, GenRec { op, (uint16_t) (int16_t) value });   // instrument stays last
            };

            for (size_t p = 0; p + 1 < nPresets; ++p)
            {
                const int program = rd16 (*phdr, p * 38 + 20), bank = rd16 (*phdr, p * 38 + 22);
                const auto it = corrections.find ({ bank, program });
                if (it == corrections.end() || it->second == 0) continue;

                const size_t from = rd16 (*phdr, p * 38 + 24), to = std::min<size_t> (rd16 (*phdr, (p + 1) * 38 + 24), nBags - 1);
                if (from >= to) continue;

                const auto& first = zones[from];
                const bool hasGlobal = first.empty() || first.back().op != genInstrument;
                bool fc = false, ff = false;
                const int globalCoarse = hasGlobal ? get (first, genCoarse, fc) : 0;
                const int globalFine = hasGlobal ? get (first, genFine, ff) : 0;

                for (size_t z = from; z < to; ++z)
                {
                    auto& zone = zones[z];
                    if (zone.empty() || zone.back().op != genInstrument) continue;
                    bool hc = false, hf = false;
                    int coarse = get (zone, genCoarse, hc), fine = get (zone, genFine, hf);
                    if (! hc) coarse = globalCoarse;
                    if (! hf) fine = globalFine;
                    const int totalCents = coarse * 100 + fine + it->second;
                    int newCoarse = (int) std::lround (totalCents / 100.0);
                    newCoarse = std::clamp (newCoarse, -120, 120);
                    const int newFine = std::clamp (totalCents - newCoarse * 100, -99, 99);
                    set (zone, genCoarse, newCoarse);
                    set (zone, genFine, newFine);
                }
            }

            std::vector<uint8_t> newGen, newBag;
            size_t count = 0;
            for (size_t z = 0; z + 1 < nBags; ++z)
            {
                if (count > 0xFFFF) { error = "Too many generators to save this SoundFont. Keep the fixes in the project instead."; return false; }
                wr16 (newBag, (uint16_t) count);
                wr16 (newBag, bagMod[z]);
                for (const auto& g : zones[z]) { wr16 (newGen, g.op); wr16 (newGen, g.amount); }
                count += zones[z].size();
            }
            if (count > 0xFFFF) { error = "Too many generators to save this SoundFont. Keep the fixes in the project instead."; return false; }
            wr16 (newBag, (uint16_t) count);
            wr16 (newBag, bagMod[nBags - 1]);
            for (const auto& g : tail) { wr16 (newGen, g.op); wr16 (newGen, g.amount); }

            *pbag = std::move (newBag);
            *pgen = std::move (newGen);

            std::vector<uint8_t> list;
            list.insert (list.end(), { 'p', 'd', 't', 'a' });
            for (auto& s : subs) appendChunk (list, s.first.c_str(), s.second);
            appendChunk (body, "LIST", list);
            donePdta = true;
        }
        else
        {
            body.insert (body.end(), in.begin() + (long) pos, in.begin() + (long) (start + std::min (padded, riffEnd - start)));
        }
        pos = start + padded;
    }

    if (! donePdta) { error = "The SoundFont is missing preset data."; return false; }

    std::vector<uint8_t> out;
    out.insert (out.end(), { 'R', 'I', 'F', 'F' });
    wr32 (out, (uint32_t) (body.size() + 4));
    out.insert (out.end(), { 's', 'f', 'b', 'k' });
    out.insert (out.end(), body.begin(), body.end());

    std::ofstream f (dstPath, std::ios::binary | std::ios::trunc);
    if (! f) { error = "Could not create the file."; return false; }
    f.write ((const char*) out.data(), (std::streamsize) out.size());
    if (! f) { error = "Could not write the file."; return false; }
    return true;
}

} // namespace lotus
