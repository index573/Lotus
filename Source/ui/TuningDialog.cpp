#include "TuningDialog.h"
#include "Strings.h"

#include <cmath>

namespace lotus::ui
{

using Phase = LotusProcessor::TuningPhase;
using Outcome = LotusProcessor::TuningOutcome;

juce::String TuningDialog::noteName (int key)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((key % 12) + 12) % 12]) + juce::String (key / 12);
}

namespace
{
juce::String sentenceBreak() { return language() == Language::Japanese ? juce::String::fromUTF8 ("\xe3\x80\x82") : juce::String (". "); }

juce::String presetLabel (const lotus::PresetTuning& t)
{
    return juce::String (t.bank).paddedLeft ('0', 3) + ":" + juce::String (t.program).paddedLeft ('0', 3) + "  " + juce::String (t.name);
}

juce::String adjustText (const lotus::PresetTuning& t)
{
    const double e = t.errorCents;
    juce::String what;
    switch (t.kind)
    {
        case lotus::AdjustKind::Octave:
        {
            const int oct = (int) std::abs (std::round (e / 1200.0));
            what = trf (e > 0 ? (oct == 1 ? "1 octave too high" : "{n} octaves too high")
                              : (oct == 1 ? "1 octave too low" : "{n} octaves too low"),
                        { { "n", juce::String (oct) } })
                 + sentenceBreak() + tr ("Could be intentional (organs, harmonics): listen first.");
            break;
        }
        case lotus::AdjustKind::RootNote:
        {
            const int semis = (int) std::abs (std::round (e / 100.0));
            what = trf (e > 0 ? (semis == 1 ? "Root note 1 semitone too high" : "Root note {n} semitones too high")
                              : (semis == 1 ? "Root note 1 semitone too low" : "Root note {n} semitones too low"),
                        { { "n", juce::String (semis) } });
            break;
        }
        case lotus::AdjustKind::Detune:
            what = trf (e > 0 ? "{n} cents sharp" : "{n} cents flat", { { "n", juce::String (std::abs (std::lround (e))) } });
            break;
    }
    juce::String change = trf ("{before} c \xe2\x86\x92 {after} c", { { "before", (e > 0 ? "+" : "") + juce::String (std::lround (e)) },
                                                                       { "after", juce::String (std::lround (e + t.correctionCents)) } });
    if (t.playKey != lotus::tuningReferenceKey)
        change += "  " + trf ("(measured at {note})", { { "note", TuningDialog::noteName (t.playKey) } });
    return what + "\n" + change;
}

juce::String skipReason (const lotus::PresetTuning& t)
{
    switch (t.status)
    {
        case lotus::TuneStatus::InTune:      return trf ("Already in tune ({n} cents)", { { "n", (t.errorCents >= 0 ? "+" : "") + juce::String (std::lround (t.errorCents)) } });
        case lotus::TuneStatus::DrumKit:     return tr ("Drum kit");
        case lotus::TuneStatus::SoundEffect: return tr ("Sound effect");
        case lotus::TuneStatus::Atonal:      return tr ("No clear pitch (atonal)");
        case lotus::TuneStatus::Unclear:     return tr ("Pitch unclear (it drifts or wobbles)");
        case lotus::TuneStatus::NoZone:      return tr ("Nothing to play");
        case lotus::TuneStatus::Adjust:      break;
    }
    return {};
}
} // namespace

// ---------------------------------------------------------------------------------

class TuningDialog::PlayButton final : public juce::Button
{
public:
    PlayButton() : juce::Button ({}) {}
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (down ? Palette::panelRaised.brighter (0.15f) : over ? Palette::panelRaised.brighter (0.08f) : Palette::panelRaised);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (over ? Palette::accent.withAlpha (0.6f) : Palette::outline);
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);

        const float h = r.getHeight() * 0.36f;
        const float x = r.getX() + 10.0f, cy = r.getCentreY();
        juce::Path tri;
        tri.addTriangle (x, cy - h * 0.5f, x, cy + h * 0.5f, x + h * 0.85f, cy);
        g.setColour (over ? Palette::accent : Palette::text);
        g.fillPath (tri);
        g.setFont (font (11.5f, true));
        g.drawText (getButtonText(), r.withTrimmedLeft (x - r.getX() + h + 6.0f), juce::Justification::centredLeft);
    }
};

class TuningDialog::Row final : public juce::Component
{
public:
    // index < 0: a section heading
    Row (TuningDialog& d, int idx, juce::String heading = {}) : dialog (d), index (idx), title (std::move (heading))
    {
        if (index < 0) return;
        const auto& r = dialog.processor.getTuningRows()[(size_t) index];
        adjust = r.info.status == lotus::TuneStatus::Adjust;

        for (auto* b : { &before, &after }) addAndMakeVisible (b);
        before.setButtonText (adjust ? tr ("Before") : tr ("Listen"));
        after.setButtonText (tr ("After"));
        after.setVisible (adjust);
        before.onClick = [this] { dialog.processor.previewTuningRow (index, false); };
        after.onClick = [this] { dialog.processor.previewTuningRow (index, true); };
        before.setTooltip (trf ("Play {note} as the SoundFont has it now.", { { "note", noteName (r.info.playKey) } }));
        after.setTooltip (trf ("Play {note} with the fix.", { { "note", noteName (r.info.playKey) } }));
    }

    void paint (juce::Graphics& g) override
    {
        if (index < 0)
        {
            g.setColour (Palette::textDim);
            g.setFont (font (11.0f, true).withExtraKerningFactor (0.1f));
            g.drawText (title.toUpperCase(), getLocalBounds().withTrimmedLeft (4), juce::Justification::bottomLeft);
            return;
        }

        const auto& r = dialog.processor.getTuningRows()[(size_t) index];
        auto area = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (isMouseOver (true) ? Palette::panelRaised : Palette::panelRaised.withAlpha (0.45f));
        g.fillRoundedRectangle (area, 8.0f);

        auto text = getLocalBounds().reduced (10, 4);
        if (adjust)
        {
            // Fix / Keep toggle
            const auto toggle = toggleBounds().toFloat();
            g.setColour (r.apply ? Palette::accent.withAlpha (0.18f) : Palette::background);
            g.fillRoundedRectangle (toggle, toggle.getHeight() * 0.5f);
            g.setColour (r.apply ? Palette::accent : Palette::outline);
            g.drawRoundedRectangle (toggle, toggle.getHeight() * 0.5f, 1.0f);
            g.setColour (r.apply ? Palette::text : Palette::textDim);
            g.setFont (font (11.5f, true));
            g.drawText (r.apply ? tr ("Fix") : tr ("Keep"), toggle, juce::Justification::centred);
            text.removeFromLeft (toggleBounds().getRight() - 2);
        }
        text.removeFromRight (adjust ? 176 : 92);

        g.setColour (Palette::text);
        g.setFont (font (13.0f, true));
        g.drawText (presetLabel (r.info), text.removeFromTop (18), juce::Justification::centredLeft, true);
        g.setColour (adjust ? Palette::textDim : Palette::textFaint);
        g.setFont (font (11.5f));
        g.drawFittedText (adjust ? adjustText (r.info) : skipReason (r.info), text, juce::Justification::topLeft, 3, 1.0f);
    }

    void resized() override
    {
        if (index < 0) return;
        auto r = getLocalBounds().reduced (8, 0);
        const int h = 24;
        if (adjust)
        {
            after.setBounds (r.removeFromRight (82).withSizeKeepingCentre (82, h));
            r.removeFromRight (6);
        }
        before.setBounds (r.removeFromRight (82).withSizeKeepingCentre (82, h));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (adjust && toggleBounds().contains (e.getPosition()))
        {
            const auto& r = dialog.processor.getTuningRows()[(size_t) index];
            dialog.processor.setTuningRowApply (index, ! r.apply);
            repaint();
        }
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    bool isAdjust() const { return adjust; }

private:
    juce::Rectangle<int> toggleBounds() const { return { 10, getHeight() / 2 - 12, 64, 24 }; }

    TuningDialog& dialog;
    int index;
    juce::String title;
    bool adjust = false;
    PlayButton before, after;
};

// ---------------------------------------------------------------------------------

TuningDialog::TuningDialog (LotusProcessor& p) : processor (p)
{
    for (auto* b : { &button1, &button2, &button3, &button4 }) addChildComponent (b);
    addChildComponent (viewport);
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
}

TuningDialog::~TuningDialog()
{
    viewport.setViewedComponent (nullptr, false);
}

juce::Rectangle<int> TuningDialog::panelBounds() const
{
    switch (shown)
    {
        case Phase::Review:    return getLocalBounds().withSizeKeepingCentre (690, 430);
        case Phase::AskSave:   return getLocalBounds().withSizeKeepingCentre (560, 220);
        case Phase::Ask:       return getLocalBounds().withSizeKeepingCentre (520, 210);
        case Phase::Analyzing: return getLocalBounds().withSizeKeepingCentre (440, 160);
        case Phase::Done:      return getLocalBounds().withSizeKeepingCentre (460, 160);
        case Phase::Idle: case Phase::Scanning: break;
    }
    return {};
}

void TuningDialog::sync()
{
    const auto phase = processor.getTuningPhase();
    const bool visible = phase != Phase::Idle && phase != Phase::Scanning;

    if (phase != shown || processor.getTuningOutcome() != shownOutcome)
    {
        shown = phase;
        shownOutcome = processor.getTuningOutcome();
        setVisible (visible);
        if (visible) toFront (false);
        if (phase == Phase::Review) rebuildRows();
        configureButtons();
        resized();
        repaint();
    }

    if (phase == Phase::Analyzing && processor.getTuningDone() != shownDone)
    {
        shownDone = processor.getTuningDone();
        repaint (panelBounds());
    }
}

void TuningDialog::refreshTexts()
{
    if (shown == Phase::Review) rebuildRows();
    configureButtons();
    repaint();
}

void TuningDialog::configureButtons()
{
    for (auto* b : { &button1, &button2, &button3, &button4 }) { b->setVisible (false); b->onClick = nullptr; }
    viewport.setVisible (shown == Phase::Review);

    auto use = [] (juce::TextButton& b, const juce::String& text, std::function<void()> f)
    {
        b.setButtonText (text);
        b.onClick = std::move (f);
        b.setVisible (true);
    };

    switch (shown)
    {
        case Phase::Ask:
            use (button1, tr ("Yes"), [this] { processor.answerTuningPrompt (true); });
            use (button2, tr ("No"),  [this] { processor.answerTuningPrompt (false); });
            break;
        case Phase::Analyzing:
            use (button1, tr ("Cancel"), [this] { processor.cancelTuningCheck(); });
            break;
        case Phase::Review:
            use (button1, tr ("Continue"), [this] { processor.finishTuningReview(); });
            use (button2, tr ("Cancel"),   [this] { processor.cancelTuningCheck(); });
            break;
        case Phase::AskSave:
            use (button1, tr ("Yes, save"),       [this] { processor.saveTuningFixes (LotusProcessor::SaveChoice::Save); });
            use (button2, tr ("Yes, save as..."), [this] { if (onSaveAs) onSaveAs(); });
            use (button3, tr ("No"),              [this] { processor.saveTuningFixes (LotusProcessor::SaveChoice::KeepInProject); });
            use (button4, tr ("Cancel"),          [this] { processor.saveTuningFixes (LotusProcessor::SaveChoice::Cancel); });
            break;
        case Phase::Done:
            use (button1, tr ("OK"), [this] { processor.dismissTuningMessage(); });
            break;
        case Phase::Idle: case Phase::Scanning: break;
    }
}

void TuningDialog::rebuildRows()
{
    rows.clear();
    list.removeAllChildren();
    const auto& data = processor.getTuningRows();
    adjustedCount = skippedCount = 0;
    for (const auto& r : data) (r.info.status == lotus::TuneStatus::Adjust ? adjustedCount : skippedCount)++;

    auto heading = [this] (const juce::String& t) { rows.push_back (std::make_unique<Row> (*this, -1, t)); };
    if (adjustedCount > 0) heading (trf ("To adjust ({n})", { { "n", juce::String (adjustedCount) } }));
    for (int i = 0; i < (int) data.size(); ++i)
    {
        if (i == adjustedCount && skippedCount > 0) heading (trf ("Left alone ({n})", { { "n", juce::String (skippedCount) } }));
        rows.push_back (std::make_unique<Row> (*this, i));
    }
    for (auto& r : rows) list.addAndMakeVisible (*r);
    layoutRows();
}

void TuningDialog::layoutRows()
{
    const int w = juce::jmax (100, viewport.getWidth() - viewport.getScrollBarThickness() - 2);
    int y = 0;
    for (auto& r : rows)
    {
        const bool heading = dynamic_cast<Row*> (r.get()) != nullptr && r->getNumChildComponents() == 0;
        const auto* row = dynamic_cast<Row*> (r.get());
        const int h = heading ? 26 : (row != nullptr && row->isAdjust() ? 62 : 46);
        r->setBounds (0, y, w, h);
        y += h + 4;
    }
    list.setSize (w, juce::jmax (y, 10));
}

void TuningDialog::resized()
{
    auto p = panelBounds().reduced (22, 18);
    if (p.isEmpty()) return;

    auto buttons = p.removeFromBottom (32);
    const int bw = shown == Phase::AskSave ? 118 : 110;
    for (auto* b : { &button1, &button2, &button3, &button4 })
        if (b->isVisible())
        {
            b->setBounds (buttons.removeFromRight (bw));
            buttons.removeFromRight (8);
        }

    if (shown == Phase::Review)
    {
        p.removeFromTop (54);
        p.removeFromBottom (10);
        viewport.setBounds (p);
        layoutRows();
    }
}

void TuningDialog::paint (juce::Graphics& g)
{
    if (shown == Phase::Idle || shown == Phase::Scanning) return;
    g.fillAll (Palette::background.withAlpha (0.78f));

    const auto panel = panelBounds().toFloat();
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (panel, 16.0f);
    g.setColour (Palette::accent.withAlpha (0.5f));
    g.drawRoundedRectangle (panel.reduced (0.5f), 16.0f, 1.2f);

    auto area = panelBounds().reduced (24, 20);
    auto title = [&] (const juce::String& t, int h = 30)
    {
        g.setColour (Palette::text);
        g.setFont (font (17.0f, true));
        g.drawFittedText (t, area.removeFromTop (h), juce::Justification::topLeft, 2, 1.0f);
    };
    auto body = [&] (const juce::String& t, int h, juce::Colour c = Palette::textDim)
    {
        g.setColour (c);
        g.setFont (font (12.5f));
        g.drawFittedText (t, area.removeFromTop (h), juce::Justification::topLeft, 4, 1.0f);
    };

    switch (shown)
    {
        case Phase::Ask:
            title (tr ("Strange tuning and incorrect root notes detected."), 48);
            body (tr ("Attempt to adjust to C5/A=440Hz?"), 24, Palette::text);
            body (tr ("Lotus will measure every instrument at C5 (middle C, MIDI 60). Drum kits and sounds without a clear pitch are left alone, and you can listen to each change before keeping it."), 70);
            break;

        case Phase::Analyzing:
        {
            const int done = processor.getTuningDone(), total = juce::jmax (1, processor.getTuningTotal());
            title (trf ("Analyzing... {done}/{total}", { { "done", juce::String (juce::jmin (done + 1, total)) }, { "total", juce::String (total) } }));
            area.removeFromTop (8);
            const auto bar = area.removeFromTop (12).toFloat();
            g.setColour (Palette::background);
            g.fillRoundedRectangle (bar, 6.0f);
            g.setColour (Palette::accent);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, (float) done / (float) total)), 6.0f);
            area.removeFromTop (10);
            const auto* sf = processor.getSoundFont();
            if (sf != nullptr && juce::isPositiveAndBelow (done, (int) sf->presets.size()))
                body (juce::String (sf->presets[(size_t) done].name), 20, Palette::textFaint);
            break;
        }

        case Phase::Review:
            title (tr ("Tuning results"), 26);
            body (trf ("{a} to adjust, {s} left alone. Click Fix/Keep to choose, and use the play buttons to hear C5 before and after.",
                       { { "a", juce::String (adjustedCount) }, { "s", juce::String (skippedCount) } }), 30);
            break;

        case Phase::AskSave:
            title (tr ("Save fixed version?"));
            body (trf ("Yes, save writes {file} next to the original.", { { "file", processor.getFixedSaveTarget().getFileName() } }), 22);
            body (tr ("No keeps the fixes in this project only. The .sf2 file isn't changed."), 22);
            body (tr ("Cancel throws the fixes away."), 22);
            if (shownOutcome == Outcome::SaveFailed)
                body (trf ("Couldn't save: {error}", { { "error", tr (processor.getTuningDetail()) } }), 40, juce::Colour (0xffff6b7a));
            break;

        case Phase::Done:
        {
            juce::String message;
            switch (shownOutcome)
            {
                case Outcome::Saved:          message = trf ("Saved as {file}. Lotus is now using the fixed file.", { { "file", processor.getTuningDetail() } }); break;
                case Outcome::Kept:           message = tr ("The fixes are kept in this project. The .sf2 file wasn't changed."); break;
                case Outcome::NothingChanged: message = tr ("Nothing was changed."); break;
                case Outcome::SaveFailed: case Outcome::None: break;
            }
            title (tr ("Tuning check"));
            body (message, 60, Palette::text);
            break;
        }

        case Phase::Idle: case Phase::Scanning: break;
    }
}

} // namespace lotus::ui
