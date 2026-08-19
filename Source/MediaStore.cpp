//  juce_core esconde <jni.h> y todos los ayudantes JNI detras de este
//  interruptor, y hay que ponerlo antes del primer include de JuceHeader.h.
#define JUCE_CORE_INCLUDE_JNI_HELPERS 1

#include "MediaStore.h"

#if JUCE_ANDROID

namespace juce
{
    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (getContentResolver, "getContentResolver", "()Landroid/content/ContentResolver;")
    DECLARE_JNI_CLASS (ZatiMsContext, "android/content/Context")
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (insert,           "insert",           "(Landroid/net/Uri;Landroid/content/ContentValues;)Landroid/net/Uri;") \
        METHOD (openOutputStream, "openOutputStream", "(Landroid/net/Uri;)Ljava/io/OutputStream;") \
        METHOD (update,           "update",           "(Landroid/net/Uri;Landroid/content/ContentValues;Ljava/lang/String;[Ljava/lang/String;)I") \
        METHOD (deleteRow,        "delete",           "(Landroid/net/Uri;Ljava/lang/String;[Ljava/lang/String;)I")
    DECLARE_JNI_CLASS (ZatiResolver, "android/content/ContentResolver")
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (constructor, "<init>", "()V") \
        METHOD (putString,   "put",    "(Ljava/lang/String;Ljava/lang/String;)V") \
        METHOD (putInteger,  "put",    "(Ljava/lang/String;Ljava/lang/Integer;)V")
    DECLARE_JNI_CLASS (ZatiValues, "android/content/ContentValues")
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        STATICMETHOD (valueOf, "valueOf", "(I)Ljava/lang/Integer;")
    DECLARE_JNI_CLASS (ZatiInteger, "java/lang/Integer")
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (write, "write", "([BII)V") \
        METHOD (close, "close", "()V")
    DECLARE_JNI_CLASS (ZatiOutStream, "java/io/OutputStream")
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        STATICFIELD (external, "EXTERNAL_CONTENT_URI", "Landroid/net/Uri;")
    DECLARE_JNI_CLASS (ZatiAudioMedia, "android/provider/MediaStore$Audio$Media")
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        STATICFIELD (sdkInt, "SDK_INT", "I")
    DECLARE_JNI_CLASS (ZatiBuildVersion, "android/os/Build$VERSION")
    #undef JNI_CLASS_MEMBERS
}

namespace MediaStore
{
    int sdk()
    {
        auto* env = juce::getEnv();
        if (env == nullptr) return 0;
        const int v = env->GetStaticIntField (juce::ZatiBuildVersion,
                                              juce::ZatiBuildVersion.sdkInt);
        if (env->ExceptionCheck()) { env->ExceptionClear(); return 0; }
        return v;
    }

    juce::String publicar (const juce::File& local,
                           const juce::String& subcarpeta,
                           const juce::String& mime)
    {
        //  RELATIVE_PATH es de API 29. Por debajo no hace falta esto: el permiso
        //  de escritura sigue valiendo y ProjectStore ya escribe directamente en
        //  la Musica compartida cuando la sonda dice que puede.
        if (sdk() < 29) return {};
        if (! local.existsAsFile()) return {};

        auto* env = juce::getEnv();
        if (env == nullptr) return {};

        //  El CONTEXTO de la app, no la actividad: esto es almacenamiento y
        //  vive mas que cualquier ventana. Cada paso se comprueba, porque una
        //  excepcion JNI que se queda pendiente es lo que convierte un fallo en
        //  un aborto del proceso.
        auto context = juce::getAppContext();
        if (context.get() == nullptr) return {};

        juce::LocalRef<jobject> resolver (env->CallObjectMethod (context.get(),
                                                                 juce::ZatiMsContext.getContentResolver));
        if (env->ExceptionCheck()) { env->ExceptionClear(); return {}; }
        if (resolver.get() == nullptr) return {};

        juce::LocalRef<jobject> coleccion (env->GetStaticObjectField (juce::ZatiAudioMedia,
                                                                      juce::ZatiAudioMedia.external));
        if (env->ExceptionCheck()) { env->ExceptionClear(); return {}; }
        if (coleccion.get() == nullptr) return {};

        const juce::String rel = "Music/" + subcarpeta;

        auto ponTexto = [env] (jobject valores, const char* clave, const juce::String& v)
        {
            juce::LocalRef<jstring> k (env->NewStringUTF (clave));
            env->CallVoidMethod (valores, juce::ZatiValues.putString, k.get(),
                                 juce::javaString (v).get());
            if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
            return true;
        };
        auto ponEntero = [env] (jobject valores, const char* clave, int v)
        {
            juce::LocalRef<jobject> boxed (env->CallStaticObjectMethod (juce::ZatiInteger,
                                                                        juce::ZatiInteger.valueOf, (jint) v));
            if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
            juce::LocalRef<jstring> k (env->NewStringUTF (clave));
            env->CallVoidMethod (valores, juce::ZatiValues.putInteger, k.get(), boxed.get());
            if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
            return true;
        };

        juce::LocalRef<jobject> valores (env->NewObject (juce::ZatiValues,
                                                         juce::ZatiValues.constructor));
        if (env->ExceptionCheck() || valores.get() == nullptr) { env->ExceptionClear(); return {}; }

        //  IS_PENDING mientras se escribe: sin el, un gestor de ficheros o un
        //  reproductor puede toparse con el fichero a medio copiar y ensenarlo
        //  roto. Se limpia al final, que es lo que lo hace visible.
        if (! ponTexto (valores.get(), "_display_name", local.getFileName())) return {};
        if (! ponTexto (valores.get(), "mime_type", mime)) return {};
        if (! ponTexto (valores.get(), "relative_path", rel)) return {};
        if (! ponEntero (valores.get(), "is_pending", 1)) return {};

        juce::LocalRef<jobject> uri (env->CallObjectMethod (resolver.get(), juce::ZatiResolver.insert,
                                                            coleccion.get(), valores.get()));
        if (env->ExceptionCheck()) { env->ExceptionClear(); return {}; }
        if (uri.get() == nullptr) return {};

        bool bien = false;
        {
            juce::LocalRef<jobject> salida (env->CallObjectMethod (resolver.get(),
                                                                    juce::ZatiResolver.openOutputStream,
                                                                    uri.get()));
            if (env->ExceptionCheck()) { env->ExceptionClear(); }
            else if (salida.get() != nullptr)
            {
                //  A TROZOS, no de una vez. Un rebote por pistas son 48 MB
                //  medidos, y meterlos en un array de Java para copiarlos
                //  deshace justo lo que costo hacer el rebote por bloques.
                constexpr int kTrozo = 256 * 1024;
                juce::LocalRef<jbyteArray> buf (env->NewByteArray (kTrozo));
                juce::FileInputStream in (local);
                if (buf.get() != nullptr && in.openedOk())
                {
                    juce::HeapBlock<char> tmp ((size_t) kTrozo);
                    bien = true;
                    while (! in.isExhausted())
                    {
                        const int leidos = in.read (tmp.getData(), kTrozo);
                        if (leidos <= 0) break;
                        env->SetByteArrayRegion (buf.get(), 0, leidos, (const jbyte*) tmp.getData());
                        env->CallVoidMethod (salida.get(), juce::ZatiOutStream.write, buf.get(), 0, leidos);
                        if (env->ExceptionCheck()) { env->ExceptionClear(); bien = false; break; }
                    }
                }
                env->CallVoidMethod (salida.get(), juce::ZatiOutStream.close);
                if (env->ExceptionCheck()) { env->ExceptionClear(); bien = false; }
            }
        }

        //  Y se cierra la fila: publicada si fue bien, borrada si no. Dejar una
        //  fila pendiente es dejar un fichero fantasma que el sistema no ensena
        //  y que tampoco se puede volver a escribir con el mismo nombre.
        juce::LocalRef<jobject> cierre (env->NewObject (juce::ZatiValues, juce::ZatiValues.constructor));
        if (cierre.get() != nullptr && bien)
        {
            ponEntero (cierre.get(), "is_pending", 0);
            env->CallIntMethod (resolver.get(), juce::ZatiResolver.update,
                                uri.get(), cierre.get(), nullptr, nullptr);
            if (env->ExceptionCheck()) { env->ExceptionClear(); bien = false; }
        }
        if (! bien)
        {
            env->CallIntMethod (resolver.get(), juce::ZatiResolver.deleteRow, uri.get(), nullptr, nullptr);
            if (env->ExceptionCheck()) env->ExceptionClear();
            return {};
        }

        return rel + "/" + local.getFileName();
    }
}

#else

namespace MediaStore
{
    int sdk() { return 0; }
    juce::String publicar (const juce::File&, const juce::String&, const juce::String&) { return {}; }
}

#endif
