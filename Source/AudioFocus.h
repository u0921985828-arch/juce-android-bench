#pragma once

#include <JuceHeader.h>

// ============================================================================
//  AudioFocus - preguntarle a Android si nos toca a nosotros hacer ruido.
//
//  Android reparte el altavoz entre las apps, y una que ni pide el foco ni
//  escucha cuando lo pierde se porta mal de dos formas que desde dentro no se
//  ven. Llega una llamada o una alarma y ZATI sigue sonando encima: muchas
//  compilaciones de fabricante nos callan o nos bajan sin decirlo, asi que los
//  medidores siguen moviendose mientras no sale nada, y eso se lee como "la app
//  esta rota". O otra app se lleva el foco en exclusiva y nuestro flujo se
//  queda mudo indefinidamente, porque nadie escucha el aviso que lo dice.
//
//  Lo que hacemos con cada perdida es el contrato de siempre:
//
//    LOSS (-1)                 el altavoz es de otro. Parar, y no volver solos.
//    LOSS_TRANSIENT (-2)       un aviso, una llamada. Pausa; se vuelve con GAIN.
//    LOSS_TRANSIENT_CAN_DUCK   podemos seguir sonando bajito por debajo, y eso
//                    (-3)      es exactamente lo que hacemos.
//
//                              Esto PARABA, con el argumento de que un sampler
//                              a un tercio de volumen no sirve para tocar. El
//                              argumento habla del aviso equivocado: CAN_DUCK
//                              es lo que manda una NOTIFICACION - una alerta de
//                              bateria, el tono de un mensaje - y dura un
//                              tercio de segundo. Contestarle parando el
//                              secuenciador y soltando el dispositivo convertia
//                              un tintineo en "la app se paro y no volvio", que
//                              es lo que pasaba al 5% de bateria, en mitad de
//                              una toma.
//
//                              Atenuar cuesta una multiplicacion. No se suelta
//                              nada, asi que no hay nada que tenga que
//                              sobrevivir a que lo reconstruyan.
//    GAIN (1)                  volver, pero solo si fuimos nosotros los que
//                              paramos.
//
//  Todo es API obsoleta que funciona: requestAudioFocus con un tipo de flujo y
//  no el constructor AudioFocusRequest de API 26. Ese constructor pide
//  fontaneria de AudioAttributes para comportamientos que no usamos, y la
//  llamada de tres argumentos se sigue atendiendo en el Android de hoy. Si eso
//  cambia alguna vez, cambia en una funcion.
//
//  Fuera de Android son dos llamadas vacias, para que quien llama no lleve
//  ramas por plataforma.
// ============================================================================
// ============================================================================
//  RutaAudio - por donde esta saliendo el sonido AHORA MISMO.
//
//  Vive en esta unidad y no en una suya porque la pregunta se le hace al MISMO
//  `android.media.AudioManager` que el foco, y esa clase se declara con un
//  `DECLARE_JNI_CLASS` que no se puede escribir dos veces: dos declaraciones de
//  la misma clase de Java son dos reglas, y la que se quede vieja seria una
//  llamada a una firma que ya no existe.
//
//  Existe por la monitorizacion directa (ver `AudioEngine::setMonitor`). Sin
//  cascos, sacar el microfono por el altavoz es un acople - y ademas la
//  produccion se cuela en la toma. Se le pregunta AL APARATO, no a una casilla
//  de buena fe, que es la misma regla que `canReallyWriteInto`: en Android la
//  mitad de las respuestas que uno se imagina no son la que da el sistema.
//
//  `getDevices` es de API 23 y `minSdk` es 24, asi que no hay que caer a
//  `isWiredHeadsetOn`, que ademas lleva obsoleta desde la 14 y no sabe nada de
//  Bluetooth ni de USB.
//
//  Fuera de Android devuelve false: en un escritorio no hay altavoz de
//  telefono que realimentar, y el banco fuerza la respuesta con `ZATI_RUTA`.
// ============================================================================
namespace RutaAudio
{
    //  true cuando NO hay ninguna salida de escucha personal enchufada, o sea
    //  cuando lo que suene va a ir al aire.
    bool porAltavoz();
}

class AudioFocus
{
public:
    //  Las dos se llaman en el hilo de mensajes, y ninguna puede tocar el audio
    //  directamente: devuelven el mando al dueno, que es quien decide.
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void audioFocusLost (bool permanently) = 0;
        //  Seguir sonando, bajito. No es una perdida: no para nada, no se suelta
        //  nada, y el nivel vuelve con el GAIN.
        virtual void audioFocusDucked() = 0;
        virtual void audioFocusGained() = 0;
    };

    explicit AudioFocus (Listener& l);
    ~AudioFocus();

    //  Devuelve false cuando Android nos lo niega. El flujo se abre igual: una
    //  negativa no es un fallo, y un instrumento mudo seria peor respuesta que
    //  uno al que el sistema resulta que esta atenuando.
    bool request();
    void abandon();

    bool isHeld() const noexcept { return held; }

private:
    Listener& listener;
    bool held = false;

   #if JUCE_ANDROID
    struct Impl;
    std::unique_ptr<Impl> impl;
   #endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioFocus)
};
