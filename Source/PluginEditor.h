#pragma once

#include "PluginProcessor.h"
#include "ui/LotusLookAndFeel.h"
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
    /** Everything is laid out at a fixed logical size inside this, then scaled. */
    class Content final : public juce::Component
    {
    public:
        explicit Content (LotusEditor& e) : editor (e) {}
        void paint (juce::Graphics&) override;
        void resized() override;
        LotusEditor& editor;
    };

    void timerCallback() override;
    void refreshPresetList();
    void refreshInfo();
    void updateEnablement();
    void openFileChooser();
    void loadFile (const juce::File&);
    void stepPreset (int delta);

    LotusProcessor& processor;
    lotus::ui::LotusLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    Content content { *this };

    lotus::ui::LotusLogo logo;
    juce::TextButton loadButton { "Load SF2" };

    lotus::ui::Card presetCard;
    lotus::ui::IconButton prevButton { lotus::ui::IconButton::Icon::Left };
    lotus::ui::IconButton nextButton { lotus::ui::IconButton::Icon::Right };
    juce::ComboBox presetBox;
    juce::Label infoLabel;

    lotus::ui::Card envCard { "Envelope" };
    lotus::ui::EnvelopeView envelopeView;
    lotus::ui::Knob attackKnob, decayKnob, sustainKnob, releaseKnob;

    lotus::ui::Card consoleCard { "Console" };
    juce::ComboBox consoleBox;
    juce::Label consoleDesc;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> consoleAttachment;

    lotus::ui::Card noteOffCard { "Note off" };
    lotus::ui::Segmented noteOffSwitch;
    juce::Label noteOffDesc;

    lotus::ui::Card envSourceCard { "Envelope from" };
    lotus::ui::Segmented envSourceSwitch;
    juce::Label envSourceDesc;

    lotus::ui::Card outputCard;
    lotus::ui::Knob volumeKnob, bendKnob;

    std::unique_ptr<juce::FileChooser> chooser;
    int seenGeneration = -1;
    int seenPreset = -1;
    int seenVoices = -1;
    bool dragHover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LotusEditor)
};
