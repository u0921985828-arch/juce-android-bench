//  juce_core hides <jni.h> and every JNI helper behind this switch, and it has
//  to be set before the first include of JuceHeader.h in this file.
#define JUCE_CORE_INCLUDE_JNI_HELPERS 1

#include "SystemInsets.h"

#if JUCE_ANDROID

//  DECLARE_JNI_CLASS expands to a class deriving from JNIClassBase that names
//  Array, JNINativeMethod and numBytes unqualified, so it only compiles inside
//  namespace juce.
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
    //  Before Android 15 the system lays the window out below the bars, so
    //  they are not ours to subtract.
    if (juce::getAndroidSDKVersion() < 35)
        return {};

    if (juce::ZatiView.getRootWindowInsets == nullptr
        || juce::ZatiWindowInsets.getInsets == nullptr
        || juce::ZatiInsetsType.systemBars == nullptr)
        return {};

    auto* env = juce::getEnv();

    //  Every step is checked, and any pending Java exception is cleared before
    //  returning: an exception left on the thread makes the NEXT JNI call from
    //  anywhere in the app abort the process, which would turn a cosmetic
    //  margin into a crash somewhere else entirely.
    const auto failed = [env]
    {
        if (! env->ExceptionCheck()) return false;
        env->ExceptionClear();
        return true;
    };

    //  The ACTIVITY, not the app context. getAppContext() hands back an
    //  android.content.Context which on this app is the Application, and
    //  calling Activity.getWindow() on it is a JNI type error that ART turns
    //  into an immediate abort - the app closing the instant it opened.
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

    //  Android answers in physical pixels; everything above this line is in
    //  logical ones.
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
