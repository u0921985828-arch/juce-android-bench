#include "MainComponent.h"

MainComponent::MainComponent()
{
    // 0 inputs (no RECORD_AUDIO needed for P0), 2 outputs.
    setAudioChannels (0, 2);

    addAndMakeVisible (padA);
    addAndMakeVisible (padB);
    addAndMakeVisible (loadBtn);
    addAndMakeVisible (status);

    padA.setEnabled (false);
    padB.setEnabled (false);

    padA.onClick = [this] { engine.postNoteOn (0, 0.0f, 0.8f); };   // slot 0, no pitch shift
    padB.onClick = [this] { engine.postNoteOn (1, 5.0f, 0.8f); };   // slot 1, +5 semitones

    loadBtn.onClick = [this] { openFileChooser(); };

    status.setJustificationType (juce::Justification::centred);
    status.setText ("Load a sample, then tap a pad.", juce::dontSendNotification);

    // Message-thread GC of retired sample buffers.
    startTimer (200);

    setSize (420, 260);
}

MainComponent::~MainComponent()
{
    // Stop the audio callback BEFORE members (engine) are destroyed.
    shutdownAudio();
}

// ---------------------------------------------------------------------------
//  Audio callbacks — forwarded straight into the engine.
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
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (16);

    status.setBounds (area.removeFromTop (28));
    area.removeFromTop (8);
    loadBtn.setBounds (area.removeFromTop (44));
    area.removeFromTop (12);

    juce::FlexBox pads;
    pads.flexDirection = juce::FlexBox::Direction::row;
    pads.items.add (juce::FlexItem (padA).withFlex (1.0f).withMargin (6));
    pads.items.add (juce::FlexItem (padB).withFlex (1.0f).withMargin (6));
    pads.performLayout (area);
}

void MainComponent::timerCallback()
{
    engine.collectRetiredSamples();
}

void MainComponent::openFileChooser()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Select an audio sample",
        juce::File{},
        "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3");

    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File{})
            return;   // cancelled

        // Serialize reloads: keep Load disabled until the decode completes and
        // the GC timer has had a chance to drain the retired buffer.
        loadBtn.setEnabled (false);
        status.setText ("Loading " + file.getFileName() + " ...", juce::dontSendNotification);

        loader.loadAsync (file, [this] (bool success)
        {
            loadBtn.setEnabled (true);
            if (success)
            {
                sampleLoaded = true;
                padA.setEnabled (true);
                padB.setEnabled (true);
                status.setText ("Loaded. Tap PAD A / PAD B.", juce::dontSendNotification);
            }
            else
            {
                status.setText ("Failed to load that file.", juce::dontSendNotification);
            }
        });
    });
}
