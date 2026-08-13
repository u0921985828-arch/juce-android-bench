#pragma once

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

    static juce::File samples()    { return sub ("Samples"); }
    static juce::File presets()    { return sub ("Presets"); }
    static juce::File recordings() { return sub ("Recordings"); }
    static juce::File exports()    { return sub ("Exports"); }

    // Creates the whole tree. Safe to call every launch.
    static void ensureTree()
    {
        for (auto* n : { "Samples", "Projects", "Presets", "Recordings", "Exports" })
            home().getChildFile (n).createDirectory();
    }

    static juce::File root() { return sub ("Projects"); }

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

    //  A file name we can put on disk. Unlike sanitise() for project folders
    //  this keeps the dot, because an extension is how the loader knows what
    //  it is looking at.
    static juce::String sanitiseFileName (const juce::String& name)
    {
        auto s = name.trim().retainCharacters (
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_.");
        s = s.trim();
        return s.length() > 80 ? s.substring (s.length() - 80) : s;
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
    //  atomico y sobreescribe, asi que no hay ni un instante sin fichero.
    static bool writeSample (const juce::File& dest, const juce::AudioBuffer<float>& buffer,
                             double sampleRate)
    {
        if (! ensureDirectory (dest.getParentDirectory())) return false;

        const auto tmp = dest.getSiblingFile (dest.getFileName() + ".escribiendo");
        tmp.deleteFile();

        if (! writeSampleTo (tmp, buffer, sampleRate)) { tmp.deleteFile(); return false; }
        if (tmp.moveFileTo (dest))                     return true;

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
