#include "MainComponent.h"

namespace
{
    // References, not copies: the accent tokens are mutable (skins).
    const juce::Colour& kPadLoaded = ShardColours::amber;
    const juce::Colour& kAccent    = ShardColours::amber;
    const juce::Colour  kRec       = ShardColours::red;
    const juce::Colour  kStepOff   = ShardColours::key;
    const juce::Colour  kKey       = ShardColours::key;

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
        // Not static: accent is skin-mutable, so re-read it on every call.
        const juce::Colour primaries[3] = { ShardColours::accent, ShardColours::red, ShardColours::yellow };
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
        seqSheet.addAndMakeVisible (b);
        stepButtons.add (b);
    }

    // Module bar — rule of three: PADS / SEC / FX, one floating sheet each.
    // Nothing ever replaces the machine face; CHOP is a button inside the
    // PADS sheet and the pattern chain is a row inside the SEC sheet.
    {
        juce::TextButton* mb[3]  = { &padsButton, &secButton, &fxOpenButton };
        Sheet*            sh[3]  = { &padSheet, &seqSheet, &fxSheet };
        for (int i = 0; i < 3; ++i)
        {
            styleButton (*mb[i], kKey);
            mb[i]->setColour (juce::TextButton::buttonOnColourId, kAccent);
            auto* s = sh[i]; auto* b = mb[i];
            b->onClick = [this, s, b] { if (s->isVisible()) closeAllSheets(); else openSheet (*s, *b); };
            addAndMakeVisible (b);
        }

        juce::TextButton* cb[3] = { &padCloseButton, &seqCloseButton, &fxCloseButton };
        std::function<void (juce::Graphics&)> pc[3] =
        {
            [this] (juce::Graphics& g) { paintPadSheetContent (g); },
            [this] (juce::Graphics& g) { paintSeqSheetContent (g); },
            [this] (juce::Graphics& g) { paintFxSheetContent (g); },
        };
        for (int i = 0; i < 3; ++i)
        {
            auto* s = sh[i];
            addAndMakeVisible (s);
            s->setVisible (false);
            s->onDismiss = [this] { closeAllSheets(); };
            s->paintContent = pc[i];
            styleButton (*cb[i], kKey);
            cb[i]->onClick = [this] { closeAllSheets(); };
            s->addAndMakeVisible (cb[i]);
        }
    }

    // Sample browser sheet — no module button of its own: it is opened by the
    // LOAD flow (arm LOAD, tap a pad) and targets that pad.
    {
        addAndMakeVisible (browseSheet);
        browseSheet.setVisible (false);
        browseSheet.onDismiss = [this] { closeAllSheets(); };
        browseSheet.paintContent = [this] (juce::Graphics& g) { paintBrowseSheetContent (g); };

        browseFilter = std::make_unique<juce::WildcardFileFilter> (
            "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3", "*", "Muestras de audio");

        auto start = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
        if (! start.isDirectory())
            start = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

        browser = std::make_unique<juce::FileBrowserComponent> (
            juce::FileBrowserComponent::openMode
          | juce::FileBrowserComponent::canSelectFiles
          | juce::FileBrowserComponent::filenameBoxIsReadOnly,   // no keyboard on mobile
            start, browseFilter.get(), nullptr);
        browser->addListener (this);
        browseSheet.addAndMakeVisible (*browser);

        styleButton (browseCloseButton, kKey);
        browseCloseButton.onClick = [this] { closeAllSheets(); };
        browseSheet.addAndMakeVisible (browseCloseButton);

        styleButton (browseLoadButton, kAccent);
        browseLoadButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        browseLoadButton.onClick = [this] { loadBrowserSelection(); };
        browseSheet.addAndMakeVisible (browseLoadButton);
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
    seqSheet.addAndMakeVisible (clearButton);

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
    seqSheet.addAndMakeVisible (bpmSlider);   // lives in the sequencer sheet, not the main tabs

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
        s.setDoubleClickReturnValue (true, def);     // double-tap = back to default
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
        repaint (editInfoArea.expanded (4));
    };
    endSlider.onValueChange = [this]
    {
        if (selectedPad < 0) return;
        double v = juce::jmax (endSlider.getValue(), startSlider.getValue() + 0.01);
        padEnd01[(size_t) selectedPad] = (float) v;
        const int len = engine.getSampleLength (selectedPad);
        engine.setPadEnd (selectedPad, (int) (v * len));
        waveform.setTrim (padStart01[(size_t) selectedPad], (float) v);
        repaint (editInfoArea.expanded (4));
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
        seqSheet.repaint();   // sheet card itself can grow/shrink with the bank's LEN
    };
    seqSheet.addAndMakeVisible (patternSlider);

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
        seqSheet.repaint();   // sheet card grows/shrinks with LEN
    };
    seqSheet.addAndMakeVisible (lengthSlider);

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
        seqSheet.addAndMakeVisible (b);
        patternButtons.add (b);
    }

    styleButton (chainClearButton, kKey);
    chainClearButton.onClick = [this]
    {
        patternActiveUI.fill (false);
        for (auto* b : patternButtons) b->setToggleState (false, juce::dontSendNotification);
        rebuildChain();
    };
    seqSheet.addAndMakeVisible (chainClearButton);

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
    seqSheet.addAndMakeVisible (noteSlider);

    // Master FX (filter + drive).
    fxTypeButton.setClickingTogglesState (true);
    styleButton (fxTypeButton, kKey);
    fxTypeButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
    fxTypeButton.onClick = [this]
    {
        const bool hp = fxTypeButton.getToggleState();
        fxTypeButton.setButtonText (hp ? "HPF" : "LPF");
        engine.setFxType (hp ? 1 : 0);
        fxSheet.repaint();
        refreshMacroValues();
    };
    fxSheet.addAndMakeVisible (fxTypeButton);

    // FX as rotary KNOBS (vintage identity).
    initKnob (cutoffSlider, 20.0, 20000.0, 1.0, 20000.0, 1000.0, [this] { engine.setFxCutoff ((float) cutoffSlider.getValue()); fxSheet.repaint(); refreshMacroValues(); });
    initKnob (resoSlider,    0.3,  4.0, 0.01, 0.707, 0.0,       [this] { engine.setFxReso   ((float) resoSlider.getValue()); fxSheet.repaint(); refreshMacroValues(); });
    initKnob (driveSlider,   0.0,  1.0, 0.01, 0.0,   0.0,       [this] { engine.setFxDrive  ((float) driveSlider.getValue()); refreshMacroValues(); });
    initKnob (dlyTimeSlider, 20.0, 1000.0, 1.0, 250.0, 0.0,     [this] { engine.setDlyTime  ((float) dlyTimeSlider.getValue()); refreshMacroValues(); });
    initKnob (dlyFbSlider,   0.0,  0.95, 0.01, 0.35, 0.0,       [this] { engine.setDlyFb    ((float) dlyFbSlider.getValue()); refreshMacroValues(); });
    initKnob (dlyMixSlider,  0.0,  1.0, 0.01, 0.0,   0.0,       [this] { engine.setDlyMix   ((float) dlyMixSlider.getValue()); refreshMacroValues(); });

    // CTRL 1-3: context-sensitive macro knobs. Which parameters they touch
    // depends on the active bank (FILTRO / DELAY / PAD) — groovebox style,
    // three big knobs that are always the three most useful ones.
    initKnob (macroCtrl1, 0.0, 1.0, 0.001, 0.0, 0.0, [this] { macroMoved (0); });
    initKnob (macroCtrl2, 0.0, 1.0, 0.001, 0.0, 0.0, [this] { macroMoved (1); });
    initKnob (macroCtrl3, 0.0, 1.0, 0.001, 0.0, 0.0, [this] { macroMoved (2); });

    {
        const char* bankNames[3] = { "FILTRO", "DELAY", "PAD" };
        for (int i = 0; i < 3; ++i)
        {
            auto* b = new juce::TextButton (bankNames[i]);
            styleButton (*b, kKey);
            b->setColour (juce::TextButton::buttonOnColourId, kAccent);
            b->onClick = [this, i] { setMacroBank (i); };
            addAndMakeVisible (b);
            macroBankBtns.add (b);
        }
    }

    addAndMakeVisible (waveform);

    // COLORS badge in the header: taps cycle the 4 accent skins.
    skinButton.setColour (juce::TextButton::buttonColourId, ShardColours::screenBg);
    skinButton.setColour (juce::TextButton::textColourOffId, ShardColours::accentBright);
    skinButton.setColour (juce::TextButton::textColourOnId,  ShardColours::accentBright);
    skinButton.onClick = [this]
    {
        ShardColours::setSkin (ShardColours::currentSkin + 1);
        applySkin();
    };
    addAndMakeVisible (skinButton);

    // Controls live inside their sheets, not on the machine face.
    for (juce::Component* c : { (juce::Component*) &pitchSlider, (juce::Component*) &volSlider, (juce::Component*) &panSlider,
                                (juce::Component*) &attackSlider, (juce::Component*) &releaseSlider, (juce::Component*) &chokeSlider,
                                (juce::Component*) &startSlider, (juce::Component*) &endSlider,
                                (juce::Component*) &reverseButton, (juce::Component*) &loopButton })
        padSheet.addAndMakeVisible (c);
    padSheet.addAndMakeVisible (chopButton);
    for (juce::Component* c : { (juce::Component*) &cutoffSlider, (juce::Component*) &resoSlider, (juce::Component*) &driveSlider,
                                (juce::Component*) &dlyTimeSlider, (juce::Component*) &dlyFbSlider, (juce::Component*) &dlyMixSlider,
                                (juce::Component*) &testButton })
        fxSheet.addAndMakeVisible (c);

    status.setJustificationType (juce::Justification::centred);
    status.setColour (juce::Label::textColourId, ShardColours::inkDim);
    status.setText ("Tap a pad to play", juce::dontSendNotification);
    addAndMakeVisible (status);

    startTimer (60);
    setSize (500, 1080);
    setMacroBank (0);
    applySkin();
}

// Restyle everything that captured accent-coloured values at construction —
// the rest of the UI reads ShardColours at paint time and only needs repaint.
void MainComponent::applySkin()
{
    const auto acc = ShardColours::accent;
    // Lit-state text must stay legible on a dark accent (TINTA skin).
    const auto onTxt = acc.getPerceivedBrightness() < 0.5f ? ShardColours::inkLight : ShardColours::ink;

    juce::TextButton* accented[] = { &padsButton, &secButton, &fxOpenButton,
                                     &loadButton, &fxTypeButton };
    for (auto* b : accented)
    {
        b->setColour (juce::TextButton::buttonOnColourId, acc);
        b->setColour (juce::TextButton::textColourOnId, onTxt);
    }
    for (auto* b : macroBankBtns)
    {
        b->setColour (juce::TextButton::buttonOnColourId, acc);
        b->setColour (juce::TextButton::textColourOnId, onTxt);
    }
    for (int i = 0; i < patternButtons.size(); ++i)
        patternButtons[i]->setColour (juce::TextButton::buttonOnColourId, patternRowColour (i));

    styleButton (playButton, acc);
    playButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    playButton.setColour (juce::TextButton::textColourOnId,  juce::Colours::white);
    playButton.setColour (juce::TextButton::buttonOnColourId, ShardColours::accentDim);

    juce::Slider* tracks[] = { &startSlider, &endSlider, &bpmSlider, &patternSlider, &lengthSlider };
    for (auto* s : tracks)
        s->setColour (juce::Slider::trackColourId, acc);
    lnf.setColour (juce::Slider::trackColourId, acc);
    lnf.setColour (juce::Slider::thumbColourId, acc);
    lnf.applyBrowserColours();                     // the file list follows the skin too
    styleButton (browseLoadButton, acc);
    browseLoadButton.setColour (juce::TextButton::textColourOffId, onTxt);

    skinButton.setButtonText (juce::String ("COLORS ") + juce::String::charToString ((juce::juce_wchar) 0x00B7)
                              + " " + ShardColours::skinName (ShardColours::currentSkin));
    skinButton.setColour (juce::TextButton::textColourOffId, ShardColours::accentBright);
    skinButton.setColour (juce::TextButton::textColourOnId,  ShardColours::accentBright);

    repaint();
}

// --- Context-sensitive CTRL 1-3 ---------------------------------------------
// Bank 0 FILTRO: cutoff / reso / drive.  Bank 1 DELAY: time / feedback / mix.
// Bank 2 PAD: pitch / start / end of the selected pad.
void MainComponent::setMacroBank (int bank)
{
    macroBank = juce::jlimit (0, 2, bank);
    for (int i = 0; i < 3; ++i)
        if (auto* b = macroBankBtns[i]) b->setToggleState (i == macroBank, juce::dontSendNotification);

    auto config = [] (juce::Slider& s, double lo, double hi, double step, double skewMid, double def,
                      std::function<juce::String (double)> fmt)
    {
        s.setRange (lo, hi, step);
        if (skewMid > 0.0) s.setSkewFactorFromMidPoint (skewMid);
        else               s.setSkewFactor (1.0);
        s.setDoubleClickReturnValue (true, def);     // double-tap = bank default
        s.textFromValueFunction = std::move (fmt);
    };

    if (macroBank == 0)
    {
        config (macroCtrl1, 20.0, 20000.0, 1.0, 1000.0, 20000.0, [] (double v) { return v >= 1000.0 ? juce::String (v / 1000.0, 1) + "k" : juce::String ((int) v); });
        config (macroCtrl2, 0.3, 4.0, 0.01, 0.0, 0.707,          [] (double v) { return juce::String (v, 2); });
        config (macroCtrl3, 0.0, 1.0, 0.01, 0.0, 0.0,            [] (double v) { return juce::String (v, 2); });
    }
    else if (macroBank == 1)
    {
        config (macroCtrl1, 20.0, 1000.0, 1.0, 0.0, 250.0,       [] (double v) { return juce::String ((int) v) + " ms"; });
        config (macroCtrl2, 0.0, 0.95, 0.01, 0.0, 0.35,          [] (double v) { return juce::String (v, 2); });
        config (macroCtrl3, 0.0, 1.0, 0.01, 0.0, 0.0,            [] (double v) { return juce::String (v, 2); });
    }
    else
    {
        config (macroCtrl1, -24.0, 24.0, 1.0, 0.0, 0.0,          [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) v) + " st"; });
        config (macroCtrl2, 0.0, 1.0, 0.001, 0.0, 0.0,           [] (double v) { return juce::String (v, 3); });
        config (macroCtrl3, 0.0, 1.0, 0.001, 0.0, 1.0,           [] (double v) { return juce::String (v, 3); });
    }

    refreshMacroValues();
    repaint();   // the knob-name labels above CTRL 1-3 change with the bank
}

// Load the bank's current engine/pad values into the three knobs (silently).
void MainComponent::refreshMacroValues()
{
    auto set = [] (juce::Slider& s, double v) { s.setValue (v, juce::dontSendNotification); s.updateText(); };
    if (macroBank == 0)
    {
        set (macroCtrl1, cutoffSlider.getValue());
        set (macroCtrl2, resoSlider.getValue());
        set (macroCtrl3, driveSlider.getValue());
    }
    else if (macroBank == 1)
    {
        set (macroCtrl1, dlyTimeSlider.getValue());
        set (macroCtrl2, dlyFbSlider.getValue());
        set (macroCtrl3, dlyMixSlider.getValue());
    }
    else if (selectedPad >= 0)
    {
        set (macroCtrl1, padPitch[(size_t) selectedPad]);
        set (macroCtrl2, padStart01[(size_t) selectedPad]);
        set (macroCtrl3, padEnd01[(size_t) selectedPad]);
    }
}

void MainComponent::macroMoved (int idx)
{
    juce::Slider* ms[3] = { &macroCtrl1, &macroCtrl2, &macroCtrl3 };
    const double v = ms[idx]->getValue();

    if (macroBank == 0)
    {
        if (idx == 0) { cutoffSlider.setValue (v, juce::dontSendNotification); engine.setFxCutoff ((float) v); }
        if (idx == 1) { resoSlider.setValue   (v, juce::dontSendNotification); engine.setFxReso   ((float) v); }
        if (idx == 2) { driveSlider.setValue  (v, juce::dontSendNotification); engine.setFxDrive  ((float) v); }
    }
    else if (macroBank == 1)
    {
        if (idx == 0) { dlyTimeSlider.setValue (v, juce::dontSendNotification); engine.setDlyTime ((float) v); }
        if (idx == 1) { dlyFbSlider.setValue   (v, juce::dontSendNotification); engine.setDlyFb   ((float) v); }
        if (idx == 2) { dlyMixSlider.setValue  (v, juce::dontSendNotification); engine.setDlyMix  ((float) v); }
    }
    else if (selectedPad >= 0)
    {
        const int sp = selectedPad;
        if (idx == 0) { padPitch[(size_t) sp] = (float) v; pitchSlider.setValue (v, juce::dontSendNotification); engine.setPadPitch (sp, (float) v); }
        if (idx == 1) { startSlider.setValue (v, juce::sendNotification); }   // reuse its clamping + trim logic
        if (idx == 2) { endSlider.setValue   (v, juce::sendNotification); }
    }
}

// --- Sheets ------------------------------------------------------------------
void MainComponent::openSheet (Sheet& s, juce::TextButton& toggle)
{
    closeAllSheets();
    if (selectedPad < 0) selectPad (0);
    toggle.setToggleState (true, juce::dontSendNotification);
    s.setVisible (true);
    s.toFront (false);
    resized();
    repaint();
}

void MainComponent::closeAllSheets()
{
    juce::TextButton* mb[3] = { &padsButton, &secButton, &fxOpenButton };
    Sheet*            sh[3] = { &padSheet, &seqSheet, &fxSheet };
    for (int i = 0; i < 3; ++i)
    {
        mb[i]->setToggleState (false, juce::dontSendNotification);
        sh[i]->setVisible (false);
    }
    browseSheet.setVisible (false);
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

    // 2. Recessed LCD bezel around the scope (square, dark inset on a light face).
    if (! screenBezel.isEmpty())
    {
        auto r = screenBezel.toFloat();
        g.setColour (ShardColours::knobBody2);
        g.fillRoundedRectangle (r.expanded (3.0f), 3.0f);
        g.setColour (ShardColours::knobEdge.withAlpha (0.7f));
        g.drawRoundedRectangle (r.expanded (3.0f).reduced (0.5f), 3.0f, 1.2f);
    }

    // 3. Header: ARTiFACTS wordmark (the COLORS badge is now skinButton —
    //    a real button that cycles the 4 accent skins).
    if (! headerArea.isEmpty())
    {
        auto h = headerArea;
        g.setColour (ShardColours::ink);
        g.setFont (ShardColours::displayFont (22.0f).withExtraKerningFactor (0.10f));
        g.drawText ("ARTiFACTS", h.getX(), h.getY(), 180, h.getHeight(), juce::Justification::centredLeft);
    }

    // 4. Machine face: CTRL labels (bank-dependent), VU strip, step LEDs.
    {
        g.setColour (ShardColours::ink.withAlpha (0.85f));
        g.setFont (ShardColours::monoFont (10.0f, true).withExtraKerningFactor (0.16f));
        auto mn = [&g] (juce::Slider& s, const juce::String& t)
        {
            auto r = s.getBounds();
            g.drawText (t, r.getX() - 6, r.getY() - 14, r.getWidth() + 12, 12, juce::Justification::centred);
        };
        static const char* bankLabels[3][3] = { { "CUTOFF", "RESO", "DRIVE" },
                                                { "TIME",   "FBK",  "MIX"   },
                                                { "PITCH",  "START","END"   } };
        mn (macroCtrl1, bankLabels[macroBank][0]);
        mn (macroCtrl2, bankLabels[macroBank][1]);
        mn (macroCtrl3, bankLabels[macroBank][2]);

        // Stereo VU: two segmented LED rows (L/R) on a recessed strip.
        if (! vuArea.isEmpty())
        {
            auto scr = vuArea.toFloat();
            g.setColour (ShardColours::screenBg);
            g.fillRoundedRectangle (scr, 2.0f);

            auto in = vuArea.reduced (24, 3);
            const int nSeg = 28;
            const float segW = (float) in.getWidth() / (float) nSeg;

            g.setColour (ShardColours::lcdFg.withAlpha (0.55f));
            g.setFont (ShardColours::monoFont (8.0f, true));
            g.drawText ("L", vuArea.getX() + 6, in.getY() - 1, 12, in.getHeight() / 2, juce::Justification::centredLeft);
            g.drawText ("R", vuArea.getX() + 6, in.getCentreY(), 12, in.getHeight() / 2, juce::Justification::centredLeft);

            auto drawRow = [&] (float level, juce::Rectangle<float> row)
            {
                const int lit = (int) std::round (std::sqrt (juce::jlimit (0.0f, 1.0f, level)) * (float) nSeg);
                for (int i = 0; i < nSeg; ++i)
                {
                    const bool hot = i >= (int) (nSeg * 0.82f);
                    juce::Colour c = i < lit ? (hot ? ShardColours::red : ShardColours::accent)
                                             : ShardColours::lcdFg.withAlpha (0.10f);
                    g.setColour (c);
                    g.fillRect (juce::Rectangle<float> (row.getX() + (float) i * segW + 1.0f, row.getY(),
                                                        segW - 2.0f, row.getHeight()));
                }
            };
            auto rows = in.toFloat();
            auto top  = rows.removeFromTop (rows.getHeight() * 0.5f).reduced (0, 1.0f);
            auto bot  = rows.reduced (0, 1.0f);
            drawRow (vuL, top);
            drawRow (vuR, bot);
        }

        // Step LEDs: 16 segments, the playhead lit in the page's primary
        // colour — the beat stays visible without opening the SEC sheet.
        if (! stepStripArea.isEmpty())
        {
            auto scr = stepStripArea.toFloat();
            g.setColour (ShardColours::screenBg);
            g.fillRoundedRectangle (scr, 2.0f);

            auto in = stepStripArea.reduced (8, 4);
            const float segW = (float) in.getWidth() / 16.0f;
            const int ps = engine.getPlayStep();
            const int page = ps >= 0 ? ps / 16 : 0;
            const int cur  = ps >= 0 ? ps % 16 : -1;

            for (int i = 0; i < 16; ++i)
            {
                auto r = juce::Rectangle<float> (in.getX() + (float) i * segW + 1.5f, (float) in.getY(),
                                                 segW - 3.0f, (float) in.getHeight());
                if (i == cur)
                {
                    g.setColour (patternRowColour (page));
                    g.fillRoundedRectangle (r, 1.5f);
                }
                else
                {
                    g.setColour (ShardColours::lcdFg.withAlpha ((i % 4 == 0) ? 0.30f : 0.12f));
                    g.drawRoundedRectangle (r.reduced (0.5f), 1.5f, 1.0f);
                }
            }
        }
    }
}

// FX sheet: knob labels + the live filter response display.
void MainComponent::paintFxSheetContent (juce::Graphics& g)
{
    if (fxSheet.sheetBounds.isEmpty()) return;

    g.setColour (ShardColours::ink.withAlpha (0.9f));
    g.setFont (ShardColours::monoFont (11.0f, true).withExtraKerningFactor (0.14f));
    g.drawText ("FX", fxSheet.sheetBounds.reduced (14, 12).removeFromTop (16), juce::Justification::centredLeft);

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

        // Live filter response: a 2-pole magnitude curve on a recessed LCD,
        // redrawn as CUTOFF/RESO/LPF-HPF change — see the shape, not just Hz.
        if (! fxCurveArea.isEmpty())
        {
            auto scr = fxCurveArea.toFloat();
            g.setColour (ShardColours::knobBody2);
            g.fillRoundedRectangle (scr.expanded (3.0f), 3.0f);
            g.setColour (ShardColours::screenBg);
            g.fillRect (scr);

            auto plot = scr.reduced (10.0f, 14.0f);
            const float fLo = 20.0f, fHi = 20000.0f;
            const float dbTop = 24.0f, dbBot = -36.0f;
            auto xForF  = [&plot, fLo, fHi] (float f)  { return plot.getX() + plot.getWidth() * (std::log (f / fLo) / std::log (fHi / fLo)); };
            auto yForDb = [&plot, dbTop, dbBot] (float db) { return plot.getY() + plot.getHeight() * ((dbTop - db) / (dbTop - dbBot)); };

            // Grid: decades + 0 dB line, dim LCD green.
            g.setColour (ShardColours::lcdFg.withAlpha (0.18f));
            for (float f : { 100.0f, 1000.0f, 10000.0f })
                g.drawVerticalLine ((int) xForF (f), plot.getY(), plot.getBottom());
            g.drawHorizontalLine ((int) yForDb (0.0f), plot.getX(), plot.getRight());
            g.setColour (ShardColours::lcdFg.withAlpha (0.45f));
            g.setFont (ShardColours::monoFont (8.5f, true));
            g.drawText ("100",  (int) xForF (100.0f) - 14,   (int) plot.getBottom() + 1, 28, 10, juce::Justification::centred);
            g.drawText ("1K",   (int) xForF (1000.0f) - 14,  (int) plot.getBottom() + 1, 28, 10, juce::Justification::centred);
            g.drawText ("10K",  (int) xForF (10000.0f) - 14, (int) plot.getBottom() + 1, 28, 10, juce::Justification::centred);

            const float fc = juce::jmax (20.0f, (float) cutoffSlider.getValue());
            const float q  = juce::jmax (0.05f, (float) resoSlider.getValue());
            const bool  hp = fxTypeButton.getToggleState();

            juce::Path curve;
            const int n = juce::jmax (32, (int) plot.getWidth() / 2);
            for (int i = 0; i <= n; ++i)
            {
                const float f  = fLo * std::pow (fHi / fLo, (float) i / (float) n);
                const float r2 = (f / fc) * (f / fc);
                const float den = std::sqrt ((1.0f - r2) * (1.0f - r2) + r2 / (q * q));
                const float mag = (hp ? r2 : 1.0f) / juce::jmax (1.0e-6f, den);
                const float db  = juce::jlimit (dbBot, dbTop, 20.0f * std::log10 (juce::jmax (1.0e-6f, mag)));
                const float x = xForF (f), y = yForDb (db);
                if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
            }
            g.setColour (ShardColours::accent);
            g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved));

            // Cutoff marker + readout.
            g.setColour (ShardColours::yellow.withAlpha (0.8f));
            g.drawVerticalLine ((int) xForF (juce::jlimit (fLo, fHi, fc)), plot.getY(), plot.getBottom());
            g.setColour (ShardColours::lcdFg);
            g.setFont (ShardColours::monoFont (10.0f, true).withExtraKerningFactor (0.12f));
            const juce::String fcTxt = fc >= 1000.0f ? juce::String (fc / 1000.0f, 1) + " kHz" : juce::String ((int) fc) + " Hz";
            g.drawText ((hp ? "HPF  " : "LPF  ") + fcTxt + "   Q " + juce::String (q, 2),
                        (int) scr.getX() + 8, (int) scr.getY() + 3, (int) scr.getWidth() - 16, 12,
                        juce::Justification::centredLeft);
        }
    }
}

// PADS sheet: per-pad knob labels, trim labels, and the sample-info card.
void MainComponent::paintPadSheetContent (juce::Graphics& g)
{
    if (padSheet.sheetBounds.isEmpty()) return;

    const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00B7);
    const int sp = juce::jmax (0, selectedPad);
    g.setColour (ShardColours::ink.withAlpha (0.9f));
    g.setFont (ShardColours::monoFont (11.0f, true).withExtraKerningFactor (0.14f));
    g.drawText ("PAD " + juce::String (sp + 1)
                + (padName[(size_t) sp].isNotEmpty() ? "  " + dot + "  " + padName[(size_t) sp].toUpperCase() : juce::String()),
                padSheet.sheetBounds.reduced (14, 12).removeFromTop (16), juce::Justification::centredLeft);

    {
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

        // Sample-info card: what exactly is on this pad — mini waveform with
        // the trim window shaded, plus duration / channels / rate / size.
        if (! editInfoArea.isEmpty() && editInfoArea.getHeight() > 40)
        {
            auto scr = editInfoArea.toFloat();
            g.setColour (ShardColours::knobBody2);
            g.fillRoundedRectangle (scr.expanded (3.0f), 3.0f);
            g.setColour (ShardColours::screenBg);
            g.fillRect (scr);

            const auto sb = uiSample[(size_t) sp];

            if (sb == nullptr || sb->buffer.getNumSamples() <= 0)
            {
                g.setColour (ShardColours::lcdFg.withAlpha (0.5f));
                g.setFont (ShardColours::monoFont (11.0f, true).withExtraKerningFactor (0.16f));
                g.drawText ("PAD VACIO  -  LOAD O REC PARA CARGAR", editInfoArea, juce::Justification::centred);
            }
            else
            {
                auto in    = editInfoArea.reduced (10, 8);
                auto meta  = in.removeFromBottom (14);
                in.removeFromBottom (4);
                auto plotR = in.toFloat();

                const auto& buf = sb->buffer;
                const int   nSamps = buf.getNumSamples();
                const int   nCh    = buf.getNumChannels();
                const float s0 = padStart01[(size_t) sp], s1 = padEnd01[(size_t) sp];

                // Trim window shading (kept region slightly lit).
                g.setColour (ShardColours::lcdFg.withAlpha (0.07f));
                g.fillRect (plotR.getX() + plotR.getWidth() * s0, plotR.getY(),
                            plotR.getWidth() * juce::jmax (0.0f, s1 - s0), plotR.getHeight());

                // Min/max column waveform.
                juce::Path wf;
                const int cols = juce::jmax (16, (int) plotR.getWidth());
                const float midY = plotR.getCentreY(), half = plotR.getHeight() * 0.48f;
                for (int c = 0; c < cols; ++c)
                {
                    const int a = (int) ((juce::int64) nSamps * c / cols);
                    const int b = juce::jmax (a + 1, (int) ((juce::int64) nSamps * (c + 1) / cols));
                    float lo = 0.0f, hi = 0.0f;
                    for (int ch = 0; ch < nCh; ++ch)
                    {
                        const auto range = buf.findMinMax (ch, a, b - a);
                        lo = juce::jmin (lo, range.getStart());
                        hi = juce::jmax (hi, range.getEnd());
                    }
                    const float x = plotR.getX() + (float) c * plotR.getWidth() / (float) cols;
                    wf.addLineSegment ({ x, midY - hi * half, x, midY - lo * half }, 1.0f);
                }
                g.setColour (ShardColours::accent.withAlpha (0.9f));
                g.fillPath (wf);

                // Trim edges.
                g.setColour (ShardColours::yellow.withAlpha (0.85f));
                g.drawVerticalLine ((int) (plotR.getX() + plotR.getWidth() * s0), plotR.getY(), plotR.getBottom());
                g.drawVerticalLine ((int) (plotR.getX() + plotR.getWidth() * s1) - 1, plotR.getY(), plotR.getBottom());

                // Facts row: duration / channels / rate / size.
                const double sr   = sb->sourceSampleRate;
                const double secs = sr > 0.0 ? (double) nSamps / sr : 0.0;
                const int    kb   = (int) ((juce::int64) nSamps * nCh * (int) sizeof (float) / 1024);
                const juce::String facts = juce::String (secs, 2) + " s   "
                                         + (nCh >= 2 ? "STEREO" : "MONO") + "   "
                                         + juce::String (sr / 1000.0, 1) + " kHz   "
                                         + juce::String (kb) + " KB";
                g.setColour (ShardColours::lcdFg);
                g.setFont (ShardColours::monoFont (9.5f, true).withExtraKerningFactor (0.10f));
                g.drawText (facts, meta, juce::Justification::centredLeft);
            }
        }
    }
}

// Sequencer sheet content: title/chain text + step-selection/playhead rings.
// Called from SeqOverlay::paint() (set as its paintContent callback) so it
// draws in the overlay's own paint pass, on top of everything else.
void MainComponent::paintSeqSheetContent (juce::Graphics& g)
{
    if (seqSheet.sheetBounds.isEmpty()) return;

    const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00B7);
    const int sp = juce::jmax (0, selectedPad);
    auto inner = seqSheet.sheetBounds.reduced (12, 6);

    g.setColour (ShardColours::ink.withAlpha (0.9f));
    g.setFont (ShardColours::monoFont (11.0f, true).withExtraKerningFactor (0.14f));
    const juce::String t = "STEPS  " + dot + "  PAD " + juce::String (sp + 1)
                         + (padName[(size_t) sp].isNotEmpty() ? "   " + padName[(size_t) sp] : juce::String())
                         + "   " + dot + "   P" + juce::String (selectedPattern + 1);
    g.drawText (t, inner.removeFromTop (16), juce::Justification::centredLeft);

    // Which bank is actually sounding right now (may differ from the one
    // being viewed/edited).
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
    // Ring the bank being edited on the chain-include row.
    if (auto* b = patternButtons[selectedPattern])
    {
        g.setColour (ShardColours::ink.withAlpha (0.7f));
        g.drawRect (b->getBounds(), 2);
    }
}

void MainComponent::Sheet::paint (juce::Graphics& g)
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

    // SP-style numbering: pad 01 sits BOTTOM-left, 16 top-right — logical row
    // r of the pad index maps to visual row (rows-1-r).
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
        {
            const int idx = r * cols + c;
            const int vr  = rows - 1 - r;
            if (auto* p = pads[idx])
                p->setBounds (grid.getX() + c * (cell + gap),
                             grid.getY() + vr * (cell + gap),
                             cell, cell);
        }
}

void MainComponent::resized()
{
    fxCurveArea = editInfoArea = {};
    vuArea = stepStripArea = {};

    auto area = getLocalBounds().reduced (8);

    // The LCD grows to absorb whatever the face doesn't need (the pads are
    // width-bound squares) — the screen is the protagonist.
    int screenH;
    {
        const int chromeBelow = 8 + 42 + 6 + 40 + 8 + 18 + 6;          // gaps + module bar + transport + status
        const int cell = (area.getWidth() - 3 * 8) / 4;                // square pad cells, 4 cols, gap 8
        const int bodyNeed = 20 + 6 + 18 + 4 + 86 + 6 + 20 + 8        // VU + bank chips + CTRL knobs + step LEDs
                           + (4 * cell + 3 * 8);                       // + pads
        screenH = juce::jmax (96, area.getHeight() - (30 + 8) - chromeBelow - bodyNeed);
    }

    // --- Top chrome ---
    headerArea = area.removeFromTop (30);
    skinButton.setBounds (headerArea.withLeft (headerArea.getRight() - 156).reduced (0, 2));
    area.removeFromTop (8);

    screenBezel = area.removeFromTop (screenH);
    waveform.setBounds (screenBezel);
    area.removeFromTop (8);

    // Module bar — rule of three: PADS / SEC / FX, taller than the transport
    // row so "opens a window" and "does something" read as different shapes.
    tabBarArea = area.removeFromTop (42);
    {
        auto row = tabBarArea;
        juce::TextButton* mb[3] = { &padsButton, &secButton, &fxOpenButton };
        const int w = row.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
            mb[i]->setBounds ((i < 2 ? row.removeFromLeft (w) : row).reduced (2));
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

    // --- Machine face: VU, bank chips, CTRL 1-3, step LEDs, pads ---
    {
        vuArea = area.removeFromTop (20).reduced (2, 0);
        area.removeFromTop (6);

        auto bankRow = area.removeFromTop (18);      // chips, deliberately small
        const int bw = bankRow.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
            macroBankBtns[i]->setBounds ((i < 2 ? bankRow.removeFromLeft (bw) : bankRow).reduced (34, 0));
        area.removeFromTop (4);

        auto mrow = area.removeFromTop (86);             // the 3 CTRL macros
        juce::Slider* mk[3] = { &macroCtrl1, &macroCtrl2, &macroCtrl3 };
        const int w = mrow.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
        {
            auto cell = (i < 2 ? mrow.removeFromLeft (w) : mrow);
            cell.removeFromTop (14);                     // gap for label
            mk[i]->setBounds (cell.reduced (10, 0));
        }
        area.removeFromTop (6);

        stepStripArea = area.removeFromTop (20).reduced (2, 0);
        area.removeFromTop (8);
        layoutPadGrid (area, 4, 4, 8);
    }

    // --- Floating sheets (each sized by its own content, capped at 86%) ---
    const auto full = getLocalBounds();
    auto sheetFromBottom = [&full] (Sheet& s, int desiredH)
    {
        s.setBounds (full);
        auto f = full;
        const int cappedH = (int) (f.getHeight() * 0.86f);
        auto sheet = f.removeFromBottom (juce::jmin (desiredH, cappedH)).reduced (8);
        s.sheetBounds = sheet;
        return sheet.reduced (14, 12);
    };
    auto placeKnobRow = [] (juce::Rectangle<int> row, juce::Slider** ks)
    {
        const int w = row.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
        {
            auto cell = (i < 2 ? row.removeFromLeft (w) : row);
            cell.removeFromTop (16);                     // gap for knob name
            ks[i]->setBounds (cell.reduced (6, 2));
        }
    };

    // PADS sheet: per-pad knobs, trim, REV/LOOP + AUTO CHOP, sample-info card.
    {
        auto inner = sheetFromBottom (padSheet, 496);
        auto titleRow = inner.removeFromTop (32);
        padCloseButton.setBounds (titleRow.removeFromRight (32).reduced (2));

        juce::Slider* k1[3] = { &pitchSlider, &volSlider, &panSlider };
        juce::Slider* k2[3] = { &attackSlider, &releaseSlider, &chokeSlider };
        placeKnobRow (inner.removeFromTop (86), k1);
        placeKnobRow (inner.removeFromTop (86), k2);
        inner.removeFromTop (6);

        const int labelW = 64;
        auto ctrlRow = [&inner, labelW] (int h) { auto r = inner.removeFromTop (h); r.removeFromLeft (labelW); return r; };
        startSlider.setBounds (ctrlRow (26)); inner.removeFromTop (4);
        endSlider.setBounds   (ctrlRow (26)); inner.removeFromTop (8);

        auto rr = inner.removeFromTop (30);
        reverseButton.setBounds (rr.removeFromLeft (rr.getWidth() / 3).reduced (3, 0));
        loopButton.setBounds    (rr.removeFromLeft (rr.getWidth() / 2).reduced (3, 0));
        chopButton.setBounds    (rr.reduced (3, 0));
        inner.removeFromTop (8);

        editInfoArea = inner;      // sample-info card (drawn in paintPadSheetContent)
    }

    // FX sheet: tight knob boxes + the live filter curve.
    {
        auto inner = sheetFromBottom (fxSheet, 566);
        auto titleRow = inner.removeFromTop (28);
        fxCloseButton.setBounds (titleRow.removeFromRight (32).reduced (2));

        auto ctrl = inner.removeFromBottom (40);
        fxTypeButton.setBounds (ctrl.removeFromLeft (ctrl.getWidth() / 2).reduced (3));
        testButton.setBounds   (ctrl.reduced (3));
        inner.removeFromBottom (8);

        juce::Slider* r1[3] = { &cutoffSlider, &resoSlider, &driveSlider };
        juce::Slider* r2[3] = { &dlyTimeSlider, &dlyFbSlider, &dlyMixSlider };
        placeKnobRow (inner.removeFromTop (140), r1);
        placeKnobRow (inner.removeFromTop (140), r2);
        inner.removeFromTop (10);

        fxCurveArea = inner;       // live filter response (paintFxSheetContent)
    }

    // BROWSE sheet: the tallest of them all — the file list wants the room.
    {
        auto inner = sheetFromBottom (browseSheet, full.getHeight());   // clamps to the 86% cap
        auto titleRow = inner.removeFromTop (32);
        browseCloseButton.setBounds (titleRow.removeFromRight (32).reduced (2));

        browseLoadButton.setBounds (inner.removeFromBottom (38).reduced (2, 0));
        inner.removeFromBottom (8);
        if (browser != nullptr) browser->setBounds (inner);
    }

    // SEC sheet: pattern/len, chain row, chain-clear/note, step grid, bpm/clear.
    {
        const int patLen = engine.getPatternLength (selectedPattern);
        const int rows   = juce::jmax (1, patLen / kStepCols);
        const int gap    = 4;
        const int fixedRowsH = 164;   // title + pattern/len + chain + clr/note + bpm/clear rows, incl. gaps

        const int maxCellW  = (full.getWidth() - 16 /*outer reduce*/ - 28 /*inner reduce*/ - (kStepCols - 1) * gap) / kStepCols;
        const int comfyCell = juce::jlimit (36, 64, maxCellW);
        const int gridH     = rows * comfyCell + (rows - 1) * gap;

        auto inner = sheetFromBottom (seqSheet, fixedRowsH + 24 + 16 + gridH);
        auto titleRow = inner.removeFromTop (32);          // 2-line title drawn by paintSeqSheetContent
        seqCloseButton.setBounds (titleRow.removeFromRight (32).reduced (2));

        {
            auto row1 = inner.removeFromTop (26);
            const int w1 = row1.getWidth() / 2;
            patternSlider.setBounds (row1.removeFromLeft (w1).reduced (2, 0));
            lengthSlider.setBounds  (row1.reduced (2, 0));
            inner.removeFromTop (4);

            // Chain: the 8 coloured include-toggles, then clear + note.
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
            const int cols = kStepCols;

            const int gridMaxCellW = (inner.getWidth()  - (cols - 1) * gap) / cols;
            const int gridMaxCellH = (inner.getHeight() - (rows - 1) * gap) / rows;
            const int cell = juce::jlimit (20, 70, juce::jmin (gridMaxCellW, gridMaxCellH));

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
        openBrowseForPad (index);
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
    for (int i = 0; i < kNumPads; ++i) refreshPad (i);
    if (macroBank == 2) refreshMacroValues();      // PAD bank tracks the selection
    if (padSheet.isVisible()) padSheet.repaint();  // its title/card follow the selection
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

void MainComponent::openBrowseForPad (int index)
{
    browseTargetPad = index;
    selectPad (index);                       // the target pad reads as selected behind the sheet
    closeAllSheets();
    browseSheet.setVisible (true);
    browseSheet.toFront (false);
    if (browser != nullptr) browser->refresh();
    selectionChanged();                      // sync the CARGAR button to the current selection
    resized();
    repaint();
}

// A file is only loadable once one is actually picked (folders don't count).
void MainComponent::selectionChanged()
{
    const bool ready = browser != nullptr
                    && browser->getNumSelectedFiles() > 0
                    && browser->getSelectedFile (0).existsAsFile();
    browseLoadButton.setEnabled (ready);
    browseSheet.repaint();                   // the header shows the pick
}

void MainComponent::fileDoubleClicked (const juce::File& f)
{
    if (f.existsAsFile())
        loadBrowserSelection();              // double-tap a file = load it straight away
}

void MainComponent::loadBrowserSelection()
{
    if (browser == nullptr || browseTargetPad < 0) return;
    const auto f = browser->getSelectedFile (0);
    if (! f.existsAsFile()) return;

    const int index = browseTargetPad;
    const juce::String fileName = f.getFileName();
    closeAllSheets();

    status.setText ("Cargando pad " + juce::String (index + 1) + " ...", juce::dontSendNotification);
    loader.loadAsync (juce::URL (f), index, [this, index, fileName] (bool ok, juce::String detail, SampleBuffer::Ptr sb)
    {
        if (ok)
        {
            assignSampleToPad (index, sb, fileName);
            status.setText ("Pad " + juce::String (index + 1) + " cargado  [" + detail + "]", juce::dontSendNotification);
        }
        else
        {
            status.setText ("Fallo al cargar: " + detail, juce::dontSendNotification);
        }
    });
}

// Sheet header: which pad is being filled and what is currently picked.
void MainComponent::paintBrowseSheetContent (juce::Graphics& g)
{
    if (browseSheet.sheetBounds.isEmpty()) return;

    auto inner = browseSheet.sheetBounds.reduced (12, 6);
    g.setColour (ShardColours::ink.withAlpha (0.9f));
    g.setFont (ShardColours::monoFont (11.0f, true).withExtraKerningFactor (0.14f));
    g.drawText ("CARGAR EN PAD " + juce::String (juce::jmax (0, browseTargetPad) + 1),
                inner.removeFromTop (16), juce::Justification::centredLeft);

    const bool picked = browser != nullptr && browser->getNumSelectedFiles() > 0
                     && browser->getSelectedFile (0).existsAsFile();
    g.setColour (ShardColours::inkDim);
    g.setFont (ShardColours::monoFont (9.5f, true).withExtraKerningFactor (0.08f));
    g.drawText (picked ? browser->getSelectedFile (0).getFileName()
                       : juce::String ("elige una muestra  -  wav / aiff / flac / ogg / mp3"),
                inner.removeFromTop (14), juce::Justification::centredLeft);
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
    const int prevPlayStep = lastPlayStep;
    lastPlayStep = ps;

    // Keep the SEC sheet's readout/rings fresh while the sequencer runs.
    if (engine.isPlaying() && seqSheet.isVisible())
        seqSheet.repaint();

    // Face strips: VU ballistics (fast attack, ~0.8 decay/frame) and the
    // step-LED playhead.
    {
        const float pl = engine.readOutPeakL();
        const float pr = engine.readOutPeakR();
        const float prevL = vuL, prevR = vuR;
        vuL = juce::jmax (pl, vuL * 0.80f); if (vuL < 0.004f) vuL = 0.0f;
        vuR = juce::jmax (pr, vuR * 0.80f); if (vuR < 0.004f) vuR = 0.0f;
        if ((vuL != prevL || vuR != prevR) && ! vuArea.isEmpty())
            repaint (vuArea.expanded (2));
        if (ps != prevPlayStep && ! stepStripArea.isEmpty())
            repaint (stepStripArea.expanded (2));
    }

    if (recordingActive)
        status.setText ("Recording pad " + juce::String (recordingSlot + 1)
                        + "  " + juce::String (engine.getRecordSeconds(), 1) + "s",
                        juce::dontSendNotification);
}
