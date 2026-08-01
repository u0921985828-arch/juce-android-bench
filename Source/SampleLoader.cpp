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

void SampleLoader::loadAsync (const juce::URL& url, int slot, std::function<void (bool)> onFinished)
{
    pool.addJob ([this, url, slot, callback = std::move (onFinished)]
    {
        bool success = false;

        // Android's file picker hands back a content:// URI (Storage Access
        // Framework), NOT a real file path — so read via URL/stream, not File.
        juce::AudioFormatReader* rawReader = nullptr;

        if (url.isLocalFile())
        {
            rawReader = formatManager.createReaderFor (url.getLocalFile());
        }
        else if (auto stream = url.createInputStream (
                     juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)))
        {
            rawReader = formatManager.createReaderFor (std::move (stream));
        }

        if (rawReader != nullptr)
        {
            std::unique_ptr<juce::AudioFormatReader> reader (rawReader);

            const int numChannels = (int) reader->numChannels;
            const int numSamples  = (int) reader->lengthInSamples;

            if (numChannels > 0 && numSamples > 3)   // need >=4 samples for Hermite
            {
                SampleBuffer::Ptr sb = new SampleBuffer();
                sb->buffer.setSize (numChannels, numSamples);
                reader->read (&sb->buffer, 0, numSamples, 0, true, true);
                sb->sourceSampleRate = reader->sampleRate;   // F_src

                engine.publishSample (slot, sb);   // thread-safe atomic hand-off
                success = true;
            }
        }

        juce::MessageManager::callAsync ([callback, success]
        {
            if (callback)
                callback (success);
        });
    });
}
