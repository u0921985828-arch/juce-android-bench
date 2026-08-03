#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "SampleBuffer.h"

// ============================================================================
//  Exporter — the bounce. Where the music finally leaves ZATI.
//
//  Everything up to here happens inside the app: you chop, you sequence, you
//  arrange. None of it is a file anyone else can hear. This turns the song (or
//  the chain, or the single pattern you are on) into a WAV in ZATI/Exports.
//
//  How it renders, and why:
//
//  · Through a SECOND engine, not the live one. A clone gets the whole machine
//    copied into it — every pad parameter, every pattern, every note, the
//    chain, the song, the FX — and the same ref-counted sample buffers, so the
//    bounce costs no extra audio memory. The live engine keeps playing
//    untouched, and the render is not tied to real time: three minutes of
//    music writes in a couple of seconds.
//
//  · On a background thread, in blocks, into one buffer. The audio callback is
//    never involved, so none of the RT rules apply here — this is the one
//    place in the codebase that may allocate freely.
//
//  Two products:
//    MASTER  one stereo file, exactly what you hear.
//    PISTAS  the master plus one file per pad that has a sample — the same
//            render with everything else muted, so the stems sum back to the
//            master. That is what a mixing engineer asks for.
//
//  Clipping is handled by measuring, not guessing: if the master peaks over
//  0 dBFS the whole bounce is scaled down by exactly that much and the amount
//  is reported. Stems get the SAME scaling, or they would no longer sum.
// ============================================================================
class Exporter : public juce::Thread
{
public:
    static constexpr int kNumPads = AudioEngine::kNumPads;

    using PadSamples = std::array<SampleBuffer::Ptr, kNumPads>;
    using PadNames   = std::array<juce::String, kNumPads>;

    Exporter (AudioEngine& liveEngine,
              PadSamples samples,
              PadNames names,
              juce::File destDir,
              juce::String baseName,
              bool wantStems,
              double sr)
        : juce::Thread ("zati-export"),
          live (liveEngine), pads (std::move (samples)), padNames (std::move (names)),
          dir (std::move (destDir)), base (std::move (baseName)),
          stems (wantStems), sampleRate (sr > 0.0 ? sr : 44100.0)
    {
        totalPasses = 1;
        if (stems)
            for (int i = 0; i < kNumPads; ++i)
                if (pads[(size_t) i] != nullptr) ++totalPasses;
    }

    ~Exporter() override { stopThread (4000); }

    // --- Read by the UI timer while the job runs -------------------------
    std::atomic<float> progress { 0.0f };     // 0..1 over all passes
    std::atomic<int>   passDone { 0 };
    std::atomic<int>   passTotal { 1 };
    std::atomic<bool>  finished { false };

    // Written before `finished` is released, so an acquire-read is safe.
    juce::String resultText;
    bool         resultOk = false;
    juce::File   resultFolder;

    void run() override
    {
        passTotal.store (totalPasses, std::memory_order_relaxed);

        const int steps = live.lengthInSteps();
        if (steps <= 0 || ! live.hasContentToRender())
        {
            resultOk = false;
            resultText = "nada que exportar: no hay pasos con sonido";
            finished.store (true, std::memory_order_release);
            return;
        }

        // 16th notes, plus a tail so the last hit, its release and the delay
        // repeats all fit inside the file instead of being cut off.
        const double secPerStep = (60.0 / juce::jmax (20.0, live.getBpm())) * 0.25;
        const juce::int64 bodyLen = (juce::int64) (secPerStep * (double) steps * sampleRate);
        const juce::int64 tailLen = (juce::int64) (juce::jmax (2.0, 4.0 * live.getDelayTimeSeconds()) * sampleRate);
        const juce::int64 totalLen = bodyLen + tailLen;

        if (! dir.createDirectory())
        {
            resultOk = false;
            resultText = "no se pudo crear " + dir.getFullPathName();
            finished.store (true, std::memory_order_release);
            return;
        }

        juce::AudioBuffer<float> buffer;
        try
        {
            buffer.setSize (2, (int) totalLen);
        }
        catch (...)
        {
            resultOk = false;
            resultText = "sin memoria para " + juce::String (totalLen / (juce::int64) sampleRate) + " s";
            finished.store (true, std::memory_order_release);
            return;
        }

        // --- Pass 1: the master. Its peak sets the gain for everything. ---
        if (! renderPass (-1, buffer)) return;   // cancelled

        const float peak = bufferPeak (buffer);
        const float gain = (peak > 1.0f) ? (0.999f / peak) : 1.0f;

        auto masterFile = uniqueFile (base + ".wav");
        if (! writeWav (masterFile, buffer, gain))
        {
            resultOk = false;
            resultText = "no se pudo escribir " + masterFile.getFileName();
            finished.store (true, std::memory_order_release);
            return;
        }
        passDone.store (1, std::memory_order_relaxed);
        progress.store (1.0f / (float) totalPasses, std::memory_order_relaxed);

        int written = 1;

        // --- Remaining passes: one stem per loaded pad. -------------------
        if (stems)
        {
            for (int p = 0; p < kNumPads && ! threadShouldExit(); ++p)
            {
                if (pads[(size_t) p] == nullptr) continue;

                if (! renderPass (p, buffer)) return;   // cancelled

                auto label = padNames[(size_t) p].isNotEmpty()
                               ? sanitise (padNames[(size_t) p])
                               : juce::String ("pad");
                auto f = uniqueFile (base + "_" + juce::String (p + 1).paddedLeft ('0', 2)
                                          + "_" + label + ".wav");
                if (writeWav (f, buffer, gain))
                    ++written;

                passDone.store (written, std::memory_order_relaxed);
                progress.store ((float) written / (float) totalPasses, std::memory_order_relaxed);
            }
        }

        if (threadShouldExit())
        {
            resultOk = false;
            resultText = "cancelado";
            finished.store (true, std::memory_order_release);
            return;
        }

        const double secs = (double) totalLen / sampleRate;
        resultOk     = true;
        resultFolder = dir;
        resultText   = juce::String (written) + (written == 1 ? " archivo, " : " archivos, ")
                     + juce::String (secs, 1) + " s"
                     + (gain < 1.0f
                          ? " (bajado " + juce::String (-juce::Decibels::gainToDecibels (gain), 1) + " dB para no saturar)"
                          : juce::String());
        progress.store (1.0f, std::memory_order_relaxed);
        finished.store (true, std::memory_order_release);
    }

private:
    // Renders the whole arrangement once. `soloPad` < 0 means the full mix;
    // otherwise only that pad sounds. Returns false if the job was cancelled.
    bool renderPass (int soloPad, juce::AudioBuffer<float>& dest)
    {
        // On the heap: an engine carries the eight pattern banks and their
        // note grids, which is more than a worker thread's stack should hold.
        auto engine = std::make_unique<AudioEngine>();
        auto& off = *engine;

        off.setOffline (true);
        off.prepareToPlay (sampleRate, kBlock);
        off.copyStateFrom (live);

        for (int i = 0; i < kNumPads; ++i)
            if (pads[(size_t) i] != nullptr)
                off.publishSample (i, pads[(size_t) i]);

        if (soloPad >= 0)
        {
            // A stem must contain its pad even if that pad is muted in the
            // live mix — you export stems precisely to rebalance later.
            off.clearSolo();
            for (int i = 0; i < kNumPads; ++i)
                off.setPadMute (i, i != soloPad);
        }

        off.setPlaying (true);

        const int total = dest.getNumSamples();
        for (int pos = 0; pos < total; pos += kBlock)
        {
            if (threadShouldExit())
            {
                resultOk = false;
                resultText = "cancelado";
                finished.store (true, std::memory_order_release);
                return false;
            }

            const int n = juce::jmin (kBlock, total - pos);
            off.renderNextBlock (dest, pos, n);

            // Freeing retired buffers here keeps the clone's queue from
            // filling on a long render; it is the message thread's job in the
            // live engine, and this background thread owns this clone.
            off.collectRetiredSamples();

            if ((pos & 0x3ffff) == 0)
            {
                const float within = (float) pos / (float) total;
                const float base01 = (float) passDone.load (std::memory_order_relaxed) / (float) totalPasses;
                progress.store (base01 + within / (float) totalPasses, std::memory_order_relaxed);
            }
        }

        off.setPlaying (false);
        return true;
    }

    static float bufferPeak (const juce::AudioBuffer<float>& b)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            peak = juce::jmax (peak, b.getMagnitude (ch, 0, b.getNumSamples()));
        return peak;
    }

    bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& src, float gain)
    {
        f.deleteFile();
        auto stream = f.createOutputStream();
        if (stream == nullptr || ! stream->openedOk())
            return false;

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), sampleRate, 2, 24, {}, 0));
        if (writer == nullptr)
            return false;
        stream.release();   // the writer owns it now

        // Chunked so the gain is applied without a second full-length copy.
        constexpr int chunk = 16384;
        juce::AudioBuffer<float> tmp (2, chunk);
        const int total = src.getNumSamples();
        for (int pos = 0; pos < total; pos += chunk)
        {
            const int n = juce::jmin (chunk, total - pos);
            for (int ch = 0; ch < 2; ++ch)
            {
                tmp.copyFrom (ch, 0, src, ch, pos, n);
                if (gain < 1.0f)
                    tmp.applyGain (ch, 0, n, gain);
            }
            if (! writer->writeFromAudioSampleBuffer (tmp, 0, n))
                return false;
        }
        return true;
    }

    juce::File uniqueFile (const juce::String& name) const
    {
        return dir.getChildFile (name).getNonexistentSibling (false);
    }

    static juce::String sanitise (const juce::String& s)
    {
        auto t = s.upToLastOccurrenceOf (".", false, false);
        if (t.isEmpty()) t = s;
        return t.retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_")
                .substring (0, 24);
    }

    static constexpr int kBlock = 512;

    AudioEngine& live;
    PadSamples   pads;
    PadNames     padNames;
    juce::File   dir;
    juce::String base;
    bool         stems;
    double       sampleRate;
    int          totalPasses = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Exporter)
};
