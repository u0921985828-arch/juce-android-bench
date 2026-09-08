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
//  Y LA MEMORIA ES CONSTANTE, QUE NO LO ERA.
//
//  La primera version materializaba el espectrograma ENTERO: magnitud, parte
//  real y parte imaginaria, tres vectores de ventanas x bandas. El comentario
//  decia "cabe: cinco segundos son 1.7 MB por canal" y era verdad para cinco
//  segundos, solo que nada obligaba a que fueran cinco. Echando la cuenta con
//  las de verdad - hop 256, 513 bandas - un minuto a 48 kHz son 23 MB POR
//  VECTOR, o sea 92 MB con los cinco, y la app deja grabar hasta CINCO
//  MINUTOS (setRecordLimit topa en 300 s): 460 MB pedidos de golpe, sin un
//  solo catch, en un telefono. Eso no lanza bad_alloc, lo mata el sistema.
//
//  Tres cambios y el consumo deja de depender de la duracion:
//
//    - La fase no se guarda. Se vuelve a hacer la FFT directa en la segunda
//      pasada. Cuesta una transformada mas por ventana y quita DOS de los
//      tres vectores grandes.
//    - El perfil no necesita todas las ventanas. El ruido es estacionario -
//      es la hipotesis sobre la que se sostiene el metodo entero - asi que el
//      percentil sale igual de 1024 ventanas repartidas por todo el fichero
//      que de las cincuenta mil que tiene. Eso acota el unico vector que
//      quedaba: 2.1 MB, dure lo que dure la muestra.
//    - La sintesis va en flujo. El solape-suma se acumula en un buffer de UNA
//      ventana y se van soltando muestras terminadas por detras, encima del
//      propio buffer. Funciona porque lo que se escribe queda siempre por
//      DETRAS de lo que se lee: la ventana f sintetiza hasta at+fft y las
//      muestras que quedan cerradas son las de antes de at.
//
//  Medido en el banco con una muestra de cinco minutos: el pico de memoria del
//  proceso sube +0.1 MB por encima de los 55 MB que ya ocupa la propia muestra,
//  frente a los ~460 MB que pedia la cuenta anterior (346 de espectrograma mas
//  115 de solape-suma). Y el mismo sonido exactamente: -18.8 dB de suelo con
//  -0.28 dB perdidos en el tono, los mismos dos numeros de antes.
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

        //  Cuantas ventanas se miran para estimar el ruido. Repartidas por
        //  todo el fichero, no las primeras: el ruido es estacionario pero el
        //  SONIDO no, y coger solo el principio de un break da un percentil
        //  sacado de dos compases.
        constexpr int kProfileFrames = 1024;

        const int frames = (len - fft) / hop + 1;
        if (frames < 4) return;

        juce::dsp::FFT engine (order);

        //  Raiz de Hann en analisis y en sintesis: el producto de las dos es
        //  Hann, y Hann con 75% de solape suma exactamente uno. Poner la
        //  ventana entera en el analisis y nada en la sintesis deja saltos en
        //  los bordes de cada ventana en cuanto la ganancia cambia.
        std::vector<float> win ((size_t) fft);
        for (int i = 0; i < fft; ++i)
            win[(size_t) i] = std::sqrt (0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi
                                                                  * (float) i / (float) fft)));

        const int profFrames = juce::jmin (frames, kProfileFrames);

        //  Todo lo que se reserva, reservado AQUI y contado: son 2.1 MB de
        //  perfil, 8 KB de acumuladores y poco mas. Y con red, porque una
        //  muestra sin limpiar es un fastidio y un proceso muerto es el
        //  trabajo de la tarde.
        std::vector<float> fd, prof, col, noise, gain, sPrev, acc, accN;
        try
        {
            fd      .assign ((size_t) fft * 2, 0.0f);
            prof    .assign ((size_t) profFrames * bins, 0.0f);
            col     .assign ((size_t) profFrames, 0.0f);
            noise   .assign ((size_t) bins, 0.0f);
            gain    .assign ((size_t) bins, 1.0f);
            sPrev   .assign ((size_t) bins, 0.0f);
            acc     .assign ((size_t) fft, 0.0f);
            accN    .assign ((size_t) fft, 0.0f);
        }
        catch (const std::bad_alloc&)
        {
            return;                     // la muestra se queda como estaba
        }

        //  Cuanto se sobre-estima el ruido y hasta donde se deja bajar.
        //
        //  `over` por encima de 1 da por hecho que hay MAS ruido del medido,
        //  que hace falta porque el ruido fluctua y restar justo la media deja
        //  la mitad de las ventanas por encima. Y es MUCHO mas pequeno que el
        //  1.5-4.0 de la resta cruda que habia antes: el estimador dirigido de
        //  abajo ya no necesita que se le empuje: 3.0 de sobre-resta en
        //  magnitud son NUEVE en potencia, y con Wiener eso se lleva el sonido
        //  por delante.
        //
        //  El suelo evita el silencio absoluto, que suena peor que el ruido:
        //  un hueco perfecto entre notas delata el proceso.
        const float over   = 1.0f + 1.0f * strength;
        const float floorG = 0.06f * (1.0f - strength) + 0.008f;

        //  Una ventana, transformada y con su magnitud puesta donde se pida.
        auto analyse = [&] (const float* d, int at) noexcept
        {
            std::fill (fd.begin(), fd.end(), 0.0f);
            for (int i = 0; i < fft; ++i)
                fd[(size_t) i] = d[at + i] * win[(size_t) i];
            engine.performRealOnlyForwardTransform (fd.data(), true);
        };

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* d = buf.getReadPointer (ch);
            float*       w = buf.getWritePointer (ch);

            std::fill (acc.begin(),  acc.end(),  0.0f);
            std::fill (accN.begin(), accN.end(), 0.0f);
            std::fill (sPrev.begin(), sPrev.end(), 0.0f);

            // --- Pasada 1: el perfil, de profFrames ventanas repartidas. ----
            for (int q = 0; q < profFrames; ++q)
            {
                //  Repartidas de verdad: la ultima cae en la ultima ventana,
                //  no a un tercio del fichero.
                const int f  = (profFrames == frames) ? q
                             : (int) ((juce::int64) q * (frames - 1) / (profFrames - 1));
                analyse (d, f * hop);

                for (int k = 0; k < bins; ++k)
                {
                    const float re = fd[(size_t) (2 * k)];
                    const float im = fd[(size_t) (2 * k + 1)];
                    prof[(size_t) (q * bins + k)] = std::sqrt (re * re + im * im);
                }
            }

            //  El perfil: percentil 20 por banda. nth_element y no sort - solo
            //  hace falta saber quien cae en esa posicion, no ordenar mil
            //  numeros por banda.
            const size_t pick = (size_t) juce::jlimit (0, profFrames - 1, (int) (profFrames * 0.20f));
            for (int k = 0; k < bins; ++k)
            {
                for (int q = 0; q < profFrames; ++q)
                    col[(size_t) q] = prof[(size_t) (q * bins + k)];
                std::nth_element (col.begin(), col.begin() + (long) pick, col.end());
                noise[(size_t) k] = col[pick];
            }

            // --- Pasada 2: sintesis en flujo. -------------------------------
            //
            //  acc[0] corresponde siempre a la muestra `emitted`. Antes de
            //  cada ventana se sueltan las muestras que ya no puede tocar
            //  nadie, y esas caen SIEMPRE por detras de donde se va a leer.
            int emitted = 0;

            //  La suma de ventanas en REGIMEN, calculada y no supuesta: es la
            //  de w^2 en las posiciones que se solapan sobre un punto
            //  cualquiera del interior. De ahi sale el suelo del divisor.
            float wsteady = 0.0f;
            for (int m = -fft / hop; m <= fft / hop; ++m)
            {
                const int idx = fft / 2 + m * hop;
                if (idx >= 0 && idx < fft) wsteady += win[(size_t) idx] * win[(size_t) idx];
            }
            const float wsumFloor = 0.5f * juce::jmax (1.0e-6f, wsteady);

            auto flush = [&] (int upTo) noexcept
            {
                while (emitted < upTo)
                {
                    const int n = juce::jmin (hop, len - emitted);
                    for (int i = 0; i < n; ++i)
                    {
                        //  Dividir por la suma de ventanas y no dar por hecho
                        //  que vale uno: en los dos primeros y los dos ultimos
                        //  saltos no hay solape completo, y sin esta division
                        //  los bordes salen atenuados.
                        //
                        //  PERO CON SUELO, Y ESTE ES EL FALLO QUE SE VEIA. En
                        //  el primer salto solo hay UNA ventana encima, asi que
                        //  la suma vale una fraccion de lo que vale en regimen
                        //  - y dividir por una fraccion MULTIPLICA. Con el
                        //  guardia en 1e-6 la primera muestra se podia
                        //  amplificar por miles: un pico enorme en la muestra
                        //  cero que la app anunciaba como "pico +5.2 dB"
                        //  despues de una operacion que solo puede ATENUAR.
                        //
                        //  El suelo es la mitad de la suma en regimen. Por
                        //  debajo se deja el borde algo bajo, que es un borde
                        //  de cuatro milisegundos, en vez de meter un chasquido
                        //  al principio de cada muestra limpiada.
                        const float den = juce::jmax (accN[(size_t) i], wsumFloor);
                        if (den > 1.0e-6f)
                            w[emitted + i] = acc[(size_t) i] / den;
                    }

                    std::rotate (acc.begin(),  acc.begin()  + hop, acc.end());
                    std::rotate (accN.begin(), accN.begin() + hop, accN.end());
                    std::fill (acc.end()  - hop, acc.end(),  0.0f);
                    std::fill (accN.end() - hop, accN.end(), 0.0f);
                    emitted += hop;
                }
            };

            for (int f = 0; f < frames; ++f)
            {
                const int at = f * hop;
                flush (at);                     // deja acc[0] apuntando a `at`

                analyse (d, at);                // la fase, otra vez: no se guardo

                //  WIENER CON SNR A PRIORI DIRIGIDO POR DECISION, que es lo
                //  que separa una limpieza de una resta.
                //
                //  La resta cruda que habia aqui decide la ganancia de cada
                //  banda MIRANDO SOLO ESA VENTANA: |Y| - alpha*ruido. Y el
                //  ruido es aleatorio, asi que en dos ventanas seguidas la
                //  misma banda cae a un lado y a otro del umbral. Eso son las
                //  campanitas -bandas sueltas que se abren un fotograma y se
                //  cierran al siguiente- y es lo que obligaba a los dos
                //  suavizados de detras: uno en frecuencia y otro en el
                //  tiempo, los dos sobre la GANANCIA, que es tapar el sintoma.
                //
                //  Lo que se estima aqui es el SNR A PRIORI -cuanta senal hay
                //  de verdad en esa banda- y no la magnitud de esta ventana:
                //
                //    gamma = |Y|^2 / lambda            (a posteriori, medido)
                //    xi    = a * S_ant^2 / lambda
                //          + (1-a) * max (gamma-1, 0)  (a priori, DIRIGIDO)
                //    G     = xi / (1 + xi)             (Wiener)
                //
                //  El primer termino es la memoria: lo que se estimo limpio en
                //  la ventana anterior. Con a = 0.98 el estimador se apoya casi
                //  entero en el pasado mientras el nivel no cambie -o sea que
                //  el ruido deja de fluctuar la ganancia- y suelta la memoria
                //  en cuanto llega un ataque, porque ahi gamma se dispara y el
                //  segundo termino manda. Suaviza donde hace falta y no
                //  suaviza donde no, que es justo lo que un suavizado fijo no
                //  puede hacer.
                //
                //  Por eso se van los dos suavizados: no es que sobren, es que
                //  eran el parche de este estimador.
                constexpr float aDD = 0.98f;
                for (int k = 0; k < bins; ++k)
                {
                    const float re = fd[(size_t) (2 * k)];
                    const float im = fd[(size_t) (2 * k + 1)];
                    const float m  = std::sqrt (re * re + im * im);

                    const float nk     = over * noise[(size_t) k];
                    const float lambda = juce::jmax (1.0e-18f, nk * nk);

                    const float gamma = (m * m) / lambda;
                    const float inst  = juce::jmax (0.0f, gamma - 1.0f);
                    const float mem   = (sPrev[(size_t) k] * sPrev[(size_t) k]) / lambda;

                    const float xi = aDD * mem + (1.0f - aDD) * inst;
                    const float g  = juce::jmax (floorG, xi / (1.0f + xi));

                    gain [(size_t) k] = g;
                    sPrev[(size_t) k] = g * m;      // la limpia de ESTA, memoria de la siguiente
                }

                for (int k = 0; k < bins; ++k)
                {
                    fd[(size_t) (2 * k)]     *= gain[(size_t) k];
                    fd[(size_t) (2 * k + 1)] *= gain[(size_t) k];
                }
                engine.performRealOnlyInverseTransform (fd.data());

                for (int i = 0; i < fft; ++i)
                {
                    const float wi = win[(size_t) i];
                    acc [(size_t) i] += fd[(size_t) i] * wi;
                    accN[(size_t) i] += wi * wi;
                }
            }

            //  Y el rabo: lo que queda en el acumulador cuando ya no hay mas
            //  ventanas. Sin esto la ultima ventana entera se quedaba sin
            //  escribir.
            flush (juce::jmin (len, (frames - 1) * hop + fft));
        }
    }
}
