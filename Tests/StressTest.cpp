// ZATI expo bench — the audio half.
//
// The interface can be measured by walking a component tree. The engine has to
// be RUN, and run the way a stand runs it: sixteen pads slammed at once, the
// same pad retriggered faster than a human can, the command queue pushed past
// its own capacity, the device torn down and rebuilt mid-phrase. What comes
// out is checked for the three things that end a demo — silence, a NaN, and a
// block that took longer than it had.
#include <JuceHeader.h>
#include "../Source/AudioEngine.h"
#include <chrono>
#include <cstdio>

using Clock = std::chrono::steady_clock;

//  Sixteen coherent sine waves sum to sixteen times one sine wave, which no
//  sampler ever plays and which puts the master saturator into permanent
//  action - a bench that measures its own test signal. Each pad gets its own
//  phase and a noise floor, so the sum behaves like sixteen real one-shots.
static SampleBuffer::Ptr makeSample (double sr, double seconds, float freq)
{
    auto* sb = new SampleBuffer();
    const int n = (int) (sr * seconds);
    juce::Random rng ((juce::int64) (freq * 1000.0f));
    const float phase = rng.nextFloat() * juce::MathConstants<float>::twoPi;
    sb->buffer.setSize (2, n);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
        {
            const float env = std::exp (-3.0f * (float) i / (float) n);
            const float tone = std::sin (phase + juce::MathConstants<float>::twoPi * freq * (float) i / (float) sr);
            sb->buffer.setSample (c, i, 0.6f * env * (0.8f * tone + 0.2f * (rng.nextFloat() * 2.0f - 1.0f)));
        }
    sb->sourceSampleRate = sr;
    return SampleBuffer::Ptr (sb);
}

struct Stats { double peak = 0, rms = 0, worstBlockMs = 0, totalMs = 0; int blocks = 0; bool nan = false; bool clipped = false;
               long hot = 0, samples = 0; };   // hot = samples the master saturator had to bend

static Stats runBlocks (AudioEngine& e, juce::AudioBuffer<float>& buf, int blockSize, int nBlocks,
                        std::function<void (int)> beforeBlock = {})
{
    Stats s;
    double acc = 0.0;
    for (int b = 0; b < nBlocks; ++b)
    {
        if (beforeBlock) beforeBlock (b);
        const auto t0 = Clock::now();
        e.renderNextBlock (buf, 0, blockSize);
        const double ms = std::chrono::duration<double, std::milli> (Clock::now() - t0).count();
        s.worstBlockMs = juce::jmax (s.worstBlockMs, ms);
        s.totalMs += ms;
        ++s.blocks;

        for (int c = 0; c < buf.getNumChannels(); ++c)
            for (int i = 0; i < blockSize; ++i)
            {
                const float v = buf.getSample (c, i);
                if (std::isnan (v) || std::isinf (v)) s.nan = true;
                if (std::abs (v) > 1.0f) s.clipped = true;
                s.peak = juce::jmax (s.peak, (double) std::abs (v));
                if (std::abs (v) > 0.944f) ++s.hot;      // -0.5 dBFS, the saturator's knee
                ++s.samples;
                acc += (double) v * v;
            }
    }
    s.rms = std::sqrt (acc / juce::jmax (1.0, (double) (nBlocks * blockSize * buf.getNumChannels())));
    return s;
}

static void report (const char* name, const Stats& s, double blockMsBudget)
{
    const double load = s.totalMs / juce::jmax (1, s.blocks) / blockMsBudget * 100.0;
    std::printf ("%-34s peak %.3f  rms %.4f  sat %5.2f%%  avg %.3f ms  worst %.3f ms  budget %.2f ms  load %.1f%%  %s%s\n",
                 name, s.peak, s.rms, 100.0 * (double) s.hot / juce::jmax (1.0, (double) s.samples),
                 s.totalMs / juce::jmax (1, s.blocks), s.worstBlockMs, blockMsBudget, load,
                 s.nan ? "NaN! " : "", s.clipped ? "CLIP!" : "");
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const double sr = 48000.0;
    const int    bs = 128;                       // Oboe's low-latency burst on a modern phone
    const double budgetMs = 1000.0 * bs / sr;    // 2.67 ms

    AudioEngine e;
    e.prepareToPlay (sr, bs);
    e.setPolyphony (32, 4);

    juce::AudioBuffer<float> buf (2, bs);

    //  padGain is value-initialised to zero: the app sets every channel at
    //  startup, so a bench that skips it renders a perfect, silent pass.
    for (int p = 0; p < 16; ++p)
    {
        e.setPadGain (p, 0.85f);
        e.publishSample (p, makeSample (44100.0, 1.2, 110.0f * (float) (p + 1)));
    }

    // The engine adopts a published sample at the top of a block.
    runBlocks (e, buf, bs, 8);

    // 1. SILENCE — nothing playing must cost nothing and produce nothing.
    report ("idle", runBlocks (e, buf, bs, 400), budgetMs);

    // 2. ALL SIXTEEN AT ONCE — the thing every demo does in the first minute.
    {
        auto s = runBlocks (e, buf, bs, 600, [&e] (int b)
        {
            if (b == 0) for (int p = 0; p < 16; ++p) e.postNoteOn (p, 1.0f);
        });
        report ("16 pads at once", s, budgetMs);
    }

    // 3. MACHINE-GUN RETRIGGER — one pad, every single block, for 5 seconds.
    //    This is where a voice pool leaks or a choke group deadlocks.
    {
        auto s = runBlocks (e, buf, bs, 1875, [&e] (int) { e.postNoteOn (3, 0.9f); });
        report ("1 pad retriggered every block", s, budgetMs);
    }

    // 4. QUEUE SATURATION — push far more commands per block than the FIFO
    //    holds. Dropping is fine; wedging is not, so we check it still plays.
    {
        auto s = runBlocks (e, buf, bs, 400, [&e] (int)
        {
            for (int k = 0; k < 64; ++k) e.postNoteOn (k % 16, 0.7f);
        });
        report ("queue saturated (64 cmds/block)", s, budgetMs);
        std::printf ("%-34s dropped %d commands, still audible: %s\n", "", e.takeDroppedCommands(),
                     s.rms > 1.0e-4 ? "YES" : "NO  <-- WEDGED");
    }

    // 5. DEVICE CHANGE MID-PHRASE — the headphone-unplug path, 40 times.
    {
        int reprepares = 0;
        auto s = runBlocks (e, buf, bs, 800, [&e, &reprepares] (int b)
        {
            if (b % 20 == 0) { e.prepareToPlay (b % 40 == 0 ? 44100.0 : 48000.0, 128); ++reprepares; }
            if (b % 7 == 0)  e.postNoteOn (b % 16, 0.9f);
        });
        report ("route changes mid-phrase", s, budgetMs);
        std::printf ("%-34s %d re-prepares, still audible: %s\n", "", reprepares,
                     s.rms > 1.0e-4 ? "YES" : "NO  <-- DEAD");
    }

    // 6. EVERY BUFFER SIZE — the load has to fit the budget at the SMALLEST
    //    one, because that is the one that makes the app feel like hardware.
    for (int b : { 64, 96, 128, 192, 256, 480, 512 })
    {
        AudioEngine e2;
        e2.prepareToPlay (sr, b);
        e2.setPolyphony (32, 4);
        for (int p = 0; p < 16; ++p) { e2.setPadGain (p, 0.85f); e2.publishSample (p, makeSample (44100.0, 1.2, 110.0f * (float) (p + 1))); }
        juce::AudioBuffer<float> bb (2, b);
        runBlocks (e2, bb, b, 4);
        auto s = runBlocks (e2, bb, b, (int) (sr * 3 / b), [&e2] (int i) { if (i % 4 == 0) e2.postNoteOn (i % 16, 1.0f); });
        char name[64]; std::snprintf (name, sizeof name, "buffer %d (%.2f ms round trip)", b, 2.0 * 1000.0 * b / sr);
        report (name, s, 1000.0 * b / sr);
    }

    return 0;
}
