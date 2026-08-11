#pragma once

#include <JuceHeader.h>
#include <vector>
#include <algorithm>

// ============================================================================
//  QUITAR RUIDO — resta espectral con perfil sacado de la propia muestra.
//
//  Lo que hay que quitar de una grabacion hecha con el telefono no es un
//  chasquido ni un golpe de mesa: es el SUELO. El siseo del preamplificador,
//  el aire acondicionado, el zumbido de la red y sus armonicos. Todo eso es
//  estacionario - esta ahi antes de que empiece el sonido, sigue durante, y
//  sigue despues -, y por eso no se puede quitar con una puerta de ruido: una
//  puerta solo lo esconde en los silencios y lo deja intacto justo donde el
//  sonido esta sonando.
//
//  La resta espectral si puede: se estima cuanta energia tiene el ruido EN
//  CADA BANDA, y se le resta esa cantidad a cada banda de cada instante. Donde
//  el sonido es mas fuerte que el ruido, queda el sonido; donde no, queda el
//  suelo que se elija.
//
//  EL PERFIL SALE DE LA MUESTRA, no de un trozo que haya que marcar a mano.
//  Por banda, se toma el PERCENTIL 20 de la magnitud a lo largo de todas las
//  ventanas: en una banda donde solo hay ruido, el percentil 20 es el ruido; en
//  una donde hay una nota, la nota ocupa una minoria de las ventanas y el
//  percentil 20 sigue siendo el ruido. La media no sirve - una nota larga la
//  sube y entonces la resta se come la nota - y el minimo tampoco - un solo
//  fotograma raro lo hunde y no se quita nada.
//
//  LA SUAVIZACION NO ES ADORNO. Restar banda a banda deja "ruido musical":
//  bandas sueltas que sobreviven un fotograma y desaparecen al siguiente, y que
//  se oyen como campanitas. Se suaviza la GANANCIA en frecuencia (tres bandas)
//  y en el tiempo (ataque rapido, caida lenta), que es lo que convierte una
//  resta cruda en algo que se puede poner en un disco.
//
//  Fuera del hilo de audio, siempre: reserva memoria y recorre el fichero
//  entero. Se llama desde el hilo de mensajes con la muestra parada.
// ============================================================================
namespace Denoise
{
    //  fuerza 0..1. 0 es una limpieza suave que no se nota; 1 es agresiva y
    //  deja el suelo 40 dB por debajo, a costa de comerse las colas mas
    //  flojas. El valor de la app es 0.6.
    inline void process (juce::AudioBuffer<float>& buf, float strength01)
    {
        const int numCh = buf.getNumChannels();
        const int len   = buf.getNumSamples();
        if (numCh < 1 || len < 2048) return;

        const float strength = juce::jlimit (0.0f, 1.0f, strength01);

        //  1024 muestras son 23 ms a 44.1 kHz: bastante resolucion en
        //  frecuencia para separar un zumbido de una nota, y bastante corto
        //  para no emborronar un transitorio de caja. Con 2048 los golpes
        //  pierden el filo y con 512 el zumbido de red y su primer armonico
        //  caen en la misma banda.
        constexpr int order = 10;
        constexpr int fft   = 1 << order;          // 1024
        constexpr int hop   = fft / 4;             // 75% de solape
        constexpr int bins  = fft / 2 + 1;

        juce::dsp::FFT engine (order);

        //  Raiz de Hann en analisis y en sintesis: el producto de las dos es
        //  Hann, y Hann con 75% de solape suma exactamente uno. Poner la
        //  ventana entera en el analisis y nada en la sintesis deja saltos en
        //  los bordes de cada ventana en cuanto la ganancia cambia.
        std::vector<float> win ((size_t) fft);
        for (int i = 0; i < fft; ++i)
            win[(size_t) i] = std::sqrt (0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi
                                                                  * (float) i / (float) fft)));

        const int frames = (len - fft) / hop + 1;
        if (frames < 4) return;

        std::vector<float> fd ((size_t) fft * 2, 0.0f);
        std::vector<float> mag ((size_t) frames * bins, 0.0f);
        std::vector<float> phRe ((size_t) frames * bins, 0.0f);
        std::vector<float> phIm ((size_t) frames * bins, 0.0f);
        std::vector<float> noise ((size_t) bins, 0.0f);
        std::vector<float> col ((size_t) frames, 0.0f);
        std::vector<float> gain ((size_t) bins, 1.0f);
        std::vector<float> prevGain ((size_t) bins, 1.0f);
        std::vector<float> out ((size_t) len, 0.0f);
        std::vector<float> norm ((size_t) len, 0.0f);

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* d = buf.getReadPointer (ch);
            std::fill (out.begin(), out.end(), 0.0f);
            std::fill (norm.begin(), norm.end(), 0.0f);
            std::fill (prevGain.begin(), prevGain.end(), 1.0f);

            //  Pasada 1: el espectro de cada ventana, guardado entero. Cabe:
            //  cinco segundos a 44.1 kHz son 861 ventanas por 513 bandas, 1.7
            //  MB por canal, y esto no corre en el hilo de audio.
            for (int f = 0; f < frames; ++f)
            {
                const int at = f * hop;
                std::fill (fd.begin(), fd.end(), 0.0f);
                for (int i = 0; i < fft; ++i)
                    fd[(size_t) i] = d[at + i] * win[(size_t) i];

                engine.performRealOnlyForwardTransform (fd.data(), true);

                for (int k = 0; k < bins; ++k)
                {
                    const float re = fd[(size_t) (2 * k)];
                    const float im = fd[(size_t) (2 * k + 1)];
                    mag [(size_t) (f * bins + k)] = std::sqrt (re * re + im * im);
                    phRe[(size_t) (f * bins + k)] = re;
                    phIm[(size_t) (f * bins + k)] = im;
                }
            }

            //  El perfil: percentil 20 por banda. nth_element y no sort - solo
            //  hace falta saber quien cae en esa posicion, no ordenar 861
            //  numeros por banda.
            const size_t pick = (size_t) juce::jlimit (0, frames - 1, (int) (frames * 0.20f));
            for (int k = 0; k < bins; ++k)
            {
                for (int f = 0; f < frames; ++f)
                    col[(size_t) f] = mag[(size_t) (f * bins + k)];
                std::nth_element (col.begin(), col.begin() + (long) pick, col.end());
                noise[(size_t) k] = col[pick];
            }

            //  Cuanto se resta y hasta donde se deja bajar. alpha por encima de
            //  1 resta MAS de lo estimado, que es lo que hace falta porque el
            //  ruido fluctua y restar justo la media deja la mitad de las
            //  ventanas por encima. El suelo evita el silencio absoluto, que
            //  suena peor que el ruido: un hueco perfecto entre notas delata
            //  el proceso.
            const float alpha = 1.5f + 2.5f * strength;
            const float floorG = 0.06f * (1.0f - strength) + 0.008f;

            for (int f = 0; f < frames; ++f)
            {
                for (int k = 0; k < bins; ++k)
                {
                    const float m = mag[(size_t) (f * bins + k)];
                    const float clean = m - alpha * noise[(size_t) k];
                    gain[(size_t) k] = (m > 1.0e-9f) ? juce::jmax (floorG, clean / m) : 1.0f;
                }

                //  Suavizado en frecuencia: tres bandas. Sin esto quedan
                //  bandas sueltas abiertas en medio de bandas cerradas, que es
                //  exactamente lo que se oye como campanitas.
                float prev = gain[0];
                for (int k = 1; k < bins - 1; ++k)
                {
                    const float sm = (prev + gain[(size_t) k] + gain[(size_t) (k + 1)]) / 3.0f;
                    prev = gain[(size_t) k];
                    gain[(size_t) k] = sm;
                }

                //  Y en el tiempo: abre rapido y cierra despacio. Al reves se
                //  come el ataque de cada golpe, que es justo lo unico que no
                //  se puede tocar en una caja de ritmos.
                for (int k = 0; k < bins; ++k)
                {
                    const float g0 = gain[(size_t) k];
                    const float p  = prevGain[(size_t) k];
                    gain[(size_t) k] = (g0 > p) ? (0.60f * p + 0.40f * g0)
                                                : (0.85f * p + 0.15f * g0);
                    prevGain[(size_t) k] = gain[(size_t) k];
                }

                std::fill (fd.begin(), fd.end(), 0.0f);
                for (int k = 0; k < bins; ++k)
                {
                    fd[(size_t) (2 * k)]     = phRe[(size_t) (f * bins + k)] * gain[(size_t) k];
                    fd[(size_t) (2 * k + 1)] = phIm[(size_t) (f * bins + k)] * gain[(size_t) k];
                }
                engine.performRealOnlyInverseTransform (fd.data());

                const int at = f * hop;
                for (int i = 0; i < fft; ++i)
                {
                    const float w = win[(size_t) i];
                    out [(size_t) (at + i)] += fd[(size_t) i] * w;
                    norm[(size_t) (at + i)] += w * w;
                }
            }

            //  Dividir por la suma de ventanas y no dar por hecho que vale uno:
            //  en los dos primeros y los dos ultimos saltos no hay solape
            //  completo, y sin esta division los bordes salen atenuados.
            float* w = buf.getWritePointer (ch);
            for (int i = 0; i < len; ++i)
                w[i] = (norm[(size_t) i] > 1.0e-6f) ? out[(size_t) i] / norm[(size_t) i]
                                                    : w[i];
        }
    }
}
