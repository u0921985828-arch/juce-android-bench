#pragma once

#include <JuceHeader.h>
#include <cmath>
#include "SampleBuffer.h"

// ============================================================================
//  Voice — one playing sample. Phase accumulator + Hermite interpolation,
//  with a playback window (trim start/end), optional reverse and loop, and a
//  short anti-click gain envelope. POD-ish, header-only.
// ============================================================================
struct Voice
{
    bool   active    = false;
    bool   releasing = false;
    bool   loop      = false;
    bool   reverse   = false;
    double pos       = 0.0;
    double delta     = 0.0;

    //  ANTI-ALIAS AL SUBIR EL TONO.
    //
    //  Leer mas rapido que la fuente (delta > 1) sube el espectro entero, y lo
    //  que pasa de Nyquist no se pierde: vuelve PLEGADO como parciales que no
    //  son armonicos de nada. En un chop de break eso es el silbido metalico
    //  que no estaba en el disco, y a +12 semitonos se lleva por delante todo
    //  lo que la fuente tenia por encima de 11 kHz.
    //
    //  Un polo, no un banco: el filtro va DESPUES de la interpolacion, en el
    //  camino de la voz, y tiene que costar dos multiplicaciones porque hay
    //  hasta cuarenta y ocho voces. Corta en Nyquist/delta, que es exactamente
    //  la frecuencia por encima de la cual el material se pliega. Con delta<=1
    //  el coeficiente es 1 y el filtro es la identidad: una voz que no sube de
    //  tono no paga nada y suena EXACTAMENTE igual que antes.
    float aaCoef = 1.0f;      // 1 = sin filtrar
    float aaL = 0.0f, aaR = 0.0f;

    //  Se recalcula en cada start(), junto a delta, porque depende de el.
    void updateAntiAlias() noexcept
    {
        if (delta <= 1.0)  { aaCoef = 1.0f; aaL = aaR = 0.0f; return; }
        //  fc normalizada = 0.5/delta. Coeficiente de un polo:
        //  a = 1 - exp(-2*pi*fc). Acotado para que a delta enormes siga
        //  dejando pasar algo en vez de cerrar del todo.
        const double fc = 0.5 / delta;
        aaCoef = (float) juce::jlimit (0.05, 1.0, 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * fc));
        aaL = aaR = 0.0f;
    }

    //  TAPE vs TONE. Tape is what a sampler does by nature: read faster and
    //  the sound goes up AND gets shorter, because pitch and time are the
    //  same knob. Tone keeps the length: the read head still travels at real
    //  speed, and the pitch comes from two overlapping grains resampled
    //  against it and crossfaded, so a vocal can go up a fifth without
    //  turning into a chipmunk in half the time.
    bool   pitchMode = false;   // false = tape (varispeed), true = pitch only
    double timeStep  = 0.0;     // source samples per output sample at unity pitch, signed
    double ratio     = 1.0;     // 2^(semitones/12)
    double gLen      = 2048.0;  // grain length, output samples
    double gPhase    = 0.0;
    double gOffA     = 0.0, gOffB = 0.0;
    int    slot      = -1;
    std::uint32_t serial = 0;   // when this voice was started; lowest = oldest

    //  How hard this note was struck, kept for the life of the voice. It has
    //  to live here rather than being folded into the start gain, because the
    //  mixer retargets a sounding voice whenever a fader moves - and that
    //  would otherwise reset every note to full strength mid-flight.
    float  velocity  = 1.0f;
    int    winStart  = 1;      // playback window [winStart, winEnd) in samples
    int    winEnd    = 2;

    //  EL FUNDIDO DE LOS BORDES DEL RECORTE, que no es la envolvente del pad.
    //
    //  ATAQUE y CAIDA son de la NOTA: cuentan desde que se golpea y desde que
    //  se suelta. Esto es del RECORTE: cuenta desde el borde de la ventana,
    //  este donde este dentro de la muestra. La diferencia se ve en un bucle,
    //  donde el ataque suena una vez y esto suena en cada vuelta, y en un
    //  troceado, donde lo que hay que suavizar es el sitio por el que se
    //  corto y no el momento en que se toco.
    //
    //  En MUESTRAS y no en fraccion del recorte: el chasquido de un corte dura
    //  lo que dura, y no mas porque el trozo sea largo. Cinco milisegundos
    //  quitan un corte en medio de un grave; en fraccion, esos mismos cinco
    //  milisegundos serian el 1% de un trozo y el 50% de otro.
    int    fadeInSamp  = 0;
    int    fadeOutSamp = 0;

    float  gain      = 0.0f;
    float  target    = 0.0f;
    float  stepUp    = 0.0f;
    float  stepDown  = 0.0f;
    float  stepCtl   = 0.0f;   // volume-cut rate: fixed ~10 ms, not the attack

    float  panL      = 0.7071f;   // equal-power pan gains, precomputed in start()
    float  panR      = 0.7071f;
    float  panTL     = 0.7071f;   // pan targets — retarget() moves these, render() slews
    float  panTR     = 0.7071f;

    //  padGain is the pad's level (volume knob, mute, solo); vel is how hard
    //  this particular note was struck. They were one number, which is why
    //  every hit came out the same: there was nowhere to put the difference.
    void start (int slotIndex, float semitones, float padGain,
                double fSrc, double fSys,
                int startSamp, int endSamp, bool loopOn, bool rev, int srcLen,
                float pan = 0.0f, float attackMs = 2.0f, float releaseMs = 3.0f,
                bool keepLength = false, float vel = 1.0f,
                float fadeInMs = 0.0f, float fadeOutMs = 0.0f) noexcept
    {
        slot     = slotIndex;
        winStart = juce::jlimit (1, juce::jmax (1, srcLen - 3), startSamp);
        winEnd   = juce::jlimit (winStart + 1, juce::jmax (winStart + 1, srcLen - 2), endSamp);
        loop     = loopOn;
        reverse  = rev;

        ratio    = std::pow (2.0, (double) semitones / 12.0);
        //  En MAGNITUD: quien decide el sentido es rev, no el signo de una
        //  frecuencia que ha salido de una cabecera. Con fSrc negativa la
        //  posicion caminaba HACIA ATRAS desde el principio de la ventana, y
        //  leer por debajo de winStart es leer fuera del buffer.
        timeStep = (rev ? -1.0 : 1.0) * (std::abs (fSrc) / juce::jmax (1.0, fSys));
        delta    = timeStep * ratio;

        //  UNA VOZ QUE NO AVANZA NO TERMINA NUNCA.
        //
        //  delta sale de la frecuencia que declara el fichero, y un fichero
        //  puede declarar cero - o algo que no es un numero -. Con delta = 0
        //  la posicion no se mueve, nunca llega al final de la ventana y la
        //  voz se queda sonando para siempre: ocupa su hueco, y a la octava
        //  vez el pad deja de responder. Con una frecuencia negativa sale un
        //  NaN que ademas envenena la salida.
        //
        //  Medido en Tests/StressTest: "frecuencia 0" dejaba una voz colgada
        //  y "frecuencia negativa" sacaba NaN. Ninguna de las dos puede pasar
        //  de aqui - y este es el sitio, porque es el unico por el que pasan
        //  TODOS los caminos: cargar, cortar, grabar y remuestrear.
        if (! std::isfinite (delta) || std::abs (delta) < 1.0e-9)
        {
            active = false;
            return;
        }
        //  delta puede ser negativa en reverso: lo que decide el plegado es su
        //  MAGNITUD, y updateAntiAlias mira delta directamente, asi que se le
        //  pasa ya en positivo por la unica via que hay - recalcular con el
        //  valor absoluto. Un pad al reves a +12 st se pliega igual que uno
        //  del derecho.
        { const double keep = delta; delta = std::abs (delta); updateAntiAlias(); delta = keep; }
        pos      = rev ? (double) (winEnd - 1) : (double) winStart;

        //  Los fundidos, en muestras de la FUENTE y no de la salida: se miden
        //  contra pos, que camina por el buffer de origen. A 44.1 kHz de fuente
        //  sonando en un aparato de 48, cinco milisegundos son 220 muestras de
        //  fuente y no 240 - y el borde que hay que suavizar esta en la fuente.
        //
        //  Y acotados a un tercio de la ventana cada uno: un fundido mas largo
        //  que el propio trozo no es un fundido, es un mando de volumen puesto
        //  al reves. Un tercio deja siempre un tercio de trozo a nivel pleno.
        const double fSrcAbs = std::abs (fSrc) > 1.0 ? std::abs (fSrc) : juce::jmax (1.0, fSys);
        const int    tercio  = juce::jmax (0, (winEnd - winStart) / 3);
        fadeInSamp  = juce::jlimit (0, tercio, (int) (fadeInMs  * 0.001 * fSrcAbs));
        fadeOutSamp = juce::jlimit (0, tercio, (int) (fadeOutMs * 0.001 * fSrcAbs));

        //  UN BUCLE SIN FUNDIDO CHASQUEA EN CADA VUELTA.
        //
        //  Al dar la vuelta, la senal salta del ultimo dato de la ventana al
        //  primero, y salvo que los dos valgan lo mismo -que no pasa nunca- eso
        //  es un escalon: un chasquido por vuelta, y a 120 BPM con un bucle de
        //  un compas son dos por segundo. Es el mismo problema que el corte de
        //  un trozo, que ya se arreglo con fundido, y por eso la solucion es la
        //  misma y no una nueva: si el pad esta en BUCLE y quien lo puso no
        //  eligio fundido, se le pone el minimo que quita el escalon.
        //
        //  Tres milisegundos, que es lo que dura medio ciclo de 160 Hz: por
        //  debajo de eso el fundido ya no tapa el salto de un grave, y por
        //  encima se empieza a oir que la vuelta "respira". Y sigue acotado al
        //  tercio, asi que un bucle de dos milisegundos no se convierte en un
        //  mando de volumen.
        if (loopOn && fadeInSamp == 0 && fadeOutSamp == 0)
        {
            const int minimo = juce::jlimit (0, tercio, (int) (0.003 * fSrcAbs));
            fadeInSamp = fadeOutSamp = minimo;
        }

        //  45 ms grains: long enough that the crossfade does not buzz at the
        //  grain rate, short enough that the smearing stays inside a drum hit.
        pitchMode = keepLength;
        gLen      = juce::jmax (64.0, 0.045 * fSys);
        gPhase    = 0.0;
        gOffA     = 0.0;
        gOffB     = 0.0;

        // Equal-power pan law: pan in [-1, 1], 0 = centre.
        const float panAngle = (juce::jlimit (-1.0f, 1.0f, pan) * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi;
        panL = panTL = std::cos (panAngle);
        panR = panTR = std::sin (panAngle);

        //  Floored rather than allowed to reach zero: the softest playable
        //  tap has to make a sound, or the pad reads as broken.
        velocity  = juce::jlimit (0.10f, 1.0f, vel);

        target    = padGain * velocity;
        gain      = 0.0f;
        releasing = false;
        const double fadeIn  = juce::jmax (1.0, 0.001 * (double) juce::jmax (0.1f, attackMs)  * fSys);
        const double fadeOut = juce::jmax (1.0, 0.001 * (double) juce::jmax (0.1f, releaseMs) * fSys);
        stepUp    = (float) (target / fadeIn);
        stepDown  = (float) (target / fadeOut);
        stepCtl   = (float) (1.0 / juce::jmax (1.0, 0.010 * fSys));
        gate      = -1;          // el que dispara la pone si el paso lleva largo
        panPropio = false;       // idem: solo si el paso trae bloqueo de pan
        active    = true;
    }

    //  EL LARGO DE LA NOTA. Ver AudioEngine::setStepLen.
    //
    //  Un pad es un disparo: suena hasta que se acaba la muestra. Eso vale para
    //  percusion y no vale para lo demas - un bajo, un pad de cuerda, una voz -
    //  donde el largo de la nota es la mitad de lo que se escribe. La cuenta
    //  atras se pone al disparar y suelta la nota cuando llega a cero; -1 es
    //  "sin largo", que es como nace un pad y como sonaba todo hasta ahora.
    int gate = -1;              // muestras hasta soltar, -1 = suelta sola

    //  EL PAN DE ESTA VOZ ES SUYO, y no del pad.
    //
    //  `retarget` vuelve a leer el pan del pad UNA VEZ POR BLOQUE, para que un
    //  fader de la mesa mueva lo que ya esta sonando. Con el bloqueo de pan
    //  del paso eso lo deshace en el bloque siguiente: la voz nace a la
    //  izquierda y a los 2.7 ms esta donde diga el pad. Medido asi la primera
    //  vez - el bloqueo "no hacia nada" y hacia 128 muestras de algo.
    bool panPropio = false;

    void release() noexcept { releasing = true; }
    void kill()    noexcept { active = false; releasing = false; gain = 0.0f; gate = -1; panPropio = false; }

    // Voice steal: fast fixed declick fade (~1.5 ms) regardless of the pad's
    // musical release — used when the same pad retriggers and this instance
    // must get out of the way without a click.
    void steal (double fSys) noexcept
    {
        if (! active) return;
        releasing = true;
        stepDown  = (float) (juce::jmax (gain, 0.05f) / juce::jmax (1.0, 0.0015 * fSys));
    }

    // Control-rate update (once per block, audio thread): a looping/long voice
    // keeps following its pad's VOLUME and PAN instead of freezing the values
    // captured at start(). Gain ramps in render(); pan slews there too.
    void retarget (float g, float pan) noexcept
    {
        if (! active || releasing) return;
        target = g * velocity;
        //  El volumen SI lo sigue: un bloqueo de pan no puede dejar la voz
        //  sorda a la mesa de mezclas, que es otra cosa.
        if (panPropio) return;
        const float panAngle = (juce::jlimit (-1.0f, 1.0f, pan) * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi;
        panTL = std::cos (panAngle);
        panTR = std::sin (panAngle);
    }

    void render (juce::AudioBuffer<float>& out, int start, int num,
                 const SampleBuffer* sb) noexcept
    {
        if (! active || sb == nullptr || num <= 0)
            return;

        //  La cuenta atras del largo. Se mira por bloque y no por muestra: el
        //  motor ya parte el bloque en los bordes de paso, asi que el error
        //  maximo es un trozo de paso, y una nota no se afina al milisegundo
        //  por su final. Cero reservas, cero ramas caras.
        if (gate >= 0)
        {
            gate -= num;
            if (gate <= 0) { gate = -1; release(); }
        }

        const int srcLen = sb->buffer.getNumSamples();
        const int srcCh  = sb->buffer.getNumChannels();
        if (srcLen < 4 || srcCh < 1) { active = false; return; }

        //  Clamp the window against THIS buffer once per block. The pad's
        //  sample can be swapped underneath a sounding voice, so the window
        //  captured at start() may no longer fit. Doing it here means the
        //  inner loop needs no per-sample safety test at all: inside
        //  [winStart, winEnd) the Hermite taps idx-1 .. idx+2 are provably in
        //  range, and that test used to run on every single sample.
        if (winStart < 1)          winStart = 1;
        if (winEnd   > srcLen - 2) winEnd   = srcLen - 2;
        if (winStart >= winEnd)    { active = false; return; }
        if (pos < (double) winStart)        pos = (double) winStart;
        if (pos > (double) (winEnd - 1))    pos = (double) (winEnd - 1);

        const float* srcL = sb->buffer.getReadPointer (0);
        const float* srcR = (srcCh > 1) ? sb->buffer.getReadPointer (1) : nullptr;
        const int    outCh = out.getNumChannels();
        const bool   stereoOut = outCh > 1;
        float* dstL = out.getWritePointer (0, start);
        float* dstR = stereoOut ? out.getWritePointer (1, start) : nullptr;

        //  Pan glides to its target across exactly one block. The old version
        //  used a fixed per-sample coefficient, which made the glide twice as
        //  slow at 96 kHz as at 48 — a control whose speed depended on the
        //  sound card.
        const float panIncL = (panTL - panL) / (float) num;
        const float panIncR = (panTR - panR) / (float) num;

        //  CUANTO DEJA PASAR EL BORDE, para la posicion en la que se esta.
        //
        //  Coseno alzado y no una rampa recta: una rampa recta tiene un codo en
        //  cada punta -la pendiente salta de cero a su valor de golpe- y ese
        //  codo es una discontinuidad de la DERIVADA, que se oye como un
        //  chasquido mas suave pero se oye. El coseno alzado entra y sale con
        //  pendiente cero por los dos lados, que es lo que hace que un corte
        //  suene a que la nota empieza y no a que alguien la enchufo.
        const bool hayFundido = (fadeInSamp > 0 || fadeOutSamp > 0);
        auto bordeGain = [this] (double p) noexcept -> float
        {
            float g = 1.0f;
            if (fadeInSamp > 0)
            {
                const double d = p - (double) winStart;
                if (d < (double) fadeInSamp)
                {
                    const float x = (float) juce::jlimit (0.0, 1.0, d / (double) fadeInSamp);
                    g *= 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * x);
                }
            }
            if (fadeOutSamp > 0)
            {
                const double d = (double) (winEnd - 1) - p;
                if (d < (double) fadeOutSamp)
                {
                    const float x = (float) juce::jlimit (0.0, 1.0, d / (double) fadeOutSamp);
                    g *= 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * x);
                }
            }
            return g;
        };

        //  Gain envelope and pan slew, identical in both modes.
        auto advanceEnvelope = [this] () noexcept -> bool
        {
            if (releasing)
            {
                gain -= stepDown;
                if (gain <= 0.0f) { active = false; gain = 0.0f; return false; }
            }
            else if (gain < target)
            {
                gain += stepUp;
                if (gain > target) gain = target;
            }
            else if (gain > target)
            {
                // A volume CUT is not a musical release: it used to fall
                // at the pad's attack rate, so a pad with a one-second
                // attack took a second to get quieter.
                gain -= stepCtl;
                if (gain < target) gain = target;
            }
            return true;
        };

        //  TONE mode: the position still walks at real speed, and the pitch
        //  comes from two grains reading against it at the pitch ratio, half a
        //  grain apart, under triangular windows that sum to exactly one. Each
        //  head is re-anchored to the position when its own window is at zero,
        //  so the splice happens where it cannot be heard.
        if (pitchMode && std::abs (ratio - 1.0) > 1.0e-9)
        {
            const double lo = (double) winStart, hi = (double) (winEnd - 1);
            const double drift    = timeStep * (ratio - 1.0);
            const double phaseInc = 1.0 / gLen;

            //  Which side of the playing position a grain reads from. Shifting
            //  UP the head runs ahead of the position, so it starts on it;
            //  shifting DOWN it runs behind, and starting on the position
            //  would send it looking for material BEFORE the sample begins -
            //  where it found the clamp instead and held one value for most of
            //  every grain. Starting a grain-span ahead and letting it fall
            //  back onto the position puts the ragged edge at the end of a
            //  sound instead of at its attack.
            const double gStart = (drift < 0.0) ? -drift * gLen : 0.0;
            if (gPhase == 0.0 && gOffA == 0.0 && gOffB == 0.0)
                gOffA = gOffB = gStart;

            //  Where exactly the incoming grain starts. Restarting it at the
            //  nominal offset splices two copies of the same sound at an
            //  arbitrary phase, and when that phase lands near opposite the
            //  crossfade CANCELS: a sine dropped 33 dB an octave down while
            //  the same shift upwards came through untouched, purely because
            //  of where the numbers happened to fall.
            //
            //  So the start is chosen rather than assumed. Around the nominal
            //  point, take the offset whose material correlates best with what
            //  the outgoing grain is about to play - which is a plain
            //  autocorrelation of the source at that moment. The two grains
            //  then add instead of fighting, and the crossfade stops being a
            //  gamble. (This is WSOLA; the search is ~120 candidates twice per
            //  grain, a few hundred thousand multiplies a second per voice.)
            auto alignedStart = [&] (double outgoingOff) noexcept -> double
            {
                constexpr int N = 192;      // correlation window
                constexpr int S = 240;      // search radius, samples
                const int ref = (int) (pos + outgoingOff);
                if (ref < winStart || ref + N >= winEnd) return gStart;

                const int base = (int) (pos + gStart);
                const float* r = srcL + ref;

                //  The correlation itself. Four independent accumulators
                //  rather than one: floating-point addition is not
                //  associative, so a single running sum is a dependency chain
                //  the compiler is not allowed to vectorise or pipeline. Four
                //  chains it can do both to, and over 192 samples the
                //  precision difference is far below anything an argmax cares
                //  about. Float, not double, for the same reason - twice the
                //  lanes per register, and we are comparing candidates, not
                //  measuring anything.
                auto correlate = [r] (const float* c) noexcept
                {
                    float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
                    for (int k = 0; k < N; k += 4)
                    {
                        a0 += r[k]     * c[k];
                        a1 += r[k + 1] * c[k + 1];
                        a2 += r[k + 2] * c[k + 2];
                        a3 += r[k + 3] * c[k + 3];
                    }
                    return (a0 + a1) + (a2 + a3);
                };

                const auto usable = [&] (int d) noexcept
                {
                    const int a = base + d;
                    return a >= winStart && a + N < winEnd;
                };

                //  ...and the search, coarse then fine. Sweeping all 481
                //  offsets two at a time was 241 correlations for a surface
                //  whose peak is a period wide - far broader than the step. A
                //  pass at sixteen finds which period we are on and a pass at
                //  one lands on it: forty correlations instead of two hundred
                //  and forty, and the same answer.
                float best = -1.0e30f;
                int   bestD = 0;

                for (int d = -S; d <= S; d += 16)
                {
                    if (! usable (d)) continue;
                    const float acc = correlate (srcL + base + d);
                    if (acc > best) { best = acc; bestD = d; }
                }

                const int lo2 = juce::jmax (-S, bestD - 8);
                const int hi2 = juce::jmin ( S, bestD + 8);

                for (int d = lo2; d <= hi2; ++d)
                {
                    if (d % 16 == 0 || ! usable (d)) continue;   // the coarse pass had these
                    const float acc = correlate (srcL + base + d);
                    if (acc > best) { best = acc; bestD = d; }
                }

                return gStart + (double) bestD;
            };

            for (int i = 0; i < num; ++i)
            {
                if (! advanceEnvelope()) break;
                panL += panIncL;
                panR += panIncR;

                const double pA = juce::jlimit (lo, hi, pos + gOffA);
                const double pB = juce::jlimit (lo, hi, pos + gOffB);
                const double phB = (gPhase < 0.5) ? gPhase + 0.5 : gPhase - 0.5;
                const float  wA = grainWindow (gPhase);
                const float  wB = grainWindow (phB);

                const int   ia = (int) pA; const float fa = (float) (pA - (double) ia);
                const int   ib = (int) pB; const float fb = (float) (pB - (double) ib);

                const float l = wA * hermite4 (fa, srcL, ia) + wB * hermite4 (fb, srcL, ib);
                const float ge = hayFundido ? gain * bordeGain (pos) : gain;
                dstL[i] += ge * panL * l;
                if (stereoOut)
                    dstR[i] += ge * panR * (srcR != nullptr
                                              ? wA * hermite4 (fa, srcR, ia) + wB * hermite4 (fb, srcR, ib)
                                              : l);

                pos   += timeStep;
                gOffA += drift;
                gOffB += drift;

                const double prevPhase = gPhase;
                gPhase += phaseInc;
                if (gPhase >= 1.0)                          { gPhase -= 1.0; gOffA = alignedStart (gOffB); }
                else if (gPhase >= 0.5 && prevPhase < 0.5)  { gOffB = alignedStart (gOffA); }

                if (reverse ? (pos < lo) : (pos > hi))
                {
                    if (! loop) { active = false; break; }
                    pos   = reverse ? hi : lo;
                    gOffA = gOffB = gStart;
                }
            }

            panL = juce::jlimit (0.0f, 1.0f, panL);
            panR = juce::jlimit (0.0f, 1.0f, panR);
            return;
        }

        const double step = reverse ? -delta : delta;   // always positive magnitude
        int i = 0;

        while (i < num && active)
        {
            // How far to the edge of the window, and therefore how many
            // samples can run before anything needs deciding again.
            const double dist = reverse ? (pos - (double) winStart)
                                        : ((double) (winEnd - 1) - pos);
            if (dist <= 0.0)
            {
                if (! loop) { active = false; break; }

                //  A LOOP NEEDS SOMEWHERE TO GO.
                //
                //  When the window is a single sample - winEnd == winStart + 1,
                //  which a four-frame file or a trim dragged shut both produce -
                //  dist is zero at the wrap point too, so resetting pos and
                //  continuing arrives back here with nothing changed. That is
                //  not a glitch, it is an infinite loop INSIDE THE AUDIO
                //  CALLBACK: the block never returns, the stream never feeds,
                //  and the app is silent and then killed for not responding.
                //
                //  Two samples is the floor for anything that can advance. Below
                //  it the voice stops, which is the honest answer to "loop this
                //  one sample forever".
                if (winEnd - winStart < 2) { active = false; break; }

                pos = reverse ? (double) (winEnd - 1) : (double) winStart;
                continue;
            }

            int run = (int) (dist / step) + 1;
            if (run > num - i) run = num - i;
            if (run < 1)       run = 1;

            for (int k = 0; k < run; ++k, ++i)
            {
                if (! advanceEnvelope()) break;

                panL += panIncL;
                panR += panIncR;

                const int   idx  = (int) pos;
                const float frac = (float) (pos - (double) idx);

                //  A mono sample is the normal case for a drum hit, and the
                //  old code ran the four-point interpolation twice over the
                //  identical data to fill two identical channels.
                float l = hermite4 (frac, srcL, idx);
                float r = (srcR != nullptr) ? hermite4 (frac, srcR, idx) : l;
                if (aaCoef < 1.0f)
                {
                    aaL += aaCoef * (l - aaL);   l = aaL;
                    aaR += aaCoef * (r - aaR);   r = aaR;
                }
                const float ge = hayFundido ? gain * bordeGain (pos) : gain;
                dstL[i] += ge * panL * l;
                if (stereoOut)
                    dstR[i] += ge * panR * r;

                pos += delta;
            }
        }

        panL = juce::jlimit (0.0f, 1.0f, panL);
        panR = juce::jlimit (0.0f, 1.0f, panR);
    }

private:
    //  Trapezoid, not triangle. Two heads reading the same source a fixed
    //  distance apart comb-filter each other wherever they overlap, and a
    //  triangular pair overlaps ALL the time - on a held note the two copies
    //  can land half a period out and cancel, which is heard as the pitch
    //  wandering rather than as a shift. This holds one head alone at full
    //  gain for most of its turn and crosses over in a tenth of a grain, so
    //  the interference exists only in that sliver. The pair still sums to
    //  exactly one everywhere.
    static constexpr double kXFade = 0.03;
    static inline float grainWindow (double p) noexcept
    {
        if (p < kXFade)         return (float) (p / kXFade);
        if (p < 0.5)            return 1.0f;
        if (p < 0.5 + kXFade)   return (float) (1.0 - (p - 0.5) / kXFade);
        return 0.0f;
    }

    static inline float hermite4 (float frac, const float* y, int idx) noexcept
    {
        const float ym1 = y[idx - 1];
        const float y0  = y[idx];
        const float y1  = y[idx + 1];
        const float y2  = y[idx + 2];
        const float c0 = y0;
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }
};
