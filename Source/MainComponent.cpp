#include "MainComponent.h"

namespace
{
    const juce::Colour kBg        = ShardColours::chassis;
    const juce::Colour kPadEmpty  = ShardColours::padTop;
    const juce::Colour kPadLoaded = ShardColours::amber;
    const juce::Colour kAccent    = ShardColours::amber;
    const juce::Colour kRec       { 0xffd0433a };
    const juce::Colour kStepOff   = ShardColours::panel;

    void styleButton (juce::TextButton& b, juce::Colour c)
    {
        b.setColour (juce::TextButton::buttonColourId, c);
        b.setColour (juce::TextButton::textColourOffId, juce::Colours::white.withAlpha (0.9f));
        b.setColour (juce::TextButton::textColourOnId,  juce::Colours::white);
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

    styleButton (testButton, juce::Colour (0xff394150));
    testButton.onClick = [this] { engine.postTestTone(); status.setText ("Test tone", juce::dontSendNotification); };
    addAndMakeVisible (testButton);

    styleButton (recButton, juce::Colour (0xff394150));
    recButton.onClick = [this] { toggleRecording(); };
    addAndMakeVisible (recButton);

    playButton.setClickingTogglesState (true);
    styleButton (playButton, juce::Colour (0xff394150));
    playButton.setColour (juce::TextButton::buttonOnColourId, kPadLoaded);
    playButton.onClick = [this]
    {
        const bool on = playButton.getToggleState();
        engine.setPlaying (on);
        playButton.setButtonText (on ? "STOP" : "PLAY");
    };
    addAndMakeVisible (playButton);

    styleButton (clearButton, juce::Colour (0xff394150));
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
    styleButton (fxTypeButton, juce::Colour (0xff394150));
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

    editLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    editLabel.setText ("select a pad", juce::dontSendNotification);
    addAndMakeVisible (editLabel);

    status.setJustificationType (juce::Justification::centred);
    status.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));
    status.setText ("Tap empty pad = load   /   lit pad = play   /   REC = mic", juce::dontSendNotification);
    addAndMakeVisible (status);

    startTimer (60);
    setSize (480, 1040);
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
    g.fillAll (kBg);

    // Knob names above each FX knob.
    g.setColour (ShardColours::amber.withAlpha (0.7f));
    g.setFont (juce::Font (juce::FontOptions (10.0f)).withExtraKerningFactor (0.1f));
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
    auto area = getLocalBounds().reduced (10);

    // The screen (spectrum) on top.
    spectrum.setBounds (area.removeFromTop (92));
    area.removeFromTop (6);
    status.setBounds (area.removeFromTop (20));
    area.removeFromTop (4);

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
    area.removeFromTop (2);

    // FX knob row (labels drawn in paint over each knob).
    {
        auto row = area.removeFromTop (78);
        row.removeFromTop (12);                         // gap for knob names
        juce::Slider* knobs[6] = { &cutoffSlider, &resoSlider, &driveSlider,
                                   &dlyTimeSlider, &dlyFbSlider, &dlyMixSlider };
        const int w = row.getWidth() / 6;
        for (auto* k : knobs) k->setBounds (row.removeFromLeft (w).reduced (2, 0));
    }
    bpmSlider.setBounds (area.removeFromTop (24));
    area.removeFromTop (6);

    // Bottom-up: sequencer, per-pad controls, waveform.
    {
        auto row = area.removeFromBottom (32);
        const int w = row.getWidth() / kNumSteps;
        for (int s = 0; s < kNumSteps; ++s)
            stepButtons[s]->setBounds (row.removeFromLeft (w).reduced (1));
    }
    area.removeFromBottom (6);
    {
        auto row = area.removeFromBottom (28);
        reverseButton.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2));
        loopButton.setBounds (row.reduced (2));
    }
    chokeSlider.setBounds (area.removeFromBottom (24));
    {
        auto row = area.removeFromBottom (24);
        startSlider.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2, 0));
        endSlider.setBounds (row.reduced (2, 0));
    }
    {
        auto row = area.removeFromBottom (24);
        pitchSlider.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2, 0));
        volSlider.setBounds (row.reduced (2, 0));
    }
    editLabel.setBounds (area.removeFromBottom (18));
    waveform.setBounds  (area.removeFromBottom (58));
    area.removeFromBottom (6);

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
        auto base = padHasSample[(size_t) index] ? kPadLoaded.withBrightness (0.55f) : kPadEmpty;
        if (index == selectedPad) base = base.brighter (0.25f);
        const float f = padFlash[(size_t) index];
        if (f > 0.0f) base = base.interpolatedWith (ShardColours::amber, juce::jlimit (0.0f, 1.0f, f));
        p->setColour (juce::TextButton::buttonColourId, base);
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
        styleButton (recButton, juce::Colour (0xff394150));
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
