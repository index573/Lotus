#pragma once

#include "PluginProcessor.h"
#include "ui/LotusLookAndFeel.h"
#include "ui/TuningDialog.h"
#include "ui/Widgets.h"

class LotusEditor final : public juce::AudioProcessorEditor,
                          public juce::FileDragAndDropTarget,
                          private juce::Timer
{
public:
    explicit LotusEditor (LotusProcessor&);
    ~LotusEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

    static constexpr int baseWidth = 760;
    static constexpr int baseHeight = 470;

private:
    // Laid out at a fixed logical size, then scaled to the window.
    class Content final : public juce::Component
    {
    public:
        explicit Content (LotusEditor& e) : editor (e) {}
        void paint (juce::Graphics&) override;
        void resized() override;
        LotusEditor& editor;
    };

    enum Page { soundPage = 0, pitchPage, effectsPage, numPages };
    enum Effect { snes = 0, n64, ps1, led, numEffects };

    void timerCallback() override;
    void applyLanguage();
    void refreshPresetList();
    void refreshInfo();
    void refreshConsole();
    void refreshTuneText();
    void refreshCorrectionsStatus();
    void updateEnablement();
    void updateSongMeter();
    void showVerdict();
    void showPage (int page);
    void showEffect (int effect);
    void showLanguageMenu();
    void openFileChooser();
    void saveAsChooser();
    void loadFile (const juce::File&);
    void stepPreset (int delta);
    float param (const char* id) const;
    bool tuneInHz() const;

    LotusProcessor& processor;
    lotus::ui::LotusLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    Content content { *this };

    lotus::ui::LotusLogo logo;
    lotus::ui::IconButton languageButton { lotus::ui::IconButton::Icon::Globe };
    lotus::ui::Segmented pageTabs { juce::StringArray { "Sound", "Pitch", "Effects" } };
    juce::TextButton loadButton;

    lotus::ui::Card presetCard;
    lotus::ui::IconButton prevButton { lotus::ui::IconButton::Icon::Left };
    lotus::ui::IconButton nextButton { lotus::ui::IconButton::Icon::Right };
    juce::ComboBox presetBox;
    juce::Label infoLabel;

    // Sound page
    lotus::ui::Card envCard { "Envelope" };
    lotus::ui::EnvelopeView envelopeView;
    lotus::ui::Knob attackKnob, decayKnob, sustainKnob, releaseKnob;

    lotus::ui::Card consoleCard { "Console" };
    juce::ComboBox consoleBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> consoleAttachment;
    lotus::ui::Chip formatChip, voicesChip;
    lotus::ui::SongMeter songMeter;
    double overUntilMs = 0.0;

    lotus::ui::Card playCard { "Playback" };
    juce::Label noteOffLabel, envSourceLabel;
    lotus::ui::Segmented noteOffSwitch, envSourceSwitch;

    lotus::ui::Card outputCard;
    lotus::ui::Knob volumeKnob, bendKnob, glideKnob;

    // Pitch page
    lotus::ui::Card vibCard { "Vibrato" };
    lotus::ui::VibratoView vibratoView;
    lotus::ui::Segmented vibShapeSwitch;
    lotus::ui::Knob vibAmountKnob, vibSpeedKnob, vibDelayKnob, vibAttackKnob;

    lotus::ui::Card tuneCard { "Tuning" };
    lotus::ui::Knob tuneKnob;
    lotus::ui::Segmented tuneUnitSwitch { juce::StringArray { "Cents", "Hz" } };
    juce::Label tuneHint;

    lotus::ui::Card sfTuneCard { "SoundFont tuning" };
    juce::Label sfTuneText, sfTuneStatus;
    juce::TextButton checkTuningButton, clearFixesButton;

    // Effects page
    lotus::ui::Card fxListCard { "Effects" };
    std::array<std::unique_ptr<lotus::ui::EffectRow>, numEffects> fxRows;
    lotus::ui::Card fxCard;
    std::array<std::unique_ptr<lotus::ui::Switch>, numEffects> fxSwitches;
    juce::Label fxDesc, fxControlLabel, fxHint;

    lotus::ui::Segmented snesFilter;
    lotus::ui::Knob snesDelayKnob, snesFeedbackKnob, snesVolumeKnob;
    juce::ComboBox n64PresetBox, ps1PresetBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> n64PresetAttachment, ps1PresetAttachment;
    lotus::ui::Knob n64MixKnob, ps1DepthKnob;

    // Overlays
    lotus::ui::VerdictPopup verdict;
    lotus::ui::TuningDialog tuningDialog;

    std::array<std::vector<juce::Component*>, numPages> pages;
    std::array<std::vector<juce::Component*>, numEffects> effectControls;

    std::unique_ptr<juce::FileChooser> chooser;
    int page = soundPage, selectedEffect = snes;
    int seenGeneration = -1, seenPreset = -1, seenVoices = -1, seenConsole = -1, seenLanguage = -1, seenFixes = -1;
    bool seenEncoding = false;
    bool dragHover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LotusEditor)
};
