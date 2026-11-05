#include "PluginEditor.h"

using namespace lotus::ui;

LotusEditor::LotusEditor (LotusProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      attackKnob  (p.params, ParamIDs::attack,  "Attack"),
      decayKnob   (p.params, ParamIDs::decay,   "Decay"),
      sustainKnob (p.params, ParamIDs::sustain, "Sustain"),
      releaseKnob (p.params, ParamIDs::release, "Release"),
      noteOffSwitch   (p.params, ParamIDs::noteOff,   { "Release", "Cut" }),
      envSourceSwitch (p.params, ParamIDs::envSource, { "Lotus", "SoundFont" }),
      volumeKnob (p.params, ParamIDs::volume,    "Volume"),
      bendKnob   (p.params, ParamIDs::bendRange, "Bend"),
      snesSwitch (p.params, ParamIDs::snesOn),
      snesFilter (p.params, ParamIDs::snesFilter, { "Flat", "Low", "High", "Band" }),
      snesDelayKnob    (p.params, ParamIDs::snesDelay,    "Delay"),
      snesFeedbackKnob (p.params, ParamIDs::snesFeedback, "Feedback"),
      snesVolumeKnob   (p.params, ParamIDs::snesVolume,   "Volume"),
      ps1Switch (p.params, ParamIDs::ps1On),
      ps1DepthKnob (p.params, ParamIDs::ps1Depth, "Depth")
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    content.addAndMakeVisible (logo);
    content.addAndMakeVisible (loadButton);
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

    content.addAndMakeVisible (envCard);
    content.addAndMakeVisible (envelopeView);
    for (auto* k : { &attackKnob, &decayKnob, &sustainKnob, &releaseKnob })
        content.addAndMakeVisible (k);
    attackKnob.getSlider().setTooltip ("Attack: fade-in time. 0 = instant.");
    decayKnob.getSlider().setTooltip ("Decay: time to fall from full level to the sustain level.");
    sustainKnob.getSlider().setTooltip ("Sustain: level held while the key is down.");
    releaseKnob.getSlider().setTooltip ("Release: time from note-off to silence. Exact - 0 means it stops dead.");

    content.addAndMakeVisible (consoleCard);
    content.addAndMakeVisible (consoleBox);
    content.addAndMakeVisible (consoleDesc);
    for (int i = 0; i < lotus::numConsoles; ++i)
        consoleBox.addItem (lotus::getConsoleProfile ((lotus::ConsoleId) i).name, i + 1);
    consoleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.params, ParamIDs::console, consoleBox);
    consoleBox.onChange = [this] { refreshInfo(); };
    for (auto* l : { &consoleDesc, &noteOffDesc, &envSourceDesc })
    {
        l->setFont (font (11.5f));
        l->setColour (juce::Label::textColourId, Palette::textDim);
        l->setJustificationType (juce::Justification::topLeft);
        l->setMinimumHorizontalScale (1.0f);
        l->setInterceptsMouseClicks (false, false);
        content.addAndMakeVisible (l);
    }

    content.addAndMakeVisible (noteOffCard);
    content.addAndMakeVisible (noteOffSwitch);
    noteOffSwitch.setTooltip ("Release: notes fade out with the envelope's release.\nCut: notes stop the moment the key is let go.");
    noteOffSwitch.onChange = [this] (int) { updateEnablement(); };

    content.addAndMakeVisible (envSourceCard);
    content.addAndMakeVisible (envSourceSwitch);
    envSourceSwitch.setTooltip ("Lotus: use the ADSR knobs above.\nSoundFont: use the envelope stored in the .sf2 file.");
    envSourceSwitch.onChange = [this] (int) { updateEnablement(); };

    content.addAndMakeVisible (outputCard);
    content.addAndMakeVisible (volumeKnob);
    content.addAndMakeVisible (bendKnob);
    bendKnob.getSlider().setTooltip ("Pitch bend range in semitones.");

    // --- Effects page -----------------------------------------------------------
    content.addAndMakeVisible (pageTabs);
    pageTabs.setTooltip ("Switch between the instrument and the console effects.");
    pageTabs.onChange = [this] (int page) { showPage (page); };

    for (auto* c : std::initializer_list<juce::Component*> { &snesCard, &snesSwitch, &snesFilter, &snesDelayKnob,
                                                              &snesFeedbackKnob, &snesVolumeKnob, &ps1Card, &ps1Switch,
                                                              &ps1PresetBox, &ps1DepthKnob })
        content.addChildComponent (c);

    for (auto* l : { &snesDesc, &snesFilterLabel, &snesHint, &ps1Desc, &ps1TypeLabel, &ps1Hint })
    {
        l->setFont (font (12.0f));
        l->setColour (juce::Label::textColourId, Palette::textDim);
        l->setJustificationType (juce::Justification::topLeft);
        l->setMinimumHorizontalScale (1.0f);
        l->setInterceptsMouseClicks (false, false);
        content.addChildComponent (l);
    }
    snesDesc.setText ("The S-DSP echo unit: a delay of up to 240 ms, signed feedback, and the 8-tap FIR "
                      "filter that colours every repeat.", juce::dontSendNotification);
    snesFilterLabel.setText ("FILTER", juce::dontSendNotification);
    snesFilterLabel.setFont (font (11.0f, true).withExtraKerningFactor (0.12f));
    ps1TypeLabel.setText ("TYPE", juce::dontSendNotification);
    ps1TypeLabel.setFont (font (11.0f, true).withExtraKerningFactor (0.12f));
    ps1Desc.setText ("The SPU reverb with Sony's preset rooms, running at 22.05 kHz like the hardware.",
                     juce::dontSendNotification);
    for (auto* l : { &snesHint, &ps1Hint })
        l->setColour (juce::Label::textColourId, Palette::leaf);

    snesSwitch.setTooltip ("Turn the SNES echo on or off.");
    ps1Switch.setTooltip ("Turn the PS1 reverb on or off.");
    snesFilter.setTooltip ("The echo FIR filter, using the four presets from Nintendo's standard sound driver.");
    snesDelayKnob.getSlider().setTooltip ("EDL: echo delay in 16 ms steps (0-240 ms).");
    snesFeedbackKnob.getSlider().setTooltip ("EFB: how much of each repeat feeds back. Negative values flip the phase of each repeat.");
    snesVolumeKnob.getSlider().setTooltip ("EVOL: echo return level.");
    ps1DepthKnob.getSlider().setTooltip ("Reverb return level.");

    for (int i = 0; i < lotus::numPs1ReverbPresets; ++i)
        ps1PresetBox.addItem (lotus::ps1ReverbPresetName (i), i + 1);
    ps1PresetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.params, ParamIDs::ps1Preset, ps1PresetBox);

    snesSwitch.onChange = [this] (bool) { updateEnablement(); };
    ps1Switch.onChange = [this] (bool) { updateEnablement(); };

    soundPage = { &envCard, &envelopeView, &attackKnob, &decayKnob, &sustainKnob, &releaseKnob,
                  &consoleCard, &consoleBox, &consoleDesc, &noteOffCard, &noteOffSwitch, &noteOffDesc,
                  &envSourceCard, &envSourceSwitch, &envSourceDesc, &outputCard, &volumeKnob, &bendKnob };
    effectsPage = { &snesCard, &snesSwitch, &snesDesc, &snesFilterLabel, &snesFilter, &snesDelayKnob, &snesFeedbackKnob,
                    &snesVolumeKnob, &snesHint, &ps1Card, &ps1Switch, &ps1Desc, &ps1TypeLabel, &ps1PresetBox, &ps1DepthKnob, &ps1Hint };

    setResizable (true, true);
    setResizeLimits (baseWidth * 3 / 4, baseHeight * 3 / 4, baseWidth * 2, baseHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setSize (baseWidth, baseHeight);

    for (auto* l : { &consoleDesc, &noteOffDesc, &envSourceDesc })
        l->toFront (false);   // above their cards
    for (auto* c : effectsPage)
        if (c != &snesCard && c != &ps1Card) c->toFront (false);

    sendLookAndFeelChange();   // make sure children created before setLookAndFeel pick it up
    refreshPresetList();
    updateEnablement();
    showPage (0);
    startTimerHz (30);
}

void LotusEditor::showPage (int page)
{
    for (auto* c : soundPage) c->setVisible (page == 0);
    for (auto* c : effectsPage) c->setVisible (page == 1);
    pageTabs.setSelected (page);
    updateEffectHints();
}

void LotusEditor::updateEffectHints()
{
    const int console = (int) processor.params.getRawParameterValue (ParamIDs::console)->load();
    seenConsole = console;
    const auto name = juce::String (lotus::getConsoleProfile ((lotus::ConsoleId) console).name);
    snesHint.setText (console == (int) lotus::ConsoleId::Snes
                          ? "Console is SNES: sample-exact echo."
                          : "Console is " + name + ". Delay times are kept, but set Console to SNES for the exact filter sound.",
                      juce::dontSendNotification);
    ps1Hint.setText (console == (int) lotus::ConsoleId::Ps1
                         ? "Console is PS1: sample-exact reverb."
                         : "Console is " + name + ". Times are kept; set Console to PS1 for the exact sound.",
                     juce::dontSendNotification);
}

LotusEditor::~LotusEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
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
    juce::Path border;
    border.addRoundedRectangle (r, 16.0f);
    juce::Path dashed;
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

    // Soft glow behind the logo
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
    g.drawText ("SoundFont player for video game music", logoB.getRight() + 11, logoB.getY() + 27, 320, 16,
                juce::Justification::centredLeft);
}

void LotusEditor::Content::resized()
{
    auto& e = editor;
    auto r = getLocalBounds().reduced (20, 16);

    // Header
    auto header = r.removeFromTop (46);
    e.logo.setBounds (header.removeFromLeft (46));
    e.loadButton.setBounds (header.removeFromRight (120).withSizeKeepingCentre (120, 34));
    header.removeFromRight (16);
    e.pageTabs.setBounds (header.removeFromRight (190).withSizeKeepingCentre (190, 34));
    r.removeFromTop (12);

    // Preset bar
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

    // Effects page uses the same area as the envelope + footer
    {
        auto fx = r;
        auto snesR = fx.removeFromLeft (430);
        fx.removeFromLeft (12);
        auto ps1R = fx;

        e.snesCard.setBounds (snesR);
        e.snesSwitch.setBounds (snesR.getRight() - 58, snesR.getY() + 10, 44, 22);
        auto sIn = snesR.reduced (14, 0).withTrimmedTop (40).withTrimmedBottom (12);
        e.snesDesc.setBounds (sIn.removeFromTop (34).withTrimmedLeft (-3));
        sIn.removeFromTop (8);
        e.snesFilterLabel.setBounds (sIn.removeFromTop (16).withTrimmedLeft (-3));
        sIn.removeFromTop (4);
        e.snesFilter.setBounds (sIn.removeFromTop (32));
        e.snesHint.setBounds (sIn.removeFromBottom (32).withTrimmedLeft (-3));
        sIn.removeFromTop (10);
        const int w = sIn.getWidth() / 3;
        for (auto* k : { &e.snesDelayKnob, &e.snesFeedbackKnob, &e.snesVolumeKnob })
            k->setBounds (sIn.removeFromLeft (w).withSizeKeepingCentre (w, juce::jmin (sIn.getHeight(), 116)));

        e.ps1Card.setBounds (ps1R);
        e.ps1Switch.setBounds (ps1R.getRight() - 58, ps1R.getY() + 10, 44, 22);
        auto pIn = ps1R.reduced (14, 0).withTrimmedTop (40).withTrimmedBottom (12);
        e.ps1Desc.setBounds (pIn.removeFromTop (34).withTrimmedLeft (-3));
        pIn.removeFromTop (8);
        e.ps1TypeLabel.setBounds (pIn.removeFromTop (16).withTrimmedLeft (-3));
        pIn.removeFromTop (4);
        e.ps1PresetBox.setBounds (pIn.removeFromTop (32));
        e.ps1Hint.setBounds (pIn.removeFromBottom (32).withTrimmedLeft (-3));
        pIn.removeFromTop (10);
        e.ps1DepthKnob.setBounds (pIn.withSizeKeepingCentre (120, juce::jmin (pIn.getHeight(), 116)));
    }

    // Footer
    auto footer = r.removeFromBottom (112);
    r.removeFromBottom (12);

    // Envelope
    e.envCard.setBounds (r);
    auto envIn = e.envCard.getContentBounds().translated (r.getX(), r.getY());
    auto knobs = envIn.removeFromRight (360);
    envIn.removeFromRight (16);
    e.envelopeView.setBounds (envIn);
    const int kw = knobs.getWidth() / 4;
    for (auto* k : { &e.attackKnob, &e.decayKnob, &e.sustainKnob, &e.releaseKnob })
        k->setBounds (knobs.removeFromLeft (kw).withSizeKeepingCentre (kw, juce::jmin (knobs.getHeight(), 110)));

    // Footer cards
    const int gap = 12;
    auto consoleR = footer.removeFromLeft (222);   footer.removeFromLeft (gap);
    auto noteOffR = footer.removeFromLeft (150);   footer.removeFromLeft (gap);
    auto envSrcR  = footer.removeFromLeft (182);   footer.removeFromLeft (gap);
    auto outR     = footer;

    e.consoleCard.setBounds (consoleR);
    auto cIn = e.consoleCard.getContentBounds().translated (consoleR.getX(), consoleR.getY());
    e.consoleBox.setBounds (cIn.removeFromTop (30));
    cIn.removeFromTop (6);
    e.consoleDesc.setBounds (cIn.withTrimmedLeft (-3));

    e.noteOffCard.setBounds (noteOffR);
    auto nIn = e.noteOffCard.getContentBounds().translated (noteOffR.getX(), noteOffR.getY());
    e.noteOffSwitch.setBounds (nIn.removeFromTop (32));
    nIn.removeFromTop (8);
    e.noteOffDesc.setBounds (nIn.withTrimmedLeft (-3));

    e.envSourceCard.setBounds (envSrcR);
    auto sIn = e.envSourceCard.getContentBounds().translated (envSrcR.getX(), envSrcR.getY());
    e.envSourceSwitch.setBounds (sIn.removeFromTop (32));
    sIn.removeFromTop (8);
    e.envSourceDesc.setBounds (sIn.withTrimmedLeft (-3));

    e.outputCard.setBounds (outR);
    auto oIn = e.outputCard.getContentBounds().translated (outR.getX(), outR.getY());
    e.volumeKnob.setBounds (oIn.removeFromLeft (oIn.getWidth() / 2));
    e.bendKnob.setBounds (oIn);
}

// ---------------------------------------------------------------------------------
// State sync

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

    if ((int) processor.params.getRawParameterValue (ParamIDs::console)->load() != seenConsole)
        updateEffectHints();

    const int voices = processor.getActiveVoiceCount();
    if (voices != seenVoices)
    {
        seenVoices = voices;
        refreshInfo();
    }

    lotus::ui::EnvelopeView::State s;
    auto& ps = processor.params;
    s.attack  = ps.getRawParameterValue (ParamIDs::attack)->load();
    s.decay   = ps.getRawParameterValue (ParamIDs::decay)->load();
    s.sustain = ps.getRawParameterValue (ParamIDs::sustain)->load() / 100.0f;
    s.release = ps.getRawParameterValue (ParamIDs::release)->load();
    s.cut     = ps.getRawParameterValue (ParamIDs::noteOff)->load() > 0.5f;
    s.sf2     = ps.getRawParameterValue (ParamIDs::envSource)->load() > 0.5f;
    envelopeView.setState (s);
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
    const auto& prof = lotus::getConsoleProfile ((lotus::ConsoleId) juce::jmax (0, consoleBox.getSelectedId() - 1));
    consoleDesc.setText (prof.description, juce::dontSendNotification);

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

    noteOffDesc.setText (cut ? "Notes stop the instant the key is released."
                             : "Notes fade out over the release time.", juce::dontSendNotification);
    const float snesAlpha = snesSwitch.isOn() ? 1.0f : 0.45f;
    for (auto* c : std::initializer_list<juce::Component*> { &snesFilter, &snesFilterLabel, &snesDelayKnob, &snesFeedbackKnob, &snesVolumeKnob })
        c->setAlpha (snesAlpha);
    const float ps1Alpha = ps1Switch.isOn() ? 1.0f : 0.45f;
    for (auto* c : std::initializer_list<juce::Component*> { &ps1PresetBox, &ps1TypeLabel, &ps1DepthKnob })
        c->setAlpha (ps1Alpha);

    envSourceDesc.setText (sf2Env ? "Using the envelopes stored in the .sf2 file."
                                  : "Using the ADSR knobs above.", juce::dontSendNotification);
}

// ---------------------------------------------------------------------------------
// Loading

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
