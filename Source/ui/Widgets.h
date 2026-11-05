#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "LotusLookAndFeel.h"

namespace lotus::ui
{

/** The lotus mark, drawn as vector paths so it is crisp at any scale. */
class LotusLogo final : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
};

/** A rounded card with a small caps title. */
class Card final : public juce::Component
{
public:
    explicit Card (juce::String titleText = {}) : title (std::move (titleText)) { setInterceptsMouseClicks (false, true); }
    void paint (juce::Graphics&) override;
    juce::Rectangle<int> getContentBounds() const;
    void setTitle (juce::String t) { title = std::move (t); repaint(); }

private:
    juce::String title;
};

/** Rotary knob + caption + editable value, attached to a parameter. */
class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider& getSlider() { return slider; }

private:
    juce::Slider slider;
    juce::String caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** Pill-shaped segmented switch bound to a choice parameter. */
class Segmented final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Segmented (juce::AudioProcessorValueTreeState&, const juce::String& paramId, juce::StringArray labels);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    int getSelected() const { return selected; }

    std::function<void (int)> onChange;

private:
    int indexAt (juce::Point<int>) const;

    juce::StringArray labels;
    int selected = 0, hovered = -1;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

/** Live drawing of the envelope that will actually be applied. */
class EnvelopeView final : public juce::Component
{
public:
    struct State
    {
        float attack = 0, decay = 0, sustain = 1, release = 0;
        bool cut = false, sf2 = false;
        bool operator== (const State& o) const
        {
            return juce::exactlyEqual (attack, o.attack) && juce::exactlyEqual (decay, o.decay)
                && juce::exactlyEqual (sustain, o.sustain) && juce::exactlyEqual (release, o.release)
                && cut == o.cut && sf2 == o.sf2;
        }
        bool operator!= (const State& o) const { return ! (*this == o); }
    };

    void setState (const State& s) { if (s != state) { state = s; repaint(); } }
    void paint (juce::Graphics&) override;

private:
    State state;
};

/** Small round icon button (used for the preset arrows). */
class IconButton final : public juce::Button
{
public:
    enum class Icon { Left, Right };
    explicit IconButton (Icon i) : juce::Button ({}), icon (i) {}
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    Icon icon;
};

} // namespace lotus::ui
