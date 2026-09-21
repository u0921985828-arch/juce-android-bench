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

// ============================================================================
//  LA ENVOLVENTE DE UNA MUESTRA, EN UN SOLO SITIO.
//
//  La dibujaban DOS y ahora la piden TRES: el visor grande de la ficha
//  (`WaveformDisplay::computeMinMax`), la tapa del pad (`PadButton::buildSpark`,
//  44 columnas para 40 px de alto) y el bloque de clip de la linea de tiempo,
//  que es el que la persona pidio - «que las tomas que se graben se vea el
//  audio facil». Un tercer DIBUJANTE no entra: `ChopPreview` se retiro por eso
//  y `Tests/plano.py` lo vigila. Lo que se comparte es la CUENTA, que es lo que
//  las tres hacian igual con tres bucles distintos.
//
//  Y devuelve el par min/max por columna y no una curva: una onda resumida a la
//  media se lee plana -el valor medio de un golpe es casi cero- y era el fallo
//  que la primera version del visor tuvo.
namespace Onda
{
    //  `d` es el canal 0 con `largo` muestras. Se resume `[desde, desde+tramo)`
    //  en `columnas` pares. El tramo puede pasarse del final -el zoom del visor
    //  lo hace- y cada columna se acota por su cuenta.
    //
    //  `norm` multiplica lo que sale: la tapa del pad normaliza cada trozo a su
    //  propio pico -si no, la cola de un break se dibuja como una raya al lado
    //  del transitorio- y los otros dos dibujan lo que hay.
    inline void envolvente (const float* d, int largo,
                            juce::int64 desde, juce::int64 tramo, int columnas,
                            juce::Array<float>& mins, juce::Array<float>& maxs,
                            float norm = 1.0f)
    {
        mins.clearQuick(); maxs.clearQuick();
        if (d == nullptr || largo < 1 || columnas < 1) return;

        for (int x = 0; x < columnas; ++x)
        {
            //  Y LA COLUMNA NUNCA SE QUEDA VACIA. Con el tramo mas corto que
            //  las columnas -un clip de un paso en 1/64, o un zoom a tope- dos
            //  columnas seguidas caen en la misma muestra y `e <= a`; el bucle
            //  no se ejecutaria y el centinela saldria tal cual, o sea una
            //  columna de +1 a -1: la onda dibujada como un bloque macizo.
            int a = (int) juce::jlimit ((juce::int64) 0, (juce::int64) (largo - 1),
                                        desde + (juce::int64) x * tramo / columnas);
            int e = (int) juce::jlimit ((juce::int64) 1, (juce::int64) largo,
                                        desde + (juce::int64) (x + 1) * tramo / columnas);
            if (e <= a) e = a + 1;
            if (e > largo) e = largo;

            //  Centinelas al reves y no ceros: con `mn = mx = 0` una señal con
            //  desplazamiento de continua -toda positiva- se dibujaria bajando
            //  hasta cero, que es media onda inventada.
            float mn = 1.0f, mx = -1.0f;
            for (int i = a; i < e; ++i) { const float s = d[i]; mn = juce::jmin (mn, s); mx = juce::jmax (mx, s); }
            mins.add (juce::jlimit (-1.0f, 1.0f, mn * norm));
            maxs.add (juce::jlimit (-1.0f, 1.0f, mx * norm));
        }
    }
}
