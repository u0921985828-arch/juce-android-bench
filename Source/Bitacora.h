#pragma once

#include <JuceHeader.h>
#include <atomic>
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

    //  UNA LINEA, UNA LLAMADA A write(2).
    //
    //  Escribir el texto y el salto por separado eran dos llamadas, y desde que
    //  el vigilante de atascos escribe en el MISMO descriptor desde OTRO hilo,
    //  dos llamadas son dos sitios por los que se puede colar media linea
    //  ajena. Con O_APPEND una sola escritura corta es atomica; dos no lo son.
    inline void linea (const char* a, const char* b = nullptr,
                       const char* c = nullptr, const char* d = nullptr) noexcept
    {
       #if ! JUCE_WINDOWS
        if (salida < 0) return;
        char buf[224];
        size_t n = 0;
        const auto pega = [&buf, &n] (const char* t)
        {
            if (t == nullptr) return;
            while (*t != 0 && n < sizeof (buf) - 2) buf[n++] = *t++;
        };
        pega (a); pega (b); pega (c); pega (d);
        buf[n++] = '\n';
        if (::write (salida, buf, n) < 0) {}
       #else
        juce::ignoreUnused (a, b, c, d);
       #endif
    }

    //  Un entero a texto sin reservar nada: esto lo llama un hilo que puede
    //  estar corriendo mientras la app se cae.
    inline void cifra (int v, char* salidaTexto, size_t tope) noexcept
    {
        if (tope == 0) return;
        if (v < 0) v = 0;
        char alReves[12];
        int k = 0;
        do { alReves[k++] = (char) ('0' + v % 10); v /= 10; } while (v > 0 && k < 11);
        size_t n = 0;
        while (k > 0 && n < tope - 1) salidaTexto[n++] = alReves[--k];
        salidaTexto[n] = 0;
    }

    inline void paso (const char* que) noexcept
    {
        if (que == nullptr) return;
        std::strncpy (ultimo, que, sizeof (ultimo) - 1);
        ultimo[sizeof (ultimo) - 1] = 0;
        linea (ultimo);
    }

    // ------------------------------------------------------------------------
    //  EL ATASCO DEL HILO DE MENSAJES, QUE ES LO QUE ANDROID LLAMA «NO RESPONDE».
    //
    //  Esta caja negra sabia decir DONDE se cayo la app y no sabia decir nada de
    //  cuando la app no se cae: se queda quieta. Y eso es lo que salio dos veces
    //  en el telefono -«Zati Sampler no responde · Esperar / Aceptar»- la segunda
    //  con el audio SONANDO, medidor a -11 dB y forma de onda viva, o sea con el
    //  hilo de audio intacto y el de mensajes parado. La tanda anterior dedujo la
    //  causa LEYENDO CODIGO a partir de una captura, acerto en un fallo real
    //  -la sonda se salia del bufer de AAudio- y no acerto en el sintoma, porque
    //  una captura de pantalla no es una medida. Esto es lo que convierte la
    //  siguiente captura en un dato.
    //
    //  Como: el hilo de mensajes deja un LATIDO en cada vuelta del temporizador
    //  y una etiqueta con lo que esta haciendo; un hilo aparte mira el reloj cada
    //  decima de segundo y, en cuanto el latido se pasa del plazo, escribe el
    //  parte. Se escribe MIENTRAS el atasco dura y no despues, que es la mitad
    //  que importa: si la persona da a «Aceptar» el sistema mata el proceso con
    //  un SIGKILL y lo que no estuviera en disco no existe.
    //
    //  La etiqueta es un `const char*` a un literal y no un String: ponerla
    //  cuesta una escritura de puntero, asi que puede estar en los sitios
    //  calientes sin pagar nada, y leerla desde otro hilo mientras la app agoniza
    //  no toca el monton.
    // ------------------------------------------------------------------------
    inline std::atomic<juce::uint32> latido { 0 };
    inline std::atomic<const char*>  tarea  { "arranque" };
    inline std::atomic<bool>         avisado { false };
    inline std::atomic<int>          peorMs { 0 };
    inline char peorDonde[64] = "";
    inline char dondeAtasco[64] = "";
    //  Un segundo. Android cuenta cinco para dar la app por colgada, asi que
    //  esto avisa con cuatro de margen: lo que interesa apuntar no es el atasco
    //  que ya se vio, es el que iba camino de verse.
    inline int umbralMs = 1000;
    //  Y lo peor de la vez anterior, para poder enseñarlo sin pedirle a nadie
    //  que abra un fichero de texto en el telefono.
    inline juce::String atascoPrevio;

    inline void apuntaAtasco (const char* prefijo, int ms, const char* donde) noexcept
    {
        char num[12];
        cifra (ms, num, sizeof (num));
        linea (prefijo, num, " ms en ", donde != nullptr ? donde : "?");
    }

    //  El hilo de mensajes, una vez por vuelta del temporizador.
    inline void late() noexcept
    {
        const auto ahora = juce::Time::getMillisecondCounter();
        const auto antes = latido.exchange (ahora, std::memory_order_acq_rel);
        if (antes == 0) return;

        const int hueco = (int) (ahora - antes);
        if (hueco < umbralMs) return;

        //  La cifra EXACTA y la etiqueta que el vigilante latcheo al empezar:
        //  aqui `tarea` ya ha vuelto a lo que sea que se este haciendo ahora, y
        //  apuntar esa seria apuntar al testigo en vez de al culpable.
        const char* donde = avisado.load (std::memory_order_acquire) && dondeAtasco[0] != 0
                              ? dondeAtasco
                              : tarea.load (std::memory_order_acquire);
        apuntaAtasco ("ATASCO ", hueco, donde);

        if (hueco > peorMs.load (std::memory_order_relaxed))
        {
            peorMs.store (hueco, std::memory_order_relaxed);
            std::strncpy (peorDonde, donde != nullptr ? donde : "?", sizeof (peorDonde) - 1);
            peorDonde[sizeof (peorDonde) - 1] = 0;
        }

        avisado.store (false, std::memory_order_release);
    }

    //  LA ETIQUETA, COMO AMBITO. Se pone al entrar y se devuelve al salir, para
    //  que un tramo caro dentro de otro no deje la de dentro puesta para
    //  siempre.
    struct Tarea
    {
        explicit Tarea (const char* que) noexcept
            : antes (tarea.exchange (que, std::memory_order_acq_rel)) {}
        ~Tarea() { tarea.store (antes, std::memory_order_release); }
        Tarea (const Tarea&) = delete;
        Tarea& operator= (const Tarea&) = delete;
        const char* antes;
    };

    struct Vigilante final : juce::Thread
    {
        Vigilante() : juce::Thread ("zati-atasco") {}
        ~Vigilante() override { stopThread (500); }

        void run() override
        {
            while (! threadShouldExit())
            {
                wait (100);

                const auto ultimo = latido.load (std::memory_order_acquire);
                if (ultimo == 0) continue;          // el hilo de mensajes no ha latido aun

                const int hueco = (int) (juce::Time::getMillisecondCounter() - ultimo);

                if (hueco < umbralMs || avisado.load (std::memory_order_acquire))
                    continue;

                const char* donde = tarea.load (std::memory_order_acquire);
                std::strncpy (dondeAtasco, donde != nullptr ? donde : "?",
                              sizeof (dondeAtasco) - 1);
                dondeAtasco[sizeof (dondeAtasco) - 1] = 0;
                avisado.store (true, std::memory_order_release);

                //  EN CURSO, con el «>=» delante: la cifra final no se sabe aun
                //  -el atasco sigue- y la que importa es que esta linea este en
                //  disco antes de que alguien pulse «Aceptar».
                apuntaAtasco ("ATASCO >= ", hueco, dondeAtasco);
            }
        }
    };

    inline std::unique_ptr<Vigilante> vigilante;

   #if ! JUCE_WINDOWS
    extern "C" inline void alCaer (int sig) noexcept
    {
        char n[12];
        cifra (sig, n, sizeof (n));
        linea ("CAIDA senal ", n, " en ", ultimo);

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

            //  Y EL PEOR ATASCO DE LA VEZ ANTERIOR, que es lo que una app que
            //  no se cae -se queda quieta- deja como unico rastro. Se busca el
            //  mayor y no el ultimo: lo que hace falta saber es cual fue el
            //  peor, y el ultimo suele ser el que provoco el «Aceptar».
            int peorPrevio = 0;
            for (const auto& l : juce::StringArray::fromLines (texto))
            {
                if (! l.startsWith ("ATASCO")) continue;
                const auto n = l.retainCharacters ("0123456789 ").trim()
                                .upToFirstOccurrenceOf (" ", false, false).getIntValue();
                if (n > peorPrevio) { peorPrevio = n; atascoPrevio = l.trim(); }
            }
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

        //  Y EL PLAZO, ajustable para el banco: `Tests/atasco.py` necesita
        //  poder pedir uno corto sin tener que quedarse tres segundos parado
        //  por cada corrida, y subirlo a un numero enorme es la rotura a
        //  proposito que comprueba que esta regla puede fallar.
        if (const auto* u = std::getenv ("ZATI_ATASCO_MS"))
            if (const int v = juce::String (u).getIntValue(); v > 0)
                umbralMs = v;

        vigilante = std::make_unique<Vigilante>();
        vigilante->startThread (juce::Thread::Priority::low);
    }

    //  Y SE PARA DONDE SE PARA TODO. Un hilo que sigue mirando un descriptor
    //  que ya se cerro escribe en el numero de otro.
    inline void para() noexcept
    {
        if (vigilante != nullptr) { vigilante->stopThread (500); vigilante.reset(); }
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
    inline void finLimpio() noexcept
    {
        paso ("fin limpio");
        //  Y EL LATIDO SE PARA CON LA APP. En segundo plano Android congela el
        //  proceso -el «freezer» de las apps en cache- y el temporizador deja
        //  de latir minutos enteros. Sin esto, al descongelar, el vigilante
        //  veia un hueco de minutos, apuntaba un atasco que no lo era y se
        //  quedaba ENGANCHADO en `avisado`, que solo suelta `late()`. Cero es
        //  «no ha latido aun» y el vigilante no mira.
        latido.store (0, std::memory_order_release);
        avisado.store (false, std::memory_order_release);
    }

    //  Y VOLVER A PRIMER PLANO ANOTA UN PASO, que es la otra mitad.
    //
    //  Sin esto, «fin limpio» se quedaria como ultima linea mientras la app
    //  sigue viva delante de la persona, y una caida DESPUES de reanudar se
    //  leeria como un cierre correcto. Es el mismo fallo por el otro lado: un
    //  mecanismo que no avisa nunca pasa la mitad de la prueba que uno que
    //  avisa siempre.
    //
    //  Y EL RELOJ DEL VIGILANTE EMPIEZA AQUI, no en el ultimo latido de antes
    //  de irse. Medido en la captura: la ultima linea de la vez anterior era
    //  «reanudada» y detras NADA, ni un «ATASCO», con la app muerta por un
    //  «no responde». Es la firma exacta del enganche: el vigilante habia
    //  gastado su aviso en el hueco falso del congelador y el atasco de verdad
    //  -abrir el dispositivo al volver, dentro de esta misma llamada- no lo
    //  apunto nadie. Con esto lo que tarde `appResumed` se cuenta desde cero y
    //  queda escrito con su etiqueta.
    inline void reanudada() noexcept
    {
        latido.store (juce::Time::getMillisecondCounter(), std::memory_order_release);
        avisado.store (false, std::memory_order_release);
        paso ("reanudada");
    }
}
