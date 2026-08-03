#include "AudioEngine.h"

// ============================================================================
//  AudioEngine implementation. See AudioEngine.h for the threading contract.
// ============================================================================

AudioEngine::AudioEngine()
{
    for (auto& l : patternLength) l.store (kMinPatLen, std::memory_order_relaxed);
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
    recordBuffer.setSize (1, (int) (20.0 * systemSampleRate));
    recordBuffer.clear();

    juce::dsp::ProcessSpec spec { systemSampleRate, (juce::uint32) juce::jmax (1, maxBlock), 2 };
    masterFilter.prepare (spec);
    masterFilter.reset();

    delayLine.prepare (spec);
    delayLine.setMaximumDelayInSamples (juce::jmax (1, (int) (systemSampleRate * 1.0)));
    delayLine.reset();
}

void AudioEngine::releaseResources() noexcept
{
    for (auto& v : voices)
        v.kill();
}

void AudioEngine::triggerPad (int slot, int extraSemis) noexcept
{
    if (slot < 0 || slot >= kNumPads)
        return;
    auto* sb = padSample[(size_t) slot];
    if (sb == nullptr)
        return;

    // Choke group: fade out any other pad's voices sharing this pad's group.
    const int group = padChoke[(size_t) slot].load (std::memory_order_relaxed);
    if (group > 0)
        for (int j = 0; j < kNumPads; ++j)
            if (j != slot && padChoke[(size_t) j].load (std::memory_order_relaxed) == group)
                for (int k = 0; k < kVoicesPerPad; ++k)
                    voices[(size_t) (j * kVoicesPerPad + k)].release();

    const int len = sb->buffer.getNumSamples();
    int st = padStart[(size_t) slot].load (std::memory_order_relaxed);
    int en = padEnd[(size_t) slot].load (std::memory_order_relaxed);
    if (en <= 0 || en > len) en = len;
    if (st < 0 || st >= en)  st = 0;

    triggeredMask.fetch_or ((std::uint32_t) (1u << slot), std::memory_order_relaxed);

    // Round-robin voice pair: declick-steal the old instance, start the new.
    voices[(size_t) (slot * kVoicesPerPad + voiceFlip[(size_t) slot])].steal (systemSampleRate);
    voiceFlip[(size_t) slot] ^= 1;
    voices[(size_t) (slot * kVoicesPerPad + voiceFlip[(size_t) slot])].start (slot,
                                 padPitch[(size_t) slot].load (std::memory_order_relaxed) + (float) extraSemis,
                                 effectiveGain (slot),
                                 sb->sourceSampleRate, systemSampleRate,
                                 st, en,
                                 padLoop[(size_t) slot].load (std::memory_order_relaxed),
                                 padReverse[(size_t) slot].load (std::memory_order_relaxed),
                                 len,
                                 padPan[(size_t) slot].load (std::memory_order_relaxed),
                                 padAttack[(size_t) slot].load (std::memory_order_relaxed),
                                 padRelease[(size_t) slot].load (std::memory_order_relaxed));
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

    // 1. Adopt freshly published per-pad samples.
    for (int slot = 0; slot < kNumPads; ++slot)
        if (auto* incoming = pendingPad[(size_t) slot].exchange (nullptr, std::memory_order_acquire))
        {
            retired.push (padSample[(size_t) slot]);
            padSample[(size_t) slot] = incoming;
        }

    // 2. Clear output.
    out.clear (startSample, numSamples);

    auto renderVoices = [this, &out] (int s, int nn) noexcept
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
        for (int v = 0; v < kNumVoices; ++v)
            if (voices[(size_t) v].slot >= 0)
                voices[(size_t) v].render (out, s, nn, padSample[(size_t) voices[(size_t) v].slot]);
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

    // 5b. Master FX: filter -> drive -> delay. Each stage is BYPASSED when
    //     neutral, so the default signal path is untouched (a bug here must
    //     never silence the whole output). All knob-driven params are
    //     one-pole smoothed here (~20 ms) — the atomics jump per block and
    //     applying them raw produced zipper (filter) and crackle (delay time).
    {
        const int outCh = out.getNumChannels();

        // Block-rate smoothing coefficient for a ~20 ms time constant.
        const float kBlock = 1.0f - std::exp ((float) -numSamples / (0.020f * (float) systemSampleRate));

        const int   ft      = fxType.load   (std::memory_order_relaxed);
        const float cutT    = juce::jlimit (20.0f, (float) (systemSampleRate * 0.45), fxCutoff.load (std::memory_order_relaxed));
        const float resoT   = juce::jlimit (0.1f, 4.0f, fxReso.load (std::memory_order_relaxed));
        smCutoff += kBlock * (cutT  - smCutoff);
        smReso   += kBlock * (resoT - smReso);

        // LPF fully open (high cutoff, low resonance) = transparent -> skip,
        // but only when both the smoothed value AND the target agree, so the
        // stage never pops in/out mid-glide. State is reset on re-entry —
        // stale integrator state from minutes ago is not a valid IC.
        const bool filterActive = ! (ft == 0 && smCutoff >= 19000.0f && cutT >= 19000.0f
                                             && smReso <= 0.72f && resoT <= 0.72f);
        if (filterActive)
        {
            if (! filterWasActive)
                masterFilter.reset();
            juce::dsp::AudioBlock<float> block (out.getArrayOfWritePointers(), (size_t) outCh,
                                                (size_t) startSample, (size_t) numSamples);
            masterFilter.setType (ft == 0 ? juce::dsp::StateVariableTPTFilterType::lowpass
                                          : juce::dsp::StateVariableTPTFilterType::highpass);
            masterFilter.setCutoffFrequency (smCutoff);
            masterFilter.setResonance (smReso);
            juce::dsp::ProcessContextReplacing<float> ctx (block);
            masterFilter.process (ctx);
        }
        filterWasActive = filterActive;

        const float driveT = fxDrive.load (std::memory_order_relaxed);
        smDrive += kBlock * (driveT - smDrive);
        if (smDrive > 0.0001f || driveT > 0.0001f)
        {
            const float k  = 1.0f + smDrive * 24.0f;           // drive gain into tanh
            const float mk = 1.0f / std::tanh (k);             // makeup to keep level
            for (int ch = 0; ch < outCh; ++ch)
            {
                float* w = out.getWritePointer (ch, startSample);
                for (int i = 0; i < numSamples; ++i)
                    w[i] = std::tanh (k * w[i]) * mk;
            }
        }

        // Delay (feedback) after the filter/drive. Time is smoothed PER
        // SAMPLE — a per-block jump through a linear-interp delay line is a
        // hard discontinuity (crackle on every TIME move).
        const float mixT = dlyMix.load (std::memory_order_relaxed);
        const float fbT  = juce::jlimit (0.0f, 0.95f, dlyFb.load (std::memory_order_relaxed));
        const float dsT  = juce::jlimit (1.0f, (float) (systemSampleRate - 1.0),
                                         dlyTime.load (std::memory_order_relaxed) * (float) systemSampleRate / 1000.0f);
        smDlyMix += kBlock * (mixT - smDlyMix);
        smDlyFb  += kBlock * (fbT  - smDlyFb);
        if (smDlySamp <= 0.0f) smDlySamp = dsT;                // first block: no sweep from 0
        const float kSamp = 1.0f - std::exp (-1.0f / (0.020f * (float) systemSampleRate));

        if (smDlyMix > 0.001f || mixT > 0.001f)
        {
            float* w0 = out.getWritePointer (0, startSample);
            float* w1 = (outCh > 1) ? out.getWritePointer (1, startSample) : w0;
            for (int i = 0; i < numSamples; ++i)
            {
                smDlySamp += kSamp * (dsT - smDlySamp);
                delayLine.setDelay (smDlySamp);
                for (int ch = 0; ch < juce::jmin (2, outCh); ++ch)
                {
                    float* w = (ch == 0) ? w0 : w1;
                    const float in = w[i];
                    const float d  = delayLine.popSample (ch);
                    delayLine.pushSample (ch, in + d * smDlyFb);
                    w[i] = in * (1.0f - smDlyMix) + d * smDlyMix;
                }
            }
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
        case Command::Type::NoteOn:  triggerPad (c.slot); break;
        case Command::Type::NoteOff:
            if (c.slot >= 0 && c.slot < kNumPads)
                for (int k = 0; k < kVoicesPerPad; ++k)
                    voices[(size_t) (c.slot * kVoicesPerPad + k)].release();
            break;
        case Command::Type::Panic:   for (auto& v : voices) v.kill(); break;
    }
}

// ---------------------------------------------------------------------------
//  Message thread
// ---------------------------------------------------------------------------

void AudioEngine::postNoteOn (int slot) noexcept
{
    Command c; c.type = Command::Type::NoteOn; c.slot = slot;
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
