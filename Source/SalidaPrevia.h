#pragma once

#include <JuceHeader.h>

// ============================================================================
//  SalidaPrevia - lo que ANDROID sabe de como murio la vez anterior.
//
//  La caja negra (Bitacora.h) cuenta lo que la app alcanza a escribir, y hay
//  una forma de morir que no deja escribir nada: la captura del telefono traia
//  el «Zati Sampler no responde» y en la caja negra, como ultima linea, un
//  «ATASCO 1081 ms en pads/cargar» que ya habia TERMINADO - detras, ni un
//  «ATASCO >=», que el vigilante escribe al primer segundo de cualquier
//  parada. O el hilo de mensajes no estaba parado, o el vigilante se paro con
//  el. Desde la app no hay manera de saber cual.
//
//  Android si lo sabe. Desde la API 30 guarda por que murio cada proceso
//  (ApplicationExitInfo): el motivo -6 es ANR-, la frase del sistema -«Input
//  dispatching timed out ... no window has focus», por ejemplo, que es un ANR
//  con el hilo principal LIBRE- y, en un ANR, la pila de cada hilo en el
//  momento del cartel. Eso es lo que convierte la siguiente captura en un dato
//  y no en una hipotesis mas.
//
//  En escritorio no hay nada que leer; el banco inyecta un parte con
//  ZATI_SALIDA_PREVIA=<fichero> (la primera linea es el motivo, la segunda la
//  descripcion y el resto la traza) para medir el recorte sin Android.
// ============================================================================
namespace SalidaPrevia
{
    struct Parte
    {
        bool hay = false;
        int motivo = 0;               // ApplicationExitInfo.REASON_*
        juce::String descripcion;
        juce::String traza;           // solo en un ANR
    };

    //  JNI y lectura de la traza: fuera del hilo de mensajes.
    Parte lee();

    //  REASON_ANR es 6; el resto tambien tiene nombre para el renglon.
    juce::String nombreMotivo (int motivo);

    //  La cabeza del hilo "main" de una traza de ANR: sus primeras `lineas`
    //  lineas de pila (las «at ...» de Java y las «#NN pc ...» nativas). Es lo
    //  que dice DONDE estaba el hilo principal cuando salio el cartel. Pura, y
    //  por eso el banco la mide.
    juce::String cabezaDelMain (const juce::String& traza, int lineas = 8);
}
