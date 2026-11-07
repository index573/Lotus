#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Codecs.h"
#include "dsp/Synth.h"
#include "dsp/VoiceRegistry.h"

#include <thread>
#include <unordered_map>

namespace ParamIDs
{
    inline constexpr const char* attack       = "attack";
    inline constexpr const char* decay        = "decay";
    inline constexpr const char* sustain      = "sustain";
    inline constexpr const char* release      = "release";
    inline constexpr const char* noteOff      = "noteOff";      // 0 Release, 1 Cut
    inline constexpr const char* envSource    = "envSource";    // 0 Lotus, 1 SoundFont
    inline constexpr const char* console      = "console";
    inline constexpr const char* nativeFormat = "nativeFormat";
    inline constexpr const char* voiceLimit   = "voiceLimit";
    inline constexpr const char* volume       = "volume";
    inline constexpr const char* bendRange    = "bendRange";
    inline constexpr const char* glide        = "glide";

    inline constexpr const char* snesOn       = "snesEchoOn";
    inline constexpr const char* snesDelay    = "snesEchoDelay";
    inline constexpr const char* snesFeedback = "snesEchoFeedback";
    inline constexpr const char* snesVolume   = "snesEchoVolume";
    inline constexpr const char* snesFilter   = "snesEchoFilter";
    inline constexpr const char* n64On        = "n64ReverbOn";
    inline constexpr const char* n64Preset    = "n64ReverbType";
    inline constexpr const char* n64Mix       = "n64ReverbMix";
    inline constexpr const char* ps1On        = "ps1ReverbOn";
    inline constexpr const char* ps1Preset    = "ps1ReverbPreset";
    inline constexpr const char* ps1Depth     = "ps1ReverbDepth";
    inline constexpr const char* ledOn        = "amigaLedFilter";
}

class LotusProcessor final : public juce::AudioProcessor,
                             private juce::Timer
{
public:
    LotusProcessor();
    ~LotusProcessor() override;

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
    double getTailLengthSeconds() const override;

    // Host programs are MIDI program numbers 0-127, resolved against the SoundFont.
    int getNumPrograms() override { return 128; }
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Loads an SF2 on the message thread. Returns an empty string or the error. */
    juce::String loadSoundFont (const juce::File& file);

    const lotus::SoundFont* getSoundFont() const { return soundFont.get(); }
    juce::File getSoundFontFile() const { return soundFontFile; }

    int  getPresetIndex() const { return synth.getPresetIndex(); }
    void selectPreset (int index);

    int  getActiveVoiceCount() const { return synth.getActiveVoiceCount(); }
    bool consumePresetChangedByMidi() { return synth.presetChangedByMidi.exchange (false); }

    /** True while samples are being converted to the console's native format. */
    bool isEncoding() const { return encoding.load(); }

    lotus::ConsoleId getConsole() const;

    /** Voices playing right now across every Lotus in this project set to the same console. */
    int getSongVoices() const { return songVoices.load (std::memory_order_relaxed); }

    std::atomic<int> soundFontGeneration { 0 };
    juce::AudioProcessorValueTreeState params;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    int findPresetForProgram (int program) const;
    float value (const char* id) const;

    void timerCallback() override;
    void startEncoding (lotus::Codec codec);
    void stopEncoding();

    lotus::Synth synth;
    std::shared_ptr<const lotus::SoundFont> soundFont;
    juce::File soundFontFile;
    juce::SpinLock engineLock;   // guards SoundFont/pool swaps; the audio thread only try-locks

    std::vector<float> scratchR;
    int registrySlot = -1;
    std::atomic<int> songVoices { 0 };
    std::unordered_map<const char*, std::atomic<float>*> raw;   // keyed by the ParamIDs constants

    // Native-format encoding runs on its own thread.
    std::thread encoder;
    std::atomic<bool> cancelEncode { false }, encoding { false }, encodeDone { false };
    std::unique_ptr<std::vector<int16_t>> encodedResult, installedPool;
    lotus::Codec encodingCodec = lotus::Codec::None, installedCodec = lotus::Codec::None;
    const lotus::SoundFont* encodedFrom = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LotusProcessor)
};
