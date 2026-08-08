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

    static juce::File home()
    {
        static juce::File cached = []
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
            //  internal app data - it turns up over a USB cable, which is how
            //  a sample pack actually gets onto a phone. A library nothing can
            //  reach is not a library.
            if (auto ext = AppStorage::externalFilesDir(); ext != juce::File())
            {
                auto candidate = ext.getChildFile ("ZATI");
                if (canReallyWriteInto (candidate))
                    return candidate;
            }

            //  Last resort. Always writable, visible to nothing - but a save
            //  that lands somewhere private beats a save that does not land.
            auto fallback = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                .getChildFile ("ZATI");
            fallback.createDirectory();
            return fallback;
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
        auto s = name.trim().retainCharacters (
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_");
        s = s.trim().substring (0, 40);
        return s.isEmpty() ? "SIN NOMBRE" : s;
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

    // Write `buffer` as a 24-bit WAV next to the project. Returns false if the
    // writer could not be created (out of space, bad path).
    static bool writeSample (const juce::File& dest, const juce::AudioBuffer<float>& buffer,
                             double sampleRate)
    {
        dest.getParentDirectory().createDirectory();
        dest.deleteFile();

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
        return writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    }

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

        SampleBuffer::Ptr sb = new SampleBuffer();
        sb->buffer.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&sb->buffer, 0, (int) reader->lengthInSamples, 0, true, true);
        sb->sourceSampleRate = reader->sampleRate;
        return sb;
    }

    static juce::File sampleFile (const juce::File& projectFolder, int pad)
    {
        return projectFolder.getChildFile ("samples")
                            .getChildFile ("pad" + juce::String (pad + 1).paddedLeft ('0', 2) + ".wav");
    }
};
