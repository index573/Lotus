#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Synth.h"

namespace ParamIDs
{
    inline constexpr const char* attack    = "attack";
    inline constexpr const char* decay     = "decay";
    inline constexpr const char* sustain   = "sustain";
    inline constexpr const char* release   = "release";
    inline constexpr const char* noteOff   = "noteOff";    // 0 = Release, 1 = Cut
    inline constexpr const char* envSource = "envSource";  // 0 = Lotus ADSR, 1 = SoundFont
    inline constexpr const char* console   = "console";
    inline constexpr const char* volume    = "volume";
    inline constexpr const char* bendRange = "bendRange";
}

class LotusProcessor final : public juce::AudioProcessor
{
public:
    LotusProcessor();
    ~LotusProcessor() override = default;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Lotus"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Host "programs" are MIDI program numbers 0-127, resolved against the SoundFont.
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Lotus
    /** Loads an SF2 (message thread). Returns an empty string on success, else the error. */
    juce::String loadSoundFont (const juce::File& file);

    const lotus::SoundFont* getSoundFont() const { return soundFont.get(); }
    juce::File getSoundFontFile() const { return soundFontFile; }

    int  getPresetIndex() const { return synth.getPresetIndex(); }
    void selectPreset (int index);

    int  getActiveVoiceCount() const { return synth.getActiveVoiceCount(); }
    bool consumePresetChangedByMidi() { return synth.presetChangedByMidi.exchange (false); }

    /** Incremented whenever the loaded SoundFont changes, so the UI knows to refresh. */
    std::atomic<int> soundFontGeneration { 0 };

    juce::AudioProcessorValueTreeState params;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    int findPresetForProgram (int program) const;

    lotus::Synth synth;
    std::unique_ptr<lotus::SoundFont> soundFont;
    juce::File soundFontFile;
    juce::SpinLock engineLock;   // guards synth <-> soundFont swaps; audio thread only try-locks

    std::vector<float> scratchR;   // mono hosts only

    std::atomic<float>* pAttack = nullptr;
    std::atomic<float>* pDecay = nullptr;
    std::atomic<float>* pSustain = nullptr;
    std::atomic<float>* pRelease = nullptr;
    std::atomic<float>* pNoteOff = nullptr;
    std::atomic<float>* pEnvSource = nullptr;
    std::atomic<float>* pConsole = nullptr;
    std::atomic<float>* pVolume = nullptr;
    std::atomic<float>* pBendRange = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LotusProcessor)
};
