#include <JuceHeader.h>
#include "MainComponent.h"
#include "Lang.h"
#include "ProjectStore.h"
#include "UiAudit.h"
#include "StoreArt.h"

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

    //  El nombre que se lee: "Zati Sampler". El del PROYECTO sigue siendo Zati
    //  -da nombre a las rutas de compilacion- y el identificador de Android
    //  sigue siendo com.artifacts.zati, que ES la app y no se puede tocar sin
    //  convertirla en otra distinta.
    const juce::String getApplicationName() override       { return "Zati Sampler"; }
    const juce::String getApplicationVersion() override    { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override
    {
        //  Before the window: every caption the interface builds is read
        //  through the table, and a component built in one language and then
        //  retranslated flickers on the first frame.
        ProjectStore::ensureTree();
        Lang::loadPreference();
        //  Before the window: every component captures colours as it is built,
        //  so a chassis applied afterwards would leave half the face on the
        //  previous one until something forced a restyle.
        ZatiColours::loadSkinPreference();

        //  The audit run picks its own language: a dump is comparable across
        //  the four only if the language is an input, not whatever the last
        //  session happened to leave in the preferences file.
        if (UiAudit::enabled())
        {
            const auto want = UiAudit::env ("ZATI_LANG");
            for (int i = 0; i < Lang::numLanguages; ++i)
                if (want == Lang::code ((Lang::Id) i))
                    Lang::set ((Lang::Id) i);
        }

        mainWindow = std::make_unique<MainWindow> (getApplicationName());

        //  El banner de la ficha no necesita ni ventana ni maquetado: se
        //  dibuja, se escribe y se sale.
        if (const auto banner = UiAudit::env ("ZATI_BANNER"); banner.isNotEmpty())
        {
            const auto size = UiAudit::env ("ZATI_BANNER_SIZE");
            const int bw = size.contains ("x") ? size.upToFirstOccurrenceOf ("x", false, false).getIntValue() : 1024;
            const int bh = size.contains ("x") ? size.fromFirstOccurrenceOf ("x", false, false).getIntValue() : 500;
            StoreArt::writeFeature (banner, juce::jmax (16, bw), juce::jmax (16, bh));
            quit();
            return;
        }

        if (UiAudit::enabled())
            startAudit();
    }

    //  Lay out at the size asked for, open the sheet asked for, let the
    //  message loop settle the two, then measure and leave. Two ticks rather
    //  than one because opening a sheet triggers its own resized().
    void startAudit()
    {
        auto* c = content();
        if (c == nullptr) { quit(); return; }

        const auto size = UiAudit::env ("ZATI_SIZE");
        if (size.contains ("x"))
        {
            const int w = size.upToFirstOccurrenceOf ("x", false, false).getIntValue();
            const int h = size.fromFirstOccurrenceOf ("x", false, false).getIntValue();
            if (w > 0 && h > 0)
            {
                mainWindow->setSize (w, h);
                c->setSize (w, h);
            }
        }

        //  LEAVE THE APP AND COME BACK, n times, before measuring anything.
        //
        //  "I go out of the app for a while, or I lock the phone, and when I
        //  come back the sounds are no longer on the pads" is not a thing you
        //  can catch by looking at one launch. It is the suspend/resume pair,
        //  and it is exactly two calls.
        //
        //  But NOT YET: the session is restored on the first timer tick, and
        //  appSuspended autosaves whatever the machine holds. Suspending from
        //  here, before that tick, wrote an EMPTY state over the session's own
        //  state.xml - the bench destroying the thing it was built to measure,
        //  and reporting a clean pass while doing it. So the cycles wait for
        //  the same settle everything else waits for.
        const int cycles = UiAudit::env ("ZATI_CYCLE").getIntValue();

        juce::Timer::callAfterDelay (400, [this, cycles]
        {
            auto* cc = content();
            if (cc == nullptr) { quit(); return; }

            for (int i = 0; i < cycles; ++i)
            {
                cc->appSuspended();
                cc->appResumed();
            }

            //  El trabajo de mentira ANTES de abrir la ficha: cargar doce
            //  pads mueve la seleccion y vuelve a maquetar, y hacerlo despues
            //  dejaba la ficha abierta sobre un estado que ya no era el suyo.
            if (UiAudit::env ("ZATI_DEMO").isNotEmpty())
                cc->auditDemo();

            cc->auditOpen (UiAudit::env ("ZATI_OPEN"));

            juce::Timer::callAfterDelay (400, [this]
            {
                if (auto* c2 = content())
                {
                    c2->resized();

                    //  UNA FOTO, si la piden. Se saca del componente y no de
                    //  la pantalla del sistema: en el banco no hay pantalla, y
                    //  una captura del escritorio traeria el marco de la
                    //  ventana y el fondo del gestor.
                    const auto shot = UiAudit::env ("ZATI_SHOT");
                    if (shot.isNotEmpty())
                    {
                        const double s = UiAudit::env ("ZATI_SHOT_SCALE").getDoubleValue();
                        UiAudit::snapshot (*c2, shot, s > 0.05 ? (float) s : 1.0f);
                    }
                    else
                    {
                        UiAudit::dump (*c2);
                    }
                }
                quit();
            });
        });
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
