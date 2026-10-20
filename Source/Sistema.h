#pragma once

#include <JuceHeader.h>

//  LO QUE EL SISTEMA YA DECIDIO POR LA PERSONA, leido en vez de supuesto
//  (accesibilidad #28, #29 y #31): cuanto dura una pulsacion larga, si quiere
//  animaciones y donde vive el gesto de atras. Fuera de Android contesta lo de
//  siempre, y `ZATI_SISTEMA` lo convierte en ENTRADA para el banco, como
//  `ZATI_INSETS` con los margenes: «pulsacion=900;animacion=0;gestos=0,30,0,30».
namespace Sistema
{
    //  ViewConfiguration.getLongPressTimeout(), acotado a 200..1500 ms.
    int pulsacionLargaMs (int siNoContesta);

    //  Settings.Global ANIMATOR_DURATION_SCALE: 0 es «quitar animaciones».
    float escalaAnimaciones();

    //  WindowInsets.Type.systemGestures(), en unidades logicas (arriba, izquierda, abajo, derecha).
    juce::BorderSize<int> gestos();

    //  View.setSystemGestureExclusionRects, en unidades logicas; el banco lee lo ultimo pedido.
    void excluyeGestos (const juce::Array<juce::Rectangle<int>>& zonas);
    juce::Array<juce::Rectangle<int>> ultimaExclusion();
}
