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
//  Todo lo que se puede tocar sin abrir un dialogo del sistema. Los botones
//  que llaman a FileChooser quedan fuera a proposito: en un banco sin pantalla
//  un dialogo nativo no vuelve, y la sesion se quedaria colgada ahi para
//  siempre en vez de medir nada.
static void recogeControles (juce::Component& c,
                             juce::Array<juce::Button*>& botones,
                             juce::Array<juce::Slider*>& mandos)
{
    if (! c.isVisible()) return;

    if (auto* b = dynamic_cast<juce::Button*> (&c))
    {
        const auto t = b->getButtonText().toUpperCase();
        if (! t.contains ("CARGAR") && ! t.contains ("LOAD") && ! t.contains ("EXPORT")
            && ! t.contains ("MIC") && ! t.contains ("REC"))
            botones.add (b);
    }
    else if (auto* s = dynamic_cast<juce::Slider*> (&c))
    {
        mandos.add (s);
    }

    for (auto* k : c.getChildren()) recogeControles (*k, botones, mandos);
}

static void fuzz (MainComponent& mc, int semilla, int sesiones, int acciones)
{
    static const char* kFichas[] = { "pads", "pad2", "pad3", "sec", "paso", "song",
                                     "mix", "xy", "set", "proj", "gest", "midi",
                                     "rack", "chop", "manual", "" };
    static const int kAnchos[] = { 280, 320, 360, 393, 412, 480, 653, 915 };

    int peorSolapes = 0, peorFuera = 0, estados = 0;
    juce::String culpableSolape, culpableFuera;

    for (int ses = 0; ses < sesiones; ++ses)
    {
        juce::Random r (semilla * 7919 + ses);

        const int w = kAnchos[r.nextInt (juce::numElementsInArray (kAnchos))];
        const int h = kAnchos[r.nextInt (juce::numElementsInArray (kAnchos))];
        mc.setSize (juce::jmax (280, w), juce::jmax (280, h));
        Lang::set ((Lang::Id) r.nextInt (4));
        ZatiColours::setSkin (r.nextInt (4));
        //  Sin applySkin ni retranslateUi: son privados y no hace falta
        //  llamarlos aqui - auditOpen y resized vuelven a maquetar, que es lo
        //  que mide esta prueba. La carcasa y el idioma se cambian igual, y
        //  el siguiente repintado los coge.
        mc.auditOpen (kFichas[r.nextInt (juce::numElementsInArray (kFichas))]);

        for (int a = 0; a < acciones; ++a)
        {
            juce::Array<juce::Button*> botones;
            juce::Array<juce::Slider*> mandos;
            recogeControles (mc, botones, mandos);

            if (r.nextInt (5) == 0)
            {
                mc.auditOpen (kFichas[r.nextInt (juce::numElementsInArray (kFichas))]);
            }
            else if (! mandos.isEmpty() && r.nextBool())
            {
                auto* s = mandos[r.nextInt (mandos.size())];
                const auto lo = s->getMinimum(), hi = s->getMaximum();
                s->setValue (lo + r.nextDouble() * (hi - lo), juce::sendNotificationSync);
            }
            else if (! botones.isEmpty())
            {
                botones[r.nextInt (botones.size())]->triggerClick();
            }

            mc.resized();

            const auto hal = UiAudit::check (mc);
            ++estados;
            if (hal.solapes > peorSolapes)
            {
                peorSolapes = hal.solapes;
                culpableSolape = juce::String (semilla * 7919 + ses) + " " + juce::String (w) + "x" + juce::String (h)
                               + " accion " + juce::String (a);
            }
            if (hal.fuera > peorFuera)
            {
                peorFuera = hal.fuera;
                culpableFuera = juce::String (semilla * 7919 + ses) + " " + juce::String (w) + "x" + juce::String (h)
                              + " accion " + juce::String (a);
            }
        }
    }

    std::cout << "{\"fuzz\":1,\"estados\":" << estados
              << ",\"solapes\":" << peorSolapes
              << ",\"fuera\":" << peorFuera
              << ",\"solape_en\":\"" << culpableSolape << "\""
              << ",\"fuera_en\":\"" << culpableFuera << "\"}" << std::endl;
}

class ArtifactsApplication : public juce::JUCEApplication
{
public:
    ArtifactsApplication() = default;

    //  El nombre que se lee: "Zati Sampler". El del PROYECTO sigue siendo Zati
    //  -da nombre a las rutas de compilacion- y el identificador de Android
    //  sigue siendo com.artifacts.zati, que ES la app y no se puede tocar sin
    //  convertirla en otra distinta.
    //
    //  Estuvo una corrida diciendo "ARTiFACTS ZATI" porque una pregunta sobre
    //  como se llamaba el fichero de la release se leyo como una correccion
    //  del nombre de la app. ARTiFACTS es el estudio y firma esto; el nombre
    //  de la app es Zati Sampler.
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
                    //  MUCHA GENTE TOCANDO, no un estado escogido.
                    //
                    //  ZATI_FUZZ=semilla,sesiones,acciones. Cada sesion sortea
                    //  un tamano, un idioma, una carcasa y una tirada de
                    //  acciones - abrir fichas, mover mandos, cambiar de banco
                    //  y de pagina - y despues de CADA una se comprueban las
                    //  dos reglas que no dependen del idioma. Un estado por
                    //  repintado en vez de uno por arranque de proceso: expo.py
                    //  mide 476 combinaciones fijas y esto mide las que a nadie
                    //  se le ocurrieron.
                    //  La misma comprobacion sobre el estado FIJO que pide
                    //  ZATI_SIZE/ZATI_OPEN, para poder contrastarla con lo que
                    //  dice expo.py del mismo estado. Una regla escrita dos
                    //  veces -aqui en C++ y alli en Python- que no se contrasta
                    //  es dos reglas.
                    else if (UiAudit::env ("ZATI_CHECK").isNotEmpty())
                    {
                        const auto h = UiAudit::check (*c2);
                        std::cout << "{\"check\":1,\"mirados\":" << h.mirados
                                  << ",\"solapes\":" << h.solapes
                                  << ",\"fuera\":" << h.fuera << "}" << std::endl;
                    }
                    else if (const auto fz = UiAudit::env ("ZATI_FUZZ"); fz.isNotEmpty())
                    {
                        juce::StringArray part;
                        part.addTokens (fz, ",", "");
                        const int semilla  = part.size() > 0 ? part[0].getIntValue() : 1;
                        const int sesiones = part.size() > 1 ? juce::jmax (1, part[1].getIntValue()) : 200;
                        const int acciones = part.size() > 2 ? juce::jmax (1, part[2].getIntValue()) : 30;
                        fuzz (*c2, semilla, sesiones, acciones);
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
