#include "AudioEngine.h"

namespace
{
    //  Padé approximant of tanh. std::tanh is a libm call of ~30 cycles and
    //  the drive stage runs it on every sample of every channel; this is a
    //  handful of multiplies, accurate to well under a dB inside the range
    //  that matters, and clamped so the ratio cannot run away for large
    //  arguments (drive pushes |x| up to ~25).
    inline float fastTanh (float x) noexcept
    {
        const float x2 = x * x;
        const float a  = x  * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
        const float b  = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
        return juce::jlimit (-1.0f, 1.0f, a / b);
    }
}

// ============================================================================
//  AudioEngine implementation. See AudioEngine.h for the threading contract.
// ============================================================================

AudioEngine::AudioEngine()
{
    for (auto& l : patternLength) l.store (kMinPatLen, std::memory_order_relaxed);

    //  -1 is "silent". Zero-initialised would mean "parked at the very start",
    //  and the UI would draw a read head on a pad that has never played.
    for (auto& p : padPos) p.store (-1.0f, std::memory_order_relaxed);

    //  Every pad fully sent to every effect. An effect only becomes audible
    //  when its own MIX is raised, so this default means switching one on
    //  still affects the whole kit, exactly as it did before pads could be
    //  taken off a send individually.
    for (auto& pad : padSend)  for (auto& s : pad) s.store (1.0f, std::memory_order_relaxed);
    //  The SMOOTHER, though, starts closed. What it follows is the pad send
    //  times the effect's own MIX, and every MIX starts at zero - starting it
    //  at the pad value instead opened all six sends for the first 20 ms of
    //  the app's life, which with the tone effects meant the dry path was
    //  nearly muted for exactly as long.
    for (auto& pad : smSend)   pad.fill (0.0f);
}

AudioEngine::~AudioEngine()
{
    for (auto*& p : padSample)
        if (p != nullptr) { p->decReferenceCount(); p = nullptr; }
    for (auto& slot : pendingPad)
        if (auto* p = slot.exchange (nullptr)) p->decReferenceCount();
    retired.drain ([] (SampleBuffer* p) { if (p) p->decReferenceCount(); });
}

// ---------------------------------------------------------------------------
//  Audio thread
// ---------------------------------------------------------------------------

void AudioEngine::prepareToPlay (double sampleRate, int maxBlockSize) noexcept
{
    systemSampleRate = (sampleRate > 0.0) ? sampleRate : 44100.0;
    maxBlock         = (maxBlockSize > 0) ? maxBlockSize : 512;

    // ~20 s mono record buffer (allocated here, never in the callback).
    // A bounce clone never records, so it does not pay for one.
    if (! offlineMode)
    {
        recordBuffer.setSize (1, (int) (20.0 * systemSampleRate));
        recordBuffer.clear();
    }

    juce::dsp::ProcessSpec spec { systemSampleRate, (juce::uint32) juce::jmax (1, maxBlock), 2 };
    masterFilter.prepare (spec);
    masterFilter.reset();

    hpFilter.prepare (spec);
    hpFilter.reset();
    hpFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);

    reverb.prepare (spec);
    reverb.reset();

    fxDry.setSize (2, juce::jmax (1, maxBlock));

    //  One buffer per effect bus plus the scratch a single pad is rendered
    //  into before it is split between the dry path and its sends. Allocated
    //  here for the same reason as everything else in this function: the
    //  callback is not allowed to.
    padScratch.setSize (2, juce::jmax (1, maxBlock));
    padScratch.clear();
    for (auto& b : fxBus) { b.setSize (2, juce::jmax (1, maxBlock)); b.clear(); }
    busRinging.fill (false);

    delayLine.prepare (spec);
    delayLine.setMaximumDelayInSamples (juce::jmax (1, (int) (systemSampleRate * 1.0)));
    delayLine.reset();
}

void AudioEngine::releaseResources() noexcept
{
    for (auto& v : voices)
        v.kill();
}

void AudioEngine::triggerPad (int slot, int extraSemis, float vel, float from01) noexcept
{
    if (slot < 0 || slot >= kNumPads)
        return;
    auto* sb = padSample[(size_t) slot];
    if (sb == nullptr)
        return;

    // Choke group: fade out any other pad's voices sharing this pad's group.
    const int group = padChoke[(size_t) slot].load (std::memory_order_relaxed);
    if (group > 0)
        for (auto& v : voices)
            if (v.active && v.slot >= 0 && v.slot != slot
                && padChoke[(size_t) v.slot].load (std::memory_order_relaxed) == group)
                v.release();

    //  AUTOCUT: this pad cuts its own tail. steal() rather than release() -
    //  the point is that the previous hit is GONE by the time the new one
    //  speaks, so it gets the 1.5 ms declick fade and not the pad's musical
    //  release, which on a long tail would leave the two overlapping for as
    //  long as the release lasts and defeat the whole thing.
    if (padSelfCut[(size_t) slot].load (std::memory_order_relaxed))
        for (auto& v : voices)
            if (v.active && v.slot == slot)
                v.steal (systemSampleRate);

    const int len = sb->buffer.getNumSamples();
    int st = padStart[(size_t) slot].load (std::memory_order_relaxed);
    int en = padEnd[(size_t) slot].load (std::memory_order_relaxed);
    if (en <= 0 || en > len) en = len;
    if (st < 0 || st >= en)  st = 0;

    //  Auditioning from a point in the waveform: start there and keep the
    //  pad's end, so a tap plays the rest of the sound and not a slice of it.
    //  A tap past the end would start a voice with nothing left to read.
    if (from01 >= 0.0f)
    {
        const int at = (int) (juce::jlimit (0.0f, 1.0f, from01) * (float) len);
        if (at < en - 2) st = juce::jmax (0, at);
    }

    triggeredMask.fetch_or ((std::uint32_t) (1u << slot), std::memory_order_relaxed);

    //  Pick a voice out of the shared pool. A free one if there is one, and
    //  otherwise the oldest - by serial, so "oldest" means the one that has
    //  been sounding longest rather than whichever slot the counter happens
    //  to be pointing at. Two caps decide when we steal: the pad's own share
    //  of the pool, so one held pad cannot starve the other fifteen, and the
    //  pool itself.
    //
    //  A stolen voice gets steal(), which is a fast fade rather than a cut -
    //  it keeps the same voice alive for a few milliseconds while the new
    //  note starts elsewhere. Only when the WHOLE pool is busy does the new
    //  note have to land on the voice being taken, and then the fade has
    //  nowhere to happen. At 48 voices that is rare and it is buried.
    Voice* chosen  = nullptr;
    Voice* oldest  = nullptr;
    Voice* oldestOnPad = nullptr;
    int    onPad   = 0;

    for (auto& v : voices)
    {
        if (! v.active)
        {
            if (chosen == nullptr) chosen = &v;
            continue;
        }

        if (oldest == nullptr || v.serial < oldest->serial)
            oldest = &v;

        if (v.slot == slot)
        {
            ++onPad;
            if (oldestOnPad == nullptr || v.serial < oldestOnPad->serial)
                oldestOnPad = &v;
        }
    }

    if (onPad >= kMaxVoicesOnPad && oldestOnPad != nullptr)
    {
        oldestOnPad->steal (systemSampleRate);
        if (chosen == nullptr) chosen = oldestOnPad;
    }
    else if (chosen == nullptr && oldest != nullptr)
    {
        oldest->steal (systemSampleRate);
        chosen = oldest;
    }

    if (chosen == nullptr)
        return;

    chosen->serial = ++voiceSerial;
    chosen->start (slot,
                   padPitch[(size_t) slot].load (std::memory_order_relaxed) + (float) extraSemis,
                   effectiveGain (slot),
                   sb->sourceSampleRate, systemSampleRate,
                   st, en,
                   padLoop[(size_t) slot].load (std::memory_order_relaxed),
                   padReverse[(size_t) slot].load (std::memory_order_relaxed),
                   len,
                   padPan[(size_t) slot].load (std::memory_order_relaxed),
                   padAttack[(size_t) slot].load (std::memory_order_relaxed),
                   padRelease[(size_t) slot].load (std::memory_order_relaxed),
                   padKeepLength[(size_t) slot].load (std::memory_order_relaxed),
                   vel);
}

void AudioEngine::renderNextBlock (juce::AudioBuffer<float>& out,
                                   int startSample, int numSamples) noexcept
{
    juce::ScopedNoDenormals noDenormals;

    // 0. Capture mic input BEFORE clearing (input is in channel 0 on entry).
    if (recording.load (std::memory_order_acquire) && out.getNumChannels() > 0)
    {
        const int cap = recordBuffer.getNumSamples();
        int rp = recordPos.load (std::memory_order_relaxed);
        const int n = juce::jmin (numSamples, cap - rp);
        if (n > 0)
        {
            recordBuffer.copyFrom (0, rp, out.getReadPointer (0, startSample), n);
            rp += n;
            recordPos.store (rp, std::memory_order_relaxed);
        }
        if (rp >= cap)
            recording.store (false, std::memory_order_release);   // full -> auto stop
    }

    // 0b. Latency probe: arm on the first block after the request, so the
    //     record index and the click's frame share one timeline (the capture
    //     above and this counter both advance by numSamples per callback).
    if (probeArm.exchange (false, std::memory_order_acq_rel))
    {
        probeCounter = 0;
        probeClickAt = (int) (0.30 * systemSampleRate);   // let the stream settle first
        probeLength  = (int) (1.20 * systemSampleRate);
        recordPos.store (0, std::memory_order_relaxed);
        recording.store (true, std::memory_order_release);
        probing.store (true, std::memory_order_release);
    }

    // 1. Adopt freshly published per-pad samples.
    for (int slot = 0; slot < kNumPads; ++slot)
        if (auto* incoming = pendingPad[(size_t) slot].exchange (nullptr, std::memory_order_acquire))
        {
            retired.push (padSample[(size_t) slot]);
            padSample[(size_t) slot] = incoming;
        }

    // 2. Clear output.
    out.clear (startSample, numSamples);

    // 2b. Work out, once per block, where each pad's signal is going: how
    //     much of it into each effect bus, and how much is left for the dry
    //     path. The tone effects take a pad off dry by the same amount they
    //     take it on to their own bus, so a fully-sent filter replaces the
    //     sound instead of sitting beside it. Delay and reverb add on top.
    //
    //     Sends are smoothed per block for the same reason every other knob
    //     here is: a raw jump in a gain that is being summed is a click.
    const int  busChans = juce::jmin (2, out.getNumChannels());
    const float kSend   = 1.0f - std::exp ((float) -numSamples / (0.020f * (float) systemSampleRate));
    const float fxMixNow[kNumFx] =
    {
        juce::jlimit (0.0f, 1.0f, fxMix.load  (std::memory_order_relaxed)),
        juce::jlimit (0.0f, 1.0f, hpMix.load  (std::memory_order_relaxed)),
        juce::jlimit (0.0f, 1.0f, drvMix.load (std::memory_order_relaxed)),
        juce::jlimit (0.0f, 1.0f, dlyMix.load (std::memory_order_relaxed)),
        juce::jlimit (0.0f, 1.0f, crMix.load  (std::memory_order_relaxed)),
        juce::jlimit (0.0f, 1.0f, rvMix.load  (std::memory_order_relaxed))
    };

    float sendGain[kNumPads][kNumFx];
    float dryGain[kNumPads];
    bool  busFed[kNumFx] = {};
    bool  padSplit[kNumPads];

    for (int p = 0; p < kNumPads; ++p)
    {
        float dry = 1.0f;
        bool  any = false;
        for (int f = 0; f < kNumFx; ++f)
        {
            const float target = fxMixNow[f] * padSend[(size_t) p][(size_t) f].load (std::memory_order_relaxed);
            float& sm = smSend[(size_t) p][(size_t) f];
            sm += kSend * (target - sm);
            const float g = (sm < 0.0005f && target < 0.0005f) ? 0.0f : sm;
            sendGain[p][f] = g;
            if (g > 0.0f) { any = true; busFed[f] = true; }
            if (fxIsTone[f]) dry *= (1.0f - g);
        }
        dryGain[p]  = dry;
        padSplit[p] = any;
    }

    for (int f = 0; f < kNumFx; ++f)
        if (busFed[f] || busRinging[f])
            fxBus[(size_t) f].clear (startSample, numSamples);

    auto renderVoices = [&] (int s, int nn) noexcept
    {
        // Control-rate retarget first: looping/long voices keep following
        // their pad's VOLUME/PAN knobs instead of freezing start() values.
        for (int v = 0; v < kNumVoices; ++v)
        {
            auto& vc = voices[(size_t) v];
            if (vc.active && vc.slot >= 0)
                vc.retarget (effectiveGain (vc.slot),
                             padPan [(size_t) vc.slot].load (std::memory_order_relaxed));
        }

        //  A pad that sends nowhere goes straight to the master, exactly as
        //  before. One that does is rendered on its own first, because you
        //  cannot take a share of a signal that has already been summed with
        //  fifteen others.
        for (int p = 0; p < kNumPads; ++p)
        {
            bool sounding = false;
            for (int v = 0; v < kNumVoices && ! sounding; ++v)
                sounding = voices[(size_t) v].active && voices[(size_t) v].slot == p;

            if (! sounding)
                continue;

            if (! padSplit[p])
            {
                for (int v = 0; v < kNumVoices; ++v)
                {
                    auto& vc = voices[(size_t) v];
                    if (vc.active && vc.slot == p)
                        vc.render (out, s, nn, padSample[(size_t) p]);
                }
                continue;
            }

            for (int ch = 0; ch < 2; ++ch)
                padScratch.clear (ch, s, nn);

            for (int v = 0; v < kNumVoices; ++v)
            {
                auto& vc = voices[(size_t) v];
                if (vc.active && vc.slot == p)
                    vc.render (padScratch, s, nn, padSample[(size_t) p]);
            }

            if (dryGain[p] > 0.0005f)
                for (int ch = 0; ch < busChans; ++ch)
                    out.addFrom (ch, s, padScratch, ch, s, nn, dryGain[p]);

            for (int f = 0; f < kNumFx; ++f)
                if (sendGain[p][f] > 0.0f)
                    for (int ch = 0; ch < busChans; ++ch)
                        fxBus[(size_t) f].addFrom (ch, s, padScratch, ch, s, nn, sendGain[p][f]);
        }
    };

    // 3. Drain UI trigger commands (taps fire at block start — human jitter
    //    dwarfs one block; the sequencer below is the sample-accurate path).
    constexpr int kMaxCmds = 256;
    Command local[kMaxCmds];
    int n = 0;
    commands.drain ([&local, &n] (const Command& c) noexcept { if (n < kMaxCmds) local[n++] = c; });
    for (int i = 0; i < n; ++i)
        handleCommand (local[i]);

    // 4+5. Sequencer transport + voice rendering, sample-accurate: the block
    //      is split at step boundaries, each step fires exactly on its frame
    //      (the old version quantised every step to frame 0 of the block —
    //      up to ~12 ms of jitter and flams at high BPM/large blocks).
    const bool isPlaying = playing.load (std::memory_order_relaxed);
    if (isPlaying && ! wasPlaying)
    {
        currentStep = -1; stepAccum = 0.0; chainPos = 0;
        songStep = -1; songBar.store (-1, std::memory_order_relaxed);
        for (int ln = 0; ln < kSongLanes; ++ln) { lanePattern[ln] = -1; laneStartStep[ln] = 0; }
        playStep.store (-1, std::memory_order_relaxed);
    }
    else if (! isPlaying && wasPlaying)
    {
        playStep.store (-1, std::memory_order_relaxed);
    }
    wasPlaying = isPlaying;

    if (! isPlaying)
    {
        renderVoices (startSample, numSamples);
    }
    else
    {
        const int chainLen = chainLength.load (std::memory_order_relaxed);
        if (chainLen <= 0) chainPos = 0;
        int patternIdx = chainLen > 0 ? chainSlots[(size_t) chainPos].load (std::memory_order_relaxed)
                                      : editPattern.load (std::memory_order_relaxed);
        playingPattern.store (patternIdx, std::memory_order_relaxed);

        const double beatsPerStep   = 0.25;   // 16th notes
        const double secPerStep     = (60.0 / juce::jmax (20.0, (double) bpm.load (std::memory_order_relaxed))) * beatsPerStep;
        const double samplesPerStep = juce::jmax (1.0, secPerStep * systemSampleRate);

        auto firePatternStep = [this] (int bank, int stepInPattern) noexcept
        {
            const std::uint16_t mask = patternBank[(size_t) bank][(size_t) stepInPattern].load (std::memory_order_relaxed);
            for (int p = 0; p < kNumPads; ++p)
                if ((mask & (std::uint16_t) (1u << p)) != 0)
                    triggerPad (p, (int) stepNote[(size_t) bank][(size_t) stepInPattern][(size_t) p].load (std::memory_order_relaxed));
        };

        auto fireStep = [this, chainLen, &patternIdx, &firePatternStep]() noexcept
        {
            //  Song mode: the timeline drives everything. Several lanes run at
            //  once, so a pattern, a break and a one-shot can all land on the
            //  same bar — which a single queue of banks could never express.
            if (songMode.load (std::memory_order_relaxed))
            {
                const int bars = juce::jlimit (1, kSongBars, songBars.load (std::memory_order_relaxed));
                const int total = bars * kBarSteps;
                songStep = (songStep + 1) % total;
                const int bar = songStep / kBarSteps;
                songBar.store (bar, std::memory_order_relaxed);

                // At the top of a bar, read what each lane starts here.
                if (songStep % kBarSteps == 0)
                {
                    for (int ln = 0; ln < kSongLanes; ++ln)
                    {
                        const int cell = songCell[(size_t) ln][(size_t) bar].load (std::memory_order_relaxed);
                        if (cell == kContinued)
                            continue;                         // a pattern from an earlier bar still owns this lane
                        if (cell > 0 && cell <= kNumPatterns)
                        {
                            lanePattern[ln]   = cell - 1;
                            laneStartStep[ln] = songStep;
                        }
                        else if (cell < 0)
                        {
                            lanePattern[ln] = -1;             // a one-shot owns no lane time
                            triggerPad (-cell - 1);
                        }
                        else
                        {
                            lanePattern[ln] = -1;             // empty: this lane rests
                        }
                    }
                }

                for (int ln = 0; ln < kSongLanes; ++ln)
                {
                    const int bank = lanePattern[ln];
                    if (bank < 0) continue;
                    const int len = juce::jlimit (kMinPatLen, kMaxPatLen, patternLength[(size_t) bank].load (std::memory_order_relaxed));
                    const int off = songStep - laneStartStep[ln];
                    if (off < 0 || off >= len) { lanePattern[ln] = -1; continue; }
                    firePatternStep (bank, off);
                    if (ln == 0) playingPattern.store (bank, std::memory_order_relaxed);
                }

                currentStep = songStep % kBarSteps;
                playStep.store (currentStep, std::memory_order_relaxed);
                return;
            }

            const int prevStep = currentStep;
            const int len = juce::jlimit (kMinPatLen, kMaxPatLen, patternLength[(size_t) patternIdx].load (std::memory_order_relaxed));
            currentStep = (currentStep + 1) % len;

            // This bank's pattern (its own length, not always 16) just
            // completed a full loop — advance the chain.
            if (currentStep == 0 && prevStep >= 0 && chainLen > 0)
            {
                chainPos   = (chainPos + 1) % chainLen;
                patternIdx = chainSlots[(size_t) chainPos].load (std::memory_order_relaxed);
                playingPattern.store (patternIdx, std::memory_order_relaxed);
            }

            firePatternStep (patternIdx, currentStep);
            playStep.store (currentStep, std::memory_order_relaxed);
        };

        if (currentStep < 0)
            fireStep();   // first step exactly at transport start

        int offset    = startSample;
        int remaining = numSamples;
        while (remaining > 0)
        {
            const double toBoundary = samplesPerStep - stepAccum;
            const int seg = juce::jlimit (1, remaining, (int) std::ceil (toBoundary));
            renderVoices (offset, seg);
            stepAccum += seg;
            offset    += seg;
            remaining -= seg;
            if (stepAccum >= samplesPerStep - 1.0e-9)
            {
                stepAccum -= samplesPerStep;
                fireStep();   // voices started here render from the next segment on
            }
        }
        stepPhase.store ((float) (stepAccum / samplesPerStep), std::memory_order_relaxed);
    }

    // 5b. The six effect buses: ISO, HPF, DRIVE, CRUSH, DELAY, REVERB.
    //     Each one runs on its own input, made upstream out of the pads that
    //     were sent to it, and returns into the master at full level. The
    //     wet/dry balance that used to live here now lives in the send, which
    //     is what lets a single pad be soaked in delay while the rest stay
    //     dry - impossible while the effects were inserts across everything.
    //
    //     A bus runs while it is being fed AND for as long as it keeps making
    //     sound after the feed stops. That is the whole point of a send: shut
    //     it and the delay repeats already inside the line still come out and
    //     die away on their own, instead of being cut off mid-tail. When a
    //     bus finally falls silent it is left alone entirely, so effects
    //     nobody is using cost nothing.
    {
        const int chans = busChans;

        // Block-rate smoothing coefficient for a ~20 ms time constant.
        const float kBlock = 1.0f - std::exp ((float) -numSamples / (0.020f * (float) systemSampleRate));
        const float nyq    = (float) (systemSampleRate * 0.45);

        auto live = [&] (int f) noexcept { return busFed[f] || busRinging[f]; };

        auto blockFor = [this, startSample, numSamples, chans] (int f) noexcept
        {
            return juce::dsp::AudioBlock<float> (fxBus[(size_t) f].getArrayOfWritePointers(),
                                                 (size_t) chans, (size_t) startSample, (size_t) numSamples);
        };

        //  Return the bus to the master and decide whether it is still alive.
        //  The threshold is far below anything audible; it exists so a reverb
        //  tail is not processed forever after it has decayed to nothing.
        auto returnBus = [this, &out, startSample, numSamples, chans] (int f) noexcept
        {
            auto& bus = fxBus[(size_t) f];
            for (int ch = 0; ch < chans; ++ch)
                out.addFrom (ch, startSample, bus, ch, startSample, numSamples);
            busRinging[(size_t) f] = (bus.getMagnitude (startSample, numSamples) > 1.0e-5f);
        };

        // --- 1. ISO: low-pass, the one you sweep on a break. --------------
        {
            const float cutT = juce::jlimit (20.0f, nyq, fxCutoff.load (std::memory_order_relaxed));
            const float resT = juce::jlimit (0.1f, 4.0f, fxReso.load (std::memory_order_relaxed));
            smCutoff += kBlock * (cutT - smCutoff);
            smReso   += kBlock * (resT - smReso);

            const bool active = live (0);
            if (active)
            {
                if (! filterWasActive) masterFilter.reset();
                masterFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
                masterFilter.setCutoffFrequency (smCutoff);
                masterFilter.setResonance (smReso);
                auto b = blockFor (0);
                juce::dsp::ProcessContextReplacing<float> ctx (b);
                masterFilter.process (ctx);
                returnBus (0);
            }
            filterWasActive = active;
        }

        // --- 2. HPF: its own filter, so ISO + HPF = band-pass. ------------
        {
            const float frqT = juce::jlimit (20.0f, nyq, hpFreq.load (std::memory_order_relaxed));
            const float resT = juce::jlimit (0.1f, 4.0f, hpReso.load (std::memory_order_relaxed));
            smHpFreq += kBlock * (frqT - smHpFreq);
            smHpReso += kBlock * (resT - smHpReso);

            const bool active = live (1);
            if (active)
            {
                if (! hpWasActive) hpFilter.reset();
                hpFilter.setCutoffFrequency (smHpFreq);
                hpFilter.setResonance (smHpReso);
                auto b = blockFor (1);
                juce::dsp::ProcessContextReplacing<float> ctx (b);
                hpFilter.process (ctx);
                returnBus (1);
            }
            hpWasActive = active;
        }

        // --- 3. DRIVE: tanh, then a tone control. -------------------------
        {
            const float drvT  = juce::jlimit (0.0f, 1.0f, fxDrive.load (std::memory_order_relaxed));
            const float toneT = juce::jlimit (200.0f, 20000.0f, drvTone.load (std::memory_order_relaxed));
            smDrive   += kBlock * (drvT  - smDrive);
            smDrvTone += kBlock * (toneT - smDrvTone);

            if (live (2))
            {
                const float k  = 1.0f + smDrive * 24.0f;      // gain into the tanh
                //  Compensate by the gain going IN, not by tanh's own ceiling:
                //  tanh(k) is ~1 for any useful k, so that "makeup" was a
                //  no-op and DRIVE at 70% came out three times louder than
                //  dry - a distortion knob that is really a volume knob.
                const float mk = 1.0f / (1.0f + smDrive * 2.5f);
                const float a  = juce::jlimit (0.0f, 1.0f,
                                    1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi
                                                     * smDrvTone / (float) systemSampleRate));
                for (int ch = 0; ch < chans; ++ch)
                {
                    float* w = fxBus[2].getWritePointer (ch, startSample);
                    float lp = drvLp[ch];
                    for (int i = 0; i < numSamples; ++i)
                    {
                        lp += a * (fastTanh (k * w[i]) * mk - lp);
                        w[i] = lp;
                    }
                    drvLp[ch] = lp;
                }
                returnBus (2);
            }
        }

        // --- 4. CRUSH: bit depth and sample-and-hold, the two halves of lo-fi.
        {
            if (live (4))
            {
                const float bits   = juce::jlimit (1.0f, 16.0f, crBits.load (std::memory_order_relaxed));
                const float levels = juce::jmax (1.0f, std::pow (2.0f, bits) * 0.5f);
                const float step   = juce::jmax (1.0f, crRate.load (std::memory_order_relaxed));

                float* w0 = fxBus[4].getWritePointer (0, startSample);
                float* w1 = (chans > 1) ? fxBus[4].getWritePointer (1, startSample) : w0;
                for (int i = 0; i < numSamples; ++i)
                {
                    crPhase += 1.0f;
                    const bool take = (crPhase >= step);
                    if (take) crPhase -= step;
                    for (int ch = 0; ch < chans; ++ch)
                    {
                        float* w = (ch == 0) ? w0 : w1;
                        if (take) crHold[ch] = std::round (w[i] * levels) / levels;
                        w[i] = crHold[ch];
                    }
                }
                returnBus (4);
            }
        }

        // --- 5. DELAY. Time is smoothed PER SAMPLE: a per-block jump through
        //        a linear-interp line is a hard discontinuity (crackle on
        //        every TIME move).
        {
            const float fbT  = juce::jlimit (0.0f, 0.95f, dlyFb.load (std::memory_order_relaxed));
            const float dsT  = juce::jlimit (1.0f, (float) (systemSampleRate - 1.0),
                                             dlyTime.load (std::memory_order_relaxed) * (float) systemSampleRate / 1000.0f);
            smDlyFb  += kBlock * (fbT  - smDlyFb);
            if (smDlySamp <= 0.0f) smDlySamp = dsT;            // first block: no sweep from 0
            const float kSamp = 1.0f - std::exp (-1.0f / (0.020f * (float) systemSampleRate));

            if (live (3))
            {
                float* w0 = fxBus[3].getWritePointer (0, startSample);
                float* w1 = (chans > 1) ? fxBus[3].getWritePointer (1, startSample) : w0;
                for (int i = 0; i < numSamples; ++i)
                {
                    smDlySamp += kSamp * (dsT - smDlySamp);
                    delayLine.setDelay (smDlySamp);
                    for (int ch = 0; ch < chans; ++ch)
                    {
                        float* w = (ch == 0) ? w0 : w1;
                        const float in = w[i];
                        const float d  = delayLine.popSample (ch);
                        delayLine.pushSample (ch, in + d * smDlyFb);
                        w[i] = d;
                    }
                }
                returnBus (3);
            }
        }

        // --- 6. REVERB. Wet only: the dry it would mix back already reached
        //        the master by the direct path, and adding it twice would
        //        only comb-filter the sound.
        {
            if (live (5))
            {
                juce::Reverb::Parameters prm;
                prm.roomSize   = juce::jlimit (0.0f, 1.0f, rvSize.load (std::memory_order_relaxed));
                prm.damping    = juce::jlimit (0.0f, 1.0f, rvDamp.load (std::memory_order_relaxed));
                prm.wetLevel   = 1.0f;
                prm.dryLevel   = 0.0f;
                prm.width      = 1.0f;
                prm.freezeMode = 0.0f;
                reverb.setParameters (prm);

                auto b = blockFor (5);
                juce::dsp::ProcessContextReplacing<float> ctx (b);
                reverb.process (ctx);
                returnBus (5);
            }
        }
    }

    // 5d. Master safety. Sixteen pads at full level plus a delay with
    //     feedback and a reverb tail will pass 0 dBFS, and what comes out of
    //     an integer DAC then is hard clipping: the ugliest sound a sampler
    //     can make, and one the user cannot see coming.
    //
    //     This is NOT a loudness stage. Below -0.5 dBFS it is mathematically
    //     transparent — the branch does nothing at all — and above it the
    //     signal is bent rather than cut, which is audible as saturation
    //     instead of as tearing. The export already measured and compensated;
    //     the thing you actually listen to had nothing.
    {
        constexpr float thresh = 0.944f;      // -0.5 dBFS
        const int outCh = juce::jmin (2, out.getNumChannels());
        for (int ch = 0; ch < outCh; ++ch)
        {
            float* w = out.getWritePointer (ch, startSample);
            for (int i = 0; i < numSamples; ++i)
            {
                const float v = w[i];
                if (v > thresh || v < -thresh)
                {
                    const float sign = (v < 0.0f) ? -1.0f : 1.0f;
                    const float over = (v * sign - thresh) / (1.0f - thresh);
                    w[i] = sign * (thresh + (1.0f - thresh) * fastTanh (over));
                }
            }
        }
    }

    // 5b-probe. The click, written AFTER the effects so nothing colours or
    //     delays it. A single sample would never leave a phone speaker, so it
    //     is a short decaying 3 kHz burst: a hard onset the microphone can
    //     find, and high enough to sit clear of room rumble.
    if (probing.load (std::memory_order_acquire))
    {
        const int outCh = juce::jmin (2, out.getNumChannels());
        constexpr int burst = 192;
        for (int i = 0; i < numSamples; ++i)
        {
            const int t = probeCounter + i - probeClickAt;
            if (t >= 0 && t < burst)
            {
                const float env = std::exp (-(float) t / 45.0f);
                const float v = 0.9f * env * std::sin (2.0f * juce::MathConstants<float>::pi
                                                       * 3000.0f * (float) t / (float) systemSampleRate);
                for (int ch = 0; ch < outCh; ++ch)
                    out.getWritePointer (ch, startSample)[i] = v;
            }
        }

        probeCounter += numSamples;
        if (probeCounter >= probeLength)
        {
            recording.store (false, std::memory_order_release);
            probing.store (false, std::memory_order_release);
        }
    }

    // 5c. Feed the scope ring (post-FX mono sum) for the spectrum display.
    {
        int wi = scopeWrite.load (std::memory_order_relaxed);
        const int outCh = out.getNumChannels();
        const float* l = out.getReadPointer (0, startSample);
        const float* r = (outCh > 1) ? out.getReadPointer (1, startSample) : l;
        for (int i = 0; i < numSamples; ++i)
        {
            scope[(size_t) wi] = 0.5f * (l[i] + r[i]);
            wi = (wi + 1) & (kScopeSize - 1);
        }
        scopeWrite.store (wi, std::memory_order_release);
    }

    // 6. Diagnostic test tone.
    int tt = testToneRemaining.load (std::memory_order_relaxed);
    if (tt > 0)
    {
        const int total = juce::jmax (1, (int) (0.4 * systemSampleRate));
        const int fade  = juce::jmax (1, (int) (0.005 * systemSampleRate));
        const double inc = 2.0 * juce::MathConstants<double>::pi * 440.0 / systemSampleRate;
        const int outCh = out.getNumChannels();
        float* dL = out.getWritePointer (0);
        float* dR = (outCh > 1) ? out.getWritePointer (1) : dL;
        const int nOut = juce::jmin (tt, numSamples);
        for (int i = 0; i < nOut; ++i)
        {
            const int done = total - tt;
            float amp = 0.2f;
            if (done < fade)    amp *= (float) done / (float) fade;
            else if (tt < fade) amp *= (float) tt   / (float) fade;
            const float s = amp * (float) std::sin (testPhase);
            dL[startSample + i] += s;
            if (outCh > 1) dR[startSample + i] += s;
            testPhase += inc;
            if (testPhase > 2.0 * juce::MathConstants<double>::pi) testPhase -= 2.0 * juce::MathConstants<double>::pi;
            --tt;
        }
        testToneRemaining.store (tt, std::memory_order_relaxed);
    }

    //  Where each pad's read head is, as a fraction of its whole source. The
    //  UI draws it over the waveform, so what you hear and what you see are
    //  the same thing moving. Sixteen relaxed stores a block; nothing reads
    //  back, so there is no ordering to get wrong.
    {
        float p[(size_t) kNumPads];
        for (auto& x : p) x = -1.0f;

        for (const auto& v : voices)
        {
            if (! v.active || v.slot < 0 || v.slot >= kNumPads) continue;

            if (auto* sb = padSample[(size_t) v.slot])
            {
                const int srcLen = sb->buffer.getNumSamples();
                if (srcLen > 1)
                    p[(size_t) v.slot] = juce::jmax (p[(size_t) v.slot],
                                                     (float) (v.pos / (double) srcLen));
            }
        }

        for (int i = 0; i < kNumPads; ++i)
            padPos[(size_t) i].store (p[(size_t) i], std::memory_order_relaxed);
    }

    // 7. Output peaks for the VU (max-hold until the UI reads) — last stage,
    //    after every contributor including the test tone.
    {
        const int outCh = out.getNumChannels();
        const float pl = out.getMagnitude (0, startSample, numSamples);
        const float pr = (outCh > 1) ? out.getMagnitude (1, startSample, numSamples) : pl;
        float prev = outPeakL.load (std::memory_order_relaxed);
        if (pl > prev) outPeakL.store (pl, std::memory_order_relaxed);
        prev = outPeakR.load (std::memory_order_relaxed);
        if (pr > prev) outPeakR.store (pr, std::memory_order_relaxed);
    }
}

void AudioEngine::handleCommand (const Command& c) noexcept
{
    switch (c.type)
    {
        case Command::Type::NoteOn:  triggerPad (c.slot, 0, c.velocity, c.from01); break;
        case Command::Type::NoteOff:
            if (c.slot >= 0 && c.slot < kNumPads)
                for (auto& v : voices)
                    if (v.active && v.slot == c.slot)
                        v.release();
            break;
        case Command::Type::Panic:   for (auto& v : voices) v.kill(); break;
    }
}

// ---------------------------------------------------------------------------
//  Message thread
// ---------------------------------------------------------------------------

void AudioEngine::postNoteOn (int slot, float vel) noexcept
{
    Command c; c.type = Command::Type::NoteOn; c.slot = slot; c.velocity = vel;
    commands.push (c);
}

void AudioEngine::postNoteOnFrom (int slot, float from01, float vel) noexcept
{
    Command c; c.type = Command::Type::NoteOn; c.slot = slot; c.velocity = vel;
    c.from01 = from01;
    commands.push (c);
}

void AudioEngine::postNoteOff (int slot) noexcept
{
    Command c; c.type = Command::Type::NoteOff; c.slot = slot;
    commands.push (c);
}

void AudioEngine::postPanic() noexcept
{
    Command c; c.type = Command::Type::Panic;
    commands.push (c);
}

void AudioEngine::postTestTone() noexcept
{
    testToneRemaining.store ((int) (0.4 * systemSampleRate), std::memory_order_relaxed);
}

int AudioEngine::getSampleLength (int slot) const noexcept
{
    if (slot < 0 || slot >= kNumPads) return 0;
    auto* sb = padSample[(size_t) slot];
    return sb != nullptr ? sb->buffer.getNumSamples() : 0;
}

void AudioEngine::publishSample (int slot, SampleBuffer::Ptr newBuffer) noexcept
{
    if (slot < 0 || slot >= kNumPads || newBuffer == nullptr)
        return;

    // Reset this pad's trim window to the full new sample.
    padStart[(size_t) slot].store (0, std::memory_order_relaxed);
    padEnd[(size_t) slot].store (newBuffer->buffer.getNumSamples(), std::memory_order_relaxed);

    newBuffer->incReferenceCount();
    if (auto* old = pendingPad[(size_t) slot].exchange (newBuffer.get(), std::memory_order_release))
        old->decReferenceCount();
}

void AudioEngine::collectRetiredSamples() noexcept
{
    retired.drain ([] (SampleBuffer* p) { if (p) p->decReferenceCount(); });
}

void AudioEngine::copyScope (float* dst, int n) noexcept
{
    const int wi = scopeWrite.load (std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
        dst[i] = scope[(size_t) ((wi - n + i) & (kScopeSize - 1))];
}

void AudioEngine::setStep (int patternIdx, int step, int pad, bool on) noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps || pad < 0 || pad >= kNumPads) return;
    const std::uint16_t bit = (std::uint16_t) (1u << pad);
    std::uint16_t cur = patternBank[(size_t) patternIdx][(size_t) step].load (std::memory_order_relaxed);
    cur = on ? (std::uint16_t) (cur | bit) : (std::uint16_t) (cur & ~bit);
    patternBank[(size_t) patternIdx][(size_t) step].store (cur, std::memory_order_relaxed);
}

void AudioEngine::clearPattern (int patternIdx) noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns) return;
    for (auto& m : patternBank[(size_t) patternIdx]) m.store (0, std::memory_order_relaxed);
}

void AudioEngine::setPatternLength (int patternIdx, int len) noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns) return;
    patternLength[(size_t) patternIdx].store (juce::jlimit (kMinPatLen, kMaxPatLen, len), std::memory_order_relaxed);
}

int AudioEngine::getPatternLength (int patternIdx) const noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns) return kMinPatLen;
    return patternLength[(size_t) patternIdx].load (std::memory_order_relaxed);
}

void AudioEngine::setStepNote (int patternIdx, int step, int pad, int semis) noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps || pad < 0 || pad >= kNumPads) return;
    stepNote[(size_t) patternIdx][(size_t) step][(size_t) pad].store ((std::int8_t) juce::jlimit (-24, 24, semis), std::memory_order_relaxed);
}

int AudioEngine::getStepNote (int patternIdx, int step, int pad) const noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps || pad < 0 || pad >= kNumPads) return 0;
    return stepNote[(size_t) patternIdx][(size_t) step][(size_t) pad].load (std::memory_order_relaxed);
}

bool AudioEngine::addToChain (int patternIdx) noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns) return false;
    const int len = chainLength.load (std::memory_order_relaxed);
    if (len >= kMaxChain) return false;
    chainSlots[(size_t) len].store (patternIdx, std::memory_order_relaxed);
    chainLength.store (len + 1, std::memory_order_relaxed);
    return true;
}

// ---------------------------------------------------------------------------
//  Recording (message thread)
// ---------------------------------------------------------------------------

void AudioEngine::startRecording (int slot) noexcept
{
    if (slot < 0 || slot >= kNumPads) return;
    recordSlot = slot;
    recordPos.store (0, std::memory_order_relaxed);
    recording.store (true, std::memory_order_release);
}

SampleBuffer::Ptr AudioEngine::finishRecording() noexcept
{
    recording.store (false, std::memory_order_release);
    SampleBuffer::Ptr sb;
    const int len = recordPos.load (std::memory_order_acquire);
    if (len > 4)
    {
        sb = new SampleBuffer();
        sb->buffer.setSize (1, len);
        sb->buffer.copyFrom (0, 0, recordBuffer, 0, 0, len);
        sb->sourceSampleRate = systemSampleRate;
        publishSample (recordSlot, sb);
    }
    return sb;
}

float AudioEngine::getRecordSeconds() const noexcept
{
    return (float) (recordPos.load (std::memory_order_relaxed) / systemSampleRate);
}

// ---------------------------------------------------------------------------
//  Offline bounce (message thread)
// ---------------------------------------------------------------------------

void AudioEngine::copyStateFrom (const AudioEngine& s) noexcept
{
    // Straight atomic-to-atomic copies. Both engines are touched from the
    // message thread here; the source may be sounding, which only means a
    // knob moved mid-copy could land on either side of the export. Nothing
    // tears — every field is an independent atomic.
    auto copyArr = [] (auto& dst, const auto& src)
    {
        for (size_t i = 0; i < dst.size(); ++i)
            dst[i].store (src[i].load (std::memory_order_relaxed), std::memory_order_relaxed);
    };

    copyArr (padPitch,   s.padPitch);
    copyArr (padGain,    s.padGain);
    copyArr (padStart,   s.padStart);
    copyArr (padEnd,     s.padEnd);
    copyArr (padLoop,    s.padLoop);
    copyArr (padReverse, s.padReverse);
    copyArr (padKeepLength, s.padKeepLength);
    copyArr (padChoke,   s.padChoke);
    copyArr (padPan,     s.padPan);
    copyArr (padAttack,  s.padAttack);
    copyArr (padRelease, s.padRelease);
    copyArr (padMute,    s.padMute);
    copyArr (padSolo,    s.padSolo);
    for (size_t i = 0; i < padSend.size(); ++i) copyArr (padSend[i], s.padSend[i]);
    for (size_t i = 0; i < smSend.size();  ++i) smSend[i] = s.smSend[i];
    refreshSolo();

    bpm.store (s.bpm.load (std::memory_order_relaxed), std::memory_order_relaxed);
    editPattern.store (s.editPattern.load (std::memory_order_relaxed), std::memory_order_relaxed);
    copyArr (patternLength, s.patternLength);
    copyArr (chainSlots,    s.chainSlots);
    chainLength.store (s.chainLength.load (std::memory_order_relaxed), std::memory_order_relaxed);

    for (size_t b = 0; b < patternBank.size(); ++b)
    {
        copyArr (patternBank[b], s.patternBank[b]);
        for (size_t st = 0; st < stepNote[b].size(); ++st)
            copyArr (stepNote[b][st], s.stepNote[b][st]);
    }

    songMode.store (s.songMode.load (std::memory_order_relaxed), std::memory_order_relaxed);
    songBars.store (s.songBars.load (std::memory_order_relaxed), std::memory_order_relaxed);
    for (size_t ln = 0; ln < songCell.size(); ++ln)
        copyArr (songCell[ln], s.songCell[ln]);

    auto copyOne = [] (auto& dst, const auto& src)
    {
        dst.store (src.load (std::memory_order_relaxed), std::memory_order_relaxed);
    };
    for (auto pair : { std::pair<std::atomic<float>*, const std::atomic<float>*>
                         { &fxCutoff, &s.fxCutoff }, { &fxReso,  &s.fxReso  }, { &fxMix,   &s.fxMix   },
                         { &hpFreq,   &s.hpFreq   }, { &hpReso,  &s.hpReso  }, { &hpMix,   &s.hpMix   },
                         { &fxDrive,  &s.fxDrive  }, { &drvTone, &s.drvTone }, { &drvMix,  &s.drvMix  },
                         { &dlyTime,  &s.dlyTime  }, { &dlyFb,   &s.dlyFb   }, { &dlyMix,  &s.dlyMix  },
                         { &crBits,   &s.crBits   }, { &crRate,  &s.crRate  }, { &crMix,   &s.crMix   },
                         { &rvSize,   &s.rvSize   }, { &rvDamp,  &s.rvDamp  }, { &rvMix,   &s.rvMix   } })
        copyOne (*pair.first, *pair.second);
    fxType.store (s.fxType.load (std::memory_order_relaxed), std::memory_order_relaxed);

    // Start the FX smoothers already AT their targets. A live engine glides
    // over ~20 ms because a knob just moved; a bounce has no such history,
    // and gliding from the defaults would fade the filter in over the first
    // bar of every export.
    smCutoff  = fxCutoff.load (std::memory_order_relaxed);
    smReso    = fxReso.load   (std::memory_order_relaxed);
    smFxMix   = fxMix.load    (std::memory_order_relaxed);
    smHpFreq  = hpFreq.load   (std::memory_order_relaxed);
    smHpReso  = hpReso.load   (std::memory_order_relaxed);
    smHpMix   = hpMix.load    (std::memory_order_relaxed);
    smDrive   = fxDrive.load  (std::memory_order_relaxed);
    smDrvTone = drvTone.load  (std::memory_order_relaxed);
    smDrvMix  = drvMix.load   (std::memory_order_relaxed);
    smCrMix   = crMix.load    (std::memory_order_relaxed);
    smRvMix   = rvMix.load    (std::memory_order_relaxed);
    smDlyMix  = dlyMix.load   (std::memory_order_relaxed);
    smDlyFb   = dlyFb.load    (std::memory_order_relaxed);
    smDlySamp = (float) (dlyTime.load (std::memory_order_relaxed) * 0.001 * systemSampleRate);
}

int AudioEngine::lengthInSteps() const noexcept
{
    if (songMode.load (std::memory_order_relaxed))
    {
        // The song is as long as its LAST occupied bar, not as long as the
        // slider says: exporting eight bars of silence after the track ends
        // is the kind of thing you only notice once the file is uploaded.
        const int bars = juce::jlimit (1, kSongBars, songBars.load (std::memory_order_relaxed));
        int last = -1;
        for (int ln = 0; ln < kSongLanes; ++ln)
            for (int b = 0; b < bars; ++b)
                if (songCell[(size_t) ln][(size_t) b].load (std::memory_order_relaxed) != 0)
                    last = juce::jmax (last, b);
        return (last < 0) ? 0 : (last + 1) * kBarSteps;
    }

    const int chainLen = chainLength.load (std::memory_order_relaxed);
    if (chainLen > 0)
    {
        int total = 0;
        for (int i = 0; i < chainLen; ++i)
        {
            const int bank = juce::jlimit (0, kNumPatterns - 1, chainSlots[(size_t) i].load (std::memory_order_relaxed));
            total += juce::jlimit (kMinPatLen, kMaxPatLen, patternLength[(size_t) bank].load (std::memory_order_relaxed));
        }
        return total;
    }

    const int bank = juce::jlimit (0, kNumPatterns - 1, editPattern.load (std::memory_order_relaxed));
    return juce::jlimit (kMinPatLen, kMaxPatLen, patternLength[(size_t) bank].load (std::memory_order_relaxed));
}

bool AudioEngine::hasContentToRender() const noexcept
{
    auto bankHasNotes = [this] (int bank) noexcept
    {
        const int len = juce::jlimit (kMinPatLen, kMaxPatLen, patternLength[(size_t) bank].load (std::memory_order_relaxed));
        for (int s = 0; s < len; ++s)
            if (patternBank[(size_t) bank][(size_t) s].load (std::memory_order_relaxed) != 0)
                return true;
        return false;
    };

    if (songMode.load (std::memory_order_relaxed))
    {
        const int bars = juce::jlimit (1, kSongBars, songBars.load (std::memory_order_relaxed));
        for (int ln = 0; ln < kSongLanes; ++ln)
            for (int b = 0; b < bars; ++b)
            {
                const int cell = songCell[(size_t) ln][(size_t) b].load (std::memory_order_relaxed);
                if (cell < 0) return true;                                   // a one-shot always sounds
                if (cell > 0 && cell <= kNumPatterns && bankHasNotes (cell - 1)) return true;
            }
        return false;
    }

    const int chainLen = chainLength.load (std::memory_order_relaxed);
    if (chainLen > 0)
    {
        for (int i = 0; i < chainLen; ++i)
            if (bankHasNotes (juce::jlimit (0, kNumPatterns - 1, chainSlots[(size_t) i].load (std::memory_order_relaxed))))
                return true;
        return false;
    }

    return bankHasNotes (juce::jlimit (0, kNumPatterns - 1, editPattern.load (std::memory_order_relaxed)));
}

// ---------------------------------------------------------------------------
//  Latency probe (message thread)
// ---------------------------------------------------------------------------

void AudioEngine::startLatencyProbe() noexcept
{
    probeArm.store (true, std::memory_order_release);
}

//  Find the click in what the microphone heard. The threshold is derived from
//  the room's own noise in the 300 ms BEFORE the click rather than being a
//  constant: a quiet room and a busy street need different bars, and a fixed
//  one would either miss the click or trigger on a passing car.
float AudioEngine::finishLatencyProbe() const noexcept
{
    if (probing.load (std::memory_order_acquire)) return -1.0f;

    const int captured = recordPos.load (std::memory_order_acquire);
    const int emitted  = probeClickAt;
    if (captured <= emitted + 64 || recordBuffer.getNumSamples() <= emitted) return -1.0f;

    const float* r = recordBuffer.getReadPointer (0);

    double noise = 0.0;
    const int noiseTo = juce::jmax (1, emitted - 1024);
    for (int i = 0; i < noiseTo; ++i) noise += (double) r[i] * r[i];
    const float floorRms = (float) std::sqrt (noise / (double) noiseTo);
    const float thresh   = juce::jmax (8.0f * floorRms, 0.02f);

    // A round trip past half a second is not a measurement, it is a car door.
    const int limit = juce::jmin (captured, emitted + (int) (0.5 * systemSampleRate));
    for (int i = emitted; i < limit; ++i)
        if (std::abs (r[i]) > thresh)
            return (float) ((double) (i - emitted) * 1000.0 / systemSampleRate);

    return -1.0f;
}
