#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "Eq5.h"
#include "Dinamica.h"
#include "Fdn.h"
#include "Lfo.h"

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

    //  Y LAS DOS DE LA FAMILIA DE CARACTER, que no son la de DLY y REV a
    //  proposito: la de PIT tiene que enseñar el diente del grano mas largo
    //  -120 ms- y la de FRZ dos vueltas de la ventana mas larga -500-. Con los
    //  dos segundos compartidos, la de PIT daria veinte dientes en cuarenta y
    //  ocho columnas y la de FRZ cien. Una ventana compartida vale cuando las
    //  dos escalas se parecen; aqui no.
    static constexpr float kVentanaPit = 480.0f;
    static constexpr float kVentanaFrz = 1000.0f;
    //  Y la de TRN, que es lo que dura un golpe con su cola.
    static constexpr float kVentanaTrn = 300.0f;

    //  LOS DOS QUE SUMAN DIBUJAN TIEMPO Y LOS NUEVE QUE SUSTITUYEN DIBUJAN
    //  NIVEL. No es la misma pregunta que `AudioEngine::sustituye` aunque hoy
    //  den lo mismo: aquella dice lo que el motor HACE con el camino seco y
    //  esta dice que forma tiene el dibujo. Escritas por separado a proposito,
    //  o el banco no podria comprobar la primera contra la segunda.
    //  Y CADA UNO CON SU VENTANA, que es lo que la familia crecio a pedir. La
    //  compartida vale mientras las escalas se parezcan: DLY y REV van los dos
    //  en segundos. PIT dibuja un diente de 20 a 120 ms y FRZ una vuelta de 20
    //  a 500, asi que con los dos segundos compartidos saldrian veinte y cien
    //  dientes en cuarenta y ocho columnas. Devuelve 0 el que no dibuja
    //  tiempo, que es lo que `deTiempo` pregunta.
    inline float ventanaDe (int f) noexcept
    {
        switch (f)
        {
            case AudioEngine::kFxDly:
            case AudioEngine::kFxRev: return kVentanaMs;
            case AudioEngine::kFxPit: return kVentanaPit;
            case AudioEngine::kFxTrn: return kVentanaTrn;
            case AudioEngine::kFxFrz: return kVentanaFrz;
            default:                  return 0.0f;
        }
    }

    inline bool deTiempo (int f) noexcept { return ventanaDe (f) > 0.0f; }

    //  LOS QUE DIBUJAN UNA ONDA. BIT desde que su visor pasa la onda por
    //  `crush`, y RNG porque su curva ES la portadora: los dos tienen amplitud
    //  en el eje vertical, asi que su capa viva es la onda que de verdad sale.
    inline bool deOnda (int f) noexcept
    {
        return f == AudioEngine::kFxBit || f == AudioEngine::kFxRng;
    }

    //  Y LOS QUE DIBUJAN FRECUENCIA. FLT y HPF desde el principio; WID y EXC
    //  porque su eje tambien es la frecuencia -donde se abre el lado y donde
    //  se anaden los armonicos- y por tanto su capa viva es el mismo espectro.
    inline bool deFrecuencia (int f) noexcept
    {
        return f == AudioEngine::kFxFlt || f == AudioEngine::kFxHpf
            || f == AudioEngine::kFxWid || f == AudioEngine::kFxExc;
    }

    //  LA QUINTA FAMILIA: MODULACION. Su forma es el LFO, y sale de
    //  `Lfo::valorEn`, o sea de la MISMA funcion que avanza el hilo de audio.
    //  Nace compartida y no copiada porque copiarla es exactamente el fallo
    //  que esta cabecera enumera arriba: nueve de once visores dibujaban una
    //  formula parecida escrita al lado, y REV llevaba tres constantes que no
    //  existen en la reverb.
    inline bool deModulacion (int f) noexcept
    {
        return f >= AudioEngine::kFxCho && f <= AudioEngine::kFxTrm;
    }

    //  DOS PERIODOS, y la ventana se mide en PERIODOS y no en milisegundos.
    //  Con una ventana de tiempo fijo el mando RATE si moveria el dibujo
    //  -saldrian mas ciclos- y a 8 Hz en dos segundos son 48 columnas para 16
    //  ciclos: tres puntos por ciclo, o sea una forma que ya no es la que
    //  suena. En periodos la forma se lee igual a 0.05 Hz que a 8, y lo que se
    //  pierde -RATE- se dice en `mandosDe` y se enseña en la capa VIVA, donde
    //  el punto viaja a la velocidad de verdad.
    static constexpr float kPeriodosMod = 2.0f;

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
            //  LOS CUATRO DE MODULACION: RATE es un TIEMPO y su eje se mide
            //  en periodos, asi que no cabe -y se dice, no se finge-. Donde
            //  RATE se ve es en el punto de trabajo, que viaja a la fase del
            //  motor.
            case AudioEngine::kFxCho:
            case AudioEngine::kFxFla:
            case AudioEngine::kFxPha:
            case AudioEngine::kFxTrm: return { false, true  };
            //  RNG: su FREQ es lo mismo que un RATE -un tiempo- y la ventana
            //  de su visor se mide en periodos, asi que tampoco cabe.
            case AudioEngine::kFxRng: return { false, true  };
            //  Y los cinco que quedan mueven los DOS: la pendiente y el
            //  periodo del diente de PIT, el ancho y el cruce de WID, el cruce
            //  y la fuerza de EXC, los dos tramos de TRN y la vuelta y la
            //  costura de FRZ.
            case AudioEngine::kFxPit:
            case AudioEngine::kFxWid:
            case AudioEngine::kFxExc:
            case AudioEngine::kFxTrn:
            case AudioEngine::kFxFrz: return { true,  true  };
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


        //  ==================================================================
        //  LOS SEIS DE CARACTER. Cuatro traen su propio eje y se escriben
        //  aqui, antes del bucle, como ya hacen DLY y BIT: su dibujo no es
        //  «una funcion de la columna» sino una simulacion, y meterla dentro
        //  del bucle la correria cuarenta y ocho veces para el mismo
        //  resultado. WID y EXC si son funciones de la frecuencia y van con
        //  los filtros, abajo.

        //  RNG: la PORTADORA, que es la unica forma que este efecto tiene. Su
        //  FREQ no cabe -es un tiempo, igual que el RATE de los cuatro de
        //  modulacion- asi que la ventana se mide en PERIODOS y lo que se ve
        //  es ANILLO: a 1 la portadora cruza el cero y a 0 no lo cruza nunca.
        //  Y sale de `Lfo::valorEn`, o sea de la misma funcion que multiplica
        //  en el hilo de audio.
        if (fx == AudioEngine::kFxRng)
        {
            const float an = juce::jlimit (0.0f, 1.0f, p1);
            for (int i = 0; i < kPuntos; ++i)
            {
                const float t = (float) i / (float) (kPuntos - 1);
                const float p = Lfo::valorEn (t * kPeriodosMod);
                const float g = an * p + (1.0f - an) * (0.5f + 0.5f * p);
                pon (i, 0.5f + 0.45f * g);
            }
            return;
        }

        //  PIT: el RECORRIDO DE LA CABEZA, que es lo unico que enseña los dos
        //  mandos a la vez. La pendiente es `1 - ratio` -o sea los semitonos-
        //  y el periodo del diente es el GRANO, que es exactamente la cuenta
        //  que corre en el motor. Con cero semitonos la pendiente es cero y
        //  sale una raya: eso es correcto, ahi el afinador no hace nada.
        if (fx == AudioEngine::kFxPit)
        {
            const float ratio = std::pow (2.0f, juce::jlimit (-12.0f, 12.0f, p0) / 12.0f);
            const float gran  = juce::jlimit (10.0f, (float) (AudioEngine::kPitGranoMax * 1000.0), p1);
            for (int i = 0; i < kPuntos; ++i)
            {
                const float ms = (float) i / (float) (kPuntos - 1) * kVentanaPit;
                float f = std::fmod (ms * (1.0f - ratio) / gran, 1.0f);
                if (f < 0.0f) f += 1.0f;
                pon (i, 0.05f + 0.90f * f);
            }
            return;
        }

        //  TRN: la envolvente de un golpe pasada por los MISMOS dos seguidores
        //  y la misma cuenta de ganancia que corre en el hilo de audio. No es
        //  una curva parecida escrita al lado: si algun dia cambian las cuatro
        //  constantes de tiempo, el dibujo va detras.
        if (fx == AudioEngine::kFxTrn)
        {
            constexpr double fsD = 48000.0;
            const int n = (int) (fsD * kVentanaTrn * 0.001);
            const float aRap = Dinamica::coefDe (1.0f,   fsD);
            const float rRap = Dinamica::coefDe (25.0f,  fsD);
            const float aLen = Dinamica::coefDe (35.0f,  fsD);
            const float rLen = Dinamica::coefDe (300.0f, fsD);
            float rapido = 0.0f, lento = 0.0f, tope = 1.0e-6f;

            std::array<float, kPuntos> env {};
            for (int k = 0; k < n; ++k)
            {
                //  Un golpe: ataque de dos milisegundos y caida de ciento
                //  veinte. Es la forma que un moldeador existe para tocar.
                const float ms = (float) k / (float) fsD * 1000.0f;
                const float x  = ms < 2.0f ? ms * 0.5f : std::exp (-(ms - 2.0f) / 120.0f);

                rapido = (x > rapido) ? aRap * rapido + (1.0f - aRap) * x
                                      : rRap * rapido + (1.0f - rRap) * x;
                lento  = (x > lento)  ? aLen * lento  + (1.0f - aLen) * x
                                      : rLen * lento  + (1.0f - rLen) * x;

                const float dif = juce::Decibels::gainToDecibels (rapido, -100.0f)
                                - juce::Decibels::gainToDecibels (lento,  -100.0f);
                const float dB  = juce::jlimit (-18.0f, 18.0f,
                                     juce::jlimit (-1.0f, 1.0f, p0) * juce::jmax (0.0f,  dif)
                                   + juce::jlimit (-1.0f, 1.0f, p1) * juce::jmax (0.0f, -dif));
                const float y = x * juce::Decibels::decibelsToGain (dB);

                const int i = juce::jlimit (0, kPuntos - 1,
                                            (int) ((float) k * (float) (kPuntos - 1) / (float) (n - 1)));
                env[(size_t) i] = juce::jmax (env[(size_t) i], std::abs (y));
                tope = juce::jmax (tope, std::abs (y));
            }
            //  Normalizado contra su propio pico: lo que este visor dice es la
            //  FORMA -cuanto pega el golpe contra lo que queda detras- y no el
            //  nivel, que ya lo dice el punto de trabajo.
            for (int i = 0; i < kPuntos; ++i) pon (i, env[(size_t) i] / tope);
            return;
        }

        //  FRZ: la ventana dando vueltas. Se pasa la envolvente de un sonido
        //  que decae y el dibujo enseña las dos cosas que el efecto hace: que
        //  el trozo se REPITE -el periodo es VENTANA- y que en la costura las
        //  dos mitades se cruzan, que es lo que SUAVE mueve.
        //
        //  Y su ventana no es la de DLY y REV: la lap mas larga son 500 ms, y
        //  con dos segundos la mas corta daria cien vueltas en cuarenta y ocho
        //  columnas. Un segundo son dos vueltas de la mas larga, que es lo
        //  minimo para que «se repite» se vea.
        if (fx == AudioEngine::kFxFrz)
        {
            const float vent  = juce::jlimit (20.0f, (float) (AudioEngine::kFrzVentanaMax * 1000.0), p0);
            const float suave = juce::jlimit (0.0f, 1.0f, p1);
            const float cruce = juce::jmax (1.0f, suave * vent * 0.25f);
            for (int i = 0; i < kPuntos; ++i)
            {
                const float ms = (float) i / (float) (kPuntos - 1) * kVentanaFrz;
                const float dentro = std::fmod (ms, vent);
                float y = std::exp (-dentro / 140.0f);
                //  En la costura suenan las dos mitades a la vez. Dos trozos
                //  sin relacion suman en potencia, o sea `sqrt((1-t)^2 + t^2)`,
                //  que baja a 0.707 en el medio: ese es el bache que SUAVE
                //  ensancha.
                if (dentro > vent - cruce)
                {
                    const float t = (vent - dentro) / cruce;
                    y *= std::sqrt (t * t + (1.0f - t) * (1.0f - t));
                }
                pon (i, 0.05f + 0.90f * y);
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

                //  WID: cuanto LADO sobrevive en cada frecuencia. Debajo del
                //  cruce vale cero -ahi el grave se suma a mono- y encima vale
                //  el ancho. El modulo del cruce sale de `svfDb` con la Q del
                //  cruce de `Dinamica`, que es la misma pieza que corre en el
                //  hilo de audio, y por DOS porque un Linkwitz-Riley son dos
                //  Butterworth en cascada.
                case AudioEngine::kFxWid:
                {
                    const float m = juce::Decibels::decibelsToGain (
                                      2.0f * AudioEngine::svfDb (hzDe (t), p1, 1.0f / Dinamica::kQ, true));
                    pon (i, 0.05f + 0.45f * juce::jlimit (0.0f, 2.0f, p0) * m);
                    break;
                }

                //  EXC: lo que se AÑADE encima de la banda. El renglon de
                //  abajo es la señal tal cual y lo que sube son los armonicos
                //  que el saturador mete en la banda alta — por eso la curva
                //  arranca en el suelo y no en el centro.
                case AudioEngine::kFxExc:
                {
                    const float m = juce::Decibels::decibelsToGain (
                                      2.0f * AudioEngine::svfDb (hzDe (t), p0, 1.0f / Dinamica::kQ, true));
                    pon (i, 0.12f + 0.80f * juce::jlimit (0.0f, 1.0f, p1) * m);
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

                //  MODULACION: el LFO, con la amplitud puesta por el segundo
            //  mando. La curva sale de `Lfo::valorEn` y no de un seno escrito
            //  aqui: si algun dia la forma deja de ser un seno, el dibujo va
            //  detras sin que nadie tenga que acordarse.
            case AudioEngine::kFxCho:
            case AudioEngine::kFxPha:
            case AudioEngine::kFxTrm:
            {
                const float amp = juce::jlimit (0.0f, 1.0f, p1);
                for (int i = 0; i < kPuntos; ++i)
                {
                    const float t = (float) i / (float) (kPuntos - 1);
                    pon (i, 0.5f + 0.45f * amp * Lfo::valorEn (t * kPeriodosMod));
                }
                break;
            }

            //  FLA lo mismo, pero su mando va de 0 a 1 y la realimentacion de
            //  -0.95 a +0.95, asi que el centro del recorrido es CERO y los dos
            //  extremos son profundos. Se dibuja el MODULO, que es lo que se
            //  oye: un peine con la realimentacion invertida no suena mas
            //  flojo, suena distinto.
            case AudioEngine::kFxFla:
            {
                const float amp = std::abs (juce::jlimit (0.0f, 1.0f, p1) * 2.0f - 1.0f);
                for (int i = 0; i < kPuntos; ++i)
                {
                    const float t = (float) i / (float) (kPuntos - 1);
                    pon (i, 0.5f + 0.45f * amp * Lfo::valorEn (t * kPeriodosMod));
                }
                break;
            }

            default: pon (i, 0.5f); break;
            }
        }
    }
}
