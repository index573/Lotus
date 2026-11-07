#pragma once

// The SoundFont tuning check, as an overlay: the "strange tuning" question, the progress
// bar, the review list (with before/after previews at C5), the save choices and the final
// message. It mirrors the phase held by the processor, so closing and reopening the
// plugin window doesn't lose the session.

#include "../PluginProcessor.h"
#include "LotusLookAndFeel.h"

namespace lotus::ui
{

class TuningDialog final : public juce::Component
{
public:
    explicit TuningDialog (LotusProcessor&);
    ~TuningDialog() override;

    /** Call regularly (the editor's timer): follows the processor's phase and progress. */
    void sync();
    void refreshTexts();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {}   // swallow clicks behind the panel

    std::function<void()> onSaveAs;

    /** Note name in FL Studio's numbering (middle C = C5). */
    static juce::String noteName (int key);

private:
    class Row;
    class PlayButton;

    juce::Rectangle<int> panelBounds() const;
    void rebuildRows();
    void layoutRows();
    void configureButtons();

    LotusProcessor& processor;
    LotusProcessor::TuningPhase shown = LotusProcessor::TuningPhase::Idle;
    LotusProcessor::TuningOutcome shownOutcome = LotusProcessor::TuningOutcome::None;
    int shownDone = -1;

    juce::TextButton button1, button2, button3, button4;
    juce::Viewport viewport;
    juce::Component list;
    std::vector<std::unique_ptr<juce::Component>> rows;
    int adjustedCount = 0, skippedCount = 0;
};

} // namespace lotus::ui
