#include <JuceHeader.h>
#include "MainComponent.h"

//  Storage for the two Oboe dials declared in AudioPath.h. They live here so
//  that the patched JUCE module finds them at link time on Android, and so
//  that every other platform links a pair of harmless zeroes.
extern "C" int zatiOboeUsage    = 0;
extern "C" int zatiOboeForceI16 = 0;

// ============================================================================
//  Application entry — standard JUCEApplication + a resizable DocumentWindow
//  hosting MainComponent.
// ============================================================================
class ArtifactsApplication : public juce::JUCEApplication
{
public:
    ArtifactsApplication() = default;

    const juce::String getApplicationName() override       { return "Zati"; }
    const juce::String getApplicationVersion() override    { return "0.0.1"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    // ---- Top-level window ----
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (juce::String name)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);

           #if JUCE_IOS || JUCE_ANDROID
            setFullScreen (true);
           #else
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());
           #endif

            setVisible (true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (ArtifactsApplication)
