//  juce_core hides its JNI helpers - LocalRef, GlobalRef, DECLARE_JNI_CLASS,
//  getAppContext, AndroidInterfaceImplementer - behind this switch, and even
//  <jni.h> itself. Without it the Android build fails on jobject before it
//  gets as far as anything of ours. It has to be set before the first include
//  of JuceHeader.h in this translation unit, which is why it sits above the
//  header that pulls it in.
#define JUCE_CORE_INCLUDE_JNI_HELPERS 1

#include "AudioFocus.h"

#if JUCE_ANDROID

//  The bits of android.media.AudioManager we need. juce_core declares
//  AndroidContext and JavaInteger for us; AudioManager itself is only declared
//  inside juce_video, which we do not link.
//
//  This has to sit inside namespace juce: DECLARE_JNI_CLASS expands to a class
//  deriving from JNIClassBase and referring to Array, JNINativeMethod and
//  numBytes, all unqualified, so at global scope none of them resolve.
namespace juce
{
    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (requestAudioFocus,  "requestAudioFocus",  "(Landroid/media/AudioManager$OnAudioFocusChangeListener;II)I") \
        METHOD (abandonAudioFocus,  "abandonAudioFocus",  "(Landroid/media/AudioManager$OnAudioFocusChangeListener;)I")

    DECLARE_JNI_CLASS (ZatiAudioManager, "android/media/AudioManager")
    #undef JNI_CLASS_MEMBERS
}

namespace
{
    constexpr jint kStreamMusic  = 3;   // AudioManager.STREAM_MUSIC
    constexpr jint kFocusGain    = 1;   // AudioManager.AUDIOFOCUS_GAIN
    constexpr jint kRequestGrant = 1;   // AudioManager.AUDIOFOCUS_REQUEST_GRANTED

    //  The proxy Android calls back on. Built exactly the way JUCE builds its
    //  own listeners: a dynamic interface implementer that dispatches by
    //  method name, because there is no C++ side to an interface otherwise.
    class FocusListener : public juce::AndroidInterfaceImplementer
    {
    public:
        explicit FocusListener (std::function<void (int)> cb) : onChange (std::move (cb)) {}

    private:
        std::function<void (int)> onChange;

        jobject invoke (jobject proxy, jobject method, jobjectArray args) override
        {
            auto* env = juce::getEnv();
            const auto name = juce::juceString ((jstring) env->CallObjectMethod (method, juce::JavaMethod.getName));

            if (name == "onAudioFocusChange" && args != nullptr && env->GetArrayLength (args) == 1)
            {
                juce::LocalRef<jobject> boxed (env->GetObjectArrayElement (args, 0));
                const int change = (int) env->CallIntMethod (boxed, juce::JavaInteger.intValue);

                //  Android calls this on its own main thread, which is the
                //  message thread - but going through the message manager
                //  costs nothing and removes the question entirely.
                if (onChange)
                {
                    auto cb = onChange;
                    juce::MessageManager::callAsync ([cb, change] { cb (change); });
                }

                return nullptr;
            }

            return juce::AndroidInterfaceImplementer::invoke (proxy, method, args);
        }
    };
}

struct AudioFocus::Impl
{
    juce::GlobalRef manager;
    juce::GlobalRef listenerObject;

    explicit Impl (std::function<void (int)> cb)
    {
        auto* env = juce::getEnv();

        manager = juce::GlobalRef (juce::LocalRef<jobject> (
            env->CallObjectMethod (juce::getAppContext().get(),
                                   juce::AndroidContext.getSystemService,
                                   juce::javaString ("audio").get())));

        listenerObject = juce::GlobalRef (juce::CreateJavaInterface (
            new FocusListener (std::move (cb)),
            "android/media/AudioManager$OnAudioFocusChangeListener"));
    }
};

AudioFocus::AudioFocus (Listener& l) : listener (l)
{
    impl = std::make_unique<Impl> ([this] (int change)
    {
        //  AudioManager's constants. Negative is a loss, and how negative
        //  says whether it is coming back.
        switch (change)
        {
            case -1: listener.audioFocusLost (true);  break;   // LOSS
            case -2: listener.audioFocusLost (false); break;   // LOSS_TRANSIENT
            case -3: listener.audioFocusDucked();     break;   // ...CAN_DUCK
            case  1: listener.audioFocusGained();     break;   // GAIN
            default: break;
        }
    });
}

AudioFocus::~AudioFocus()
{
    abandon();
}

bool AudioFocus::request()
{
    if (held || impl == nullptr || impl->manager.get() == nullptr)
        return held;

    const jint result = juce::getEnv()->CallIntMethod (impl->manager.get(),
                                                       juce::ZatiAudioManager.requestAudioFocus,
                                                       impl->listenerObject.get(),
                                                       kStreamMusic, kFocusGain);
    held = (result == kRequestGrant);
    return held;
}

void AudioFocus::abandon()
{
    if (! held || impl == nullptr || impl->manager.get() == nullptr)
        return;

    juce::getEnv()->CallIntMethod (impl->manager.get(),
                                   juce::ZatiAudioManager.abandonAudioFocus,
                                   impl->listenerObject.get());
    held = false;
}

#else

//  Desktop: there is nothing to arbitrate, so the focus is always ours.
AudioFocus::AudioFocus (Listener& l) : listener (l) {}
AudioFocus::~AudioFocus() = default;
bool AudioFocus::request() { held = true; return true; }
void AudioFocus::abandon() { held = false; }

#endif
