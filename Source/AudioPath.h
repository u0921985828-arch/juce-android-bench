#pragma once

#include <JuceHeader.h>

#if JUCE_ANDROID
 #include <sys/system_properties.h>
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
}
