//  Las ayudas de JNI de juce_core, antes del primer include (ver SystemInsets.cpp).
#define JUCE_CORE_INCLUDE_JNI_HELPERS 1

#include "Sistema.h"

namespace
{
    juce::Array<juce::Rectangle<int>> pedida;
}

#if JUCE_ANDROID

namespace juce
{
    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        STATICMETHOD (getLongPressTimeout, "getLongPressTimeout", "()I")
    DECLARE_JNI_CLASS (ZatiViewConfiguration, "android/view/ViewConfiguration")
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        STATICMETHOD (getFloat, "getFloat", "(Landroid/content/ContentResolver;Ljava/lang/String;F)F")
    DECLARE_JNI_CLASS (ZatiSettingsGlobal, "android/provider/Settings$Global")
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (getWindow, "getWindow", "()Landroid/view/Window;")
    DECLARE_JNI_CLASS (ZatiSisActivity, "android/app/Activity")
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (getDecorView, "getDecorView", "()Landroid/view/View;")
    DECLARE_JNI_CLASS (ZatiSisWindow, "android/view/Window")
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (getRootWindowInsets, "getRootWindowInsets", "()Landroid/view/WindowInsets;") \
        METHOD (setSystemGestureExclusionRects, "setSystemGestureExclusionRects", "(Ljava/util/List;)V")
    DECLARE_JNI_CLASS_WITH_MIN_SDK (ZatiSisView, "android/view/View", 29)
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (getInsets, "getInsets", "(I)Landroid/graphics/Insets;")
    DECLARE_JNI_CLASS_WITH_MIN_SDK (ZatiSisWindowInsets, "android/view/WindowInsets", 30)
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        STATICMETHOD (systemGestures, "systemGestures", "()I")
    DECLARE_JNI_CLASS_WITH_MIN_SDK (ZatiSisInsetsType, "android/view/WindowInsets$Type", 30)
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        FIELD (left,   "left",   "I") \
        FIELD (top,    "top",    "I") \
        FIELD (right,  "right",  "I") \
        FIELD (bottom, "bottom", "I")
    DECLARE_JNI_CLASS_WITH_MIN_SDK (ZatiSisInsets, "android/graphics/Insets", 29)
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (constructor, "<init>", "(IIII)V")
    DECLARE_JNI_CLASS (ZatiSisRect, "android/graphics/Rect")
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (constructor, "<init>", "()V") \
        METHOD (add, "add", "(Ljava/lang/Object;)Z")
    DECLARE_JNI_CLASS (ZatiSisArrayList, "java/util/ArrayList")
    #undef JNI_CLASS_MEMBERS
}

namespace
{
    bool fallo (JNIEnv* env)
    {
        if (! env->ExceptionCheck()) return false;
        env->ExceptionClear();
        return true;
    }

    double escala()
    {
        const auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        return d != nullptr ? juce::jmax (0.1, d->scale) : 1.0;
    }

    juce::LocalRef<jobject> vistaRaiz (JNIEnv* env)
    {
        juce::LocalRef<jobject> activity (juce::getMainActivity());
        if (activity == nullptr) return {};
        juce::LocalRef<jobject> window (env->CallObjectMethod (activity, juce::ZatiSisActivity.getWindow));
        if (fallo (env) || window == nullptr) return {};
        juce::LocalRef<jobject> decor (env->CallObjectMethod (window, juce::ZatiSisWindow.getDecorView));
        if (fallo (env)) return {};
        return decor;
    }
}

int Sistema::pulsacionLargaMs (int siNoContesta)
{
    auto* env = juce::getEnv();
    const jint ms = env->CallStaticIntMethod (juce::ZatiViewConfiguration, juce::ZatiViewConfiguration.getLongPressTimeout);
    if (fallo (env) || ms <= 0) return siNoContesta;
    return juce::jlimit (200, 1500, (int) ms);
}

float Sistema::escalaAnimaciones()
{
    auto* env = juce::getEnv();
    juce::LocalRef<jobject> activity (juce::getMainActivity());
    if (activity == nullptr) return 1.0f;
    juce::LocalRef<jobject> resolver (env->CallObjectMethod (activity, juce::AndroidContext.getContentResolver));
    if (fallo (env) || resolver == nullptr) return 1.0f;
    const float v = env->CallStaticFloatMethod (juce::ZatiSettingsGlobal, juce::ZatiSettingsGlobal.getFloat,
                                                resolver.get(), juce::javaString ("animator_duration_scale").get(), 1.0f);
    if (fallo (env)) return 1.0f;
    return juce::jmax (0.0f, v);
}

juce::BorderSize<int> Sistema::gestos()
{
    if (juce::getAndroidSDKVersion() < 30) return {};
    auto* env = juce::getEnv();
    auto decor = vistaRaiz (env);
    if (decor == nullptr) return {};
    juce::LocalRef<jobject> wi (env->CallObjectMethod (decor, juce::ZatiSisView.getRootWindowInsets));
    if (fallo (env) || wi == nullptr) return {};
    const jint mask = env->CallStaticIntMethod (juce::ZatiSisInsetsType, juce::ZatiSisInsetsType.systemGestures);
    if (fallo (env)) return {};
    juce::LocalRef<jobject> in (env->CallObjectMethod (wi, juce::ZatiSisWindowInsets.getInsets, mask));
    if (fallo (env) || in == nullptr) return {};
    const auto s = escala();
    const auto log = [s] (jint px) { return (int) std::ceil ((double) px / s); };
    const juce::BorderSize<int> r { log (env->GetIntField (in, juce::ZatiSisInsets.top)),
                                    log (env->GetIntField (in, juce::ZatiSisInsets.left)),
                                    log (env->GetIntField (in, juce::ZatiSisInsets.bottom)),
                                    log (env->GetIntField (in, juce::ZatiSisInsets.right)) };
    if (fallo (env)) return {};
    return r;
}

void Sistema::excluyeGestos (const juce::Array<juce::Rectangle<int>>& zonas)
{
    if (zonas == pedida) return;
    pedida = zonas;
    if (juce::getAndroidSDKVersion() < 29) return;
    auto* env = juce::getEnv();
    auto decor = vistaRaiz (env);
    if (decor == nullptr) return;
    juce::LocalRef<jobject> lista (env->NewObject (juce::ZatiSisArrayList, juce::ZatiSisArrayList.constructor));
    if (fallo (env) || lista == nullptr) return;
    const auto s = escala();
    for (const auto& z : zonas)
    {
        const auto f = z.toDouble() * s;
        juce::LocalRef<jobject> rect (env->NewObject (juce::ZatiSisRect, juce::ZatiSisRect.constructor,
                                                      (jint) std::floor (f.getX()), (jint) std::floor (f.getY()),
                                                      (jint) std::ceil (f.getRight()), (jint) std::ceil (f.getBottom())));
        if (fallo (env) || rect == nullptr) return;
        env->CallBooleanMethod (lista, juce::ZatiSisArrayList.add, rect.get());
        if (fallo (env)) return;
    }
    env->CallVoidMethod (decor, juce::ZatiSisView.setSystemGestureExclusionRects, lista.get());
    fallo (env);
}

#else

namespace
{
    //  La entrada del banco: «clave=valor;...». Vacia fuera del banco.
    juce::String banco (const char* clave)
    {
        static const auto todo = juce::StringArray::fromTokens (
            juce::SystemStats::getEnvironmentVariable ("ZATI_SISTEMA", {}), ";", {});
        for (const auto& t : todo)
            if (t.upToFirstOccurrenceOf ("=", false, false).trim() == clave)
                return t.fromFirstOccurrenceOf ("=", false, false).trim();
        return {};
    }
}

int Sistema::pulsacionLargaMs (int siNoContesta)
{
    const auto b = banco ("pulsacion");
    return b.isNotEmpty() ? juce::jlimit (200, 1500, b.getIntValue()) : siNoContesta;
}

float Sistema::escalaAnimaciones()
{
    const auto b = banco ("animacion");
    return b.isNotEmpty() ? juce::jmax (0.0f, b.getFloatValue()) : 1.0f;
}

juce::BorderSize<int> Sistema::gestos()
{
    auto n = juce::StringArray::fromTokens (banco ("gestos"), ",", {});
    if (n.size() != 4) return {};
    return { n[0].getIntValue(), n[1].getIntValue(), n[2].getIntValue(), n[3].getIntValue() };
}

void Sistema::excluyeGestos (const juce::Array<juce::Rectangle<int>>& zonas) { pedida = zonas; }

#endif

juce::Array<juce::Rectangle<int>> Sistema::ultimaExclusion() { return pedida; }
