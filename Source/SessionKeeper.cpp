#include "SessionKeeper.h"
#include "ProjectStore.h"

SessionKeeper::SessionKeeper() : juce::Thread ("zati-session")
{
    //  NOT background priority.
    //
    //  This thread's whole job is to get the user's work onto disk before
    //  Android decides to reclaim the process, and that decision can arrive at
    //  any moment. A background-priority thread on a busy phone may not be
    //  scheduled for seconds at a time, which leaves exactly the window this
    //  file exists to close: sixteen pads chopped, the app backgrounded, the
    //  process reaped, and half the WAVs never written.
    //
    //  It still never touches the message thread or the audio thread, so
    //  normal priority costs the instrument nothing.
    ownedBy.fill (-1);
    startThread (juce::Thread::Priority::normal);
}

SessionKeeper::~SessionKeeper()
{
    //  A write in flight is a WAV that would be left half-finished, so give it
    //  a moment to land before the thread is cut.
    stopThread (3000);
}

juce::File SessionKeeper::folder()
{
    //  Dot-prefixed: this is not the user's content, and it should not turn up
    //  in a file manager or the media scanner next to their projects.
    return ProjectStore::home().getChildFile (".sesion");
}

juce::File SessionKeeper::stateFile() { return folder().getChildFile ("state.xml"); }

juce::File SessionKeeper::padFile (int pad)
{
    return folder().getChildFile ("samples")
                   .getChildFile ("pad" + juce::String (pad + 1).paddedLeft ('0', 2) + ".wav");
}

//  UN WAV POR SONIDO, NO POR PAD.
//
//  Dieciseis pads de un AUTO CHOP apuntan al MISMO SampleBuffer y solo se
//  diferencian por su recorte. Esto escribia uno por pad: dieciseis copias del
//  mismo break en disco -sesenta megas por un troceado de cuatro segundos- y,
//  peor, dieciseis buffers distintos al volver, o sea el troceado convertido en
//  dieciseis sonidos sueltos que casualmente suenan igual. Quien los vuelve a
//  unir es captureState, que guarda de que pad sale cada uno; lo que hace falta
//  aqui es no escribir los que salen de otro.
//
//  El DUENO es el pad de indice mas bajo que comparte el buffer, y se recalcula
//  entero en cada sync porque puede cambiar sin que el buffer cambie: vaciar el
//  pad 1 de un troceado deja al 2 de dueno, y su puntero no se ha movido, asi
//  que sin recalcular nadie escribiria ese WAV y el troceado entero se perderia
//  al siguiente arranque.
void SessionKeeper::sync (const SampleBuffer::Ptr* live, int numPads)
{
    bool anything = false;
    const int n = juce::jmin (numPads, kMaxPads);
    {
        const juce::ScopedLock sl (lock);

        for (int i = 0; i < n; ++i)
        {
            int dueno = -1;
            if (live[i] != nullptr)
            {
                dueno = i;
                for (int j = 0; j < i; ++j)
                    if (live[j].get() == live[i].get()) { dueno = j; break; }
            }

            const bool cambioBuffer = live[i].get() != seen[(size_t) i].get();
            const bool cambioDueno  = dueno != ownedBy[(size_t) i];

            if (! cambioBuffer && ! cambioDueno)
                continue;

            seen[(size_t) i]    = live[i];
            ownedBy[(size_t) i] = dueno;

            if (dueno == i || dueno < 0)
            {
                //  Dueno, o vacio: se escribe (o se borra) su fichero.
                queued[(size_t) i] = live[i];
                dirty[(size_t) i]  = true;
                anything = true;
            }
            else
            {
                //  Sale de otro: nada que escribir, y si tenia fichero propio
                //  de antes sobra - lo borra el escritor, que es quien puede
                //  tocar el disco.
                queued[(size_t) i] = nullptr;
                dirty[(size_t) i]  = true;
                anything = true;
            }
        }
    }

    if (anything)
        notify();
}

void SessionKeeper::adopt (const SampleBuffer::Ptr* live, int numPads)
{
    const juce::ScopedLock sl (lock);

    for (int i = 0; i < juce::jmin (numPads, kMaxPads); ++i)
    {
        seen[(size_t) i]   = live[i];
        queued[(size_t) i] = nullptr;
        dirty[(size_t) i]  = false;

        //  Y el dueno tambien, o el primer sync tras recuperar veria que
        //  "cambio" y reescribiria los sesenta y cuatro ficheros que acaba de
        //  leer.
        int dueno = -1;
        if (live[i] != nullptr)
        {
            dueno = i;
            for (int j = 0; j < i; ++j)
                if (live[j].get() == live[i].get()) { dueno = j; break; }
        }
        ownedBy[(size_t) i] = dueno;
    }
}

void SessionKeeper::writeState (const juce::ValueTree& state, const juce::String& projectName)
{
    //  El otro lado de la misma carrera: este hilo crea .sesion mientras el de
    //  sesion crea .sesion/samples, y el createDirectory de JUCE se da por
    //  vencido si el mkdir devuelve EEXIST. Ver ProjectStore::ensureDirectory.
    if (! ProjectStore::ensureDirectory (folder())) return;

    //  The name of the open project rides along in the session's own copy of
    //  the tree, so coming back restores the header too - and a project.xml
    //  written from the same state never carries it.
    auto copy = state.createCopy();
    copy.setProperty ("sesion", true, nullptr);
    copy.setProperty ("proyecto", projectName, nullptr);

    //  Written beside the real name, read back, and only then moved into
    //  place. replaceWithText hides two failures at once - it discards the
    //  result of the append, and so does the caller - so a write that ran out
    //  of space halfway renamed a TRUNCATED file over a good one and returned
    //  true. Next launch parseXML gives nullptr, restoreSession bails, and the
    //  whole session is gone even though all sixteen WAVs are intact beside
    //  it. Nothing anywhere saw an error.
    const auto text = copy.toXmlString();
    const auto tmp  = stateFile().getSiblingFile ("state.xml.tmp");

    tmp.deleteFile();
    if (! tmp.replaceWithText (text))
    {
        tmp.deleteFile();
        return;
    }

    if (juce::parseXML (tmp) == nullptr)     // the only check that means anything
    {
        tmp.deleteFile();
        return;
    }

    //  Mover encima, sin borrar antes: rename(2) sobreescribe y es atomico, y
    //  el deleteFile() que habia aqui solo creaba un instante - corto, pero
    //  real, y este proceso lo mata Android sin avisar - en el que existia el
    //  temporal validado y NO existia la sesion. Justo lo que este fichero
    //  entero se escribio para que no pasara.
    tmp.moveFileTo (stateFile());
}

bool SessionKeeper::isIdle() const
{
    const juce::ScopedLock sl (lock);

    if (writing)
        return false;

    for (int i = 0; i < kMaxPads; ++i)
        if (dirty[(size_t) i])
            return false;

    return true;
}

bool SessionKeeper::flush (int timeoutMs)
{
    //  A flush is somebody waiting, and the writer should push harder while
    //  somebody is. It has to raise its OWN priority, though: JUCE's
    //  setPriority asserts that the caller is the thread being changed, and
    //  the Android implementation re-nices gettid() - so calling it from here
    //  boosted the MESSAGE thread and left the writer exactly where it was.
    //  The flag is read by the writer at the top of each item.
    hurry.store (true, std::memory_order_release);
    const juce::ScopeGuard slowDown { [this] { hurry.store (false, std::memory_order_release); } };

    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax (0, timeoutMs);

    while (! isIdle())
    {
        if (juce::Time::getMillisecondCounter() >= deadline)
            return false;

        juce::Thread::sleep (20);
    }

    return true;
}

void SessionKeeper::clear()
{
    {
        const juce::ScopedLock sl (lock);
        for (int i = 0; i < kMaxPads; ++i)
        {
            seen[(size_t) i]    = nullptr;
            queued[(size_t) i]  = nullptr;
            dirty[(size_t) i]   = false;
            ownedBy[(size_t) i] = -1;
        }
    }

    //  Only an in-flight write is left now, and it would recreate the folder
    //  under us: wait for it, then take the whole thing away.
    flush (1000);
    folder().deleteRecursively();
}

void SessionKeeper::run()
{
    while (! threadShouldExit())
    {
        int pad = -1;
        SampleBuffer::Ptr sb;

        {
            const juce::ScopedLock sl (lock);

            for (int i = 0; i < kMaxPads; ++i)
            {
                if (! dirty[(size_t) i])
                    continue;

                pad = i;
                sb  = queued[(size_t) i];
                dirty[(size_t) i]  = false;
                queued[(size_t) i] = nullptr;
                writing = true;
                break;
            }
        }

        if (pad < 0)
        {
            wait (-1);          // nothing to do: sleep until sync() pokes us
            continue;
        }

        //  Somebody is waiting on a flush: this is its own thread, so it may
        //  say so.
        setPriority (hurry.load (std::memory_order_acquire) ? juce::Thread::Priority::high
                                                            : juce::Thread::Priority::normal);

        const auto dest = padFile (pad);

        //  UN INSTRUMENTO NO SE ESCRIBE. Va como receta en state.xml -familia
        //  y preset- y al volver se sintetiza: escribir el audio serian 2 MB
        //  por pad para devolver algo que ya no seria un instrumento. Ver
        //  captureState y MainComponent::stepPadJob.
        if (sb != nullptr && sb->familia >= 0)
        {
            dest.deleteFile();
        }
        else if (sb != nullptr && sb->buffer.getNumSamples() > 0)
        {
            //  Escribir al lado y mover ya lo hace writeSample por dentro -
            //  se metio ahi porque saveProject llamaba directo al destino y no
            //  tenia esta red. Envolverlo otra vez aqui no anadia nada y si
            //  quitaba: el deleteFile() antes del moveFileTo abria una ventana
            //  sin fichero ninguno que rename(2) no tiene.
            ProjectStore::writeSample (dest, sb->buffer, sb->sourceSampleRate);
        }
        else
        {
            dest.deleteFile();      // the pad was emptied
        }

        sb = nullptr;               // release it here, off the message thread

        {
            const juce::ScopedLock sl (lock);
            writing = false;
        }
    }
}
