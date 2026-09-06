#pragma once

#include <JuceHeader.h>
#include <csignal>
#include <cstring>

#if ! JUCE_WINDOWS
 #include <unistd.h>
 #include <fcntl.h>
#endif

// ============================================================================
//  Bitacora — la caja negra.
//
//  Existe porque hay una clase de fallo que este proyecto no puede medir: el
//  que solo pasa en el telefono. La exportacion cerraba la app en un movil y
//  aqui, en el banco, sale entera por los cuatro caminos - a pelo, con hilo,
//  con el temporizador y cancelandola a mitad -, con la cancion mas larga que
//  la app admite y con el monton limitado a 64 MB. Cuando el escritorio dice
//  que todo esta bien y el aparato se cierra, la unica salida es que sea LA APP
//  quien cuente donde estaba.
//
//  Como funciona, y por que asi:
//
//  · Un fichero de texto en la carpeta ZATI, no un log del sistema. El log de
//    Android se lo lleva el reinicio y hace falta un cable y un ordenador para
//    leerlo; esto se abre desde el propio telefono.
//
//  · El ultimo paso vive en un buffer FIJO de caracteres, no en un String. Un
//    manejador de senal no puede reservar memoria ni tomar un cerrojo: si la
//    app se esta cayendo por corromper el monton, pedirle memoria al monton es
//    como se pierde la unica linea que importaba. Por lo mismo el descriptor se
//    abre al arrancar y se escribe con write(2), que es de las poquisimas
//    llamadas que se pueden hacer dentro de una senal.
//
//  · Y se anota tambien la SALIDA LIMPIA. Sin esa linea no hay forma de
//    distinguir "se cerro a la mitad" de "se cerro bien y el ultimo paso fue
//    ese": la ausencia del final es lo que convierte el ultimo paso en un
//    culpable.
// ============================================================================
namespace Bitacora
{
    //  96 caracteres es de sobra para "exportar/pista 41" y cabe en cualquier
    //  pila. Fijo a proposito: ver arriba.
    inline char ultimo[96] = "arranque";
    inline int  salida = -1;          // descriptor abierto una vez
    inline juce::File fichero;
    inline juce::String previa;       // como acabo la vez anterior, para contarlo

    inline void escribe (const char* texto) noexcept
    {
       #if ! JUCE_WINDOWS
        if (salida >= 0 && texto != nullptr)
        {
            const auto n = std::strlen (texto);
            if (::write (salida, texto, n) < 0) {}     // en una senal no hay a quien quejarse
        }
       #else
        juce::ignoreUnused (texto);
       #endif
    }

    inline void paso (const char* que) noexcept
    {
        if (que == nullptr) return;
        std::strncpy (ultimo, que, sizeof (ultimo) - 1);
        ultimo[sizeof (ultimo) - 1] = 0;
        escribe (ultimo);
        escribe ("\n");
    }

   #if ! JUCE_WINDOWS
    extern "C" inline void alCaer (int sig) noexcept
    {
        escribe ("CAIDA senal ");
        char n[4] = { (char) ('0' + (sig / 10) % 10), (char) ('0' + sig % 10), ' ', 0 };
        escribe (n);
        escribe ("en ");
        escribe (ultimo);
        escribe ("\n");

        //  Y se vuelve a levantar con el manejador por defecto, para que el
        //  sistema haga su parte - el volcado, el aviso - y el codigo de salida
        //  siga siendo el de una caida. Tragarse la senal seria dejar la app
        //  en un estado imposible fingiendo que no ha pasado nada.
        std::signal (sig, SIG_DFL);
        std::raise (sig);
    }
   #endif

    //  Se llama una vez, al arrancar, antes de construir nada.
    inline void instalar (const juce::File& carpeta)
    {
        fichero = carpeta.getChildFile ("zati-bitacora.txt");

        //  Lo de la vez anterior se lee ANTES de tocar el fichero, y solo
        //  interesa la ultima linea: si no dice "fin limpio", la vez pasada se
        //  cerro sola y ahi esta el sitio.
        if (fichero.existsAsFile())
        {
            const auto texto = fichero.loadFileAsString().trim();
            const auto ultimaLinea = texto.fromLastOccurrenceOf ("\n", false, false).trim();
            if (ultimaLinea.isNotEmpty() && ! ultimaLinea.startsWith ("fin limpio"))
                previa = ultimaLinea;
        }

        //  El fichero se rehace en cada arranque: lo que hace falta es la
        //  sesion de AHORA y la ultima linea de la anterior, no un historico
        //  que crece sin freno en la carpeta de la persona.
        fichero.replaceWithText ({});

       #if ! JUCE_WINDOWS
        salida = ::open (fichero.getFullPathName().toRawUTF8(), O_WRONLY | O_APPEND | O_CREAT, 0644);

        for (int s : { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT })
            std::signal (s, alCaer);
       #endif

        //  Y las excepciones que nadie coge, que en Android tambien cierran la
        //  app y no son una senal hasta que abort() las convierte en una.
        std::set_terminate ([]
        {
            escribe ("EXCEPCION sin coger en ");
            escribe (ultimo);
            escribe ("\n");
            std::abort();
        });

        paso ("arranque");
    }

    //  LA SALIDA LIMPIA SE ESCRIBE DONDE ANDROID DE VERDAD SE VA.
    //
    //  Tenia un solo llamante -`Main.cpp::shutdown()`- y en Android ese camino
    //  casi nunca corre: con `targetSDK 36` el boton ATRAS manda la tarea al
    //  fondo sin destruir la actividad, asi que solo llega `onPause`, y la
    //  muerte posterior del proceso es un SIGKILL que ni se puede capturar. O
    //  sea que CUALQUIER salida normal dejaba el fichero sin esta linea y el
    //  arranque siguiente enseñaba «La vez anterior se cerro en: ...» encima de
    //  la linea de sesion recuperada. Un parte de caida que sale siempre no es
    //  un parte: es ruido, y ademas entrena a no leer el que importe.
    //
    //  El tell que lo separa de una caida de verdad sigue intacto: `alCaer`
    //  escribe CON PREFIJO -«CAIDA senal 11 en ...»- asi que un paso desnudo
    //  nunca fue una caida.
    inline void finLimpio() noexcept { paso ("fin limpio"); }

    //  Y VOLVER A PRIMER PLANO ANOTA UN PASO, que es la otra mitad.
    //
    //  Sin esto, «fin limpio» se quedaria como ultima linea mientras la app
    //  sigue viva delante de la persona, y una caida DESPUES de reanudar se
    //  leeria como un cierre correcto. Es el mismo fallo por el otro lado: un
    //  mecanismo que no avisa nunca pasa la mitad de la prueba que uno que
    //  avisa siempre.
    inline void reanudada() noexcept { paso ("reanudada"); }
}
