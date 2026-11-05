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
    return v < 20.0f ? v : v / 1000.0f;   // bare numbers: "0.5" = seconds, "250" = ms
}

std::unique_ptr<juce::AudioParameterFloat> timeParam (const char* id, const char* name, float def)
{
    juce::NormalisableRange<float> range (0.0f, 10.0f);
    range.setSkewForCentre (0.5f);
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, range, def,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return formatTime (v); })
            .withValueFromStringFunction ([] (const juce::String& s) { return parseTime (s); }));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout LotusProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (timeParam (ParamIDs::attack,  "Attack",  0.0f));
    layout.add (timeParam (ParamIDs::decay,   "Decay",   0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::sustain, 1 }, "Sustain", juce::NormalisableRange<float> (0.0f, 100.0f), 100.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; })
            .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); })));

    layout.add (timeParam (ParamIDs::release, "Release", 0.05f));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIDs::noteOff, 1 }, "Note Off", juce::StringArray { "Release", "Cut" }, 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIDs::envSource, 1 }, "Envelope", juce::StringArray { "Lotus", "SoundFont" }, 0));

    juce::StringArray consoles;
    for (int i = 0; i < lotus::numConsoles; ++i)
        consoles.add (lotus::getConsoleProfile ((lotus::ConsoleId) i).name);
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIDs::console, 1 }, "Console", consoles, 0));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::volume, 1 }, "Volume", juce::NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int)
            {
                return v <= -60.0f ? juce::String ("-inf dB") : juce::String (v, 1) + " dB";
            })
            .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); })));

    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParamIDs::bendRange, 1 }, "Bend Range", 0, 24, 2,
        juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return juce::String (v) + " st"; })));

    return layout;
}

LotusProcessor::LotusProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "Lotus", createLayout())
{
    pAttack    = params.getRawParameterValue (ParamIDs::attack);
    pDecay     = params.getRawParameterValue (ParamIDs::decay);
    pSustain   = params.getRawParameterValue (ParamIDs::sustain);
    pRelease   = params.getRawParameterValue (ParamIDs::release);
    pNoteOff   = params.getRawParameterValue (ParamIDs::noteOff);
    pEnvSource = params.getRawParameterValue (ParamIDs::envSource);
    pConsole   = params.getRawParameterValue (ParamIDs::console);
    pVolume    = params.getRawParameterValue (ParamIDs::volume);
    pBendRange = params.getRawParameterValue (ParamIDs::bendRange);
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
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    if (buffer.getNumChannels() == 0 || numSamples == 0)
        return;

    const juce::SpinLock::ScopedTryLockType lock (engineLock);
    if (! lock.isLocked())
        return;   // a SoundFont is being swapped in right now; output silence for one block

    lotus::EngineParams p;
    p.attack = pAttack->load();
    p.decay = pDecay->load();
    p.sustain = pSustain->load() / 100.0f;
    p.release = pRelease->load();
    p.cutOnNoteOff = pNoteOff->load() > 0.5f;
    p.useSf2Envelope = pEnvSource->load() > 0.5f;
    p.console = (lotus::ConsoleId) juce::jlimit (0, lotus::numConsoles - 1, (int) pConsole->load());
    const float db = pVolume->load();
    p.masterGain = db <= -60.0f ? 0.0f : juce::Decibels::decibelsToGain (db);
    p.bendRange = (int) pBendRange->load();
    synth.setParams (p);

    // Stereo: render straight into the host buffer. Mono: render the right side into
    // scratch and fold down.
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

    if (! stereo)
    {
        buffer.applyGain (0, 0, numSamples, 0.5f);
        buffer.addFrom (0, 0, scratchR.data(), numSamples, 0.5f);
    }

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);
}

// ---------------------------------------------------------------------------------

juce::String LotusProcessor::loadSoundFont (const juce::File& file)
{
    std::string error;
    auto loaded = lotus::SoundFont::loadFromFile (file.getFullPathName().toStdString(), error);
    if (loaded == nullptr)
        return error.empty() ? juce::String ("Could not load SoundFont.") : juce::String (error);

    std::unique_ptr<lotus::SoundFont> old;
    {
        const juce::SpinLock::ScopedLockType lock (engineLock);
        old = std::move (soundFont);
        soundFont = std::move (loaded);
        synth.setSoundFont (soundFont.get());
        soundFontFile = file;
    }
    old.reset();   // freed here, outside the lock and off the audio thread

    ++soundFontGeneration;
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
    return {};
}

void LotusProcessor::selectPreset (int index)
{
    synth.setPresetIndex (index);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

// Host programs are a fixed list of 128 MIDI program numbers (VST3 requires the
// count to be fixed when the plugin is created). Program n selects the preset with
// program number n, preferring the bank that's currently selected.
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

int LotusProcessor::getNumPrograms()
{
    return 128;
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
    // May be called on the audio thread (VST3 program parameter), so never block.
    const juce::SpinLock::ScopedTryLockType lock (engineLock);
    if (! lock.isLocked()) return;
    const int idx = findPresetForProgram (index);
    if (idx >= 0 && idx != synth.getPresetIndex())
    {
        synth.setPresetIndex (idx);
        synth.presetChangedByMidi = true;   // lets the editor follow along
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
        // Bank/program too, so the right preset is found even if the file was re-ordered.
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
            soundFontFile = file;   // remember it so the UI can say what's missing
    }

    if (soundFont)
    {
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
