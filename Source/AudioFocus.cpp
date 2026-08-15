//  juce_core esconde sus ayudas de JNI - LocalRef, GlobalRef,
//  DECLARE_JNI_CLASS, getAppContext, AndroidInterfaceImplementer - y hasta el
//  propio <jni.h> detras de este interruptor. Sin el, la compilacion de Android
//  se rompe en jobject antes de llegar a nada nuestro. Tiene que estar puesto
//  antes del primer include de JuceHeader.h de esta unidad, y por eso va encima
//  de la cabecera que lo arrastra.
#define JUCE_CORE_INCLUDE_JNI_HELPERS 1

#include "AudioFocus.h"

#if JUCE_ANDROID

//  Los trozos de android.media.AudioManager que hacen falta. juce_core declara
//  AndroidContext y JavaInteger por nosotros; AudioManager solo esta declarado
//  dentro de juce_video, que no enlazamos.
//
//  Esto tiene que vivir dentro de namespace juce: DECLARE_JNI_CLASS se expande
//  a una clase que hereda de JNIClassBase y que nombra Array, JNINativeMethod y
//  numBytes sin cualificar, asi que en el ambito global no resuelve ninguno.
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

    //  El intermediario al que Android devuelve la llamada. Montado igual que
    //  monta JUCE los suyos: un implementador dinamico de interfaz que reparte
    //  por nombre de metodo, porque de otra forma una interfaz de Java no tiene
    //  lado de C++.
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

                //  Android llama a esto en su hilo principal, que es el de
                //  mensajes - pero pasar por el gestor de mensajes no cuesta
                //  nada y quita la duda del medio.
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
        //  Las constantes de AudioManager. Negativo es una perdida, y cuanto
        //  de negativo dice si va a volver.
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

//  Escritorio: no hay nada que repartir, asi que el foco es siempre nuestro.
AudioFocus::AudioFocus (Listener& l) : listener (l) {}
AudioFocus::~AudioFocus() = default;
bool AudioFocus::request() { held = true; return true; }
void AudioFocus::abandon() { held = false; }

#endif
