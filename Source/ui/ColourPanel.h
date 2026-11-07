#pragma once

#include "../PluginProcessor.h"

namespace lotus::ui
{

/** Overlay for giving each note colour (MIDI channel) its own preset. */
class ColourPanel final : public juce::Component
{
public:
    explicit ColourPanel (LotusProcessor&);

    void show();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    std::function<void()> onChange;

private:
    juce::Rectangle<int> panelBounds() const;
    juce::Rectangle<int> tileBounds (int index) const;
    int tileAt (juce::Point<int>) const;
    void showPresetMenu (int colour);

    LotusProcessor& processor;
    int hover = -1;
    juce::TextButton resetButton, doneButton;
};

} // namespace lotus::ui
