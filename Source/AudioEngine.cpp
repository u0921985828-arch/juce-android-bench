#include "AudioEngine.h"
#include <cmath>
#include <limits>

namespace
{
    //  Padé approximant of tanh. std::tanh is a libm call of ~30 cycles and
    //  the drive stage runs it on every sample of every channel; this is a
    //  handful of multiplies, accurate to well under a dB inside the range
    //  that matters, and clamped so the ratio cannot run away for large
    //  arguments (drive pushes |x| up to ~25).
    inline float fastTanh (float x) noexcept
    {
        //  FUERA DE RANGO, ANTES DE ELEVAR AL CUADRADO.
        //
        //  Esta aproximacion empieza por x*x, y el cuadrado de un valor grande
        //  NO CABE en un float: 1e30 al cuadrado es infinito, arriba y abajo
        //  de la fraccion, e inf/inf es NaN. Y jlimit no lo tapa - una
        //  comparacion con NaN siempre es falsa, asi que lo deja pasar tal
        //  cual. De ahi salia el NaN que apagaba la maquina con una muestra de
        //  valores enormes: el saturador del master es lo ultimo que toca el
        //  audio y lo convertia en silencio permanente.
        //
        //  Mas alla de +-5 la tangente hiperbolica vale +-1 con nueve cifras,
        //  asi que cortar ahi no cambia el sonido de nada y quita el infinito
        //  de en medio. Cuesta dos comparaciones, y este es el mismo tanh que
        //  usa DRV, que tenia el mismo agujero.
        if (! std::isfinite (x)) return 0.0f;
        if (x >  5.0f) return  1.0f;
        if (x < -5.0f) return -1.0f;

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

    //  AUTOCUT on, on every pad. Retriggering a pad over its own tail is the
    //  exception, not the rule: it is what a held chord wants and what a hat,
    //  a stab or a vocal played fast does not. The switch is still there to
    //  turn it off per pad.
    for (auto& c : padSelfCut) c.store (true, std::memory_order_relaxed);

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

    //  El filtro de cada pad, abierto del todo. Cero seria 0 Hz - los 64 pads
    //  mudos en el arranque - que es lo que pasa cuando un parametro cuyo
    //  valor neutro NO es cero se deja con el cero del constructor.
    for (auto& c : padCutoff) c.store (kFiltOpenHz, std::memory_order_relaxed);
    for (auto& r : padReso)   r.store (0.0f, std::memory_order_relaxed);

    //  Y el estado de las etapas que RETIENEN un valor. Un cambio de ruta
    //  vuelve a pasar por aqui con el motor cargado, y dejar la muestra
    //  retenida del dispositivo anterior es un escalon de continua en la
    //  primera muestra del nuevo.
    drvLp[0] = drvLp[1] = 0.0f;   drvWasActive = false;
    crHold[0] = crHold[1] = 0.0f; crPhase = 0.0f; crWasActive = false;
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

void AudioEngine::prepareToPlay (double sampleRate, int maxBlockSize, int inputChannels) noexcept
{
    systemSampleRate = (sampleRate > 0.0) ? sampleRate : 44100.0;
    maxBlock         = (maxBlockSize > 0) ? maxBlockSize : 512;

    //  The record buffer, allocated here and never in the callback. This is
    //  the only moment it can safely change size: JUCE calls prepareToPlay
    //  before the stream starts, so no callback is inside it.
    //
    //  It used to be twenty seconds of mono, which is a hit and not a phrase.
    //  Now it is a minute, and it keeps both channels when the device actually
    //  gives us two - most phones have one microphone and hand back one, and
    //  the take is mono then, honestly rather than by duplication.
    //
    //  A bounce clone never records, so it does not pay for any of it.
    if (! offlineMode)
    {
        recordChannels = juce::jlimit (1, recordAllowStereo ? 2 : 1,
                                       inputChannels > 0 ? inputChannels : 1);

        //  A minute of stereo float at 48 kHz is 23 MB. If the allocation
        //  fails, fall back to something small rather than leaving the
        //  microphone with nowhere to write.
        try
        {
            recordBuffer.setSize (recordChannels, (int) (recordSeconds * systemSampleRate));
        }
        catch (const std::bad_alloc&)
        {
            recordChannels = 1;
            recordBuffer.setSize (1, (int) (5.0 * systemSampleRate));
        }

        recordBuffer.clear();
    }

    //  How many frames one silhouette column covers. FX-404 v232 puts the
    //  analyser's whole window on screen at 1x, and its window is fftSize -
    //  32768 frames, about 0.74 s at 44.1 kHz. Same span here, expressed as a
    //  duration so it holds at any device rate.
    scopeColLen = juce::jmax (1, (int) (0.74 * systemSampleRate / (double) kScopeCols));

    juce::dsp::ProcessSpec spec { systemSampleRate, (juce::uint32) juce::jmax (1, maxBlock), 2 };
    masterFilter.prepare (spec);
    masterFilter.reset();

    hpFilter.prepare (spec);
    hpFilter.reset();
    hpFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);

    //  La FDN reserva sus cuatro lineas y sus dos difusores aqui, que es el
    //  unico sitio donde puede reservar: en el render no se toca memoria.
    reverb.prepare (sampleRate, 2);
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

    //  A tap that arrived while the stream was going away must not fire into
    //  the one that replaces it, seconds later and out of nowhere.
    fallbackTriggers.store (0, std::memory_order_relaxed);
}

void AudioEngine::triggerPad (int slot, int extraSemis, float vel, float from01) noexcept
{
    if (slot < 0 || slot >= kNumPads)
        return;
    auto* sb = padSample[(size_t) slot];
    if (sb == nullptr)
        return;

    //  LA NOTA SALE DE AQUI Y NO DE OTRO SITIO.
    //
    //  triggerPad es el embudo por el que pasan TODOS los disparos - el dedo,
    //  el secuenciador, la cadena, una celda de la cancion, el MIDI que entra -
    //  asi que poner el envio aqui es lo unico que garantiza que no haya un
    //  camino que suene por dentro y no salga por el cable. Ponerlo en
    //  handleCommand, que es donde apetece, se habria dejado fuera al
    //  secuenciador, que es justo lo que la gente quiere mandar al hardware.
    //
    //  Va DESPUES de comprobar que el pad tiene sonido, a proposito: un pad
    //  vacio no suena, y mandar su nota haria que el aparato de al lado tocara
    //  algo que en esta app no se oye.
    //
    //  Y aqui no se envia nada. Se deja escrito - cuatro bytes en una cola sin
    //  cerrojos - y sigue. Enviar reserva memoria y habla con el sistema; esto
    //  es el hilo de audio.
    if (midiOutOn.load (std::memory_order_relaxed))
        midiOut.push ({ (std::uint8_t) slot,
                        (std::uint8_t) juce::jlimit (1, 127, (int) std::lround (vel * 127.0f)),
                        true });

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

    triggeredMask.fetch_or ((std::uint64_t) 1u << slot, std::memory_order_relaxed);

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

    //  Only as far as the device's pool goes. The array is always 64; a
    //  low-tier phone plays the first sixteen of it and never pays for the
    //  rest, which is the whole point of the tier.
    for (int vi = 0; vi < voiceLimit; ++vi)
    {
        auto& v = voices[(size_t) vi];

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

    if (onPad >= maxVoicesOnPad && oldestOnPad != nullptr)
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

    //  El pad del bombeo abre la envolvente de golpe. Aqui, en triggerPad, y
    //  no en el secuenciador: asi vale igual tocado a dedo que disparado por
    //  un paso, que es lo que un sidechain tiene que hacer.
    if (slot == duckPad.load (std::memory_order_relaxed))
        duckEnv = 1.0f;

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
                   vel,
                   padFadeIn[(size_t) slot].load (std::memory_order_relaxed),
                   padFadeOut[(size_t) slot].load (std::memory_order_relaxed));
}

void AudioEngine::renderNextBlock (juce::AudioBuffer<float>& out,
                                   int startSample, int numSamples) noexcept
{
    juce::ScopedNoDenormals noDenormals;

    //  0a. One renderer at a time. A phone changes audio route by tearing the
    //      stream down and building a new one, and the old callback thread can
    //      still be in here when the new one arrives. Everything below assumes
    //      it is alone: the command queue is single-consumer by contract, and
    //      two consumers do not glitch it, they WEDGE it - permanently, for
    //      the life of the process. This is the whole bug behind "I unplugged
    //      my headphones and the pads went dead while TEST still beeped": the
    //      test tone is a lone atomic and survived, the pads went through the
    //      queue and did not.
    //
    //      Losing one block during a route change is inaudible. Losing the
    //      transport is the app.
    if (inRender.exchange (true, std::memory_order_acquire))
    {
        out.clear (startSample, numSamples);
        return;
    }

    struct RenderGuard
    {
        std::atomic<bool>& flag;
        ~RenderGuard() { flag.store (false, std::memory_order_release); }
    } renderGuard { inRender };

    // 0. Capture mic input BEFORE clearing (input is in channel 0 on entry).
    //    Only when the take is coming from the microphone: a resample reads
    //    the master at the bottom of this function instead.
    if (recording.load (std::memory_order_acquire)
        && ! recordFromMaster.load (std::memory_order_acquire)
        && out.getNumChannels() > 0)
    {
        const int cap = recordBuffer.getNumSamples();
        int rp = recordPos.load (std::memory_order_relaxed);
        const int n = juce::jmin (numSamples, cap - rp);
        if (n > 0)
        {
            //  However many channels the device is giving us, up to what the
            //  buffer was sized for. On a phone with one microphone that is
            //  one, and the take stays mono.
            const int chans = juce::jmin (recordBuffer.getNumChannels(), out.getNumChannels());
            for (int ch = 0; ch < chans; ++ch)
                recordBuffer.copyFrom (ch, rp, out.getReadPointer (ch, startSample), n);

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

    // 1. Adopt freshly published per-pad samples, and let go of cleared ones.
    //    A clear is consumed FIRST so that "empty this pad, then load a new
    //    sound into it" in the same block ends with the new sound rather than
    //    with nothing.
    for (int slot = 0; slot < kNumPads; ++slot)
    {
        if (pendingClear[(size_t) slot].exchange (false, std::memory_order_acquire))
        {
            for (auto& v : voices)
                if (v.active && v.slot == slot)
                    v.kill();

            retired.push (padSample[(size_t) slot]);
            padSample[(size_t) slot] = nullptr;
            padStart[(size_t) slot].store (0, std::memory_order_relaxed);
            padEnd[(size_t) slot].store (0, std::memory_order_relaxed);
        }

        if (auto* incoming = pendingPad[(size_t) slot].exchange (nullptr, std::memory_order_acquire))
        {
            retired.push (padSample[(size_t) slot]);
            padSample[(size_t) slot] = incoming;
        }
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

    //  Dos preguntas de una vez, en lugar de 384: hay ALGUN efecto abierto, y
    //  manda ALGUN pad. Si las dos son que no, el reparto de todos los pads es
    //  "todo al seco" y no hay nada que suavizar - y ese es el estado en el
    //  que la maquina pasa la mayor parte del tiempo.
    const bool anyFxOpen = (fxMixNow[0] + fxMixNow[1] + fxMixNow[2]
                          + fxMixNow[3] + fxMixNow[4] + fxMixNow[5]) > 0.0f;
    const std::uint64_t sendMask = padSendMask.load (std::memory_order_relaxed);
    //  Y la de los filtros, que decide lo mismo: un pad filtrado tiene que
    //  renderizarse APARTE aunque no mande a ningun efecto, porque no se puede
    //  filtrar una senal que ya se sumo con otras quince.
    const std::uint64_t filtMask = padFiltMask.load (std::memory_order_relaxed);

    for (int p = 0; p < kNumPads; ++p)
    {
        const bool filtered = (filtMask >> (unsigned) p) & 1ull;
        //  ...y el suavizado tiene que TERMINAR de bajar antes de saltarse el
        //  pad, o un envio que se cierra se queda congelado a medio camino en
        //  vez de irse a cero: silencio a medias que no se va nunca. Por eso
        //  el corte mira tambien smSendHot, que es el pad que aun se mueve.
        const bool listed = (sendMask >> (unsigned) p) & 1ull;
        if (! listed && ! anyFxOpen && ! smSendHot[(size_t) p])
        {
            dryGain[p]  = 1.0f;
            padSplit[p] = filtered;      // sin envios pero con filtro: tambien aparte
            for (int f = 0; f < kNumFx; ++f) sendGain[p][f] = 0.0f;
            continue;
        }

        float dry = 1.0f;
        bool  any = false;
        bool  hot = false;
        for (int f = 0; f < kNumFx; ++f)
        {
            const float target = fxMixNow[f] * padSend[(size_t) p][(size_t) f].load (std::memory_order_relaxed);
            float& sm = smSend[(size_t) p][(size_t) f];
            sm += kSend * (target - sm);
            const float g = (sm < 0.0005f && target < 0.0005f) ? 0.0f : sm;
            sendGain[p][f] = g;
            if (g > 0.0f) { any = true; busFed[f] = true; }
            if (sm != 0.0f) hot = true;      // aun no ha terminado de bajar
            if (fxIsTone[f]) dry *= (1.0f - g);
        }
        dryGain[p]  = dry;
        padSplit[p] = any || filtered;
        smSendHot[(size_t) p] = hot;
    }

    for (int f = 0; f < kNumFx; ++f)
        if (busFed[f] || busRinging[f])
            fxBus[(size_t) f].clear (startSample, numSamples);

    auto renderVoices = [&] (int s, int nn) noexcept
    {
        //  One pass over the pool: bucket the live voices by pad, and read
        //  each pad's gain and pan ONCE instead of once per voice that
        //  happens to be on it.
        padFirstVoice.fill (-1);

        float padGainNow[kNumPads], padPanNow[kNumPads];
        bool  padTouched[kNumPads] = {};

        for (int v = voiceLimit - 1; v >= 0; --v)
        {
            auto& vc = voices[(size_t) v];
            if (! vc.active || vc.slot < 0 || vc.slot >= kNumPads) continue;

            if (! padTouched[vc.slot])
            {
                padTouched[vc.slot] = true;
                padGainNow[vc.slot] = effectiveGain (vc.slot);
                padPanNow [vc.slot] = padPan[(size_t) vc.slot].load (std::memory_order_relaxed);
            }

            // Control-rate retarget: a looping or long voice keeps following
            // its pad's VOLUME and PAN instead of freezing start()'s values.
            vc.retarget (padGainNow[vc.slot], padPanNow[vc.slot]);

            voiceNextInPad[(size_t) v] = padFirstVoice[(size_t) vc.slot];
            padFirstVoice[(size_t) vc.slot] = v;
        }

        //  A pad that sends nowhere goes straight to the master, exactly as
        //  before. One that does is rendered on its own first, because you
        //  cannot take a share of a signal that has already been summed with
        //  fifteen others.
        for (int p = 0; p < kNumPads; ++p)
        {
            if (padFirstVoice[(size_t) p] < 0)      // nothing of this pad is sounding
                continue;

            if (! padSplit[p])
            {
                for (int v = padFirstVoice[(size_t) p]; v >= 0; v = voiceNextInPad[(size_t) v])
                    voices[(size_t) v].render (out, s, nn, padSample[(size_t) p]);
                continue;
            }

            for (int ch = 0; ch < 2; ++ch)
                padScratch.clear (ch, s, nn);

            for (int v = padFirstVoice[(size_t) p]; v >= 0; v = voiceNextInPad[(size_t) v])
                voices[(size_t) v].render (padScratch, s, nn, padSample[(size_t) p]);

            //  EL FILTRO DEL PAD, sobre lo que el pad acaba de sonar y antes
            //  de repartirlo: el seco y los seis envios salen todos del mismo
            //  sitio, asi que filtrar aqui filtra las siete rutas de una vez.
            //  Filtrar despues habria querido decir siete filtros por pad.
            if ((filtMask >> (unsigned) p) & 1ull)
            {
                //  Coeficientes UNA VEZ POR BLOQUE, no por muestra: la tangente
                //  cuesta lo que cuesta y el corte lo mueve un dedo, no el
                //  audio. Y en float por muestra, en double por bloque.
                const float hz = juce::jlimit (20.0f, (float) (0.45 * systemSampleRate),
                                               padCutoff[(size_t) p].load (std::memory_order_relaxed));
                const float rs = padReso[(size_t) p].load (std::memory_order_relaxed);
                //  Q de 0.707 (Butterworth, sin pico) a 8. Mas arriba el filtro
                //  se pone a oscilar solo, que es un sintetizador y no un
                //  sampler: 8 son unos 18 dB de realce, suficiente para que un
                //  barrido cante y poco para que se desmande.
                const float q = 0.707f + rs * (8.0f - 0.707f);
                const float g = (float) std::tan (juce::MathConstants<double>::pi * (double) hz / systemSampleRate);
                const float k = 1.0f / q;
                const float a1 = 1.0f / (1.0f + g * (g + k));
                const float a2 = g * a1;
                const float a3 = g * a2;

                for (int ch = 0; ch < busChans; ++ch)
                {
                    auto& st = padFiltState[(size_t) p][(size_t) juce::jmin (ch, 1)];
                    float ic1 = st.ic1, ic2 = st.ic2;
                    float* d = padScratch.getWritePointer (ch, s);

                    for (int i = 0; i < nn; ++i)
                    {
                        const float x  = d[i];
                        const float v3 = x - ic2;
                        const float v1 = a1 * ic1 + a2 * v3;
                        const float v2 = ic2 + a2 * ic1 + a3 * v3;
                        ic1 = 2.0f * v1 - ic1;
                        ic2 = 2.0f * v2 - ic2;
                        d[i] = v2;                    // paso bajo
                    }

                    //  Los integradores, no las muestras: si uno se va a NaN
                    //  -una muestra envenenada entrando con Q alta- se queda
                    //  ahi para siempre y el pad enmudece hasta reiniciar,
                    //  porque el estado se realimenta. La barrera del master
                    //  limpia la SALIDA y no puede limpiar esto.
                    if (! std::isfinite (ic1) || ! std::isfinite (ic2)) ic1 = ic2 = 0.0f;
                    st.ic1 = ic1; st.ic2 = ic2;
                }
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
    //  ...y la de MIDI, en el mismo sitio y con el mismo trato: un teclado no
    //  es un ciudadano de segunda, dispara igual que un dedo. Dos colas, un
    //  consumidor.
    midiCommands.drain ([&local, &n] (const Command& c) noexcept { if (n < kMaxCmds) local[n++] = c; });

    //  CUANTIZAR EL DISPARO EN DIRECTO. Ver setLiveQuantise.
    //
    //  Al SIGUIENTE paso, no al mas cercano: el mas cercano puede estar en el
    //  pasado y no hay forma de disparar hacia atras. Con una ventana en la
    //  mitad del paso - si acabas de pasar uno, suena ya; si estas llegando al
    //  siguiente, espera - que es lo que redondear al mas cercano significa
    //  cuando solo se puede esperar.
    const bool quantiseNow = liveQuant.load (std::memory_order_relaxed)
                          && playing.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        if (quantiseNow && local[i].type == Command::Type::NoteOn && numPending < (int) pending.size())
        {
            const double sps = samplesPerStepNow();
            if (sps > 1.0)
            {
                const double toNext = sps - stepAccum;
                if (toNext > sps * 0.5)
                {
                    pending[(size_t) numPending++] = { (int) toNext, local[i].slot,
                                                       (int) local[i].semitones, local[i].velocity };
                    continue;
                }
            }
        }
        handleCommand (local[i]);
    }

    //  3b. The lifeboat. Anything the queue refused arrives here instead, as
    //      one bit per pad. It fires at the pad's own settings because that is
    //      all a bit can carry, which is exactly enough to keep playing.
    if (const auto mask = fallbackTriggers.exchange (0, std::memory_order_acquire))
        for (int p = 0; p < kNumPads; ++p)
            if ((mask & ((std::uint64_t) 1u << p)) != 0)
                triggerPad (p);

    // 4+5. Sequencer transport + voice rendering, sample-accurate: the block
    //      is split at step boundaries, each step fires exactly on its frame
    //      (the old version quantised every step to frame 0 of the block —
    //      up to ~12 ms of jitter and flams at high BPM/large blocks).
    const bool isPlaying = playing.load (std::memory_order_relaxed);
    if (isPlaying && ! wasPlaying)
    {
        currentStep = -1; stepAccum = 0.0; chainPos = 0; numPending = 0;
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

        //  Cuanto dura un paso, en negras. Era 0.25 fijo - semicorcheas - y por
        //  eso no habia forma de escribir un tresillo ni de bajar a fusas: el
        //  patron entero estaba clavado a la rejilla de 1/16.
        const double beatsPerStep   = (double) stepBeats.load (std::memory_order_relaxed);
        const double secPerStep     = (60.0 / juce::jmax (20.0, (double) bpm.load (std::memory_order_relaxed))) * beatsPerStep;
        const double samplesPerStep = juce::jmax (1.0, secPerStep * systemSampleRate);

        //  A step no longer speaks once, on the beat. Swing pushes it late and
        //  a roll makes it speak several times, so what a step produces is a
        //  little list of hits with sample offsets, and the render loop below
        //  stops at each of them.
        auto firePatternStep = [this, samplesPerStep] (int bank, int stepInPattern) noexcept
        {
            const std::uint64_t mask = patternBank[(size_t) bank][(size_t) stepInPattern].load (std::memory_order_relaxed);
            if (mask == 0) return;

            //  Swing: the odd sixteenths arrive late by a fraction of a step.
            //  The even ones never move - that is what keeps the bar where it
            //  was while the feel changes.
            const float sw = swing.load (std::memory_order_relaxed);
            const int lateBy = ((stepInPattern & 1) != 0)
                                 ? (int) ((double) (sw - 0.5f) * 2.0 * samplesPerStep * 0.5)
                                 : 0;

            for (int p = 0; p < kNumPads; ++p)
            {
                if ((mask & ((std::uint64_t) 1u << p)) == 0) continue;

                const int semis = (int) stepNote[(size_t) bank][(size_t) stepInPattern][(size_t) p].load (std::memory_order_relaxed);

                //  Zero means "never set", which is every pattern made before
                //  these existed - so zero reads as full and as a single hit.
                const int rawV = (int) stepVel [(size_t) bank][(size_t) stepInPattern][(size_t) p].load (std::memory_order_relaxed);
                const int rawR = (int) stepRoll[(size_t) bank][(size_t) stepInPattern][(size_t) p].load (std::memory_order_relaxed);
                const float vel  = rawV <= 0 ? 1.0f : juce::jlimit (0.02f, 1.0f, (float) rawV / 127.0f);
                const int   hits = rawR <= 0 ? 1    : juce::jlimit (1, 8, rawR);

                for (int h = 0; h < hits; ++h)
                {
                    if (numPending >= (int) pending.size()) break;
                    const int at = lateBy + (int) (samplesPerStep * (double) h / (double) hits);
                    pending[(size_t) numPending++] = { at, p, semis, vel };
                }
            }
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

        //  Anything already due speaks before a sample is rendered.
        auto fireDueHits = [this]() noexcept
        {
            for (int i = 0; i < numPending; )
            {
                if (pending[(size_t) i].countdown <= 0)
                {
                    const auto h = pending[(size_t) i];
                    pending[(size_t) i] = pending[(size_t) --numPending];
                    triggerPad (h.pad, h.semis, h.vel);
                }
                else ++i;
            }
        };

        auto nextHitIn = [this]() noexcept
        {
            int best = std::numeric_limits<int>::max();
            for (int i = 0; i < numPending; ++i)
                best = juce::jmin (best, pending[(size_t) i].countdown);
            return best;
        };

        fireDueHits();

        int offset    = startSample;
        int remaining = numSamples;
        while (remaining > 0)
        {
            //  Stop at whichever comes first: the next step boundary, or the
            //  next hit inside the step this one already queued.
            const double toBoundary = samplesPerStep - stepAccum;
            int seg = juce::jlimit (1, remaining, (int) std::ceil (toBoundary));
            seg = juce::jlimit (1, seg, nextHitIn());

            renderVoices (offset, seg);
            stepAccum += seg;
            offset    += seg;
            remaining -= seg;

            for (int i = 0; i < numPending; ++i)
                pending[(size_t) i].countdown -= seg;

            if (stepAccum >= samplesPerStep - 1.0e-9)
            {
                stepAccum -= samplesPerStep;
                fireStep();   // voices started here render from the next segment on
            }

            fireDueHits();
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

        // --- 1. FLT: el barrido, en las dos direcciones. -------------------
        //
        //  Ver setFltSweep. -1 cierra por arriba, +1 abre por abajo, y el
        //  centro no procesa: en la zona muerta la etapa se salta entera, que
        //  es lo unico que hace que "neutro" sea de verdad neutro y no un paso
        //  bajo a 20 kHz con su fase y su resonancia puestas encima.
        {
            const float swT  = juce::jlimit (-1.0f, 1.0f, fltSweep.load (std::memory_order_relaxed));
            const float resT = juce::jlimit (0.1f, 4.0f, fxReso.load (std::memory_order_relaxed));
            smSweep += kBlock * (swT - smSweep);
            smReso  += kBlock * (resT - smReso);

            //  Exponencial, no lineal: el oido oye octavas. Repartido lineal,
            //  la mitad del recorrido se gasta entre 10 y 20 kHz, donde no pasa
            //  nada, y todo lo que importa cae en el ultimo centimetro.
            constexpr float kDead = 0.03f;
            const float mag = std::abs (smSweep);
            const bool  swept = mag > kDead;
            float freq = 0.0f;
            auto  type = juce::dsp::StateVariableTPTFilterType::lowpass;

            if (swept)
            {
                const float t = (mag - kDead) / (1.0f - kDead);            // 0..1
                if (smSweep < 0.0f)
                {
                    //  Cerrando por arriba: de 20 kHz a 90 Hz.
                    type = juce::dsp::StateVariableTPTFilterType::lowpass;
                    freq = 20000.0f * std::pow (90.0f / 20000.0f, t);
                }
                else
                {
                    //  Abriendo por abajo: de 20 Hz a 6 kHz.
                    type = juce::dsp::StateVariableTPTFilterType::highpass;
                    freq = 20.0f * std::pow (6000.0f / 20.0f, t);
                }
            }

            //  EL BUS SE DEVUELVE SIEMPRE QUE ESTE VIVO, SE FILTRE O NO.
            //
            //  FLT es de los que RESTAN SECO - fxIsTone[0] - porque un filtro
            //  es un inserto y no un envio: lo que un pad manda a este bus deja
            //  de ir por el camino seco. Saltarse returnBus en la zona muerta
            //  dejaba entonces al pad SIN camino: el seco quitado y el bus sin
            //  devolver. Silencio total con el efecto encendido y el barrido en
            //  el centro, que es donde queda la mitad de las veces.
            //
            //  Asi que "swept" decide si se PROCESA y "fed" decide si se
            //  DEVUELVE. De paso desaparece el salto de nivel al cruzar el
            //  centro: el bus no se va, solo deja de filtrarse - y en el borde
            //  de la zona muerta el filtro ya esta en su extremo, donde es casi
            //  transparente.
            const bool fed = live (0);
            if (fed)
            {
                if (swept)
                {
                    //  Reset al entrar Y al cambiar de lado. Un paso bajo
                    //  cargado con energia grave que de pronto se declara paso
                    //  alto suelta su estado de golpe: un golpe seco justo al
                    //  cruzar el centro, que es por donde pasa el dedo cada vez
                    //  que vuelve.
                    if (! filterWasActive || fltWasHigh != (smSweep > 0.0f))
                        masterFilter.reset();
                    masterFilter.setType (type);
                    masterFilter.setCutoffFrequency (juce::jlimit (20.0f, nyq, freq));
                    masterFilter.setResonance (smReso);
                    auto b = blockFor (0);
                    juce::dsp::ProcessContextReplacing<float> ctx (b);
                    masterFilter.process (ctx);
                    fltWasHigh = (smSweep > 0.0f);
                }
                returnBus (0);
            }
            filterWasActive = fed && swept;
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

            //  ESTADO QUE SOBREVIVE A UN BUS MUERTO ES UN GOLPE ESPERANDO.
            //
            //  `drvLp` es el estado del paso bajo de salida y no se reiniciaba
            //  nunca: ni en prepareToPlay - que solo limpiaba smSend - ni al
            //  volver a encenderse. Un bus se declara muerto (busRinging
            //  falso), la etapa deja de correr, pasan diez segundos, se vuelve
            //  a mandar un pad: la primera muestra sale del valor que se quedo
            //  guardado, que es DC. Un escalon de continua entrando al master,
            //  o sea un golpe seco cada vez que se reactiva el efecto. Y
            //  sobrevivia a un cambio de ruta de audio, que es cuando mas se
            //  nota porque coincide con enchufar los cascos.
            //
            //  Se limpia AL ENTRAR y no al preparar, porque cambiar un envio
            //  no pasa por prepareToPlay.
            const bool drvNow = live (2);
            if (drvNow && ! drvWasActive) { drvLp[0] = drvLp[1] = 0.0f; }
            drvWasActive = drvNow;

            if (drvNow)
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
            //  Mismo agujero que DRV, y aqui peor: `crHold` es literalmente la
            //  muestra retenida, asi que al reactivarse el bus salia el ultimo
            //  valor cuantizado de hace diez segundos, mantenido hasta que la
            //  fase volviera a disparar - con crRate alto, cientos de muestras
            //  de continua seguidas.
            const bool crNow = live (4);
            if (crNow && ! crWasActive) { crHold[0] = crHold[1] = 0.0f; crPhase = 0.0f; }
            crWasActive = crNow;

            if (crNow)
            {
                const float bits   = juce::jlimit (1.0f, 16.0f, crBits.load (std::memory_order_relaxed));
                const float levels = juce::jmax (1.0f, std::pow (2.0f, bits) * 0.5f);
                const float step   = juce::jmax (1.0f, crRate.load (std::memory_order_relaxed));

                //  El canal por FUERA y la muestra por dentro. Estaba al reves,
                //  con un `w = (ch == 0) ? w0 : w1` que es una rama por muestra
                //  y por canal dentro del bucle mas caliente de la etapa: nada
                //  de eso se puede vectorizar, y el compilador no puede saber
                //  que los dos punteros no se solapan.
                //
                //  La fase es del EFECTO y no del canal, asi que se avanza una
                //  sola vez: se guarda al terminar el primer canal y los demas
                //  la reproducen desde el mismo sitio, que es lo que hacia el
                //  bucle anterior y lo que mantiene los dos canales retenidos
                //  a la vez - que es de donde sale el sonido de un crusher y
                //  no de dos.
                const float phase0 = crPhase;
                for (int ch = 0; ch < chans; ++ch)
                {
                    float* w    = fxBus[4].getWritePointer (ch, startSample);
                    float phase = phase0;
                    float hold  = crHold[ch];

                    for (int i = 0; i < numSamples; ++i)
                    {
                        phase += 1.0f;
                        if (phase >= step) { phase -= step; hold = std::round (w[i] * levels) / levels; }
                        w[i] = hold;
                    }

                    crHold[ch] = hold;
                    if (ch == 0) crPhase = phase;
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
            //  live(5) OR la energia interna de la FDN, y no dentro del if:
            //  poner al dia busRinging solo cuando ya se procesa es un candado
            //  - en cuanto el bus se declara muerto una vez, no vuelve a
            //  procesarse y no puede volver a declararse vivo. Con Freeverb no
            //  se notaba porque siempre sacaba algo en la primera muestra.
            if (live (5) || reverb.ringing())
            {
                reverb.setParameters (rvSize.load (std::memory_order_relaxed),
                                      rvDamp.load (std::memory_order_relaxed));
                reverb.process (fxBus[5], startSample, numSamples);
                returnBus (5);
                //  ...y la reverb manda sobre lo que returnBus acaba de
                //  deducir: la cola esta dentro de las lineas antes de estar en
                //  la salida. Ver Fdn::ringing.
                busRinging[5] = busRinging[5] || reverb.ringing();
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
        //  LA BARRERA NO ES EL LIMITADOR, Y ATARLAS FUE UN AGUJERO.
        //
        //  Esto eran dos decisiones metidas en un solo `if`: el limitador es
        //  de SONIDO y es una preferencia; el filtro de no-finitos es de
        //  INTEGRIDAD y no lo es. Con las dos juntas, `outCh` valia cero
        //  cuando el limitador estaba apagado - y Exporter.h lo apaga a
        //  proposito en el motor del rebote, para poder medir el pico de
        //  verdad antes de compensarlo. O sea que el unico camino en el que
        //  un NaN se ESCRIBE A DISCO era exactamente el que se quedaba sin
        //  guardia.
        //
        //  Y no se notaba mirando: `bufferPeak` devuelve NaN, `(peak > 1.0f)`
        //  con NaN es falso, asi que el rebote decidia que no habia que bajar
        //  nada y escribia el fichero entero envenenado sin decir una palabra.
        //  El banco tampoco lo veia, porque solo ejercia el motor VIVO, que
        //  siempre lleva el limitador puesto.
        constexpr float thresh = 0.944f;      // -0.5 dBFS
        const int  guardCh = juce::jmin (2, out.getNumChannels());
        const bool limit   = safetyLimiter.load (std::memory_order_relaxed);
        for (int ch = 0; ch < guardCh; ++ch)
        {
            float* w = out.getWritePointer (ch, startSample);
            for (int i = 0; i < numSamples; ++i)
            {
                const float v = w[i];

                //  AQUI NO SALE UN NaN, VENGA DE DONDE VENGA.
                //
                //  Un NaN no se atenua ni se satura: se propaga. Entra por una
                //  muestra y sale por el bus, por el delay - que se realimenta
                //  y ya no vuelve nunca -, por la reverb y por el master, y la
                //  app se queda MUDA hasta que se reinicia. Y entra facil: un
                //  WAV de coma flotante corrupto trae NaN dentro, y un valor
                //  de 1e30 se convierte en infinito en cuanto se multiplica
                //  por algo, y el saturador de un infinito da NaN.
                //
                //  Medido en Tests/StressTest: de ocho muestras hostiles,
                //  CUATRO apagaban la maquina entera. Este if cuesta una
                //  comparacion por muestra - dos por bloque estereo de 512 - y
                //  convierte "la app se queda muda" en "ese pad no suena".
                if (! std::isfinite (v)) { w[i] = 0.0f; continue; }

                if (limit && (v > thresh || v < -thresh))
                {
                    const float sign = (v < 0.0f) ? -1.0f : 1.0f;
                    const float over = (v * sign - thresh) / (1.0f - thresh);
                    w[i] = sign * (thresh + (1.0f - thresh) * fastTanh (over));
                }
            }
        }
    }

    // 5e. RESAMPLE. The master, after everything, which is the whole point:
    //     what lands on the pad is what you just heard - the effects, the
    //     master saturation, the level, all of it printed. Written before the
    //     probe click so a latency measurement never ends up inside a take.
    if (recording.load (std::memory_order_acquire)
        && recordFromMaster.load (std::memory_order_acquire)
        && out.getNumChannels() > 0)
    {
        const int cap = recordBuffer.getNumSamples();
        int rp = recordPos.load (std::memory_order_relaxed);
        const int n = juce::jmin (numSamples, cap - rp);
        if (n > 0)
        {
            //  A mono record buffer fed by a stereo master must print the
            //  AVERAGE of the two, not the left plus half the right: measured,
            //  that came back a ratio of 1.487 against what was heard, which
            //  is a resample that arrives louder than the thing it copied and
            //  clips a layer earlier every time round.
            if (recordBuffer.getNumChannels() == 1 && out.getNumChannels() > 1)
            {
                recordBuffer.copyFrom (0, rp, out.getReadPointer (0, startSample), n, 0.5f);
                recordBuffer.addFrom  (0, rp, out.getReadPointer (1, startSample), n, 0.5f);
            }
            else
            {
                const int chans = juce::jmin (recordBuffer.getNumChannels(), out.getNumChannels());
                for (int ch = 0; ch < chans; ++ch)
                    recordBuffer.copyFrom (ch, rp, out.getReadPointer (ch, startSample), n);
            }

            rp += n;
            recordPos.store (rp, std::memory_order_relaxed);
        }
        if (rp >= cap)
            recording.store (false, std::memory_order_release);
    }

    // 5c-bombeo. El sidechain del pad elegido, antes del ducking del sistema.
    //
    //  Va aqui y no en 5b porque tiene que agachar el MASTER entero, colas de
    //  delay y reverb incluidas - un bombeo que deja la reverb a tope no abre
    //  ningun hueco. Y va antes del saturador por la misma razon que el duck
    //  del sistema: bajar lo que ENTRA al limitador, no lo que sale.
    if (duckEnv > 0.0001f || duckPad.load (std::memory_order_relaxed) >= 0)
    {
        const float amt = juce::jlimit (0.0f, 1.0f, duckAmt.load (std::memory_order_relaxed));
        const float rel = juce::jmax (20.0f, duckRel.load (std::memory_order_relaxed));
        //  Recuperacion exponencial: es la forma de una envolvente de
        //  compresor y la que no deja escalon al volver.
        const float k = 1.0f - std::exp (-1000.0f / (rel * (float) juce::jmax (8000.0, systemSampleRate)));
        const int   outCh = juce::jmin (2, out.getNumChannels());

        float* w[2] = { nullptr, nullptr };
        for (int ch = 0; ch < outCh; ++ch) w[ch] = out.getWritePointer (ch, startSample);

        float env = duckEnv;
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = 1.0f - amt * env;
            for (int ch = 0; ch < outCh; ++ch) w[ch][i] *= g;
            env -= k * env;
        }
        duckEnv = (env < 1.0e-5f) ? 0.0f : env;
    }

    // 5c-duck. The master level, ramped.
    //
    //  DESPUES del saturador Y del remuestreo, no antes.
    //
    //  Estaba antes para que el limitador viera menos señal mientras se atenua.
    //  El precio era que el remuestreo, que captura el master en 5e, imprimia
    //  la atenuacion DENTRO de la toma: llega una notificacion mientras
    //  remuestreas y te llevas su bache de medio segundo grabado para siempre.
    //  Atenuar es monitorizacion y dura un segundo; la toma se queda.
    {
        const float target = masterTarget.load (std::memory_order_relaxed);

        if (target < 0.99999f || masterGain < 0.99999f)
        {
            //  A 12 ms time constant: settled in about forty milliseconds,
            //  which is fast enough to be under the chime it is making room
            //  for and a hundred times too slow to click. Measured: 0.271 of
            //  level at the bottom, back to 1.000, over 13 blocks of 128.
            const float k = 1.0f - std::exp (-1.0f / (0.012f * (float) juce::jmax (8000.0, systemSampleRate)));
            const int outCh = juce::jmin (2, out.getNumChannels());

            float* w[2] = { nullptr, nullptr };
            for (int ch = 0; ch < outCh; ++ch) w[ch] = out.getWritePointer (ch, startSample);

            float gain = masterGain;
            for (int i = 0; i < numSamples; ++i)
            {
                gain += (target - gain) * k;
                for (int ch = 0; ch < outCh; ++ch) w[ch][i] *= gain;
            }
            masterGain = gain;
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

    // 5c. Feed the scope ring (post-FX mono sum) for the LCD.
    //
    //     Two rings, because the screen wants two different things. The plain
    //     sample ring is 2048 frames - 43 ms, an instant - and that is all the
    //     old display ever needed.
    //
    //     The waveform silhouette wants nearly a second of signal on screen at
    //     once, which is 35000 frames. Copying THAT to the message thread every
    //     tick would be 140 KB a frame, and it is the largest recurring cost in
    //     the whole UI. So the audio thread decimates as it goes: a running
    //     min/max per column, one column emitted every scopeColLen frames. The
    //     UI copies 256 columns instead of 35000 samples, and re-bucketing
    //     columns into however many the screen is wide is exact - the min of
    //     mins is the min.
    {
        int wi = scopeWrite.load (std::memory_order_relaxed);
        const int outCh = out.getNumChannels();
        const float* l = out.getReadPointer (0, startSample);
        const float* r = (outCh > 1) ? out.getReadPointer (1, startSample) : l;

        int   ci = scopeColWrite.load (std::memory_order_relaxed);
        float mn = colMin, mx = colMax;
        int   ct = colCount;

        for (int i = 0; i < numSamples; ++i)
        {
            const float m = 0.5f * (l[i] + r[i]);
            scope[(size_t) wi] = m;
            wi = (wi + 1) & (kScopeSize - 1);

            mn = juce::jmin (mn, m);
            mx = juce::jmax (mx, m);

            if (++ct >= scopeColLen)
            {
                scopeColMin[(size_t) ci] = mn;
                scopeColMax[(size_t) ci] = mx;
                ci = (ci + 1) & (kScopeCols - 1);
                mn =  1.0e9f; mx = -1.0e9f; ct = 0;
            }
        }

        colMin = mn; colMax = mx; colCount = ct;
        scopeWrite.store (wi, std::memory_order_release);
        scopeColWrite.store (ci, std::memory_order_release);
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

//  A refused trigger used to be a trigger that never happened: push() has
//  always returned false when the queue cannot take it, and nobody has ever
//  looked at the answer. So the one failure the transport can actually have
//  was also the one it reported to nobody - the pad simply made no sound, and
//  the app looked broken with nothing in it to see.
//
//  Now a refusal falls back to a single atomic word (see fallbackTriggers).
//  It loses the velocity and the audition point, which is the right thing to
//  lose: a pad that speaks at its own level beats a pad that does not speak.
void AudioEngine::postNoteOn (int slot, float vel) noexcept
{
    Command c; c.type = Command::Type::NoteOn; c.slot = slot; c.velocity = vel;
    if (! commands.push (c))
        noteOnByLifeboat (slot);
}

void AudioEngine::postNoteOnFrom (int slot, float from01, float vel) noexcept
{
    Command c; c.type = Command::Type::NoteOn; c.slot = slot; c.velocity = vel;
    c.from01 = from01;
    if (! commands.push (c))
        noteOnByLifeboat (slot);
}

void AudioEngine::noteOnByLifeboat (int slot) noexcept
{
    if (slot < 0 || slot >= kNumPads)
        return;

    droppedCommands.fetch_add (1, std::memory_order_relaxed);
    fallbackTriggers.fetch_or ((std::uint64_t) 1u << slot, std::memory_order_release);
}

//  ENTRADA MIDI. Su propia cola, por el contrato de un solo productor - ver
//  el comentario de midiCommands. El bote salvavidas es el mismo: una nota que
//  no cabe suena igual, solo que al nivel del pad y sin dinamica, que es lo
//  correcto que perder.
void AudioEngine::postNoteOnFromMidi (int slot, float vel) noexcept
{
    Command c; c.type = Command::Type::NoteOn; c.slot = slot; c.velocity = vel;
    if (! midiCommands.push (c))
        noteOnByLifeboat (slot);
}

void AudioEngine::postNoteOffFromMidi (int slot) noexcept
{
    Command c; c.type = Command::Type::NoteOff; c.slot = slot;
    midiCommands.push (c);
}

void AudioEngine::postNoteOff (int slot) noexcept
{
    Command c; c.type = Command::Type::NoteOff; c.slot = slot;
    commands.push (c);
}

void AudioEngine::postPanic() noexcept
{
    Command c; c.type = Command::Type::Panic;
    if (! commands.push (c))
        droppedCommands.fetch_add (1, std::memory_order_relaxed);
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

//  The silhouette's columns, oldest first. Cosmetic like copyScope: a torn
//  column while the audio thread writes one is a pixel, not a fault.
int AudioEngine::copyScopeColumns (float* dstMin, float* dstMax, int n) noexcept
{
    n = juce::jlimit (0, kScopeCols, n);
    const int ci = scopeColWrite.load (std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
    {
        const int k = (ci - n + i) & (kScopeCols - 1);
        dstMin[i] = scopeColMin[(size_t) k];
        dstMax[i] = scopeColMax[(size_t) k];
    }
    return n;
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
    const std::uint64_t bit = (std::uint64_t) 1u << pad;
    std::uint64_t cur = patternBank[(size_t) patternIdx][(size_t) step].load (std::memory_order_relaxed);
    cur = on ? (cur | bit) : (cur & ~bit);
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

//  Velocity and roll, same shape as the note. Zero means "never set" in both,
//  which is what every pattern written before they existed says - and it has
//  to keep meaning full level and one hit, or old patterns would come back
//  silent or stuttering.
void AudioEngine::setStepVel (int patternIdx, int step, int pad, int vel) noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps || pad < 0 || pad >= kNumPads) return;
    stepVel[(size_t) patternIdx][(size_t) step][(size_t) pad].store ((std::uint8_t) juce::jlimit (1, 127, vel), std::memory_order_relaxed);
}

void AudioEngine::setStepRoll (int patternIdx, int step, int pad, int hits) noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps || pad < 0 || pad >= kNumPads) return;
    stepRoll[(size_t) patternIdx][(size_t) step][(size_t) pad].store ((std::uint8_t) juce::jlimit (1, 8, hits), std::memory_order_relaxed);
}

int AudioEngine::getStepVel (int patternIdx, int step, int pad) const noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps || pad < 0 || pad >= kNumPads) return 127;
    const int v = (int) stepVel[(size_t) patternIdx][(size_t) step][(size_t) pad].load (std::memory_order_relaxed);
    return v <= 0 ? 127 : v;
}

int AudioEngine::getStepRoll (int patternIdx, int step, int pad) const noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps || pad < 0 || pad >= kNumPads) return 1;
    const int r = (int) stepRoll[(size_t) patternIdx][(size_t) step][(size_t) pad].load (std::memory_order_relaxed);
    return r <= 0 ? 1 : r;
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

void AudioEngine::startRecording (int slot, bool fromMaster) noexcept
{
    if (slot < 0 || slot >= kNumPads) return;
    recordSlot = slot;
    recordPos.store (0, std::memory_order_relaxed);
    //  Set the SOURCE before arming, or a block that lands between the two
    //  records the wrong thing.
    recordFromMaster.store (fromMaster, std::memory_order_release);
    recording.store (true, std::memory_order_release);
}

SampleBuffer::Ptr AudioEngine::finishRecording() noexcept
{
    recording.store (false, std::memory_order_release);
    SampleBuffer::Ptr sb;
    const int len = recordPos.load (std::memory_order_acquire);
    if (len > 4)
    {
        //  Two channels that are bit-for-bit identical are one channel that
        //  the device duplicated, which is what a single microphone routed
        //  into a stereo stream looks like. Keeping both would double the
        //  file, the project and the memory to say the same thing twice.
        int chans = juce::jmax (1, recordBuffer.getNumChannels());

        if (chans == 2)
        {
            const float* l = recordBuffer.getReadPointer (0);
            const float* r = recordBuffer.getReadPointer (1);
            bool identical = true;

            for (int i = 0; i < len && identical; ++i)
                identical = (l[i] == r[i]);

            if (identical) chans = 1;
        }

        sb = new SampleBuffer();
        sb->buffer.setSize (chans, len);
        for (int ch = 0; ch < chans; ++ch)
            sb->buffer.copyFrom (ch, 0, recordBuffer, ch, 0, len);

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
    //  Y el filtro CON SU MASCARA, por lo mismo que los envios: el motor del
    //  rebote no pasa por setPadCutoff, se le copia el estado entero, y sin la
    //  mascara exportaria la cancion con los 64 pads sin filtrar.
    copyArr (padFadeIn,  s.padFadeIn);
    copyArr (padFadeOut, s.padFadeOut);
    copyArr (padCutoff,  s.padCutoff);
    copyArr (padReso,    s.padReso);
    padFiltMask.store (s.padFiltMask.load (std::memory_order_relaxed), std::memory_order_relaxed);
    copyArr (padMute,    s.padMute);
    copyArr (padSolo,    s.padSolo);
    for (size_t i = 0; i < padSend.size(); ++i) copyArr (padSend[i], s.padSend[i]);
    for (size_t i = 0; i < smSend.size();  ++i) smSend[i] = s.smSend[i];
    //  Y LA MASCARA CON ELLOS. El motor del rebote no pasa nunca por
    //  setPadSend - se le copia el estado entero de golpe - asi que sin esta
    //  linea arrancaba con la mascara a cero, se saltaba los 64 pads y
    //  exportaba la cancion sin un solo efecto. Todo dato que decida si algo
    //  se PROCESA tiene que viajar con el que dice cuanto.
    smSendHot = s.smSendHot;
    padSendMask.store (s.padSendMask.load (std::memory_order_relaxed), std::memory_order_relaxed);
    refreshSolo();

    bpm.store (s.bpm.load (std::memory_order_relaxed), std::memory_order_relaxed);
    stepBeats.store (s.stepBeats.load (std::memory_order_relaxed), std::memory_order_relaxed);
    editPattern.store (s.editPattern.load (std::memory_order_relaxed), std::memory_order_relaxed);
    copyArr (patternLength, s.patternLength);
    copyArr (chainSlots,    s.chainSlots);
    chainLength.store (s.chainLength.load (std::memory_order_relaxed), std::memory_order_relaxed);

    //  The three things a step SAYS, not just which steps exist.
    //
    //  Velocity, rolls and swing were never copied, so a bounce came out
    //  straight, at full level on every step and with every roll dropped -
    //  a different performance from the one you had been listening to. Same
    //  for AUTOCUT, which the clone's constructor defaults to true, so a pad
    //  deliberately left to stack was cut in the export and nowhere else.
    copyArr (padSelfCut, s.padSelfCut);
    swing.store (s.swing.load (std::memory_order_relaxed), std::memory_order_relaxed);

    for (size_t b = 0; b < patternBank.size(); ++b)
    {
        copyArr (patternBank[b], s.patternBank[b]);
        for (size_t st = 0; st < stepNote[b].size(); ++st)
        {
            copyArr (stepNote[b][st], s.stepNote[b][st]);
            copyArr (stepVel [b][st], s.stepVel [b][st]);
            copyArr (stepRoll[b][st], s.stepRoll[b][st]);
        }
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
                         //  fltSweep, o el rebote sale SIN filtro. Es la
                         //  tercera vez que un parametro nuevo se olvida aqui:
                         //  antes fueron los recortes y despues el swing, la
                         //  velocidad y los redobles, y las tres veces el
                         //  rebote fue una interpretacion distinta de la que
                         //  se estaba escuchando.
                         { &fltSweep, &s.fltSweep },
                         { &duckAmt,  &s.duckAmt  }, { &duckRel, &s.duckRel },
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
    smSweep   = fltSweep.load (std::memory_order_relaxed);
    duckPad.store (s.duckPad.load (std::memory_order_relaxed), std::memory_order_relaxed);
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
