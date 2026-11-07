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
    explicit Segmented (juce::StringArray labels);   // unbound (e.g. page tabs)
    void setSelected (int index);
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

/** On/off pill switch bound to a bool parameter. */
class Switch final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Switch (juce::AudioProcessorValueTreeState&, const juce::String& paramId);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool isOn() const { return on; }
    std::function<void (bool)> onChange;

private:
    bool on = false;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

/** Small rounded toggle with changeable text, bound to a bool parameter. */
class Chip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Chip (juce::AudioProcessorValueTreeState&, const juce::String& paramId);
    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void enablementChanged() override { repaint(); }
    bool isOn() const { return on; }

private:
    juce::String text;
    bool on = false;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

/** One row in the effects list: name, a status dot, and selection highlight. */
class EffectRow final : public juce::Component
{
public:
    EffectRow (juce::String name, juce::String console);
    void setSelected (bool s) { if (s != selected) { selected = s; repaint(); } }
    void setActive (bool a) { if (a != active) { active = a; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    std::function<void()> onClick;

private:
    juce::String name, console;
    bool selected = false, active = false;
};

/** Song-wide voice light: green, amber at the limit, red over it (held for a moment). */
class SongMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    SongMeter() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void setState (int voices, int limit, bool overHeld, bool mismatch);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    std::function<void()> onClick;

private:
    int voices = 0, limit = 0;
    bool overHeld = false, mismatch = false;
};

/** "This song takes N consoles to run" card, drawn over the whole window. */
class VerdictPopup final : public juce::Component, private juce::Timer
{
public:
    VerdictPopup();
    void show (int consoles, const juce::String& title, const juce::String& detail);
    void showConfused (const juce::String& title, const juce::String& detail);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void()> onReset;

private:
    void timerCallback() override { phase += 0.12f; repaint (panelBounds()); }
    juce::Rectangle<int> panelBounds() const;
    static void drawConsole (juce::Graphics&, juce::Rectangle<float>, int mood);

    int count = 1;
    bool confused = false;
    float phase = 0.0f;
    juce::String title, detail;
    juce::TextButton resetButton { "Reset peak" }, closeButton { "Nice!" };
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
