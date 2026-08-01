#include "MainComponent.h"

namespace
{
    const juce::Colour kBg        { 0xff15171c };
    const juce::Colour kPadEmpty  { 0xff262a33 };
    const juce::Colour kPadLoaded { 0xff1fb6a6 };
    const juce::Colour kAccent    { 0xffe0a13a };
    const juce::Colour kRec       { 0xffd0433a };
    const juce::Colour kStepOff   { 0xff2a2f3a };

    void styleButton (juce::TextButton& b, juce::Colour c)
    {
        b.setColour (juce::TextButton::buttonColourId, c);
        b.setColour (juce::TextButton::textColourOffId, juce::Colours::white.withAlpha (0.9f));
        b.setColour (juce::TextButton::textColourOnId,  juce::Colours::white);
    }
}

MainComponent::MainComponent()
{
    // 1 input (mic, for recording) + 2 outputs. Requests RECORD_AUDIO on Android.
    setAudioChannels (1, 2);

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

    cutoffSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    cutoffSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    cutoffSlider.setRange (20.0, 20000.0, 1.0);
    cutoffSlider.setSkewFactorFromMidPoint (1000.0);
    cutoffSlider.setValue (20000.0, juce::dontSendNotification);
    cutoffSlider.setTextValueSuffix (" Hz");
    cutoffSlider.setColour (juce::Slider::trackColourId, kAccent);
    cutoffSlider.onValueChange = [this] { engine.setFxCutoff ((float) cutoffSlider.getValue()); };
    addAndMakeVisible (cutoffSlider);

    resoSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    resoSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20);
    resoSlider.setRange (0.3, 4.0, 0.01);
    resoSlider.setValue (0.707, juce::dontSendNotification);
    resoSlider.setColour (juce::Slider::trackColourId, kAccent);
    resoSlider.onValueChange = [this] { engine.setFxReso ((float) resoSlider.getValue()); };
    addAndMakeVisible (resoSlider);

    driveSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    driveSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20);
    driveSlider.setRange (0.0, 1.0, 0.01);
    driveSlider.setValue (0.0, juce::dontSendNotification);
    driveSlider.setColour (juce::Slider::trackColourId, kAccent);
    driveSlider.onValueChange = [this] { engine.setFxDrive ((float) driveSlider.getValue()); };
    addAndMakeVisible (driveSlider);

    auto initFxSlider = [this] (juce::Slider& s, double lo, double hi, double step, double def, const juce::String& suffix)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
        s.setRange (lo, hi, step);
        s.setValue (def, juce::dontSendNotification);
        s.setTextValueSuffix (suffix);
        s.setColour (juce::Slider::trackColourId, kAccent);
        addAndMakeVisible (s);
    };
    initFxSlider (dlyTimeSlider, 20.0, 1000.0, 1.0, 250.0, " ms");
    initFxSlider (dlyFbSlider,    0.0, 0.95, 0.01, 0.35, "");
    initFxSlider (dlyMixSlider,   0.0, 1.0,  0.01, 0.0,  "");
    dlyTimeSlider.onValueChange = [this] { engine.setDlyTime ((float) dlyTimeSlider.getValue()); };
    dlyFbSlider.onValueChange   = [this] { engine.setDlyFb   ((float) dlyFbSlider.getValue()); };
    dlyMixSlider.onValueChange  = [this] { engine.setDlyMix  ((float) dlyMixSlider.getValue()); };

    fxLabel.setColour (juce::Label::textColourId, kAccent.withAlpha (0.9f));
    fxLabel.setText ("MASTER FX  ·  filter + drive + delay", juce::dontSendNotification);
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
    setSize (480, 900);
}

MainComponent::~MainComponent()
{
    shutdownAudio();
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

void MainComponent::paint (juce::Graphics& g) { g.fillAll (kBg); }

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (10);

    status.setBounds (area.removeFromTop (22));
    area.removeFromTop (4);

    {
        auto row = area.removeFromTop (40);
        const int w = row.getWidth() / 5;
        loadButton.setBounds (row.removeFromLeft (w).reduced (2));
        testButton.setBounds (row.removeFromLeft (w).reduced (2));
        recButton.setBounds  (row.removeFromLeft (w).reduced (2));
        playButton.setBounds (row.removeFromLeft (w).reduced (2));
        clearButton.setBounds (row.reduced (2));
    }
    bpmSlider.setBounds (area.removeFromTop (28));
    area.removeFromTop (6);

    // Master FX section.
    {
        auto r = area.removeFromTop (20);
        fxTypeButton.setBounds (r.removeFromRight (66).reduced (1));
        fxLabel.setBounds (r);
    }
    cutoffSlider.setBounds (area.removeFromTop (24));
    {
        auto r = area.removeFromTop (24);
        resoSlider.setBounds (r.removeFromLeft (r.getWidth() / 2).reduced (2, 0));
        driveSlider.setBounds (r.reduced (2, 0));
    }
    dlyTimeSlider.setBounds (area.removeFromTop (24));
    {
        auto r = area.removeFromTop (24);
        dlyFbSlider.setBounds (r.removeFromLeft (r.getWidth() / 2).reduced (2, 0));
        dlyMixSlider.setBounds (r.reduced (2, 0));
    }
    area.removeFromTop (6);

    // Bottom-up: sequencer, controls, waveform.
    {
        auto row = area.removeFromBottom (34);
        const int w = row.getWidth() / kNumSteps;
        for (int s = 0; s < kNumSteps; ++s)
            stepButtons[s]->setBounds (row.removeFromLeft (w).reduced (1));
    }
    area.removeFromBottom (6);
    {
        auto row = area.removeFromBottom (30);
        reverseButton.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2));
        loopButton.setBounds (row.reduced (2));
    }
    chokeSlider.setBounds (area.removeFromBottom (26));
    endSlider.setBounds   (area.removeFromBottom (26));
    startSlider.setBounds (area.removeFromBottom (26));
    volSlider.setBounds   (area.removeFromBottom (26));
    pitchSlider.setBounds (area.removeFromBottom (26));
    editLabel.setBounds   (area.removeFromBottom (20));
    waveform.setBounds    (area.removeFromBottom (70));
    area.removeFromBottom (8);

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
        auto base = padHasSample[(size_t) index] ? kPadLoaded : kPadEmpty;
        if (index == selectedPad) base = base.brighter (0.4f);
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

    // Sequencer step colours + playhead.
    const int ps = engine.getPlayStep();
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
