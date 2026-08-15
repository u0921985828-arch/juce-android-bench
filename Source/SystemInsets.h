#pragma once

#include <JuceHeader.h>

// ============================================================================
//  SystemInsets - cuanto de la ventana esta pintando el sistema encima.
//
//  Desde Android 15 una app que apunta a API 35 no puede salirse del borde a
//  borde: la ventana es la pantalla entera y la barra de estado y la de
//  navegacion se pintan sobre ella. Nadie avisa. La cara sube debajo del reloj
//  y la linea de estado acaba debajo de la pildora de gestos.
//
//  JUCE da los margenes de zona segura en Android, pero solo el RECORTE de la
//  pantalla -la muesca- y no las barras, asi que por si solo no contesta a
//  esto.
//
//  Se le pregunta a la ventana, y solo de API 35 en adelante. En cualquier
//  version anterior el sistema ya coloca la ventana por debajo de las barras, y
//  restarlas otra vez se comeria por arriba el hueco de una segunda barra de
//  estado.
//
//  Fuera de Android devuelve cero, para que quien llama no tenga ramas por
//  plataforma.
// ============================================================================
namespace SystemInsets
{
    //  In logical (JUCE) units, not physical pixels.
    juce::BorderSize<int> get();
}
