#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Codecs.h"
#include "dsp/Synth.h"
#include "dsp/TuningAnalysis.h"
#include "dsp/VoiceRegistry.h"

#include <functional>
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
    inline constexpr const char* tune         = "tune";         // cents relative to A4 = 440 Hz
    inline constexpr const char* vibAmount    = "vibAmount";
    inline constexpr const char* vibSpeed     = "vibSpeed";
    inline constexpr const char* vibDelay     = "vibDelay";
    inline constexpr const char* vibAttack    = "vibAttack";
    inline constexpr const char* vibShape     = "vibShape";

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

/** Tuning as text: "+12.5 c" or "A4 = 442.0 Hz". Parsing accepts either form. */
juce::String formatTuning (float cents, bool asHz);
float parseTuning (const juce::String& text, bool bareNumbersAreHz);

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

    /** Loads an SF2 on the message thread. Returns an empty string or the error.
        A load the user started (drag and drop, Load SF2) also runs the tuning check. */
    juce::String loadSoundFont (const juce::File& file, bool userInitiated = false);

    // --- Tuning check ------------------------------------------------------------
    enum class TuningPhase { Idle, Scanning, Ask, Analyzing, Review, AskSave, Done };
    enum class TuningOutcome { None, Saved, Kept, NothingChanged, SaveFailed };
    enum class SaveChoice { Save, SaveAs, KeepInProject, Cancel };
    struct TuningRow { lotus::PresetTuning info; bool apply = false; };

    void startTuningCheck (bool automatic);   // automatic: quick scan, ask only if something looks wrong
    void answerTuningPrompt (bool fix);
    void cancelTuningCheck();
    void setTuningRowApply (int row, bool apply);
    void previewTuningRow (int row, bool fixed);
    void finishTuningReview();
    void saveTuningFixes (SaveChoice choice, const juce::File& saveAsFile = {});
    void dismissTuningMessage();

    TuningPhase getTuningPhase() const { return tuningPhase; }
    int getTuningDone() const { return tuningProgressDone.load(); }
    int getTuningTotal() const { return tuningProgressTotal.load(); }
    const std::vector<TuningRow>& getTuningRows() const { return tuningRows; }
    TuningOutcome getTuningOutcome() const { return tuningOutcome; }
    juce::String getTuningDetail() const { return tuningDetail; }
    juce::File getFixedSaveTarget() const;
    int getStoredCorrectionCount() const { return (int) storedCorrections.size(); }
    void clearStoredCorrections();

    const lotus::SoundFont* getSoundFont() const { return soundFont.get(); }
    juce::File getSoundFontFile() const { return soundFontFile; }

    int  getPresetIndex() const { return synth.getPresetIndex(); }
    void selectPreset (int index);

    // --- Note colours -------------------------------------------------------------
    // Each of FL Studio's 16 note colours (MIDI channels 1-16 in other hosts) can play its
    // own preset; -1 follows the main preset.
    void setColourPreset (int colour, int presetIndex);
    int  getColourPreset (int colour) const { return synth.getColourPreset (colour); }
    juce::String getColourPresetName (int colour) const;   // what notes of that colour play
    bool hasColourPresets() const;

    /** For drum kits: the sample on a key, so FL's piano roll can label it. Empty otherwise. */
    juce::String getKeyName (int key, int colour) const;

    /** Called on the message thread when preset or colour names change. */
    std::function<void()> onNamesChanged;

    // --- Host-driven notes (the FL Studio edition) ---------------------------------
    uint32_t hostNoteOn (int key, int velocity, int colour, float pitchCents, float gain, float pan);
    void hostNoteLevels (uint32_t id, float pitchCents, float gain, float pan);
    void hostNoteOff (uint32_t id);
    void hostNoteKill (uint32_t id);
    bool isNoteSounding (uint32_t id) const;

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
    void notifyNamesChanged();
    void clearColourPresets();
    float value (const char* id) const;

    void timerCallback() override;
    void startEncoding (lotus::Codec codec);
    void stopEncoding();

    void installSoundFont (std::shared_ptr<const lotus::SoundFont> active);
    void applyStoredCorrections();
    void startTuningWorker (bool thorough);
    void stopTuningWorker();
    void stopPreview();

    lotus::Synth synth;
    std::shared_ptr<const lotus::SoundFont> soundFont;         // what plays (with any fixes kept in the project)
    std::shared_ptr<const lotus::SoundFont> loadedSoundFont;   // as read from disk
    lotus::TuningCorrections storedCorrections;
    juce::File soundFontFile;
    mutable juce::SpinLock engineLock;   // guards SoundFont/pool swaps; the audio thread only try-locks

    std::vector<float> scratchR;
    int registrySlot = -1;
    std::atomic<int> songVoices { 0 };
    std::unordered_map<const char*, std::atomic<float>*> raw;   // keyed by the ParamIDs constants

    // Native-format encoding runs on its own thread.
    std::thread encoder;
    std::atomic<bool> cancelEncode { false }, encoding { false }, encodeDone { false };
    std::unique_ptr<std::vector<int16_t>> encodedResult, installedPool;
    lotus::Codec encodingCodec = lotus::Codec::None, installedCodec = lotus::Codec::None;
    const std::vector<int16_t>* encodedFrom = nullptr;   // the sample pool that was encoded

    // Tuning check
    std::thread tuningWorker;
    std::atomic<bool> tuningCancel { false }, tuningDoneFlag { false };
    std::atomic<int> tuningProgressDone { 0 }, tuningProgressTotal { 0 };
    std::vector<lotus::PresetTuning> tuningResult;   // handed over when tuningDoneFlag is set
    double tuningStartedMs = 0.0;
    TuningPhase tuningPhase = TuningPhase::Idle;
    TuningOutcome tuningOutcome = TuningOutcome::None;
    juce::String tuningDetail;
    std::vector<TuningRow> tuningRows;
    std::shared_ptr<const lotus::SoundFont> tuningSource, previewFixed;

    // Before/after audition
    lotus::Synth previewSynth;
    std::vector<float> previewL, previewR;
    double previewOffAtMs = 0.0;
    bool previewPlaying = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE (LotusProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LotusProcessor)
};
