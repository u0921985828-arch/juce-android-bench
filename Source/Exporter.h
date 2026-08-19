#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "SampleBuffer.h"
#include "Lang.h"
#include "Bitacora.h"

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
//
//  · Y SE ESCRIBE POR BLOQUES, sin la cancion entera en memoria.
//
//    La version anterior reservaba un AudioBuffer del largo COMPLETO del
//    rebote: 64 compases a 120 BPM son 128 s, o sea 49 MB de una sola pieza, y
//    la misma cancion con la rejilla en 1/8 y el tempo abajo pide 1.2 GB. Eso
//    no cerraba la app - medido: juce::AudioBuffer usa HeapBlock<char,true>,
//    que SI lanza, y el try/catch daba "Sin memoria para 130 s" en vez de
//    reventar - pero convertia en imposible lo que la maquina puede tocar. Un
//    telefono con la memoria justa no puede exportar su propia cancion.
//
//    Ahora se renderiza en trozos de 512 y cada trozo se escribe al vuelo, asi
//    que la memoria del rebote es constante -unos 4 KB- y no depende del largo.
//    Medido con la cancion mas larga que la app admite, 130 s: el proceso llega
//    a 32 MB de pico y el rebote sale entero con el monton limitado a 64 MB,
//    donde el codigo anterior contestaba que no habia memoria.
//
//    El precio es una pasada mas: el pico manda la ganancia y hay que conocerlo
//    ANTES de escribir la primera muestra, asi que se mide en una pasada aparte
//    y se vuelve a renderizar para escribir. Es determinista - un motor propio,
//    fuera de tiempo real - asi que las dos pasadas dan exactamente lo mismo:
//    el master salio con los mismos 1152104 bytes que antes, al byte.
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
              double sr,
              bool comprimido = false)
        : juce::Thread ("zati-export"),
          live (liveEngine), pads (std::move (samples)), padNames (std::move (names)),
          dir (std::move (destDir)), base (std::move (baseName)),
          stems (wantStems), sampleRate (sr > 0.0 ? sr : 44100.0), ogg (comprimido)
    {
        //  Dos: la que mide el pico y la que escribe el master. Contarla es lo
        //  honesto - la barra la recorre igual que las demas.
        totalPasses = 2;
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
            resultText = T ("Nada que exportar: no hay pasos con sonido");
            finished.store (true, std::memory_order_release);
            return;
        }

        //  LO QUE DURE UN PASO, preguntado al motor y no dado por hecho.
        //
        //  Estaba escrito 0.25 - semicorcheas - de cuando la rejilla no se
        //  podia cambiar. Con la rejilla en 1/8 un paso dura el DOBLE, asi que
        //  el fichero se reservaba con la mitad del largo y la exportacion
        //  cortaba la cancion por la mitad sin decir nada. Con 1/32 sobraba el
        //  doble de silencio al final.
        const double secPerStep = (60.0 / juce::jmax (20.0, live.getBpm()))
                                * (double) live.getStepBeats();
        const juce::int64 bodyLen = (juce::int64) (secPerStep * (double) steps * sampleRate);
        const juce::int64 tailLen = (juce::int64) (juce::jmax (2.0, live.getFxTailSeconds()) * sampleRate);
        const juce::int64 totalLen = bodyLen + tailLen;

        if (! dir.createDirectory())
        {
            resultOk = false;
            resultText = T ("No se pudo crear %1", dir.getFullPathName());
            finished.store (true, std::memory_order_release);
            return;
        }

        // --- Pass 1: measure. Nothing is written; the peak sets the gain. --
        Bitacora::paso ("exportar/medir");
        float peak = 0.0f;
        if (! renderPass (-1, totalLen, 1.0f, nullptr, &peak, 0)) return;   // cancelled

        const float gain = (peak > 1.0f) ? (0.999f / peak) : 1.0f;
        passDone.store (1, std::memory_order_relaxed);

        // --- Pass 2: the master, written as it renders. --------------------
        Bitacora::paso ("exportar/master");
        auto masterFile = uniqueFile (base + extension());
        if (! writeRender (masterFile, -1, totalLen, gain, 1))
        {
            if (threadShouldExit()) return;      // writeRender ya dejo el parte
            resultOk = false;
            resultText = T ("No se pudo escribir %1", masterFile.getFileName());
            finished.store (true, std::memory_order_release);
            return;
        }
        passDone.store (2, std::memory_order_relaxed);
        progress.store (2.0f / (float) totalPasses, std::memory_order_relaxed);

        int written = 1;
        int pasadas = 2;

        // --- Remaining passes: one stem per loaded pad. -------------------
        if (stems)
        {
            for (int p = 0; p < kNumPads && ! threadShouldExit(); ++p)
            {
                if (pads[(size_t) p] == nullptr) continue;

                auto label = padNames[(size_t) p].isNotEmpty()
                               ? sanitise (padNames[(size_t) p])
                               : juce::String ("pad");
                {
                    //  El numero de pad va en la miga: si la app se cierra en
                    //  una pista concreta, es la muestra de ESE pad la que hay
                    //  que mirar y no "la exportacion".
                    char m[32] = "exportar/pista ";
                    const int q = p + 1;
                    m[15] = (char) ('0' + (q / 10) % 10); m[16] = (char) ('0' + q % 10); m[17] = 0;
                    Bitacora::paso (m);
                }
                auto f = uniqueFile (base + "_" + juce::String (p + 1).paddedLeft ('0', 2)
                                          + "_" + label + extension());
                if (writeRender (f, p, totalLen, gain, pasadas))
                    ++written;
                else if (threadShouldExit())
                    return;

                ++pasadas;
                passDone.store (pasadas, std::memory_order_relaxed);
                progress.store ((float) pasadas / (float) totalPasses, std::memory_order_relaxed);
            }
        }

        if (threadShouldExit())
        {
            resultOk = false;
            resultText = T ("Cancelado");
            finished.store (true, std::memory_order_release);
            return;
        }

        const double secs = (double) totalLen / sampleRate;
        resultOk     = true;
        resultFolder = dir;
        //  El parte final, tambien traducido. Era la unica frase que la
        //  persona lee cuando la exportacion sale BIEN, y estaba en castellano
        //  en las cuatro compilaciones.
        resultText   = (written == 1 ? T ("%1 archivo, %2 s", juce::String (written), juce::String (secs, 1))
                                     : T ("%1 archivos, %2 s", juce::String (written), juce::String (secs, 1)))
                     + (gain < 1.0f
                          ? " " + T ("(bajado %1 dB para no saturar)",
                                     juce::String (-juce::Decibels::gainToDecibels (gain), 1))
                          : juce::String());
        Bitacora::paso ("exportar/hecho");
        progress.store (1.0f, std::memory_order_relaxed);
        finished.store (true, std::memory_order_release);
    }

private:
    //  Abre el fichero y renderiza DENTRO de el: una pasada, un WAV, memoria
    //  constante. Devuelve false si no se pudo escribir o si se cancelo - las
    //  dos se distinguen mirando threadShouldExit, que es lo que hace run().
    bool writeRender (const juce::File& f, int soloPad, juce::int64 totalLen,
                      float gain, int pasada)
    {
        f.deleteFile();
        auto stream = f.createOutputStream();
        if (stream == nullptr || ! stream->openedOk())
            return false;

        //  El escritor, segun el formato. Vorbis no tiene "bits por muestra":
        //  el 24 se ignora y lo que manda es el indice de calidad, y 5 de 10 es
        //  el que la propia libreria documenta como transparente.
        std::unique_ptr<juce::AudioFormat> fmt;
        if (ogg) fmt.reset (new juce::OggVorbisAudioFormat());
        else     fmt.reset (new juce::WavAudioFormat());

        std::unique_ptr<juce::AudioFormatWriter> writer (
            fmt->createWriterFor (stream.get(), sampleRate, 2, ogg ? 16 : 24, {}, ogg ? 5 : 0));
        if (writer == nullptr)
            return false;
        stream.release();   // the writer owns it now

        if (! renderPass (soloPad, totalLen, gain, writer.get(), nullptr, pasada))
        {
            //  Un rebote a medias no se queda en la carpeta pareciendo un
            //  fichero bueno: el escritor se cierra y el fichero se borra.
            writer.reset();
            f.deleteFile();
            return false;
        }
        return true;
    }

    // Renders the whole arrangement once. `soloPad` < 0 means the full mix;
    // otherwise only that pad sounds. Escribe en `writer` si lo hay y anota el
    // pico en `peakOut` si lo hay. Returns false if the job was cancelled.
    bool renderPass (int soloPad, juce::int64 totalLen, float gain,
                     juce::AudioFormatWriter* writer, float* peakOut, int pasada)
    {
        // On the heap: an engine carries the eight pattern banks and their
        // note grids, which is more than a worker thread's stack should hold.
        auto engine = std::make_unique<AudioEngine>();
        auto& off = *engine;

        off.setOffline (true);
        off.prepareToPlay (sampleRate, kBlock);

        //  PUBLISH FIRST, COPY SECOND.
        //
        //  publishSample resets the pad's trim window to the whole file - it
        //  has to, since a new sound in a pad cannot inherit the old one's
        //  in and out points. Done AFTER copyStateFrom it threw away every
        //  trim the copy had just brought over, so a bounce ignored START and
        //  END on every pad. On a chopped kit, where all sixteen pads share
        //  one buffer and differ ONLY by trim, the exported file was sixteen
        //  overlapping copies of the whole break.
        //
        //  Publishing before the copy costs nothing and lets the state have
        //  the last word, which is the order applyState already uses.
        for (int i = 0; i < kNumPads; ++i)
            if (pads[(size_t) i] != nullptr)
                off.publishSample (i, pads[(size_t) i]);

        off.copyStateFrom (live);
        //  Sin el limitador de seguridad: es de la escucha. Impreso aqui, el
        //  bufferPeak de abajo medía un pico que el limitador acababa de
        //  aplastar, nunca lo encontraba por encima de 1.0 y anunciaba que no
        //  habia hecho falta bajar nada - sobre una señal que ya venia
        //  saturada. Medir y compensar solo sirve si hay algo que medir.
        off.setSafetyLimiter (false);

        if (soloPad >= 0)
        {
            // A stem must contain its pad even if that pad is muted in the
            // live mix — you export stems precisely to rebalance later.
            off.clearSolo();
            for (int i = 0; i < kNumPads; ++i)
                off.setPadMute (i, i != soloPad);
        }

        off.setPlaying (true);

        //  EL UNICO buffer del rebote, y mide un bloque. Ver la cabecera: el
        //  que media la cancion entera es el que cerraba la app.
        juce::AudioBuffer<float> trozo (2, kBlock);

        for (juce::int64 pos = 0; pos < totalLen; pos += kBlock)
        {
            if (threadShouldExit())
            {
                resultOk = false;
                resultText = T ("Cancelado");
                finished.store (true, std::memory_order_release);
                return false;
            }

            const int n = (int) juce::jmin ((juce::int64) kBlock, totalLen - pos);
            off.renderNextBlock (trozo, 0, n);

            if (peakOut != nullptr)
                for (int ch = 0; ch < 2; ++ch)
                    *peakOut = juce::jmax (*peakOut, trozo.getMagnitude (ch, 0, n));

            if (writer != nullptr)
            {
                if (gain < 1.0f)
                    for (int ch = 0; ch < 2; ++ch)
                        trozo.applyGain (ch, 0, n, gain);

                if (! writer->writeFromAudioSampleBuffer (trozo, 0, n))
                {
                    off.setPlaying (false);
                    return false;              // disco lleno, o el fichero se fue
                }
            }

            // Freeing retired buffers here keeps the clone's queue from
            // filling on a long render; it is the message thread's job in the
            // live engine, and this background thread owns this clone.
            off.collectRetiredSamples();

            if ((pos & 0x3ffff) == 0)
            {
                const float within = (float) pos / (float) juce::jmax ((juce::int64) 1, totalLen);
                const float base01 = (float) pasada / (float) totalPasses;
                progress.store (base01 + within / (float) totalPasses, std::memory_order_relaxed);
            }
        }

        off.setPlaying (false);
        return true;
    }

    juce::String extension() const { return ogg ? ".ogg" : ".wav"; }

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
    //  OGG VORBIS Y NO MP3, y no por gusto: el codificador de MP3 es LAME, que
    //  no viene con JUCE y arrastra su propia licencia. Vorbis SI viene, es
    //  libre de patentes, comprime lo mismo y lo abre cualquier telefono - un
    //  master de tres minutos pasa de 30 MB a 3, que es lo que separa "lo
    //  tengo" de "te lo mando".
    bool         ogg = false;
    int          totalPasses = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Exporter)
};
