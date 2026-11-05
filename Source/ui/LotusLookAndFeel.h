#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace lotus::ui
{

namespace Palette
{
    inline const juce::Colour background  { 0xff141118 };
    inline const juce::Colour panel       { 0xff1d1924 };
    inline const juce::Colour panelRaised { 0xff262130 };
    inline const juce::Colour outline     { 0xff2f2939 };
    inline const juce::Colour text        { 0xffefe8f4 };
    inline const juce::Colour textDim     { 0xff8f869c };
    inline const juce::Colour textFaint   { 0xff5d5568 };
    inline const juce::Colour accent      { 0xfff59ac0 };   // lotus pink
    inline const juce::Colour accentDeep  { 0xffd76d9c };
    inline const juce::Colour leaf        { 0xff86d1ae };   // lotus-leaf green
}

juce::Font font (float height, bool bold = false);

class LotusLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LotusLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parentArea) override;
};

} // namespace lotus::ui
