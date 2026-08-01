#include "SampleLoader.h"
#include "AudioEngine.h"

SampleLoader::SampleLoader (AudioEngine& engineToLoadInto)
    : engine (engineToLoadInto)
{
    formatManager.registerBasicFormats();          // WAV, AIFF, FLAC, Ogg
   #if JUCE_USE_MP3AUDIOFORMAT
    formatManager.registerFormat (new juce::MP3AudioFormat(), false);   // + MP3
   #endif
}

void SampleLoader::loadAsync (const juce::URL& url, int slot,
                              std::function<void (bool, juce::String, SampleBuffer::Ptr)> onFinished)
{
    pool.addJob ([this, url, slot, callback = std::move (onFinished)]
    {
        bool              success = false;
        juce::String      detail;
        SampleBuffer::Ptr loaded;

        // 1. Open a stream. On Android the picker returns a content:// URL that
        //    must be opened through AndroidDocument (URL::createInputStream is
        //    for http and returns null for content URIs).
        std::unique_ptr<juce::InputStream> source;

       #if JUCE_ANDROID
        auto doc = juce::AndroidDocument::fromDocument (url);
        if (doc.hasValue())
            source = doc.createInputStream();
        else
            detail = "cannot open document";
       #else
        if (url.isLocalFile())
            source = std::make_unique<juce::FileInputStream> (url.getLocalFile());
        else
            source = url.createInputStream (
                juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress));
       #endif

        if (source == nullptr)
        {
            if (detail.isEmpty()) detail = "no stream";
        }
        else
        {
            // 2. Slurp into memory -> a seekable stream the WAV/format parser can
            //    rewind (Android content streams are typically non-seekable).
            juce::MemoryBlock mb;
            source->readIntoMemoryBlock (mb);

            if (mb.getSize() <= 44)
            {
                detail = "read " + juce::String ((int) mb.getSize()) + " bytes";
            }
            else if (auto* rawReader = formatManager.createReaderFor (
                         std::make_unique<juce::MemoryInputStream> (mb.getData(), mb.getSize(), true)))
            {
                std::unique_ptr<juce::AudioFormatReader> reader (rawReader);
                const int numChannels = (int) reader->numChannels;
                const int numSamples  = (int) reader->lengthInSamples;

                if (numChannels > 0 && numSamples > 3)
                {
                    SampleBuffer::Ptr sb = new SampleBuffer();
                    sb->buffer.setSize (numChannels, numSamples);
                    reader->read (&sb->buffer, 0, numSamples, 0, true, true);
                    sb->sourceSampleRate = reader->sampleRate;

                    engine.publishSample (slot, sb);
                    loaded  = sb;
                    success = true;
                    detail  = juce::String (numChannels) + "ch "
                            + juce::String ((int) reader->sampleRate) + "Hz";
                }
                else
                {
                    detail = "empty audio";
                }
            }
            else
            {
                detail = "unknown format (" + juce::String ((int) mb.getSize()) + "B)";
            }
        }

        juce::MessageManager::callAsync ([callback, success, detail, loaded]
        {
            if (callback)
                callback (success, detail, loaded);
        });
    });
}
