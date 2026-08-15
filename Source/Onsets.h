#pragma once

#include <JuceHeader.h>
#include <vector>
#include <cmath>

// ============================================================================
//  Onsets - donde EMPIEZA cada golpe de una muestra.
//
//  Cortar en trozos iguales es aritmetica, no troceado. Un break de bateria no
//  tiene los golpes repartidos a intervalos regulares -por eso suena a alguien
//  tocando y no a una caja de ritmos- asi que dividir por dieciseis deja la
//  mitad de los cortes en medio de un golpe y la otra mitad en el silencio de
//  antes. Lo que se oye es el ataque decapitado de cada trozo y un chasquido en
//  el que le sigue. Eso es lo que separa un troceador de juguete de uno de
//  verdad, y esta app tenia el primero.
//
//  El detector es FLUJO ESPECTRAL, que es lo que usan los de verdad, y no
//  energia: un charles encima de un bombo que ya suena no sube la energia
//  -puede bajarla- pero SI cambia el reparto por frecuencias. Medir solo el
//  nivel se pierde todos los golpes que caen sobre una cola.
//
//    * Ventana de 1024 con salto de 256: a 48 kHz son 5.3 ms de resolucion,
//      que es mas fino que lo que un dedo puede colocar y mas grueso que el
//      periodo de un grave, que es lo que evita que un bombo cuente como
//      cuatro golpes.
//    * Solo la diferencia POSITIVA de cada bin. Lo que baja es una cola que se
//      apaga, y sumarlo en valor absoluto convierte cada final de nota en un
//      comienzo.
//    * Umbral ADAPTATIVO por mediana movil, no fijo. Un fijo funciona en la
//      muestra con la que se afina y falla en la siguiente, porque el nivel
//      absoluto de un fichero no dice nada: lo que importa es cuanto destaca un
//      golpe sobre lo que hay a su alrededor. La mediana -y no la media- porque
//      la media la levantan los propios golpes que se buscan.
//    * Y una distancia minima entre golpes, porque un ataque de bateria dura
//      varios saltos y sin ella cada golpe sale tres veces.
//
//  Y EL CORTE SE RETRASA HASTA EL CRUCE POR CERO, hacia atras. El flujo dice en
//  que ventana empezo el golpe, no en que muestra; cortar en la muestra del
//  salto deja un escalon de continua al principio del trozo, que es un clic en
//  cada disparo - justo lo que el troceado por golpes venia a arreglar.
// ============================================================================
namespace Onsets
{
    struct Params
    {
        int   fft        = 1024;
        int   hop        = 256;
        float threshold  = 1.5f;     // veces la mediana local
        float minGapMs   = 45.0f;    // dos golpes mas juntos que esto son uno
        int   medianWin  = 17;       // saltos a cada lado para la mediana
    };

    //  Devuelve las posiciones de comienzo, en muestras, ordenadas. La primera
    //  es siempre 0: el primer trozo empieza donde empieza la muestra, aunque
    //  el primer golpe llegue mas tarde - lo de delante tiene que ir a algun
    //  sitio, y tirarlo seria decidir por la persona.
    inline std::vector<int> detect (const juce::AudioBuffer<float>& buf,
                                    double sampleRate,
                                    Params p = {})
    {
        std::vector<int> out;
        const int len = buf.getNumSamples();
        if (len < p.fft * 2 || sampleRate <= 0.0) { out.push_back (0); return out; }

        const int order = (int) std::round (std::log2 ((double) p.fft));
        juce::dsp::FFT fft (order);
        const int n = 1 << order;
        const int half = n / 2;

        std::vector<float> win ((size_t) n);
        for (int i = 0; i < n; ++i)
            win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi
                                                      * (float) i / (float) (n - 1));

        const int frames = 1 + (len - n) / p.hop;
        if (frames < 4) { out.push_back (0); return out; }

        std::vector<float> flux ((size_t) frames, 0.0f);
        //  El nivel de cada ventana, aparte del flujo. Ver la puerta de abajo.
        std::vector<float> nivel ((size_t) frames, 0.0f);
        std::vector<float> prev ((size_t) half, 0.0f);
        std::vector<float> mag  ((size_t) half, 0.0f);
        std::vector<float> fd   ((size_t) (2 * n), 0.0f);

        const float* src = buf.getReadPointer (0);
        const bool stereo = buf.getNumChannels() > 1;
        const float* src2 = stereo ? buf.getReadPointer (1) : nullptr;

        for (int f = 0; f < frames; ++f)
        {
            const int off = f * p.hop;
            std::fill (fd.begin(), fd.end(), 0.0f);
            for (int i = 0; i < n; ++i)
            {
                const float s = stereo ? 0.5f * (src[off + i] + src2[off + i]) : src[off + i];
                fd[(size_t) i] = s * win[(size_t) i];
            }
            fft.performFrequencyOnlyForwardTransform (fd.data());

            float acc = 0.0f;
            for (int k = 0; k < half; ++k)
            {
                //  En raiz y no lineal: la magnitud lineal deja el flujo entero
                //  en manos de los graves, que llevan casi toda la energia de un
                //  break, y un charles no mueve la suma ni un uno por ciento.
                mag[(size_t) k] = std::sqrt (fd[(size_t) k]);
                const float d = mag[(size_t) k] - prev[(size_t) k];
                if (d > 0.0f) acc += d;
            }
            std::swap (mag, prev);
            flux[(size_t) f] = acc;

            double e = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const float sm = stereo ? 0.5f * (src[off + i] + src2[off + i]) : src[off + i];
                e += (double) sm * sm;
            }
            nivel[(size_t) f] = (float) std::sqrt (e / (double) n);
        }

        //  Umbral adaptativo: la mediana de la ventana movil por el factor. Se
        //  copia el trozo y se usa nth_element, que es seleccion parcial - una
        //  ordenacion entera por cada salto seria mover el detector de lineal a
        //  cuadratico sin ganar un decimal.
        std::vector<float> pico;
        pico.reserve ((size_t) frames);
        std::vector<float> ventana;
        ventana.reserve ((size_t) (2 * p.medianWin + 1));

        float mayor = 0.0f;
        for (float v : flux) mayor = juce::jmax (mayor, v);
        if (mayor <= 1.0e-9f) { out.push_back (0); return out; }

        const int minGap = juce::jmax (1, (int) (sampleRate * (double) p.minGapMs / 1000.0));
        int ultimo = -minGap * 2;

        for (int f = 1; f < frames - 1; ++f)
        {
            //  Un maximo local, primero: sin esto el detector marca todos los
            //  saltos de la subida de un golpe y no su comienzo.
            if (flux[(size_t) f] < flux[(size_t) (f - 1)] || flux[(size_t) f] < flux[(size_t) (f + 1)])
                continue;

            ventana.clear();
            for (int j = juce::jmax (0, f - p.medianWin); j < juce::jmin (frames, f + p.medianWin + 1); ++j)
                ventana.push_back (flux[(size_t) j]);
            auto mid = ventana.begin() + (long) ventana.size() / 2;
            std::nth_element (ventana.begin(), mid, ventana.end());
            const float med = *mid;

            //  Y un suelo absoluto contra el maximo del fichero: en un pasaje
            //  callado la mediana local vale casi cero y CUALQUIER cosa la
            //  supera por 1.6 veces, asi que el siseo entre golpes salia como
            //  una fila de golpes.
            if (flux[(size_t) f] < med * p.threshold) continue;
            if (flux[(size_t) f] < mayor * 0.04f)     continue;

            //  Y LA PUERTA QUE FALTABA: un golpe SUBE el nivel.
            //
            //  Un tono grave que decae produce flujo positivo periodico y de
            //  verdad, y no porque cambie: 55 Hz a 48 kHz con ventana de 1024
            //  cae ENTRE dos bins - el paso es 46.9 Hz - asi que su fuga
            //  espectral se reparte distinto segun la fase con la que la ventana
            //  lo pilla, y el reparto oscila con el periodo del tono. Dieciocho
            //  milisegundos de periodo contra cinco de salto: el flujo sube y
            //  baja para siempre. Medido: un bombo suelto de 950 ms salia con
            //  DIECISEIS golpes, uno cada 59 ms, que es la distancia minima.
            //
            //  Ningun umbral sobre el flujo arregla eso, porque el flujo es
            //  real. Lo que distingue el rizado de un golpe es que dentro de una
            //  cola el NIVEL solo baja. Se compara con dos saltos antes -no con
            //  el anterior, que dentro del ataque ya subio- y se pide un 5%.
            //
            //  Y no sustituye al flujo, se suma a el: una puerta solo de nivel
            //  es un detector por energia, que es justo lo que se pierde un
            //  charles encima de un bombo que ya suena.
            if (f >= 2 && nivel[(size_t) f] < nivel[(size_t) (f - 2)] * 1.05f) continue;

            //  El centro de la ventana, que es donde el flujo tiene su maximo:
            //  la ventana esta enventanada en Hann, asi que un golpe pesa mas
            //  cuando cae en el medio que cuando acaba de entrar por el borde.
            //  Usar el PRINCIPIO de la ventana -que suena razonable- pone el
            //  corte hasta 21 ms ANTES del golpe: medido, 1 de 16 dentro de
            //  tolerancia y 14 inventados.
            int pos = f * p.hop + p.fft / 2;
            if (pos - ultimo < minGap) continue;
            ultimo = pos;
            pico.push_back ((float) pos);
        }

        //  Y EL CORTE AL PIE DEL ATAQUE, que no es lo mismo que el instante
        //  que dio el flujo.
        //
        //  El flujo dice en que ventana esta el golpe, con 5.3 ms de paso y una
        //  ventana de 21 ms: no puede decir la muestra. Cortar en su respuesta
        //  deja el corte con el golpe ya empezado - medido, 0.29 de amplitud en
        //  el punto de corte, o sea un escalon y un clic en cada disparo, que es
        //  justo lo que el troceado por golpes venia a arreglar.
        //
        //  Asi que se afina sobre la senal: en los 20 ms anteriores se busca el
        //  PIE, o sea la ultima muestra antes del golpe en la que la envolvente
        //  todavia estaba por debajo del 10% del pico de esa ventana. La ultima
        //  y no la mas callada: la mas callada puede estar en el otro extremo de
        //  la ventana, veinte milisegundos antes, y ahi ya no es este golpe.
        //  Doce y no veinte: con veinte el pie de un golpe que venia de una
        //  cola larga se iba mas de 15 ms hacia atras, y un corte que se va
        //  del golpe deja de ser ese golpe - salia como fallo Y como invento
        //  a la vez. Doce cubren medio periodo de 42 Hz.
        const int atras = juce::jmax (16, (int) (sampleRate * 0.012));
        constexpr int suave = 16;      // muestras de la envolvente

        out.push_back (0);
        for (float fp : pico)
        {
            const int pos0 = juce::jlimit (0, len - 1, (int) fp);
            const int tope = juce::jmax (0, pos0 - atras);

            float cima = 0.0f;
            for (int i = tope; i <= pos0; ++i) cima = juce::jmax (cima, std::abs (src[i]));
            const float liston = cima * 0.10f;

            auto env = [src, len] (int i) noexcept
            {
                float m = 0.0f;
                for (int j = i; j < juce::jmin (len, i + suave); ++j) m = juce::jmax (m, std::abs (src[j]));
                return m;
            };

            int pos = pos0;
            float mejor = env (pos0);
            for (int i = pos0; i > tope; --i)
            {
                const float e = env (i);
                if (e <= liston) { pos = i; break; }      // el pie, y se para aqui
                if (e < mejor)   { mejor = e; pos = i; }  // por si nunca baja del liston
            }

            if (pos > 0 && pos != out.back()) out.push_back (pos);
        }

        return out;
    }
}
