#include "PluginEditor.h"

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

// Singular and plural names for the verdict popup.
juce::String consoleNoun (lotus::ConsoleId id, bool plural)
{
    using C = lotus::ConsoleId;
    switch (id)
    {
        case C::Nes:        return plural ? "NESes" : "NES";
        case C::Snes:       return plural ? "SNESes" : "SNES";
        case C::N64:        return plural ? "N64s" : "N64";
        case C::Gba:        return plural ? "GBAs" : "GBA";
        case C::NintendoDs: return plural ? "Nintendo DSes" : "Nintendo DS";
        case C::Ps1:        return plural ? "PS1s" : "PS1";
        case C::Ps2:        return plural ? "PS2s" : "PS2";
        case C::MegaDrive:  return plural ? "Mega Drives" : "Mega Drive";
        case C::Saturn:     return plural ? "Saturns" : "Saturn";
        case C::Amiga500:   return plural ? "Amiga 500s" : "Amiga 500";
        case C::Amiga1200:  return plural ? "Amiga 1200s" : "Amiga 1200";
        case C::Modern: case C::Linear: case C::Raw: case C::count: break;
    }
    return plural ? "computers" : "computer";
}

juce::String verdictQuip (int n)
{
    if (n == 1) return "Great job!";
    if (n == 2) return "You got a spare, right?";
    if (n == 3) return "Time to raid the attic.";
    if (n == 4) return "Is this a LAN party?";
    if (n <= 7) return "That's a shelf, not a console.";
    return "You're running a museum now.";
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
      snesFilter (p.params, ParamIDs::snesFilter, { "Flat", "Low", "High", "Band" }),
      snesDelayKnob    (p.params, ParamIDs::snesDelay,    "Delay"),
      snesFeedbackKnob (p.params, ParamIDs::snesFeedback, "Feedback"),
      snesVolumeKnob   (p.params, ParamIDs::snesVolume,   "Volume"),
      n64MixKnob   (p.params, ParamIDs::n64Mix,   "Mix"),
      ps1DepthKnob (p.params, ParamIDs::ps1Depth, "Depth")
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    // Header and preset bar
    content.addAndMakeVisible (logo);
    content.addAndMakeVisible (pageTabs);
    content.addAndMakeVisible (loadButton);
    pageTabs.onChange = [this] (int pg) { showPage (pg); };
    loadButton.onClick = [this] { openFileChooser(); };
    loadButton.setTooltip ("Load a SoundFont (.sf2). You can also drag one onto the window.");

    content.addAndMakeVisible (presetCard);
    for (auto* c : std::initializer_list<juce::Component*> { &prevButton, &presetBox, &nextButton, &infoLabel })
        content.addAndMakeVisible (c);
    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (+1); };
    prevButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");
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
    soundPage = { &envCard, &envelopeView, &attackKnob, &decayKnob, &sustainKnob, &releaseKnob,
                  &consoleCard, &consoleBox, &formatChip, &voicesChip, &songMeter,
                  &playCard, &noteOffLabel, &noteOffSwitch, &envSourceLabel, &envSourceSwitch,
                  &outputCard, &volumeKnob, &bendKnob, &glideKnob };

    attackKnob.getSlider().setTooltip ("Fade-in time. 0 = instant.");
    decayKnob.getSlider().setTooltip ("Time to fall from full level to the sustain level.");
    sustainKnob.getSlider().setTooltip ("Level held while the key is down.");
    releaseKnob.getSlider().setTooltip ("Time from note-off to silence. Exact: 0 stops dead.");

    juce::String lastGroup;
    for (int i = 0; i < lotus::numConsoles; ++i)
    {
        const auto& prof = lotus::getConsoleProfile ((lotus::ConsoleId) i);
        if (lastGroup != prof.group)
        {
            consoleBox.addSectionHeading (prof.group);
            lastGroup = prof.group;
        }
        consoleBox.addItem (prof.name, i + 1);
    }
    consoleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.params, ParamIDs::console, consoleBox);
    consoleBox.onChange = [this] { refreshConsole(); };
    formatChip.setTooltip ("Play the samples as they'd sound after conversion to the console's own format "
                           "(BRR, ADPCM, DPCM). Converting takes a moment after switching.");
    songMeter.onClick = [this] { showVerdict(); };
    verdict.onReset = [this] { lotus::VoiceRegistry::get().resetPeak (processor.getConsole()); };
    voicesChip.setTooltip ("Limit polyphony to the console's hardware voices. Extra notes steal the oldest, like the real thing.");

    for (auto* l : { &noteOffLabel, &envSourceLabel }) styleSmallLabel (*l, 12.5f);
    noteOffLabel.setText ("Note off", juce::dontSendNotification);
    envSourceLabel.setText ("Envelope", juce::dontSendNotification);
    noteOffLabel.setJustificationType (juce::Justification::centredLeft);
    envSourceLabel.setJustificationType (juce::Justification::centredLeft);
    noteOffSwitch.setTooltip ("Release: notes fade out over the release time.\nCut: notes stop the moment the key is let go.");
    envSourceSwitch.setTooltip ("Lotus: use the ADSR knobs.\nSF2: use the envelopes stored in the SoundFont.");
    noteOffSwitch.onChange = [this] (int) { updateEnablement(); };
    envSourceSwitch.onChange = [this] (int) { updateEnablement(); };

    bendKnob.getSlider().setTooltip ("Pitch bend range in semitones.");
    glideKnob.getSlider().setTooltip ("Slide from the previous note's pitch. 0 = off.");

    for (auto* c : soundPage) content.addChildComponent (c);

    // Effects page
    for (int i = 0; i < numEffects; ++i)
    {
        fxRows[(size_t) i] = std::make_unique<EffectRow> (effectInfo[i].name, effectInfo[i].console);
        fxRows[(size_t) i]->onClick = [this, i] { showEffect (i); };
        fxSwitches[(size_t) i] = std::make_unique<Switch> (p.params, effectInfo[i].onParam);
        fxSwitches[(size_t) i]->setTooltip (juce::String ("Turn the ") + effectInfo[i].name + " on or off.");
        fxSwitches[(size_t) i]->onChange = [this] (bool) { updateEnablement(); };
    }

    styleSmallLabel (fxDesc, 12.5f);
    styleSmallLabel (fxControlLabel, 11.0f, true);
    styleSmallLabel (fxHint, 12.0f);
    fxHint.setColour (juce::Label::textColourId, Palette::leaf);

    for (int i = 0; i < lotus::numN64FxPresets; ++i) n64PresetBox.addItem (lotus::n64FxPresetName (i), i + 1);
    for (int i = 0; i < lotus::numPs1ReverbPresets; ++i) ps1PresetBox.addItem (lotus::ps1ReverbPresetName (i), i + 1);
    n64PresetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.params, ParamIDs::n64Preset, n64PresetBox);
    ps1PresetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.params, ParamIDs::ps1Preset, ps1PresetBox);

    snesFilter.setTooltip ("Echo FIR filter: the four presets from Nintendo's standard SNES sound driver.");
    snesDelayKnob.getSlider().setTooltip ("EDL: echo delay in 16 ms steps.");
    snesFeedbackKnob.getSlider().setTooltip ("EFB: feedback. Negative values flip the phase of each repeat.");
    snesVolumeKnob.getSlider().setTooltip ("EVOL: echo return level.");
    n64MixKnob.getSlider().setTooltip ("Effect send level.");
    ps1DepthKnob.getSlider().setTooltip ("Reverb return level.");

    effectControls[snes] = { &snesFilter, &snesDelayKnob, &snesFeedbackKnob, &snesVolumeKnob };
    effectControls[n64] = { &n64PresetBox, &n64MixKnob };
    effectControls[ps1] = { &ps1PresetBox, &ps1DepthKnob };
    effectControls[led] = {};

    effectsPage = { &fxListCard, &fxCard, &fxDesc, &fxControlLabel, &fxHint };
    for (auto& r : fxRows) effectsPage.push_back (r.get());
    for (auto* c : effectsPage) content.addChildComponent (c);
    for (auto& sw : fxSwitches) content.addChildComponent (sw.get());
    for (auto& list : effectControls)
        for (auto* c : list) content.addChildComponent (c);

    content.addChildComponent (verdict);

    setResizable (true, true);
    setResizeLimits (baseWidth * 3 / 4, baseHeight * 3 / 4, baseWidth * 2, baseHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setSize (baseWidth, baseHeight);

    sendLookAndFeelChange();
    refreshPresetList();
    refreshConsole();
    updateEnablement();
    showPage (0);
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

void LotusEditor::showPage (int pg)
{
    page = pg;
    for (auto* c : soundPage) c->setVisible (pg == 0);
    for (auto* c : effectsPage) c->setVisible (pg == 1);
    pageTabs.setSelected (pg);
    showEffect (selectedEffect);
}

void LotusEditor::showEffect (int effect)
{
    selectedEffect = effect;
    const bool fxPage = page == 1;

    for (int i = 0; i < numEffects; ++i)
    {
        fxRows[(size_t) i]->setSelected (i == effect);
        fxSwitches[(size_t) i]->setVisible (fxPage && i == effect);
        for (auto* c : effectControls[(size_t) i]) c->setVisible (fxPage && i == effect);
    }

    const auto& info = effectInfo[effect];
    fxCard.setTitle (info.name);
    fxDesc.setText (info.description, juce::dontSendNotification);
    fxControlLabel.setText (effect == snes ? "FILTER" : (effect == led ? "" : "TYPE"), juce::dontSendNotification);
    fxControlLabel.setVisible (fxPage && effect != led);

    content.resized();
    updateEnablement();
}

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
    g.drawText ("Drop to load SoundFont", getLocalBounds(), juce::Justification::centred);
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
    g.drawText ("SoundFont player for video game music", logoB.getRight() + 11, logoB.getY() + 27, 300, 16,
                juce::Justification::centredLeft);
}

void LotusEditor::Content::resized()
{
    auto& e = editor;
    e.verdict.setBounds (getLocalBounds());
    auto r = getLocalBounds().reduced (20, 16);

    auto header = r.removeFromTop (46);
    e.logo.setBounds (header.removeFromLeft (46));
    e.loadButton.setBounds (header.removeFromRight (120).withSizeKeepingCentre (120, 34));
    header.removeFromRight (16);
    e.pageTabs.setBounds (header.removeFromRight (190).withSizeKeepingCentre (190, 34));
    r.removeFromTop (12);

    auto bar = r.removeFromTop (54);
    e.presetCard.setBounds (bar);
    auto barIn = bar.reduced (10, 10);
    e.prevButton.setBounds (barIn.removeFromLeft (34));
    barIn.removeFromLeft (6);
    e.infoLabel.setBounds (barIn.removeFromRight (180));
    barIn.removeFromRight (8);
    e.nextButton.setBounds (barIn.removeFromRight (34));
    barIn.removeFromRight (6);
    e.presetBox.setBounds (barIn);
    r.removeFromTop (12);

    const auto inside = [] (const Card& c) { return c.getContentBounds().translated (c.getX(), c.getY()); };

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
        e.noteOffLabel.setBounds (row1.removeFromLeft (62));
        e.noteOffSwitch.setBounds (row1);
        pIn.removeFromTop (8);
        auto row2 = pIn.removeFromTop (30);
        e.envSourceLabel.setBounds (row2.removeFromLeft (62));
        e.envSourceSwitch.setBounds (row2);

        e.outputCard.setBounds (outR);
        auto oIn = outR.reduced (8, 10);
        const int ow = oIn.getWidth() / 3;
        for (auto* k : { &e.volumeKnob, &e.bendKnob, &e.glideKnob })
            k->setBounds (oIn.removeFromLeft (ow));
    }

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

void LotusEditor::timerCallback()
{
    if (processor.soundFontGeneration.load() != seenGeneration)
        refreshPresetList();

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

    for (int i = 0; i < numEffects; ++i)
        fxRows[(size_t) i]->setActive (param (effectInfo[i].onParam) > 0.5f);

    updateSongMeter();

    EnvelopeView::State s;
    s.attack  = param (ParamIDs::attack);
    s.decay   = param (ParamIDs::decay);
    s.sustain = param (ParamIDs::sustain) / 100.0f;
    s.release = param (ParamIDs::release);
    s.cut     = param (ParamIDs::noteOff) > 0.5f;
    s.sf2     = param (ParamIDs::envSource) > 0.5f;
    envelopeView.setState (s);
}

void LotusEditor::updateSongMeter()
{
    const auto console = processor.getConsole();
    const auto& prof = lotus::getConsoleProfile (console);
    auto& reg = lotus::VoiceRegistry::get();
    const int now = reg.currentTotal (console);
    const double t = juce::Time::getMillisecondCounterHiRes();
    if (prof.voiceLimit > 0 && now > prof.voiceLimit)
        overUntilMs = t + 2000.0;   // keep red long enough to notice a brief overload
    songMeter.setState (now, prof.voiceLimit, t < overUntilMs);

    const int tracks = reg.instancesOn (console);
    songMeter.setTooltip ("Voices playing right now across " + juce::String (tracks)
                          + (tracks == 1 ? " Lotus" : " Lotuses") + " set to " + prof.name
                          + ". Click to see how many consoles your song needs.");
}

void LotusEditor::showVerdict()
{
    const auto console = processor.getConsole();
    const auto& prof = lotus::getConsoleProfile (console);
    auto& reg = lotus::VoiceRegistry::get();
    const int peak = reg.peakTotal (console);
    const int tracks = reg.instancesOn (console);
    const juce::String across = juce::String (peak) + (peak == 1 ? " voice" : " voices") + " at once across "
                              + juce::String (tracks) + (tracks == 1 ? " track." : " tracks.");

    if (prof.voiceLimit <= 0)
    {
        const bool isN64 = console == lotus::ConsoleId::N64;
        verdict.show (1, "This song takes 1 " + consoleNoun (console, false) + " to run. Great job!",
                      (isN64 ? juce::String ("The N64 mixes in software, so there's no fixed voice count. ")
                           : juce::String ("No fixed voice count here. "))
                          + "Peak: " + across);
        return;
    }

    if (peak == 0)
    {
        verdict.show (0, "Play your song first!",
                      "Lotus counts the most voices sounding at once across every track set to "
                          + juce::String (prof.name) + ". Play it through, then click here again.");
        return;
    }

    const int n = (peak + prof.voiceLimit - 1) / prof.voiceLimit;
    verdict.show (n, "This song takes " + juce::String (n) + " " + consoleNoun (console, n != 1) + " to run. " + verdictQuip (n),
                  "Peak: " + across + " The " + consoleNoun (console, false) + " has " + juce::String (prof.voiceLimit) + ".");
}

void LotusEditor::refreshConsole()
{
    seenConsole = (int) processor.getConsole();
    seenEncoding = processor.isEncoding();
    const auto& prof = lotus::getConsoleProfile ((lotus::ConsoleId) seenConsole);

    consoleBox.setTooltip (prof.description);

    const bool hasCodec = prof.codec != lotus::Codec::None;
    formatChip.setEnabled (hasCodec);
    formatChip.setText (! hasCodec ? juce::String (prof.sampleBits == 8 ? "8-bit PCM" : "16-bit PCM")
                                   : seenEncoding ? juce::String ("Converting...")
                                                  : juce::String (lotus::codecName (prof.codec)) + " samples");

    voicesChip.setEnabled (prof.voiceLimit > 0);
    voicesChip.setText (prof.voiceLimit > 0 ? juce::String (prof.voiceLimit) + (prof.voiceLimit == 1 ? " voice" : " voices")
                                            : juce::String ("Any voices"));

    if (page == 1) updateEnablement();
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
            ? "Missing: " + missing.getFileName() + "  -  click Load SF2 to find it"
            : "Drop a .sf2 here, or click Load SF2");
        presetBox.setEnabled (false);
        prevButton.setEnabled (false);
        nextButton.setEnabled (false);
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
            presetBox.addSectionHeading (pr.bank == 128 ? juce::String ("Bank 128 (Drums)") : "Bank " + juce::String (pr.bank));
            lastBank = pr.bank;
        }
        presetBox.addItem (juce::String (pr.bank).paddedLeft ('0', 3) + ":" + juce::String (pr.program).paddedLeft ('0', 3)
                               + "   " + juce::String (pr.name),
                           (int) i + 1);
    }

    presetBox.setEnabled (true);
    prevButton.setEnabled (true);
    nextButton.setEnabled (true);
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
        infoLabel.setText ("No SoundFont loaded", juce::dontSendNotification);
        return;
    }

    juce::String name = juce::String (sf->name).trim();
    if (name.isEmpty()) name = processor.getSoundFontFile().getFileNameWithoutExtension();
    const int voices = juce::jmax (0, seenVoices);
    infoLabel.setText (name + "\n" + juce::String (sf->presets.size()) + " presets  |  "
                           + juce::String (voices) + (voices == 1 ? " voice" : " voices"),
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
    const juce::String consoleName = lotus::getConsoleProfile (console).name;
    juce::String hint;
    if (selectedEffect == led)
        hint = (console == lotus::ConsoleId::Amiga500 || console == lotus::ConsoleId::Amiga1200)
                 ? "On top of the " + consoleName + "'s own output filtering."
                 : "Works on any console; it's applied to the final output.";
    else if (console == info.home)
        hint = "Console is " + consoleName + ": running at the hardware's own rate.";
    else
        hint = "Console is " + consoleName + ". Times are kept; set Console to "
             + lotus::getConsoleProfile (info.home).name + " for the authentic sound.";
    fxHint.setText (hint, juce::dontSendNotification);
}

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

    chooser = std::make_unique<juce::FileChooser> ("Load a SoundFont", start, "*.sf2");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) loadFile (f);
                          });
}

void LotusEditor::loadFile (const juce::File& f)
{
    const auto err = processor.loadSoundFont (f);
    if (err.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't load " + f.getFileName(), err);
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
