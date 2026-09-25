#pragma once

#include <cstdio>

#include <JuceHeader.h>
#include "SampleBuffer.h"
#include "AppStorage.h"

// ============================================================================
//  ProjectStore — the ZATI folder tree, and where a project lives inside it.
//
//  The app owns ONE home directory and creates the whole tree on first run, so
//  there is always somewhere obvious to put things:
//
//      ZATI/
//        Samples/      drop your own audio here — the browser opens here
//        Projects/     one folder per project (see below)
//        Presets/      saved pad settings
//        Recordings/   what REC captures from the mic
//        Exports/      bounces
//
//  Home is the user's Music folder when that is writable, because a sampler is
//  useless if you cannot get audio into it: Music is visible over USB and in
//  any file manager, so you can drop a pack in from the desktop. If it is not
//  writable (an Android version that hides shared storage behind a permission
//  the user declined) it falls back to app-data, which is always writable —
//  saving must never fail because of a permission prompt.
//
//  A project is a FOLDER, not a single file:
//
//      Projects/<name>/
//          project.xml        the whole machine state
//          samples/pad01.wav  a copy of every loaded pad
//
//  Copying the audio in is deliberate. Storing paths would be smaller, but a
//  pad recorded with REC has no file behind it at all, and any sample the user
//  later moves or deletes would silently empty a pad. A self-contained folder
//  survives both, and can be copied to another device as-is.
// ============================================================================
class ProjectStore
{
public:
    // The tree's home. Resolved once, then remembered.
    //  ASK BY WRITING, not by asking.
    //
    //  This used to pick Music whenever hasWriteAccess() said yes, and on a
    //  modern Android that question does not mean what it says: shared storage
    //  answers "yes, writable" to a stat() and then refuses the write, because
    //  scoped storage decides per app and not per directory bit. The result is
    //  the worst kind of failure - GUARDAR appears to work, the folder is
    //  never created, and the project is gone the next time you open the app.
    //
    //  So the probe is a real file: create it, write a byte, read it back,
    //  delete it. Anywhere that survives that is somewhere we can keep your
    //  work; anywhere that does not is not, whatever its permission bits say.
    //  App-data is the fallback and is always writable - a save must never
    //  fail because shared storage changed its mind.
    static bool canReallyWriteInto (const juce::File& dir)
    {
        if (dir == juce::File()) return false;
        if (! dir.createDirectory()) return false;

        auto probe = dir.getChildFile (".zati-write-test");
        probe.deleteFile();
        if (! probe.replaceWithText ("z")) return false;

        const bool ok = probe.existsAsFile() && probe.loadFileAsString() == "z";
        probe.deleteFile();
        return ok;
    }

    //  WHERE THE LIBRARY LIVES - and it must not move.
    //
    //  This used to be a pure probe: try shared Music, then the app's external
    //  files folder, then internal app data, first one that accepts a real
    //  write wins. Correct on any single launch and quietly catastrophic
    //  across two, because the probe has no memory. externalFilesDir() is a
    //  JNI call; let it fail once - the context not ready, a storage volume
    //  still mounting, an OEM quirk - and the app silently relocates its whole
    //  library to the internal folder. Everything written before, including
    //  the invisible session that holds the sounds on your pads, is still on
    //  disk and is now in a place nothing looks at. The pads come back empty
    //  and nothing anywhere says why.
    //
    //  So the choice is made once and REMEMBERED, in the one directory on
    //  Android that can never move: the app's own internal data. The probe
    //  only runs when there is nothing remembered, or when what was remembered
    //  is gone.
    static juce::File anchorFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("zati-home.txt");
    }

    static juce::File home()
    {
        static juce::File cached = []
        {
            //  1. Where we put it last time. This is the answer on every
            //     launch but the first, which is the whole point.
            //
            //  And it is honoured even when the probe FAILS today, which is
            //  the part that took a second pass to get right. "Not writable
            //  right now" and "gone for good" look identical from here -
            //  external storage still mounting at cold boot, a permission
            //  revoked and re-granted, an OEM volume quirk - and re-probing on
            //  the first of those, then rewriting the anchor with the answer,
            //  relocates the whole library permanently and erases the only
            //  record of where it used to be. That is the exact disaster this
            //  anchor was added to prevent, reintroduced one line lower down.
            //
            //  So: if the remembered directory EXISTS, it wins, writable today
            //  or not. Only a path that is gone, or an anchor that was never
            //  written, sends us back to the probe.
            if (const auto a = anchorFile(); a.existsAsFile())
            {
                const auto text = a.loadFileAsString().trim();
                if (text.isNotEmpty() && juce::File::isAbsolutePath (text))
                {
                    const juce::File remembered (text);
                    if (remembered.isDirectory())
                        return remembered;
                }
            }

            const auto chosen = [] () -> juce::File
            {
                auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
                if (music != juce::File())
                {
                    auto candidate = music.getChildFile ("ZATI");
                    if (canReallyWriteInto (candidate))
                        return candidate;
                }

                //  Shared Music said no. Next best is the app's OWN folder on
                //  external storage: writable with no permission, and - unlike
                //  internal app data - it turns up over a USB cable, which is
                //  how a sample pack actually gets onto a phone. A library
                //  nothing can reach is not a library.
                if (auto ext = AppStorage::externalFilesDir(); ext != juce::File())
                {
                    auto candidate = ext.getChildFile ("ZATI");
                    if (canReallyWriteInto (candidate))
                        return candidate;
                }

                //  Last resort. Always writable, visible to nothing - but a
                //  save that lands somewhere private beats a save that does
                //  not land.
                auto fallback = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                    .getChildFile ("ZATI");
                fallback.createDirectory();
                return fallback;
            }();

            //  Remember it before anything is written into it.
            anchorFile().getParentDirectory().createDirectory();
            anchorFile().replaceWithText (chosen.getFullPathName());
            return chosen;
        }();
        return cached;
    }

    //  DE DONDE SE CARGA UN SONIDO Y DONDE CAE UNO GRABADO, y se puede elegir.
    //
    //  Del telefono, en la misma frase que la de proyectos. Es la carpeta del
    //  navegador de muestras y la que recibe lo que REC captura, o sea las dos
    //  puntas del mismo camino, y por eso es UNA: quien tiene su banco de
    //  sonidos en la carpeta de descargas la quiere para las dos cosas.
    //
    //  Y cae al defecto si la elegida ya no acepta escritura, que en un movil no
    //  es raro -una tarjeta desmontada, un permiso revocado-: `carpeta` lo
    //  comprueba escribiendo un byte cada vez. Un navegador que abre en una
    //  carpeta muerta se lee como que la app perdio los sonidos.
    static juce::File samples()    { return carpeta (Carpeta::samples); }
    static juce::File presets()    { return sub ("Presets"); }
    //  LOS KITS QUE HACE LA PERSONA, en la biblioteca y no dentro de un
    //  proyecto: un kit existe para usarse en OTRO proyecto, que es lo que lo
    //  distingue de los sonidos que un proyecto ya lleva copiados dentro.
    //
    //  Y en la misma forma que un banco descargado de internet -una carpeta con
    //  audios numerados- a proposito: asi el que se guarda aqui y el que te
    //  bajas entran por la MISMA puerta, que es CARGAR KIT ordenando por
    //  nombre. Un formato propio habria sido una segunda forma de hacer lo
    //  mismo, y ademas la unica que no sabria leer nadie mas.
    static juce::File kits()       { return sub ("Kits"); }
    //  EL CONTENIDO DESCARGABLE, en la biblioteca y con la misma forma que un
    //  kit: una carpeta de carpetas de audios numerados. Ver Instrumentos.h -
    //  se eligio asi para que un pack se pueda montar a mano, mirar desde el
    //  gestor de ficheros y arreglar cuando algo salga mal, que es lo que un
    //  formato propio no deja hacer.
    static juce::File instrumentos() { return sub ("Instrumentos"); }
    static juce::File recordings() { return sub ("Recordings"); }

    //  LAS TRES CARPETAS QUE SE PUEDEN ELEGIR, Y UN SOLO MECANISMO.
    //
    //  Empezo con UNA -donde cae el rebote-: cargar un sonido abre un navegador
    //  y se elige de donde, y sacarlo no preguntaba nada. Llego del telefono la
    //  otra mitad -«molaria poder elegir cual es la carpeta predeterminada para
    //  apertura y guardar proyectos, abrir y guardar samples»- y con eso son
    //  TRES, que es justo el numero a partir del cual copiar el mecanismo deja
    //  de ser barato: *una regla escrita tres veces son tres reglas, y la
    //  tercera es la que un dia se escribe mal*. La comprobacion de escritura,
    //  la vuelta al defecto y el fichero de preferencia son los mismos para las
    //  tres, asi que se escriben una vez.
    //
    //  La eleccion es de la PERSONA y no del proyecto, asi que vive donde el
    //  idioma y la carcasa: en el directorio interno de la app, legible antes
    //  de que nadie haya decidido donde esta la biblioteca.
    //
    //  Y NO ES LO MISMO QUE `home()`. La biblioteca se ANCLA y no se mueve
    //  -esa es la invariante que costo una sesion huerfana- y esto es otra
    //  cosa: una preferencia por encima de ella, que si no vale se cae al
    //  defecto de siempre sin tocar el ancla. Un fichero por carpeta y no uno
    //  con tres lineas, que asi borrar una eleccion no puede llevarse las otras
    //  dos por delante.
    enum class Carpeta { proyectos, samples, exports };

    static const char* nombreDe (Carpeta q) noexcept
    {
        switch (q)
        {
            case Carpeta::proyectos: return "Projects";
            case Carpeta::samples:   return "Samples";
            case Carpeta::exports:   break;
        }
        return "Exports";
    }

    static juce::File prefDe (Carpeta q)
    {
        const char* f = q == Carpeta::proyectos ? "zati-proyectos.txt"
                      : q == Carpeta::samples   ? "zati-samples.txt"
                                                : "zati-exportar.txt";
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile (f);
    }

    static juce::File porDefecto (Carpeta q) { return sub (nombreDe (q)); }

    //  La elegida SI SIGUE VALIENDO, y si no la de siempre. Se comprueba cada
    //  vez y no solo al elegirla: una carpeta de una tarjeta que ya no esta
    //  montada existe en el fichero de preferencias y no en el aparato, y un
    //  rebote de cuarenta segundos no puede terminar en un sitio que no acepta
    //  escritura. Comprobar es barato - un fichero de un byte - y equivocarse
    //  aqui cuesta la unica accion de esta app que no se deshace tocando otra
    //  vez.
    //
    //  Y AHORA TAMBIEN CUESTA UN PROYECTO. Con la carpeta de proyectos elegida
    //  fuera, esta misma pregunta es la que decide si GUARDAR encuentra donde
    //  escribir: caer al defecto es peor que fallar solo si nadie lo dice, asi
    //  que el que llama se entera por `elegidaVale`.
    static juce::File carpeta (Carpeta q)
    {
        const auto f = prefDe (q);
        if (f.existsAsFile())
        {
            const auto ruta = f.loadFileAsString().trim();
            if (ruta.isNotEmpty())
            {
                juce::File elegida (ruta);
                if (canReallyWriteInto (elegida))
                    return elegida;
            }
        }
        return porDefecto (q);
    }

    //  Si hay una elegida Y sigue aceptando escritura. Es lo que separa «no has
    //  elegido» de «elegiste una que ya no esta», que para quien mira la ficha
    //  son dos frases distintas.
    static bool elegidaVale (Carpeta q)
    {
        const auto f = prefDe (q);
        if (! f.existsAsFile()) return false;
        const auto ruta = f.loadFileAsString().trim();
        return ruta.isNotEmpty() && canReallyWriteInto (juce::File (ruta));
    }

    //  Devuelve false si la carpeta no acepta una escritura de verdad, que en
    //  Android es la mitad de las que se pueden LISTAR: el navegador entra en
    //  ellas y el sistema no deja dejar nada dentro. Se dice al elegirla y no
    //  al terminar el rebote.
    static bool setCarpeta (Carpeta q, const juce::File& dir)
    {
        if (! canReallyWriteInto (dir)) return false;
        prefDe (q).getParentDirectory().createDirectory();
        prefDe (q).replaceWithText (dir.getFullPathName());
        return true;
    }

    static void olvidaCarpeta (Carpeta q) { prefDe (q).deleteFile(); }
    static bool hayElegida (Carpeta q)    { return prefDe (q).existsAsFile(); }

    //  Los tres nombres de antes, que siguen valiendo y no se tocan: quince
    //  sitios llaman a `exports()` y renombrarlos seria un cambio de esta tanda
    //  que no arregla nada. Lo que era un mecanismo pasa a ser una ventana.
    static juce::File exportPrefFile()     { return prefDe (Carpeta::exports); }
    static juce::File exportsPorDefecto()  { return porDefecto (Carpeta::exports); }
    static juce::File exports()            { return carpeta (Carpeta::exports); }
    static bool setExports (const juce::File& dir) { return setCarpeta (Carpeta::exports, dir); }
    static void clearExports()             { olvidaCarpeta (Carpeta::exports); }
    static bool exportsElegida()           { return hayElegida (Carpeta::exports); }

    // Creates the whole tree. Safe to call every launch.
    static void ensureTree()
    {
        for (auto* n : { "Samples", "Projects", "Presets", "Recordings", "Exports" })
            home().getChildFile (n).createDirectory();
    }

    //  DONDE VIVEN LOS PROYECTOS, y se puede elegir.
    //
    //  Del telefono: «molaria poder elegir cual es la carpeta predeterminada
    //  para apertura y guardar proyectos». Es la misma carpeta para las dos
    //  cosas a proposito y no por ahorro: abrir de un sitio y guardar en otro
    //  es como se pierde un proyecto sin que falle nada -lo guardas, la lista
    //  no lo enseña, y no hay nada que mirar-.
    //
    //  Y ESTO NO MUEVE LA BIBLIOTECA. `home()` se ancla y no se mueve, que es
    //  la invariante que costo una sesion huerfana; esto es una preferencia
    //  POR ENCIMA de ella, que se cae al defecto sin tocar el ancla. Un
    //  proyecto guardado en la carpeta de antes sigue donde estaba: lo que
    //  cambia es donde se busca a partir de ahora, que es lo que se pidio.
    static juce::File root() { return carpeta (Carpeta::proyectos); }

    static juce::File folderFor (const juce::String& name)
    {
        return root().getChildFile (sanitise (name));
    }

private:
    static juce::File sub (const char* name)
    {
        auto dir = home().getChildFile (name);
        dir.createDirectory();
        return dir;
    }

public:

    // Names become folder names, so strip anything a filesystem dislikes and
    // keep it short enough to stay readable in the list.
    static juce::String sanitise (const juce::String& name)
    {
        //  TWO DIFFERENT NAMES MUST NEVER BECOME ONE FOLDER.
        //
        //  The whitelist is ASCII, and the app ships in Chinese and Arabic.
        //  Every name written in either of them - and every Spanish name with
        //  an accent in it - was stripped to nothing and collapsed onto the
        //  same literal folder. Save two of them and the second one silently
        //  destroyed the first, and it skipped the overwrite warning too,
        //  because by then currentProject already WAS that literal.
        //
        //  Non-Latin letters stay: a filesystem takes them and a person needs
        //  them. Only what a path cannot survive is removed. And when nothing
        //  legible is left, the fallback carries a digest of the original, so
        //  two different names still land in two different folders.
        juce::String s;
        for (auto c : name.trim())
        {
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"'
                || c == '<' || c == '>' || c == '|' || c == 0 || c < 32 || c == '.')
                continue;
            s << (juce::juce_wchar) c;
        }

        s = s.trim().substring (0, 40).trim();

        if (s.isNotEmpty())
            return s;

        const auto digest = juce::String::toHexString (name.trim().hashCode()).toUpperCase();
        return name.trim().isEmpty() ? juce::String ("SIN NOMBRE")
                                     : "SIN NOMBRE " + digest.getLastCharacters (6);
    }

    //  UN NOMBRE NO PUEDE SALIRSE DE SU CARPETA.
    //
    //  Habia TRES reglas para lo mismo y solo una quitaba los puntos:
    //  `sanitise` (carpetas de proyecto) si, `sanitiseFileName` no —guarda el
    //  punto a proposito, que una extension es como el cargador sabe que mira—
    //  y `juce::File::createLegalFileName` tampoco, que quita `\` y `/` y deja
    //  los puntos intactos.
    //
    //  Y `juce::File::getChildFile` resuelve `..` subiendo un nivel: esta
    //  escrito en juce_File.cpp y es su comportamiento documentado. Asi que un
    //  kit llamado `..` escribia sus dieciseis WAV en la RAIZ de la biblioteca,
    //  al lado de Samples, Presets e Instrumentos, y `../../..` se salia de
    //  ZATI entera. El nombre lo escribe la persona en su propio aparato, asi
    //  que el dano es acotado — pero el de `importIntoLibrary` sale del nombre
    //  que trae un documento del selector de Android, que es la unica cadena de
    //  esta app que viene de fuera.
    //
    //  Un componente de ruta es un nombre y nunca una direccion: ni separadores
    //  ni un nombre hecho solo de puntos. Y se comprueba ademas la
    //  POSTCONDICION donde se usa —`isAChildOf`—, que es la misma leccion que
    //  `ensureDirectory`: lo que importa no es lo que devuelve la funcion sino
    //  donde acabo el fichero.
    static juce::String componente (const juce::String& name)
    {
        auto s = name.removeCharacters ("/\\").trim();
        //  `.`, `..`, `...`: nada que no sean puntos no es un nombre, es una
        //  direccion relativa.
        if (s.isNotEmpty() && s.containsOnly (".")) return {};
        return s;
    }

    //  A file name we can put on disk. Unlike sanitise() for project folders
    //  this keeps the dot, because an extension is how the loader knows what
    //  it is looking at.
    static juce::String sanitiseFileName (const juce::String& name)
    {
        auto s = name.trim().retainCharacters (
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_.");
        s = s.trim();
        s = s.length() > 80 ? s.substring (s.length() - 80) : s;
        //  Ver componente: la lista blanca deja pasar el punto —hace falta para
        //  la extension— y con el pasaba `..`, que getChildFile resuelve
        //  subiendo un nivel.
        return componente (s);
    }

    static juce::StringArray list()
    {
        juce::StringArray names;
        for (const auto& e : juce::RangedDirectoryIterator (root(), false, "*",
                                                            juce::File::findDirectories))
            if (e.getFile().getChildFile ("project.xml").existsAsFile())
                names.add (e.getFile().getFileName());

        names.sort (true);
        return names;
    }

    //  CREAR UNA CARPETA NO ES SEGURO ENTRE HILOS, Y AQUI HAY DOS.
    //
    //  juce::File::createDirectory crea el arbol componente a componente con
    //  mkdir(), y trata CUALQUIER error como fallo - incluido EEXIST, que es
    //  lo que devuelve un mkdir cuando otro hilo acaba de crear ese mismo
    //  componente. El que pierde la carrera recibe "fail" y se va SIN crear el
    //  hijo que iba a crear.
    //
    //  Aqui pasa siempre en el mismo sitio: el hilo de mensajes crea .sesion
    //  para escribir state.xml en el mismo instante en que el hilo de sesion
    //  crea .sesion/samples para el primer pad. Medido con una sonda, 4 de
    //  cada 5 arranques: mkdir=0, la carpeta no existe, el stream se abre con
    //  ENOENT y el WAV del PAD 1 no se escribe nunca. Siempre el primero,
    //  porque es el unico que compite con la creacion del arbol - y el fallo
    //  era mudo, porque writeSample devolvia false y nadie mira ese false.
    //
    //  Lo que importa no es quien la cree, sino que este. Se comprueba la
    //  POSTCONDICION en vez del valor de retorno, y se reintenta: eso es
    //  correcto gane quien gane la carrera.
    static bool ensureDirectory (const juce::File& dir)
    {
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            if (dir.isDirectory()) return true;
            dir.createDirectory();
            if (dir.isDirectory()) return true;
            juce::Thread::sleep (2);
        }
        return dir.isDirectory();
    }

    //  EL RELEVO QUE NO DEJA HUECO. JUCE `moveFileTo` BORRA el destino y
    //  despues renombra (juce_File.cpp:300-315), asi que entre las dos
    //  llamadas no existe ni el fichero viejo ni el nuevo: si Android mata el
    //  proceso ahi, state.xml no esta, el arranque siguiente carga la fabrica
    //  y el escritor PISA los 64 WAV de la sesion. `replaceFileIn` con destino
    //  existente va a rename(2)... y si rename falla, JUCE copia a un
    //  temporal y vuelve a entrar por el camino que BORRA el destino antes
    //  (juce_SharedCode_posix.h:409-424): medido en Tests/guardado.py con un
    //  directorio en el sitio de state.xml, lo borro y escribio encima
    //  diciendo que si. Asi que rename(2) directo y nada mas: sustituye sin
    //  hueco, y si no puede, dice que no y deja el bueno donde estaba. Los tres
    //  sitios que escriben al lado y mueven pasan por aqui (Tribunal 2026-09,
    //  7.1 y 7.5).
    static bool ponEncima (const juce::File& tmp, const juce::File& dest)
    {
       #if JUCE_WINDOWS
        return tmp.replaceFileIn (dest);     // alli rename no sustituye
       #else
        return std::rename (tmp.getFullPathName().toRawUTF8(),
                            dest.getFullPathName().toRawUTF8()) == 0;
       #endif
    }

    //  ESCRIBIR UN FICHERO DE TEXTO SIN PERDER EL QUE HABIA.
    //
    //  `replaceWithText` esconde dos fallos a la vez -se come el resultado del
    //  append, y el que llama se come el suyo- asi que una escritura que se
    //  queda sin espacio a mitad renombra un fichero TRUNCADO encima de uno
    //  bueno y devuelve true. Eso ya estaba aprendido y escrito en
    //  SessionKeeper::writeState... y a ocho lineas de ahi, `autosave` escribia
    //  el project.xml con un replaceWithText a pelo. Y es peor: la sesion se
    //  rehace sola, un proyecto guardado no, y autosave corre en cada onPause,
    //  o sea justo antes de que Android mate el proceso.
    //
    //  Se escribe al lado, se vuelve a leer, y solo entonces se mueve encima:
    //  rename(2) sobreescribe y es atomico, asi que no existe el instante en el
    //  que el bueno ya no esta y el nuevo todavia no. POR `ponEncima` y no por
    //  `moveFileTo`, que borraba antes y abria justo ese instante.
    //
    //  `valida` es la unica comprobacion que significa algo, y por eso la pone
    //  quien llama: para un XML es parseXML, y para la lista de licencias es
    //  que vuelvan las mismas lineas. Sin validador solo se comprueba que la
    //  escritura no fallara, que es lo minimo y sigue siendo mas de lo que
    //  habia.
    static bool escribeTexto (const juce::File& dest, const juce::String& texto,
                              std::function<bool (const juce::File&)> valida = {})
    {
        if (! ensureDirectory (dest.getParentDirectory())) return false;

        const auto tmp = dest.getSiblingFile (dest.getFileName() + ".escribiendo");
        tmp.deleteFile();

        if (! tmp.replaceWithText (texto))          { tmp.deleteFile(); return false; }
        if (valida != nullptr && ! valida (tmp))    { tmp.deleteFile(); return false; }
        if (ponEncima (tmp, dest))                  return true;

        tmp.deleteFile();
        return false;
    }

    //  El validador de los dos que escriben un arbol: un XML que no se puede
    //  volver a leer no es un proyecto, es un fichero.
    static bool esXmlLegible (const juce::File& f)
    {
        return juce::parseXML (f) != nullptr;
    }

    // Write `buffer` as a 24-bit WAV next to the project. Returns false if the
    // writer could not be created (out of space, bad path).
    //
    //  NUNCA SE BORRA EL BUENO ANTES DE TENER EL NUEVO.
    //
    //  Empezaba por dest.deleteFile(), o sea que si luego fallaba cualquier
    //  cosa - sin espacio, carpeta de solo lectura, formato rechazado - la
    //  copia anterior ya no existia y la nueva tampoco. SessionKeeper::run ya
    //  habia resuelto esto por fuera, escribiendo en .tmp y moviendo, pero
    //  saveProject llamaba aqui DIRECTAMENTE sobre el destino: guardar encima
    //  de un proyecto con el disco lleno le borraba los 64 WAV y no escribia
    //  ninguno, y lo unico que se veia era "64 pads no se escribieron" sobre
    //  un proyecto que acababa de quedarse mudo.
    //
    //  La red va DENTRO, para que ninguna ruta pueda saltarsela. rename(2) es
    //  atomico y sobreescribe, asi que no hay ni un instante sin fichero -
    //  siempre que se llame a rename(2) y no a `moveFileTo`. Ver ponEncima.
    static bool writeSample (const juce::File& dest, const juce::AudioBuffer<float>& buffer,
                             double sampleRate)
    {
        if (! ensureDirectory (dest.getParentDirectory())) return false;

        const auto tmp = dest.getSiblingFile (dest.getFileName() + ".escribiendo");
        tmp.deleteFile();

        if (! writeSampleTo (tmp, buffer, sampleRate)) { tmp.deleteFile(); return false; }
        if (ponEncima (tmp, dest))                     return true;

        tmp.deleteFile();
        return false;
    }

private:
    static bool writeSampleTo (const juce::File& dest, const juce::AudioBuffer<float>& buffer,
                               double sampleRate)
    {
        std::unique_ptr<juce::FileOutputStream> out (dest.createOutputStream());
        if (out == nullptr || ! out->openedOk())
            return false;

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (out.get(), sampleRate > 0.0 ? sampleRate : 44100.0,
                                 (unsigned int) juce::jmax (1, buffer.getNumChannels()),
                                 24, {}, 0));
        if (writer == nullptr)
            return false;

        out.release();                       // the writer owns the stream now
        const bool wrote = writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());

        //  THE ANSWER IS NOT KNOWN UNTIL THE WRITER IS GONE.
        //
        //  writeFromAudioSampleBuffer can return true on a buffered stream that
        //  has not touched the disk yet: the final flush and the WAV header
        //  rewrite happen in ~AudioFormatWriter, after that value is fixed. Out
        //  of space, that meant this function reported success, SessionKeeper
        //  took it as permission to delete the previous take and move a
        //  headerless stub over it, and saveProject counted it as saved.
        //
        //  Destroy the writer first, then ask the file whether anything real
        //  is there - a WAV of a buffer this size cannot be smaller than its
        //  own header plus a frame.
        writer.reset();

        if (! wrote || ! dest.existsAsFile() || dest.getSize() < 64)
        {
            dest.deleteFile();
            return false;
        }

        return true;
    }

public:
    // Read a WAV back into a SampleBuffer. Returns nullptr when the file is
    // missing or undecodable, so a damaged project loads with that pad empty
    // rather than refusing to open at all.
    static SampleBuffer::Ptr readSample (const juce::File& src)
    {
        if (! src.existsAsFile())
            return nullptr;

        juce::AudioFormatManager fm;
        fm.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (src));
        if (reader == nullptr || reader->numChannels == 0 || reader->lengthInSamples < 4)
            return nullptr;

        //  BELIEVE THE FILE, NOT ITS HEADER.
        //
        //  lengthInSamples comes from the DECLARED size of the data chunk;
        //  JUCE does not clamp it to how many bytes are actually there. A WAV
        //  truncated by a process that was killed mid-write, or copied off
        //  another phone half-finished, therefore asks for an allocation of
        //  whatever number happens to sit in those four bytes - gigabytes, or
        //  a negative int after the cast. setSize throws, nothing catches it,
        //  and the process aborts.
        //
        //  Which would be survivable anywhere except here: restoreSession runs
        //  this over all sixteen pads on the first timer tick of EVERY launch,
        //  so one bad byte is a boot loop with no way out from inside the app.
        //
        //  So the declared length is checked against the bytes on disk before
        //  a single one is allocated, and the allocation is caught anyway.
        const auto declared = reader->lengthInSamples;
        const auto frameBytes = (juce::int64) reader->numChannels
                              * (juce::int64) juce::jmax (1u, reader->bitsPerSample / 8);
        const auto possible = juce::jmax ((juce::int64) 0, src.getSize() / juce::jmax ((juce::int64) 1, frameBytes));

        if (declared <= 0 || declared > possible || declared > 0x3fffffff)
            return nullptr;

        const int n = (int) declared;

        try
        {
            SampleBuffer::Ptr sb = new SampleBuffer();
            sb->buffer.setSize ((int) reader->numChannels, n);
            reader->read (&sb->buffer, 0, n, 0, true, true);
            sb->sourceSampleRate = reader->sampleRate;
            return sb;
        }
        catch (const std::bad_alloc&)
        {
            return nullptr;
        }
    }

    static juce::File sampleFile (const juce::File& projectFolder, int pad)
    {
        return projectFolder.getChildFile ("samples")
                            .getChildFile ("pad" + juce::String (pad + 1).paddedLeft ('0', 2) + ".wav");
    }
};
