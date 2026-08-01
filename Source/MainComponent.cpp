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
}

MainComponent::MainComponent()
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (spectrum);

    // Output only at startup so the app always makes sound; the mic input is
    // opened on demand when recording (avoids risking output on a denied perm).
    setAudioChannels (0, 2);

    padGain.fill (0.85f);
    padEnd01.fill (1.0f);

    for (int i = 0; i < kNumPads; ++i)
    {
        auto* p = new juce::TextButton (juce::String (i + 1));
        p->getProperties().set ("pad", true);
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
        addAndMakeVisible (b);
        stepButtons.add (b);
    }

    // Mode tabs.
    const char* tabNames[] = { "TOCAR", "EDITAR", "SEC", "FX" };
    const Mode  tabModes[] = { Mode::Perform, Mode::Edit, Mode::Seq, Mode::Fx };
    for (int i = 0; i < 4; ++i)
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
    styleButton (playButton, kKey);
    playButton.setColour (juce::TextButton::buttonOnColourId, kPadLoaded);
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
        engine.clearPattern();
        for (auto& row : pattern) row.fill (false);
        if (selectedPad >= 0) selectPad (selectedPad);
    };
    addAndMakeVisible (clearButton);

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
    initSlider (pitchSlider, -24.0, 24.0, 1.0, 0.0);
    initSlider (volSlider,     0.0,  1.0, 0.01, 0.85);
    initSlider (startSlider,   0.0,  1.0, 0.001, 0.0);
    initSlider (endSlider,     0.0,  1.0, 0.001, 1.0);
    initSlider (bpmSlider,    60.0, 200.0, 1.0, 120.0);

    pitchSlider.setTextValueSuffix (" st");
    bpmSlider.setTextValueSuffix (" bpm");

    pitchSlider.onValueChange = [this] { if (selectedPad >= 0) { padPitch[(size_t) selectedPad] = (float) pitchSlider.getValue(); engine.setPadPitch (selectedPad, (float) pitchSlider.getValue()); } };
    volSlider.onValueChange   = [this] { if (selectedPad >= 0) { padGain[(size_t) selectedPad]  = (float) volSlider.getValue();   engine.setPadGain  (selectedPad, (float) volSlider.getValue()); } };
    bpmSlider.onValueChange   = [this] { engine.setBpm (bpmSlider.getValue()); };

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

    chokeSlider.setSliderStyle (juce::Slider::IncDecButtons);
    chokeSlider.setRange (0.0, 8.0, 1.0);
    chokeSlider.setValue (0.0, juce::dontSendNotification);
    chokeSlider.setColour (juce::Slider::textBoxTextColourId, ShardColours::lcdFg);
    chokeSlider.setColour (juce::Slider::textBoxBackgroundColourId, ShardColours::screenBg);
    chokeSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    chokeSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 90, 22);
    chokeSlider.textFromValueFunction = [] (double v) { return v <= 0.0 ? juce::String ("CHOKE: off") : "CHOKE: " + juce::String ((int) v); };
    chokeSlider.onValueChange = [this] { if (selectedPad >= 0) { padChokeUI[(size_t) selectedPad] = (int) chokeSlider.getValue(); engine.setPadChoke (selectedPad, (int) chokeSlider.getValue()); } };
    addAndMakeVisible (chokeSlider);

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
    initKnob (cutoffSlider, 20.0, 20000.0, 1.0, 20000.0, 1000.0, [this] { engine.setFxCutoff ((float) cutoffSlider.getValue()); });
    initKnob (resoSlider,    0.3,  4.0, 0.01, 0.707, 0.0,       [this] { engine.setFxReso   ((float) resoSlider.getValue()); });
    initKnob (driveSlider,   0.0,  1.0, 0.01, 0.0,   0.0,       [this] { engine.setFxDrive  ((float) driveSlider.getValue()); });
    initKnob (dlyTimeSlider, 20.0, 1000.0, 1.0, 250.0, 0.0,     [this] { engine.setDlyTime  ((float) dlyTimeSlider.getValue()); });
    initKnob (dlyFbSlider,   0.0,  0.95, 0.01, 0.35, 0.0,       [this] { engine.setDlyFb    ((float) dlyFbSlider.getValue()); });
    initKnob (dlyMixSlider,  0.0,  1.0, 0.01, 0.0,   0.0,       [this] { engine.setDlyMix   ((float) dlyMixSlider.getValue()); });

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
    for (int i = 0; i < 4; ++i)
        if (auto* t = tabButtons[i]) t->setToggleState (i == (int) m, juce::dontSendNotification);

    const bool perform = (m == Mode::Perform);
    const bool edit    = (m == Mode::Edit);
    const bool seq     = (m == Mode::Seq);
    const bool fx      = (m == Mode::Fx);

    for (auto* p : pads) p->setVisible (! fx);          // pads used in perform/edit/seq
    for (auto* s : stepButtons) s->setVisible (seq);

    waveform.setVisible (edit);
    editLabel.setVisible (edit);
    pitchSlider.setVisible (edit); volSlider.setVisible (edit);
    startSlider.setVisible (edit); endSlider.setVisible (edit);
    chokeSlider.setVisible (edit); reverseButton.setVisible (edit); loopButton.setVisible (edit);

    cutoffSlider.setVisible (fx); resoSlider.setVisible (fx); driveSlider.setVisible (fx);
    dlyTimeSlider.setVisible (fx); dlyFbSlider.setVisible (fx); dlyMixSlider.setVisible (fx);
    fxTypeButton.setVisible (fx); testButton.setVisible (fx);

    clearButton.setVisible (seq);
    bpmSlider.setVisible (seq);

    if ((edit || seq) && selectedPad < 0)
        selectPad (0);

    resized();
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
    panel (seqPanelArea);
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

    // 3. Header: ARTiFACTS wordmark + SHARD model badge (dark LCD chip).
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
        g.drawText ("SHARD", bin.removeFromLeft (74), juce::Justification::centredLeft);
        g.setColour (ShardColours::lcdFg.withAlpha (0.6f));
        g.setFont (ShardColours::monoFont (8.5f, true).withExtraKerningFactor (0.24f));
        g.drawText ("SAMPLER", bin, juce::Justification::centredRight);
    }

    // 4. Mode-specific labels.
    if (mode == Mode::Fx)
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
        g.setFont (ShardColours::monoFont (11.0f, true).withExtraKerningFactor (0.06f));
        auto lab = [&g] (juce::Slider& s, const char* t)
        {
            auto r = s.getBounds();
            g.drawText (t, r.getX() - 66, r.getY(), 60, r.getHeight(), juce::Justification::centredLeft);
        };
        lab (pitchSlider, "PITCH"); lab (volSlider, "VOLUME");
        lab (startSlider, "START"); lab (endSlider, "END");
        lab (chokeSlider, "CHOKE");
    }
    else if (mode == Mode::Seq && ! seqPanelArea.isEmpty())
    {
        g.setColour (ShardColours::ink.withAlpha (0.9f));
        g.setFont (ShardColours::monoFont (11.0f, true).withExtraKerningFactor (0.14f));
        const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00B7);
        const int sp = juce::jmax (0, selectedPad);
        const juce::String t = "STEPS  " + dot + "  PAD " + juce::String (sp + 1)
                             + (padName[(size_t) sp].isNotEmpty() ? "   " + padName[(size_t) sp] : juce::String());
        g.drawText (t, seqPanelArea.reduced (12, 6).removeFromTop (16), juce::Justification::centredLeft);
    }
}

void MainComponent::layoutPadGrid (juce::Rectangle<int> area, int cols, int rows, int gap)
{
    juce::Grid grid;
    using Track = juce::Grid::TrackInfo;
    using Fr = juce::Grid::Fr;
    for (int c = 0; c < cols; ++c) grid.templateColumns.add (Track (Fr (1)));
    for (int r = 0; r < rows; ++r) grid.templateRows.add (Track (Fr (1)));
    grid.setGap (juce::Grid::Px ((float) gap));
    for (auto* p : pads) grid.items.add (juce::GridItem (*p));
    grid.performLayout (area);
}

void MainComponent::resized()
{
    fxPanelArea = seqPanelArea = editPanelArea = editCtrlArea = {};

    auto area = getLocalBounds().reduced (8);

    // --- Always-visible top chrome ---
    headerArea = area.removeFromTop (30);
    area.removeFromTop (8);

    screenBezel = area.removeFromTop (82);
    spectrum.setBounds (screenBezel);
    area.removeFromTop (8);

    // Tab bar.
    tabBarArea = area.removeFromTop (36);
    {
        auto row = tabBarArea;
        const int w = row.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
            tabButtons[i]->setBounds ((i < 3 ? row.removeFromLeft (w) : row).reduced (2));
    }
    area.removeFromTop (6);

    // Transport: LOAD | PLAY | REC (always).
    {
        auto row = area.removeFromTop (38);
        const int w = row.getWidth() / 3;
        loadButton.setBounds (row.removeFromLeft (w).reduced (2));
        playButton.setBounds (row.removeFromLeft (w).reduced (2));
        recButton.setBounds  (row.reduced (2));
    }
    area.removeFromTop (8);

    // Status pinned to the bottom.
    status.setBounds (area.removeFromBottom (18));
    area.removeFromBottom (6);

    // --- Mode body ---
    if (mode == Mode::Perform)
    {
        layoutPadGrid (area, 4, 4, 6);
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
    else // Edit or Seq: compact pad selector on top, panel below.
    {
        auto padArea = area.removeFromTop ((int) (area.getHeight() * (mode == Mode::Edit ? 0.40f : 0.34f)));
        layoutPadGrid (padArea, 4, 4, 5);
        area.removeFromTop (8);

        if (mode == Mode::Edit)
        {
            editPanelArea = area;
            auto inner = area.reduced (10, 8);
            editLabel.setBounds (inner.removeFromTop (18));
            inner.removeFromTop (2);
            waveform.setBounds (inner.removeFromTop (60));
            inner.removeFromTop (8);
            editCtrlArea = inner;                        // labels drawn in paint

            const int labelW = 64;
            auto ctrlRow = [&inner, labelW] (int h) { auto r = inner.removeFromTop (h); r.removeFromLeft (labelW); return r; };
            pitchSlider.setBounds (ctrlRow (26)); inner.removeFromTop (4);
            volSlider.setBounds   (ctrlRow (26)); inner.removeFromTop (4);
            startSlider.setBounds (ctrlRow (26)); inner.removeFromTop (4);
            endSlider.setBounds   (ctrlRow (26)); inner.removeFromTop (6);
            chokeSlider.setBounds (ctrlRow (26)); inner.removeFromTop (6);
            {
                auto rr = inner.removeFromTop (30);
                reverseButton.setBounds (rr.removeFromLeft (rr.getWidth() / 2).reduced (3, 0));
                loopButton.setBounds (rr.reduced (3, 0));
            }
        }
        else // Seq
        {
            seqPanelArea = area;
            auto inner = area.reduced (10, 8);
            inner.removeFromTop (18);                     // title drawn in paint
            auto bottom = inner.removeFromBottom (30);
            bpmSlider.setBounds (bottom.removeFromLeft ((int) (bottom.getWidth() * 0.66f)).reduced (2, 0));
            clearButton.setBounds (bottom.reduced (3, 0));
            inner.removeFromBottom (8);

            // Keep the step pad roughly square — cap its height, top-aligned.
            auto steps = inner.removeFromTop (juce::jmin (inner.getHeight(), 170));
            const int rowH = steps.getHeight() / 2;
            for (int r = 0; r < 2; ++r)
            {
                auto row = (r == 0 ? steps.removeFromTop (rowH) : steps);
                const int w = row.getWidth() / 8;
                for (int c = 0; c < 8; ++c)
                {
                    const int idx = r * 8 + c;
                    stepButtons[idx]->setBounds ((c < 7 ? row.removeFromLeft (w) : row).reduced (3));
                }
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
    if (selectedPad < 0) return;
    const bool nv = ! pattern[(size_t) step][(size_t) selectedPad];
    pattern[(size_t) step][(size_t) selectedPad] = nv;
    engine.setStep (step, selectedPad, nv);
}

void MainComponent::refreshPad (int index)
{
    if (auto* p = pads[index])
    {
        const bool has = padHasSample[(size_t) index];
        auto base = has ? ShardColours::padTop : ShardColours::padBg2;
        if (index == selectedPad) base = base.darker (0.06f);
        const float f = padFlash[(size_t) index];
        if (f > 0.0f) base = base.interpolatedWith (ShardColours::amber, juce::jlimit (0.0f, 1.0f, f));
        p->setColour (juce::TextButton::buttonColourId, base);
        p->setColour (juce::TextButton::textColourOffId,
                      has ? ShardColours::amberDim : ShardColours::inkDim.withAlpha (0.55f));
        p->getProperties().set ("fn", has ? padName[(size_t) index] : juce::String());
    }
}

void MainComponent::selectPad (int index)
{
    selectedPad = index;
    updateControlsFromPad (index);
    waveform.setSample (uiSample[(size_t) index]);
    waveform.setTrim (padStart01[(size_t) index], padEnd01[(size_t) index]);
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

    selectPad (index);
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

    // Feed the spectrum screen + readout.
    juce::String rd = (selectedPad >= 0) ? ("PAD " + juce::String (selectedPad + 1)) : juce::String ("SHARD");
    const int ps = engine.getPlayStep();
    if (engine.isPlaying() && ps >= 0) rd += "   STEP " + juce::String (ps + 1);
    if (recordingActive)               rd = "REC " + juce::String (engine.getRecordSeconds(), 1) + "s";
    spectrum.setReadout (rd);
    spectrum.setBpm (bpmSlider.getValue());
    engine.copyScope (scopeTmp, 1024);
    spectrum.setSamples (scopeTmp, 1024);

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

    // Sequencer step colours + playhead (reuse ps from above).
    for (int s = 0; s < kNumSteps; ++s)
    {
        const bool on = (selectedPad >= 0) && pattern[(size_t) s][(size_t) selectedPad];
        auto col = on ? kPadLoaded : kStepOff;
        if (s == ps) col = col.brighter (0.7f);
        stepButtons[s]->setColour (juce::TextButton::buttonColourId, col);
    }
    lastPlayStep = ps;

    if (recordingActive)
        status.setText ("Recording pad " + juce::String (recordingSlot + 1)
                        + "  " + juce::String (engine.getRecordSeconds(), 1) + "s",
                        juce::dontSendNotification);
}
