#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
juce::String formatTime (float seconds)
{
    if (seconds <= 0.0f)  return "0 ms";
    if (seconds < 1.0f)   return juce::String (juce::roundToInt (seconds * 1000.0f)) + " ms";
    return juce::String (seconds, 2) + " s";
}

float parseTime (const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    const float v = t.getFloatValue();
    if (t.endsWith ("ms")) return v / 1000.0f;
    if (t.endsWith ("s"))  return v;
    return v < 20.0f ? v : v / 1000.0f;   // bare "0.5" is seconds, "250" is ms
}

juce::String percent (float v) { return juce::String (juce::roundToInt (v)) + "%"; }
} // namespace

juce::String formatTuning (float cents, bool asHz)
{
    const double hz = 440.0 * std::pow (2.0, cents / 1200.0);
    if (asHz) return "A4 = " + juce::String (hz, 1) + " Hz";
    return (cents > 0.0f ? "+" : "") + juce::String (cents, 1) + " c";
}

float parseTuning (const juce::String& text, bool bareMeansHz)
{
    const auto t = text.trim().toLowerCase().replace ("a4", "").replace ("=", "").trim();
    const double v = t.getDoubleValue();
    const bool hz = t.endsWith ("hz") || (! t.endsWith ("c") && ! t.endsWith ("cents") && (bareMeansHz || v > 200.0));
    const double cents = hz ? (v > 0.0 ? 1200.0 * std::log2 (v / 440.0) : 0.0) : v;
    return (float) juce::jlimit (-100.0, 100.0, cents);
}

namespace
{

std::unique_ptr<juce::AudioParameterFloat> timeParam (const char* id, const char* name, float maxSeconds, float def)
{
    juce::NormalisableRange<float> range (0.0f, maxSeconds);
    range.setSkewForCentre (maxSeconds * 0.05f);
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, range, def,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return formatTime (v); })
            .withValueFromStringFunction ([] (const juce::String& s) { return parseTime (s); }));
}

std::unique_ptr<juce::AudioParameterFloat> percentParam (const char* id, const char* name, float def)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), def,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return percent (v); })
            .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); }));
}

std::unique_ptr<juce::AudioParameterBool> boolParam (const char* id, const char* name, bool def = false)
{
    return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def);
}

template <typename NameFn>
std::unique_ptr<juce::AudioParameterChoice> choiceParam (const char* id, const char* name, int count, NameFn nameOf, int def)
{
    juce::StringArray items;
    for (int i = 0; i < count; ++i) items.add (nameOf (i));
    return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def);
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout LotusProcessor::createLayout()
{
    using namespace ParamIDs;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (timeParam (attack, "Attack", 10.0f, 0.0f));
    layout.add (timeParam (decay, "Decay", 10.0f, 0.0f));
    layout.add (percentParam (sustain, "Sustain", 100.0f));
    layout.add (timeParam (release, "Release", 10.0f, 0.05f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { noteOff, 1 }, "Note Off", juce::StringArray { "Release", "Cut" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { envSource, 1 }, "Envelope", juce::StringArray { "Lotus", "SF2" }, 0));

    layout.add (choiceParam (console, "Console", lotus::numConsoles,
                             [] (int i) { return lotus::getConsoleProfile ((lotus::ConsoleId) i).name; }, 0));
    layout.add (boolParam (nativeFormat, "Native Sample Format"));
    layout.add (boolParam (voiceLimit, "Hardware Voice Limit"));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { volume, 1 }, "Volume", juce::NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return v <= -60.0f ? juce::String ("-inf dB") : juce::String (v, 1) + " dB"; })
            .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); })));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { bendRange, 1 }, "Bend Range", 0, 24, 2,
        juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return juce::String (v) + " st"; })));
    layout.add (timeParam (glide, "Glide", 2.0f, 0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { tune, 1 }, "Tuning", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float c, int) { return formatTuning (c, false); })
            .withValueFromStringFunction ([] (const juce::String& t) { return parseTuning (t, false); })));

    {
        juce::NormalisableRange<float> amount (0.0f, 200.0f, 0.1f);
        amount.setSkewForCentre (40.0f);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { vibAmount, 1 }, "Vibrato Amount", amount, 0.0f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return juce::String (v, v < 10.0f ? 1 : 0) + " c"; })
                .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); })));
        juce::NormalisableRange<float> speed (0.1f, 20.0f, 0.01f);
        speed.setSkewForCentre (5.0f);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { vibSpeed, 1 }, "Vibrato Speed", speed, 5.5f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + " Hz"; })
                .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); })));
        layout.add (timeParam (vibDelay, "Vibrato Delay", 4.0f, 0.0f));
        layout.add (timeParam (vibAttack, "Vibrato Attack", 4.0f, 0.0f));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { vibShape, 1 }, "Vibrato Shape",
                                                                  juce::StringArray { "Sine", "Triangle", "Square" }, 0));
    }

    layout.add (boolParam (snesOn, "SNES Echo"));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { snesDelay, 1 }, "SNES Echo Delay", 0, 15, 5,
        juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return juce::String (v * 16) + " ms"; })));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { snesFeedback, 1 }, "SNES Echo Feedback", -128, 127, 64,
        juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return percent (v * 100.0f / 128.0f); })));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { snesVolume, 1 }, "SNES Echo Volume", 0, 127, 48,
        juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return percent (v * 100.0f / 128.0f); })));
    layout.add (choiceParam (snesFilter, "SNES Echo Filter", lotus::numSnesFirPresets, lotus::snesFirPresetName, 0));

    layout.add (boolParam (n64On, "N64 Reverb"));
    layout.add (choiceParam (n64Preset, "N64 Reverb Type", lotus::numN64FxPresets, lotus::n64FxPresetName, 1));
    layout.add (percentParam (n64Mix, "N64 Reverb Mix", 50.0f));

    layout.add (boolParam (ps1On, "PS1 Reverb"));
    layout.add (choiceParam (ps1Preset, "PS1 Reverb Type", lotus::numPs1ReverbPresets, lotus::ps1ReverbPresetName, 2));
    layout.add (percentParam (ps1Depth, "PS1 Reverb Depth", 50.0f));

    layout.add (boolParam (ledOn, "Amiga LED Filter"));

    return layout;
}

LotusProcessor::LotusProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "Lotus", createLayout())
{
    using namespace ParamIDs;
    for (const char* id : { attack, decay, sustain, release, noteOff, envSource, console, nativeFormat, voiceLimit,
                            volume, bendRange, glide, tune, vibAmount, vibSpeed, vibDelay, vibAttack, vibShape,
                            snesOn, snesDelay, snesFeedback, snesVolume, snesFilter,
                            n64On, n64Preset, n64Mix, ps1On, ps1Preset, ps1Depth, ledOn })
    {
        raw[id] = params.getRawParameterValue (id);
        jassert (raw[id] != nullptr);
    }

    registrySlot = lotus::VoiceRegistry::get().join();

    startTimerHz (10);
}

LotusProcessor::~LotusProcessor()
{
    stopTimer();
    stopEncoding();
    stopTuningWorker();
    lotus::VoiceRegistry::get().leave (registrySlot);
}

float LotusProcessor::value (const char* id) const
{
    return raw.at (id)->load();
}

lotus::ConsoleId LotusProcessor::getConsole() const
{
    return (lotus::ConsoleId) juce::jlimit (0, lotus::numConsoles - 1, (int) value (ParamIDs::console));
}

double LotusProcessor::getTailLengthSeconds() const
{
    using namespace ParamIDs;
    const bool fx = value (snesOn) > 0.5f || value (ps1On) > 0.5f || value (n64On) > 0.5f;
    return fx ? 10.0 : 0.0;
}

bool LotusProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void LotusProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const juce::SpinLock::ScopedLockType lock (engineLock);
    synth.prepare (sampleRate, samplesPerBlock);
    previewSynth.prepare (sampleRate, samplesPerBlock);
    scratchR.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
    previewL.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
    previewR.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
}

void LotusProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    using namespace ParamIDs;
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    if (buffer.getNumChannels() == 0 || numSamples == 0)
        return;

    const juce::SpinLock::ScopedTryLockType lock (engineLock);
    if (! lock.isLocked())
        return;   // a SoundFont or sample pool is being swapped in; one silent block

    lotus::EngineParams p;
    p.attack = value (attack);
    p.decay = value (decay);
    p.sustain = value (sustain) / 100.0f;
    p.release = value (release);
    p.cutOnNoteOff = value (noteOff) > 0.5f;
    p.useSf2Envelope = value (envSource) > 0.5f;
    p.console = getConsole();
    p.nativeFormat = value (nativeFormat) > 0.5f;
    p.hardwareVoices = value (voiceLimit) > 0.5f;
    const float db = value (volume);
    p.masterGain = db <= -60.0f ? 0.0f : juce::Decibels::decibelsToGain (db);
    p.bendRange = (int) value (bendRange);
    p.glide = value (glide);
    p.tuneCents = value (tune);
    p.vibAmount = value (vibAmount);
    p.vibSpeed = value (vibSpeed);
    p.vibDelay = value (vibDelay);
    p.vibAttack = value (vibAttack);
    p.vibShape = (int) value (vibShape);

    p.fx.snesEcho     = value (snesOn) > 0.5f;
    p.fx.snesDelay    = (int) value (snesDelay);
    p.fx.snesFeedback = (int) value (snesFeedback);
    p.fx.snesVolume   = (int) value (snesVolume);
    p.fx.snesFilter   = (int) value (snesFilter);
    p.fx.n64Reverb    = value (n64On) > 0.5f;
    p.fx.n64Preset    = (int) value (n64Preset);
    p.fx.n64Mix       = value (n64Mix) / 100.0f;
    p.fx.ps1Reverb    = value (ps1On) > 0.5f;
    p.fx.ps1Preset    = (int) value (ps1Preset);
    p.fx.ps1Depth     = value (ps1Depth) / 100.0f;
    p.fx.ledFilter    = value (ledOn) > 0.5f;
    synth.setParams (p);

    const bool stereo = buffer.getNumChannels() >= 2;
    if (! stereo && (int) scratchR.size() < numSamples)
        scratchR.resize ((size_t) numSamples);   // only if the host breaks its block-size promise

    float* left = buffer.getWritePointer (0);
    float* right = stereo ? buffer.getWritePointer (1) : scratchR.data();

    // Sample-accurate MIDI: render up to each event, then apply it.
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, numSamples, meta.samplePosition);
        if (at > pos)
        {
            synth.render (left + pos, right + pos, at - pos);
            pos = at;
        }
        synth.handleMidi (meta.data, meta.numBytes);
    }
    if (pos < numSamples)
        synth.render (left + pos, right + pos, numSamples - pos);

    songVoices.store (lotus::VoiceRegistry::get().publish (registrySlot, p.console, synth.getActiveVoiceCount()),
                      std::memory_order_relaxed);

    // Tuning-check audition, mixed on top.
    if (previewSynth.getSoundFont() != nullptr && (int) previewL.size() >= numSamples)
    {
        previewSynth.setParams (p);
        previewSynth.render (previewL.data(), previewR.data(), numSamples);
        for (int i = 0; i < numSamples; ++i) { left[i] += previewL[(size_t) i]; right[i] += previewR[(size_t) i]; }
    }

    if (! stereo)
    {
        buffer.applyGain (0, 0, numSamples, 0.5f);
        buffer.addFrom (0, 0, scratchR.data(), numSamples, 0.5f);
    }

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);
}

// Native sample format: encode in the background, swap in under the lock.

void LotusProcessor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    if (previewPlaying && now >= previewOffAtMs)
    {
        const juce::SpinLock::ScopedLockType lock (engineLock);
        previewSynth.allNotesOff();
        previewPlaying = false;
    }

    // Keep the progress view up for a moment even when analysis is instant.
    if (tuningDoneFlag.load() && (tuningPhase != TuningPhase::Analyzing || now - tuningStartedMs > 700.0))
    {
        tuningDoneFlag = false;
        if (tuningWorker.joinable()) tuningWorker.join();
        auto results = std::move (tuningResult);
        tuningResult.clear();

        if (results.empty())
            tuningPhase = TuningPhase::Idle;   // cancelled
        else if (tuningPhase == TuningPhase::Scanning)
            tuningPhase = lotus::looksMistuned (results) ? TuningPhase::Ask : TuningPhase::Idle;
        else if (tuningPhase == TuningPhase::Analyzing)
        {
            tuningRows.clear();
            lotus::TuningCorrections all;
            for (const auto& t : results)
                if (t.status == lotus::TuneStatus::Adjust)
                {
                    tuningRows.push_back ({ t, t.recommended });
                    all[{ t.bank, t.program }] = t.correctionCents;
                }
            for (const auto& t : results)
                if (t.status != lotus::TuneStatus::Adjust)
                    tuningRows.push_back ({ t, false });
            previewFixed = lotus::withCorrections (*tuningSource, all);
            tuningPhase = TuningPhase::Review;
        }
    }

    if (encodeDone.exchange (false))
    {
        if (encoder.joinable()) encoder.join();
        std::unique_ptr<std::vector<int16_t>> old;
        if (encodedResult != nullptr && soundFont != nullptr && encodedFrom == &soundFont->samples())
        {
            const juce::SpinLock::ScopedLockType lock (engineLock);
            old = std::move (installedPool);
            installedPool = std::move (encodedResult);
            installedCodec = encodingCodec;
            synth.setCodedPool (installedPool.get(), installedCodec);
        }
        encodedResult.reset();
        encoding = false;
    }

    const auto& profile = lotus::getConsoleProfile (getConsole());
    const bool wanted = value (ParamIDs::nativeFormat) > 0.5f && profile.codec != lotus::Codec::None && soundFont != nullptr;
    if (! wanted || encoding.load()) return;
    if (installedCodec == profile.codec && installedPool != nullptr) return;

    startEncoding (profile.codec);
}

void LotusProcessor::startEncoding (lotus::Codec codec)
{
    stopEncoding();
    encoding = true;
    cancelEncode = false;
    encodingCodec = codec;
    encodedFrom = &soundFont->samples();

    auto source = soundFont;   // keeps the samples alive while the thread runs
    encoder = std::thread ([this, source, codec]
    {
        auto result = std::make_unique<std::vector<int16_t>> (lotus::applyCodec (source->samples(), codec, &cancelEncode));
        if (! cancelEncode.load() && ! result->empty())
            encodedResult = std::move (result);
        encodeDone = true;
    });
}

void LotusProcessor::stopEncoding()
{
    cancelEncode = true;
    if (encoder.joinable()) encoder.join();
    encodeDone = false;
    encodedResult.reset();
    encoding = false;
}

juce::String LotusProcessor::loadSoundFont (const juce::File& file, bool userInitiated)
{
    std::string error;
    std::shared_ptr<const lotus::SoundFont> loaded = lotus::SoundFont::loadFromFile (file.getFullPathName().toStdString(), error);
    if (loaded == nullptr)
        return error.empty() ? juce::String ("Could not load SoundFont.") : juce::String (error);

    stopEncoding();
    stopTuningWorker();
    stopPreview();
    tuningPhase = TuningPhase::Idle;
    tuningRows.clear();

    std::shared_ptr<const lotus::SoundFont> old;
    std::unique_ptr<std::vector<int16_t>> oldPool;
    {
        const juce::SpinLock::ScopedLockType lock (engineLock);
        old = std::move (soundFont);
        soundFont = loaded;
        synth.setSoundFont (soundFont.get());
        synth.setCodedPool (nullptr, lotus::Codec::None);
        oldPool = std::move (installedPool);
        installedCodec = lotus::Codec::None;
        if (file != soundFontFile) clearColourPresets();   // slots point at presets of the old file
        soundFontFile = file;
    }
    loadedSoundFont = loaded;
    storedCorrections.clear();
    // Freed here, outside the lock and off the audio thread.
    old.reset();
    oldPool.reset();

    ++soundFontGeneration;
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
    notifyNamesChanged();

    if (userInitiated)
        startTuningCheck (true);
    return {};
}

void LotusProcessor::installSoundFont (std::shared_ptr<const lotus::SoundFont> active)
{
    std::shared_ptr<const lotus::SoundFont> old;
    {
        const juce::SpinLock::ScopedLockType lock (engineLock);
        const int preset = synth.getPresetIndex();
        old = std::move (soundFont);
        soundFont = std::move (active);
        synth.setSoundFont (soundFont.get());
        synth.setPresetIndex (preset);
        if (old == nullptr || old->pool != soundFont->pool)
        {
            synth.setCodedPool (nullptr, lotus::Codec::None);
            installedCodec = lotus::Codec::None;
        }
    }
    old.reset();
    ++soundFontGeneration;
}

void LotusProcessor::applyStoredCorrections()
{
    if (loadedSoundFont == nullptr) return;
    installSoundFont (storedCorrections.empty() ? loadedSoundFont : lotus::withCorrections (*loadedSoundFont, storedCorrections));
}

void LotusProcessor::clearStoredCorrections()
{
    storedCorrections.clear();
    applyStoredCorrections();
}

void LotusProcessor::startTuningWorker (bool thorough)
{
    stopTuningWorker();
    tuningCancel = false;
    tuningDoneFlag = false;
    tuningProgressDone = 0;
    tuningProgressTotal = soundFont != nullptr ? (int) soundFont->presets.size() : 0;
    tuningStartedMs = juce::Time::getMillisecondCounterHiRes();
    tuningSource = soundFont;

    auto source = tuningSource;
    tuningWorker = std::thread ([this, source, thorough]
    {
        lotus::TuningOptions options;
        options.thorough = thorough;
        auto result = lotus::analyzeTuning (*source, options, [this] (int done, int total)
        {
            tuningProgressDone = done;
            tuningProgressTotal = total;
            return ! tuningCancel.load();
        });
        tuningResult = std::move (result);
        tuningDoneFlag = true;
    });
}

void LotusProcessor::stopTuningWorker()
{
    tuningCancel = true;
    if (tuningWorker.joinable()) tuningWorker.join();
    tuningDoneFlag = false;
    tuningResult.clear();
}

void LotusProcessor::stopPreview()
{
    const juce::SpinLock::ScopedLockType lock (engineLock);
    previewSynth.setSoundFont (nullptr);
    previewPlaying = false;
}

void LotusProcessor::startTuningCheck (bool automatic)
{
    if (soundFont == nullptr) return;
    tuningOutcome = TuningOutcome::None;
    tuningDetail = {};
    tuningPhase = automatic ? TuningPhase::Scanning : TuningPhase::Analyzing;
    startTuningWorker (! automatic);
}

void LotusProcessor::answerTuningPrompt (bool fix)
{
    if (tuningPhase != TuningPhase::Ask) return;
    if (! fix) { tuningPhase = TuningPhase::Idle; return; }
    tuningPhase = TuningPhase::Analyzing;
    startTuningWorker (true);
}

void LotusProcessor::cancelTuningCheck()
{
    stopTuningWorker();
    stopPreview();
    previewFixed.reset();
    tuningRows.clear();
    tuningPhase = TuningPhase::Idle;
}

void LotusProcessor::setTuningRowApply (int row, bool apply)
{
    if (juce::isPositiveAndBelow (row, (int) tuningRows.size()) && tuningRows[(size_t) row].info.status == lotus::TuneStatus::Adjust)
        tuningRows[(size_t) row].apply = apply;
}

void LotusProcessor::previewTuningRow (int row, bool fixed)
{
    if (! juce::isPositiveAndBelow (row, (int) tuningRows.size()) || tuningSource == nullptr) return;
    const auto& info = tuningRows[(size_t) row].info;
    const auto* sf = fixed && previewFixed != nullptr ? previewFixed.get() : tuningSource.get();
    {
        const juce::SpinLock::ScopedLockType lock (engineLock);
        previewSynth.setSoundFont (sf);
        previewSynth.setPresetIndex (info.presetIndex);
        previewSynth.noteOn (info.playKey, 100);
    }
    previewPlaying = true;
    previewOffAtMs = juce::Time::getMillisecondCounterHiRes() + 1400.0;
}

void LotusProcessor::finishTuningReview()
{
    stopPreview();
    const bool any = std::any_of (tuningRows.begin(), tuningRows.end(), [] (const TuningRow& r) { return r.apply; });
    if (any) { tuningPhase = TuningPhase::AskSave; tuningOutcome = TuningOutcome::None; return; }
    tuningOutcome = TuningOutcome::NothingChanged;
    tuningPhase = TuningPhase::Done;
}

juce::File LotusProcessor::getFixedSaveTarget() const
{
    auto f = soundFontFile.getSiblingFile (soundFontFile.getFileNameWithoutExtension() + "_FIXED.sf2");
    return f.exists() ? f.getNonexistentSibling (true) : f;
}

void LotusProcessor::saveTuningFixes (SaveChoice choice, const juce::File& saveAsFile)
{
    if (choice == SaveChoice::Cancel) { cancelTuningCheck(); return; }

    // New fixes were measured on top of any already kept in the project, so they add.
    auto combined = storedCorrections;
    for (const auto& r : tuningRows)
        if (r.apply) combined[{ r.info.bank, r.info.program }] += r.info.correctionCents;
    for (auto it = combined.begin(); it != combined.end();)
        it = it->second == 0 ? combined.erase (it) : std::next (it);

    stopPreview();

    if (choice == SaveChoice::KeepInProject)
    {
        storedCorrections = combined;
        applyStoredCorrections();
        previewFixed.reset();
        tuningOutcome = TuningOutcome::Kept;
        tuningPhase = TuningPhase::Done;
        return;
    }

    const auto target = choice == SaveChoice::Save ? getFixedSaveTarget() : saveAsFile;
    if (target == juce::File()) return;

    std::string error;
    if (! lotus::writeCorrectedSoundFont (soundFontFile.getFullPathName().toStdString(), target.getFullPathName().toStdString(), combined, error))
    {
        tuningOutcome = TuningOutcome::SaveFailed;
        tuningDetail = error;
        tuningPhase = TuningPhase::AskSave;
        return;
    }

    const int preset = synth.getPresetIndex();
    const auto err = loadSoundFont (target, false);
    if (err.isNotEmpty())
    {
        tuningOutcome = TuningOutcome::SaveFailed;
        tuningDetail = err;
        tuningPhase = TuningPhase::AskSave;
        return;
    }
    selectPreset (preset);
    previewFixed.reset();
    tuningOutcome = TuningOutcome::Saved;
    tuningDetail = target.getFileName();
    tuningPhase = TuningPhase::Done;
}

void LotusProcessor::dismissTuningMessage()
{
    if (tuningPhase == TuningPhase::Done) tuningPhase = TuningPhase::Idle;
}

void LotusProcessor::selectPreset (int index)
{
    synth.setPresetIndex (index);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
    notifyNamesChanged();
}

void LotusProcessor::notifyNamesChanged()
{
    if (onNamesChanged == nullptr) return;
    if (juce::MessageManager::existsAndIsCurrentThread()) onNamesChanged();
    else juce::MessageManager::callAsync ([safe = juce::WeakReference<LotusProcessor> (this)]
                                          { if (safe != nullptr && safe->onNamesChanged) safe->onNamesChanged(); });
}

void LotusProcessor::clearColourPresets()
{
    for (int c = 0; c < lotus::Synth::numColours; ++c) synth.setColourPreset (c, -1);
}

void LotusProcessor::setColourPreset (int colour, int presetIndex)
{
    synth.setColourPreset (colour, presetIndex);
    notifyNamesChanged();
}

bool LotusProcessor::hasColourPresets() const
{
    for (int c = 0; c < lotus::Synth::numColours; ++c)
        if (synth.getColourPreset (c) >= 0) return true;
    return false;
}

juce::String LotusProcessor::getColourPresetName (int colour) const
{
    const juce::SpinLock::ScopedTryLockType lock (engineLock);
    if (! lock.isLocked() || soundFont == nullptr || soundFont->presets.empty()) return {};
    const int idx = juce::jlimit (0, (int) soundFont->presets.size() - 1, synth.presetForColour (colour));
    return juce::String::fromUTF8 (soundFont->presets[(size_t) idx].name.c_str());
}

juce::String LotusProcessor::getKeyName (int key, int colour) const
{
    const juce::SpinLock::ScopedTryLockType lock (engineLock);
    if (! lock.isLocked() || soundFont == nullptr || soundFont->presets.empty()) return {};
    const int idx = juce::jlimit (0, (int) soundFont->presets.size() - 1, synth.presetForColour (colour));
    const auto& preset = soundFont->presets[(size_t) idx];
    if (! lotus::isDrumKit (preset)) return {};
    for (const auto& r : preset.regions)
        if (key >= r.loKey && key <= r.hiKey && 100 >= r.loVel && 100 <= r.hiVel)
            return juce::String::fromUTF8 (r.sampleName.c_str());
    return {};
}

uint32_t LotusProcessor::hostNoteOn (int key, int velocity, int colour, float pitchCents, float gain, float pan)
{
    const juce::SpinLock::ScopedLockType lock (engineLock);
    return synth.hostNoteOn (key, velocity, colour, pitchCents, gain, pan);
}

void LotusProcessor::hostNoteLevels (uint32_t id, float pitchCents, float gain, float pan)
{
    const juce::SpinLock::ScopedLockType lock (engineLock);
    synth.hostNoteLevels (id, pitchCents, gain, pan);
}

void LotusProcessor::hostNoteOff (uint32_t id)
{
    const juce::SpinLock::ScopedLockType lock (engineLock);
    synth.hostNoteOff (id);
}

void LotusProcessor::hostNoteKill (uint32_t id)
{
    const juce::SpinLock::ScopedLockType lock (engineLock);
    synth.hostNoteKill (id);
}

bool LotusProcessor::isNoteSounding (uint32_t id) const
{
    const juce::SpinLock::ScopedLockType lock (engineLock);
    return synth.isNoteSounding (id);
}

int LotusProcessor::findPresetForProgram (int program) const
{
    if (soundFont == nullptr) return -1;
    const int cur = synth.getPresetIndex();
    const int bank = juce::isPositiveAndBelow (cur, (int) soundFont->presets.size()) ? soundFont->presets[(size_t) cur].bank : 0;
    int idx = soundFont->findPreset (bank, program);
    if (idx < 0) idx = soundFont->findPreset (0, program);
    if (idx < 0)
        for (size_t i = 0; i < soundFont->presets.size() && idx < 0; ++i)
            if (soundFont->presets[i].program == program) idx = (int) i;
    return idx;
}

int LotusProcessor::getCurrentProgram()
{
    const juce::SpinLock::ScopedTryLockType lock (engineLock);
    if (! lock.isLocked() || soundFont == nullptr) return 0;
    const int cur = synth.getPresetIndex();
    return juce::isPositiveAndBelow (cur, (int) soundFont->presets.size())
               ? juce::jlimit (0, 127, soundFont->presets[(size_t) cur].program) : 0;
}

void LotusProcessor::setCurrentProgram (int index)
{
    // Can arrive on the audio thread (VST3 program parameter), so never block.
    const juce::SpinLock::ScopedTryLockType lock (engineLock);
    if (! lock.isLocked()) return;
    const int idx = findPresetForProgram (index);
    if (idx >= 0 && idx != synth.getPresetIndex())
    {
        synth.setPresetIndex (idx);
        synth.presetChangedByMidi = true;
    }
}

const juce::String LotusProcessor::getProgramName (int index)
{
    const juce::SpinLock::ScopedTryLockType lock (engineLock);
    if (lock.isLocked())
    {
        const int idx = findPresetForProgram (index);
        if (idx >= 0) return juce::String (soundFont->presets[(size_t) idx].name);
    }
    return "Program " + juce::String (index + 1);
}

void LotusProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = params.copyState();
    state.setProperty ("sf2Path", soundFontFile.getFullPathName(), nullptr);
    state.setProperty ("presetIndex", synth.getPresetIndex(), nullptr);
    if (soundFont && juce::isPositiveAndBelow (synth.getPresetIndex(), (int) soundFont->presets.size()))
    {
        const auto& pr = soundFont->presets[(size_t) synth.getPresetIndex()];
        state.setProperty ("presetBank", pr.bank, nullptr);
        state.setProperty ("presetProgram", pr.program, nullptr);
    }

    // Colour slots by bank:program, so they survive presets being reordered.
    juce::StringArray colours;
    for (int c = 0; c < lotus::Synth::numColours; ++c)
    {
        const int idx = synth.getColourPreset (c);
        if (soundFont && juce::isPositiveAndBelow (idx, (int) soundFont->presets.size()))
            colours.add (juce::String (soundFont->presets[(size_t) idx].bank) + ":" + juce::String (soundFont->presets[(size_t) idx].program));
        else
            colours.add ("-");
    }
    state.setProperty ("colourPresets", colours.joinIntoString (","), nullptr);

    auto fixes = juce::ValueTree ("TuningFixes");
    for (const auto& [key, cents] : storedCorrections)
    {
        juce::ValueTree fix ("Fix");
        fix.setProperty ("bank", key.first, nullptr);
        fix.setProperty ("program", key.second, nullptr);
        fix.setProperty ("cents", cents, nullptr);
        fixes.appendChild (fix, nullptr);
    }
    state.removeChild (state.getChildWithName ("TuningFixes"), nullptr);
    state.appendChild (fixes, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void LotusProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (params.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    params.replaceState (state);

    const juce::String path = state.getProperty ("sf2Path", {}).toString();
    if (path.isEmpty())
        return;

    const juce::File file (path);
    if (file != soundFontFile || soundFont == nullptr)
    {
        if (file.existsAsFile())
            loadSoundFont (file);
        else
            soundFontFile = file;   // so the UI can say what's missing
    }

    if (soundFont)
    {
        lotus::TuningCorrections fixes;
        for (const auto& fix : state.getChildWithName ("TuningFixes"))
            fixes[{ (int) fix.getProperty ("bank"), (int) fix.getProperty ("program") }] = (int) fix.getProperty ("cents");
        if (fixes != storedCorrections)
        {
            storedCorrections = fixes;
            applyStoredCorrections();
        }

        // Bank/program first, in case the file's presets were reordered since saving.
        int index = soundFont->findPreset ((int) state.getProperty ("presetBank", -1), (int) state.getProperty ("presetProgram", -1));
        if (index < 0) index = (int) state.getProperty ("presetIndex", 0);
        synth.setPresetIndex (index);

        const auto colours = juce::StringArray::fromTokens (state.getProperty ("colourPresets", {}).toString(), ",", {});
        for (int c = 0; c < lotus::Synth::numColours; ++c)
        {
            const juce::String slot = c < colours.size() ? colours[c] : juce::String ("-");
            synth.setColourPreset (c, slot.contains (":") ? soundFont->findPreset (slot.upToFirstOccurrenceOf (":", false, false).getIntValue(),
                                                                                    slot.fromFirstOccurrenceOf (":", false, false).getIntValue())
                                                          : -1);
        }
    }
    ++soundFontGeneration;
    notifyNamesChanged();
}

juce::AudioProcessorEditor* LotusProcessor::createEditor()
{
    return new LotusEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LotusProcessor();
}
