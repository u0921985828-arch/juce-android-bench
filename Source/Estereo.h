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
}
