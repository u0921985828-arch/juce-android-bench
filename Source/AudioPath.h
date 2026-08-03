#pragma once

#include <JuceHeader.h>

#if JUCE_ANDROID
 #include <sys/system_properties.h>
 #include <dlfcn.h>
#endif

// ============================================================================
//  AudioPath — does this phone actually give us the fast lane?
//
//  JUCE already asks Oboe for SharingMode::Exclusive and
//  PerformanceMode::LowLatency on the real output stream
//  (juce_Oboe_android.cpp), so there is no setting left switched off in the
//  app. The question is whether ANDROID GRANTS IT, and that is decided by the
//  device, not by us.
//
//  The dividing line is AAudio MMAP. With it, the app shares a ring buffer
//  straight with the hardware and a 256-frame burst comes out around 10-20 ms.
//  Without it the same API routes through AudioFlinger, the system mixer, and
//  the same burst costs 40-60 ms. The 42 ms we could not explain lives exactly
//  there.
//
//  Whether MMAP is available at all is a system property. Reading it turns
//  "the latency is high and we don't know why" into one of two answers:
//  the phone cannot do it and no version of this app will change that, or it
//  can and something is blocking it.
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

    //  AAUDIO_POLICY_NEVER = 1, _AUTO = 2, _ALWAYS = 3 (see AAudio's
    //  aaudio_policy_t). Vendors also ship the same answer under their own
    //  key, so both are consulted before giving up.
    inline Mmap mmapPolicy()
    {
        for (auto* key : { "aaudio.mmap_policy",
                           "persist.vendor.audio.aaudio.mmap_policy",
                           "ro.vendor.audio.aaudio.mmap_policy" })
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

    inline juce::String describe (Mmap m)
    {
        switch (m)
        {
            case Mmap::Never:  return "no soportado";
            case Mmap::Auto:   return "disponible";
            case Mmap::Always: return "forzado";
            default:           return "desconocido";
        }
    }

    // ========================================================================
    //  The property above says what the phone SUPPORTS. Under policy AUTO -
    //  which is what this device reports - support is not a grant: Android
    //  decides stream by stream, and it can refuse ours while happily saying
    //  "disponible". Reading a property therefore cannot tell us whether we
    //  are on the fast lane; only opening a stream can.
    //
    //  So we open one. Before the app's real audio device exists, we ask
    //  AAudio directly for EXCLUSIVE + LOW_LATENCY and look at what comes
    //  back. AAudio never lies about this: if the returned stream says
    //  EXCLUSIVE, it is an MMAP stream sharing a ring buffer with the
    //  hardware. If it says SHARED, we got AudioFlinger and its mixer, which
    //  is where the unexplained 40 ms lives.
    //
    //  The probe repeats the open across usages, because usage is the one
    //  thing an app controls that changes the answer. A MEDIA stream on a
    //  Xiaomi walks into the vendor post-processing chain (Dolby, the system
    //  equaliser) and post-processing forces the mixer path; GAME normally
    //  bypasses it. If GAME gets EXCLUSIVE and MEDIA does not, the fix is one
    //  builder call - and the fields below carry it to the real stream.
    //
    //  Everything goes through dlopen: libaaudio only exists from API 26, and
    //  the app supports 24.
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
        bool lowLatency = false;
        bool useI16     = false;  // exclusivity needed 16-bit
        int  usage      = 0;      // the usage that won, 0 = none did
        int  burst      = 0;
        int  capacity   = 0;
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
        auto closeIt  = (int  (*) (Stream))            sym ("AAudioStream_close");

        if (create == nullptr || openIt == nullptr || closeIt == nullptr
             || setDir == nullptr || setShare == nullptr || setPerf == nullptr
             || getShare == nullptr)
        {
            dlclose (lib);
            return r;
        }

        //  GAME before MEDIA, and float before 16-bit, so that the first
        //  EXCLUSIVE we see is also the least intrusive change to make.
        struct Attempt { int usage; bool i16; };
        const Attempt attempts[] = { { kUsageGame,  false }, { kUsageGame,  true },
                                     { kUsageMedia, false }, { kUsageMedia, true } };

        for (const auto& a : attempts)
        {
            Builder b = nullptr;
            if (create (&b) != 0 || b == nullptr)
                continue;

            setDir   (b, 0);                       // AAUDIO_DIRECTION_OUTPUT
            setShare (b, 0);                       // AAUDIO_SHARING_MODE_EXCLUSIVE
            setPerf  (b, 12);                      // AAUDIO_PERFORMANCE_MODE_LOW_LATENCY
            if (setFmt   != nullptr) setFmt   (b, a.i16 ? 1 : 2);   // I16 : FLOAT
            if (setChans != nullptr) setChans (b, channels);
            if (setRate  != nullptr) setRate  (b, sampleRate);
            if (setUsage != nullptr) setUsage (b, a.usage);

            Stream s = nullptr;
            const int result = openIt (b, &s);

            if (delBuild != nullptr)
                delBuild (b);

            if (result != 0 || s == nullptr)
                continue;

            const bool exclusive = (getShare (s) == 0);

            //  Keep the first stream that opens at all, so a phone that never
            //  grants MMAP still reports its real burst instead of nothing.
            if (! r.ran || (exclusive && ! r.exclusive))
            {
                r.ran        = true;
                r.exclusive  = exclusive;
                r.lowLatency = (getPerf != nullptr && getPerf (s) == 12);
                r.useI16     = a.i16;
                r.usage      = exclusive ? a.usage : 0;
                r.burst      = getBurst != nullptr ? getBurst (s) : 0;
                r.capacity   = getCap   != nullptr ? getCap   (s) : 0;
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
        if (! f.ran)       return "sin respuesta";
        if (! f.exclusive) return "compartida (el telefono no la concede)";

        return juce::String ("EXCLUSIVA · ") + (f.usage == kUsageGame ? "game" : "media")
                 + (f.useI16 ? " · 16 bit" : "")
                 + (f.burst > 0 ? " · burst " + juce::String (f.burst) : juce::String());
    }
}

// ============================================================================
//  Two dials on JUCE's Oboe stream that JUCE does not expose.
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
extern "C" int zatiOboeForceI16;
