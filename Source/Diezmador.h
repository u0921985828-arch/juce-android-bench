#pragma once

#include <JuceHeader.h>
#include <vector>
#include <cmath>

// ============================================================================
//  EL DIEZMADOR — bajar de 4x a la tasa de salida sin traerse el pliegue.
// ============================================================================
//
//  POR QUE HACE FALTA, con la cifra.
//
//  La sintesis de esta casa limita en banda los FLANCOS -`polyBlep`, `sawBl`,
//  `pulsoBl`- y salta los parciales aditivos que pasan de Nyquist. Eso cubre los
//  osciladores y no cubre lo que viene despues: `limita()` es `soft()`, un
//  polinomio de QUINTO orden aplicado por muestra en trece de las dieciseis
//  formas, y la FM de PIANO ELEC y CAMPANAS no tiene limite de banda ninguno.
//
//  El caso medido: PIANO ELEC / GLASS lleva razon 28.0, asi que en la raiz +24
//  -523.25 Hz- el modulador esta en **14.65 kHz** y su banda lateral de orden
//  dos cae en 29.3 kHz. A 48 kHz eso vuelve plegado a **18.7 kHz** y se queda
//  ESCRITO DENTRO de la muestra: ya no hay filtro que lo quite despues.
//
//  Generando a 4x el Nyquist del render sube a 96 kHz, ese producto cabe entero,
//  y este filtro lo tira antes de guardar. El pliegue deja de existir en vez de
//  taparse.
//
// ----------------------------------------------------------------------------
//  POR QUE UN FIR PROPIO Y NO `juce::dsp::Oversampling`.
//
//  La razon que manda es la cuarta y no es tecnica: el diseño del FIR de esa
//  clase depende de la VERSION de JUCE. Toda esta casa depende de que la semilla
//  fija de `Sintes` y el `1000 + index*37` de `Kits` den **el mismo audio en
//  cada arranque y en cada telefono** -si no, un proyecto guardado suena
//  distinto al abrirlo-. Una actualizacion de JUCE cambiaria las sesenta y
//  cuatro muestras de fabrica y el audio de todo proyecto con receta, y nadie
//  sabria por que. Los coeficientes son NUESTROS, como los iconos y como la
//  fabrica.
//
//  Y las otras tres: aqui solo hace falta BAJAR -no el par subir/bajar con su
//  contabilidad de latencia-; esto corre FUERA DE LINEA, sin bloques ni estado
//  que arrastrar entre llamadas; y el retardo se compensa rindiendo cabecera de
//  mas y tirandola, que ya hay que hacerlo para el fundido cruzado.
//
// ----------------------------------------------------------------------------
//  LOS TRES NUMEROS DEL DISEÑO, y de donde sale cada uno.
//
//  · **513 taps** y no los 377 que pide la formula de Kaiser para 120 dB con una
//    transicion de 4.8 kHz a 192 kHz. Con 513 el centro cae en la muestra 256,
//    que a 4x son **64 muestras de salida CLAVADAS**; con 377 el retardo caeria
//    en 63.75 y el punto de bucle quedaria tres cuartos de muestra donde `pre`
//    no dice que esta. El largo lo decide el retardo, no la atenuacion.
//
//  · **La banda de paso acaba en 19 200 Hz**, y la razon no es el oido: `soft()`
//    es de quinto orden, y un orden cinco sobre una entrada acotada a B produce
//    hasta 5B. El Nyquist del render son 96 kHz, y **96/5 = 19.2**. Por encima
//    de eso lo que hay es producto del saturador y no señal.
//
//  · **Fase lineal**, o sea simetrico. El material es un CUERPO DE BUCLE: una
//    fase no lineal deforma distinto la cabeza y el punto de retorno, y entonces
//    el fundido cruzado deja de sumar las dos mitades de la misma muestra, que
//    es lo unico que hace la costura continua por construccion.
// ============================================================================
namespace Diezmador
{
    //  EL PRODUCTO Y EL PATRON, con sus dos largos.
    //
    //  El producto genera a 4x y baja con 513 taps. El PATRON del banco genera
    //  el mismo material a 16x y baja con 2049, y de la comparacion entre los
    //  dos sale el pliegue: lo que el producto tiene de mas es lo que se plego.
    //
    //  Los dos largos NO son la misma cifra escalada por gusto. La transicion es
    //  la misma en HERCIOS -de 19 200 a 24 000, o sea 4 800- pero relativa a la
    //  tasa de render se estrecha cuatro veces al pasar de 4x a 16x, y Kaiser
    //  pide taps en proporcion inversa: (120-8)/(2.285 * 2pi * 4800/768000) =
    //  **1249**. Se sube al siguiente que cumpla las dos condiciones de abajo.
    //
    //  Y las dos condiciones, que son las que fijan los numeros exactos:
    //   · impar, para que sea simetrico y la fase salga lineal;
    //   · con la mitad MULTIPLO DEL FACTOR, para que el retardo de grupo caiga
    //     en una muestra de salida entera. 513/2 = 256, y 256/4 = 64 clavadas.
    //     2049/2 = 1024, y 1024/16 = 64, las mismas.
    static constexpr int kOs       = 4;
    static constexpr int kTaps     = 513;
    static constexpr int kOsPatron = 16;
    static constexpr int kTapsPatron = 2049;

    //  DONDE ACABA LA BANDA UTIL, y la razon no es el oido.
    //
    //  `soft()` -el saturador de la mitad larga de las veinticuatro formas- es un polinomio
    //  de QUINTO orden, y un orden cinco sobre una entrada acotada a B produce
    //  hasta 5B. A 4x el Nyquist del render son 96 kHz, y **96/5 = 19.2**. Por
    //  encima de eso lo que hay es producto del saturador y no señal.
    //
    //  Es el MISMO numero para el patron a 16x, y a proposito: si el patron
    //  cortara mas arriba, la diferencia entre los dos mediria el corte y no el
    //  pliegue.
    static constexpr double kBandaHz = 19200.0;
    static constexpr double kParoHz  = 24000.0;    // la mitad de la tasa de salida

    inline double bandaHz (double) noexcept { return kBandaHz; }

    namespace detail
    {
        //  Bessel modificada de orden cero, por su serie. Converge rapido para
        //  los argumentos de una ventana de Kaiser (beta hasta ~15) y se calcula
        //  una sola vez en toda la vida del proceso.
        inline double i0 (double x) noexcept
        {
            double suma = 1.0, term = 1.0;
            for (int k = 1; k < 64; ++k)
            {
                term *= (x * x) / (4.0 * (double) k * (double) k);
                suma += term;
                if (term < suma * 1.0e-16) break;
            }
            return suma;
        }

        //  El nucleo, dado el factor y el largo. Ver `coeficientes`.
        inline std::vector<double> diseña (int os, int taps)
        {
            const int mitad = taps / 2;
            std::vector<double> c ((size_t) taps, 0.0);

            //  Corte a mitad de la transicion, normalizado a la tasa de RENDER.
            const double fc = (kBandaHz + kParoHz) * 0.5 / (48000.0 * (double) os);

            //  Kaiser para 120 dB: beta = 0.1102 (A - 8.7).
            const double beta = 0.1102 * (120.0 - 8.7);
            const double den  = i0 (beta);

            double suma = 0.0;
            for (int i = 0; i < taps; ++i)
            {
                const int    m = i - mitad;
                const double x = (double) m;
                //  sinc normalizado. En el centro vale 2*fc por el limite.
                const double sn = (m == 0)
                    ? 2.0 * fc
                    : std::sin (juce::MathConstants<double>::twoPi * fc * x)
                        / (juce::MathConstants<double>::pi * x);

                const double r = (double) m / (double) mitad;
                const double w = i0 (beta * std::sqrt (juce::jmax (0.0, 1.0 - r * r))) / den;

                c[(size_t) i] = sn * w;
                suma += c[(size_t) i];
            }

            //  GANANCIA UNIDAD EN CONTINUA, y esto no es cosmetica: sin
            //  normalizar, el error de truncado de la serie deja el nivel
            //  desplazado unas centesimas de decibelio y la igualacion por
            //  sonoridad de `Kits` lo arrastra a los sesenta y cuatro.
            for (auto& v : c) v /= suma;
            return c;
        }
    }

    //  LOS COEFICIENTES, calculados UNA vez por factor.
    //
    //  `static const` dentro de la funcion: la inicializacion de un local
    //  estatico esta garantizada una sola vez y con cerrojo, que es lo mismo que
    //  ya hace `Sintes::rango()` y por la misma razon -esto lo llaman varios
    //  hilos del cargador a la vez-. Lo que se escribe a mano son los cuatro
    //  numeros de arriba, no los 257 ni los 1025.
    inline const std::vector<double>& coeficientes (int os)
    {
        static const std::vector<double> producto = detail::diseña (kOs, kTaps);
        static const std::vector<double> patron   = detail::diseña (kOsPatron, kTapsPatron);
        return (os == kOsPatron) ? patron : producto;
    }

    inline int tapsDe  (int os) noexcept { return (os == kOsPatron) ? kTapsPatron : kTaps; }
    inline int mitadDe (int os) noexcept { return tapsDe (os) / 2; }

    //  CUANTAS MUESTRAS DE RENDER hacen falta para sacar `salida` muestras.
    //
    //  El filtro necesita `mitad` por delante del primer punto y `mitad` por
    //  detras del ultimo. Se rinde de mas y se tira, que es mas barato y mas
    //  honesto que rellenar con ceros DESPUES: el silencio de antes del ataque
    //  si es verdad, pero un corte al final no lo es.
    inline int largoDeRender (int salida, int os = kOs) noexcept
    {
        return salida * os + 2 * mitadDe (os);
    }

    //  DIEZMA. `src` tiene `largoDeRender (salida, os)`; escribe `salida`.
    //
    //  El bucle salta de `os` en `os` y aprovecha la simetria: la mitad de
    //  multiplicaciones que taps.
    inline void diezma (const float* src, float* dst, int salida, int os = kOs) noexcept
    {
        const auto& h = coeficientes (os);
        const int mitad = mitadDe (os);
        for (int n = 0; n < salida; ++n)
        {
            const float* p = src + n * os;           // el centro cae en p[mitad]
            double acc = (double) p[mitad] * h[(size_t) mitad];
            for (int j = 1; j <= mitad; ++j)
                acc += ((double) p[mitad - j] + (double) p[mitad + j]) * h[(size_t) (mitad + j)];
            dst[n] = (float) acc;
        }
    }
}
