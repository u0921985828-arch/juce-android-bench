#include "MainComponent.h"

namespace
{
    const juce::Colour kPadLoaded = ShardColours::amber;
    const juce::Colour kAccent    = ShardColours::amber;
    const juce::Colour kRec       = ShardColours::red;
    const juce::Colour kStepOff   = ShardColours::key;
    const juce::Colour kKey       = ShardColours::key;

    void styleButton (juce::TextButton& b, juce::Colour c)
    {
        // Text follows the cap luminance: dark ink on light caps, light on dark.
        const bool darkCap = c.getPerceivedBrightness() < 0.5f;
        b.setColour (juce::TextButton::buttonColourId, c);
        b.setColour (juce::TextButton::textColourOffId, darkCap ? ShardColours::inkLight : ShardColours::ink);
        b.setColour (juce::TextButton::textColourOnId,  ShardColours::ink);
    }

    // Cycle the 3 primaries across the 8 pattern banks so each has its own
    // colour identity in the chain-include row.
    juce::Colour patternRowColour (int idx)
    {
        static const juce::Colour primaries[3] = { ShardColours::accent, ShardColours::red, ShardColours::yellow };
        return primaries[(size_t) (idx % 3)];
    }
}

MainComponent::MainComponent()
{
    setLookAndFeel (&lnf);

    // Output only at startup so the app always makes sound; the mic input is
    // opened on demand when recording (avoids risking output on a denied perm).
    setAudioChannels (0, 2);

    padGain.fill (0.85f);
    padEnd01.fill (1.0f);
    padAttack.fill (2.0f);
    padRelease.fill (5.0f);

    for (int i = 0; i < kNumPads; ++i)
    {
        auto* p = new PadButton (i);
        p->onClick = [this, i] { padClicked (i); };
        addAndMakeVisible (p);
        pads.add (p);
        refreshPad (i);
    }

    for (int s = 0; s < kNumSteps; ++s)
    {
        auto* b = new juce::TextButton();
        b->onClick = [this, s] { stepClicked (s); };
        styleButton (*b, kStepOff);
        seqOverlay.addAndMakeVisible (b);
        stepButtons.add (b);
    }

    // Mode tabs (TOCAR/EDITAR/FX). SEC is a separate button that opens the
    // sequencer as a pop-up sheet instead of switching the visible tab.
    const char* tabNames[] = { "TOCAR", "EDITAR", "FX" };
    const Mode  tabModes[] = { Mode::Perform, Mode::Edit, Mode::Fx };
    for (int i = 0; i < 3; ++i)
    {
        auto* t = new juce::TextButton (tabNames[i]);
        styleButton (*t, kKey);
        t->setColour (juce::TextButton::buttonOnColourId, kAccent);
        t->setClickingTogglesState (true);
        const Mode m = tabModes[i];
        t->onClick = [this, m] { setMode (m); };
        addAndMakeVisible (t);
        tabButtons.add (t);
    }
    styleButton (secButton, kKey);
    secButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
    secButton.onClick = [this] { if (seqOverlay.isVisible()) closeSeqSheet(); else openSeqSheet(); };
    addAndMakeVisible (secButton);

    addAndMakeVisible (seqOverlay);
    seqOverlay.setVisible (false);
    seqOverlay.onDismiss = [this] { closeSeqSheet(); };
    seqOverlay.paintContent = [this] (juce::Graphics& g) { paintSeqSheetContent (g); };
    styleButton (seqCloseButton, kKey);
    seqCloseButton.onClick = [this] { closeSeqSheet(); };
    seqOverlay.addAndMakeVisible (seqCloseButton);

    // Transport / actions.
    loadButton.setClickingTogglesState (true);
    styleButton (loadButton, kKey);
    loadButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
    loadButton.onClick = [this]
    {
        loadArmed = loadButton.getToggleState();
        status.setText (loadArmed ? "LOAD armed — tap a pad to load a sample"
                                  : "Tap a pad to play", juce::dontSendNotification);
    };
    addAndMakeVisible (loadButton);

    styleButton (testButton, kKey);
    testButton.onClick = [this] { engine.postTestTone(); status.setText ("Test tone", juce::dontSendNotification); };
    addAndMakeVisible (testButton);

    styleButton (recButton, kKey);
    recButton.onClick = [this] { toggleRecording(); };
    addAndMakeVisible (recButton);

    playButton.setClickingTogglesState (true);
    styleButton (playButton, kAccent);                   // PLAY is the accent hero button
    playButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    playButton.setColour (juce::TextButton::textColourOnId,  juce::Colours::white);
    playButton.setColour (juce::TextButton::buttonOnColourId, ShardColours::accentDim);
    playButton.onClick = [this]
    {
        const bool on = playButton.getToggleState();
        engine.setPlaying (on);
        playButton.setButtonText (on ? "STOP" : "PLAY");
    };
    addAndMakeVisible (playButton);

    styleButton (clearButton, kKey);
    clearButton.onClick = [this]
    {
        engine.clearPattern (selectedPattern);
        for (auto& row : pattern[(size_t) selectedPattern]) row.fill (false);
        if (selectedPad >= 0) selectPad (selectedPad);
    };
    seqOverlay.addAndMakeVisible (clearButton);

    // Per-pad edit controls.
    auto initSlider = [this] (juce::Slider& s, double lo, double hi, double step, double def)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setColour (juce::Slider::textBoxTextColourId, ShardColours::lcdFg);
        s.setColour (juce::Slider::textBoxBackgroundColourId, ShardColours::screenBg);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 66, 22);
        s.setRange (lo, hi, step);
        s.setValue (def, juce::dontSendNotification);
        s.setColour (juce::Slider::trackColourId, kPadLoaded);
        addAndMakeVisible (s);
    };
    initSlider (startSlider,   0.0,  1.0, 0.001, 0.0);
    initSlider (endSlider,     0.0,  1.0, 0.001, 1.0);
    initSlider (bpmSlider,    60.0, 200.0, 1.0, 120.0);
    bpmSlider.setTextValueSuffix (" bpm");
    bpmSlider.onValueChange = [this] { engine.setBpm (bpmSlider.getValue()); };
    seqOverlay.addAndMakeVisible (bpmSlider);   // lives in the sequencer sheet, not the main tabs

    // Per-pad controls as rotary KNOBS, not faders — "nops, no faders".
    auto initKnob = [this] (juce::Slider& s, double lo, double hi, double step, double def,
                            double skewMid, std::function<void()> cb)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setColour (juce::Slider::textBoxTextColourId, ShardColours::lcdFg);
        s.setColour (juce::Slider::textBoxBackgroundColourId, ShardColours::screenBg);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 20);
        s.setRange (lo, hi, step);
        if (skewMid > 0.0) s.setSkewFactorFromMidPoint (skewMid);
        s.setValue (def, juce::dontSendNotification);
        s.onValueChange = std::move (cb);
        addAndMakeVisible (s);
    };
    initKnob (pitchSlider, -24.0, 24.0, 1.0, 0.0, 0.0,
             [this] { if (selectedPad >= 0) { padPitch[(size_t) selectedPad] = (float) pitchSlider.getValue(); engine.setPadPitch (selectedPad, (float) pitchSlider.getValue()); } });
    initKnob (volSlider, 0.0, 1.0, 0.01, 0.85, 0.0,
             [this] { if (selectedPad >= 0) { padGain[(size_t) selectedPad] = (float) volSlider.getValue(); engine.setPadGain (selectedPad, (float) volSlider.getValue()); } });
    initKnob (panSlider, -1.0, 1.0, 0.01, 0.0, 0.0,
             [this] { if (selectedPad >= 0) { padPan[(size_t) selectedPad] = (float) panSlider.getValue(); engine.setPadPan (selectedPad, (float) panSlider.getValue()); } });
    initKnob (attackSlider, 0.0, 200.0, 1.0, 2.0, 20.0,
             [this] { if (selectedPad >= 0) { padAttack[(size_t) selectedPad] = (float) attackSlider.getValue(); engine.setPadAttack (selectedPad, (float) attackSlider.getValue()); } });
    initKnob (releaseSlider, 1.0, 800.0, 1.0, 5.0, 40.0,
             [this] { if (selectedPad >= 0) { padRelease[(size_t) selectedPad] = (float) releaseSlider.getValue(); engine.setPadRelease (selectedPad, (float) releaseSlider.getValue()); } });
    initKnob (chokeSlider, 0.0, 8.0, 1.0, 0.0, 0.0,
             [this] { if (selectedPad >= 0) { padChokeUI[(size_t) selectedPad] = (int) chokeSlider.getValue(); engine.setPadChoke (selectedPad, (int) chokeSlider.getValue()); } });

    pitchSlider.setTextValueSuffix (" st");
    attackSlider.setTextValueSuffix (" ms");
    releaseSlider.setTextValueSuffix (" ms");
    panSlider.textFromValueFunction = [] (double v)
    {
        if (std::abs (v) < 0.005) return juce::String ("C");
        return (v < 0 ? "L" : "R") + juce::String ((int) std::round (std::abs (v) * 100.0));
    };
    panSlider.updateText();
    chokeSlider.textFromValueFunction = [] (double v) { return v <= 0.0 ? juce::String ("off") : juce::String ((int) v); };
    chokeSlider.updateText();

    startSlider.onValueChange = [this]
    {
        if (selectedPad < 0) return;
        double v = juce::jmin (startSlider.getValue(), endSlider.getValue() - 0.01);
        padStart01[(size_t) selectedPad] = (float) v;
        const int len = engine.getSampleLength (selectedPad);
        engine.setPadStart (selectedPad, (int) (v * len));
        waveform.setTrim ((float) v, padEnd01[(size_t) selectedPad]);
    };
    endSlider.onValueChange = [this]
    {
        if (selectedPad < 0) return;
        double v = juce::jmax (endSlider.getValue(), startSlider.getValue() + 0.01);
        padEnd01[(size_t) selectedPad] = (float) v;
        const int len = engine.getSampleLength (selectedPad);
        engine.setPadEnd (selectedPad, (int) (v * len));
        waveform.setTrim (padStart01[(size_t) selectedPad], (float) v);
    };

    reverseButton.setClickingTogglesState (true);
    styleButton (reverseButton, kStepOff);
    reverseButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
    reverseButton.onClick = [this] { if (selectedPad >= 0) { padReverse[(size_t) selectedPad] = reverseButton.getToggleState(); engine.setPadReverse (selectedPad, reverseButton.getToggleState()); } };
    addAndMakeVisible (reverseButton);

    loopButton.setClickingTogglesState (true);
    styleButton (loopButton, kStepOff);
    loopButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
    loopButton.onClick = [this] { if (selectedPad >= 0) { padLoop[(size_t) selectedPad] = loopButton.getToggleState(); engine.setPadLoop (selectedPad, loopButton.getToggleState()); } };
    addAndMakeVisible (loopButton);

    styleButton (chopButton, kKey);
    chopButton.onClick = [this] { autoChopSelected(); };
    addAndMakeVisible (chopButton);

    // Pattern bank selector (drives what the step grid shows/edits).
    patternSlider.setSliderStyle (juce::Slider::IncDecButtons);
    patternSlider.setRange (0.0, (double) (kNumPatterns - 1), 1.0);
    patternSlider.setValue (0.0, juce::dontSendNotification);
    patternSlider.setColour (juce::Slider::textBoxTextColourId, ShardColours::lcdFg);
    patternSlider.setColour (juce::Slider::textBoxBackgroundColourId, ShardColours::screenBg);
    patternSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    patternSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 90, 22);
    patternSlider.textFromValueFunction = [] (double v) { return "PATTERN " + juce::String ((int) v + 1); };
    patternSlider.updateText();   // refresh textbox with the new formatter
    patternSlider.onValueChange = [this]
    {
        selectedPattern = (int) patternSlider.getValue();
        engine.setEditPattern (selectedPattern);
        selectedStep = -1;
        noteSlider.setValue (0.0, juce::dontSendNotification);
        lengthSlider.setValue (engine.getPatternLength (selectedPattern), juce::dontSendNotification);
        resized();
    };
    seqOverlay.addAndMakeVisible (patternSlider);

    // Pattern length (FL-Studio-style fader): how many steps this bank plays
    // before looping / handing off to the next chain entry — 16 up to 64,
    // one row of 8 at a time. Changing it reflows the step grid itself
    // (more/fewer rows), so it forces a full resized(), not just a repaint.
    lengthSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    lengthSlider.setRange ((double) kMinPatLen, (double) kMaxPatLen, (double) kStepCols);
    lengthSlider.setValue ((double) kMinPatLen, juce::dontSendNotification);
    lengthSlider.setColour (juce::Slider::textBoxTextColourId, ShardColours::lcdFg);
    lengthSlider.setColour (juce::Slider::textBoxBackgroundColourId, ShardColours::screenBg);
    lengthSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lengthSlider.setColour (juce::Slider::trackColourId, ShardColours::accent);
    lengthSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
    lengthSlider.textFromValueFunction = [] (double v) { return "LEN " + juce::String ((int) v); };
    lengthSlider.updateText();
    lengthSlider.onValueChange = [this]
    {
        engine.setPatternLength (selectedPattern, (int) lengthSlider.getValue());
        resized();
    };
    seqOverlay.addAndMakeVisible (lengthSlider);

    // Chain include row: 8 coloured toggles, one per pattern bank — tap to
    // put that bank in (or out of) the played sequence. 0 active = fall back
    // to just looping whichever bank is being edited (unchanged behaviour).
    for (int i = 0; i < kNumPatterns; ++i)
    {
        auto* b = new juce::TextButton (juce::String (i + 1));
        styleButton (*b, kKey);
        b->setColour (juce::TextButton::buttonOnColourId, patternRowColour (i));
        b->setClickingTogglesState (true);
        b->onClick = [this, i]
        {
            patternActiveUI[(size_t) i] = patternButtons[i]->getToggleState();
            rebuildChain();
        };
        seqOverlay.addAndMakeVisible (b);
        patternButtons.add (b);
    }

    styleButton (chainClearButton, kKey);
    chainClearButton.onClick = [this]
    {
        patternActiveUI.fill (false);
        for (auto* b : patternButtons) b->setToggleState (false, juce::dontSendNotification);
        rebuildChain();
    };
    seqOverlay.addAndMakeVisible (chainClearButton);

    // Piano roll: per-step semitone offset for the selected pad (tap a step
    // to select it, then dial its pitch here — melodies from one sample).
    noteSlider.setSliderStyle (juce::Slider::IncDecButtons);
    noteSlider.setRange (-24.0, 24.0, 1.0);
    noteSlider.setValue (0.0, juce::dontSendNotification);
    noteSlider.setColour (juce::Slider::textBoxTextColourId, ShardColours::lcdFg);
    noteSlider.setColour (juce::Slider::textBoxBackgroundColourId, ShardColours::screenBg);
    noteSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    noteSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 90, 22);
    noteSlider.textFromValueFunction = [] (double v) { return "NOTE " + (v > 0 ? juce::String ("+") : juce::String()) + juce::String ((int) v); };
    noteSlider.updateText();   // refresh textbox with the new formatter
    noteSlider.onValueChange = [this]
    {
        if (selectedPad >= 0 && selectedStep >= 0)
            engine.setStepNote (selectedPattern, selectedStep, selectedPad, (int) noteSlider.getValue());
    };
    seqOverlay.addAndMakeVisible (noteSlider);

    // Master FX (filter + drive).
    fxTypeButton.setClickingTogglesState (true);
    styleButton (fxTypeButton, kKey);
    fxTypeButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
    fxTypeButton.onClick = [this]
    {
        const bool hp = fxTypeButton.getToggleState();
        fxTypeButton.setButtonText (hp ? "HPF" : "LPF");
        engine.setFxType (hp ? 1 : 0);
    };
    addAndMakeVisible (fxTypeButton);

    // FX as rotary KNOBS (vintage identity).
    initKnob (cutoffSlider, 20.0, 20000.0, 1.0, 20000.0, 1000.0, [this] { const float v=(float) cutoffSlider.getValue(); engine.setFxCutoff (v); macroFilter.setValue (v, juce::dontSendNotification); });
    initKnob (resoSlider,    0.3,  4.0, 0.01, 0.707, 0.0,       [this] { engine.setFxReso   ((float) resoSlider.getValue()); });
    initKnob (driveSlider,   0.0,  1.0, 0.01, 0.0,   0.0,       [this] { const float v=(float) driveSlider.getValue(); engine.setFxDrive (v); macroDrive.setValue (v, juce::dontSendNotification); });
    initKnob (dlyTimeSlider, 20.0, 1000.0, 1.0, 250.0, 0.0,     [this] { engine.setDlyTime  ((float) dlyTimeSlider.getValue()); });
    initKnob (dlyFbSlider,   0.0,  0.95, 0.01, 0.35, 0.0,       [this] { engine.setDlyFb    ((float) dlyFbSlider.getValue()); });
    initKnob (dlyMixSlider,  0.0,  1.0, 0.01, 0.0,   0.0,       [this] { const float v=(float) dlyMixSlider.getValue(); engine.setDlyMix (v); macroSend.setValue (v, juce::dontSendNotification); });

    // Quick-access macros on the perform screen (shared engine params).
    initKnob (macroFilter, 20.0, 20000.0, 1.0, 20000.0, 1000.0, [this] { const float v=(float) macroFilter.getValue(); engine.setFxCutoff (v); cutoffSlider.setValue (v, juce::dontSendNotification); });
    initKnob (macroDrive,   0.0, 1.0, 0.01, 0.0, 0.0,           [this] { const float v=(float) macroDrive.getValue();  engine.setFxDrive  (v); driveSlider.setValue  (v, juce::dontSendNotification); });
    initKnob (macroSend,    0.0, 1.0, 0.01, 0.0, 0.0,           [this] { const float v=(float) macroSend.getValue();   engine.setDlyMix   (v); dlyMixSlider.setValue (v, juce::dontSendNotification); });

    addAndMakeVisible (waveform);

    editLabel.setColour (juce::Label::textColourId, ShardColours::inkDim);
    editLabel.setText ("select a pad", juce::dontSendNotification);
    addAndMakeVisible (editLabel);

    status.setJustificationType (juce::Justification::centred);
    status.setColour (juce::Label::textColourId, ShardColours::inkDim);
    status.setText ("Tap a pad to play", juce::dontSendNotification);
    addAndMakeVisible (status);

    startTimer (60);
    setSize (500, 1080);
    setMode (Mode::Perform);
}

// Show only the controls relevant to the active mode, then re-layout.
void MainComponent::setMode (Mode m)
{
    mode = m;
    for (int i = 0; i < 3; ++i)
        if (auto* t = tabButtons[i]) t->setToggleState (i == (int) m, juce::dontSendNotification);

    const bool perform = (m == Mode::Perform);
    const bool edit    = (m == Mode::Edit);
    const bool fx      = (m == Mode::Fx);

    for (auto* p : pads) p->setVisible (! fx);          // pads used in perform/edit

    waveform.setVisible (true);                          // hero screen, all modes
    editLabel.setVisible (edit);
    pitchSlider.setVisible (edit); volSlider.setVisible (edit);
    startSlider.setVisible (edit); endSlider.setVisible (edit);
    chokeSlider.setVisible (edit); reverseButton.setVisible (edit); loopButton.setVisible (edit);
    panSlider.setVisible (edit); attackSlider.setVisible (edit); releaseSlider.setVisible (edit);
    chopButton.setVisible (edit);

    cutoffSlider.setVisible (fx); resoSlider.setVisible (fx); driveSlider.setVisible (fx);
    dlyTimeSlider.setVisible (fx); dlyFbSlider.setVisible (fx); dlyMixSlider.setVisible (fx);
    fxTypeButton.setVisible (fx); testButton.setVisible (fx);

    macroFilter.setVisible (perform); macroDrive.setVisible (perform); macroSend.setVisible (perform);

    if (edit && selectedPad < 0)
        selectPad (0);

    resized();
    repaint();
}

// The sequencer sheet pops up over whichever tab is showing — it doesn't
// change `mode` at all, just shows the overlay on top.
void MainComponent::openSeqSheet()
{
    if (selectedPad < 0) selectPad (0);
    secButton.setToggleState (true, juce::dontSendNotification);
    seqOverlay.setVisible (true);
    seqOverlay.toFront (false);
    resized();
    repaint();
}

void MainComponent::closeSeqSheet()
{
    secButton.setToggleState (false, juce::dontSendNotification);
    seqOverlay.setVisible (false);
    repaint();
}

MainComponent::~MainComponent()
{
    shutdownAudio();
    setLookAndFeel (nullptr);
}

// ---------------------------------------------------------------------------
//  Audio callbacks
// ---------------------------------------------------------------------------

void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    engine.prepareToPlay (sampleRate, samplesPerBlockExpected);
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    engine.renderNextBlock (*info.buffer, info.startSample, info.numSamples);
}

void MainComponent::releaseResources()
{
    engine.releaseResources();
}

// ---------------------------------------------------------------------------
//  UI
// ---------------------------------------------------------------------------

void MainComponent::paint (juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();

    // 1. Full-bleed light chassis (edge to edge — the whole screen is the face).
    g.setGradientFill (juce::ColourGradient (ShardColours::chassisTop, full.getCentreX(), full.getY(),
                                             ShardColours::chassisBot, full.getCentreX(), full.getBottom(), false));
    g.fillRect (full);

    // Helper: a square raised sub-panel with a soft border, controls grouped.
    auto panel = [&g] (juce::Rectangle<int> ri)
    {
        if (ri.isEmpty()) return;
        auto r = ri.toFloat();
        g.setColour (ShardColours::panel);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (ShardColours::panelHi.withAlpha (0.8f));
        g.drawLine (r.getX() + 3, r.getY() + 1.0f, r.getRight() - 3, r.getY() + 1.0f, 1.2f);
        g.setColour (ShardColours::panelLo.withAlpha (0.7f));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.2f);
    };
    panel (fxPanelArea);
    panel (editPanelArea);

    // 2. Recessed LCD bezel around the scope (square, dark inset on a light face).
    if (! screenBezel.isEmpty())
    {
        auto r = screenBezel.toFloat();
        g.setColour (ShardColours::knobBody2);
        g.fillRoundedRectangle (r.expanded (3.0f), 3.0f);
        g.setColour (ShardColours::knobEdge.withAlpha (0.7f));
        g.drawRoundedRectangle (r.expanded (3.0f).reduced (0.5f), 3.0f, 1.2f);
    }

    // 3. Header: ARTiFACTS wordmark + COLORS model badge (dark LCD chip).
    if (! headerArea.isEmpty())
    {
        auto h = headerArea;
        g.setColour (ShardColours::ink);
        g.setFont (ShardColours::displayFont (22.0f).withExtraKerningFactor (0.10f));
        g.drawText ("ARTiFACTS", h.getX(), h.getY(), 180, h.getHeight(), juce::Justification::centredLeft);

        auto badge = juce::Rectangle<int> (h.getRight() - 156, h.getY() + 2, 156, h.getHeight() - 4);
        g.setColour (ShardColours::screenBg);
        g.fillRoundedRectangle (badge.toFloat(), 2.0f);
        g.setColour (ShardColours::amber.withAlpha (0.8f));
        g.drawRoundedRectangle (badge.toFloat().reduced (0.5f), 2.0f, 1.2f);
        auto bin = badge.reduced (10, 0);
        g.setColour (ShardColours::amberBright);
        g.setFont (ShardColours::monoFont (14.0f, true).withExtraKerningFactor (0.22f));
        g.drawText ("COLORS", bin.removeFromLeft (86), juce::Justification::centredLeft);
        g.setColour (ShardColours::lcdFg.withAlpha (0.6f));
        g.setFont (ShardColours::monoFont (8.5f, true).withExtraKerningFactor (0.24f));
        g.drawText ("SAMPLER", bin, juce::Justification::centredRight);
    }

    // 4. Mode-specific labels.
    if (mode == Mode::Perform)
    {
        g.setColour (ShardColours::ink.withAlpha (0.85f));
        g.setFont (ShardColours::monoFont (10.0f, true).withExtraKerningFactor (0.16f));
        auto mn = [&g] (juce::Slider& s, const char* t)
        {
            auto r = s.getBounds();
            g.drawText (t, r.getX() - 6, r.getY() - 14, r.getWidth() + 12, 12, juce::Justification::centred);
        };
        mn (macroFilter, "FILTER"); mn (macroDrive, "DRIVE"); mn (macroSend, "SEND");
    }
    else if (mode == Mode::Fx)
    {
        g.setColour (ShardColours::ink.withAlpha (0.85f));
        g.setFont (ShardColours::monoFont (10.5f, true).withExtraKerningFactor (0.12f));
        auto name = [&g] (juce::Slider& s, const char* t)
        {
            auto r = s.getBounds();
            g.drawText (t, r.getX() - 6, r.getY() - 15, r.getWidth() + 12, 13, juce::Justification::centred);
        };
        name (cutoffSlider, "CUTOFF"); name (resoSlider, "RESO");  name (driveSlider, "DRIVE");
        name (dlyTimeSlider, "TIME");  name (dlyFbSlider, "FBK");  name (dlyMixSlider, "MIX");
    }
    else if (mode == Mode::Edit)
    {
        g.setColour (ShardColours::ink.withAlpha (0.9f));
        // Knobs: label above (same convention as FX).
        g.setFont (ShardColours::monoFont (10.0f, true).withExtraKerningFactor (0.10f));
        auto name = [&g] (juce::Slider& s, const char* t)
        {
            auto r = s.getBounds();
            g.drawText (t, r.getX() - 6, r.getY() - 14, r.getWidth() + 12, 12, juce::Justification::centred);
        };
        name (pitchSlider, "PITCH"); name (volSlider, "VOLUME"); name (panSlider, "PAN");
        name (attackSlider, "ATTACK"); name (releaseSlider, "RELEASE"); name (chokeSlider, "CHOKE");

        // Start/End stay linear (a trim range, not a knob): label to the left.
        g.setFont (ShardColours::monoFont (11.0f, true).withExtraKerningFactor (0.06f));
        auto lab = [&g] (juce::Slider& s, const char* t)
        {
            auto r = s.getBounds();
            g.drawText (t, r.getX() - 66, r.getY(), 60, r.getHeight(), juce::Justification::centredLeft);
        };
        lab (startSlider, "START"); lab (endSlider, "END");
    }
}

// Sequencer sheet content: title/chain text + step-selection/playhead rings.
// Called from SeqOverlay::paint() (set as its paintContent callback) so it
// draws in the overlay's own paint pass, on top of everything else.
void MainComponent::paintSeqSheetContent (juce::Graphics& g)
{
    if (seqPanelArea.isEmpty()) return;

    const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00B7);
    const int sp = juce::jmax (0, selectedPad);
    auto inner = seqPanelArea.reduced (12, 6);

    g.setColour (ShardColours::ink.withAlpha (0.9f));
    g.setFont (ShardColours::monoFont (11.0f, true).withExtraKerningFactor (0.14f));
    const juce::String t = "STEPS  " + dot + "  PAD " + juce::String (sp + 1)
                         + (padName[(size_t) sp].isNotEmpty() ? "   " + padName[(size_t) sp] : juce::String())
                         + "   " + dot + "   P" + juce::String (selectedPattern + 1);
    g.drawText (t, inner.removeFromTop (16), juce::Justification::centredLeft);

    // Which bank is actually sounding right now (may differ from the one
    // being viewed/edited) — the coloured row below shows chain membership.
    const juce::String chainStr = (engine.getChainLength() <= 0)
        ? "looping P" + juce::String (selectedPattern + 1)
        : "playing P" + juce::String (engine.getPlayingPattern() + 1);
    g.setColour (ShardColours::inkDim);
    g.setFont (ShardColours::monoFont (9.5f, true).withExtraKerningFactor (0.10f));
    g.drawText (chainStr, inner.removeFromTop (14), juce::Justification::centredLeft);

    // Selection (yellow) and playhead (red) rings — flat fills never blend,
    // these are drawn as outlines on top instead.
    if (selectedStep >= 0)
    {
        if (auto* b = stepButtons[selectedStep])
        {
            g.setColour (ShardColours::yellow);
            g.drawRect (b->getBounds(), 2);
        }
    }
    const int ps = engine.getPlayStep();
    if (ps >= 0 && engine.getPlayingPattern() == selectedPattern)
    {
        if (auto* b = stepButtons[ps])
        {
            g.setColour (ShardColours::red);
            g.drawRect (b->getBounds(), 2);
        }
    }
    if (auto* b = patternButtons[selectedPattern])
    {
        g.setColour (ShardColours::ink.withAlpha (0.7f));
        g.drawRect (b->getBounds(), 2);
    }
}

void MainComponent::SeqOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.45f));
    if (sheetBounds.isEmpty()) return;

    g.setColour (ShardColours::chassisTop);
    g.fillRoundedRectangle (sheetBounds.toFloat(), 14.0f);
    g.setColour (ShardColours::ink.withAlpha (0.55f));
    g.drawRoundedRectangle (sheetBounds.toFloat().reduced (0.5f), 14.0f, 1.5f);

    if (paintContent) paintContent (g);
}

// Flat-style overlay rings (drawn over the step buttons' plain fill, never
// blended into it): yellow marks the step selected for NOTE editing, red
// marks the live playhead — only when viewing the pattern that's actually
// sounding, so a chain playing a different bank doesn't ring the wrong grid.
void MainComponent::layoutPadGrid (juce::Rectangle<int> area, int cols, int rows, int gap)
{
    // True square pads (Akai-style) — sized by whichever dimension is
    // tighter, then centred in the given area rather than stretched to fill
    // it (which would make them rectangular).
    const int cellW = (area.getWidth()  - (cols - 1) * gap) / cols;
    const int cellH = (area.getHeight() - (rows - 1) * gap) / rows;
    const int cell  = juce::jmin (cellW, cellH);

    auto grid = area.withSizeKeepingCentre (cols * cell + (cols - 1) * gap,
                                            rows * cell + (rows - 1) * gap);

    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
        {
            const int idx = r * cols + c;
            if (auto* p = pads[idx])
                p->setBounds (grid.getX() + c * (cell + gap),
                             grid.getY() + r * (cell + gap),
                             cell, cell);
        }
}

void MainComponent::resized()
{
    fxPanelArea = seqPanelArea = editPanelArea = editCtrlArea = {};

    auto area = getLocalBounds().reduced (8);

    // --- Always-visible top chrome ---
    headerArea = area.removeFromTop (30);
    area.removeFromTop (8);

    screenBezel = area.removeFromTop (96);
    waveform.setBounds (screenBezel);
    area.removeFromTop (8);

    // Tab bar: TOCAR / EDITAR / FX, plus SEC (opens the sequencer sheet).
    tabBarArea = area.removeFromTop (36);
    {
        auto row = tabBarArea;
        const int w = row.getWidth() / 4;
        for (int i = 0; i < 3; ++i)
            tabButtons[i]->setBounds (row.removeFromLeft (w).reduced (2));
        secButton.setBounds (row.reduced (2));
    }
    area.removeFromTop (6);

    // Transport: LOAD | REC | PLAY (PLAY is the wide accent hero).
    {
        auto row = area.removeFromTop (40);
        const int u = row.getWidth() / 4;
        loadButton.setBounds (row.removeFromLeft (u).reduced (2));
        recButton.setBounds  (row.removeFromLeft (u).reduced (2));
        playButton.setBounds (row.reduced (2));
    }
    area.removeFromTop (8);

    // Status pinned to the bottom.
    status.setBounds (area.removeFromBottom (18));
    area.removeFromBottom (6);

    // --- Mode body ---
    if (mode == Mode::Perform)
    {
        auto mrow = area.removeFromTop (86);             // 3 quick macros
        juce::Slider* mk[3] = { &macroFilter, &macroDrive, &macroSend };
        const int w = mrow.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
        {
            auto cell = (i < 2 ? mrow.removeFromLeft (w) : mrow);
            cell.removeFromTop (14);                     // gap for label
            mk[i]->setBounds (cell.reduced (10, 0));
        }
        area.removeFromTop (8);
        layoutPadGrid (area, 4, 4, 8);
    }
    else if (mode == Mode::Fx)
    {
        fxPanelArea = area;
        auto inner = area.reduced (12, 14);
        auto ctrl  = inner.removeFromBottom (40);
        fxTypeButton.setBounds (ctrl.removeFromLeft (ctrl.getWidth() / 2).reduced (3));
        testButton.setBounds   (ctrl.reduced (3));
        inner.removeFromBottom (10);

        juce::Slider* r1[3] = { &cutoffSlider, &resoSlider, &driveSlider };
        juce::Slider* r2[3] = { &dlyTimeSlider, &dlyFbSlider, &dlyMixSlider };
        const int rowH = inner.getHeight() / 2;
        auto place = [] (juce::Rectangle<int> row, juce::Slider** ks)
        {
            const int w = row.getWidth() / 3;
            for (int i = 0; i < 3; ++i)
            {
                auto cell = (i < 2 ? row.removeFromLeft (w) : row);
                cell.removeFromTop (16);                 // gap for knob name
                ks[i]->setBounds (cell.reduced (6, 2));
            }
        };
        place (inner.removeFromTop (rowH), r1);
        place (inner, r2);
    }
    else // Edit: compact pad selector on top, panel below.
    {
        auto padArea = area.removeFromTop ((int) (area.getHeight() * 0.40f));
        layoutPadGrid (padArea, 4, 4, 5);
        area.removeFromTop (8);

        editPanelArea = area;
        auto inner = area.reduced (10, 8);
        editLabel.setBounds (inner.removeFromTop (18));
        inner.removeFromTop (8);
        editCtrlArea = inner;                        // labels drawn in paint

        // Knobs, not faders: PITCH/VOLUME/PAN, then ATTACK/RELEASE/CHOKE.
        juce::Slider* k1[3] = { &pitchSlider, &volSlider, &panSlider };
        juce::Slider* k2[3] = { &attackSlider, &releaseSlider, &chokeSlider };
        auto placeKnobs = [] (juce::Rectangle<int> row, juce::Slider** ks)
        {
            const int w = row.getWidth() / 3;
            for (int i = 0; i < 3; ++i)
            {
                auto cell = (i < 2 ? row.removeFromLeft (w) : row);
                cell.removeFromTop (16);                 // gap for knob name
                ks[i]->setBounds (cell.reduced (6, 2));
            }
        };
        const int knobRowH = juce::jmin (86, inner.getHeight() / 3);
        placeKnobs (inner.removeFromTop (knobRowH), k1);
        placeKnobs (inner.removeFromTop (knobRowH), k2);
        inner.removeFromTop (6);

        // Start/End: a trim range on the waveform, stays a linear slider pair.
        const int labelW = 64;
        auto ctrlRow = [&inner, labelW] (int h) { auto r = inner.removeFromTop (h); r.removeFromLeft (labelW); return r; };
        startSlider.setBounds (ctrlRow (26)); inner.removeFromTop (4);
        endSlider.setBounds   (ctrlRow (26)); inner.removeFromTop (8);

        {
            auto rr = inner.removeFromTop (30);
            reverseButton.setBounds (rr.removeFromLeft (rr.getWidth() / 2).reduced (3, 0));
            loopButton.setBounds (rr.reduced (3, 0));
        }
        inner.removeFromTop (6);
        chopButton.setBounds (inner.removeFromTop (30).reduced (3, 0));
    }

    // --- Sequencer sheet: pops up over whichever tab is showing ---
    {
        seqOverlay.setBounds (getLocalBounds());
        auto full = getLocalBounds();
        auto sheet = full.removeFromBottom ((int) (full.getHeight() * 0.86f)).reduced (8);
        seqOverlay.sheetBounds = sheet;

        seqPanelArea = sheet;
        auto inner = sheet.reduced (14, 12);
        auto titleRow = inner.removeFromTop (32);          // 2-line title drawn by paintSeqSheetContent
        seqCloseButton.setBounds (titleRow.removeFromRight (32).reduced (2));

        // Pattern bank + length, chain-include row, note (3 rows).
        {
            auto row1 = inner.removeFromTop (26);
            const int w1 = row1.getWidth() / 2;
            patternSlider.setBounds (row1.removeFromLeft (w1).reduced (2, 0));
            lengthSlider.setBounds  (row1.reduced (2, 0));
            inner.removeFromTop (4);

            // 8 coloured chain-include toggles, one per pattern bank.
            auto row2 = inner.removeFromTop (28);
            const int pw = row2.getWidth() / kNumPatterns;
            for (int i = 0; i < kNumPatterns; ++i)
                patternButtons[i]->setBounds ((i < kNumPatterns - 1 ? row2.removeFromLeft (pw) : row2).reduced (2));
            inner.removeFromTop (4);

            auto row3 = inner.removeFromTop (26);
            const int w3 = row3.getWidth() / 2;
            chainClearButton.setBounds (row3.removeFromLeft (w3).reduced (2, 0));
            noteSlider.setBounds       (row3.reduced (2, 0));
        }
        inner.removeFromTop (6);

        auto bottom = inner.removeFromBottom (30);
        bpmSlider.setBounds (bottom.removeFromLeft ((int) (bottom.getWidth() * 0.66f)).reduced (2, 0));
        clearButton.setBounds (bottom.reduced (3, 0));
        inner.removeFromBottom (8);

        // Step grid: always 8 columns, rows = pattern length / 8 (2..8, i.e.
        // 16..64 steps). Real square cells, sized to fill whatever room the
        // sheet gives them (bigger with fewer rows, smaller with more),
        // capped so they never get silly at either extreme.
        {
            const int patLen = engine.getPatternLength (selectedPattern);
            const int cols = kStepCols;
            const int rows = juce::jmax (1, patLen / cols);
            const int gap  = 4;

            const int maxCellW = (inner.getWidth()  - (cols - 1) * gap) / cols;
            const int maxCellH = (inner.getHeight() - (rows - 1) * gap) / rows;
            const int cell = juce::jlimit (20, 70, juce::jmin (maxCellW, maxCellH));

            auto steps = inner.removeFromTop (juce::jmin (inner.getHeight(), rows * cell + (rows - 1) * gap));
            steps = steps.withSizeKeepingCentre (cols * cell + (cols - 1) * gap, steps.getHeight());

            for (int s = 0; s < kNumSteps; ++s)
            {
                if (s >= patLen) { stepButtons[s]->setVisible (false); continue; }
                const int r = s / cols, c = s % cols;
                stepButtons[s]->setVisible (true);
                stepButtons[s]->setBounds (steps.getX() + c * (cell + gap),
                                           steps.getY() + r * (cell + gap),
                                           cell, cell);
            }
        }
    }
}

void MainComponent::padClicked (int index)
{
    if (loadArmed)
    {
        loadArmed = false;
        loadButton.setToggleState (false, juce::dontSendNotification);
        openChooserForPad (index);
        return;
    }

    if (padHasSample[(size_t) index])
        engine.postNoteOn (index);
    else
        status.setText ("Empty pad — press LOAD, then tap to load a sample", juce::dontSendNotification);

    selectPad (index);   // selection drives EDIT and SEC
}

void MainComponent::stepClicked (int step)
{
    if (step >= engine.getPatternLength (selectedPattern)) return;   // past the pattern's own length
    selectedStep = step;
    noteSlider.setValue (engine.getStepNote (selectedPattern, step, juce::jmax (0, selectedPad)), juce::dontSendNotification);
    repaint();   // move the selection ring (drawn in paintOverChildren)

    if (selectedPad < 0) return;
    const bool nv = ! pattern[(size_t) selectedPattern][(size_t) step][(size_t) selectedPad];
    pattern[(size_t) selectedPattern][(size_t) step][(size_t) selectedPad] = nv;
    engine.setStep (selectedPattern, step, selectedPad, nv);
}

void MainComponent::refreshPad (int index)
{
    if (auto* p = pads[index])
    {
        p->setSelected (index == selectedPad);
        p->setFlash (padFlash[(size_t) index]);
    }
}

void MainComponent::selectPad (int index)
{
    selectedPad = index;
    updateControlsFromPad (index);
    waveform.setSample (uiSample[(size_t) index]);
    waveform.setTrim (padStart01[(size_t) index], padEnd01[(size_t) index]);
    if (auto sb = uiSample[(size_t) index])
    {
        const double sr = sb->sourceSampleRate;
        const double secs = sr > 0.0 ? (double) sb->buffer.getNumSamples() / sr : 0.0;
        const juce::String tag = juce::String (index + 1).paddedLeft ('0', 2)
                               + (padName[(size_t) index].isNotEmpty() ? "  " + padName[(size_t) index].toUpperCase() : juce::String());
        waveform.setInfo (tag, sr, secs, sb->buffer.getNumChannels());
    }
    else
        waveform.setInfo ("PAD " + juce::String (index + 1), 0.0, 0.0, 0);
    const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00B7);
    editLabel.setText ("PAD " + juce::String (index + 1)
                       + (padName[(size_t) index].isNotEmpty() ? "  " + dot + "  " + padName[(size_t) index] : juce::String()),
                       juce::dontSendNotification);
    for (int i = 0; i < kNumPads; ++i) refreshPad (i);
}

void MainComponent::updateControlsFromPad (int index)
{
    pitchSlider.setValue (padPitch[(size_t) index], juce::dontSendNotification);
    volSlider.setValue   (padGain[(size_t) index],  juce::dontSendNotification);
    startSlider.setValue (padStart01[(size_t) index], juce::dontSendNotification);
    endSlider.setValue   (padEnd01[(size_t) index],   juce::dontSendNotification);
    reverseButton.setToggleState (padReverse[(size_t) index], juce::dontSendNotification);
    loopButton.setToggleState    (padLoop[(size_t) index],    juce::dontSendNotification);
    chokeSlider.setValue (padChokeUI[(size_t) index], juce::dontSendNotification);
    panSlider.setValue     (padPan[(size_t) index],     juce::dontSendNotification);
    attackSlider.setValue  (padAttack[(size_t) index],  juce::dontSendNotification);
    releaseSlider.setValue (padRelease[(size_t) index], juce::dontSendNotification);
}

void MainComponent::assignSampleToPad (int index, SampleBuffer::Ptr sb, const juce::String& name)
{
    if (sb == nullptr) return;
    padHasSample[(size_t) index] = true;
    uiSample[(size_t) index]     = sb;
    padStart01[(size_t) index]   = 0.0f;
    padEnd01[(size_t) index]     = 1.0f;
    if (name.isNotEmpty())
        padName[(size_t) index] = name.upToLastOccurrenceOf (".", false, false);

    // Push this pad's UI params into the engine. The engine's per-pad gain
    // defaults to 0 (silent); setVal(dontSendNotification) never fires the
    // slider callbacks, so without this the pad plays at zero gain.
    engine.setPadGain    (index, padGain[(size_t) index]);
    engine.setPadPitch   (index, padPitch[(size_t) index]);
    engine.setPadLoop    (index, padLoop[(size_t) index]);
    engine.setPadReverse (index, padReverse[(size_t) index]);
    engine.setPadChoke   (index, padChokeUI[(size_t) index]);
    engine.setPadPan     (index, padPan[(size_t) index]);
    engine.setPadAttack  (index, padAttack[(size_t) index]);
    engine.setPadRelease (index, padRelease[(size_t) index]);

    if (auto* p = pads[index]) p->setSampleInfo (uiSample[(size_t) index], padName[(size_t) index]);

    selectPad (index);
}

void MainComponent::rebuildChain()
{
    engine.clearChain();
    for (int i = 0; i < kNumPatterns; ++i)
        if (patternActiveUI[(size_t) i])
            engine.addToChain (i);
    repaint();
}

void MainComponent::autoChopSelected()
{
    if (selectedPad < 0) return;
    auto src = uiSample[(size_t) selectedPad];
    if (src == nullptr) return;
    const int len = src->buffer.getNumSamples();
    if (len < kNumPads) return;

    const juce::String baseName = padName[(size_t) selectedPad].isNotEmpty()
                                 ? padName[(size_t) selectedPad] : juce::String ("CHOP");

    for (int i = 0; i < kNumPads; ++i)
    {
        const int st = (int) ((juce::int64) i * len / kNumPads);
        const int en = (int) ((juce::int64) (i + 1) * len / kNumPads);

        padHasSample[(size_t) i] = true;
        uiSample[(size_t) i]     = src;
        padStart01[(size_t) i]   = (float) st / (float) len;
        padEnd01[(size_t) i]     = (float) en / (float) len;
        padLoop[(size_t) i]      = false;
        padReverse[(size_t) i]   = false;
        padChokeUI[(size_t) i]   = 0;
        padName[(size_t) i]      = baseName + " " + juce::String (i + 1).paddedLeft ('0', 2);

        engine.publishSample (i, src);   // resets trim to full length — override right after
        engine.setPadStart   (i, st);
        engine.setPadEnd     (i, en);
        engine.setPadGain    (i, padGain[(size_t) i]);
        engine.setPadPitch   (i, padPitch[(size_t) i]);
        engine.setPadLoop    (i, false);
        engine.setPadReverse (i, false);
        engine.setPadChoke   (i, 0);
        engine.setPadPan     (i, padPan[(size_t) i]);
        engine.setPadAttack  (i, padAttack[(size_t) i]);
        engine.setPadRelease (i, padRelease[(size_t) i]);

        if (auto* p = pads[i]) p->setSampleInfo (uiSample[(size_t) i], padName[(size_t) i]);
    }

    selectPad (0);
    status.setText ("Auto-chopped into " + juce::String (kNumPads) + " pads", juce::dontSendNotification);
}

int MainComponent::firstEmptyPad() const
{
    for (int i = 0; i < kNumPads; ++i)
        if (! padHasSample[(size_t) i]) return i;
    return -1;
}

void MainComponent::openChooserForPad (int index)
{
    chooser = std::make_unique<juce::FileChooser> (
        "Assign a sample to pad " + juce::String (index + 1),
        juce::File{}, "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3");

    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (flags, [this, index] (const juce::FileChooser& fc)
    {
        const auto url = fc.getURLResult();
        if (url.isEmpty()) return;

        status.setText ("Loading pad " + juce::String (index + 1) + " ...", juce::dontSendNotification);
        const juce::String fileName = url.getFileName();
        loader.loadAsync (url, index, [this, index, fileName] (bool ok, juce::String detail, SampleBuffer::Ptr sb)
        {
            if (ok)
            {
                assignSampleToPad (index, sb, fileName);
                status.setText ("Pad " + juce::String (index + 1) + " loaded  [" + detail + "]", juce::dontSendNotification);
            }
            else
            {
                status.setText ("Load failed: " + detail, juce::dontSendNotification);
            }
        });
    });
}

void MainComponent::toggleRecording()
{
    if (! recordingActive)
    {
        int slot = (selectedPad >= 0) ? selectedPad : firstEmptyPad();
        if (slot < 0) slot = 0;
        recordingSlot = slot;
        setAudioChannels (1, 2);          // open mic input (requests RECORD_AUDIO on Android)
        engine.startRecording (slot);
        recordingActive = true;
        styleButton (recButton, kRec);
        recButton.setButtonText ("STOP");
        status.setText ("Recording pad " + juce::String (slot + 1) + " ...", juce::dontSendNotification);
    }
    else
    {
        recordingActive = false;
        auto sb = engine.finishRecording();
        setAudioChannels (0, 2);          // release the mic input, back to output-only
        styleButton (recButton, kKey);
        recButton.setButtonText ("REC");
        if (sb != nullptr)
        {
            assignSampleToPad (recordingSlot, sb, "REC " + juce::String (recordingSlot + 1));
            status.setText ("Recorded pad " + juce::String (recordingSlot + 1), juce::dontSendNotification);
        }
        else
        {
            status.setText ("Nothing recorded", juce::dontSendNotification);
        }
    }
}

void MainComponent::timerCallback()
{
    engine.collectRetiredSamples();

    const int ps = engine.getPlayStep();

    // Pad trigger feedback (taps + sequencer): flash then decay.
    const std::uint32_t trig = engine.fetchTriggered();
    bool anyFlash = false;
    for (int i = 0; i < kNumPads; ++i)
    {
        if ((trig & (std::uint32_t) (1u << i)) != 0) padFlash[(size_t) i] = 1.0f;
        if (padFlash[(size_t) i] > 0.0f)
        {
            padFlash[(size_t) i] *= 0.8f;
            if (padFlash[(size_t) i] < 0.02f) padFlash[(size_t) i] = 0.0f;
            refreshPad (i);
            anyFlash = true;
        }
    }
    juce::ignoreUnused (anyFlash);

    // Sequencer step colours (flat fill only — see paintOverChildren() for
    // the selection/playhead rings). Steps beyond the pattern's own length
    // (FL-Studio-style variable length, 16..64) aren't laid out at all — see
    // resized() — so only the visible ones need colouring here.
    const int patLen = engine.getPatternLength (selectedPattern);
    for (int s = 0; s < patLen; ++s)
    {
        // Every 16 steps ("page") gets a faint primary-colour wash on the
        // resting fill, cycling blue/red/yellow, so a long pattern reads at
        // a glance; alternating groups of 4 (the beats within a page) shade
        // a touch darker on top of that.
        const bool altBeat = ((s / 4) % 2) != 0;
        const bool on = (selectedPad >= 0) && pattern[(size_t) selectedPattern][(size_t) s][(size_t) selectedPad];
        auto rest = kStepOff.interpolatedWith (patternRowColour (s / 16), 0.16f);
        if (altBeat) rest = rest.darker (0.13f);
        // Flat fill: blue = hit, tinted white/grey = resting. Selection
        // (yellow) and the playhead (red) are drawn as rings on top in
        // paintOverChildren(), not blended into the fill, so states never
        // muddy together.
        stepButtons[s]->setColour (juce::TextButton::buttonColourId, on ? kPadLoaded : rest);
    }
    lastPlayStep = ps;

    // Keep the sheet's chain/playing-pattern readout + rings live while the
    // sequencer runs and the sheet is open.
    if (seqOverlay.isVisible() && engine.isPlaying())
        seqOverlay.repaint();

    if (recordingActive)
        status.setText ("Recording pad " + juce::String (recordingSlot + 1)
                        + "  " + juce::String (engine.getRecordSeconds(), 1) + "s",
                        juce::dontSendNotification);
}
