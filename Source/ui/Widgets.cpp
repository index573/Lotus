#include "Widgets.h"

#include <cmath>

namespace lotus::ui
{

static juce::Path makePetal (float height, float width)
{
    juce::Path p;
    p.startNewSubPath (0.0f, 0.0f);
    p.cubicTo (-width, -height * 0.32f, -width * 0.62f, -height * 0.82f, 0.0f, -height);
    p.cubicTo (width * 0.62f, -height * 0.82f, width, -height * 0.32f, 0.0f, 0.0f);
    p.closeSubPath();
    return p;
}

void LotusLogo::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float s = juce::jmin (b.getWidth(), b.getHeight());
    const juce::Point<float> base (b.getCentreX(), b.getCentreY() + s * 0.30f);

    struct P { float angle, height, width; float shade; };
    const P petals[] = {
        { -1.05f, 0.50f, 0.20f, 0.55f }, { 1.05f, 0.50f, 0.20f, 0.55f },
        { -0.52f, 0.66f, 0.22f, 0.78f }, { 0.52f, 0.66f, 0.22f, 0.78f },
        {  0.00f, 0.78f, 0.24f, 1.00f },
    };

    for (const auto& p : petals)
    {
        auto path = makePetal (p.height * s, p.width * s);
        path.applyTransform (juce::AffineTransform::rotation (p.angle).translated (base));
        const auto top = base.getPointOnCircumference (p.height * s, p.angle);
        g.setGradientFill (juce::ColourGradient (Palette::accent.interpolatedWith (Palette::background, 1.0f - p.shade).brighter (0.15f), top,
                                                 Palette::accentDeep.interpolatedWith (Palette::background, (1.0f - p.shade) * 0.8f), base, false));
        g.fillPath (path);
        g.setColour (Palette::background.withAlpha (0.55f));
        g.strokePath (path, juce::PathStrokeType (s * 0.025f));
    }

    juce::Path water;
    water.startNewSubPath (base.x - s * 0.36f, base.y + s * 0.06f);
    water.quadraticTo (base.x, base.y + s * 0.16f, base.x + s * 0.36f, base.y + s * 0.06f);
    g.setColour (Palette::leaf);
    g.strokePath (water, juce::PathStrokeType (s * 0.05f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void Card::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (Palette::outline);
    g.drawRoundedRectangle (r, 12.0f, 1.0f);

    if (title.isNotEmpty())
    {
        g.setColour (Palette::textDim);
        g.setFont (font (11.0f, true).withExtraKerningFactor (0.12f));
        g.drawText (title.toUpperCase(), 14, 10, getWidth() - 28, 14, juce::Justification::centredLeft);
    }
}

juce::Rectangle<int> Card::getContentBounds() const
{
    auto r = getLocalBounds().reduced (12, 10);
    if (title.isNotEmpty()) r.removeFromTop (18);
    return r;
}

Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& captionText)
    : caption (captionText)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (220);
    slider.setVelocityBasedMode (false);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxTextColourId, Palette::text);
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);

    if (auto* p = state.getParameter (paramId))
    {
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        if (p->convertFrom0to1 (0.0f) < 0.0f && p->convertFrom0to1 (1.0f) > 0.0f)
            slider.getProperties().set ("bipolar", true);   // arc grows from the centre
    }
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (16);
    slider.setBounds (r);
}

void Knob::paint (juce::Graphics& g)
{
    g.setColour (isEnabled() ? Palette::textDim : Palette::textFaint);
    g.setFont (font (12.0f, true));
    g.drawText (caption, getLocalBounds().removeFromTop (16), juce::Justification::centred);
}

Segmented::Segmented (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, juce::StringArray labelList)
    : labels (std::move (labelList))
{
    auto* param = state.getParameter (paramId);
    jassert (param != nullptr);

    attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float value)
    {
        const int idx = juce::jlimit (0, labels.size() - 1, juce::roundToInt (value));
        if (idx != selected)
        {
            selected = idx;
            repaint();
            if (onChange) onChange (selected);
        }
    });
    attachment->sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

Segmented::Segmented (juce::StringArray labelList)
    : labels (std::move (labelList))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Segmented::setSelected (int index)
{
    index = juce::jlimit (0, juce::jmax (0, labels.size() - 1), index);
    if (index == selected) return;
    selected = index;
    repaint();
    if (onChange) onChange (selected);
}

int Segmented::indexAt (juce::Point<int> p) const
{
    if (! getLocalBounds().contains (p) || labels.isEmpty()) return -1;
    return juce::jlimit (0, labels.size() - 1, p.x * labels.size() / juce::jmax (1, getWidth()));
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    const int idx = indexAt (e.getPosition());
    if (idx < 0 || idx == selected) return;
    if (attachment != nullptr) attachment->setValueAsCompleteGesture ((float) idx);
    else setSelected (idx);
}

Switch::Switch (juce::AudioProcessorValueTreeState& state, const juce::String& paramId)
{
    auto* param = state.getParameter (paramId);
    jassert (param != nullptr);
    attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v)
    {
        const bool now = v >= 0.5f;
        if (now != on)
        {
            on = now;
            repaint();
            if (onChange) onChange (on);
        }
    });
    attachment->sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Switch::mouseDown (const juce::MouseEvent&)
{
    attachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f);
}

void Switch::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float radius = r.getHeight() * 0.5f;
    if (on)
        g.setGradientFill (juce::ColourGradient (Palette::accent, r.getTopLeft(), Palette::accentDeep, r.getBottomRight(), false));
    else
        g.setColour (Palette::background);
    g.fillRoundedRectangle (r, radius);
    g.setColour (on ? Palette::accent : Palette::outline);
    g.drawRoundedRectangle (r, radius, 1.0f);

    const float d = r.getHeight() - 6.0f;
    const float x = on ? r.getRight() - 3.0f - d : r.getX() + 3.0f;
    g.setColour (on ? Palette::background : Palette::textDim);
    g.fillEllipse (x, r.getY() + 3.0f, d, d);
}

void Segmented::mouseMove (const juce::MouseEvent& e)
{
    const int h = indexAt (e.getPosition());
    if (h != hovered) { hovered = h; repaint(); }
}

void Segmented::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void Segmented::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = r.getHeight() * 0.5f;
    g.setColour (Palette::background);
    g.fillRoundedRectangle (r, radius);
    g.setColour (Palette::outline);
    g.drawRoundedRectangle (r, radius, 1.0f);

    const int n = juce::jmax (1, labels.size());
    const float segW = r.getWidth() / (float) n;

    for (int i = 0; i < labels.size(); ++i)
    {
        auto seg = juce::Rectangle<float> (r.getX() + segW * (float) i, r.getY(), segW, r.getHeight()).reduced (3.0f);
        if (i == selected)
        {
            g.setGradientFill (juce::ColourGradient (Palette::accent, seg.getTopLeft(), Palette::accentDeep, seg.getBottomRight(), false));
            g.fillRoundedRectangle (seg, seg.getHeight() * 0.5f);
            g.setColour (Palette::background);
        }
        else
        {
            if (i == hovered)
            {
                g.setColour (Palette::panelRaised);
                g.fillRoundedRectangle (seg, seg.getHeight() * 0.5f);
            }
            g.setColour (i == hovered ? Palette::text : Palette::textDim);
        }
        g.setFont (font (13.0f, true));
        g.drawText (labels[i], seg, juce::Justification::centred);
    }
}

Chip::Chip (juce::AudioProcessorValueTreeState& state, const juce::String& paramId)
{
    auto* param = state.getParameter (paramId);
    jassert (param != nullptr);
    attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v)
    {
        on = v >= 0.5f;
        repaint();
    });
    attachment->sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

namespace
{
const juce::Colour amber { 0xfff2c46d };
const juce::Colour red   { 0xffff6b7a };
}

void SongMeter::setState (int v, int l, bool held, bool mixed)
{
    if (v == voices && l == limit && held == overHeld && mixed == mismatch) return;
    voices = v; limit = l; overHeld = held; mismatch = mixed;
    repaint();
}

void SongMeter::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (isMouseOver() ? Palette::panelRaised : Palette::background);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (Palette::outline);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);

    juce::Colour c = Palette::textFaint;
    if (mismatch)
    {
        // Light off: limits can't be measured with several consoles in the project.
        const auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ r.getX() + 11.0f, r.getCentreY() });
        g.setColour (Palette::textFaint);
        g.drawEllipse (dot, 1.2f);
        g.setColour (Palette::textFaint);
        g.setFont (font (11.0f, true));
        g.drawText ("mixed", r.withTrimmedLeft (19.0f).withTrimmedRight (6.0f), juce::Justification::centredLeft);
        return;
    }
    if (voices > 0 || overHeld)
    {
        if (limit <= 0)                        c = Palette::leaf;
        else if (overHeld || voices > limit)   c = red;
        else if (voices == limit)              c = amber;
        else                                   c = Palette::leaf;
    }

    const float d = 7.0f;
    const auto dot = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + 11.0f, r.getCentreY() });
    if (c == red)
    {
        g.setColour (red.withAlpha (0.35f));
        g.fillEllipse (dot.expanded (3.0f));
    }
    g.setColour (c);
    g.fillEllipse (dot);

    g.setColour (Palette::textDim);
    g.setFont (font (11.0f, true));
    const juce::String text = limit > 0 ? juce::String (voices) + " / " + juce::String (limit) : juce::String (voices);
    g.drawText (text, r.withTrimmedLeft (19.0f).withTrimmedRight (6.0f), juce::Justification::centredLeft);
}

VerdictPopup::VerdictPopup()
{
    for (auto* b : { &resetButton, &closeButton }) addAndMakeVisible (b);
    resetButton.onClick = [this] { if (onReset) onReset(); setVisible (false); stopTimer(); };
    closeButton.onClick = [this] { setVisible (false); stopTimer(); };
}

void VerdictPopup::showConfused (const juce::String& t, const juce::String& d)
{
    show (1, t, d);
    confused = true;
    resetButton.setVisible (false);
    closeButton.setButtonText ("Got it");
}

void VerdictPopup::show (int consoles, const juce::String& t, const juce::String& d)
{
    confused = false;
    resetButton.setVisible (true);
    closeButton.setButtonText ("Nice!");
    count = consoles;
    title = t;
    detail = d;
    phase = 0.0f;
    setVisible (true);
    toFront (false);
    startTimerHz (30);
    repaint();
}

juce::Rectangle<int> VerdictPopup::panelBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (460, 300);
}

void VerdictPopup::resized()
{
    auto p = panelBounds().reduced (20);
    auto buttons = p.removeFromBottom (34);
    closeButton.setBounds (buttons.removeFromRight (110));
    buttons.removeFromRight (10);
    resetButton.setBounds (buttons.removeFromRight (120));
}

void VerdictPopup::mouseDown (const juce::MouseEvent& e)
{
    if (! panelBounds().contains (e.getPosition()))
    {
        setVisible (false);
        stopTimer();
    }
}

void VerdictPopup::drawConsole (juce::Graphics& g, juce::Rectangle<float> r, int mood)
{
    // A generic little console with a face.
    const float s = r.getHeight();
    auto body = r.reduced (s * 0.04f);
    g.setColour (Palette::background.withAlpha (0.5f));
    g.fillEllipse (body.getX() + s * 0.1f, body.getBottom() - s * 0.02f, body.getWidth() - s * 0.2f, s * 0.08f);

    g.setGradientFill (juce::ColourGradient (Palette::panelRaised.brighter (0.25f), body.getTopLeft(),
                                             Palette::panelRaised, body.getBottomLeft(), false));
    g.fillRoundedRectangle (body, s * 0.16f);
    g.setColour (Palette::accent.withAlpha (0.7f));
    g.drawRoundedRectangle (body, s * 0.16f, 1.6f);

    // Cartridge slot and cartridge
    auto slot = body.withHeight (s * 0.14f).withSizeKeepingCentre (body.getWidth() * 0.55f, s * 0.14f).translated (0, s * 0.08f);
    g.setColour (Palette::background);
    g.fillRoundedRectangle (slot, s * 0.04f);
    g.setColour (Palette::accent);
    g.fillRoundedRectangle (slot.reduced (s * 0.03f, 0).withTrimmedBottom (s * 0.06f).translated (0, -s * 0.07f), s * 0.03f);

    // Face
    const float eyeY = body.getY() + s * 0.48f;
    const float cx = body.getCentreX();
    const float eyeDx = s * 0.17f, eyeR = s * 0.045f;
    g.setColour (Palette::text);
    if (mood == 3)
    {
        // Confused: one small eye, one wide eye, a raised brow and a question mark.
        g.fillEllipse (cx - eyeDx - eyeR * 0.8f, eyeY - eyeR * 0.8f, eyeR * 1.6f, eyeR * 1.6f);
        g.drawEllipse (cx + eyeDx - eyeR * 1.4f, eyeY - eyeR * 1.4f, eyeR * 2.8f, eyeR * 2.8f, 1.6f);
        g.fillEllipse (cx + eyeDx - eyeR * 0.5f, eyeY - eyeR * 0.5f, eyeR, eyeR);
        g.drawLine (cx + eyeDx - eyeR * 2.0f, eyeY - s * 0.13f, cx + eyeDx + eyeR * 1.6f, eyeY - s * 0.16f, 1.6f);
        g.setColour (Palette::accent);
        g.setFont (font (s * 0.34f, true));
        g.drawText ("?", juce::Rectangle<float> (body.getRight() - s * 0.08f, body.getY() - s * 0.30f, s * 0.3f, s * 0.4f),
                    juce::Justification::centred);
        g.setColour (Palette::text);
    }
    else if (mood >= 2)
    {
        for (float ex : { cx - eyeDx, cx + eyeDx })   // dizzy x eyes
        {
            g.drawLine (ex - eyeR, eyeY - eyeR, ex + eyeR, eyeY + eyeR, 1.6f);
            g.drawLine (ex - eyeR, eyeY + eyeR, ex + eyeR, eyeY - eyeR, 1.6f);
        }
    }
    else
    {
        g.fillEllipse (cx - eyeDx - eyeR, eyeY - eyeR, eyeR * 2, eyeR * 2);
        g.fillEllipse (cx + eyeDx - eyeR, eyeY - eyeR, eyeR * 2, eyeR * 2);
    }

    juce::Path mouth;
    const float my = eyeY + s * 0.12f;
    if (mood == 3)
    {
        mouth.startNewSubPath (cx - s * 0.08f, my + s * 0.03f);   // squiggle
        mouth.quadraticTo (cx - s * 0.04f, my - s * 0.01f, cx, my + s * 0.03f);
        mouth.quadraticTo (cx + s * 0.04f, my + s * 0.07f, cx + s * 0.08f, my + s * 0.01f);
    }
    else if (mood == 1)
    {
        mouth.startNewSubPath (cx - s * 0.08f, my + s * 0.02f);   // nervous wobble
        mouth.quadraticTo (cx - s * 0.04f, my - s * 0.02f, cx, my + s * 0.02f);
        mouth.quadraticTo (cx + s * 0.04f, my + s * 0.06f, cx + s * 0.08f, my + s * 0.02f);
    }
    else
    {
        mouth.startNewSubPath (cx - s * 0.09f, my);
        mouth.quadraticTo (cx, my + s * 0.1f, cx + s * 0.09f, my);
    }
    g.strokePath (mouth, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Blush
    g.setColour (Palette::accent.withAlpha (0.35f));
    g.fillEllipse (cx - eyeDx - s * 0.11f, eyeY + s * 0.05f, s * 0.08f, s * 0.05f);
    g.fillEllipse (cx + eyeDx + s * 0.03f, eyeY + s * 0.05f, s * 0.08f, s * 0.05f);

    // Buttons
    g.setColour (Palette::leaf);
    g.fillEllipse (body.getRight() - s * 0.2f, body.getBottom() - s * 0.2f, s * 0.08f, s * 0.08f);
    g.setColour (Palette::accent);
    g.fillEllipse (body.getRight() - s * 0.32f, body.getBottom() - s * 0.16f, s * 0.08f, s * 0.08f);
}

void VerdictPopup::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background.withAlpha (0.72f));

    const auto p = panelBounds().toFloat();
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (p, 16.0f);
    g.setColour (Palette::accent.withAlpha (0.5f));
    g.drawRoundedRectangle (p.reduced (0.5f), 16.0f, 1.2f);

    // Consoles, bobbing gently out of step with each other.
    const int shown = juce::jlimit (1, 8, juce::jmax (1, count));
    const float size = shown <= 3 ? 84.0f : (shown <= 5 ? 66.0f : 50.0f);
    const float gap = 10.0f;
    const float rowW = shown * size + (shown - 1) * gap;
    float x = p.getCentreX() - rowW * 0.5f;
    const int mood = count <= 1 ? 0 : (count <= 3 ? 1 : 2);
    for (int i = 0; i < shown; ++i)
    {
        const float bob = confused ? std::sin (phase * 0.6f) * 2.0f : std::sin (phase + (float) i * 0.9f) * 3.0f;
        drawConsole (g, { x, p.getY() + 30.0f + (84.0f - size) + bob, size, size * 0.86f }, confused ? 3 : (count == 0 ? 0 : mood));
        x += size + gap;
    }
    if (count > 8)
    {
        g.setColour (Palette::accent);
        g.setFont (font (16.0f, true));
        g.drawText ("+" + juce::String (count - 8), juce::Rectangle<float> (p.getRight() - 64.0f, p.getY() + 14.0f, 50.0f, 22.0f),
                    juce::Justification::centredRight);
    }

    auto text = p.reduced (24.0f).withTrimmedTop (104.0f).withTrimmedBottom (44.0f);
    g.setColour (Palette::text);
    g.setFont (font (18.0f, true));
    g.drawFittedText (title, text.removeFromTop (48.0f).toNearestInt(), juce::Justification::centred, 2, 1.0f);
    g.setColour (Palette::textDim);
    g.setFont (font (12.5f));
    g.drawFittedText (detail, text.toNearestInt(), juce::Justification::centredTop, 3, 1.0f);
}

void Chip::mouseDown (const juce::MouseEvent&)
{
    if (isEnabled()) attachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f);
}

void Chip::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = r.getHeight() * 0.5f;
    const bool lit = on && isEnabled();
    const bool hot = isMouseOver() && isEnabled();

    g.setColour (lit ? Palette::accent.withAlpha (0.16f) : (hot ? Palette::panelRaised : Palette::background));
    g.fillRoundedRectangle (r, radius);
    g.setColour (lit ? Palette::accent.withAlpha (0.8f) : Palette::outline);
    g.drawRoundedRectangle (r, radius, 1.0f);

    const float d = 6.0f;
    g.setColour (lit ? Palette::accent : (isEnabled() ? Palette::textFaint : Palette::outline));
    g.fillEllipse (r.getX() + 9.0f, r.getCentreY() - d * 0.5f, d, d);

    g.setColour (lit ? Palette::text : (isEnabled() ? Palette::textDim : Palette::textFaint));
    g.setFont (font (11.5f, true));
    g.drawText (text, r.withTrimmedLeft (21.0f).withTrimmedRight (8.0f), juce::Justification::centredLeft);
}

EffectRow::EffectRow (juce::String n, juce::String c) : name (std::move (n)), console (std::move (c))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void EffectRow::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    if (selected || isMouseOver())
    {
        g.setColour (selected ? Palette::panelRaised : Palette::panelRaised.withAlpha (0.5f));
        g.fillRoundedRectangle (r, 9.0f);
    }
    if (selected)
    {
        g.setColour (Palette::accent);
        g.fillRoundedRectangle (r.withWidth (3.0f).reduced (0.0f, 9.0f), 1.5f);
    }

    const float d = 8.0f;
    g.setColour (active ? Palette::leaf : Palette::outline.brighter (0.2f));
    g.fillEllipse (r.getRight() - 20.0f, r.getCentreY() - d * 0.5f, d, d);

    g.setColour (selected ? Palette::text : Palette::textDim);
    g.setFont (font (14.0f, true));
    g.drawText (name, r.withTrimmedLeft (16.0f).withHeight (r.getHeight() * 0.58f), juce::Justification::bottomLeft);
    g.setColour (Palette::textFaint);
    g.setFont (font (11.5f));
    g.drawText (console, r.withTrimmedLeft (16.0f).withTrimmedTop (r.getHeight() * 0.58f), juce::Justification::topLeft);
}

void EnvelopeView::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (7.0f, 7.0f);
    const float W = area.getWidth(), H = area.getHeight();
    const float x0 = area.getX(), bottom = area.getBottom();
    constexpr float floorLevel = 1.5848932e-5f;

    if (state.sf2)
    {
        g.setColour (Palette::textDim);
        g.setFont (font (14.0f));
        g.drawText (state.cut ? "Using the SoundFont's envelope  -  notes cut on release"
                              : "Using the SoundFont's own envelope",
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    g.setColour (Palette::outline.withAlpha (0.6f));
    for (int i = 1; i < 4; ++i)
    {
        const float y = area.getY() + H * (float) i / 4.0f;
        g.drawHorizontalLine (juce::roundToInt (y), x0, area.getRight());
    }

    // Segment widths grow with sqrt(time) on a fixed scale, so turning one knob
    // never rescales the others. Sustain takes whatever is left.
    const float maxSeg = W * 0.26f;
    auto widthFor = [&] (float t) { return maxSeg * std::sqrt (juce::jlimit (0.0f, 10.0f, t) / 10.0f); };
    const float wA = widthFor (state.attack);
    const float wD = widthFor (state.decay);
    const float wR = state.cut ? 0.0f : widthFor (state.release);
    const float wS = juce::jmax (W * 0.12f, W - wA - wD - wR);

    auto yFor = [&] (float level) { return bottom - juce::jlimit (0.0f, 1.0f, level) * H; };
    const float sus = state.sustain;

    juce::Path curve;
    curve.startNewSubPath (x0, bottom);
    float x = x0;

    x += wA;
    curve.lineTo (x, yFor (1.0f));
    const juce::Point<float> peak (x, yFor (1.0f));

    const float target = juce::jmax (sus, floorLevel);
    for (int i = 1; i <= 32; ++i)
    {
        const float u = (float) i / 32.0f;
        curve.lineTo (x + wD * u, yFor (std::pow (target, u)));
    }
    x += wD;
    const juce::Point<float> decayEnd (x, yFor (sus));

    x += wS;
    curve.lineTo (x, yFor (sus));
    const juce::Point<float> releaseStart (x, yFor (sus));

    if (state.cut || wR <= 0.5f)
    {
        curve.lineTo (x + 1.0f, bottom);
    }
    else
    {
        for (int i = 1; i <= 32; ++i)
        {
            const float u = (float) i / 32.0f;
            const float level = sus * std::pow (floorLevel / juce::jmax (sus, floorLevel), u);
            curve.lineTo (x + wR * u, yFor (level));
        }
        curve.lineTo (x + wR, bottom);
    }

    juce::Path fill (curve);
    fill.lineTo (x0, bottom);
    fill.closeSubPath();

    g.setGradientFill (juce::ColourGradient (Palette::accent.withAlpha (0.28f), 0.0f, area.getY(),
                                             Palette::accent.withAlpha (0.02f), 0.0f, bottom, false));
    g.fillPath (fill);

    g.setColour (Palette::accent);
    g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    for (auto p : { peak, decayEnd, releaseStart })
    {
        g.setColour (Palette::panel);
        g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (p));
        g.setColour (Palette::accent);
        g.drawEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (p), 1.8f);
    }

    if (state.cut)
    {
        g.setColour (Palette::accent);
        g.setFont (font (11.0f, true));
        g.drawText ("CUT", juce::Rectangle<float> (releaseStart.x - 40.0f, bottom - 16.0f, 36.0f, 14.0f), juce::Justification::centredRight);
    }

}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (down ? Palette::panelRaised.brighter (0.12f) : over ? Palette::panelRaised.brighter (0.06f) : Palette::panelRaised);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (over ? Palette::accent.withAlpha (0.55f) : Palette::outline);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);

    const auto c = r.getCentre();
    const float s = r.getHeight() * 0.16f;
    juce::Path chev;
    if (icon == Icon::Left)
    {
        chev.startNewSubPath (c.x + s * 0.6f, c.y - s * 1.2f);
        chev.lineTo (c.x - s * 0.6f, c.y);
        chev.lineTo (c.x + s * 0.6f, c.y + s * 1.2f);
    }
    else
    {
        chev.startNewSubPath (c.x - s * 0.6f, c.y - s * 1.2f);
        chev.lineTo (c.x + s * 0.6f, c.y);
        chev.lineTo (c.x - s * 0.6f, c.y + s * 1.2f);
    }
    g.setColour (isEnabled() ? (over ? Palette::accent : Palette::text) : Palette::textFaint);
    g.strokePath (chev, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace lotus::ui
