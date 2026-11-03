#pragma once

#include <JuceHeader.h>

// ============================================================================
//  calidad# - LA LEY QUE MUEVE EL PELDANO DE CALIDAD, SEPARADA DE TODO.
//
//  El motor cuenta sus bloques (ver `AudioEngine::cargaPasadas`) y la cara
//  tiene un latido. Entre esas dos cosas hace falta una ley, y una ley que
//  vive dentro de un `if` del latido es una ley que no se puede medir: por eso
//  esto es una pieza suelta, sin reloj propio ni motor ni dispositivo, que
//  recibe milisegundos y dos cuentas y devuelve un peldano. `StressTest` la
//  corre con un guion sintetico y comprueba que no oscila.
//
//  LA FIGURA ES LA DE `checkXRuns` A PROPOSITO, porque el problema es el mismo
//  -corregir a la baja algo que se nota, y volver a probar sin quedarse
//  oscilando- y aprender dos leyes para el mismo problema es como escribir la
//  misma regla dos veces:
//
//    · Se juzga por VENTANAS de dos segundos, no bloque a bloque: un bloque
//      que se pasa porque el sistema se llevo el nucleo no es la app.
//    · Se BAJA un peldano en cuanto una ventana trae mas del 0.5 % de bloques
//      pasados. Medio por ciento a 128 muestras son ocho bloques por segundo,
//      o sea ocho huecos: eso ya se oye.
//    · Se SUBE tras cuarenta y cinco segundos limpios, y de uno en uno.
//    · Y un peldano que al subir volvio a crepitar queda DESCARTADO en esta
//      sesion -el `suelo`-, que es lo que evita el ir y venir eterno en el
//      telefono justo, que es justo el aparato para el que existe todo esto.
//
//  Lo que esta ley NO hace: elegir QUE se degrada. Eso lo decide el motor
//  (`AudioEngine::setCalidad`) y esta medido efecto por efecto en el banco.
// ============================================================================
struct LeyCalidad
{
    static constexpr int    kPeor        = 2;        // el peldano mas bajo que existe
    static constexpr double kVentanaMs   = 2000.0;   // cuanto dura una ventana
    static constexpr double kSubeMs      = 45000.0;  // tramo limpio para subir
    static constexpr double kGraciaMs    = 1000.0;   // al abrir el aparato
    static constexpr double kCastigoMs   = 10000.0;  // si crepita tan pronto tras subir, se descarta
    static constexpr double kFraccionMal = 0.005;    // 0.5 % de bloques pasados

    int    peldano = 0;      // 0 plena, 1 media, 2 baja
    int    suelo   = 0;      // el peldano mas alto al que se permite volver
    double graciaMs = kGraciaMs;
    double ventanaMs = 0.0;
    double limpioMs  = 0.0;
    double desdeSubioMs = -1.0;

    void arranca (int inicial) noexcept
    {
        peldano = juce::jlimit (0, kPeor, inicial);
        suelo = 0; graciaMs = kGraciaMs; ventanaMs = 0.0; limpioMs = 0.0; desdeSubioMs = -1.0;
    }

    //  `vistos` y `pasados` son las cuentas del motor DESDE LA ULTIMA VENTANA:
    //  quien llama las reinicia cuando esto devuelve true.
    //  Devuelve true si hay que reiniciar los contadores del motor.
    bool paso (double dtMs, int vistos, int pasados) noexcept
    {
        if (graciaMs > 0.0) { graciaMs -= dtMs; return true; }

        ventanaMs += dtMs;
        if (desdeSubioMs >= 0.0) desdeSubioMs += dtMs;
        if (ventanaMs < kVentanaMs) return false;
        ventanaMs = 0.0;

        if (vistos <= 0) return true;   // sin audio no se juzga nada

        const double frac = (double) pasados / (double) vistos;
        if (pasados >= 2 && frac > kFraccionMal)
        {
            limpioMs = 0.0;
            //  Si crepita justo despues de haber subido, el peldano de arriba
            //  queda descartado: en este aparato y con este proyecto no vale.
            if (desdeSubioMs >= 0.0 && desdeSubioMs < kCastigoMs)
                suelo = juce::jmin (kPeor, peldano + 1);
            desdeSubioMs = -1.0;
            peldano = juce::jmin (kPeor, peldano + 1);
            return true;
        }

        limpioMs += kVentanaMs;
        if (limpioMs >= kSubeMs && peldano > suelo)
        {
            limpioMs = 0.0;
            --peldano;
            desdeSubioMs = 0.0;
        }
        return true;
    }
};
