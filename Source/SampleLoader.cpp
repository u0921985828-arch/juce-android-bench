#include "SampleLoader.h"
#include "AudioEngine.h"

SampleLoader::SampleLoader (AudioEngine& engineToLoadInto)
    : engine (engineToLoadInto)
{
    formatManager.registerBasicFormats();   // WAV, AIFF, FLAC, Ogg, MP3 (per build flags)
}

void SampleLoader::loadAsync (const juce::File& file, std::function<void (bool)> onFinished)
{
    pool.addJob ([this, file, callback = std::move (onFinished)]
    {
        bool success = false;

        if (auto* rawReader = formatManager.createReaderFor (file))
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

                engine.publishSample (sb);   // thread-safe atomic hand-off
                success = true;
            }
        }

        // Notify the UI on the message thread.
        juce::MessageManager::callAsync ([callback, success]
        {
            if (callback)
                callback (success);
        });
    });
}
