#pragma once

#include <JuceHeader.h>

// ============================================================================
//  AppStorage - la unica carpeta de un movil que podemos escribir nosotros y
//  alcanzar la persona.
//
//  El almacenamiento por ambitos deja tres sitios donde poner cosas y solo uno
//  sirve para un sampler. La Musica compartida es donde un musico buscaria sus
//  sonidos, y donde un Android moderno no nos deja escribir sin pasar por
//  MediaStore: ProjectStore la sondea escribiendo un fichero de verdad, y en la
//  mayoria de los telefonos de hoy la sonda falla. Los datos INTERNOS de la app
//  (/data/user/0/...) siempre se pueden escribir y no los ve nadie - ni un
//  gestor de ficheros, ni un cable, ni la persona - y una biblioteca donde no
//  se pueden meter muestras no es una biblioteca. Quedan los ficheros EXTERNOS
//  de la app (Android/data/<paquete>/files), que se escriben sin permiso
//  ninguno y aparecen por USB desde un ordenador, que es como llega de verdad
//  un pack a un telefono.
//
//  Asi que este es el escalon de en medio, y es donde tiene que apoyarse la
//  biblioteca siempre que la Musica compartida nos cierre la puerta.
//
//  Vacio en escritorio y en cualquier sistema que no conteste: quien llama
//  recurre a lo de siempre.
// ============================================================================
namespace AppStorage
{
    juce::File externalFilesDir();
}
