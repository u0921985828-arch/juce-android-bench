#pragma once

#include <JuceHeader.h>
#include "Lang.h"

#include <array>
#include <atomic>
#include <cstring>

#if JUCE_ANDROID
 #include <sys/system_properties.h>
 #include <dlfcn.h>
#endif

// ============================================================================
//  AudioPath - nos da este telefono el carril rapido, si o no?
//
//  JUCE ya le pide a Oboe SharingMode::Exclusive y PerformanceMode::LowLatency
//  en el flujo de salida de verdad (juce_Oboe_android.cpp), asi que no queda
//  ningun ajuste apagado en la app. La pregunta es si ANDROID LO CONCEDE, y eso
//  lo decide el aparato y no nosotros.
//
//  La linea que separa es el MMAP de AAudio. Con el, la app comparte un buffer
//  circular directamente con el hardware y un burst de 256 sale por 10-20 ms.
//  Sin el, la misma API pasa por AudioFlinger, el mezclador del sistema, y ese
//  mismo burst cuesta 40-60. Los 42 ms que no sabiamos explicar viven
//  exactamente ahi.
//
//  Que el MMAP exista siquiera es una propiedad del sistema. Leerla convierte
//  "la latencia es alta y no sabemos por que" en una de dos respuestas: el
//  telefono no puede, y ninguna version de esta app lo va a cambiar; o si
//  puede, y algo lo esta bloqueando.
// ============================================================================
namespace AudioPath
{
    enum class Mmap { Unknown, Never, Auto, Always };

    inline juce::String readProperty (const char* name)
    {
       #if JUCE_ANDROID
        char value[PROP_VALUE_MAX + 1] = {};
        if (__system_property_get (name, value) > 0)
            return juce::String (value);
       #else
        juce::ignoreUnused (name);
       #endif
        return {};
    }

    //  AAUDIO_POLICY_NEVER = 1, _AUTO = 2, _ALWAYS = 3 (ver aaudio_policy_t).
    //  Los fabricantes publican la misma respuesta bajo su propia clave, asi que
    //  se consultan las dos antes de rendirse.
    inline Mmap policyFrom (std::initializer_list<const char*> keys)
    {
        for (auto* key : keys)
        {
            const auto v = readProperty (key).trim();
            if (v.isNotEmpty())
            {
                const int p = v.getIntValue();
                if (p == 1) return Mmap::Never;
                if (p == 2) return Mmap::Auto;
                if (p == 3) return Mmap::Always;
            }
        }
        return Mmap::Unknown;
    }

    inline Mmap mmapPolicy()
    {
        return policyFrom ({ "aaudio.mmap_policy",
                             "persist.vendor.audio.aaudio.mmap_policy",
                             "ro.vendor.audio.aaudio.mmap_policy" });
    }

    //  Otro interruptor, y el que de verdad decide. Un aparato puede anunciar
    //  MMAP con politica AUTO y traer exclusive_policy NEVER, que quiere decir
    //  MMAP solo en modo compartido: ninguna app del telefono consigue un
    //  extremo exclusivo, y ninguna peticion nuestra lo va a cambiar.
    inline Mmap exclusivePolicy()
    {
        return policyFrom ({ "aaudio.mmap_exclusive_policy",
                             "persist.vendor.audio.aaudio.mmap_exclusive_policy",
                             "ro.vendor.audio.aaudio.mmap_exclusive_policy" });
    }

    inline juce::String describe (Mmap m)
    {
        switch (m)
        {
            case Mmap::Never:  return T ("no soportado");
            case Mmap::Auto:   return T ("disponible");
            case Mmap::Always: return T ("forzado");
            default:           return T ("desconocido");
        }
    }

    // ========================================================================
    //  La propiedad de arriba dice lo que el telefono SOPORTA. Con politica
    //  AUTO - que es lo que informa este aparato - soportar no es conceder:
    //  Android decide flujo a flujo, y puede negarnos el nuestro mientras sigue
    //  diciendo "disponible" tan tranquilo. Leer una propiedad, por tanto, no
    //  puede decirnos si estamos en el carril rapido; solo abrir un flujo puede.
    //
    //  Asi que se abre uno. Antes de que exista el dispositivo de audio de
    //  verdad, se le pide a AAudio directamente EXCLUSIVE + LOW_LATENCY y se
    //  mira lo que vuelve. AAudio no miente en esto: si el flujo devuelto dice
    //  EXCLUSIVE es un flujo MMAP compartiendo un buffer circular con el
    //  hardware, y si dice SHARED nos ha tocado AudioFlinger y su mezclador, que
    //  es donde viven los 40 ms sin explicar.
    //
    //  La sonda repite la apertura por usos, porque el uso es lo unico que
    //  controla una app y que cambia la respuesta. Un flujo MEDIA en un Xiaomi
    //  se mete en la cadena de posproceso del fabricante -Dolby, el ecualizador
    //  del sistema- y el posproceso obliga al camino del mezclador; GAME
    //  normalmente lo esquiva. Si GAME consigue EXCLUSIVE y MEDIA no, el arreglo
    //  es una llamada del constructor, y los campos de abajo se la llevan al
    //  flujo de verdad.
    //
    //  Todo pasa por dlopen: libaaudio solo existe desde API 26 y la app
    //  soporta la 24.
    // ========================================================================
    enum : int
    {
        kUsageMedia = 1,   // AAUDIO_USAGE_MEDIA
        kUsageGame  = 14   // AAUDIO_USAGE_GAME
    };

    //  SEIS INTENTOS, Y LA TABLA ENTERA SE PUBLICA.
    //
    //  Antes solo sobrevivia la conclusion -«compartida MEZCLADOR»- y con el
    //  telefono delante eso no es una respuesta sino el principio de cinco
    //  tandas a ciegas: no se sabia si el aparato habia negado los seis, si
    //  habia concedido el tercero y lo habia tumbado el START, o si libaaudio
    //  ni siquiera traia el simbolo. La medida de campo decia `via compartida
    //  MEZCLADOR` con `mmap disponible · excl disponible`, o sea que el aparato
    //  concede el carril y la app no lo coge, y no habia ni un dato mas.
    //  Son seis renglones y valen una semana.
    static constexpr int kIntentos = 6;

    struct Intento
    {
        int  usage     = 0;      // lo que se pidio; 0 = los terminos del aparato
        int  pidio     = 0;      // formato pedido: 0 cualquiera, 1 i16, 2 float
        bool abrio     = false;
        bool arranco   = false;  // ...y AAudio lo llevo a STARTED
        bool exclusiva = false;
        bool mmap      = false;
        bool baja      = false;  // LOW_LATENCY concedido
        bool i16       = false;  // el formato que CONCEDIO
        int  burst     = 0;
        int  capacity  = 0;
        int  canales   = 0;
        int  rate      = 0;
        int  error     = 0;      // el codigo de AAudio que lo tumbo, 0 si ninguno
    };

    struct Fast
    {
        bool ran        = false;  // libaaudio was there and a stream opened
        bool exclusive  = false;  // Android granted MMAP
        bool mmapKnown  = false;  // the hidden symbol was there to ask
        bool mmapUsed   = false;  // ...and said we are on an MMAP ring buffer
        bool lowLatency = false;
        bool useI16     = false;  // exclusivity needed 16-bit
        int  usage      = 0;      // the usage that won, 0 = none did
        int  burst      = 0;
        int  capacity   = 0;
        int  channels   = 0;      // what the granted stream actually is
        int  rate       = 0;

        //  La tabla de arriba, y cual de sus filas decidio. -1 = ninguna.
        std::array<Intento, kIntentos> intentos {};
        int nIntentos = 0;
        int gano      = -1;

        //  Lo que costo la sonda entera, en milisegundos. Se publica porque la
        //  sonda ya provoco un ANR una vez y la siguiente captura de pantalla
        //  tiene que poder decir si el tiempo se va aqui o en otro sitio, en
        //  vez de volver a deducirlo leyendo codigo.
        int ms = 0;
    };

    // ========================================================================
    //  LA DECISION, SEPARADA DEL CONTACTO CON AAUDIO.
    //
    //  `probeFastPath` vive tras `#if JUCE_ANDROID`, asi que el banco de
    //  escritorio no la puede correr - y una regla que no puede fallar no es una
    //  regla, es una linea que imprime OK. Lo que SI se puede medir es la otra
    //  mitad: dada la tabla de seis respuestas, que `usage` se le pasa a JUCE,
    //  si se le fuerza el formato de 16 bits y que texto se pinta. Eso es esta
    //  funcion, es pura, y `Tests/audio.py` la ejercita con tablas sinteticas.
    //
    //  LIMITE DECLARADO, en vez de fingirlo medido: que la llamada lleve
    //  callback de datos y arranque el flujo antes de leer el veredicto solo lo
    //  comprueba un telefono. Aqui se mide lo que se decide con la respuesta, no
    //  la respuesta.
    // ========================================================================
    inline Fast concluye (const std::array<Intento, kIntentos>& tabla, int n, bool mmapKnown)
    {
        Fast r;
        r.intentos  = tabla;
        r.nIntentos = juce::jlimit (0, kIntentos, n);
        r.mmapKnown = mmapKnown;

        //  UN FLUJO QUE ABRE Y NO ARRANCA NO ES UN CARRIL. AAudio puede conceder
        //  el constructor y negar el START, que es donde de verdad se compromete
        //  el MMAP: preguntar por el modo de reparto antes del START es leer una
        //  intencion y no un hecho. Se queda como suelo -un flujo abierto sigue
        //  informando de los terminos nativos del aparato, que es lo que se
        //  pinta cuando no hay nada mejor- pero nunca le gana a uno que arranco.
        auto grado = [] (const Intento& x)
        {
            if (! x.abrio)   return 0;
            if (! x.arranco) return 1;
            return x.exclusiva ? 3 : 2;
        };

        for (int i = 0; i < r.nIntentos; ++i)
        {
            if (! tabla[(size_t) i].abrio)
                continue;
            if (r.gano < 0 || grado (tabla[(size_t) i]) > grado (tabla[(size_t) r.gano]))
                r.gano = i;
        }

        if (r.gano < 0)
            return r;

        const auto& g = tabla[(size_t) r.gano];
        r.ran        = true;
        r.exclusive  = g.exclusiva && g.arranco;
        r.mmapUsed   = mmapKnown && g.mmap;
        r.lowLatency = g.baja;
        r.useI16     = g.i16;
        //  Solo se le tuerce el brazo a JUCE cuando esto ha GANADO algo. Si la
        //  exclusiva no se concedio, pedirle una `usage` distinta de la de por
        //  defecto es cambiar el flujo de verdad sin ninguna medida detras.
        r.usage      = r.exclusive ? g.usage : 0;
        r.burst      = g.burst;
        r.capacity   = g.capacity;
        r.channels   = g.canales;
        r.rate       = g.rate;
        return r;
    }

    // ========================================================================
    //  EL TAMANO DE UN CUADRO SALE DEL FORMATO CONCEDIDO, NUNCA DE UN BOOLEANO.
    //
    //  La primera version del callback de la sonda escribia
    //  `canales * (i16 ? 2 : 4)`, o sea que trataba «no es de 16 bits» como «es
    //  de 4 bytes». AAudio concede CUATRO formatos y dos de ellos no miden
    //  cuatro: PCM_I24_PACKED son TRES bytes por muestra, y con el simbolo
    //  `AAudioStream_getFormat` ausente -que no estaba en la lista de
    //  obligatorios- la cuenta daba 4 sobre un bufer de 2.
    //
    //  En los dos casos el callback -que corre en el hilo de tiempo real de
    //  AAudio, sobre el bufer MMAP compartido con el kernel- se pasaba de largo
    //  el final: 128 cuadros estereo concedidos en I24 son 768 bytes y se
    //  escribian 1024, o sea 256 bytes fuera. Eso se llevo por delante el HAL de
    //  audio, el dispositivo de verdad ya no abria -la pantalla se quedo en
    //  `OUT -inf` con el VU plano- y el vigilante, que reintenta cada segundo,
    //  arrastraba otra sonda entera por el hilo de mensajes hasta que Android
    //  canto «Zati Sampler no responde». Un ANR por un `memset` mal medido.
    //
    //  Formato que no se reconoce devuelve CERO y el callback no escribe nada.
    //  Un bloque sin tocar es como mucho un chasquido de unos milisegundos en un
    //  flujo que se cierra acto seguido; pasarse del bufer es corromper memoria
    //  ajena y no hay vuelta atras de eso.
    //
    //  Vive FUERA del `#if JUCE_ANDROID` a proposito: la sonda no la puede
    //  correr el banco de escritorio, y por eso este desbordamiento se entrego.
    //  Esta cuenta si se puede, asi que se mide (`Tests/audio.py`).
    // ========================================================================
    inline int bytesPorMuestra (int formatoAAudio)
    {
        switch (formatoAAudio)
        {
            case 1:  return 2;   // AAUDIO_FORMAT_PCM_I16
            case 2:  return 4;   // AAUDIO_FORMAT_PCM_FLOAT
            case 3:  return 3;   // AAUDIO_FORMAT_PCM_I24_PACKED
            case 4:  return 4;   // AAUDIO_FORMAT_PCM_I32
            default: return 0;   // no se sabe: no se toca el bufer
        }
    }

   #if JUCE_ANDROID
    //  EL CALLBACK VACIO, Y POR QUE EXISTE.
    //
    //  La sonda abria los seis intentos SIN callback de datos y leia el
    //  veredicto. En el fichero de al lado, JUCE documenta lo contrario para su
    //  propia sonda de rafaga (juce_Oboe_android.cpp:383): «providing a callback
    //  is required on some devices to get a FAST track, so we pass an empty one
    //  to the temp stream». El flujo de verdad SI lleva callback, asi que una
    //  sonda sin el puede contestar «no hay exclusiva» por una razon que no
    //  existe cuando se toca: pedir con una cuenta y tocar con otra, que es la
    //  figura que ya costo dos tandas en otros sitios de esta casa.
    //
    //  Se rellena de ceros y no se deja como venga porque ESTO SUENA POR EL
    //  ALTAVOZ: AAudio no promete el bloque limpio, y silencio en los dos
    //  formatos que se piden -PCM_I16 y float- es un bloque de ceros. Cuantos
    //  bytes son lo dice `bytesPorMuestra`, arriba, que esta fuera del
    //  `#if JUCE_ANDROID` justamente para que el banco pueda medirlo.
    //
    //  Atomico y no un `int` a secas porque quien lo escribe es el hilo de
    //  mensajes y quien lo lee es el hilo de tiempo real de AAudio. Es una
    //  carga sin cerrojo -`is_always_lock_free` en todo lo que Android ejecuta-
    //  asi que no rompe la regla del hilo de audio.
    inline std::atomic<int> zatiProbeBytesPerFrame { 0 };

    inline int probeCallback (void*, void*, void* audioData, int32_t numFrames)
    {
        const int bpf = zatiProbeBytesPerFrame.load (std::memory_order_acquire);
        if (audioData != nullptr && numFrames > 0 && bpf > 0)
            std::memset (audioData, 0, (size_t) numFrames * (size_t) bpf);
        return 0;   // AAUDIO_CALLBACK_RESULT_CONTINUE
    }

    inline Fast probeFastPath (int sampleRate, int channels)
    {
        void* lib = dlopen ("libaaudio.so", RTLD_NOW);
        if (lib == nullptr)
            return {};

        using Builder = void*;
        using Stream  = void*;

        auto sym = [lib] (const char* n) { return dlsym (lib, n); };

        auto create   = (int  (*) (Builder*))          sym ("AAudio_createStreamBuilder");
        auto setDir   = (void (*) (Builder, int32_t))  sym ("AAudioStreamBuilder_setDirection");
        auto setShare = (void (*) (Builder, int32_t))  sym ("AAudioStreamBuilder_setSharingMode");
        auto setPerf  = (void (*) (Builder, int32_t))  sym ("AAudioStreamBuilder_setPerformanceMode");
        auto setFmt   = (void (*) (Builder, int32_t))  sym ("AAudioStreamBuilder_setFormat");
        auto setChans = (void (*) (Builder, int32_t))  sym ("AAudioStreamBuilder_setChannelCount");
        auto setRate  = (void (*) (Builder, int32_t))  sym ("AAudioStreamBuilder_setSampleRate");
        auto setUsage = (void (*) (Builder, int32_t))  sym ("AAudioStreamBuilder_setUsage");  // API 28
        auto setCb    = (void (*) (Builder, void*, void*)) sym ("AAudioStreamBuilder_setDataCallback");
        auto openIt   = (int  (*) (Builder, Stream*))  sym ("AAudioStreamBuilder_openStream");
        auto delBuild = (int  (*) (Builder))           sym ("AAudioStreamBuilder_delete");
        auto getShare = (int32_t (*) (Stream))         sym ("AAudioStream_getSharingMode");
        auto getPerf  = (int32_t (*) (Stream))         sym ("AAudioStream_getPerformanceMode");
        auto getBurst = (int32_t (*) (Stream))         sym ("AAudioStream_getFramesPerBurst");
        auto getCap   = (int32_t (*) (Stream))         sym ("AAudioStream_getBufferCapacityInFrames");
        auto getChans = (int32_t (*) (Stream))         sym ("AAudioStream_getChannelCount");
        auto getRate  = (int32_t (*) (Stream))         sym ("AAudioStream_getSampleRate");
        auto getFmt   = (int32_t (*) (Stream))         sym ("AAudioStream_getFormat");
        auto start    = (int  (*) (Stream))            sym ("AAudioStream_requestStart");
        auto stopIt   = (int  (*) (Stream))            sym ("AAudioStream_requestStop");
        auto waitSt   = (int  (*) (Stream, int32_t, int32_t*, int64_t))
                                                       sym ("AAudioStream_waitForStateChange");
        auto closeIt  = (int  (*) (Stream))            sym ("AAudioStream_close");

        //  No esta en las cabeceras del NDK, pero libaaudio lo exporta y es la
        //  unica forma de distinguir los dos caminos compartidos: un flujo
        //  SHARED puede seguir siendo MMAP -el buffer circular del kernel,
        //  mezclado en el DSP- o puede ser AudioFlinger a secas. Los dos
        //  informan SHARED y se llevan decenas de milisegundos. Oboe lo lee
        //  exactamente asi. Si el simbolo no esta, no decimos saberlo.
        auto isMmap   = (bool (*) (Stream))            sym ("AAudioStream_isMMapUsed");

        if (create == nullptr || openIt == nullptr || closeIt == nullptr
             || setDir == nullptr || setShare == nullptr || setPerf == nullptr
             || getShare == nullptr)
        {
            dlclose (lib);
            return {};
        }

        //  El primer intento no fija NADA. Un extremo exclusivo es una pieza de
        //  hardware con una frecuencia nativa, un numero de canales y un formato,
        //  y AAudio niega la exclusividad en cuanto la peticion no encaja exacta
        //  - asi que clavar 48 kHz / estereo / float, que es la version obvia de
        //  esta sonda, puede FABRICAR la misma negativa que venia a detectar. Se
        //  pide el modo de reparto y nada mas, y que conteste el aparato con sus
        //  propios terminos.
        //
        //  Solo entonces se empieza a clavar cosas, GAME antes que MEDIA y float
        //  antes que 16 bits, para que el primer EXCLUSIVE que se vea sea tambien
        //  el cambio mas pequeno sobre el flujo de verdad.
        constexpr int kAny = 0;   // AAUDIO_UNSPECIFIED
        struct Attempt { int usage; int format; int chans; int rate; };
        const Attempt attempts[kIntentos] =
        {
            { kAny,        kAny, kAny,     kAny       },   // the device's own terms
            { kUsageGame,  kAny, kAny,     kAny       },
            { kUsageGame,  2,    channels, sampleRate },   // 2 = AAUDIO_FORMAT_PCM_FLOAT
            { kUsageGame,  1,    channels, sampleRate },   // 1 = AAUDIO_FORMAT_PCM_I16
            { kUsageMedia, 2,    channels, sampleRate },
            { kUsageMedia, 1,    channels, sampleRate }
        };

        std::array<Intento, kIntentos> tabla {};
        int n = 0;
        const auto t0 = juce::Time::getMillisecondCounter();

        for (const auto& a : attempts)
        {
            auto& t = tabla[(size_t) n];
            ++n;
            t.usage = a.usage;
            t.pidio = a.format;

            Builder b = nullptr;
            if (create (&b) != 0 || b == nullptr)
                continue;

            setDir   (b, 0);                       // AAUDIO_DIRECTION_OUTPUT
            setShare (b, 0);                       // AAUDIO_SHARING_MODE_EXCLUSIVE
            setPerf  (b, 12);                      // AAUDIO_PERFORMANCE_MODE_LOW_LATENCY
            if (setFmt   != nullptr && a.format != kAny) setFmt   (b, a.format);
            if (setChans != nullptr && a.chans  != kAny) setChans (b, a.chans);
            if (setRate  != nullptr && a.rate   != kAny) setRate  (b, a.rate);
            if (setUsage != nullptr && a.usage  != kAny) setUsage (b, a.usage);
            if (setCb    != nullptr) setCb (b, (void*) &probeCallback, nullptr);

            Stream s = nullptr;
            const int result = openIt (b, &s);

            if (delBuild != nullptr)
                delBuild (b);

            if (result != 0 || s == nullptr)
            {
                t.error = result;
                continue;
            }

            t.abrio    = true;
            t.canales  = getChans != nullptr ? getChans (s) : 0;
            t.rate     = getRate  != nullptr ? getRate  (s) : 0;
            t.capacity = getCap   != nullptr ? getCap   (s) : 0;

            const int formatoDado = (getFmt != nullptr ? getFmt (s) : 0);
            t.i16 = (formatoDado == 1);

            //  Cuanto hay que poner a cero en el callback. Se calcula ANTES del
            //  START porque despues del START el callback ya puede haber
            //  entrado, y un cero tarde es el ruido que se venia a evitar.
            //
            //  Y se calcula con el formato QUE CONCEDIO AAudio, no con `i16`:
            //  ver el comentario de `bytesPorMuestra`, que esta ahi porque esta
            //  cuenta con un booleano escribia 4 bytes donde cabian 2 o 3 y
            //  acabo en un ANR. Sin formato reconocido o sin canales, cero: el
            //  callback no toca el bufer.
            const int bpm = bytesPorMuestra (formatoDado);
            zatiProbeBytesPerFrame.store (t.canales > 0 ? bpm * t.canales : 0,
                                          std::memory_order_release);

            if (start != nullptr)
            {
                const int rs = start (s);
                if (rs == 0)
                {
                    //  120 ms y no mas: son seis intentos y esto corre en el
                    //  arranque de la app. El caso normal vuelve en cuanto el
                    //  estado deja de ser STARTING, que son unos pocos
                    //  milisegundos; el tope solo acota al aparato que se cuelga.
                    int32_t estado = 0;
                    if (waitSt != nullptr)
                        waitSt (s, 3 /* AAUDIO_STREAM_STATE_STARTING */, &estado, 120000000LL);
                    //  Sin waitForStateChange no hay forma de confirmarlo y
                    //  suponerlo seria volver a lo de antes; se cree al codigo
                    //  de retorno, que es lo unico que hay.
                    t.arranco = (waitSt == nullptr || estado == 4 /* STARTED */);
                }
                else
                {
                    t.error = rs;
                }
            }

            //  Y EL VEREDICTO SE LEE CON EL FLUJO ANDANDO, que es la otra mitad
            //  de esta tanda: la rafaga y el reloj se renegocian en el START, y
            //  el modo de reparto de un flujo que nunca arranco es una promesa.
            t.exclusiva = (getShare (s) == 0);
            t.mmap      = (isMmap  != nullptr && isMmap (s));
            t.baja      = (getPerf != nullptr && getPerf (s) == 12);
            if (getBurst != nullptr) t.burst = getBurst (s);
            if (getRate  != nullptr) t.rate  = getRate  (s);

            //  SE ESPERA A STOPPED ANTES DE CERRAR. `requestStop` es una
            //  peticion: deja el flujo en STOPPING y el callback de tiempo real
            //  puede entrar una vez mas. Cerrar ahi libera el bufer bajo los
            //  pies de un callback que aun esta escribiendo en el, que es la
            //  misma corrupcion por otra puerta. 100 ms de tope, que es de
            //  sobra para un flujo que ya se paro, y si se agota se cierra
            //  igual porque no hay nada mejor que hacer.
            if (stopIt != nullptr)
            {
                stopIt (s);
                if (waitSt != nullptr)
                {
                    int32_t parado = 0;
                    waitSt (s, 9 /* AAUDIO_STREAM_STATE_STOPPING */, &parado, 100000000LL);
                }
            }
            zatiProbeBytesPerFrame.store (0, std::memory_order_release);
            closeIt (s);

            if (t.exclusiva && t.arranco)
                break;

            //  TOPE DE TIEMPO PARA LA SONDA ENTERA, y no solo por intento. Seis
            //  intentos que esperen su tope son 6 x 120 ms de arranque mas 6 x
            //  100 ms de parada mas las aperturas: 1.3 s largos en el hilo de
            //  mensajes, y esto corre en el constructor. Android cuenta cinco
            //  segundos de hilo principal parado como ANR y esta sonda ya
            //  provoco uno. Con 400 ms gastados se para y se contesta con lo
            //  que haya: media sonda es una respuesta, una app colgada no.
            if (juce::Time::getMillisecondCounter() - t0 >= 400u)
                break;
        }

        dlclose (lib);
        auto r = concluye (tabla, n, isMmap != nullptr);
        r.ms = (int) (juce::Time::getMillisecondCounter() - t0);
        return r;
    }
   #else
    inline Fast probeFastPath (int, int) { return {}; }
   #endif

    inline juce::String describe (const Fast& f)
    {
        if (! f.ran)       return T ("sin respuesta");

        //  Cuando lo niegan, se dice SOBRE QUE nos lo negaron: un flujo
        //  compartido sigue informando de los terminos nativos del aparato, y
        //  esos son los terminos que habria tenido uno exclusivo.
        const juce::String terms = (f.rate > 0 ? juce::String (f.rate / 1000) + "k" : juce::String())
                                 + (f.channels > 0 ? " " + juce::String (f.channels) + "ch" : "")
                                 + (f.useI16 ? " 16b" : " float");

        //  Compartida no es una respuesta sino dos, y la diferencia es la
        //  pregunta entera: el MMAP compartido sigue hablando con el buffer
        //  circular del hardware y cuesta un punado de milisegundos, mientras que
        //  el mezclador de AudioFlinger cuesta decenas. Decir solo "compartida"
        //  esconde en cual de las dos estamos.
        if (! f.exclusive)
            return T ("compartida %1 - ni en %2",
                      ! f.mmapKnown ? juce::String ("(?)")
                                    : f.mmapUsed ? T ("MMAP") : T ("MEZCLADOR"),
                      terms.trim());

        return T ("EXCLUSIVA")
                 + (f.usage == kUsageGame ? " · game" : "")
                 + (f.useI16 ? " · 16b" : "")
                 + (f.burst > 0 ? " · burst " + juce::String (f.burst) : juce::String());
    }

    //  QUE SE PIDIO EN UN INTENTO, y que contesto. Dos funciones y no una
    //  porque la pantalla las pinta en dos columnas: a la izquierda lo que
    //  pedimos -que es nuestro- y a la derecha lo que dijo el aparato.
    inline juce::String pideIntento (const Intento& t)
    {
        return (t.usage == kUsageGame  ? juce::String ("game")
              : t.usage == kUsageMedia ? juce::String ("media")
                                       : T ("libre"))
             + (t.pidio == 1 ? " 16b" : t.pidio == 2 ? " float" : "");
    }

    inline juce::String describeIntento (const Intento& t)
    {
        const juce::String codigo = t.error != 0 ? " " + juce::String (t.error) : juce::String();

        if (! t.abrio)   return T ("no abrio")   + codigo;
        if (! t.arranco) return T ("no arranco") + codigo;

        return (t.exclusiva ? T ("EXCLUSIVA")
                            : T ("compartida") + " " + (t.mmap ? T ("MMAP") : T ("MEZCLADOR")))
             + (t.burst > 0 ? " · " + juce::String (t.burst) : juce::String());
    }
}

// ============================================================================
//  Dos mandos del flujo Oboe de JUCE que JUCE no expone.
//
//  juce_Oboe_android.cpp is patched at build time (ci/patch_juce_oboe.py) to
//  read these before opening the output stream: the usage it requests, and
//  whether to skip the float attempt and go straight to 16-bit. Both come from
//  the probe above, which runs with no audio device open - so the real stream
//  opens with whatever configuration the phone was willing to grant MMAP for.
//
//  Y SE ESCRIBEN UNA SOLA VEZ, en el constructor. Hubo una version que las
//  reescribia al volver al primer plano, para el caso -real y avisado por la
//  propia sonda- de arrancar mientras otra app tiene el extremo exclusivo. Se
//  quito: re-sondear son hasta seis aperturas con su START y su STOP, y los
//  tres sitios donde cabia son callbacks del hilo principal de Android, que es
//  donde se cuentan los ANR. Ver el comentario de MainComponent.cpp donde
//  estaba la funcion.
//
//  Zero means "leave JUCE alone", which is what every other platform sees.
// ============================================================================
extern "C" int zatiOboeUsage;
//  LA ENTRADA SIN PROCESAR. Ver el parche: 9 = AAUDIO_INPUT_PRESET_UNPROCESSED,
//  6 = VOICE_RECOGNITION (que tambien apaga el AGC en casi todos), 0 = lo que
//  el sistema quiera. Un sampler graba fuentes, no voz: los tres arreglos que
//  Android aplica por defecto estan pensados para lo segundo.
extern "C" int zatiOboeInputPreset;
extern "C" int zatiOboeForceI16;
