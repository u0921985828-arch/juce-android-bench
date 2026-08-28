//  juce_core esconde <jni.h> y todas sus ayudas de JNI detras de este
//  interruptor, y tiene que estar puesto antes del primer include de
//  JuceHeader.h de este fichero.
#define JUCE_CORE_INCLUDE_JNI_HELPERS 1

#include "SystemInsets.h"

#if JUCE_ANDROID

//  DECLARE_JNI_CLASS se expande a una clase que hereda de JNIClassBase y que
//  nombra Array, JNINativeMethod y numBytes sin cualificar, asi que solo compila
//  dentro de namespace juce.
namespace juce
{
    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (getWindow, "getWindow", "()Landroid/view/Window;")
    DECLARE_JNI_CLASS (ZatiActivity, "android/app/Activity")
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (getDecorView, "getDecorView", "()Landroid/view/View;")
    DECLARE_JNI_CLASS (ZatiWindow, "android/view/Window")
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (getRootWindowInsets, "getRootWindowInsets", "()Landroid/view/WindowInsets;")
    DECLARE_JNI_CLASS_WITH_MIN_SDK (ZatiView, "android/view/View", 23)
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (getInsets, "getInsets", "(I)Landroid/graphics/Insets;")
    DECLARE_JNI_CLASS_WITH_MIN_SDK (ZatiWindowInsets, "android/view/WindowInsets", 30)
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        STATICMETHOD (systemBars, "systemBars", "()I")
    DECLARE_JNI_CLASS_WITH_MIN_SDK (ZatiInsetsType, "android/view/WindowInsets$Type", 30)
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        FIELD (left,   "left",   "I") \
        FIELD (top,    "top",    "I") \
        FIELD (right,  "right",  "I") \
        FIELD (bottom, "bottom", "I")
    DECLARE_JNI_CLASS_WITH_MIN_SDK (ZatiInsets, "android/graphics/Insets", 29)
    #undef JNI_CLASS_MEMBERS
}

juce::BorderSize<int> SystemInsets::get()
{
    //  Antes de Android 15 el sistema coloca la ventana por debajo de las
    //  barras, asi que no son nuestras para restarlas.
    if (juce::getAndroidSDKVersion() < 35)
        return {};

    //  Las CUATRO que se van a llamar, y no tres: ZatiActivity.getWindow se
    //  quedaba fuera de la lista y su resultado se comprobaba mas abajo. Una
    //  precondicion escrita a medias se lee como si estuviera entera.
    if (juce::ZatiActivity.getWindow == nullptr
        || juce::ZatiWindow.getDecorView == nullptr
        || juce::ZatiView.getRootWindowInsets == nullptr
        || juce::ZatiWindowInsets.getInsets == nullptr
        || juce::ZatiInsetsType.systemBars == nullptr)
        return {};

    auto* env = juce::getEnv();

    //  Cada paso se comprueba, y cualquier excepcion de Java pendiente se
    //  limpia antes de volver: una excepcion que se queda en el hilo hace que la
    //  SIGUIENTE llamada JNI desde cualquier punto de la app aborte el proceso,
    //  lo que convertiria un margen cosmetico en un cierre en otro sitio
    //  completamente distinto.
    const auto failed = [env]
    {
        if (! env->ExceptionCheck()) return false;
        env->ExceptionClear();
        return true;
    };

    //  La ACTIVIDAD, no el contexto de la app. getAppContext() devuelve un
    //  android.content.Context que en esta app es la Application, y llamar a
    //  Activity.getWindow() sobre el es un error de tipos de JNI que ART
    //  convierte en un aborto inmediato: la app cerrandose en el instante en que
    //  se abre.
    juce::LocalRef<jobject> activity (juce::getMainActivity());
    if (activity == nullptr) return {};

    juce::LocalRef<jobject> window (env->CallObjectMethod (activity, juce::ZatiActivity.getWindow));
    if (failed() || window == nullptr) return {};

    juce::LocalRef<jobject> decor (env->CallObjectMethod (window, juce::ZatiWindow.getDecorView));
    if (failed() || decor == nullptr) return {};

    juce::LocalRef<jobject> windowInsets (env->CallObjectMethod (decor, juce::ZatiView.getRootWindowInsets));
    if (failed() || windowInsets == nullptr) return {};

    const jint mask = env->CallStaticIntMethod (juce::ZatiInsetsType, juce::ZatiInsetsType.systemBars);
    if (failed()) return {};

    juce::LocalRef<jobject> insets (env->CallObjectMethod (windowInsets, juce::ZatiWindowInsets.getInsets, mask));
    if (failed() || insets == nullptr) return {};

    //  Android contesta en pixeles fisicos; todo lo que hay por encima de esta
    //  linea esta en logicos.
    const auto scale = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
                         ? juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale
                         : 1.0;
    const auto toLogical = [scale] (jint px) { return (int) std::ceil ((double) px / juce::jmax (0.1, scale)); };

    const juce::BorderSize<int> result { toLogical (env->GetIntField (insets, juce::ZatiInsets.top)),
                                         toLogical (env->GetIntField (insets, juce::ZatiInsets.left)),
                                         toLogical (env->GetIntField (insets, juce::ZatiInsets.bottom)),
                                         toLogical (env->GetIntField (insets, juce::ZatiInsets.right)) };
    if (failed()) return {};

    return result;
}

#else

juce::BorderSize<int> SystemInsets::get() { return {}; }

#endif
