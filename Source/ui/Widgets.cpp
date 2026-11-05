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
