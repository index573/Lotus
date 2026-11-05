#include "LotusLookAndFeel.h"

namespace lotus::ui
{

juce::Font font (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

LotusLookAndFeel::LotusLookAndFeel()
{
    using namespace juce;

    setColour (ResizableWindow::backgroundColourId, Palette::background);

    setColour (Slider::textBoxTextColourId, Palette::text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Slider::textBoxHighlightColourId, Palette::accent.withAlpha (0.35f));

    setColour (Label::textColourId, Palette::text);
    setColour (Label::textWhenEditingColourId, Palette::text);
    setColour (Label::backgroundWhenEditingColourId, Palette::panelRaised);
    setColour (Label::outlineWhenEditingColourId, Palette::accent.withAlpha (0.6f));
    setColour (TextEditor::highlightColourId, Palette::accent.withAlpha (0.35f));
    setColour (TextEditor::highlightedTextColourId, Palette::text);
    setColour (CaretComponent::caretColourId, Palette::accent);

    setColour (ComboBox::backgroundColourId, Palette::panelRaised);
    setColour (ComboBox::outlineColourId, Palette::outline);
    setColour (ComboBox::textColourId, Palette::text);
    setColour (ComboBox::arrowColourId, Palette::textDim);

    setColour (PopupMenu::backgroundColourId, Palette::panelRaised);
    setColour (PopupMenu::textColourId, Palette::text);
    setColour (PopupMenu::highlightedBackgroundColourId, Palette::accent.withAlpha (0.18f));
    setColour (PopupMenu::highlightedTextColourId, Palette::accent);
    setColour (PopupMenu::headerTextColourId, Palette::textDim);

    setColour (TextButton::buttonColourId, Palette::panelRaised);
    setColour (TextButton::buttonOnColourId, Palette::accent);
    setColour (TextButton::textColourOffId, Palette::text);
    setColour (TextButton::textColourOnId, Palette::background);

    setColour (ScrollBar::thumbColourId, Palette::textFaint);
    setColour (TooltipWindow::textColourId, Palette::text);
}

void LotusLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, juce::Slider& s)
{
    using namespace juce;

    const auto bounds = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float track = jmax (3.0f, radius * 0.13f);
    const float arcRadius = radius - track * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool enabled = s.isEnabled();
    const auto accent = enabled ? Palette::accent : Palette::textFaint;

    // Track
    Path bg;
    bg.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (Palette::outline);
    g.strokePath (bg, PathStrokeType (track, PathStrokeType::curved, PathStrokeType::rounded));

    // Value
    if (pos > 0.001f)
    {
        Path arc;
        arc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
        g.setGradientFill (ColourGradient (accent.withAlpha (0.65f), centre.x - radius, centre.y + radius,
                                           accent, centre.x + radius, centre.y - radius, false));
        g.strokePath (arc, PathStrokeType (track, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // Cap
    const float capR = radius - track * 2.2f;
    g.setColour (s.isMouseOverOrDragging() && enabled ? Palette::panelRaised.brighter (0.08f) : Palette::panelRaised);
    g.fillEllipse (Rectangle<float> (capR * 2, capR * 2).withCentre (centre));
    g.setColour (Palette::outline.brighter (0.15f));
    g.drawEllipse (Rectangle<float> (capR * 2, capR * 2).withCentre (centre), 1.0f);

    // Pointer dot
    const auto dot = centre.getPointOnCircumference (capR * 0.62f, angle);
    g.setColour (enabled ? Palette::text : Palette::textFaint);
    g.fillEllipse (Rectangle<float> (track, track).withCentre (dot));
}

juce::Label* LotusLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (font (12.5f));
    l->setJustificationType (juce::Justification::centred);
    l->setColour (juce::Label::textColourId, Palette::text);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return l;
}

void LotusLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    using namespace juce;
    const auto r = Rectangle<float> ((float) w, (float) h).reduced (0.5f);
    const bool hot = box.isMouseOver (true) && box.isEnabled();

    g.setColour (hot ? Palette::panelRaised.brighter (0.06f) : Palette::panelRaised);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (box.hasKeyboardFocus (true) ? Palette::accent.withAlpha (0.6f) : Palette::outline);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);

    // Chevron
    const float cx = (float) w - 16.0f, cy = (float) h * 0.5f;
    Path chev;
    chev.startNewSubPath (cx - 4.0f, cy - 2.0f);
    chev.lineTo (cx, cy + 2.5f);
    chev.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (hot ? Palette::accent : Palette::textDim);
    g.strokePath (chev, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

juce::Font LotusLookAndFeel::getComboBoxFont (juce::ComboBox&) { return font (14.0f); }

void LotusLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 34, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Font LotusLookAndFeel::getPopupMenuFont() { return font (14.0f); }

void LotusLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (Palette::panelRaised);
    g.setColour (Palette::outline);
    g.drawRect (0, 0, w, h, 1);
}

void LotusLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = Palette::panelRaised;
    if (over) fill = fill.brighter (0.07f);
    if (down) fill = fill.brighter (0.12f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (over ? Palette::accent.withAlpha (0.55f) : Palette::outline);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
}

juce::Font LotusLookAndFeel::getTextButtonFont (juce::TextButton&, int) { return font (13.5f, true); }

void LotusLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    const auto r = juce::Rectangle<float> ((float) w, (float) h);
    g.setColour (Palette::panelRaised);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Palette::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (Palette::text);
    g.setFont (font (13.0f));
    g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (10, 6), juce::Justification::centredLeft, 4);
}

juce::Rectangle<int> LotusLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    juce::GlyphArrangement ga;
    ga.addFittedText (font (13.0f), text, 0, 0, 260.0f, 200.0f, juce::Justification::topLeft, 4);
    const auto bb = ga.getBoundingBox (0, -1, true);
    const int w = (int) std::ceil (bb.getWidth()) + 22, h = (int) std::ceil (bb.getHeight()) + 14;
    return juce::Rectangle<int> (pos.x + 12, pos.y + 18, w, h).constrainedWithin (parent);
}

} // namespace lotus::ui
