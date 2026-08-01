#include "MainComponent.h"

namespace
{
    const juce::Colour kPadEmpty  = ShardColours::padTop;
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

    // Transport / actions.
    loadButton.setClickingTogglesState (true);
    styleButton (loadButton, kPadEmpty);
    loadButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
    loadButton.onClick = [this] { loadMode = loadButton.getToggleState(); };
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
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
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
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 15);
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

    fxLabel.setColour (juce::Label::textColourId, kAccent.withAlpha (0.9f));
    fxLabel.setText ("FX", juce::dontSendNotification);
    addAndMakeVisible (fxLabel);

    addAndMakeVisible (waveform);

    editLabel.setColour (juce::Label::textColourId, ShardColours::inkDim);
    editLabel.setText ("select a pad", juce::dontSendNotification);
    addAndMakeVisible (editLabel);

    status.setJustificationType (juce::Justification::centred);
    status.setColour (juce::Label::textColourId, ShardColours::inkDim);
    status.setText ("Tap empty pad = load   /   lit pad = play   /   REC = mic", juce::dontSendNotification);
    addAndMakeVisible (status);

    startTimer (60);
    setSize (500, 1080);
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

    // 4. Knob names above each FX knob.
    g.setColour (ShardColours::inkDim);
    g.setFont (ShardColours::monoFont (9.0f, true).withExtraKerningFactor (0.14f));
    auto name = [&g] (juce::Slider& s, const char* t)
    {
        auto r = s.getBounds();
        g.drawText (t, r.getX() - 4, r.getY() - 13, r.getWidth() + 8, 12, juce::Justification::centred);
    };
    name (cutoffSlider, "CUTOFF"); name (resoSlider, "RESO");  name (driveSlider, "DRIVE");
    name (dlyTimeSlider, "TIME");  name (dlyFbSlider, "FBK");  name (dlyMixSlider, "MIX");
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (8);

    // Header (wordmark + badge drawn in paint).
    headerArea = area.removeFromTop (30);
    area.removeFromTop (10);

    // The screen (scope) inside a recessed bezel.
    screenBezel = area.removeFromTop (94);
    spectrum.setBounds (screenBezel);
    area.removeFromTop (10);

    status.setBounds (area.removeFromTop (18));
    area.removeFromTop (6);

    // Transport row (6 items incl. the filter type toggle).
    {
        auto row = area.removeFromTop (38);
        const int w = row.getWidth() / 6;
        loadButton.setBounds  (row.removeFromLeft (w).reduced (2));
        testButton.setBounds  (row.removeFromLeft (w).reduced (2));
        recButton.setBounds   (row.removeFromLeft (w).reduced (2));
        playButton.setBounds  (row.removeFromLeft (w).reduced (2));
        clearButton.setBounds (row.removeFromLeft (w).reduced (2));
        fxTypeButton.setBounds (row.reduced (2));
    }
    area.removeFromTop (8);

    // FX panel: knob row + BPM grouped in one raised panel.
    {
        fxPanelArea = area.removeFromTop (104);
        auto inner  = fxPanelArea.reduced (8, 6);
        auto krow   = inner.removeFromTop (68);
        krow.removeFromTop (12);                         // gap for knob names
        juce::Slider* knobs[6] = { &cutoffSlider, &resoSlider, &driveSlider,
                                   &dlyTimeSlider, &dlyFbSlider, &dlyMixSlider };
        const int w = krow.getWidth() / 6;
        for (auto* k : knobs) k->setBounds (krow.removeFromLeft (w).reduced (2, 0));
        inner.removeFromTop (4);
        bpmSlider.setBounds (inner.removeFromTop (22));
    }
    area.removeFromTop (10);

    // Sequencer panel.
    {
        seqPanelArea = area.removeFromBottom (42);
        auto row = seqPanelArea.reduced (8, 6);
        const int w = row.getWidth() / kNumSteps;
        for (int s = 0; s < kNumSteps; ++s)
            stepButtons[s]->setBounds (row.removeFromLeft (w).reduced (1));
    }
    area.removeFromBottom (8);

    // Per-pad edit panel (waveform + controls grouped).
    {
        editPanelArea = area.removeFromBottom (196);
        auto inner = editPanelArea.reduced (8, 8);
        waveform.setBounds  (inner.removeFromTop (56));
        inner.removeFromTop (4);
        editLabel.setBounds (inner.removeFromTop (16));
        {
            auto row = inner.removeFromTop (24);
            pitchSlider.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2, 0));
            volSlider.setBounds (row.reduced (2, 0));
        }
        {
            auto row = inner.removeFromTop (24);
            startSlider.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2, 0));
            endSlider.setBounds (row.reduced (2, 0));
        }
        chokeSlider.setBounds (inner.removeFromTop (24));
        {
            auto row = inner.removeFromTop (28);
            reverseButton.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2));
            loopButton.setBounds (row.reduced (2));
        }
    }
    area.removeFromBottom (10);

    // Middle: pad grid.
    juce::Grid grid;
    using Track = juce::Grid::TrackInfo;
    using Fr = juce::Grid::Fr;
    grid.templateColumns = { Track (Fr (1)), Track (Fr (1)), Track (Fr (1)), Track (Fr (1)) };
    grid.templateRows    = { Track (Fr (1)), Track (Fr (1)), Track (Fr (1)), Track (Fr (1)) };
    grid.setGap (juce::Grid::Px (6));
    for (auto* p : pads) grid.items.add (juce::GridItem (*p));
    grid.performLayout (area);
}

void MainComponent::padClicked (int index)
{
    if (loadMode || ! padHasSample[(size_t) index])
    {
        openChooserForPad (index);
        return;
    }
    engine.postNoteOn (index);
    selectPad (index);
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
                      has ? ShardColours::amberDim : ShardColours::inkDim.withAlpha (0.4f));
    }
}

void MainComponent::selectPad (int index)
{
    selectedPad = index;
    updateControlsFromPad (index);
    waveform.setSample (uiSample[(size_t) index]);
    waveform.setTrim (padStart01[(size_t) index], padEnd01[(size_t) index]);
    editLabel.setText ("Editing pad " + juce::String (index + 1), juce::dontSendNotification);
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

void MainComponent::assignSampleToPad (int index, SampleBuffer::Ptr sb)
{
    if (sb == nullptr) return;
    padHasSample[(size_t) index] = true;
    uiSample[(size_t) index]     = sb;
    padStart01[(size_t) index]   = 0.0f;
    padEnd01[(size_t) index]     = 1.0f;

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
        loader.loadAsync (url, index, [this, index] (bool ok, juce::String detail, SampleBuffer::Ptr sb)
        {
            if (ok)
            {
                assignSampleToPad (index, sb);
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
            assignSampleToPad (recordingSlot, sb);
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
