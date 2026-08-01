#include "MainComponent.h"

namespace
{
    const juce::Colour kBg        { 0xff15171c };   // near-black background
    const juce::Colour kPadEmpty  { 0xff262a33 };   // dim slate (no sample)
    const juce::Colour kPadLoaded { 0xff1fb6a6 };   // teal accent (has sample)
    const juce::Colour kLoadOn    { 0xffe0a13a };   // amber (LOAD mode active)
}

MainComponent::MainComponent()
{
    setAudioChannels (0, 2);   // 0 in (no RECORD_AUDIO), 2 out

    for (int i = 0; i < kNumPads; ++i)
    {
        auto* p = new juce::TextButton (juce::String (i + 1));
        p->setColour (juce::TextButton::textColourOffId, juce::Colours::white.withAlpha (0.85f));
        p->onClick = [this, i] { padClicked (i); };
        addAndMakeVisible (p);
        pads.add (p);
        refreshPad (i);
    }

    loadButton.setClickingTogglesState (true);
    loadButton.setColour (juce::TextButton::buttonColourId,   kPadEmpty);
    loadButton.setColour (juce::TextButton::buttonOnColourId, kLoadOn);
    loadButton.onClick = [this]
    {
        loadMode = loadButton.getToggleState();
        status.setText (loadMode ? "LOAD: tap a pad to assign a sample"
                                 : "Tap a pad to play. LOAD to assign samples.",
                        juce::dontSendNotification);
    };
    addAndMakeVisible (loadButton);

    status.setJustificationType (juce::Justification::centred);
    status.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));
    status.setText ("Tap a pad to play. LOAD to assign samples.", juce::dontSendNotification);
    addAndMakeVisible (status);

    startTimer (200);            // retired-buffer GC
    setSize (480, 640);
}

MainComponent::~MainComponent()
{
    shutdownAudio();             // stop callback before members die
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
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (14);

    status.setBounds (area.removeFromTop (26));
    area.removeFromTop (6);
    loadButton.setBounds (area.removeFromTop (44).reduced (2));
    area.removeFromTop (10);

    juce::Grid grid;
    using Track = juce::Grid::TrackInfo;
    using Fr    = juce::Grid::Fr;
    grid.templateColumns = { Track (Fr (1)), Track (Fr (1)), Track (Fr (1)), Track (Fr (1)) };
    grid.templateRows    = { Track (Fr (1)), Track (Fr (1)), Track (Fr (1)), Track (Fr (1)) };
    grid.setGap (juce::Grid::Px (8));

    for (auto* p : pads)
        grid.items.add (juce::GridItem (*p));

    grid.performLayout (area);
}

void MainComponent::padClicked (int index)
{
    if (loadMode)
        openChooserForPad (index);
    else if (padHasSample[(size_t) index])
        engine.postNoteOn (index, 0.0f, 0.85f);   // one-shot, original pitch
}

void MainComponent::refreshPad (int index)
{
    if (auto* p = pads[index])
    {
        const bool has = padHasSample[(size_t) index];
        p->setColour (juce::TextButton::buttonColourId, has ? kPadLoaded : kPadEmpty);
    }
}

void MainComponent::openChooserForPad (int index)
{
    chooser = std::make_unique<juce::FileChooser> (
        "Assign a sample to pad " + juce::String (index + 1),
        juce::File{},
        "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3");

    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (flags, [this, index] (const juce::FileChooser& fc)
    {
        const auto url = fc.getURLResult();          // Android-safe (content:// URL)
        if (url.isEmpty())
            return;                                  // cancelled

        status.setText ("Loading pad " + juce::String (index + 1) + " ...",
                        juce::dontSendNotification);

        loader.loadAsync (url, index, [this, index] (bool ok)
        {
            if (ok)
            {
                padHasSample[(size_t) index] = true;
                refreshPad (index);
                status.setText ("Pad " + juce::String (index + 1) + " loaded.",
                                juce::dontSendNotification);
            }
            else
            {
                status.setText ("Couldn't load that file (try WAV/FLAC/MP3).",
                                juce::dontSendNotification);
            }
        });
    });
}

void MainComponent::timerCallback()
{
    engine.collectRetiredSamples();
}
