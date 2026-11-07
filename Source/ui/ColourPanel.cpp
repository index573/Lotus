#include "ColourPanel.h"

#include "LotusLookAndFeel.h"
#include "Strings.h"
#include "Widgets.h"

namespace lotus::ui
{

ColourPanel::ColourPanel (LotusProcessor& p) : processor (p)
{
    for (auto* b : { &resetButton, &doneButton }) addAndMakeVisible (b);
    resetButton.onClick = [this]
    {
        for (int c = 0; c < lotus::Synth::numColours; ++c) processor.setColourPreset (c, -1);
        if (onChange) onChange();
        repaint();
    };
    doneButton.onClick = [this] { setVisible (false); };
    setVisible (false);
}

void ColourPanel::show()
{
    resetButton.setButtonText (tr ("Reset all"));
    doneButton.setButtonText (tr ("Done"));
    hover = -1;
    setVisible (true);
    toFront (false);
    repaint();
}

juce::Rectangle<int> ColourPanel::panelBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (620, 372);
}

juce::Rectangle<int> ColourPanel::tileBounds (int index) const
{
    auto grid = panelBounds().reduced (22).withTrimmedTop (82).withTrimmedBottom (48);
    constexpr int gap = 8;
    const int w = (grid.getWidth() - 3 * gap) / 4;
    const int h = (grid.getHeight() - 3 * gap) / 4;
    const int col = index % 4, row = index / 4;
    return { grid.getX() + col * (w + gap), grid.getY() + row * (h + gap), w, h };
}

int ColourPanel::tileAt (juce::Point<int> pos) const
{
    for (int i = 0; i < lotus::Synth::numColours; ++i)
        if (tileBounds (i).contains (pos)) return i;
    return -1;
}

void ColourPanel::resized()
{
    auto buttons = panelBounds().reduced (22).removeFromBottom (34);
    doneButton.setBounds (buttons.removeFromRight (100));
    buttons.removeFromRight (10);
    resetButton.setBounds (buttons.removeFromRight (110));
}

void ColourPanel::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background.withAlpha (0.72f));

    const auto p = panelBounds().toFloat();
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (p, 16.0f);
    g.setColour (Palette::accent.withAlpha (0.5f));
    g.drawRoundedRectangle (p.reduced (0.5f), 16.0f, 1.2f);

    auto text = panelBounds().reduced (22);
    g.setColour (Palette::text);
    g.setFont (font (19.0f, true));
    g.drawText (tr ("Note colours"), text.removeFromTop (28), juce::Justification::centredLeft);
    g.setColour (Palette::textDim);
    g.setFont (font (12.5f));
    g.drawFittedText (tr ("Each note colour can play its own preset. In FL Studio, colour the notes in the piano roll. In other hosts, use MIDI channels 1-16."),
                      text.removeFromTop (44), juce::Justification::topLeft, 3, 1.0f);

    for (int i = 0; i < lotus::Synth::numColours; ++i)
    {
        const auto t = tileBounds (i).toFloat();
        const bool own = processor.getColourPreset (i) >= 0;
        g.setColour (i == hover ? Palette::panelRaised.brighter (0.08f) : Palette::panelRaised);
        g.fillRoundedRectangle (t, 9.0f);
        g.setColour (i == hover ? Palette::accent.withAlpha (0.6f) : own ? noteColour (i).withAlpha (0.55f) : Palette::outline);
        g.drawRoundedRectangle (t.reduced (0.5f), 9.0f, 1.0f);

        const float d = t.getHeight() - 16.0f;
        const auto dot = juce::Rectangle<float> (t.getX() + 8.0f, t.getCentreY() - d * 0.5f, d, d);
        g.setColour (noteColour (i).withAlpha (own ? 1.0f : 0.35f));
        g.fillEllipse (dot);
        g.setColour (own ? Palette::background : Palette::text);
        g.setFont (font (12.0f, true));
        g.drawText (juce::String (i + 1), dot, juce::Justification::centred);

        const auto label = t.withTrimmedLeft (d + 16.0f).withTrimmedRight (6.0f);
        g.setColour (own ? Palette::text : Palette::textFaint);
        g.setFont (font (12.0f));
        g.drawFittedText (own ? processor.getColourPresetName (i) : tr ("Main preset"), label.toNearestInt(),
                          juce::Justification::centredLeft, 2, 0.9f);
    }
}

void ColourPanel::mouseMove (const juce::MouseEvent& e)
{
    const int h = tileAt (e.getPosition());
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void ColourPanel::mouseExit (const juce::MouseEvent&)
{
    if (hover != -1) { hover = -1; repaint(); }
}

void ColourPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! panelBounds().contains (e.getPosition())) { setVisible (false); return; }
    const int tile = tileAt (e.getPosition());
    if (tile >= 0) showPresetMenu (tile);
}

void ColourPanel::showPresetMenu (int colour)
{
    const auto* sf = processor.getSoundFont();
    if (sf == nullptr || sf->presets.empty()) return;

    juce::PopupMenu menu;
    const int current = processor.getColourPreset (colour);
    menu.addItem (1, tr ("Main preset"), true, current < 0);
    int bank = -1;
    for (size_t i = 0; i < sf->presets.size(); ++i)
    {
        const auto& pr = sf->presets[i];
        if (pr.bank != bank)
        {
            bank = pr.bank;
            menu.addSectionHeader (bank == 128 ? tr ("Bank 128 (Drums)") : trf ("Bank {n}", { { "n", juce::String (bank) } }));
        }
        menu.addItem ((int) i + 2, juce::String (pr.program).paddedLeft ('0', 3) + "  " + juce::String::fromUTF8 (pr.name.c_str()),
                      true, current == (int) i);
    }

    const auto area = localAreaToGlobal (tileBounds (colour));
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (area).withMaximumNumColumns (3),
                        [safe = juce::Component::SafePointer<ColourPanel> (this), colour] (int result)
                        {
                            if (safe == nullptr || result <= 0) return;
                            safe->processor.setColourPreset (colour, result - 2);
                            if (safe->onChange) safe->onChange();
                            safe->repaint();
                        });
}

} // namespace lotus::ui
