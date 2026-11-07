#include "PluginEditor.h"
#include "ui/Strings.h"

#include <algorithm>

using namespace lotus::ui;

namespace
{
struct EffectInfo
{
    const char* name;
    const char* console;
    const char* onParam;
    lotus::ConsoleId home;
    const char* description;
};

const EffectInfo effectInfo[] =
{
    { "SNES Echo", "Super Nintendo", ParamIDs::snesOn, lotus::ConsoleId::Snes,
      "The S-DSP echo unit: up to 240 ms of delay, signed feedback, and the 8-tap FIR filter that colours every repeat." },
    { "N64 Reverb", "Nintendo 64", ParamIDs::n64On, lotus::ConsoleId::N64,
      "libultra's effect bus: one delay line through all-pass sections, with a low-pass in the loop and modulated taps for chorus and flange." },
    { "PS1 Reverb", "PlayStation", ParamIDs::ps1On, lotus::ConsoleId::Ps1,
      "The SPU reverb with Sony's preset rooms, running at half rate like the hardware." },
    { "LED Filter", "Amiga", ParamIDs::ledOn, lotus::ConsoleId::Amiga500,
      "The Amiga's switchable 12 dB/oct low-pass at about 3.1 kHz. It was wired to the power LED, and games and trackers flicked it on for a darker sound." },
};

// English singular/plural names for the verdict popup (other languages translate the singular).
juce::String consoleNoun (lotus::ConsoleId id, bool plural)
{
    using C = lotus::ConsoleId;
    juce::String one, many;
    switch (id)
    {
        case C::Nes:        one = "NES";         many = "NESes"; break;
        case C::Snes:       one = "SNES";        many = "SNESes"; break;
        case C::N64:        one = "N64";         many = "N64s"; break;
        case C::Gba:        one = "GBA";         many = "GBAs"; break;
        case C::NintendoDs: one = "Nintendo DS"; many = "Nintendo DSes"; break;
        case C::Ps1:        one = "PS1";         many = "PS1s"; break;
        case C::Ps2:        one = "PS2";         many = "PS2s"; break;
        case C::MegaDrive:  one = "Mega Drive";  many = "Mega Drives"; break;
        case C::Saturn:     one = "Saturn";      many = "Saturns"; break;
        case C::Amiga500:   one = "Amiga 500";   many = "Amiga 500s"; break;
        case C::Amiga1200:  one = "Amiga 1200";  many = "Amiga 1200s"; break;
        case C::Modern: case C::Linear: case C::Raw: case C::count: one = "computer"; many = "computers"; break;
    }
    if (language() != Language::English) return tr (one);
    return plural ? many : one;
}

juce::String verdictQuip (int n)
{
    if (n == 1) return tr ("Great job!");
    if (n == 2) return tr ("You got a spare, right?");
    if (n == 3) return tr ("Time to raid the attic.");
    if (n == 4) return tr ("Is this a LAN party?");
    if (n <= 7) return tr ("That's a shelf, not a console.");
    return tr ("You're running a museum now.");
}

// Japanese runs sentences together; English separates them with a space.
juce::String gap() { return language() == Language::Japanese ? juce::String() : juce::String (" "); }

juce::String voicesText (int n) { return trf (n == 1 ? "{n} voice" : "{n} voices", { { "n", juce::String (n) } }); }
juce::String tracksText (int n) { return trf (n == 1 ? "{n} track" : "{n} tracks", { { "n", juce::String (n) } }); }

juce::String joinList (juce::StringArray items)
{
    if (language() == Language::Japanese) return items.joinIntoString (juce::String::fromUTF8 ("\xe3\x80\x81"));
    if (items.size() <= 1) return items.joinIntoString ({});
    const auto last = items[items.size() - 1];
    items.remove (items.size() - 1);
    return items.joinIntoString (", ") + " and " + last;
}

void styleSmallLabel (juce::Label& l, float size = 11.5f, bool caps = false)
{
    l.setFont (caps ? font (11.0f, true).withExtraKerningFactor (0.12f) : font (size));
    l.setColour (juce::Label::textColourId, Palette::textDim);
    l.setJustificationType (juce::Justification::topLeft);
    l.setMinimumHorizontalScale (1.0f);
    l.setInterceptsMouseClicks (false, false);
}
} // namespace

LotusEditor::LotusEditor (LotusProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      attackKnob  (p.params, ParamIDs::attack,  "Attack"),
      decayKnob   (p.params, ParamIDs::decay,   "Decay"),
      sustainKnob (p.params, ParamIDs::sustain, "Sustain"),
      releaseKnob (p.params, ParamIDs::release, "Release"),
      formatChip (p.params, ParamIDs::nativeFormat),
      voicesChip (p.params, ParamIDs::voiceLimit),
      noteOffSwitch   (p.params, ParamIDs::noteOff,   { "Release", "Cut" }),
      envSourceSwitch (p.params, ParamIDs::envSource, { "Lotus", "SF2" }),
      volumeKnob (p.params, ParamIDs::volume,    "Volume"),
      bendKnob   (p.params, ParamIDs::bendRange, "Bend"),
      glideKnob  (p.params, ParamIDs::glide,     "Glide"),
      vibShapeSwitch (p.params, ParamIDs::vibShape, { "Sine", "Triangle", "Square" }),
      vibAmountKnob (p.params, ParamIDs::vibAmount, "Amount"),
      vibSpeedKnob  (p.params, ParamIDs::vibSpeed,  "Speed"),
      vibDelayKnob  (p.params, ParamIDs::vibDelay,  "Delay"),
      vibAttackKnob (p.params, ParamIDs::vibAttack, "Attack"),
      tuneKnob (p.params, ParamIDs::tune, "Tune"),
      snesFilter (p.params, ParamIDs::snesFilter, { "Flat", "Low", "High", "Band" }),
      snesDelayKnob    (p.params, ParamIDs::snesDelay,    "Delay"),
      snesFeedbackKnob (p.params, ParamIDs::snesFeedback, "Feedback"),
      snesVolumeKnob   (p.params, ParamIDs::snesVolume,   "Volume"),
      n64MixKnob   (p.params, ParamIDs::n64Mix,   "Mix"),
      ps1DepthKnob (p.params, ParamIDs::ps1Depth, "Depth"),
      tuningDialog (p),
      colourPanel (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    // Header and preset bar
    for (auto* c : std::initializer_list<juce::Component*> { &logo, &languageButton, &pageTabs, &loadButton, &presetCard,
                                                              &prevButton, &presetBox, &nextButton, &coloursButton, &infoLabel })
        content.addAndMakeVisible (c);
    pageTabs.onChange = [this] (int pg) { showPage (pg); };
    loadButton.onClick = [this] { openFileChooser(); };
    languageButton.onClick = [this] { showLanguageMenu(); };
    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (+1); };
    coloursButton.onClick = [this] { colourPanel.show(); };
    colourPanel.onChange = [this] { coloursButton.setLit (processor.hasColourPresets()); };
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx >= 0 && idx != processor.getPresetIndex())
            processor.selectPreset (idx);
        seenPreset = idx;
    };
    infoLabel.setJustificationType (juce::Justification::centredRight);
    infoLabel.setFont (font (12.5f));
    infoLabel.setColour (juce::Label::textColourId, Palette::textDim);
    infoLabel.setInterceptsMouseClicks (false, false);

    // Sound page
    pages[soundPage] = { &envCard, &envelopeView, &attackKnob, &decayKnob, &sustainKnob, &releaseKnob,
                         &consoleCard, &consoleBox, &formatChip, &voicesChip, &songMeter,
                         &playCard, &noteOffLabel, &noteOffSwitch, &envSourceLabel, &envSourceSwitch,
                         &outputCard, &volumeKnob, &bendKnob, &glideKnob };
    consoleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.params, ParamIDs::console, consoleBox);
    consoleBox.onChange = [this] { refreshConsole(); };
    songMeter.onClick = [this] { showVerdict(); };
    verdict.onReset = [this] { lotus::VoiceRegistry::get().resetPeak (processor.getConsole()); };
    for (auto* l : { &noteOffLabel, &envSourceLabel })
    {
        styleSmallLabel (*l, 12.5f);
        l->setJustificationType (juce::Justification::centredLeft);
    }
    noteOffSwitch.onChange = [this] (int) { updateEnablement(); };
    envSourceSwitch.onChange = [this] (int) { updateEnablement(); };

    // Pitch page
    pages[pitchPage] = { &vibCard, &vibratoView, &vibShapeSwitch, &vibAmountKnob, &vibSpeedKnob, &vibDelayKnob, &vibAttackKnob,
                         &tuneCard, &tuneKnob, &tuneUnitSwitch, &tuneHint,
                         &sfTuneCard, &sfTuneText, &sfTuneStatus, &checkTuningButton, &clearFixesButton };
    styleSmallLabel (tuneHint);
    styleSmallLabel (sfTuneText, 12.0f);
    styleSmallLabel (sfTuneStatus, 12.0f);
    sfTuneStatus.setColour (juce::Label::textColourId, Palette::leaf);
    tuneUnitSwitch.setSelected (tuneInHz() ? 1 : 0);
    tuneUnitSwitch.onChange = [this] (int unit)
    {
        processor.params.state.setProperty ("tuneUnit", unit, nullptr);
        refreshTuneText();
    };
    checkTuningButton.onClick = [this] { processor.startTuningCheck (false); };
    clearFixesButton.onClick = [this] { processor.clearStoredCorrections(); refreshCorrectionsStatus(); };

    // Effects page
    for (int i = 0; i < numEffects; ++i)
    {
        fxRows[(size_t) i] = std::make_unique<EffectRow> (effectInfo[i].name, effectInfo[i].console);
        fxRows[(size_t) i]->onClick = [this, i] { showEffect (i); };
        fxSwitches[(size_t) i] = std::make_unique<Switch> (p.params, effectInfo[i].onParam);
        fxSwitches[(size_t) i]->onChange = [this] (bool) { updateEnablement(); };
    }
    styleSmallLabel (fxDesc, 12.5f);
    styleSmallLabel (fxControlLabel, 11.0f, true);
    styleSmallLabel (fxHint, 12.0f);
    fxHint.setColour (juce::Label::textColourId, Palette::leaf);
    n64PresetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.params, ParamIDs::n64Preset, n64PresetBox);
    ps1PresetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.params, ParamIDs::ps1Preset, ps1PresetBox);

    effectControls[snes] = { &snesFilter, &snesDelayKnob, &snesFeedbackKnob, &snesVolumeKnob };
    effectControls[n64] = { &n64PresetBox, &n64MixKnob };
    effectControls[ps1] = { &ps1PresetBox, &ps1DepthKnob };
    effectControls[led] = {};

    pages[effectsPage] = { &fxListCard, &fxCard, &fxDesc, &fxControlLabel, &fxHint };
    for (auto& r : fxRows) pages[effectsPage].push_back (r.get());

    for (auto& list : pages)
        for (auto* c : list) content.addChildComponent (c);
    for (auto& sw : fxSwitches) content.addChildComponent (sw.get());
    for (auto& list : effectControls)
        for (auto* c : list) content.addChildComponent (c);

    content.addChildComponent (verdict);
    content.addChildComponent (tuningDialog);
    content.addChildComponent (colourPanel);
    tuningDialog.onSaveAs = [this] { saveAsChooser(); };

    setResizable (true, true);
    setResizeLimits (baseWidth * 3 / 4, baseHeight * 3 / 4, baseWidth * 2, baseHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setSize (baseWidth, baseHeight);

    sendLookAndFeelChange();
    applyLanguage();
    updateEnablement();
    showPage (soundPage);
    startTimerHz (30);
}

LotusEditor::~LotusEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

float LotusEditor::param (const char* id) const
{
    return processor.params.getRawParameterValue (id)->load();
}

bool LotusEditor::tuneInHz() const
{
    return (int) processor.params.state.getProperty ("tuneUnit", 0) == 1;
}

// ---------------------------------------------------------------------------------
// Language

void LotusEditor::applyLanguage()
{
    seenLanguage = languageGeneration();

    loadButton.setButtonText (tr ("Load SF2"));
    loadButton.setTooltip (tr ("Load a SoundFont (.sf2). You can also drag one onto the window."));
    languageButton.setTooltip (tr ("Language"));
    prevButton.setTooltip (tr ("Previous preset"));
    nextButton.setTooltip (tr ("Next preset"));
    coloursButton.setTooltip (tr ("Note colours: give each colour (MIDI channel) its own preset"));

    attackKnob.getSlider().setTooltip (tr ("Fade-in time. 0 = instant."));
    decayKnob.getSlider().setTooltip (tr ("Time to fall from full level to the sustain level."));
    sustainKnob.getSlider().setTooltip (tr ("Level held while the key is down."));
    releaseKnob.getSlider().setTooltip (tr ("Time from note-off to silence. Exact: 0 stops dead."));
    formatChip.setTooltip (tr ("Play the samples as they'd sound after conversion to the console's own format (BRR, ADPCM, DPCM). Converting takes a moment after switching."));
    voicesChip.setTooltip (tr ("Limit polyphony to the console's hardware voices. Extra notes steal the oldest, like the real thing."));
    noteOffLabel.setText (tr ("Note off"), juce::dontSendNotification);
    envSourceLabel.setText (tr ("Envelope"), juce::dontSendNotification);
    noteOffSwitch.setTooltip (tr ("Release: notes fade out over the release time.\nCut: notes stop the moment the key is let go."));
    envSourceSwitch.setTooltip (tr ("Lotus: use the ADSR knobs.\nSF2: use the envelopes stored in the SoundFont."));
    bendKnob.getSlider().setTooltip (tr ("Pitch bend range in semitones."));
    glideKnob.getSlider().setTooltip (tr ("Slide from the previous note's pitch. 0 = off."));

    vibAmountKnob.getSlider().setTooltip (tr ("How far the pitch swings, in cents (100 cents = 1 semitone). 0 = off."));
    vibSpeedKnob.getSlider().setTooltip (tr ("How fast the pitch swings."));
    vibDelayKnob.getSlider().setTooltip (tr ("How long a note holds steady before the vibrato starts."));
    vibAttackKnob.getSlider().setTooltip (tr ("How long the vibrato takes to fade in."));
    vibShapeSwitch.setTooltip (tr ("Vibrato shape."));
    tuneKnob.getSlider().setTooltip (tr ("Master tuning. Type cents (-31.8) or Hz (432 Hz). Double-click to reset to A4 = 440 Hz."));
    tuneHint.setText (tr ("Standard pitch is A4 = 440 Hz. Click the value to type cents or Hz."), juce::dontSendNotification);
    sfTuneText.setText (tr ("Finds presets with wrong root notes or detuned samples by measuring each one at C5 (middle C, MIDI 60) against A4 = 440 Hz. Drum kits and sounds without a clear pitch are left alone."),
                        juce::dontSendNotification);
    checkTuningButton.setButtonText (tr ("Check tuning..."));
    checkTuningButton.setTooltip (tr ("Analyse this SoundFont now."));
    clearFixesButton.setButtonText (tr ("Clear fixes"));
    clearFixesButton.setTooltip (tr ("Remove the tuning fixes stored in this project."));

    for (int i = 0; i < numEffects; ++i)
        fxSwitches[(size_t) i]->setTooltip (trf ("Turn the {effect} on or off.", { { "effect", tr (effectInfo[i].name) } }));
    snesFilter.setTooltip (tr ("Echo FIR filter: the four presets from Nintendo's standard SNES sound driver."));
    snesDelayKnob.getSlider().setTooltip (tr ("EDL: echo delay in 16 ms steps."));
    snesFeedbackKnob.getSlider().setTooltip (tr ("EFB: feedback. Negative values flip the phase of each repeat."));
    snesVolumeKnob.getSlider().setTooltip (tr ("EVOL: echo return level."));
    n64MixKnob.getSlider().setTooltip (tr ("Effect send level."));
    ps1DepthKnob.getSlider().setTooltip (tr ("Reverb return level."));

    // Combo boxes: same ids, translated text.
    {
        const int sel = consoleBox.getSelectedId();
        consoleBox.clear (juce::dontSendNotification);
        juce::String lastGroup;
        for (int i = 0; i < lotus::numConsoles; ++i)
        {
            const auto& prof = lotus::getConsoleProfile ((lotus::ConsoleId) i);
            if (lastGroup != prof.group) { consoleBox.addSectionHeading (tr (prof.group)); lastGroup = prof.group; }
            consoleBox.addItem (tr (prof.name), i + 1);
        }
        consoleBox.setSelectedId (sel > 0 ? sel : (int) processor.getConsole() + 1, juce::dontSendNotification);
    }
    for (auto [box, count, nameOf, id] : { std::make_tuple (&n64PresetBox, lotus::numN64FxPresets, &lotus::n64FxPresetName, ParamIDs::n64Preset),
                                           std::make_tuple (&ps1PresetBox, lotus::numPs1ReverbPresets, &lotus::ps1ReverbPresetName, ParamIDs::ps1Preset) })
    {
        box->clear (juce::dontSendNotification);
        for (int i = 0; i < count; ++i) box->addItem (tr (nameOf (i)), i + 1);
        box->setSelectedId ((int) param (id) + 1, juce::dontSendNotification);
    }

    refreshPresetList();
    refreshConsole();
    refreshTuneText();
    refreshCorrectionsStatus();
    showEffect (selectedEffect);
    tuningDialog.refreshTexts();
    content.repaint();
}

void LotusEditor::showLanguageMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Language"));
    menu.addItem (1, "English", true, language() == Language::English);
    menu.addItem (2, juce::String::fromUTF8 ("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"), true, language() == Language::Japanese);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&languageButton), [this] (int result)
    {
        if (result == 0) return;
        setLanguage (result == 2 ? Language::Japanese : Language::English);
        applyLanguage();
    });
}

// ---------------------------------------------------------------------------------
// Pages

void LotusEditor::showPage (int pg)
{
    page = pg;
    for (int i = 0; i < numPages; ++i)
        for (auto* c : pages[(size_t) i]) c->setVisible (i == pg);
    pageTabs.setSelected (pg);
    showEffect (selectedEffect);
}

void LotusEditor::showEffect (int effect)
{
    selectedEffect = effect;
    const bool fxPage = page == effectsPage;

    for (int i = 0; i < numEffects; ++i)
    {
        fxRows[(size_t) i]->setSelected (i == effect);
        fxSwitches[(size_t) i]->setVisible (fxPage && i == effect);
        for (auto* c : effectControls[(size_t) i]) c->setVisible (fxPage && i == effect);
    }

    const auto& info = effectInfo[effect];
    fxCard.setTitle (info.name);
    fxDesc.setText (tr (info.description), juce::dontSendNotification);
    fxControlLabel.setText (effect == snes ? tr ("FILTER") : (effect == led ? juce::String() : tr ("TYPE")), juce::dontSendNotification);
    fxControlLabel.setVisible (fxPage && effect != led);

    content.resized();
    updateEnablement();
}

// ---------------------------------------------------------------------------------
// Layout & painting

void LotusEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);
}

void LotusEditor::paintOverChildren (juce::Graphics& g)
{
    if (! dragHover) return;
    const auto r = getLocalBounds().toFloat().reduced (8.0f);
    g.setColour (Palette::background.withAlpha (0.82f));
    g.fillRoundedRectangle (r, 16.0f);
    g.setColour (Palette::accent);
    const float dash[] = { 8.0f, 6.0f };
    juce::Path border, dashed;
    border.addRoundedRectangle (r, 16.0f);
    juce::PathStrokeType (2.0f).createDashedStroke (dashed, border, dash, 2);
    g.fillPath (dashed);
    g.setFont (font (20.0f, true));
    g.drawText (tr ("Drop to load SoundFont"), getLocalBounds(), juce::Justification::centred);
}

void LotusEditor::resized()
{
    const float scale = (float) getWidth() / (float) baseWidth;
    content.setTransform (juce::AffineTransform::scale (scale));
    content.setBounds (0, 0, baseWidth, baseHeight);
}

void LotusEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);

    const auto glowCentre = editor.logo.getBounds().getCentre().toFloat();
    g.setGradientFill (juce::ColourGradient (Palette::accent.withAlpha (0.10f), glowCentre,
                                             Palette::accent.withAlpha (0.0f), glowCentre.translated (220.0f, 0.0f), true));
    g.fillRect (getLocalBounds());

    const auto logoB = editor.logo.getBounds();
    g.setColour (Palette::text);
    g.setFont (font (26.0f, true));
    g.drawText ("Lotus", logoB.getRight() + 10, logoB.getY() + 1, 200, 26, juce::Justification::centredLeft);
    g.setColour (Palette::textDim);
    g.setFont (font (12.5f));
    g.drawText (tr ("SoundFont player for video game music"), logoB.getRight() + 11, logoB.getY() + 27,
                editor.languageButton.getX() - logoB.getRight() - 16, 16, juce::Justification::centredLeft);
}

void LotusEditor::Content::resized()
{
    auto& e = editor;
    e.verdict.setBounds (getLocalBounds());
    e.tuningDialog.setBounds (getLocalBounds());
    e.colourPanel.setBounds (getLocalBounds());
    auto r = getLocalBounds().reduced (20, 16);

    auto header = r.removeFromTop (46);
    e.logo.setBounds (header.removeFromLeft (46));
    e.loadButton.setBounds (header.removeFromRight (120).withSizeKeepingCentre (120, 34));
    header.removeFromRight (14);
    e.pageTabs.setBounds (header.removeFromRight (252).withSizeKeepingCentre (252, 34));
    header.removeFromRight (8);
    e.languageButton.setBounds (header.removeFromRight (34).withSizeKeepingCentre (34, 34));
    r.removeFromTop (12);

    auto bar = r.removeFromTop (54);
    e.presetCard.setBounds (bar);
    auto barIn = bar.reduced (10, 10);
    e.prevButton.setBounds (barIn.removeFromLeft (34));
    barIn.removeFromLeft (6);
    e.infoLabel.setBounds (barIn.removeFromRight (190));
    barIn.removeFromRight (8);
    e.coloursButton.setBounds (barIn.removeFromRight (34));
    barIn.removeFromRight (6);
    e.nextButton.setBounds (barIn.removeFromRight (34));
    barIn.removeFromRight (6);
    e.presetBox.setBounds (barIn);
    r.removeFromTop (12);

    const auto inside = [] (const Card& c) { return c.getContentBounds().translated (c.getX(), c.getY()); };

    // --- Sound page ---------------------------------------------------------------
    {
        auto area = r;
        auto footer = area.removeFromBottom (124);
        area.removeFromBottom (12);

        e.envCard.setBounds (area);
        auto envIn = inside (e.envCard);
        auto knobs = envIn.removeFromRight (360);
        envIn.removeFromRight (16);
        e.envelopeView.setBounds (envIn);
        const int kw = knobs.getWidth() / 4;
        for (auto* k : { &e.attackKnob, &e.decayKnob, &e.sustainKnob, &e.releaseKnob })
            k->setBounds (knobs.removeFromLeft (kw).withSizeKeepingCentre (kw, juce::jmin (knobs.getHeight(), 104)));

        const int gap = 12;
        auto consoleR = footer.removeFromLeft (250); footer.removeFromLeft (gap);
        auto playR    = footer.removeFromLeft (216); footer.removeFromLeft (gap);
        auto outR     = footer;

        e.consoleCard.setBounds (consoleR);
        e.songMeter.setBounds (consoleR.getRight() - 84, consoleR.getY() + 7, 72, 20);
        auto cIn = inside (e.consoleCard);
        e.consoleBox.setBounds (cIn.removeFromTop (30));
        cIn.removeFromTop (12);
        auto chips = cIn.removeFromTop (26);
        e.formatChip.setBounds (chips.removeFromLeft (chips.getWidth() / 2 - 3));
        chips.removeFromLeft (6);
        e.voicesChip.setBounds (chips);

        e.playCard.setBounds (playR);
        auto pIn = inside (e.playCard);
        auto row1 = pIn.removeFromTop (30);
        e.noteOffLabel.setBounds (row1.removeFromLeft (76));
        e.noteOffSwitch.setBounds (row1);
        pIn.removeFromTop (8);
        auto row2 = pIn.removeFromTop (30);
        e.envSourceLabel.setBounds (row2.removeFromLeft (76));
        e.envSourceSwitch.setBounds (row2);

        e.outputCard.setBounds (outR);
        auto oIn = outR.reduced (8, 10);
        const int ow = oIn.getWidth() / 3;
        for (auto* k : { &e.volumeKnob, &e.bendKnob, &e.glideKnob })
            k->setBounds (oIn.removeFromLeft (ow));
    }

    // --- Pitch page ---------------------------------------------------------------
    {
        auto area = r;
        auto bottom = area.removeFromBottom (124);
        area.removeFromBottom (12);

        e.vibCard.setBounds (area);
        auto vIn = inside (e.vibCard);
        auto right = vIn.removeFromRight (400);
        vIn.removeFromRight (16);
        e.vibratoView.setBounds (vIn);
        e.vibShapeSwitch.setBounds (right.removeFromTop (28).withSizeKeepingCentre (300, 28));
        right.removeFromTop (6);
        const int kw = right.getWidth() / 4;
        for (auto* k : { &e.vibAmountKnob, &e.vibSpeedKnob, &e.vibDelayKnob, &e.vibAttackKnob })
            k->setBounds (right.removeFromLeft (kw).withSizeKeepingCentre (kw, juce::jmin (right.getHeight(), 104)));

        auto tuneR = bottom.removeFromLeft (300);
        bottom.removeFromLeft (12);
        e.tuneCard.setBounds (tuneR);
        auto tIn = inside (e.tuneCard);
        e.tuneKnob.setBounds (tIn.removeFromLeft (120).withTrimmedTop (-2).withTrimmedBottom (-6));
        tIn.removeFromLeft (8);
        e.tuneUnitSwitch.setBounds (tIn.removeFromTop (28));
        tIn.removeFromTop (8);
        e.tuneHint.setBounds (tIn.withTrimmedLeft (-3));

        e.sfTuneCard.setBounds (bottom);
        auto sIn = inside (e.sfTuneCard);
        auto buttons = sIn.removeFromBottom (28);
        e.checkTuningButton.setBounds (buttons.removeFromLeft (150));
        buttons.removeFromLeft (8);
        e.clearFixesButton.setBounds (buttons.removeFromLeft (110));
        e.sfTuneStatus.setBounds (sIn.removeFromBottom (18).withTrimmedLeft (-3));
        e.sfTuneText.setBounds (sIn.withTrimmedLeft (-3));
    }

    // --- Effects page -------------------------------------------------------------
    {
        auto area = r;
        auto listR = area.removeFromLeft (200);
        area.removeFromLeft (12);

        e.fxListCard.setBounds (listR);
        auto lIn = inside (e.fxListCard).withTrimmedTop (4);
        lIn.reduce (-4, 0);
        for (auto& row : e.fxRows)
        {
            row->setBounds (lIn.removeFromTop (54));
            lIn.removeFromTop (4);
        }

        e.fxCard.setBounds (area);
        for (auto& sw : e.fxSwitches)
            sw->setBounds (area.getRight() - 58, area.getY() + 10, 44, 22);

        auto dIn = area.reduced (14, 0).withTrimmedTop (40).withTrimmedBottom (12);
        e.fxDesc.setBounds (dIn.removeFromTop (36).withTrimmedLeft (-3));
        e.fxHint.setBounds (dIn.removeFromBottom (32).withTrimmedLeft (-3));
        dIn.removeFromTop (10);
        e.fxControlLabel.setBounds (dIn.removeFromTop (16).withTrimmedLeft (-3));
        dIn.removeFromTop (4);

        auto control = dIn.removeFromTop (32);
        e.snesFilter.setBounds (control);
        e.n64PresetBox.setBounds (control.withWidth (juce::jmin (260, control.getWidth())));
        e.ps1PresetBox.setBounds (e.n64PresetBox.getBounds());

        dIn.removeFromTop (8);
        const int kh = juce::jmin (dIn.getHeight(), 112);
        const int w = dIn.getWidth() / 3;
        auto knobRow = dIn;
        for (auto* k : { &e.snesDelayKnob, &e.snesFeedbackKnob, &e.snesVolumeKnob })
            k->setBounds (knobRow.removeFromLeft (w).withSizeKeepingCentre (w, kh));
        e.n64MixKnob.setBounds (dIn.removeFromLeft (w).withSizeKeepingCentre (w, kh));
        e.ps1DepthKnob.setBounds (e.n64MixKnob.getBounds());
    }
}

// ---------------------------------------------------------------------------------
// State sync

void LotusEditor::timerCallback()
{
    if (languageGeneration() != seenLanguage)
        applyLanguage();

    coloursButton.setLit (processor.hasColourPresets());

    if (processor.soundFontGeneration.load() != seenGeneration)
    {
        refreshPresetList();
        refreshCorrectionsStatus();
    }

    if (processor.consumePresetChangedByMidi() || processor.getPresetIndex() != seenPreset)
    {
        seenPreset = processor.getPresetIndex();
        if (processor.getSoundFont() != nullptr)
            presetBox.setSelectedId (seenPreset + 1, juce::dontSendNotification);
    }

    const int voices = processor.getActiveVoiceCount();
    if (voices != seenVoices)
    {
        seenVoices = voices;
        refreshInfo();
    }

    if ((int) processor.getConsole() != seenConsole || processor.isEncoding() != seenEncoding)
        refreshConsole();

    if (processor.getStoredCorrectionCount() != seenFixes)
        refreshCorrectionsStatus();

    for (int i = 0; i < numEffects; ++i)
        fxRows[(size_t) i]->setActive (param (effectInfo[i].onParam) > 0.5f);

    updateSongMeter();
    tuningDialog.sync();

    EnvelopeView::State s;
    s.attack  = param (ParamIDs::attack);
    s.decay   = param (ParamIDs::decay);
    s.sustain = param (ParamIDs::sustain) / 100.0f;
    s.release = param (ParamIDs::release);
    s.cut     = param (ParamIDs::noteOff) > 0.5f;
    s.sf2     = param (ParamIDs::envSource) > 0.5f;
    envelopeView.setState (s);

    VibratoView::State v;
    v.amount = param (ParamIDs::vibAmount);
    v.speed  = param (ParamIDs::vibSpeed);
    v.delay  = param (ParamIDs::vibDelay);
    v.attack = param (ParamIDs::vibAttack);
    v.shape  = (int) param (ParamIDs::vibShape);
    vibratoView.setState (v);
}

void LotusEditor::refreshTuneText()
{
    const bool hz = tuneInHz();
    auto& slider = tuneKnob.getSlider();
    slider.textFromValueFunction = [hz] (double v) { return formatTuning ((float) v, hz); };
    slider.valueFromTextFunction = [hz] (const juce::String& t) { return (double) parseTuning (t, hz); };
    slider.updateText();
}

void LotusEditor::refreshCorrectionsStatus()
{
    seenFixes = processor.getStoredCorrectionCount();
    const int n = seenFixes;
    sfTuneStatus.setText (n == 0 ? juce::String()
                                 : trf (n == 1 ? "1 preset is adjusted in this project. The .sf2 file is unchanged."
                                               : "{n} presets are adjusted in this project. The .sf2 file is unchanged.",
                                        { { "n", juce::String (n) } }),
                          juce::dontSendNotification);
    clearFixesButton.setVisible (page == pitchPage && n > 0);
    checkTuningButton.setEnabled (processor.getSoundFont() != nullptr);
}

void LotusEditor::updateSongMeter()
{
    const auto console = processor.getConsole();
    const auto& prof = lotus::getConsoleProfile (console);
    auto& reg = lotus::VoiceRegistry::get();
    const int now = reg.currentTotal (console);
    const double t = juce::Time::getMillisecondCounterHiRes();
    const auto inUse = reg.consolesInUse();
    const bool mismatch = std::count (inUse.begin(), inUse.end(), true) > 1;
    if (! mismatch && prof.voiceLimit > 0 && now > prof.voiceLimit)
        overUntilMs = t + 2000.0;   // keep red long enough to notice a brief overload
    songMeter.setState (now, prof.voiceLimit, ! mismatch && t < overUntilMs, mismatch);

    if (mismatch)
    {
        songMeter.setTooltip (tr ("Tracks in this project target different consoles, so limits can't be measured. Click for details."));
        return;
    }
    const int tracks = reg.instancesOn (console);
    songMeter.setTooltip (trf ("Voices playing right now across {count} set to {console}. Click to see how many consoles your song needs.",
                               { { "count", trf (tracks == 1 ? "{n} Lotus" : "{n} Lotuses", { { "n", juce::String (tracks) } }) },
                                 { "console", tr (prof.name) } }));
}

void LotusEditor::showVerdict()
{
    const auto inUse = lotus::VoiceRegistry::get().consolesInUse();
    juce::StringArray names;
    for (int i = 0; i < lotus::numConsoles; ++i)
        if (inUse[(size_t) i]) names.add (tr (lotus::getConsoleProfile ((lotus::ConsoleId) i).name));
    if (names.size() > 1)
    {
        verdict.showConfused (tr ("Console mismatch!"),
                              trf ("This project file uses {list}, so accuracy of limitations can not be measured.",
                                   { { "list", joinList (names) } }));
        return;
    }

    const auto console = processor.getConsole();
    const auto& prof = lotus::getConsoleProfile (console);
    auto& reg = lotus::VoiceRegistry::get();
    const int peak = reg.peakTotal (console);
    const int tracks = reg.instancesOn (console);
    const auto across = trf ("Peak: {voices} at once across {tracks}.", { { "voices", voicesText (peak) }, { "tracks", tracksText (tracks) } });
    auto takes = [&] (int n)
    {
        return trf ("This song takes {n} {consoles} to run.", { { "n", juce::String (n) }, { "consoles", consoleNoun (console, n != 1) } })
               + gap() + verdictQuip (n);
    };

    if (prof.voiceLimit <= 0)
    {
        verdict.show (1, takes (1),
                      (console == lotus::ConsoleId::N64 ? tr ("The N64 mixes in software, so there's no fixed voice count.")
                                                        : tr ("No fixed voice count here."))
                          + gap() + across);
        return;
    }

    if (peak == 0)
    {
        verdict.show (0, tr ("Play your song first!"),
                      trf ("Lotus counts the most voices sounding at once across every track set to {console}. Play it through, then click here again.",
                           { { "console", tr (prof.name) } }));
        return;
    }

    const int n = (peak + prof.voiceLimit - 1) / prof.voiceLimit;
    verdict.show (n, takes (n),
                  across + gap() + trf ("The {console} has {limit}.", { { "console", consoleNoun (console, false) }, { "limit", voicesText (prof.voiceLimit) } }));
}

void LotusEditor::refreshConsole()
{
    seenConsole = (int) processor.getConsole();
    seenEncoding = processor.isEncoding();
    const auto& prof = lotus::getConsoleProfile ((lotus::ConsoleId) seenConsole);

    consoleBox.setTooltip (tr (prof.description));

    const bool hasCodec = prof.codec != lotus::Codec::None;
    formatChip.setEnabled (hasCodec);
    formatChip.setText (! hasCodec ? juce::String (prof.sampleBits == 8 ? "8-bit PCM" : "16-bit PCM")
                                   : seenEncoding ? tr ("Converting...")
                                                  : trf ("{codec} samples", { { "codec", lotus::codecName (prof.codec) } }));

    voicesChip.setEnabled (prof.voiceLimit > 0);
    voicesChip.setText (prof.voiceLimit > 0 ? voicesText (prof.voiceLimit) : tr ("Any voices"));

    if (page == effectsPage) updateEnablement();
}

void LotusEditor::refreshPresetList()
{
    seenGeneration = processor.soundFontGeneration.load();
    presetBox.clear (juce::dontSendNotification);

    const auto* sf = processor.getSoundFont();
    if (sf == nullptr)
    {
        const auto missing = processor.getSoundFontFile();
        presetBox.setTextWhenNothingSelected (missing != juce::File()
            ? trf ("Missing: {file}  -  click Load SF2 to find it", { { "file", missing.getFileName() } })
            : tr ("Drop a .sf2 here, or click Load SF2"));
        presetBox.setEnabled (false);
        prevButton.setEnabled (false);
        nextButton.setEnabled (false);
        coloursButton.setEnabled (false);
        refreshInfo();
        return;
    }

    int lastBank = -1;
    const bool multipleBanks = ! sf->presets.empty() && sf->presets.front().bank != sf->presets.back().bank;
    for (size_t i = 0; i < sf->presets.size(); ++i)
    {
        const auto& pr = sf->presets[i];
        if (multipleBanks && pr.bank != lastBank)
        {
            presetBox.addSectionHeading (pr.bank == 128 ? tr ("Bank 128 (Drums)") : trf ("Bank {n}", { { "n", juce::String (pr.bank) } }));
            lastBank = pr.bank;
        }
        presetBox.addItem (juce::String (pr.bank).paddedLeft ('0', 3) + ":" + juce::String (pr.program).paddedLeft ('0', 3)
                               + "   " + juce::String (pr.name),
                           (int) i + 1);
    }

    presetBox.setEnabled (true);
    prevButton.setEnabled (true);
    nextButton.setEnabled (true);
    coloursButton.setEnabled (true);
    coloursButton.setLit (processor.hasColourPresets());
    seenPreset = processor.getPresetIndex();
    presetBox.setSelectedId (seenPreset + 1, juce::dontSendNotification);
    presetBox.setTooltip (processor.getSoundFontFile().getFullPathName());
    refreshInfo();
}

void LotusEditor::refreshInfo()
{
    const auto* sf = processor.getSoundFont();
    if (sf == nullptr)
    {
        infoLabel.setText (tr ("No SoundFont loaded"), juce::dontSendNotification);
        return;
    }

    juce::String name = juce::String (sf->name).trim();
    if (name.isEmpty()) name = processor.getSoundFontFile().getFileNameWithoutExtension();
    const int voices = juce::jmax (0, seenVoices);
    infoLabel.setText (name + "\n" + trf ("{n} presets", { { "n", juce::String (sf->presets.size()) } }) + "  |  " + voicesText (voices),
                       juce::dontSendNotification);
    infoLabel.setColour (juce::Label::textColourId, voices > 0 ? Palette::leaf : Palette::textDim);
}

void LotusEditor::updateEnablement()
{
    const bool sf2Env = envSourceSwitch.getSelected() == 1;
    const bool cut = noteOffSwitch.getSelected() == 1;
    attackKnob.setEnabled (! sf2Env);
    decayKnob.setEnabled (! sf2Env);
    sustainKnob.setEnabled (! sf2Env);
    releaseKnob.setEnabled (! sf2Env && ! cut);

    const bool on = fxSwitches[(size_t) selectedEffect]->isOn();
    for (auto* c : effectControls[(size_t) selectedEffect])
        c->setAlpha (on ? 1.0f : 0.45f);
    fxControlLabel.setAlpha (on ? 1.0f : 0.45f);

    const auto& info = effectInfo[selectedEffect];
    const auto console = processor.getConsole();
    const juce::String consoleName = tr (lotus::getConsoleProfile (console).name);
    juce::String hint;
    if (selectedEffect == led)
        hint = (console == lotus::ConsoleId::Amiga500 || console == lotus::ConsoleId::Amiga1200)
                 ? trf ("On top of the {console}'s own output filtering.", { { "console", consoleName } })
                 : tr ("Works on any console; it's applied to the final output.");
    else if (console == info.home)
        hint = trf ("Console is {console}: running at the hardware's own rate.", { { "console", consoleName } });
    else
        hint = trf ("Console is {console}. Times are kept; set Console to {home} for the authentic sound.",
                    { { "console", consoleName }, { "home", tr (lotus::getConsoleProfile (info.home).name) } });
    fxHint.setText (hint, juce::dontSendNotification);

    clearFixesButton.setVisible (page == pitchPage && processor.getStoredCorrectionCount() > 0);
}

// ---------------------------------------------------------------------------------
// Loading and saving

void LotusEditor::stepPreset (int delta)
{
    const auto* sf = processor.getSoundFont();
    if (sf == nullptr || sf->presets.empty()) return;
    const int n = (int) sf->presets.size();
    const int idx = ((processor.getPresetIndex() + delta) % n + n) % n;
    processor.selectPreset (idx);
    presetBox.setSelectedId (idx + 1, juce::dontSendNotification);
    seenPreset = idx;
}

void LotusEditor::openFileChooser()
{
    auto start = processor.getSoundFontFile();
    if (start == juce::File() || ! start.getParentDirectory().isDirectory())
        start = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

    chooser = std::make_unique<juce::FileChooser> (tr ("Load a SoundFont"), start, "*.sf2");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) loadFile (f);
                          });
}

void LotusEditor::saveAsChooser()
{
    chooser = std::make_unique<juce::FileChooser> (tr ("Save fixed SoundFont as"), processor.getFixedSaveTarget(), "*.sf2");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              if (! f.hasFileExtension ("sf2")) f = f.withFileExtension ("sf2");
                              processor.saveTuningFixes (LotusProcessor::SaveChoice::SaveAs, f);
                          });
}

void LotusEditor::loadFile (const juce::File& f)
{
    const auto err = processor.loadSoundFont (f, true);
    if (err.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                trf ("Couldn't load {file}", { { "file", f.getFileName() } }), tr (err));
    refreshPresetList();
}

bool LotusEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".sf2")) return true;
    return false;
}

void LotusEditor::fileDragEnter (const juce::StringArray&, int, int) { dragHover = true; repaint(); }
void LotusEditor::fileDragExit (const juce::StringArray&) { dragHover = false; repaint(); }

void LotusEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    repaint();
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".sf2")) { loadFile (juce::File (f)); break; }
}
