#include <JuceHeader.h>
#include "MainComponent.h"
#include "Lang.h"
#include "ProjectStore.h"

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
    const juce::String getApplicationVersion() override    { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override
    {
        //  Before the window: every caption the interface builds is read
        //  through the table, and a component built in one language and then
        //  retranslated flickers on the first frame.
        ProjectStore::ensureTree();
        Lang::loadPreference();

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

    //  Android calls these from the activity's onPause/onResume. Without them
    //  the audio stream, the microphone and the unsaved session all survived
    //  into the background, where none of the three does anything useful.
    void suspended() override   { if (auto* c = content()) c->appSuspended(); }
    void resumed()   override   { if (auto* c = content()) c->appResumed(); }

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
    MainComponent* content() const
    {
        return mainWindow != nullptr
                 ? dynamic_cast<MainComponent*> (mainWindow->getContentComponent())
                 : nullptr;
    }

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (ArtifactsApplication)
