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
                            volume, bendRange, glide, snesOn, snesDelay, snesFeedback, snesVolume, snesFilter,
                            n64On, n64Preset, n64Mix, ps1On, ps1Preset, ps1Depth, ledOn })
    {
        raw[id] = params.getRawParameterValue (id);
        jassert (raw[id] != nullptr);
    }

    registrySlot = lotus::VoiceRegistry::get().join();

    startTimerHz (5);
}

LotusProcessor::~LotusProcessor()
{
    stopTimer();
    stopEncoding();
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
    scratchR.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
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
    if (encodeDone.exchange (false))
    {
        if (encoder.joinable()) encoder.join();
        std::unique_ptr<std::vector<int16_t>> old;
        if (encodedResult != nullptr && encodedFrom == soundFont.get())
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
    encodedFrom = soundFont.get();

    auto source = soundFont;   // keeps the samples alive while the thread runs
    encoder = std::thread ([this, source, codec]
    {
        auto result = std::make_unique<std::vector<int16_t>> (lotus::applyCodec (source->samples, codec, &cancelEncode));
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

juce::String LotusProcessor::loadSoundFont (const juce::File& file)
{
    std::string error;
    std::shared_ptr<const lotus::SoundFont> loaded = lotus::SoundFont::loadFromFile (file.getFullPathName().toStdString(), error);
    if (loaded == nullptr)
        return error.empty() ? juce::String ("Could not load SoundFont.") : juce::String (error);

    stopEncoding();

    std::shared_ptr<const lotus::SoundFont> old;
    std::unique_ptr<std::vector<int16_t>> oldPool;
    {
        const juce::SpinLock::ScopedLockType lock (engineLock);
        old = std::move (soundFont);
        soundFont = std::move (loaded);
        synth.setSoundFont (soundFont.get());
        synth.setCodedPool (nullptr, lotus::Codec::None);
        oldPool = std::move (installedPool);
        installedCodec = lotus::Codec::None;
        soundFontFile = file;
    }
    // Freed here, outside the lock and off the audio thread.
    old.reset();
    oldPool.reset();

    ++soundFontGeneration;
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
    return {};
}

void LotusProcessor::selectPreset (int index)
{
    synth.setPresetIndex (index);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
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
        // Bank/program first, in case the file's presets were reordered since saving.
        int index = soundFont->findPreset ((int) state.getProperty ("presetBank", -1), (int) state.getProperty ("presetProgram", -1));
        if (index < 0) index = (int) state.getProperty ("presetIndex", 0);
        synth.setPresetIndex (index);
    }
    ++soundFontGeneration;
}

juce::AudioProcessorEditor* LotusProcessor::createEditor()
{
    return new LotusEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LotusProcessor();
}
