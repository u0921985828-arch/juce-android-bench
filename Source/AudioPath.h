#pragma once

#include <JuceHeader.h>
#include "Lang.h"

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
    };

   #if JUCE_ANDROID
    inline Fast probeFastPath (int sampleRate, int channels)
    {
        Fast r;

        void* lib = dlopen ("libaaudio.so", RTLD_NOW);
        if (lib == nullptr)
            return r;

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
        auto openIt   = (int  (*) (Builder, Stream*))  sym ("AAudioStreamBuilder_openStream");
        auto delBuild = (int  (*) (Builder))           sym ("AAudioStreamBuilder_delete");
        auto getShare = (int32_t (*) (Stream))         sym ("AAudioStream_getSharingMode");
        auto getPerf  = (int32_t (*) (Stream))         sym ("AAudioStream_getPerformanceMode");
        auto getBurst = (int32_t (*) (Stream))         sym ("AAudioStream_getFramesPerBurst");
        auto getCap   = (int32_t (*) (Stream))         sym ("AAudioStream_getBufferCapacityInFrames");
        auto getChans = (int32_t (*) (Stream))         sym ("AAudioStream_getChannelCount");
        auto getRate  = (int32_t (*) (Stream))         sym ("AAudioStream_getSampleRate");
        auto getFmt   = (int32_t (*) (Stream))         sym ("AAudioStream_getFormat");
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
            return r;
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
        const Attempt attempts[] =
        {
            { kAny,        kAny, kAny,     kAny       },   // the device's own terms
            { kUsageGame,  kAny, kAny,     kAny       },
            { kUsageGame,  2,    channels, sampleRate },   // 2 = AAUDIO_FORMAT_PCM_FLOAT
            { kUsageGame,  1,    channels, sampleRate },   // 1 = AAUDIO_FORMAT_PCM_I16
            { kUsageMedia, 2,    channels, sampleRate },
            { kUsageMedia, 1,    channels, sampleRate }
        };

        for (const auto& a : attempts)
        {
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

            Stream s = nullptr;
            const int result = openIt (b, &s);

            if (delBuild != nullptr)
                delBuild (b);

            if (result != 0 || s == nullptr)
                continue;

            const bool exclusive = (getShare (s) == 0);

            //  Se guarda el primer flujo que llegue a abrirse, para que un
            //  telefono que no concede MMAP nunca informe igual de su burst real
            //  en vez de no informar de nada.
            if (! r.ran || (exclusive && ! r.exclusive))
            {
                r.ran        = true;
                r.exclusive  = exclusive;
                r.mmapKnown  = (isMmap != nullptr);
                r.mmapUsed   = (isMmap != nullptr && isMmap (s));
                r.lowLatency = (getPerf != nullptr && getPerf (s) == 12);
                r.useI16     = (getFmt  != nullptr && getFmt  (s) == 1);
                r.usage      = exclusive ? a.usage : 0;
                r.burst      = getBurst != nullptr ? getBurst (s) : 0;
                r.capacity   = getCap   != nullptr ? getCap   (s) : 0;
                r.channels   = getChans != nullptr ? getChans (s) : 0;
                r.rate       = getRate  != nullptr ? getRate  (s) : 0;
            }

            closeIt (s);

            if (r.exclusive)
                break;
        }

        dlclose (lib);
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
}

// ============================================================================
//  Dos mandos del flujo Oboe de JUCE que JUCE no expone.
//
//  juce_Oboe_android.cpp is patched at build time (ci/patch_juce_oboe.py) to
//  read these before opening the output stream: the usage it requests, and
//  whether to skip the float attempt and go straight to 16-bit. Both are set
//  once from the probe above, before any audio device exists, and never
//  touched again - so the real stream opens with whatever configuration the
//  phone was willing to grant MMAP for.
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
