#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "Eq5.h"
#include "Dinamica.h"
#include "Fdn.h"

// ============================================================================
//  LO QUE EL VISOR DE UN EFECTO DIBUJA, SIN COMPONENTE DELANTE.
//
//  Esto vivia dentro de `FxMini::muestrea`, o sea dentro de un
//  `juce::Component`, y ahi era correcto mientras el unico cliente fuera el
//  pintor. La pregunta que llego -«los visuales de los efectos son imagenes
//  no? no son reales que digamos»- no se podia contestar con un numero
//  precisamente por eso: el banco del motor es una aplicacion de consola que
//  monta un `AudioEngine` y mide lo que SALE, y no podia llamar a un metodo
//  privado de un componente para comparar. Sale aqui, que es la misma
//  extraccion que ya se hizo con `barridoDe` y con `bajaDb` y por lo mismo:
//  en cuanto hay un segundo cliente, la regla no puede vivir dentro de uno de
//  los dos.
//
//  Y CON ELLA, LO QUE ESTABA MAL. Leidas las once ramas contra el DSP:
//
//    - FLT y HPF dibujaban un polo simple -6 dB por octava- contra un
//      `StateVariableTPTFilter` de DOCE, y le sumaban un pico de resonancia
//      que era una gaussiana inventada aqui. Ahora los dos salen de
//      `AudioEngine::svfDb`, que es el modulo del filtro que suena.
//    - REV dibujaba `tau = 0.12 + 0.75 size (1 - 0.55 damp)`: tres constantes
//      que no existen en la reverb. Medido contra la `Fdn` de verdad, la cola
//      real va de 0.12 s a 0.64 y la dibujada de 0.12 a 0.87 - coinciden abajo
//      y se separan un 36 % arriba, que es la firma de un ajuste hecho a ojo.
//      Ahora la pide: `Fdn::tauSegundos`.
//    - DRV y BIT llevaban la formula del motor COPIADA caracter por caracter.
//      Funcionaba, y el dia que alguien moviera el `24.0f` de DRV o el `*0.5f`
//      de BIT el visor habria seguido dibujando el de ayer sin que nada
//      fallara. Ahora llaman a `driveDe`/`saturaDe` y a `nivelesDe`/`crush`.
//    - El EQ tenia su rama escrita y NO SE DIBUJA NUNCA: `fxTraeCara` manda
//      -1 al visor justo para el, porque el EQ se lleva el plato entero con su
//      curva. Un `case` inalcanzable dice hacer algo que no hace, asi que se
//      va; quien enseña el EQ es `EqCurve`, que si sale de `Eq5`.
//
//  Lo que se queda escrito a mano, y declarado: la puerta y el limitador. Un
//  escalon y un `min` no son una regla, son su propia definicion — y ahora,
//  ademas, pasan la misma medida que los demas.
// ============================================================================
namespace FxVisor
{
    //  Cuarenta y ocho puntos y no doscientos: la tira mide 26 px de alto y en
    //  la pantalla mas estrecha unos 130 de ancho, asi que por encima de eso
    //  se muestrea mas fino que el pixel. Y es lo que el banco compara.
    static constexpr int kPuntos = 48;
    using Curva = std::array<float, kPuntos>;

    //  LA VENTANA DE LOS DOS QUE DIBUJAN TIEMPO, compartida a proposito: dos
    //  visores de la misma familia con dos escalas distintas se leen como si
    //  el segundo durase menos. Dos segundos es lo que cabe de un delay de un
    //  segundo con su primera repeticion.
    static constexpr float kVentanaMs = 2000.0f;

    //  Y LA DE BIT, que no es tiempo de cola sino de MUESTRA: RATE llega a 64,
    //  asi que con menos ventana el retenedor no daria ni dos escalones.
    static constexpr int kVentanaBit = 128;

    //  LOS DOS QUE SUMAN DIBUJAN TIEMPO Y LOS NUEVE QUE SUSTITUYEN DIBUJAN
    //  NIVEL. No es la misma pregunta que `AudioEngine::sustituye` aunque hoy
    //  den lo mismo: aquella dice lo que el motor HACE con el camino seco y
    //  esta dice que forma tiene el dibujo. Escritas por separado a proposito,
    //  o el banco no podria comprobar la primera contra la segunda.
    inline bool deTiempo (int f) noexcept
    {
        return f == AudioEngine::kFxDly || f == AudioEngine::kFxRev;
    }

    //  QUE MANDOS MUEVEN EL DIBUJO, dicho por la app y no adivinado por el
    //  banco.
    //
    //  `Tests/rack.py` movia los TRES a la vez y le bastaba con que uno
    //  cambiara la curva, asi que cinco mandos que no movian nada llevaban ahi
    //  desde el primer dia sin que ninguna regla pudiera verlo: el TONE de
    //  DRV, el RATE de BIT, el FREQ del de-esser, el CIERRE de la puerta y el
    //  SOLTAR del limitador. El RATE si cabia -es la otra mitad de un crusher-
    //  y entra. Los otros cuatro no caben en el eje que su visor usa, y eso se
    //  DICE en vez de fingirse: es la misma decision que la marca `valor` de
    //  los iconos, que existe porque una lista de excepciones escrita en el
    //  script solo sabe medir una de las cuatro compilaciones.
    struct Mandos { bool p0, p1; };
    inline Mandos mandosDe (int f) noexcept
    {
        switch (f)
        {
            case AudioEngine::kFxFlt: return { true,  true  };   // barrido y resonancia
            case AudioEngine::kFxHpf: return { true,  true  };   // corte y resonancia
            case AudioEngine::kFxDly: return { true,  true  };   // tiempo y realimentacion
            case AudioEngine::kFxBit: return { true,  true  };   // bits y retencion
            case AudioEngine::kFxRev: return { true,  true  };   // tamaño y amortiguado
            case AudioEngine::kFxCmp: return { true,  true  };   // umbral y ratio
            //  TONE es un paso bajo DETRAS del saturador: una curva de
            //  transferencia no tiene eje donde ponerlo.
            case AudioEngine::kFxDrv: return { true,  false };
            //  FREQ es el cruce Linkwitz-Riley que parte la banda alta, y la
            //  curva estatica es la del calculador de ganancia.
            case AudioEngine::kFxDss: return { false, true  };
            //  CIERRE y SOLTAR son TIEMPOS, y esto es una transferencia.
            case AudioEngine::kFxGte: return { true,  false };
            case AudioEngine::kFxLim: return { true,  false };
            default:                  return { false, false };
        }
    }

    //  ------------------------------------------------------------------
    //  EL MUESTREO. Cada tipo deja la curva en 0..1, donde 0 es el renglon de
    //  abajo. Lo que significa el eje X cambia con la familia y esa es la
    //  gracia: en un filtro es la frecuencia, en una transferencia el nivel de
    //  entrada, y en un eco el tiempo.
    //  ------------------------------------------------------------------
    inline void muestrea (int fx, float p0, float p1, Curva& out)
    {
        auto pon = [&out] (int i, float v)
        {
            out[(size_t) i] = juce::jlimit (0.0f, 1.0f, v);
        };

        //  EL DELAY SE ESCRIBE POR ECOS Y NO POR MUESTRA, que es la unica
        //  familia cuyo dibujo son PUNTOS y no una funcion continua: buscar
        //  «¿cae un eco en esta columna?» desde dentro del bucle es la misma
        //  cuenta al reves y sale un tren que aparece y desaparece segun el
        //  redondeo.
        //
        //  Y ES LA RESPUESTA DE VERDAD de esa realimentacion, no un adorno: el
        //  bus lleva SOLO el camino humedo -`w[i] = d`- y se realimenta con
        //  `in + d*fb`, asi que la salida a un impulso es un eco en cada `k*T`
        //  con amplitud `fb^(k-1)`. Escrito aqui y no compartido porque no hay
        //  con que compartirlo: en el motor eso no es una formula, es lo que
        //  hace un bucle de tres lineas. Lo que si hay ahora es la medida.
        if (fx == AudioEngine::kFxDly)
        {
            out.fill (0.0f);
            const float ms  = juce::jlimit (20.0f, 1000.0f, p0);
            const float fbk = juce::jlimit (0.0f, 0.95f, p1);
            for (int k = 1; k < 64; ++k)
            {
                const float seg = (float) k * ms;
                if (seg > kVentanaMs) break;
                const int i = juce::jlimit (0, kPuntos - 1,
                                            (int) std::round (seg / kVentanaMs * (float) (kPuntos - 1)));
                out[(size_t) i] = juce::jmax (out[(size_t) i], std::pow (fbk, (float) (k - 1)));
            }
            return;
        }

        //  BIT SE DIBUJA COMO UNA ONDA Y NO COMO UNA ESCALERA, que es lo que
        //  hace que sus DOS mandos se vean. La escalera de transferencia sola
        //  es la mitad de un crusher -la amplitud- y dejaba RATE, que es el
        //  tiempo, sin nada que mover. Se pasa una onda por el efecto de
        //  verdad: `AudioEngine::crush`, el mismo bucle que corre en el hilo
        //  de audio.
        if (fx == AudioEngine::kFxBit)
        {
            std::array<float, kVentanaBit> onda {};
            for (int n = 0; n < kVentanaBit; ++n)
                onda[(size_t) n] = std::sin (juce::MathConstants<float>::twoPi
                                             * (float) n / (float) kVentanaBit);

            float fase = 0.0f, hold = 0.0f;
            AudioEngine::crush (onda.data(), kVentanaBit,
                                AudioEngine::nivelesDe (p0),
                                juce::jmax (1.0f, p1), fase, hold);

            for (int i = 0; i < kPuntos; ++i)
            {
                const int n = juce::jlimit (0, kVentanaBit - 1,
                                            (int) std::round ((float) i * (float) (kVentanaBit - 1)
                                                              / (float) (kPuntos - 1)));
                pon (i, 0.5f + 0.5f * onda[(size_t) n]);
            }
            return;
        }

        //  De 20 Hz a 20 kHz en logaritmico, que es el mismo eje que ya usa la
        //  curva del EQ. Ver EqCurve::xDe.
        auto hzDe = [] (float t)
        {
            const float lo = std::log (Eq5::kFreqMin), hi = std::log (Eq5::kFreqMax);
            return std::exp (lo + t * (hi - lo));
        };
        //  De -60 a 0 dB de entrada, que es donde vive un umbral.
        auto dbDe = [] (float t) { return -60.0f + 60.0f * t; };
        //  -24..+12 dB de respuesta repartidos en el alto de la tira.
        auto dbAAlto = [] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 24.0f) / 36.0f); };

        for (int i = 0; i < kPuntos; ++i)
        {
            const float t = (float) i / (float) (kPuntos - 1);

            switch (fx)
            {
                case AudioEngine::kFxFlt:
                {
                    //  Ver AudioEngine::barridoDe y svfDb: el reparto y la
                    //  forma son los dos suyos.
                    const auto b = AudioEngine::barridoDe (p0);
                    pon (i, ! b.activo
                              ? dbAAlto (0.0f)
                              : dbAAlto (AudioEngine::svfDb (hzDe (t), b.hz, p1, b.alto)));
                    break;
                }

                case AudioEngine::kFxHpf:
                    pon (i, dbAAlto (AudioEngine::svfDb (hzDe (t), p0, p1, true)));
                    break;

                case AudioEngine::kFxDrv:
                {
                    //  La transferencia, con el MISMO tanh que suena.
                    const auto  dr = AudioEngine::driveDe (p0);
                    const float x  = -1.0f + 2.0f * t;
                    pon (i, 0.5f + 0.5f * AudioEngine::saturaDe (x, dr));
                    break;
                }

                case AudioEngine::kFxCmp:
                case AudioEngine::kFxDss:
                {
                    //  Ver Dinamica::bajaDb: la rodilla vive alli.
                    const auto modo = (fx == AudioEngine::kFxCmp ? Dinamica::compresor : Dinamica::deesser);
                    const float um  = Dinamica::umbralDeDb (modo, p0, p1);
                    const float rr  = Dinamica::ratioDe (modo, p1);
                    const float in  = dbDe (t);
                    pon (i, (in - Dinamica::bajaDb (in - um, rr) + 60.0f) / 60.0f);
                    break;
                }

                case AudioEngine::kFxGte:
                {
                    const float in = dbDe (t);
                    pon (i, in >= p0 ? (in + 60.0f) / 60.0f : 0.0f);
                    break;
                }

                case AudioEngine::kFxLim:
                {
                    const float in = dbDe (t);
                    pon (i, (juce::jmin (in, p0) + 60.0f) / 60.0f);
                    break;
                }

                case AudioEngine::kFxRev:
                {
                    //  Ver Fdn::tauSegundos: la cola sale de la reverb y no de
                    //  tres numeros ajustados a ojo aqui.
                    const float tau = Fdn::tauSegundos (p0, p1, 48000.0);
                    const float seg = t * kVentanaMs * 0.001f;
                    pon (i, std::exp (-seg / juce::jmax (0.02f, tau)));
                    break;
                }

                default: pon (i, 0.5f); break;
            }
        }
    }
}
