#pragma once

#include <JuceHeader.h>
#include "SampleBuffer.h"

// ============================================================================
//  ProjectStore — where a COLORS project lives on disk and how it is named.
//
//  A project is a FOLDER, not a single file:
//
//      <app data>/COLORS/Projects/<name>/
//          project.xml        the whole machine state
//          samples/pad01.wav  a copy of every loaded pad
//
//  Copying the audio in is deliberate. Storing paths would be smaller, but a
//  pad recorded with REC has no file behind it at all, and any sample the user
//  later moves or deletes would silently empty a pad. A self-contained folder
//  survives both, and can be copied to another device as-is.
//
//  The app-data location is used rather than shared Music because it is
//  writable on every Android version without a storage permission — projects
//  must never fail to save because of a permission prompt.
// ============================================================================
class ProjectStore
{
public:
    static juce::File root()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("COLORS")
                       .getChildFile ("Projects");
        dir.createDirectory();
        return dir;
    }

    static juce::File folderFor (const juce::String& name)
    {
        return root().getChildFile (sanitise (name));
    }

    // Names become folder names, so strip anything a filesystem dislikes and
    // keep it short enough to stay readable in the list.
    static juce::String sanitise (const juce::String& name)
    {
        auto s = name.trim().retainCharacters (
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_");
        s = s.trim().substring (0, 40);
        return s.isEmpty() ? "SIN NOMBRE" : s;
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
