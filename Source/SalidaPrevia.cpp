//  juce_core esconde <jni.h> y los ayudantes de JNI detras de esto, y tiene que
//  estar antes del primer include de JuceHeader.h. Ver AppStorage.cpp.
#define JUCE_CORE_INCLUDE_JNI_HELPERS 1

#include "SalidaPrevia.h"

namespace SalidaPrevia
{
    juce::String nombreMotivo (int motivo)
    {
        switch (motivo)
        {
            case 1:  return "SALIDA PROPIA";
            case 2:  return "SENAL";
            case 3:  return "SIN MEMORIA";
            case 4:  return "CAIDA JAVA";
            case 5:  return "CAIDA NATIVA";
            case 6:  return "ANR";
            case 7:  return "FALLO AL ARRANCAR";
            case 8:  return "PERMISO CAMBIADO";
            case 9:  return "EXCESO DE RECURSOS";
            case 10: return "CERRADA POR LA PERSONA";
            case 11: return "DETENIDA POR LA PERSONA";
            case 12: return "DEPENDENCIA";
            case 13: return "OTRA";
            case 14: return "CONGELADA";
            case 15: return "PAQUETE CAMBIADO";
            case 16: return "ACTUALIZADA";
            default: return "MOTIVO " + juce::String (motivo);
        }
    }

    juce::String cabezaDelMain (const juce::String& traza, int lineas)
    {
        //  El volcado de ANR de Android pone cada hilo como un bloque que
        //  empieza por `"nombre" prio=...` y acaba en una linea vacia. El
        //  principal se llama "main".
        juce::StringArray fuera;
        bool dentro = false;

        for (const auto& crudo : juce::StringArray::fromLines (traza))
        {
            const auto l = crudo.trim();
            if (! dentro)
            {
                dentro = l.startsWith ("\"main\"");
                continue;
            }
            if (l.isEmpty()) break;
            if (l.startsWith ("at ") || l.startsWith ("native: #") || l.startsWith ("#"))
            {
                fuera.add (l);
                if (fuera.size() >= lineas) break;
            }
        }
        return fuera.joinIntoString ("\n");
    }
}

#if JUCE_ANDROID

namespace juce
{
    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (getSystemService, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;")
    DECLARE_JNI_CLASS (ZatiSalidaContext, "android/content/Context")
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (getHistoricalProcessExitReasons, "getHistoricalProcessExitReasons", "(Ljava/lang/String;II)Ljava/util/List;")
    DECLARE_JNI_CLASS_WITH_MIN_SDK (ZatiActivityManager, "android/app/ActivityManager", 30)
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (getReason,           "getReason",           "()I") \
        METHOD (getDescription,      "getDescription",      "()Ljava/lang/String;") \
        METHOD (getTraceInputStream, "getTraceInputStream", "()Ljava/io/InputStream;")
    DECLARE_JNI_CLASS_WITH_MIN_SDK (ZatiExitInfo, "android/app/ApplicationExitInfo", 30)
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (read,  "read",  "([B)I") \
        METHOD (close, "close", "()V")
    DECLARE_JNI_CLASS (ZatiInputStream, "java/io/InputStream")
    #undef JNI_CLASS_MEMBERS
}

namespace SalidaPrevia
{
    //  Cada paso se comprueba: una excepcion de JNI pendiente es lo que
    //  convierte un «no hay nada» en un abort del proceso.
    static bool fallo (JNIEnv* env)
    {
        if (! env->ExceptionCheck()) return false;
        env->ExceptionClear();
        return true;
    }

    Parte lee()
    {
        Parte p;
        if (juce::getAndroidSDKVersion() < 30) return p;

        auto* env = juce::getEnv();
        if (env == nullptr) return p;

        auto context = juce::getAppContext();
        if (context.get() == nullptr) return p;

        juce::LocalRef<jobject> am (env->CallObjectMethod (context.get(),
                                                           juce::ZatiSalidaContext.getSystemService,
                                                           juce::javaString ("activity").get()));
        if (fallo (env) || am.get() == nullptr) return p;

        //  La ultima salida de ESTE paquete (null = el propio), una.
        juce::LocalRef<jobject> lista (env->CallObjectMethod (am.get(),
                                                              juce::ZatiActivityManager.getHistoricalProcessExitReasons,
                                                              nullptr, (jint) 0, (jint) 1));
        if (fallo (env) || lista.get() == nullptr) return p;

        const int n = env->CallIntMethod (lista.get(), juce::JavaList.size);
        if (fallo (env) || n <= 0) return p;

        juce::LocalRef<jobject> info (env->CallObjectMethod (lista.get(), juce::JavaList.get, (jint) 0));
        if (fallo (env) || info.get() == nullptr) return p;

        p.hay = true;
        p.motivo = env->CallIntMethod (info.get(), juce::ZatiExitInfo.getReason);
        if (fallo (env)) return p;

        juce::LocalRef<jstring> desc ((jstring) env->CallObjectMethod (info.get(), juce::ZatiExitInfo.getDescription));
        if (! fallo (env) && desc.get() != nullptr)
            p.descripcion = juce::juceString (desc.get());

        if (p.motivo != 6) return p;

        juce::LocalRef<jobject> flujo (env->CallObjectMethod (info.get(), juce::ZatiExitInfo.getTraceInputStream));
        if (fallo (env) || flujo.get() == nullptr) return p;

        //  La traza entera de un ANR son cientos de KB con todos los hilos; lo
        //  que hace falta es el principal, que va de los primeros. Tope de 2 MB.
        juce::MemoryOutputStream bytes;
        juce::LocalRef<jbyteArray> buf (env->NewByteArray (16384));
        for (int total = 0; total < (2 << 20);)
        {
            const int leidos = env->CallIntMethod (flujo.get(), juce::ZatiInputStream.read, buf.get());
            if (fallo (env) || leidos <= 0) break;
            jbyte* datos = env->GetByteArrayElements (buf.get(), nullptr);
            bytes.write (datos, (size_t) leidos);
            env->ReleaseByteArrayElements (buf.get(), datos, JNI_ABORT);
            total += leidos;
        }
        env->CallVoidMethod (flujo.get(), juce::ZatiInputStream.close);
        fallo (env);

        p.traza = bytes.toString();
        return p;
    }
}

#else

namespace SalidaPrevia
{
    Parte lee()
    {
        Parte p;
        const auto* ruta = std::getenv ("ZATI_SALIDA_PREVIA");
        if (ruta == nullptr) return p;

        const juce::File f (juce::String::fromUTF8 (ruta));
        if (! f.existsAsFile()) return p;

        juce::StringArray l;
        l.addLines (f.loadFileAsString());
        if (l.size() < 2) return p;

        p.hay = true;
        p.motivo = l[0].trim().getIntValue();
        p.descripcion = l[1].trim();
        l.removeRange (0, 2);
        p.traza = l.joinIntoString ("\n");
        return p;
    }
}

#endif
