#include "MainComponent.h"

namespace
{
    // References, not copies: the accent tokens are mutable (skins).
    const juce::Colour& kPadLoaded = ZatiColours::amber;
    const juce::Colour& kAccent    = ZatiColours::amber;
    const juce::Colour  kRec       = ZatiColours::red;
    const juce::Colour  kStepOff   = ZatiColours::key;
    const juce::Colour  kKey       = ZatiColours::key;

    void styleButton (juce::TextButton& b, juce::Colour c)
    {
        // Text follows the cap luminance: dark ink on light caps, light on dark.
        const bool darkCap = c.getPerceivedBrightness() < 0.5f;
        b.setColour (juce::TextButton::buttonColourId, c);
        b.setColour (juce::TextButton::textColourOffId, darkCap ? ZatiColours::inkLight : ZatiColours::ink);
        b.setColour (juce::TextButton::textColourOnId,  ZatiColours::ink);
    }

    // Cycle the 3 primaries across the 8 pattern banks so each has its own
    // colour identity in the chain-include row.
    // Pattern banks are told apart by TONE, not hue: the chassis carries no
    // colour of its own, so three steps of ink stand in for what used to be
    // three primaries. Re-read every call — the skin shifts the base tone.
    juce::Colour patternRowColour (int idx)
    {
        const juce::Colour tones[3] = { ZatiColours::accent,
                                        ZatiColours::accent.brighter (0.60f),
                                        ZatiColours::accent.brighter (1.25f) };
        return tones[(size_t) (idx % 3)];
    }
}

MainComponent::MainComponent()
{
    setLookAndFeel (&lnf);

    // Build the ZATI folder tree before anything can need it: the browser opens
    // in Samples/, projects save into Projects/, REC writes to Recordings/.
    ProjectStore::ensureTree();

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
        padZati[(size_t) i] = Zati::forPad (i);      // cut order: zati 1 is always red
        p->setZati (padZati[(size_t) i]);
        p->onClick = [this, i] { padClicked (i); };
        addAndMakeVisible (p);
        pads.add (p);
        refreshPad (i);
    }

    stepGrid.onCell = [this] (int pad, int step) { stepCellToggled (pad, step); };
    seqSheet.addAndMakeVisible (stepGrid);

    // Bar selector: 64 steps will not fit across a phone at a size worth
    // tapping, so the grid pages a bar at a time instead of shrinking.
    for (int b = 0; b < kNumSteps / kStepCols; ++b)
    {
        auto* t = new juce::TextButton (juce::String (b + 1));
        styleButton (*t, kStepOff);
        t->setColour (juce::TextButton::buttonOnColourId, kAccent);
        t->setClickingTogglesState (true);
        t->onClick = [this, b]
        {
            selectedBar = b;
            for (int i = 0; i < barButtons.size(); ++i)
                barButtons[i]->setToggleState (i == b, juce::dontSendNotification);
            refreshStepGrid();
        };
        seqSheet.addAndMakeVisible (t);
        barButtons.add (t);
    }
    barButtons[0]->setToggleState (true, juce::dontSendNotification);

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

    // Projects sheet — reached from the header chip, not the module bar (the
    // bar stays a rule of three: PADS / SEC / FX).
    {
        addAndMakeVisible (projSheet);
        projSheet.setVisible (false);
        projSheet.onDismiss = [this] { closeAllSheets(); };
        projSheet.paintContent = [this] (juce::Graphics& g) { paintProjSheetContent (g); };

        styleButton (setButton, kKey);
        setButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
        setButton.onClick = [this]
        {
            if (projSheet.isVisible()) closeAllSheets();
            else { refreshProjectList(); openSheet (projSheet, setButton); }
        };
        addAndMakeVisible (setButton);

        projList.setColour (juce::ListBox::backgroundColourId, ZatiColours::chassisTop);
        projList.setRowHeight (34);
        projModel.onChosen = [this] (int row)
        {
            if (juce::isPositiveAndBelow (row, projModel.names.size()))
                loadProject (projModel.names[row]);
        };
        projSheet.addAndMakeVisible (projList);

        styleButton (projCloseButton, kKey);
        projCloseButton.onClick = [this] { closeAllSheets(); };
        projSheet.addAndMakeVisible (projCloseButton);

        styleButton (projSaveButton, kAccent);
        projSaveButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        projSaveButton.onClick = [this]
        {
            // Reuse the highlighted name when there is one, so GUARDAR
            // overwrites the project you are looking at rather than silently
            // spawning near-duplicates.
            const int sel = projList.getSelectedRow();
            juce::String name = juce::isPositiveAndBelow (sel, projModel.names.size())
                                  ? projModel.names[sel]
                                  : currentProject;
            if (name.isEmpty())
                name = "PROYECTO " + juce::String (ProjectStore::list().size() + 1);
            saveProject (name);
        };
        projSheet.addAndMakeVisible (projSaveButton);

        styleButton (projLoadButton, kKey);
        projLoadButton.onClick = [this]
        {
            const int sel = projList.getSelectedRow();
            if (juce::isPositiveAndBelow (sel, projModel.names.size()))
                loadProject (projModel.names[sel]);
        };
        projSheet.addAndMakeVisible (projLoadButton);

        styleButton (projNewButton, kKey);
        projNewButton.onClick = [this] { newProject(); };
        projSheet.addAndMakeVisible (projNewButton);

        styleButton (projDeleteButton, kRec);
        projDeleteButton.onClick = [this]
        {
            const int sel = projList.getSelectedRow();
            if (juce::isPositiveAndBelow (sel, projModel.names.size()))
                deleteProject (projModel.names[sel]);
        };
        projSheet.addAndMakeVisible (projDeleteButton);
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

        // Start one level above Music: on Android that is the shared-storage
        // root, so Music AND Download (where most samples land) are one tap
        // away instead of buried. On desktop it lands on the home folder.
        // Open in the app's own Samples folder. It always exists (the tree is
        // created at launch) and it is where the user is told to put audio, so
        // the first thing the browser shows is their own material instead of
        // whatever the OS considers home — which on desktop is an empty /root.
        auto start = ProjectStore::samples();
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

        // Escape hatch: hand off to the OS picker. Some Android ROMs hide media
        // files from a direct directory listing no matter what is granted; the
        // system picker always reaches them (and gets its own access grant).
        styleButton (browseSystemButton, kKey);
        browseSystemButton.onClick = [this] { launchSystemPicker(); };
        browseSheet.addAndMakeVisible (browseSystemButton);
    }

    // Transport / actions.
    loadButton.setClickingTogglesState (true);
    styleButton (loadButton, kKey);
    loadButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
    loadButton.onClick = [this]
    {
        loadArmed = loadButton.getToggleState();
        // ASCII only: a raw UTF-8 dash in a literal renders as mojibake on the
        // Android build (different execution charset), so keep these plain.
        status.setText (loadArmed ? "LOAD armado - toca un pad para cargarlo"
                                  : "Toca un pad para sonar", juce::dontSendNotification);
    };
    addAndMakeVisible (loadButton);

    styleButton (testButton, kKey);
    testButton.onClick = [this] { engine.postTestTone(); status.setText ("Tono de prueba", juce::dontSendNotification); };
    addAndMakeVisible (testButton);

    styleButton (recButton, kKey);
    recButton.onClick = [this] { toggleRecordArm(); };
    addAndMakeVisible (recButton);

    styleButton (micButton, kKey);
    micButton.onClick = [this] { toggleMicSampling(); };
    padSheet.addAndMakeVisible (micButton);

    for (auto* zb : { &zatiPrevButton, &zatiNextButton })
    {
        styleButton (*zb, kKey);
        padSheet.addAndMakeVisible (zb);
    }
    zatiPrevButton.onClick = [this] { shiftZati (-1); };
    zatiNextButton.onClick = [this] { shiftZati (+1); };

    playButton.setClickingTogglesState (true);
    styleButton (playButton, kAccent);                   // PLAY is the accent hero button
    playButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    playButton.setColour (juce::TextButton::textColourOnId,  juce::Colours::white);
    playButton.setColour (juce::TextButton::buttonOnColourId, ZatiColours::accentDim);
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
        s.setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
        s.setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
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
        s.setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
        s.setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
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
        refreshWaveformSegments();
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
        refreshWaveformSegments();
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
    patternSlider.setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
    patternSlider.setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
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
        selectedBar = 0;
        resized();
        refreshStepGrid();
        seqSheet.repaint();   // sheet card itself can grow/shrink with the bank's LEN
    };
    seqSheet.addAndMakeVisible (patternSlider);

    // Pattern length (FL-Studio-style fader): how many steps this bank plays
    // before looping / handing off to the next chain entry — 16 up to 64,
    // one row of 8 at a time. Changing it reflows the step grid itself
    // (more/fewer rows), so it forces a full resized(), not just a repaint.
    lengthSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    lengthSlider.setRange ((double) kMinPatLen, (double) kMaxPatLen, (double) kStepCols);   // whole bars
    lengthSlider.setValue ((double) kMinPatLen, juce::dontSendNotification);
    lengthSlider.setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
    lengthSlider.setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
    lengthSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lengthSlider.setColour (juce::Slider::trackColourId, ZatiColours::accent);
    lengthSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
    lengthSlider.textFromValueFunction = [] (double v) { return "LEN " + juce::String ((int) v); };
    lengthSlider.updateText();
    lengthSlider.onValueChange = [this]
    {
        engine.setPatternLength (selectedPattern, (int) lengthSlider.getValue());
        resized();
        refreshStepGrid();
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
    noteSlider.setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
    noteSlider.setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
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
        juce::Slider* ks[3] = { &macroCtrl1, &macroCtrl2, &macroCtrl3 };
        for (int i = 0; i < 3; ++i)
        {
            ks[i]->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);   // the readout row measures
            ks[i]->onDragStart = [this, i] { setMacroTouched (i, true); };
            ks[i]->onDragEnd   = [this, i] { setMacroTouched (i, false); };
        }
    }

    // FX slots: four one-tap effects beside the pads.
    {
        const SlotFx defaults[kNumSlots] = { SlotFx::Filtro, SlotFx::Delay, SlotFx::Drive, SlotFx::Loop };
        for (int i = 0; i < kNumSlots; ++i)
        {
            slots[(size_t) i].fx = defaults[i];
            auto* b = new HoldButton (slotLabel (defaults[i]));
            styleButton (*b, kKey);
            b->setColour (juce::TextButton::buttonOnColourId, kAccent);
            b->onClick = [this, i] { slotTapped (i); };
            b->onHold  = [this, i] { cycleSlotFx (i); };
            addAndMakeVisible (b);
            slotButtons.add (b);
        }
    }

    {
        const char* bankNames[3] = { "FILTRO", "DELAY", "PAD" };
        for (int i = 0; i < 3; ++i)
        {
            auto* b = new juce::TextButton (bankNames[i]);
            styleButton (*b, kKey);
            b->setColour (juce::TextButton::buttonOnColourId, kAccent);
            //  This chip used to be tappable, and that is what produced "there
            //  are two delays": you could point CTRL at DELAY here WITHOUT
            //  arming the delay slot, while the FX sheet held a third copy of
            //  the same parameters. One effect, three switches, no winner.
            //  It is a READOUT now — only a slot can hand over the knobs.
            b->setInterceptsMouseClicks (false, false);
            addAndMakeVisible (b);
            macroBankBtns.add (b);
        }
    }

    addAndMakeVisible (waveform);

    // ZATI badge in the header: taps cycle the 4 accent skins.
    skinButton.setColour (juce::TextButton::buttonColourId, ZatiColours::screenBg);
    skinButton.setColour (juce::TextButton::textColourOffId, ZatiColours::lcdFg);
    skinButton.setColour (juce::TextButton::textColourOnId,  ZatiColours::lcdFg);
    skinButton.onClick = [this]
    {
        ZatiColours::setSkin (ZatiColours::currentSkin + 1);
        applySkin();
    };
    projSheet.addAndMakeVisible (skinButton);

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
    status.setColour (juce::Label::textColourId, ZatiColours::inkDim);
    status.setText ("Toca un pad para sonar", juce::dontSendNotification);
    addAndMakeVisible (status);

    startTimer (60);
    setSize (500, 1080);
    setMacroBank (0);
    applySkin();
}

// Restyle everything that captured accent-coloured values at construction —
// the rest of the UI reads ZatiColours at paint time and only needs repaint.
void MainComponent::applySkin()
{
    const auto acc = ZatiColours::accent;
    // Lit-state text must stay legible on a dark accent (TINTA skin).
    const auto onTxt = acc.getPerceivedBrightness() < 0.5f ? ZatiColours::inkLight : ZatiColours::ink;

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
    for (auto* b : slotButtons)
    {
        b->setColour (juce::TextButton::buttonOnColourId, acc);
        b->setColour (juce::TextButton::textColourOnId, onTxt);
    }
    for (int i = 0; i < patternButtons.size(); ++i)
        patternButtons[i]->setColour (juce::TextButton::buttonOnColourId, patternRowColour (i));

    styleButton (playButton, acc);
    playButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    playButton.setColour (juce::TextButton::textColourOnId,  juce::Colours::white);
    playButton.setColour (juce::TextButton::buttonOnColourId, ZatiColours::accentDim);

    juce::Slider* tracks[] = { &startSlider, &endSlider, &bpmSlider, &patternSlider, &lengthSlider };
    for (auto* s : tracks)
        s->setColour (juce::Slider::trackColourId, acc);
    lnf.setColour (juce::Slider::trackColourId, acc);
    lnf.setColour (juce::Slider::thumbColourId, acc);
    lnf.applyBrowserColours();                     // the file list follows the skin too
    styleButton (browseLoadButton, acc);
    browseLoadButton.setColour (juce::TextButton::textColourOffId, onTxt);

    skinButton.setButtonText (juce::String ("SKIN ") + juce::String::charToString ((juce::juce_wchar) 0x00B7)
                              + " " + ZatiColours::skinName (ZatiColours::currentSkin));
    skinButton.setColour (juce::TextButton::textColourOffId, ZatiColours::lcdFg);
    skinButton.setColour (juce::TextButton::textColourOnId,  ZatiColours::lcdFg);

    repaint();
}

juce::String MainComponent::macroBaseLabel (int idx) const
{
    return "CTRL " + juce::String (idx + 1);
}

juce::String MainComponent::macroParamLabel (int idx) const
{
    static const char* names[3][3] = { { "CUTOFF", "RESO",  "DRIVE" },
                                       { "TIME",   "FBK",   "MIX"   },
                                       { "PITCH",  "START", "END"   } };
    return names[juce::jlimit (0, 2, macroBank)][juce::jlimit (0, 2, idx)];
}

// The readout measures; it always carries a unit so the number means something
// on its own. Monospaced so digits do not shift as the value changes.
juce::String MainComponent::macroReadout (int idx) const
{
    const juce::Slider* ks[3] = { &macroCtrl1, &macroCtrl2, &macroCtrl3 };
    const double v = ks[juce::jlimit (0, 2, idx)]->getValue();

    if (macroBank == 0)
    {
        if (idx == 0) return v >= 1000.0 ? juce::String (v / 1000.0, 1) + " kHz"
                                         : juce::String ((int) v) + " Hz";
        if (idx == 1) return "Q " + juce::String (v, 2);
        return juce::String (juce::roundToInt (v * 100.0)) + " %";
    }
    if (macroBank == 1)
    {
        if (idx == 0) return juce::String ((int) v) + " ms";
        return juce::String (juce::roundToInt (v * 100.0)) + " %";
    }
    if (idx == 0) return (v > 0 ? "+" : "") + juce::String ((int) v) + " st";
    return juce::String (juce::roundToInt (v * 100.0)) + " %";
}

void MainComponent::setMacroTouched (int idx, bool touched)
{
    if (! juce::isPositiveAndBelow (idx, 3)) return;

    if (touched)
    {
        macroLabelTimer.stopTimer();
        macroTouched[(size_t) idx] = true;
    }
    else
    {
        // Hold the parameter name briefly after release: letting it snap back
        // the instant the finger lifts makes the name unreadable on a quick
        // tweak, which is when you most want to know what you just moved.
        macroLabelTimer.onFire = [this]
        {
            macroTouched.fill (false);
            repaint();
        };
        macroLabelTimer.startTimer (800);
    }
    repaint();
}

const char* MainComponent::slotLabel (SlotFx fx) const
{
    switch (fx)
    {
        case SlotFx::Filtro: return "ISO";
        case SlotFx::Delay:  return "DLY";
        case SlotFx::Drive:  return "DRV";
        case SlotFx::Loop:   return "LOOP";
    }
    return "--";
}

// A slot is a live switch, so it must reach the engine directly rather than
// going through the FX sheet's knobs: those only exist while that sheet is
// built, and the slot has to work from the perform screen.
void MainComponent::applySlotState (int i)
{
    if (! juce::isPositiveAndBelow (i, kNumSlots)) return;
    const auto& s = slots[(size_t) i];

    switch (s.fx)
    {
        case SlotFx::Filtro:
            // Off is a fully open filter, not a bypass flag: the engine skips
            // the stage on its own when it is transparent.
            cutoffSlider.setValue (s.on ? 800.0 : 20000.0, juce::sendNotification);
            resoSlider.setValue   (s.on ? 2.20  : 0.707,   juce::sendNotification);
            break;

        case SlotFx::Delay:
            dlyMixSlider.setValue (s.on ? 0.35 : 0.0, juce::sendNotification);
            break;

        case SlotFx::Drive:
            driveSlider.setValue (s.on ? 0.55 : 0.0, juce::sendNotification);
            break;

        case SlotFx::Loop:
            // A momentary beat-repeat needs engine support that does not exist
            // yet; until it does, the slot loops the selected pad so the
            // control is honest about what it currently does.
            if (selectedPad >= 0)
            {
                padLoop[(size_t) selectedPad] = s.on;
                engine.setPadLoop (selectedPad, s.on);
                loopButton.setToggleState (s.on, juce::dontSendNotification);
            }
            break;
    }
}

// Long press reassigns the slot. Turning it off first matters: leaving the
// outgoing effect engaged while the button starts controlling a different one
// would strand a filter or a delay with no visible switch to undo it.
void MainComponent::cycleSlotFx (int i)
{
    if (! juce::isPositiveAndBelow (i, kNumSlots)) return;

    auto& s = slots[(size_t) i];
    if (s.on)
    {
        s.on = false;
        applySlotState (i);
        slotButtons[i]->setToggleState (false, juce::dontSendNotification);
    }

    s.fx = (SlotFx) (((int) s.fx + 1) % 4);
    slotButtons[i]->setButtonText (slotLabel (s.fx));
    if (activeSlot == i) activeSlot = -1;

    status.setText ("Slot " + juce::String (i + 1) + " -> " + slotLabel (s.fx),
                    juce::dontSendNotification);
    repaint();
}

void MainComponent::slotTapped (int i)
{
    if (! juce::isPositiveAndBelow (i, kNumSlots)) return;

    auto& s = slots[(size_t) i];
    s.on = ! s.on;
    slotButtons[i]->setToggleState (s.on, juce::dontSendNotification);
    applySlotState (i);

    // The active slot takes the three knobs, so whatever you just switched on
    // is immediately the thing under your fingers.
    if (s.on)
    {
        activeSlot = i;
        if (s.fx == SlotFx::Filtro) setMacroBank (0);
        else if (s.fx == SlotFx::Delay) setMacroBank (1);
        else if (s.fx == SlotFx::Drive) setMacroBank (0);
    }
    else if (activeSlot == i)
    {
        activeSlot = -1;
    }

    status.setText (juce::String (slotLabel (s.fx)) + (s.on ? " ON" : " OFF"),
                    juce::dontSendNotification);
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
    repaint();   // the readout tracks the value live

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
    projSheet.setVisible (false);
    setButton.setToggleState (false, juce::dontSendNotification);
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
    g.setGradientFill (juce::ColourGradient (ZatiColours::chassisTop, full.getCentreX(), full.getY(),
                                             ZatiColours::chassisBot, full.getCentreX(), full.getBottom(), false));
    g.fillRect (full);

    // 2. Recessed LCD bezel around the scope (square, dark inset on a light face).
    if (! screenBezel.isEmpty())
    {
        auto r = screenBezel.toFloat();
        g.setColour (ZatiColours::knobBody2);
        g.fillRoundedRectangle (r.expanded (3.0f), 3.0f);
        g.setColour (ZatiColours::knobEdge.withAlpha (0.7f));
        g.drawRoundedRectangle (r.expanded (3.0f).reduced (0.5f), 3.0f, 1.2f);
    }

    // 3. Header: ZATI wordmark left, fragment strip right. No touch targets
    //    here — the strip is a readout, not a control: it is the state of the
    //    kit at a glance, one swatch per fragment colour, dimmed where no pad
    //    of that colour is loaded.
    if (! headerArea.isEmpty())
    {
        auto h = headerArea;
        g.setColour (ZatiColours::ink);
        g.setFont (ZatiColours::displayFont (Metrics::fTitle).withExtraKerningFactor (0.16f));
        g.drawText ("ZATI", h.getX(), h.getY(), 140, h.getHeight(), juce::Justification::centredLeft);

        const int sw = 9, sh = 13, gap = 4;
        const int stripW = Zati::kNumColours * sw + (Zati::kNumColours - 1) * gap;
        int x = h.getRight() - stripW;
        const int y = h.getCentreY() - sh / 2;

        for (int i = 0; i < Zati::kNumColours; ++i)
        {
            bool used = false;
            for (int p = 0; p < kNumPads && ! used; ++p)
                used = padHasSample[(size_t) p] && padZati[(size_t) p] == i;

            g.setColour (used ? Zati::colour (i) : Zati::colour (i).withAlpha (0.25f));
            g.fillRect (x, y, sw, sh);
            x += sw + gap;
        }
    }

    // 4. Machine face: CTRL labels (bank-dependent), VU strip, step LEDs.
    {
        g.setColour (ZatiColours::ink.withAlpha (0.85f));
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.16f));
        juce::Slider* ks[3] = { &macroCtrl1, &macroCtrl2, &macroCtrl3 };
        for (int i = 0; i < 3; ++i)
        {
            auto r = ks[i]->getBounds();
            const bool touched = macroTouched[(size_t) i];

            // Label names, readout measures — never the other way round.
            g.setColour (touched ? ZatiColours::ink : ZatiColours::ink.withAlpha (0.55f));
            g.setFont (ZatiColours::monoFont (touched ? 10.5f : 10.0f, true)
                         .withExtraKerningFactor (0.16f));
            g.drawText (touched ? macroParamLabel (i) : macroBaseLabel (i),
                        r.getX() - 8, r.getY() - 14, r.getWidth() + 16, 12,
                        juce::Justification::centred);

            auto chip = juce::Rectangle<int> (r.getX() - 2, r.getBottom() + 2, r.getWidth() + 4, 20);
            g.setColour (ZatiColours::screenBg);
            g.fillRoundedRectangle (chip.toFloat(), 2.0f);
            g.setColour (touched ? ZatiColours::lcdFg : ZatiColours::lcdFg.withAlpha (0.8f));
            g.setFont (ZatiColours::monoFont (Metrics::fLabel, true));
            g.drawText (macroReadout (i), chip, juce::Justification::centred);
        }

        // Stereo VU: two segmented LED rows (L/R) on a recessed strip.
        if (! vuArea.isEmpty())
        {
            auto scr = vuArea.toFloat();
            g.setColour (ZatiColours::screenBg);
            g.fillRoundedRectangle (scr, 2.0f);

            auto in = vuArea.reduced (24, 3);
            const int nSeg = 28;
            const float segW = (float) in.getWidth() / (float) nSeg;

            g.setColour (ZatiColours::lcdFg.withAlpha (0.55f));
            g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
            g.drawText ("L", vuArea.getX() + 6, in.getY() - 1, 12, in.getHeight() / 2, juce::Justification::centredLeft);
            g.drawText ("R", vuArea.getX() + 6, in.getCentreY(), 12, in.getHeight() / 2, juce::Justification::centredLeft);

            auto drawRow = [&] (float level, juce::Rectangle<float> row)
            {
                const int lit = (int) std::round (std::sqrt (juce::jlimit (0.0f, 1.0f, level)) * (float) nSeg);
                for (int i = 0; i < nSeg; ++i)
                {
                    const bool hot = i >= (int) (nSeg * 0.82f);
                    // Lit segments in LCD ink: accent is near-black on this dark
                    // strip, so a "lit" segment read the same as an unlit one.
                    juce::Colour c = i < lit ? (hot ? ZatiColours::red : ZatiColours::lcdFg)
                                             : ZatiColours::lcdFg.withAlpha (0.10f);
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
            g.setColour (ZatiColours::screenBg);
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
                    g.setColour (ZatiColours::lcdFg.withAlpha ((i % 4 == 0) ? 0.30f : 0.12f));
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

    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::monoFont (Metrics::fLabel, true).withExtraKerningFactor (0.14f));
    g.drawText ("FX", fxSheet.sheetBounds.reduced (14, 12).removeFromTop (16), juce::Justification::centredLeft);

    {
        g.setColour (ZatiColours::ink.withAlpha (0.85f));
        g.setFont (ZatiColours::monoFont (Metrics::fLabel, true).withExtraKerningFactor (0.12f));
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
            g.setColour (ZatiColours::knobBody2);
            g.fillRoundedRectangle (scr.expanded (3.0f), 3.0f);
            g.setColour (ZatiColours::screenBg);
            g.fillRect (scr);

            auto plot = scr.reduced (10.0f, 14.0f);
            const float fLo = 20.0f, fHi = 20000.0f;
            const float dbTop = 24.0f, dbBot = -36.0f;
            auto xForF  = [&plot, fLo, fHi] (float f)  { return plot.getX() + plot.getWidth() * (std::log (f / fLo) / std::log (fHi / fLo)); };
            auto yForDb = [&plot, dbTop, dbBot] (float db) { return plot.getY() + plot.getHeight() * ((dbTop - db) / (dbTop - dbBot)); };

            // Grid: decades + 0 dB line, dim LCD green.
            g.setColour (ZatiColours::lcdFg.withAlpha (0.18f));
            for (float f : { 100.0f, 1000.0f, 10000.0f })
                g.drawVerticalLine ((int) xForF (f), plot.getY(), plot.getBottom());
            g.drawHorizontalLine ((int) yForDb (0.0f), plot.getX(), plot.getRight());
            g.setColour (ZatiColours::lcdFg.withAlpha (0.45f));
            g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
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
            // lcdFg, not accent: accent is the achromatic chassis ink and sits
            // at ~1.06:1 on this dark plot — the curve simply vanished.
            g.setColour (ZatiColours::lcdFg);
            g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved));

            // Cutoff marker + readout.
            g.setColour (ZatiColours::yellow.withAlpha (0.8f));
            g.drawVerticalLine ((int) xForF (juce::jlimit (fLo, fHi, fc)), plot.getY(), plot.getBottom());
            g.setColour (ZatiColours::lcdFg);
            g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.12f));
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
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::monoFont (Metrics::fLabel, true).withExtraKerningFactor (0.14f));
    g.drawText ("PAD " + juce::String (sp + 1)
                + (padName[(size_t) sp].isNotEmpty() ? "  " + dot + "  " + padName[(size_t) sp].toUpperCase() : juce::String()),
                padSheet.sheetBounds.reduced (14, 12).removeFromTop (16), juce::Justification::centredLeft);

    {
        // Knobs: label above (same convention as FX).
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.10f));
        auto name = [&g] (juce::Slider& s, const char* t)
        {
            auto r = s.getBounds();
            g.drawText (t, r.getX() - 6, r.getY() - 14, r.getWidth() + 12, 12, juce::Justification::centred);
        };
        name (pitchSlider, "PITCH"); name (volSlider, "VOLUME"); name (panSlider, "PAN");
        name (attackSlider, "ATTACK"); name (releaseSlider, "RELEASE"); name (chokeSlider, "CHOKE");

        // Start/End stay linear (a trim range, not a knob): label to the left.
        g.setFont (ZatiColours::monoFont (Metrics::fLabel, true).withExtraKerningFactor (0.06f));
        auto lab = [&g] (juce::Slider& s, const char* t)
        {
            auto r = s.getBounds();
            g.drawText (t, r.getX() - 66, r.getY(), 60, r.getHeight(), juce::Justification::centredLeft);
        };
        lab (startSlider, "START"); lab (endSlider, "END");

        // ZATI row: the fragment colour this pad carries, named as well as
        // shown — the number and the name are the non-chromatic half.
        if (! zatiSwatchArea.isEmpty())
        {
            const int z = padZati[(size_t) sp];
            g.setColour (Zati::colour (z));
            g.fillRoundedRectangle (zatiSwatchArea.toFloat(), 3.0f);

            const bool darkFrag = Zati::colour (z).getPerceivedBrightness() < 0.55f;
            g.setColour (darkFrag ? ZatiColours::inkLight : ZatiColours::ink);
            g.setFont (ZatiColours::monoFont (Metrics::fLabel, true).withExtraKerningFactor (0.14f));
            g.drawText ("ZATI " + juce::String (z + 1) + "  " + Zati::name (z),
                        zatiSwatchArea, juce::Justification::centred);
        }

        // Sample-info card: what exactly is on this pad — mini waveform with
        // the trim window shaded, plus duration / channels / rate / size.
        if (! editInfoArea.isEmpty() && editInfoArea.getHeight() > 40)
        {
            auto scr = editInfoArea.toFloat();
            g.setColour (ZatiColours::knobBody2);
            g.fillRoundedRectangle (scr.expanded (3.0f), 3.0f);
            g.setColour (ZatiColours::screenBg);
            g.fillRect (scr);

            const auto sb = uiSample[(size_t) sp];

            if (sb == nullptr || sb->buffer.getNumSamples() <= 0)
            {
                g.setColour (ZatiColours::lcdFg.withAlpha (0.5f));
                g.setFont (ZatiColours::monoFont (Metrics::fLabel, true).withExtraKerningFactor (0.16f));
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
                g.setColour (ZatiColours::lcdFg.withAlpha (0.07f));
                g.fillRect (plotR.getX() + plotR.getWidth() * s0, plotR.getY(),
                            plotR.getWidth() * juce::jmax (0.0f, s1 - s0), plotR.getHeight());

                // Min/max column waveform, in the pad's own fragment colour.
                //  NOT accent: the chassis is achromatic, so accent is near-black
                //  ink meant for the light face — on this dark card it measured
                //  1.06:1 against screenBg, i.e. invisible. The zati is both
                //  guaranteed vivid on the LCD and the right signal: this card
                //  belongs to one pad, so it should wear that pad's colour.
                //  Outside the trim the wave stays drawn but dimmed, so you can
                //  see the part you are cutting away instead of losing it.
                const juce::Colour frag = Zati::colour (padZati[(size_t) sp]);
                juce::Path wfIn, wfOut;
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
                    const float t = (float) c / (float) cols;
                    auto& path = (t >= s0 && t < s1) ? wfIn : wfOut;
                    path.addLineSegment ({ x, midY - hi * half, x, midY - lo * half }, 1.0f);
                }
                g.setColour (frag.withAlpha (0.30f));
                g.fillPath (wfOut);
                g.setColour (frag);
                g.fillPath (wfIn);

                // Trim edges.
                g.setColour (ZatiColours::yellow.withAlpha (0.85f));
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
                g.setColour (ZatiColours::lcdFg);
                g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.10f));
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

    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::monoFont (Metrics::fLabel, true).withExtraKerningFactor (0.14f));
    const juce::String t = "STEPS  " + dot + "  PAD " + juce::String (sp + 1)
                         + (padName[(size_t) sp].isNotEmpty() ? "   " + padName[(size_t) sp] : juce::String())
                         + "   " + dot + "   P" + juce::String (selectedPattern + 1);
    g.drawText (t, inner.removeFromTop (16), juce::Justification::centredLeft);

    // Which bank is actually sounding right now (may differ from the one
    // being viewed/edited).
    const juce::String chainStr = (engine.getChainLength() <= 0)
        ? "looping P" + juce::String (selectedPattern + 1)
        : "playing P" + juce::String (engine.getPlayingPattern() + 1);
    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.10f));
    g.drawText (chainStr, inner.removeFromTop (14), juce::Justification::centredLeft);

    // The grid paints its own playhead and lane colours (see StepGrid).
    // Ring the bank being edited on the chain-include row.
    if (auto* b = patternButtons[selectedPattern])
    {
        g.setColour (ZatiColours::ink.withAlpha (0.7f));
        g.drawRect (b->getBounds(), 2);
    }
}

void MainComponent::Sheet::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.45f));
    if (sheetBounds.isEmpty()) return;

    g.setColour (ZatiColours::chassisTop);
    g.fillRoundedRectangle (sheetBounds.toFloat(), 14.0f);
    g.setColour (ZatiColours::ink.withAlpha (0.55f));
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
        const int chromeBelow = 16 + 4 + 16 + 8 + 44 + 8 + 18 + 6;   // VU + strip + the single module/transport row + status          // gaps + module bar + transport + status
        const int cell = (area.getWidth() - 3 * 8) / 4;                // square pad cells, 4 cols, gap 8
        const int bodyNeed = 16 + 4 + 88 + 8                            // chip readout + CTRL knobs
                           + 4 + 32 + 8                                // + FX slot row
                           + (4 * cell + 3 * 8);                       // + pads
        screenH = juce::jmax (96, area.getHeight() - (30 + 8) - chromeBelow - bodyNeed);
    }

    // --- Top chrome ---
    headerArea = area.removeFromTop (Metrics::tab);
    area.removeFromTop (8);

    //  VU above the screen and the step strip below it, so the two readouts
    //  frame the LCD instead of sitting among the controls. Both are watched,
    //  not touched, so they belong together up here.
    vuArea = area.removeFromTop (Metrics::lg).reduced (2, 0);
    area.removeFromTop (Metrics::xs);

    screenBezel = area.removeFromTop (screenH);
    waveform.setBounds (screenBezel);
    area.removeFromTop (Metrics::xs);

    stepStripArea = area.removeFromTop (Metrics::lg).reduced (2, 0);
    area.removeFromTop (Metrics::sm);

    //  Modules and transport share ONE row. Two stacked rows of near-identical
    //  caps were the heaviest thing on the face and read as one big menu; the
    //  freed 48px goes to the pads. They still read as two kinds of control:
    //  the module tabs are a tight segmented group (1px apart, inset) while
    //  the transport keeps separated caps and PLAY keeps the accent.
    tabBarArea = area.removeFromTop (Metrics::btn);
    {
        auto row  = tabBarArea;
        auto mods = row.removeFromLeft ((int) (row.getWidth() * 0.54f));
        row.removeFromLeft (Metrics::md);                 // air between the groups

        juce::TextButton* mb[4] = { &padsButton, &secButton, &fxOpenButton, &setButton };
        const int w = mods.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
            mb[i]->setBounds ((i < 3 ? mods.removeFromLeft (w) : mods).reduced (1, 3));

        const int u = row.getWidth() / 4;
        loadButton.setBounds (row.removeFromLeft (u).reduced (2, 0));
        recButton.setBounds  (row.removeFromLeft (u).reduced (2, 0));
        playButton.setBounds (row.reduced (2, 0));
    }
    area.removeFromTop (Metrics::sm);

    // Status pinned to the bottom.
    status.setBounds (area.removeFromBottom (Metrics::lg));
    area.removeFromBottom (Metrics::sm);

    // --- Machine face: CTRL 1-3 and their readout, FX slots, pads ---
    {
        auto bankRow = area.removeFromTop (Metrics::lg);      // chips, deliberately small
        const int bw = bankRow.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
            macroBankBtns[i]->setBounds ((i < 2 ? bankRow.removeFromLeft (bw) : bankRow).reduced (32, 0));
        area.removeFromTop (4);

        auto mrow = area.removeFromTop (88);             // 3 CTRL macros + their readout chips
        juce::Slider* mk[3] = { &macroCtrl1, &macroCtrl2, &macroCtrl3 };
        const int w = mrow.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
        {
            auto cell = (i < 2 ? mrow.removeFromLeft (w) : mrow);
            cell.removeFromTop (Metrics::lg);                     // gap for the name above
            cell.removeFromBottom (Metrics::xl);                  // gap for the readout chip below
            mk[i]->setBounds (cell.reduced (10, 0));
        }
        area.removeFromTop (Metrics::sm);

        area.removeFromTop (4);
        slotRowArea = area.removeFromTop (Metrics::tab);
        {
            auto row = slotRowArea;
            const int sw = row.getWidth() / kNumSlots;
            for (int i = 0; i < kNumSlots; ++i)
                slotButtons[i]->setBounds ((i < kNumSlots - 1 ? row.removeFromLeft (sw) : row).reduced (2, 0));
        }
        area.removeFromTop (Metrics::sm);
        layoutPadGrid (area, 4, 4, Metrics::sm);
    }

    // --- Floating sheets (each sized by its own content, capped at 86%) ---
    const auto full = getLocalBounds();
    //  Centred, not risen from the bottom. A bottom sheet at 86% buried the pad
    //  grid exactly while you were editing a pad — you lost sight of the thing
    //  you were adjusting. Centred at 78% x 92% the instrument stays visible
    //  behind the scrim and the window reads as temporary.
    auto sheetFromBottom = [&full] (Sheet& s, int desiredH)
    {
        s.setBounds (full);
        const int h = juce::jmin (desiredH, (int) (full.getHeight() * 0.78f));
        const int w = (int) (full.getWidth() * 0.92f);
        auto sheet = juce::Rectangle<int> (0, 0, w, h).withCentre (full.getCentre());
        s.sheetBounds = sheet;
        return sheet.reduced (Metrics::lg, Metrics::md);
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
        auto inner = sheetFromBottom (padSheet, 566);
        auto titleRow = inner.removeFromTop (32);
        padCloseButton.setBounds (titleRow.removeFromRight (32).reduced (2));

        juce::Slider* k1[3] = { &pitchSlider, &volSlider, &panSlider };
        juce::Slider* k2[3] = { &attackSlider, &releaseSlider, &chokeSlider };
        placeKnobRow (inner.removeFromTop (86), k1);
        placeKnobRow (inner.removeFromTop (86), k2);
        inner.removeFromTop (Metrics::sm);

        const int labelW = 64;
        auto ctrlRow = [&inner, labelW] (int h) { auto r = inner.removeFromTop (h); r.removeFromLeft (labelW); return r; };
        startSlider.setBounds (ctrlRow (26)); inner.removeFromTop (4);
        endSlider.setBounds   (ctrlRow (26)); inner.removeFromTop (8);

        auto rr = inner.removeFromTop (Metrics::tab);
        reverseButton.setBounds (rr.removeFromLeft (rr.getWidth() / 2).reduced (3, 0));
        loopButton.setBounds    (rr.reduced (3, 0));
        inner.removeFromTop (5);
        auto rr2 = inner.removeFromTop (Metrics::tab);
        chopButton.setBounds (rr2.removeFromLeft (rr2.getWidth() / 2).reduced (3, 0));
        micButton.setBounds  (rr2.reduced (3, 0));
        inner.removeFromTop (5);

        auto zr = inner.removeFromTop (Metrics::tab);
        zatiPrevButton.setBounds (zr.removeFromLeft (56).reduced (3, 0));
        zatiNextButton.setBounds (zr.removeFromRight (56).reduced (3, 0));
        zatiSwatchArea = zr.reduced (4, 2);      // drawn in paintPadSheetContent
        inner.removeFromTop (8);

        editInfoArea = inner;      // sample-info card (drawn in paintPadSheetContent)
    }

    // FX sheet: tight knob boxes + the live filter curve.
    {
        auto inner = sheetFromBottom (fxSheet, 566);
        auto titleRow = inner.removeFromTop (Metrics::xl);
        fxCloseButton.setBounds (titleRow.removeFromRight (32).reduced (2));

        auto ctrl = inner.removeFromBottom (40);
        fxTypeButton.setBounds (ctrl.removeFromLeft (ctrl.getWidth() / 2).reduced (3));
        testButton.setBounds   (ctrl.reduced (3));
        inner.removeFromBottom (8);

        juce::Slider* r1[3] = { &cutoffSlider, &resoSlider, &driveSlider };
        juce::Slider* r2[3] = { &dlyTimeSlider, &dlyFbSlider, &dlyMixSlider };
        placeKnobRow (inner.removeFromTop (140), r1);
        placeKnobRow (inner.removeFromTop (140), r2);
        inner.removeFromTop (Metrics::md);

        fxCurveArea = inner;       // live filter response (paintFxSheetContent)
    }

    // BROWSE sheet: the tallest of them all — the file list wants the room.
    {
        auto inner = sheetFromBottom (browseSheet, full.getHeight());   // clamps to the 86% cap
        auto titleRow = inner.removeFromTop (32);
        browseCloseButton.setBounds (titleRow.removeFromRight (32).reduced (2));

        auto actions = inner.removeFromBottom (Metrics::btn);
        browseSystemButton.setBounds (actions.removeFromRight (actions.getWidth() / 3).reduced (2, 0));
        browseLoadButton.setBounds   (actions.reduced (2, 0));
        inner.removeFromBottom (8);
        if (browser != nullptr) browser->setBounds (inner);
    }

    // PROJECT sheet: list of saved projects + the four actions.
    {
        auto inner = sheetFromBottom (projSheet, (int) (full.getHeight() * 0.7f));
        auto titleRow = inner.removeFromTop (32);
        projCloseButton.setBounds (titleRow.removeFromRight (32).reduced (2));

        skinButton.setBounds (inner.removeFromTop (32).reduced (2, 0));
        inner.removeFromTop (8);

        auto actions = inner.removeFromBottom (Metrics::btn);
        const int aw = actions.getWidth() / 4;
        projSaveButton.setBounds   (actions.removeFromLeft (aw).reduced (2, 0));
        projLoadButton.setBounds   (actions.removeFromLeft (aw).reduced (2, 0));
        projNewButton.setBounds    (actions.removeFromLeft (aw).reduced (2, 0));
        projDeleteButton.setBounds (actions.reduced (2, 0));
        inner.removeFromBottom (8);

        projList.setBounds (inner);
    }

    // SEC sheet: pattern/len, chain, bar selector, the pads x steps grid, bpm.
    {
        const int lanes  = StepGrid::kLanes;
        const int laneH  = 22;                    // 16 lanes -> 352, comfortable to tap
        const int gridH  = lanes * laneH;
        const int fixedRowsH = 196;               // title + rows above and below the grid

        auto inner = sheetFromBottom (seqSheet, fixedRowsH + gridH);
        auto titleRow = inner.removeFromTop (32);
        seqCloseButton.setBounds (titleRow.removeFromRight (32).reduced (2));

        {
            auto row1 = inner.removeFromTop (Metrics::xl);
            const int w1 = row1.getWidth() / 2;
            patternSlider.setBounds (row1.removeFromLeft (w1).reduced (2, 0));
            lengthSlider.setBounds  (row1.reduced (2, 0));
            inner.removeFromTop (Metrics::xs);

            auto row2 = inner.removeFromTop (Metrics::xl);
            const int pw = row2.getWidth() / kNumPatterns;
            for (int i2 = 0; i2 < kNumPatterns; ++i2)
                patternButtons[i2]->setBounds ((i2 < kNumPatterns - 1 ? row2.removeFromLeft (pw) : row2).reduced (2));
            inner.removeFromTop (Metrics::xs);

            auto row3 = inner.removeFromTop (Metrics::xl);
            const int w3 = row3.getWidth() / 2;
            chainClearButton.setBounds (row3.removeFromLeft (w3).reduced (2, 0));
            noteSlider.setBounds       (row3.reduced (2, 0));
            inner.removeFromTop (Metrics::sm);

            // Bar row: only when the pattern is longer than one bar. A single
            // lone "1" would be a control that never does anything.
            const int patLen = engine.getPatternLength (selectedPattern);
            const int bars   = juce::jmax (1, patLen / kStepCols);
            if (selectedBar >= bars) selectedBar = 0;

            if (bars > 1)
            {
                auto row4 = inner.removeFromTop (Metrics::xl);
                const int bw = row4.getWidth() / bars;
                for (int b = 0; b < barButtons.size(); ++b)
                {
                    barButtons[b]->setVisible (b < bars);
                    if (b < bars)
                        barButtons[b]->setBounds ((b < bars - 1 ? row4.removeFromLeft (bw) : row4).reduced (2, 0));
                }
                inner.removeFromTop (Metrics::sm);
            }
            else
            {
                for (auto* b : barButtons) b->setVisible (false);
            }
        }

        auto bottom = inner.removeFromBottom (Metrics::tab);
        bpmSlider.setBounds (bottom.removeFromLeft ((int) (bottom.getWidth() * 0.66f)).reduced (2, 0));
        clearButton.setBounds (bottom.reduced (3, 0));
        inner.removeFromBottom (Metrics::sm);

        stepGrid.setBounds (inner);
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
        status.setText ("Pad vacio - pulsa LOAD y toca el pad para cargarlo", juce::dontSendNotification);

    // REC armed + transport rolling: write the hit into the bank that is
    // actually sounding, quantised to the NEAREST step. Past the half-way
    // point of a step the intent was the next one, so round up and wrap.
    if (recArmed && engine.isPlaying() && padHasSample[(size_t) index])
    {
        const int bank = engine.getPlayingPattern();
        const int len  = engine.getPatternLength (bank);
        const int cur  = engine.getPlayStep();

        if (cur >= 0 && len > 0)
        {
            const int step = (cur + (engine.getStepPhase() > 0.5f ? 1 : 0)) % len;
            pattern[(size_t) bank][(size_t) step][(size_t) index] = true;
            engine.setStep (bank, step, index, true);
            status.setText ("Grabado pad " + juce::String (index + 1)
                                + " en paso " + juce::String (step + 1)
                                + " (P" + juce::String (bank + 1) + ")",
                            juce::dontSendNotification);
            if (seqSheet.isVisible()) seqSheet.repaint();
        }
    }

    selectPad (index);   // selection drives EDIT and SEC
}

void MainComponent::stepCellToggled (int pad, int step)
{
    if (step >= engine.getPatternLength (selectedPattern)) return;
    selectedStep = step;
    selectPad (pad);                 // the lane you touched becomes the pad you edit
    noteSlider.setValue (engine.getStepNote (selectedPattern, step, pad), juce::dontSendNotification);

    const bool nv = ! pattern[(size_t) selectedPattern][(size_t) step][(size_t) pad];
    pattern[(size_t) selectedPattern][(size_t) step][(size_t) pad] = nv;
    engine.setStep (selectedPattern, step, pad, nv);
    refreshStepGrid();
    seqSheet.repaint();
}

// Copy the pattern into the flat buffer the grid reads, plus each pad's colour
// and whether it holds a sample, then hand it the live playhead.
void MainComponent::refreshStepGrid()
{
    for (int st = 0; st < kNumSteps; ++st)
        for (int p = 0; p < kNumPads; ++p)
            gridCells[st * kNumPads + p] = pattern[(size_t) selectedPattern][(size_t) st][(size_t) p];

    for (int p = 0; p < kNumPads; ++p)
    {
        gridZati[p]   = padZati[(size_t) p];
        gridLoaded[p] = padHasSample[(size_t) p];
    }

    const int ps = (engine.isPlaying() && engine.getPlayingPattern() == selectedPattern)
                     ? engine.getPlayStep() : -1;

    stepGrid.setSource (gridCells, gridZati, gridLoaded,
                        engine.getPatternLength (selectedPattern),
                        selectedBar, ps, selectedPad);
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
    refreshWaveformSegments();

    // Last link of CUT -> PAD -> WAVEFORM -> KNOBS: the selected pad tints the
    // three CTRL pointers, so the knobs always say which fragment they act on.
    {
        const auto frag = padHasSample[(size_t) index] ? Zati::colour (padZati[(size_t) index])
                                                       : ZatiColours::accent;
        for (juce::Slider* k : { &macroCtrl1, &macroCtrl2, &macroCtrl3 })
        {
            k->setColour (juce::Slider::rotarySliderFillColourId, frag);
            k->repaint();
        }
    }

    for (int i = 0; i < kNumPads; ++i) refreshPad (i);
    repaint (headerArea);          // the fragment strip tracks which zatis are loaded
    if (macroBank == 2) refreshMacroValues();      // PAD bank tracks the selection
    if (padSheet.isVisible()) padSheet.repaint();  // title, zati swatch and card follow the selection
}

// The display shows the whole cut, not one pad: every pad pointing at the
// selected pad's buffer contributes its trim window as a coloured fragment.
// Auto-chop leaves exactly that — one shared buffer, sixteen windows.
void MainComponent::refreshWaveformSegments()
{
    juce::Array<WaveformDisplay::Segment> segs;

    if (selectedPad >= 0)
    {
        if (auto src = uiSample[(size_t) selectedPad])
        {
            for (int i = 0; i < kNumPads; ++i)
            {
                if (uiSample[(size_t) i] != src) continue;

                WaveformDisplay::Segment s;
                s.start01   = padStart01[(size_t) i];
                s.end01     = padEnd01[(size_t) i];
                s.colour    = Zati::colour (padZati[(size_t) i]);
                s.padNumber = i + 1;
                s.selected  = (i == selectedPad);
                segs.add (s);
            }

            std::sort (segs.begin(), segs.end(),
                       [] (const WaveformDisplay::Segment& a, const WaveformDisplay::Segment& b)
                       { return a.start01 < b.start01; });
        }
    }

    waveform.setSegments (std::move (segs));
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

// Assignment follows cut order by default; this is the spec's manual override,
// for organising a kit by kind of sound instead of by position.
void MainComponent::shiftZati (int delta)
{
    if (selectedPad < 0) return;

    auto& z = padZati[(size_t) selectedPad];
    z = ((z + delta) % Zati::kNumColours + Zati::kNumColours) % Zati::kNumColours;

    if (auto* p = pads[selectedPad]) p->setZati (z);
    refreshWaveformSegments();
    repaint();
    padSheet.repaint();

    status.setText ("Pad " + juce::String (selectedPad + 1) + " -> zati "
                        + juce::String (z + 1) + " " + Zati::name (z),
                    juce::dontSendNotification);
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

// Ask once for audio-read access, then run `then` either way — a refusal must
// still open the browser (internal/app storage is always readable).
void MainComponent::ensureStoragePermission (std::function<void()> then)
{
    using RP = juce::RuntimePermissions;

    if (! RP::isRequired (RP::readMediaAudio) || RP::isGranted (RP::readMediaAudio))
    {
        if (then) then();
        return;
    }

    RP::request (RP::readMediaAudio, [this, then] (bool granted)
    {
        if (! granted)
            status.setText ("Sin permiso de audio: no puedo leer tus carpetas de muestras",
                            juce::dontSendNotification);
        if (then) then();
    });
}

void MainComponent::openBrowseForPad (int index)
{
    browseTargetPad = index;
    selectPad (index);                       // the target pad reads as selected behind the sheet
    closeAllSheets();
    browseSheet.setVisible (true);
    browseSheet.toFront (false);
    resized();
    repaint();

    // The permission dialog is async: refresh the listing once it resolves, so
    // a folder that read as empty before the grant fills in straight away.
    ensureStoragePermission ([this]
    {
        if (browser != nullptr) browser->refresh();
        selectionChanged();                  // sync the CARGAR button to the selection
    });
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

// --- Projects ---------------------------------------------------------------

void MainComponent::ProjectList::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, names.size())) return;

    auto r = juce::Rectangle<int> (0, 0, w, h);
    if (selected)      { g.setColour (ZatiColours::accent);                 g.fillRect (r); }
    else if (row % 2)  { g.setColour (ZatiColours::ink.withAlpha (0.035f)); g.fillRect (r); }

    const auto fg = selected
        ? (ZatiColours::accent.getPerceivedBrightness() < 0.5f ? ZatiColours::inkLight : ZatiColours::ink)
        : ZatiColours::ink;
    g.setColour (fg);
    g.setFont (ZatiColours::monoFont (Metrics::fValue, true).withExtraKerningFactor (0.04f));
    g.drawFittedText (names[row], r.reduced (10, 0), juce::Justification::centredLeft, 1, 0.9f);
}

juce::ValueTree MainComponent::captureState() const
{
    juce::ValueTree s ("ZATI");
    s.setProperty ("version", 1, nullptr);
    s.setProperty ("bpm", bpmSlider.getValue(), nullptr);
    s.setProperty ("skin", ZatiColours::currentSkin, nullptr);
    s.setProperty ("macroBank", macroBank, nullptr);
    s.setProperty ("selectedPattern", selectedPattern, nullptr);

    juce::ValueTree fx ("FX");
    fx.setProperty ("type",   fxTypeButton.getToggleState() ? 1 : 0, nullptr);
    fx.setProperty ("cutoff", cutoffSlider.getValue(),  nullptr);
    fx.setProperty ("reso",   resoSlider.getValue(),    nullptr);
    fx.setProperty ("drive",  driveSlider.getValue(),   nullptr);
    fx.setProperty ("dlyTime", dlyTimeSlider.getValue(), nullptr);
    fx.setProperty ("dlyFb",   dlyFbSlider.getValue(),   nullptr);
    fx.setProperty ("dlyMix",  dlyMixSlider.getValue(),  nullptr);
    s.addChild (fx, -1, nullptr);

    juce::ValueTree pads ("PADS");
    for (int i = 0; i < kNumPads; ++i)
    {
        juce::ValueTree p ("PAD");
        p.setProperty ("i", i, nullptr);
        p.setProperty ("name",    padName[(size_t) i],    nullptr);
        p.setProperty ("has",     padHasSample[(size_t) i], nullptr);
        p.setProperty ("pitch",   padPitch[(size_t) i],   nullptr);
        p.setProperty ("gain",    padGain[(size_t) i],    nullptr);
        p.setProperty ("start",   padStart01[(size_t) i], nullptr);
        p.setProperty ("end",     padEnd01[(size_t) i],   nullptr);
        p.setProperty ("loop",    padLoop[(size_t) i],    nullptr);
        p.setProperty ("reverse", padReverse[(size_t) i], nullptr);
        p.setProperty ("choke",   padChokeUI[(size_t) i], nullptr);
        p.setProperty ("pan",     padPan[(size_t) i],     nullptr);
        p.setProperty ("attack",  padAttack[(size_t) i],  nullptr);
        p.setProperty ("release", padRelease[(size_t) i], nullptr);
        p.setProperty ("zati",    padZati[(size_t) i],    nullptr);
        pads.addChild (p, -1, nullptr);
    }
    s.addChild (pads, -1, nullptr);

    juce::ValueTree banks ("BANKS");
    for (int b = 0; b < kNumPatterns; ++b)
    {
        juce::ValueTree bk ("BANK");
        bk.setProperty ("i", b, nullptr);
        bk.setProperty ("len", engine.getPatternLength (b), nullptr);
        bk.setProperty ("inChain", patternActiveUI[(size_t) b], nullptr);

        // One hex word per step (16 pads = 16 bits), plus the step pitches —
        // compact enough to stay readable in the XML.
        juce::String steps, notes;
        for (int st = 0; st < kNumSteps; ++st)
        {
            int mask = 0;
            for (int p = 0; p < kNumPads; ++p)
                if (pattern[(size_t) b][(size_t) st][(size_t) p]) mask |= (1 << p);
            steps << juce::String::toHexString (mask) << " ";

            for (int p = 0; p < kNumPads; ++p)
                notes << engine.getStepNote (b, st, p) << " ";
        }
        bk.setProperty ("steps", steps.trim(), nullptr);
        bk.setProperty ("notes", notes.trim(), nullptr);
        banks.addChild (bk, -1, nullptr);
    }
    s.addChild (banks, -1, nullptr);
    return s;
}

void MainComponent::applyState (const juce::ValueTree& s)
{
    if (! s.hasType ("ZATI") && ! s.hasType ("COLORS")) return;   // COLORS: proyectos anteriores al renombrado

    ZatiColours::setSkin ((int) s.getProperty ("skin", 0));
    applySkin();

    bpmSlider.setValue ((double) s.getProperty ("bpm", 120.0), juce::sendNotification);

    if (auto fx = s.getChildWithName ("FX"); fx.isValid())
    {
        fxTypeButton.setToggleState ((int) fx.getProperty ("type", 0) != 0, juce::dontSendNotification);
        fxTypeButton.setButtonText (fxTypeButton.getToggleState() ? "HPF" : "LPF");
        engine.setFxType (fxTypeButton.getToggleState() ? 1 : 0);
        cutoffSlider.setValue  ((double) fx.getProperty ("cutoff", 20000.0), juce::sendNotification);
        resoSlider.setValue    ((double) fx.getProperty ("reso",   0.707),   juce::sendNotification);
        driveSlider.setValue   ((double) fx.getProperty ("drive",  0.0),     juce::sendNotification);
        dlyTimeSlider.setValue ((double) fx.getProperty ("dlyTime", 250.0),  juce::sendNotification);
        dlyFbSlider.setValue   ((double) fx.getProperty ("dlyFb",   0.35),   juce::sendNotification);
        dlyMixSlider.setValue  ((double) fx.getProperty ("dlyMix",  0.0),    juce::sendNotification);
    }

    if (auto pads = s.getChildWithName ("PADS"); pads.isValid())
    {
        for (const auto& p : pads)
        {
            const int i = (int) p.getProperty ("i", -1);
            if (! juce::isPositiveAndBelow (i, kNumPads)) continue;

            padName[(size_t) i]    = p.getProperty ("name", juce::String()).toString();
            padPitch[(size_t) i]   = (float) p.getProperty ("pitch", 0.0);
            padGain[(size_t) i]    = (float) p.getProperty ("gain", 0.85);
            padStart01[(size_t) i] = (float) p.getProperty ("start", 0.0);
            padEnd01[(size_t) i]   = (float) p.getProperty ("end", 1.0);
            padLoop[(size_t) i]    = (bool)  p.getProperty ("loop", false);
            padReverse[(size_t) i] = (bool)  p.getProperty ("reverse", false);
            padChokeUI[(size_t) i] = (int)   p.getProperty ("choke", 0);
            padPan[(size_t) i]     = (float) p.getProperty ("pan", 0.0);
            padAttack[(size_t) i]  = (float) p.getProperty ("attack", 2.0);
            padRelease[(size_t) i] = (float) p.getProperty ("release", 5.0);
            padZati[(size_t) i]    = (int)   p.getProperty ("zati", Zati::forPad (i));

            // Trim is stored 0..1 but the engine wants samples, and
            // publishSample has just reset the window to the whole file — so
            // it must be pushed back explicitly or every load plays untrimmed.
            if (const int len = engine.getSampleLength (i); len > 0)
            {
                engine.setPadStart (i, (int) (padStart01[(size_t) i] * len));
                engine.setPadEnd   (i, (int) (padEnd01[(size_t) i]   * len));
            }

            engine.setPadPitch   (i, padPitch[(size_t) i]);
            engine.setPadGain    (i, padGain[(size_t) i]);
            engine.setPadLoop    (i, padLoop[(size_t) i]);
            engine.setPadReverse (i, padReverse[(size_t) i]);
            engine.setPadChoke   (i, padChokeUI[(size_t) i]);
            engine.setPadPan     (i, padPan[(size_t) i]);
            engine.setPadAttack  (i, padAttack[(size_t) i]);
            engine.setPadRelease (i, padRelease[(size_t) i]);
        }
    }

    if (auto banks = s.getChildWithName ("BANKS"); banks.isValid())
    {
        for (const auto& bk : banks)
        {
            const int b = (int) bk.getProperty ("i", -1);
            if (! juce::isPositiveAndBelow (b, kNumPatterns)) continue;

            engine.setPatternLength (b, (int) bk.getProperty ("len", kMinPatLen));
            patternActiveUI[(size_t) b] = (bool) bk.getProperty ("inChain", false);
            if (auto* btn = patternButtons[b])
                btn->setToggleState (patternActiveUI[(size_t) b], juce::dontSendNotification);

            juce::StringArray st, nt;
            st.addTokens (bk.getProperty ("steps", "").toString(), " ", "");
            nt.addTokens (bk.getProperty ("notes", "").toString(), " ", "");
            st.removeEmptyStrings(); nt.removeEmptyStrings();

            for (int s2 = 0; s2 < kNumSteps; ++s2)
            {
                const int mask = s2 < st.size() ? (int) st[s2].getHexValue32() : 0;
                for (int p = 0; p < kNumPads; ++p)
                {
                    const bool on = (mask & (1 << p)) != 0;
                    pattern[(size_t) b][(size_t) s2][(size_t) p] = on;
                    engine.setStep (b, s2, p, on);

                    const int ni = s2 * kNumPads + p;
                    engine.setStepNote (b, s2, p, ni < nt.size() ? nt[ni].getIntValue() : 0);
                }
            }
        }
        rebuildChain();
    }

    for (int i = 0; i < kNumPads; ++i)
        if (auto* pb = pads[i]) pb->setZati (padZati[(size_t) i]);

    selectedPattern = juce::jlimit (0, kNumPatterns - 1, (int) s.getProperty ("selectedPattern", 0));
    patternSlider.setValue (selectedPattern, juce::dontSendNotification);   // 0-based; its text adds the +1
    patternSlider.updateText();
    engine.setEditPattern (selectedPattern);
    lengthSlider.setValue (engine.getPatternLength (selectedPattern), juce::dontSendNotification);

    setMacroBank ((int) s.getProperty ("macroBank", 0));
    selectPad (juce::jmax (0, selectedPad));
    for (int i = 0; i < kNumPads; ++i) refreshPad (i);
    resized();
    repaint();
}

void MainComponent::saveProject (const juce::String& rawName)
{
    const auto name   = ProjectStore::sanitise (rawName);
    const auto folder = ProjectStore::folderFor (name);
    folder.createDirectory();

    int written = 0, failed = 0;
    for (int i = 0; i < kNumPads; ++i)
    {
        const auto dest = ProjectStore::sampleFile (folder, i);
        if (auto sb = uiSample[(size_t) i]; sb != nullptr && sb->buffer.getNumSamples() > 0)
        {
            if (ProjectStore::writeSample (dest, sb->buffer, sb->sourceSampleRate)) ++written;
            else                                                                    ++failed;
        }
        else
        {
            dest.deleteFile();      // pad emptied since the last save
        }
    }

    const auto xml = captureState().toXmlString();
    const bool ok  = folder.getChildFile ("project.xml").replaceWithText (xml);

    currentProject = name;
    refreshProjectList();
    currentProject = name;

    status.setText (ok && failed == 0
                        ? "Guardado \"" + name + "\"  [" + juce::String (written) + " pads]"
                        : "Guardado con fallos: " + juce::String (failed) + " pads no se escribieron",
                    juce::dontSendNotification);
    projSheet.repaint();
}

void MainComponent::loadProject (const juce::String& name)
{
    const auto folder = ProjectStore::folderFor (name);
    const auto xmlFile = folder.getChildFile ("project.xml");
    if (! xmlFile.existsAsFile())
    {
        status.setText ("No encuentro el proyecto \"" + name + "\"", juce::dontSendNotification);
        return;
    }

    auto xml = juce::parseXML (xmlFile);
    if (xml == nullptr)
    {
        status.setText ("Proyecto ilegible: " + name, juce::dontSendNotification);
        return;
    }

    // Stop first: loading rewrites every pattern bank and pad under the
    // sequencer's feet otherwise.
    playButton.setToggleState (false, juce::dontSendNotification);
    playButton.setButtonText ("PLAY");
    engine.setPlaying (false);

    int restored = 0, missing = 0;
    for (int i = 0; i < kNumPads; ++i)
    {
        auto sb = ProjectStore::readSample (ProjectStore::sampleFile (folder, i));
        if (sb != nullptr) { assignSampleToPad (i, sb, padName[(size_t) i]); ++restored; }
        else
        {
            uiSample[(size_t) i] = nullptr;
            padHasSample[(size_t) i] = false;
            padName[(size_t) i] = {};
            if (auto* p = pads[i]) p->setSampleInfo (nullptr, {});
        }
    }

    applyState (juce::ValueTree::fromXml (*xml));

    // Names live in the state, so re-stamp the tiles after applyState.
    for (int i = 0; i < kNumPads; ++i)
        if (auto* p = pads[i])
            p->setSampleInfo (uiSample[(size_t) i], padName[(size_t) i]);

    for (const auto& c : juce::ValueTree::fromXml (*xml).getChildWithName ("PADS"))
        if ((bool) c.getProperty ("has", false)
            && uiSample[(size_t) (int) c.getProperty ("i", 0)] == nullptr)
            ++missing;

    currentProject = name;
    currentProject = name;
    closeAllSheets();
    status.setText ("Abierto \"" + name + "\"  [" + juce::String (restored) + " pads"
                        + (missing > 0 ? ", " + juce::String (missing) + " sin audio]" : "]"),
                    juce::dontSendNotification);
}

void MainComponent::deleteProject (const juce::String& name)
{
    ProjectStore::folderFor (name).deleteRecursively();
    if (currentProject == name)
    {
        currentProject = {};
        currentProject = {};
    }
    refreshProjectList();
    status.setText ("Borrado \"" + name + "\"", juce::dontSendNotification);
    projSheet.repaint();
}

void MainComponent::newProject()
{
    playButton.setToggleState (false, juce::dontSendNotification);
    playButton.setButtonText ("PLAY");
    engine.setPlaying (false);

    for (int i = 0; i < kNumPads; ++i)
    {
        uiSample[(size_t) i] = nullptr;
        padHasSample[(size_t) i] = false;
        padName[(size_t) i] = {};
        if (auto* p = pads[i]) p->setSampleInfo (nullptr, {});
    }
    for (int b = 0; b < kNumPatterns; ++b)
    {
        engine.clearPattern (b);
        engine.setPatternLength (b, kMinPatLen);
        for (auto& row : pattern[(size_t) b]) row.fill (false);
        patternActiveUI[(size_t) b] = false;
        if (auto* btn = patternButtons[b]) btn->setToggleState (false, juce::dontSendNotification);
    }
    rebuildChain();

    selectedPattern = 0;
    selectedStep = -1;
    currentProject = {};
    currentProject = {};
    selectPad (0);
    closeAllSheets();
    status.setText ("Proyecto nuevo", juce::dontSendNotification);
}

void MainComponent::refreshProjectList()
{
    projModel.names = ProjectStore::list();
    projList.updateContent();

    const int sel = projModel.names.indexOf (currentProject);
    if (sel >= 0) projList.selectRow (sel);
    else          projList.deselectAllRows();
    projList.repaint();
}

void MainComponent::paintProjSheetContent (juce::Graphics& g)
{
    if (projSheet.sheetBounds.isEmpty()) return;

    auto inner = projSheet.sheetBounds.reduced (12, 6);
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::monoFont (Metrics::fLabel, true).withExtraKerningFactor (0.14f));
    g.drawText ("PROYECTOS", inner.removeFromTop (16), juce::Justification::centredLeft);

    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.08f));
    g.drawText (currentProject.isNotEmpty()
                    ? "abierto: " + currentProject
                    : juce::String (projModel.names.isEmpty()
                                        ? "sin proyectos guardados - GUARDAR crea el primero"
                                        : "elige uno de la lista"),
                inner.removeFromTop (14), juce::Justification::centredLeft);
}

void MainComponent::launchSystemPicker()
{
    if (browseTargetPad < 0) return;
    const int index = browseTargetPad;

    chooser = std::make_unique<juce::FileChooser> (
        "Muestra para el pad " + juce::String (index + 1),
        juce::File{}, "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                        | juce::FileBrowserComponent::canSelectFiles,
        [this, index] (const juce::FileChooser& fc)
        {
            const auto url = fc.getURLResult();
            if (url.isEmpty()) return;

            closeAllSheets();
            const juce::String fileName = url.getFileName();
            status.setText ("Cargando pad " + juce::String (index + 1) + " ...", juce::dontSendNotification);
            loader.loadAsync (url, index, [this, index, fileName] (bool ok, juce::String detail, SampleBuffer::Ptr sb)
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
        });
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
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::monoFont (Metrics::fLabel, true).withExtraKerningFactor (0.14f));
    g.drawText ("CARGAR EN PAD " + juce::String (juce::jmax (0, browseTargetPad) + 1),
                inner.removeFromTop (16), juce::Justification::centredLeft);

    const bool picked = browser != nullptr && browser->getNumSelectedFiles() > 0
                     && browser->getSelectedFile (0).existsAsFile();
    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.08f));
    g.drawText (picked ? browser->getSelectedFile (0).getFileName()
                       : juce::String ("elige una muestra  -  wav / aiff / flac / ogg / mp3"),
                inner.removeFromTop (14), juce::Justification::centredLeft);
}

// REC on the transport arms PATTERN recording: pads you hit while the
// sequencer runs are written into the playing bank, quantised to the nearest
// step. Sampling from the mic is a per-pad action and lives in the PADS sheet.
void MainComponent::toggleRecordArm()
{
    recArmed = ! recArmed;
    styleButton (recButton, recArmed ? kRec : kKey);
    recButton.setButtonText (recArmed ? "REC ON" : "REC");

    if (recArmed && ! engine.isPlaying())
    {
        // Arming with the transport stopped is a dead end — roll it.
        playButton.setToggleState (true, juce::dontSendNotification);
        playButton.setButtonText ("STOP");
        engine.setPlaying (true);
    }

    status.setText (recArmed ? "REC: toca pads para grabarlos en el patron"
                             : "REC apagado",
                    juce::dontSendNotification);
    repaint();
}

void MainComponent::toggleMicSampling()
{
    if (! recordingActive)
    {
        int slot = (selectedPad >= 0) ? selectedPad : firstEmptyPad();
        if (slot < 0) slot = 0;

        // Ask for the mic explicitly: opening the input without the grant
        // silently yields a dead stream, which reads as "REC does nothing".
        using RP = juce::RuntimePermissions;
        auto begin = [this, slot]
        {
            recordingSlot = slot;
            setAudioChannels (1, 2);      // open mic input
            engine.startRecording (slot);
            recordingActive = true;
            styleButton (micButton, kRec);
            micButton.setButtonText ("PARAR");
            status.setText ("Grabando pad " + juce::String (slot + 1) + " ...", juce::dontSendNotification);
        };

        if (! RP::isRequired (RP::recordAudio) || RP::isGranted (RP::recordAudio))
        {
            begin();
        }
        else
        {
            RP::request (RP::recordAudio, [this, begin] (bool granted)
            {
                if (granted) begin();
                else status.setText ("Sin permiso de microfono: no puedo grabar",
                                     juce::dontSendNotification);
            });
        }
    }
    else
    {
        recordingActive = false;
        auto sb = engine.finishRecording();
        setAudioChannels (0, 2);          // release the mic input, back to output-only
        styleButton (micButton, kKey);
        micButton.setButtonText ("GRABAR MIC");
        if (sb != nullptr)
        {
            assignSampleToPad (recordingSlot, sb, "REC " + juce::String (recordingSlot + 1));
            status.setText ("Grabado en el pad " + juce::String (recordingSlot + 1), juce::dontSendNotification);
        }
        else
        {
            status.setText ("No se grabo nada", juce::dontSendNotification);
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
    // The grid reads the pattern straight from our mirror; just refresh it.
    refreshStepGrid();

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
        status.setText ("Grabando pad " + juce::String (recordingSlot + 1)
                        + "  " + juce::String (engine.getRecordSeconds(), 1) + "s",
                        juce::dontSendNotification);
}
