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
    //  `uriOut`, si se pasa, recibe el `content://` de la fila publicada. Es lo
    //  unico que `comparte` necesita y no habia forma de sacarlo: la URI era una
    //  referencia local de JNI que moria al volver. Opcional a proposito — los
    //  que solo publican no cambian ni una linea.
    juce::String publicar (const juce::File& local,
                           const juce::String& subcarpeta,
                           const juce::String& mime,
                           juce::String* uriOut = nullptr);

    //  MANDARLO A OTRA APP, que es lo que convierte un rebote en algo que
    //  existe para los demas.
    //
    //  Exportabas y tenias que salir a un gestor de ficheros a buscarlo: para
    //  alguien que no es tecnico, un rebote que no se puede mandar por WhatsApp
    //  es un rebote que no existe. No habia `ACTION_SEND` en todo el proyecto.
    //
    //  Se comparte la URI del ALMACEN DE MEDIOS y no un fichero de la carpeta
    //  privada, que es lo que evita tener que declarar un `FileProvider`: esa
    //  fila ya es publica y legible por quien la reciba, asi que basta con
    //  pasarla. Por eso `publicar` tenia que devolverla.
    //
    //  Devuelve false si no se pudo —en escritorio siempre— y quien llama lo
    //  dice: un boton que no hace nada y no lo cuenta se lee como que la app
    //  esta rota.
    bool comparte (const juce::String& contentUri, const juce::String& mime);
}
