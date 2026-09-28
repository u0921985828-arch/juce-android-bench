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

        //  LA GANANCIA QUE ESTA ZONA LLEVA YA HORNEADA. El generador escribe
        //  cada capa con su propia amplitud, asi que al saltar de capa la
        //  ganancia da un escalon que NO es el que la fuerza pedia: medido por
        //  `Tests/instr.py:128-137`, 2.7 dB en BAJOS y 5.5 dB en CUERDA PULS
        //  entre dos velocidades contiguas. La voz divide por este numero y el
        //  escalon desaparece sin una sola voz de mas. 1.0 = nada que deshacer,
        //  que es lo que vale toda muestra que no salga de `Sintes`.
        //
        //  NORMALIZADO A LA CAPA DE EN MEDIO, no a la fuerte: asi el centro del
        //  recorrido suena exactamente como sonaba y lo unico que se mueve son
        //  los extremos, que es lo que el escalon era. Lo escribe `sintetiza`
        //  con `Sintes::pesoDeCapa`, que es la misma cuenta que aplica la
        //  ganancia - dos sitios con la misma cuenta son dos reglas.
        //
        //  Y EL TIMBRE DE LA CAPA SE QUEDA. Lo que se deshace es el salto de
        //  VOLUMEN; el filtro mas abierto y los armonicos de mas siguen siendo
        //  la diferencia entre tocar flojo y tocar fuerte.
        float fuerza = 1.0f;
    };

    static constexpr int kMaxZonas = 16;
    std::array<Zona, kMaxZonas> zonas {};
    int                         nZonas = 0;

    // ------------------------------------------------------------------------
    //  COMO SE TOCA, no como se sintetizo.
    //
    //  Lo de arriba describe el MATERIAL: que muestra suena para que nota. Esto
    //  describe lo que la nota hace mientras la aguantas y al soltarla, que es
    //  justo lo que faltaba: la tabla escribe `rel` de 0.030 a 2.200 s -73.3:1-
    //  y los 384 presets sonaban a 0.180 s fijos y lineales; 129 de 384 a mas de
    //  un factor dos de lo que su fila pedia, 41 a mas de cuatro.
    //
    //  VIVE AQUI por la misma razon que `Zona`, escrita arriba: el objeto ya es
    //  contado, ya se publica por intercambio atomico y el hilo de audio ya
    //  sostiene su puntero mientras la voz dura.
    //
    //  Y LOS DEFECTOS SON LA IDENTIDAD, que no es una promesa sino la regla S0
    //  del banco: toda muestra que no sea de `Sintes` -la fabrica entera, todo
    //  WAV, toda toma- sale bit a bit igual porque cada campo de aqui, en su
    //  valor de fabrica, significa "lo de antes".
    struct Toque
    {
        float sosten  = 1.0f;   // 1.0 = mantener a tope         (= lo de antes)
        float caeEn   = 0.0f;   // s hasta el sosten; 0 = no cae (= lo de antes)
        float sueltaS = 0.0f;   // s; 0 = manda padRelease       (= lo de antes)
        float sens    = -1.0f;  // <0 = la ley de fuerza de hoy  (= lo de antes)
        float escala  = 0.0f;   // 0 = la suelta no depende de la nota
    };

    Toque toque {};

    //  LA LEY DE FUERZA, EN UN SITIO Y NO CLAVADA.
    //
    //  Estaba escrita dos veces dentro de `Voice::start` -`jlimit(0.10, 1.0,
    //  vel)` para el nivel y la misma para el brillo- y era la MISMA en los 384
    //  presets: un organo respondia al toque igual que un piano, y un organo no
    //  tiene con que responder. `sens` desclava eso: a 1 es la ley de siempre,
    //  a 0 la nota suena igual de fuerte la toques como la toques.
    //
    //  Y MENOS DE CERO SIGUE SIENDO LA LEY DE SIEMPRE, que es lo que vale para
    //  toda muestra que no salga de `Sintes`: el defecto de `Toque::sens`.
    //  Escrito con `>=` y no con `<` para que un NaN de un fichero a medio
    //  escribir caiga tambien en esa rama -comparar con NaN siempre es falso-.
    static float leyDeFuerza (float vel, float sens) noexcept
    {
        const float v = juce::jlimit (0.10f, 1.0f, vel);
        if (! (sens >= 0.0f)) return v;
        return 1.0f - juce::jlimit (0.0f, 1.0f, sens) * (1.0f - v);
    }

    //  Y de donde salio, para que vuelva del fichero de proyecto siendo un
    //  instrumento y no N copias de un WAV. Es la leccion del troceado: alli
    //  volvian los trozos y lo que no volvia era la RELACION.
    int familia = -1, preset = -1;
};

// ============================================================================
//  LOS DOS FUNDIDOS, Y COMO SE REPARTEN LA VENTANA. EN UN SOLO SITIO.
//
//  La regla estaba escrita DOS VECES -`Voice::start` y
//  `WaveformDisplay::setFades`- y decia: cada fundido, acotado a un tercio del
//  trozo. *Una regla duplicada que no se contrasta son dos reglas*, y ademas
//  esta era la equivocada: con la ficha TRIM de la captura -ventana de 2867
//  muestras a 44.1k, o sea 65 ms- el mando pedia **500 ms de SUAVE OUT y se
//  aplicaban 955 muestras, que son 21.7 ms**. Veintitres veces menos de lo que
//  ponia el mando, y el visor dibujaba lo mismo, asi que ni la pantalla lo
//  desmentia.
//
//  LO QUE SE HACE AHORA es lo que hace un sampler: los dos se reparten la
//  ventana, y si entre los dos se pasan **se escalan en proporcion hasta
//  juntarse en un punto** en vez de cortarse cada uno a un tercio. Asi un
//  fundido largo ACORTA el sonido -que es exactamente lo que se pidio: «cuando
//  pasa la barra del grafico deberia de acortarse con ese Fade el sonido»- en
//  lugar de quedarse en un tercio callado.
//
//  Y NO TIENE UNIDAD: los tres que la llaman traen la suya -la voz en muestras
//  de la fuente, el visor en fraccion de la muestra, la cara en milisegundos- y
//  lo unico que importa es que los tres numeros esten en la MISMA. Un tope con
//  unidad seria la cuarta copia de la regla.
namespace Fundido
{
    inline void reparte (double pedidoIn, double pedidoOut, double ventana,
                         double& entra, double& sale) noexcept
    {
        entra = juce::jmax (0.0, pedidoIn);
        sale  = juce::jmax (0.0, pedidoOut);

        const double v = juce::jmax (0.0, ventana);
        const double suma = entra + sale;

        if (suma > v)
        {
            //  Se juntan en un punto, y cada uno conserva SU PARTE de lo
            //  pedido: pedir 400 y 100 sobre una ventana de 250 da 200 y 50, y
            //  no 125 y 125. El que mas pidio sigue siendo el que mas manda.
            const double k = (suma > 0.0) ? v / suma : 0.0;
            entra *= k;
            sale  *= k;
        }
    }
}

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
