#include "SampleLoader.h"
#include "DeviceTier.h"
#include "AudioEngine.h"

namespace
{
    // ========================================================================
    //  Loading used to be able to kill the process, twice over.
    //
    //  The file went into a MemoryBlock whole, and then the decoded audio was
    //  allocated whole again as float - so a five minute 24-bit stereo WAV
    //  cost ~85 MB of file plus ~230 MB of buffer, over 300 MB of peak for one
    //  pad. Nothing checked a size, and AudioBuffer::setSize throws
    //  std::bad_alloc, which nobody caught: on Android that is not an
    //  exception, it is the process disappearing with no dialog.
    //
    //  Two changes. A local file is already seekable, so it is handed to the
    //  decoder directly and never slurped - that alone halves the peak for
    //  everything the in-app browser opens. And every allocation is now bounded
    //  and wrapped, so an oversized file is a message on screen instead of a
    //  crash.
    //
    //  The three limits are the same ceiling seen from different sides: what we
    //  will read, how long we will decode, and what the result may cost in RAM.
    // ========================================================================
    //  The ceilings scale with the phone: a device with 3 GB has no business
    //  decoding a quarter of a gigabyte of float, and one with 16 should not
    //  be told a forty-megabyte break is "too big". DeviceTier decides the
    //  budget once at startup and everything here is derived from it.
    inline juce::int64 budgetBytes()
    {
        return (juce::int64) DeviceTier::profile().sampleBudgetMB * 1024 * 1024;
    }

    inline juce::int64 maxFileBytes()  { return budgetBytes(); }
    inline juce::int64 maxFloatBytes() { return budgetBytes() * 4 / 3; }
    inline double      maxSeconds()    { return (double) DeviceTier::profile().sampleBudgetMB * 3.0; }

    //  Rounded through an int, not juce::String (x, 0) - zero decimal places
    //  makes JUCE skip the fixed format and print the lot.
    juce::String asMB (juce::int64 bytes)
    {
        return juce::String ((int) ((bytes + 524288) / (1024 * 1024))) + " MB";
    }
}

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
            // 2. Refuse the file before touching it, whenever its size is
            //    knowable. This is the cheapest of the three guards and the
            //    only one that costs nothing when it passes.
            const juce::int64 declared = source->getTotalLength();

            if (declared > maxFileBytes())
            {
                detail = "demasiado grande: " + asMB (declared)
                           + " (tope " + asMB (maxFileBytes()) + ")";
            }
            else
            {
                // 3. Get a seekable stream to the decoder. A file already is
                //    one, so it goes straight in and never doubles its own
                //    size in RAM; a content:// stream usually is not, so it
                //    still has to be slurped - but now with a ceiling.
                std::unique_ptr<juce::InputStream> seekable;
                juce::int64 sourceBytes = declared;

                if (url.isLocalFile())
                {
                    seekable = std::move (source);
                }
                else
                {
                    try
                    {
                        juce::MemoryBlock block;
                        source->readIntoMemoryBlock (block, (ssize_t) maxFileBytes());
                        sourceBytes = (juce::int64) block.getSize();

                        if (sourceBytes >= maxFileBytes())
                            detail = "demasiado grande: pasa de " + asMB (maxFileBytes());
                        else
                            seekable = std::make_unique<juce::MemoryInputStream> (std::move (block));
                    }
                    catch (const std::bad_alloc&)
                    {
                        detail = "sin memoria al leer el archivo";
                    }
                }

                if (seekable != nullptr)
                {
                    if (sourceBytes >= 0 && sourceBytes <= 44)
                    {
                        detail = "solo " + juce::String ((int) sourceBytes) + " bytes";
                    }
                    else if (auto* rawReader = formatManager.createReaderFor (std::move (seekable)))
                    {
                        std::unique_ptr<juce::AudioFormatReader> reader (rawReader);
                        const int    numChannels = (int) reader->numChannels;
                        const auto   lengthIn    = reader->lengthInSamples;
                        const double rate        = reader->sampleRate > 0.0 ? reader->sampleRate : 48000.0;
                        const double seconds     = (double) lengthIn / rate;

                        //  What the decoded audio will actually cost. int64
                        //  throughout: a length that overflows int is exactly
                        //  the case this guard exists for.
                        const juce::int64 floatBytes = lengthIn * (juce::int64) juce::jmax (1, numChannels)
                                                                * (juce::int64) sizeof (float);

                        if (numChannels <= 0 || lengthIn <= 3)
                            detail = "audio vacio";
                        else if (seconds > maxSeconds())
                            detail = "dura " + juce::String ((int) (seconds / 60.0)) + " min (tope "
                                       + juce::String ((int) (maxSeconds() / 60.0)) + ")";
                        else if (floatBytes > maxFloatBytes())
                            detail = "ocuparia " + asMB (floatBytes)
                                       + " en memoria (tope " + asMB (maxFloatBytes()) + ")";
                        else
                        {
                            //  The allocation that used to take the process
                            //  down with it. Bounded above, and caught here.
                            try
                            {
                                const int numSamples = (int) lengthIn;
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
                            catch (const std::bad_alloc&)
                            {
                                detail = "sin memoria para " + asMB (floatBytes);
                            }
                        }
                    }
                    else
                    {
                        detail = "formato no reconocido";
                    }
                }
                else if (detail.isEmpty())
                {
                    detail = "no pude abrir el archivo";
                }
            }
        }

        juce::MessageManager::callAsync ([callback, success, detail, loaded]
        {
            if (callback)
                callback (success, detail, loaded);
        });
    });
}
