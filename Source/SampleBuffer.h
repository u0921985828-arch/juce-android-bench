#pragma once

#include <JuceHeader.h>

// ============================================================================
//  SampleBuffer — the ref-counted PCM payload handed across threads.
//
//  Threading contract:
//    * Created & filled on a BACKGROUND thread (SampleLoader).
//    * Published to the AUDIO thread via an atomic raw-pointer swap
//      (see AudioEngine). The audio thread only ever *reads* buffer data and
//      performs atomic ref-count *increments* — never a decrement, so it can
//      never trigger a delete.
//    * Deleted on the MESSAGE thread (AudioEngine::collectRetiredSamples()).
//
//  sourceSampleRate is F_src — the rate the file was recorded at. The Voice
//  uses it against the device rate F_sys to compute the playback increment.
// ============================================================================
class SampleBuffer : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<SampleBuffer>;

    juce::AudioBuffer<float> buffer;        // decoded PCM
    double                   sourceSampleRate = 44100.0;   // F_src

    // ------------------------------------------------------------------------
    //  EL MAPA DE ZONAS DE UN INSTRUMENTO. Ver Sintes.h.
    //
    //  Un instrumento no es una muestra que se pitchea: son varias muestras -
    //  una raiz por octava, dos capas de fuerza - dentro de este mismo buffer,
    //  y al disparar se elige la que menos hay que estirar.
    //
    //  VIVE AQUI Y NO EN UNA TABLA POR PAD porque este objeto ya es contado, ya
    //  se publica al hilo de audio por intercambio atomico y el hilo de audio ya
    //  sostiene su puntero mientras la voz dura. Una tabla paralela seria un
    //  segundo sitio con la misma informacion, y la forma conocida de que los
    //  dos se separen.
    //
    //  ARRAY FIJO Y NO std::vector, a proposito: son 384 bytes por muestra
    //  contra la posibilidad de que el hilo de audio lea un puntero interno
    //  reasignado. Y nZonas == 0 significa "muestra normal", con lo que todo lo
    //  anterior - incluido cualquier proyecto ya guardado - sigue exactamente
    //  igual.
    //
    //  INMUTABLE una vez publicado: lo rellena el hilo que sintetiza, antes del
    //  intercambio, y desde ahi solo se lee.
    struct Zona
    {
        int raiz     = 0;    // semitonos sobre la raiz nominal del instrumento
        int capa     = 0;    // 0 suave, 1 fuerte
        int ini      = 0;    // ventana dentro de `buffer`
        int fin      = 0;
        int bucleIni = 0;    // bucleFin <= bucleIni  =>  esta zona no da vueltas
        int bucleFin = 0;
    };

    static constexpr int kMaxZonas = 16;
    std::array<Zona, kMaxZonas> zonas {};
    int                         nZonas = 0;

    //  Y de donde salio, para que vuelva del fichero de proyecto siendo un
    //  instrumento y no N copias de un WAV. Es la leccion del troceado: alli
    //  volvian los trozos y lo que no volvia era la RELACION.
    int familia = -1, preset = -1;
};
