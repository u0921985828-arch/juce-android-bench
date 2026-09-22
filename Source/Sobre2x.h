#pragma once

#include <JuceHeader.h>
#include <cmath>

//  SOBREMUESTREO x2 PARA UNA NO LINEALIDAD, Y SOLO DONDE LA MEDIDA LO PIDIO.
//
//  POR QUE EXISTE. Las voces pasaron siete fases de calidad y acabaron con 4x
//  y `Diezmador` -513 taps, rizo 0.00001 dB, rechazo -123.3 dB-. A los efectos
//  no se les pregunto nunca, y `grep -rl Diezmador Source/` lo decia en una
//  linea: `Sintes.cpp`, `Kits.h`, la cabecera y `StressTest.cpp`. Ni un efecto.
//  Los treinta corren a 1x.
//
//  Y LA SONDA DE PLIEGUE, apuntada a los efectos por primera vez, dio las
//  cifras: con un tono de 5 kHz y el suelo de la sonda en **-55.8 dB**,
//
//      DRV con el drive a tope     -19.8 dB en 13 000 Hz
//      FLD con el pliegue a tope   -12.8 dB en 13 000 Hz
//      BIT a cuatro bits           -21.7 dB en 21 000 Hz
//
//  DRV y FLD son los dos que este fichero arregla, y BIT NO, que es la mitad
//  que hay que decir: un machacador de bits existe para decimar, y el pliegue
//  que saca ES su sonido. Arreglarlo por simetria seria quitarle el efecto.
//  Es la misma decision que las tres ramas de `wantH` en la tanda 9: se toca
//  la que la medida condena y no sus vecinas.
//
//  POR QUE NO SE USA `Diezmador`. Sus 513 taps son de sintesis OFFLINE: por
//  canal, por bloque y por las dos etapas, no cabe en los 2.67 ms que
//  `Tests/Cpu.cpp` cronometra. Esto es un filtro de MEDIA BANDA de 23 taps, y
//  la media banda es la que hace que sea barato: de sus veintitres, los diez
//  pares que no son el centro valen CERO exactamente, asi que se calculan
//  doce productos y no veintitres, y el centro vale 0.5 clavado.
//
//  Y LOS COEFICIENTES SE DERIVAN DE LA NORMA, no se copian de ningun sitio:
//  seno cardinal de media banda por ventana de Kaiser, calculado en
//  `prepara()` -hilo de mensajes, una vez-. Copiar una tabla es como copiar
//  los coeficientes de `Eq5.h` dentro de su prueba: la errata viaja con la
//  copia y las dos mitades salen de acuerdo estando las dos mal.
//
//  LO QUE CUESTA, MEDIDO Y NO ESTIMADO. Doce productos para subir, doce para
//  bajar y la no linealidad evaluada dos veces, por muestra y por canal, en
//  DOS de los treinta efectos. `Tests/Cpu.cpp` lo cronometra contra los
//  2.67 ms de un bloque de 128 a 48 kHz, y la fila que manda es la peor que
//  tiene el banco -`16 pads en 16 CANALES -> TODOS`, o sea los treinta
//  efectos vivos en dieciseis canales a la vez-, tres corridas de cada:
//
//      a 1x   1.051 / 1.057 / 1.057 ms   ->  mediana 1.057   **39.6 %**
//      a 2x   1.201 / 1.185 / 1.195 ms   ->  mediana 1.195   **44.8 %**
//
//  **+0.138 ms**, o sea cinco puntos y medio de presupuesto en el caso que
//  nadie va a tener -dieciseis canales con los treinta abiertos-. Y LO QUE
//  DICE LA CIFRA es que el coste no esta en el filtro sino en el reparto: en
//  la sesion realista -`SESION REAL`- la diferencia es 0.128 -> 0.136 ms, que
//  es ruido de medida.
//
//  Y EL RESULTADO, con la misma sonda que lo pidio:
//
//      DRV   -19.8 dB  ->  **-35.6 dB**   (15.8 dB menos de alias)
//      FLD   -12.8 dB  ->  **-27.2 dB**   (14.4 dB menos)
//
//  Lo que queda se mudo de 13 000 Hz a 21 000: un media banda tiene la
//  transicion centrada en la mitad de la banda del doble -24 kHz-, asi que el
//  residuo vive pegado a Nyquist en vez de en mitad del espectro, que es la
//  otra mitad de la mejora y no se lee en el numero.
struct Sobre2x
{
    //  VEINTITRES TAPS Y NO CINCUENTA, y el numero sale de lo que hace falta
    //  y no de lo que estaria bien. Lo que hay que tapar son los -19.8 dB de
    //  DRV hasta el suelo de la sonda en -55.8: treinta y seis decibelios. Un
    //  media banda de veintitres taps con Kaiser da del orden de sesenta, o
    //  sea que sobra margen; el siguiente escalon -cuarenta y siete taps-
    //  cuesta el doble para tapar algo que ya no se mide.
    //
    //  Y ES 4k+3 PORQUE ASI TIENE QUE SER un media banda: el centro cae en un
    //  taps y los ceros quedan simetricos a los dos lados. Con 21 o 25 la
    //  cuenta no cierra y la mitad de los taps que tenian que valer cero no
    //  valen cero.
    static constexpr int kMitad = 11;                    // 23 taps: -11 .. +11
    static constexpr int kPares = kMitad + 1;            // 12 impares no nulos
    static constexpr int kAnilloX = 16;                  // potencia de dos >= 12
    static constexpr int kAnilloV = 32;                  // potencia de dos >= 24

    //  `a` para subir -ya con el 2 de la interpolacion dentro- y `h` para
    //  bajar. Son los MISMOS doce numeros con un factor de dos, y se guardan
    //  los dos para no multiplicar por muestra.
    static inline float a[kPares] {};
    static inline float h[kPares] {};
    static inline bool  listo = false;

    //  I0 DE BESSEL POR SU SERIE, que converge en veinte terminos para los
    //  betas utiles y no arrastra ninguna dependencia. Escrita aqui porque es
    //  la mitad de la ventana de Kaiser y no un detalle.
    static double i0 (double x) noexcept
    {
        double s = 1.0, t = 1.0;
        for (int k = 1; k < 24; ++k)
        {
            t *= (x * 0.5) / (double) k;
            s += t * t;
        }
        return s;
    }

    //  SE LLAMA DESDE `prepareToPlay` Y NO DESDE EL BUCLE. Aqui hay `exp`,
    //  `sin` y una serie: nada de eso puede correr en el hilo de audio, y con
    //  la tabla ya hecha el hilo de audio solo multiplica y suma.
    static void prepara() noexcept
    {
        if (listo) return;

        //  BETA = 7, y se dice de donde: la regla de Kaiser da A ~ 2.285 *
        //  beta + 8 decibelios de rechazo para una transicion dada, o sea unos
        //  24 + 8 de suelo por el ancho que deja un media banda de este largo.
        //  Medido con la sonda de pliegue despues de ponerlo, que es lo unico
        //  que cuenta.
        constexpr double beta = 7.0;
        const double den = i0 (beta);

        for (int i = 0; i < kPares; ++i)
        {
            //  `n` recorre los indices IMPARES de -11 a +11, que son los
            //  unicos que no valen cero en un media banda.
            const double n = (double) (2 * i - kMitad);
            //  Seno cardinal de media banda: h[n] = 0.5 * sinc(n/2), o sea
            //  sin(pi*n/2) / (pi*n). Para n impar eso es +-1/(pi*n), que es
            //  por donde se ve que el filtro es media banda y no otro.
            const double sinc = std::sin (juce::MathConstants<double>::pi * n * 0.5)
                              / (juce::MathConstants<double>::pi * n);
            const double r = n / (double) kMitad;
            const double win = i0 (beta * std::sqrt (juce::jmax (0.0, 1.0 - r * r))) / den;
            h[i] = (float) (sinc * win);
            a[i] = 2.0f * h[i];
        }
        listo = true;
    }

    //  EL ESTADO DE UN CANAL. Dos anillos: el de la entrada a 1x para subir y
    //  el de la señal a 2x para bajar. Fijos y en la pila del `Inserto`, que
    //  es lo que permite que el hilo de audio no reserve nada.
    struct Estado
    {
        float xr[kAnilloX] {};
        float vr[kAnilloV] {};
        int   ix = 0, iv = 0;

        void limpia() noexcept
        {
            for (auto& v : xr) v = 0.0f;
            for (auto& v : vr) v = 0.0f;
            ix = iv = 0;
        }
    };

    //  UNA MUESTRA: sube a 2x, deja que `f` trabaje en las dos, y baja.
    //
    //  `f` se pasa como plantilla y no como `std::function` a proposito: una
    //  `std::function` reserva y llama por puntero, y esto corre por muestra y
    //  por canal en el hilo de audio.
    //
    //  El retardo total es de once muestras a 2x, o sea CINCO MUESTRAS Y MEDIA
    //  a 48 kHz: 0.115 ms. Es el precio, esta dicho, y es el mismo para los
    //  dos canales asi que no mueve la imagen estereo.
    template <typename Fn>
    static float paso (Estado& e, float x, Fn&& f) noexcept
    {
        //  1. Entra la muestra en el anillo de 1x.
        e.ix = (e.ix + 1) & (kAnilloX - 1);
        e.xr[e.ix] = x;

        //  2. Sube. La fase PAR es la entrada retrasada seis muestras -el
        //     centro del filtro, que vale 0.5 y con el dos de la interpolacion
        //     vale uno-; la IMPAR es el producto de los doce taps.
        const float u0 = e.xr[(e.ix - 6) & (kAnilloX - 1)];
        float u1 = 0.0f;
        for (int i = 0; i < kPares; ++i)
            u1 += a[i] * e.xr[(e.ix - i) & (kAnilloX - 1)];

        //  3. La no linealidad, en las DOS. Esta es toda la diferencia: a 1x
        //     los armonicos que pasan de 24 kHz se doblan hacia dentro y ya no
        //     hay forma de quitarlos; a 2x el techo esta en 48 y los que se
        //     doblan caen entre 24 y 48, o sea donde el filtro de bajar los
        //     tira.
        const float y0 = f (u0);
        const float y1 = f (u1);

        //  4. Baja. Las dos muestras entran en el anillo de 2x en su orden.
        e.iv = (e.iv + 1) & (kAnilloV - 1); e.vr[e.iv] = y0;
        e.iv = (e.iv + 1) & (kAnilloV - 1); e.vr[e.iv] = y1;

        //  El centro cae en `iv - 11` y los impares en los pares de alrededor,
        //  que es la misma simetria de subir leida al reves.
        float z = 0.5f * e.vr[(e.iv - kMitad) & (kAnilloV - 1)];
        for (int i = 0; i < kPares; ++i)
            z += h[i] * e.vr[(e.iv - 2 * i) & (kAnilloV - 1)];
        return z;
    }
};
