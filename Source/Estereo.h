#pragma once

#include <JuceHeader.h>

// ============================================================================
//  MEDIO / LADO, escrito UNA vez.
//
//  Estaba en linea y DOS veces dentro de `Voice.h` —una en la rama de TONO y
//  otra en la normal, cuatro lineas identicas en dos bucles de muestra— y con
//  el efecto WID habria sido la tercera. Es la razon de siempre: una regla
//  escrita tres veces son tres reglas, y la tercera es la que un dia se
//  escribe con el signo cambiado.
//
//  Y LA FORMULA ES ESTA Y NO OTRA: `M = (L+R)/2`, `S = (L-R)/2`, se escala S y
//  se rehace. Es la unica forma de estrechar SIN MOVER EL CENTRO — bajar un
//  canal y subir el otro estrecha y ademas desplaza, que es lo que hace un pan
//  y no lo que hace un ancho. Medido en su dia con DOS cifras: cuanto LADO
//  queda (0.00000 / 0.08654 / 0.17307) y cuanto CENTRO sobrevive (0.08683 en
//  las tres) — solo lo primero lo cumple un fader y solo lo segundo lo cumple
//  no hacer nada.
// ============================================================================
namespace Estereo
{
    //  `w` es el ancho: 0 es mono, 1 «como viene», 2 el doble de lado.
    inline void ancho (float& l, float& r, float w) noexcept
    {
        const float m = 0.5f * (l + r);
        const float s = 0.5f * (l - r) * w;
        l = m + s;
        r = m - s;
    }

    //  ABRIR UNA MUESTRA MONO, que no trae lado que escalar.
    //
    //  Del telefono: «el switch de estereo a mono no se si funciona muy
    //  bien» -«con una mono, todas son mono»-. No podia: con L == R el lado
    //  es cero y `ancho` multiplica cero, asi que el ST y el mando ANCHO no
    //  hacian nada en la muestra de un canal, que es la de la persona. El
    //  lado se FABRICA: una copia del propio sonido retardada `kAbreMs`
    //  -la linea vive en Voice, en muestras del aparato- y a `kLadoMono`
    //  por unidad de ancho, puesta como S sobre el M que ya habia:
    //  L = M + S, R = M - S. Es el pseudo-estereo de siempre, elegido por
    //  dos cosas que se miden: la suma mono es EXACTAMENTE la señal de
    //  antes -S se suma a un lado y se resta al otro-, asi que un altavoz
    //  mono no oye nada nuevo; y el centro no se mueve, la misma regla que
    //  `ancho`. A cero no hace nada, bit a bit: el ST apagado es la muestra
    //  tal cual, y es como suena todo lo que no se abra.
    //
    //  `kLadoMono` 0.5: con ruido la correlacion entre L y R vale
    //  (1 - k^2) / (1 + k^2), 0.60 en el uno y 0.00 en el dos, y cada lado
    //  es un peine de +3.5 / -6 dB complementario al otro. Y diez
    //  milisegundos: los dientes del peine caen cada 100 Hz -por encima de
    //  eso se oye como color y no como sitio- y la copia llega antes del
    //  limite en que un golpe se oye dos veces. Canales.py mide la cifra.
    static constexpr float  kLadoMono = 0.5f;
    static constexpr double kAbreMs   = 10.0;
    inline void abre (float& l, float& r, float w, float retardada) noexcept
    {
        const float s = kLadoMono * w * retardada;
        r = l - s;
        l = l + s;
    }
}
