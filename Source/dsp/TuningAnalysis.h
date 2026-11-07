#pragma once

// Finds presets whose pitch doesn't match their keys (wrong root notes, detuned
// samples) by measuring what each preset actually plays at C5 (MIDI 60, middle C,
// 261.63 Hz at A4 = 440 Hz) with YIN pitch detection. Drum kits and sounds without a
// clear pitch are recognised and left alone.
//
// A fix is a cents offset per preset. In memory it's added to every region of the
// preset; on disk it's written as preset-level coarse/fine tune, so instruments shared
// with other presets (often drum kits) stay untouched.

#include "SoundFont.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace lotus
{

struct PitchEstimate
{
    double hz = 0.0;
    double clarity = 0.0;   // 0..1, how periodic the signal is (1 = perfectly periodic)
};

/** YIN pitch detection on a mono signal. */
PitchEstimate detectPitch (const float* x, int n, double sampleRate, double minHz = 30.0, double maxHz = 5000.0);

enum class TuneStatus { Adjust, InTune, DrumKit, SoundEffect, Atonal, Unclear, NoZone };

/** Why a preset needs adjusting. Octave-only offsets are often intentional (organ footages,
    harmonics), so they're offered but not selected unless the whole bank shares one. */
enum class AdjustKind { RootNote, Detune, Octave };

struct PresetTuning
{
    int presetIndex = -1;
    int bank = 0, program = 0;
    std::string name;
    TuneStatus status = TuneStatus::Unclear;
    int playKey = 60;          // C5, or the nearest key the preset covers
    double playedHz = 0.0;     // what the preset plays at playKey
    double errorCents = 0.0;   // against 12-TET at A4 = 440 Hz
    int correctionCents = 0;   // what an Adjust would add
    AdjustKind kind = AdjustKind::RootNote;
    bool recommended = false;  // selected for fixing by default
};

struct TuningOptions
{
    bool thorough = true;           // several windows per sample instead of one
    double toleranceCents = 25.0;   // within this, a preset counts as in tune (ensemble patches are often detuned a little)
};

constexpr int tuningReferenceKey = 60;   // "C5" in FL Studio's naming (middle C)

/** Analyses every preset. `progress` is called with (done, total); return false to cancel (result is then empty). */
std::vector<PresetTuning> analyzeTuning (const SoundFont& sf, const TuningOptions& options,
                                         const std::function<bool (int, int)>& progress = {});

/** True if the results show the patterns of a broken bank: many non-octave root errors, the
    whole bank shifted by the same octave, or the whole bank detuned. A few odd presets in an
    otherwise healthy bank don't count (those can still be fixed with a manual check). */
bool looksMistuned (const std::vector<PresetTuning>& results);

using TuningCorrections = std::map<std::pair<int, int>, int>;   // (bank, program) -> cents

/** A copy of `base` (sharing its samples) with the corrections applied. */
std::shared_ptr<const SoundFont> withCorrections (const SoundFont& base, const TuningCorrections& corrections);

/** Writes a copy of the SF2 at srcPath to dstPath with the corrections as preset-level tuning. */
bool writeCorrectedSoundFont (const std::string& srcPath, const std::string& dstPath,
                              const TuningCorrections& corrections, std::string& error);

} // namespace lotus
