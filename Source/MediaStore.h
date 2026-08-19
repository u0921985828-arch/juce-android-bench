#pragma once

#include <JuceHeader.h>

// ============================================================================
//  MediaStore - como se deja un fichero donde CUALQUIER gestor lo vea.
//
//  El permiso de escritura no sirve para esto y hay que decirlo claro: desde
//  Android 10 el sistema IGNORA WRITE_EXTERNAL_STORAGE para el almacenamiento
//  compartido, y desde Android 11 no hay forma de recuperarlo. Pedirlo y
//  quedarse tan anchos seria teatro - el permiso sale concedido y la escritura
//  sigue fallando. Se pide igual, acotado a SDK 28, porque en los telefonos
//  viejos SI funciona y ahi es la via directa; pero de Android 10 en adelante
//  la unica puerta a la Musica compartida es esta.
//
//  Y es mejor puerta que la antigua, no un rodeo: no pide NINGUN permiso -meter
//  tu propio audio en el almacen de medios es un derecho de la app-, lo deja en
//  Music/, que es donde un musico lo busca, lo indexa el escaner de medios asi
//  que aparece tambien en los reproductores, y sobrevive a desinstalar la app.
//
//  Vacio en escritorio y en cualquier sistema que no conteste: quien llama
//  recurre a escribir el fichero y ya.
// ============================================================================
namespace MediaStore
{
    //  La version de Android, o 0 si no lo sabemos. Se pregunta porque de ella
    //  depende cual de las dos puertas existe.
    int sdk();

    //  Copia `local` al almacen de medios, dentro de Music/<subcarpeta>. Devuelve
    //  la ruta que la persona vera en su gestor, o vacio si no se pudo.
    //
    //  Copia y no mueve: el rebote ya esta escrito y comprobado cuando esto
    //  corre, asi que si el almacen de medios dice que no, lo peor que pasa es
    //  que el fichero se queda donde estaba. Un fallo aqui no puede costar el
    //  trabajo.
    juce::String publicar (const juce::File& local,
                           const juce::String& subcarpeta,
                           const juce::String& mime);
}
