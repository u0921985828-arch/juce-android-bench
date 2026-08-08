//  juce_core hides <jni.h> and every JNI helper behind this switch, and it has
//  to be set before the first include of JuceHeader.h in this file.
#define JUCE_CORE_INCLUDE_JNI_HELPERS 1

#include "AppStorage.h"

#if JUCE_ANDROID

namespace juce
{
    //  Context.getExternalFilesDir(String) with a null type gives the root of
    //  the app's own directory on external storage, creating it if needed.
    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (getExternalFilesDir, "getExternalFilesDir", "(Ljava/lang/String;)Ljava/io/File;")
    DECLARE_JNI_CLASS (ZatiStorageContext, "android/content/Context")
    #undef JNI_CLASS_MEMBERS

    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (getAbsolutePath, "getAbsolutePath", "()Ljava/lang/String;")
    DECLARE_JNI_CLASS (ZatiJavaFile, "java/io/File")
    #undef JNI_CLASS_MEMBERS
}

namespace AppStorage
{
    juce::File externalFilesDir()
    {
        auto* env = juce::getEnv();
        if (env == nullptr) return {};

        //  The app CONTEXT, not the activity: this is storage, and it outlives
        //  any window. Every step is checked, because a JNI exception left
        //  pending is what turns a missing folder into a process abort.
        auto context = juce::getAppContext();
        if (context.get() == nullptr) return {};

        juce::LocalRef<jobject> dir (env->CallObjectMethod (context.get(),
                                                            juce::ZatiStorageContext.getExternalFilesDir,
                                                            nullptr));
        if (env->ExceptionCheck()) { env->ExceptionClear(); return {}; }
        if (dir.get() == nullptr) return {};      // no external storage mounted

        juce::LocalRef<jobject> path (env->CallObjectMethod (dir.get(),
                                                             juce::ZatiJavaFile.getAbsolutePath));
        if (env->ExceptionCheck()) { env->ExceptionClear(); return {}; }
        if (path.get() == nullptr) return {};

        const auto s = juce::juceString ((jstring) path.get());
        return s.isNotEmpty() ? juce::File (s) : juce::File();
    }
}

#else

namespace AppStorage { juce::File externalFilesDir() { return {}; } }

#endif
