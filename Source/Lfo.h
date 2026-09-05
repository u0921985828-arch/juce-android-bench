#pragma once

#include <JuceHeader.h>

// ============================================================================
//  EL OSCILADOR LENTO, que es la pieza que este motor NO tenia.
//
//  Grepeado antes de escribirlo: en el hilo de audio no habia ni uno. Lo mas
//  cercano era el acumulador de fase del metronomo -un seno con envolvente, de
//  un disparo- y los LFO de `Sintes.cpp`, que son variables locales dentro del
//  gran `switch` de formas, corren OFFLINE en el hilo del cargador y en
//  `double`. Ninguno de los dos sirve aqui.
//
//  Y NACE COMPARTIDA, no cuando aparezca el segundo cliente. `valorEn` es
//  `static` desde la primera linea porque el VISOR tiene que dibujar la misma
//  forma que suena, y dibujar una forma parecida escrita al lado es
//  literalmente el fallo que la tanda anterior encontro en nueve de los once
//  visores -REV con tres constantes que no existen en la reverb, FLT con un
//  polo de 6 dB/oct contra un SVF de doce-. Es la leccion de `barridoDe`, de
//  `bajaDb` y de `Fdn::tauSegundos` aplicada ANTES de pagarla.
//
//  Contrato del hilo de audio: cero reservas, cero cerrojos, `float` plano.
// ============================================================================
struct Lfo
{
    //  La forma, en 0..1 de fase y -1..+1 de valor. Estatica y sin estado: la
    //  llaman el hilo de audio y `FxVisor::muestrea`, que es de lo que se
    //  trata.
    static float valorEn (float fase) noexcept
    {
        return std::sin (juce::MathConstants<float>::twoPi * (fase - std::floor (fase)));
    }

    //  CUADRATURA Y NO DOS OSCILADORES, que es lo que hace ancho un coro. Con
    //  un LFO por canal las dos fases se separan con el tiempo -acumulan su
    //  propio error y arrancan donde les toque- y el ancho deja de ser el que
    //  se puso. Con una sola fase y un cuarto de vuelta de diferencia, la
    //  relacion entre los dos canales es la misma en la muestra uno y en la
    //  del minuto cuarenta.
    static constexpr float kCuadratura = 0.25f;

    void ponPaso (float hz, double fs) noexcept
    {
        paso = (float) (juce::jlimit (0.0f, 40.0f, hz) / juce::jmax (8000.0, fs));
    }

    //  Devuelve el valor de AHORA y deja la fase en la muestra siguiente. La
    //  fase se envuelve restando y no con `fmod`: a 0.05 Hz son veinte
    //  segundos de acumulacion en `float`, y restar uno no pierde precision
    //  donde una division la perderia.
    float avanza() noexcept
    {
        const float v = valorEn (fase);
        fase += paso;
        if (fase >= 1.0f) fase -= 1.0f;
        return v;
    }

    float enCuadratura() const noexcept { return valorEn (fase + kCuadratura); }

    void reinicia() noexcept { fase = 0.0f; }

    float fase = 0.0f;
    float paso = 0.0f;
};
