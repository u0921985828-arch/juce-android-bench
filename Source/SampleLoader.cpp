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

        // 1. Open a stream. Two kinds of URL reach here and they open
        //    differently on Android:
        //      * a real path (file://) from the in-app browser, and
        //      * a content:// document URI from the system picker, which only
        //        AndroidDocument can open.
        //    Branching on the URL kind rather than the platform is what
        //    matters: keying it off JUCE_ANDROID alone sent the browser's
        //    file:// URLs into AndroidDocument, which always rejects them.
        std::unique_ptr<juce::InputStream> source;

        if (url.isLocalFile())
        {
            const auto f = url.getLocalFile();
            auto fis = std::make_unique<juce::FileInputStream> (f);

            if (fis->openedOk())        source = std::move (fis);
            else if (! f.existsAsFile()) detail = "no existe el archivo";
            else                         detail = "sin permiso de lectura";
        }
        else
        {
           #if JUCE_ANDROID
            auto doc = juce::AndroidDocument::fromDocument (url);
            if (doc.hasValue())
                source = doc.createInputStream();
            else
                detail = "documento no accesible";
           #else
            source = url.createInputStream (
                juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress));
           #endif
        }

        if (source == nullptr)
        {
            if (detail.isEmpty()) detail = "sin flujo de datos";
        }
        else
        {
            // 2. Slurp into memory -> a seekable stream the WAV/format parser can
            //    rewind (Android content streams are typically non-seekable).
            juce::MemoryBlock mb;
            source->readIntoMemoryBlock (mb);

            if (mb.getSize() <= 44)
            {
                detail = "solo " + juce::String ((int) mb.getSize()) + " bytes";
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
                    detail = "audio vacio";
                }
            }
            else
            {
                detail = "formato no reconocido (" + juce::String ((int) mb.getSize()) + "B)";
            }
        }

        juce::MessageManager::callAsync ([callback, success, detail, loaded]
        {
            if (callback)
                callback (success, detail, loaded);
        });
    });
}
