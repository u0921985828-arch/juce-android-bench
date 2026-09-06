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
        METHOD (abandonAudioFocus,  "abandonAudioFocus",  "(Landroid/media/AudioManager$OnAudioFocusChangeListener;)I") \
        METHOD (getDevices,         "getDevices",         "(I)[Landroid/media/AudioDeviceInfo;")

    DECLARE_JNI_CLASS (ZatiAudioManager, "android/media/AudioManager")
    #undef JNI_CLASS_MEMBERS

    //  Y el trozo de AudioDeviceInfo que hace falta para saber por donde sale
    //  el sonido. Ver RutaAudio::porAltavoz.
    #define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (getType, "getType", "()I")

    DECLARE_JNI_CLASS (ZatiAudioDeviceInfo, "android/media/AudioDeviceInfo")
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

//  ---------------------------------------------------------------------------
//  Por donde sale el sonido. Ver RutaAudio en la cabecera.
//
//  Se pregunta cada vez y no se cachea: los cascos se enchufan y se quitan en
//  mitad de una sesion, y una respuesta guardada al arrancar seria exactamente
//  la clase de dato que el fichero de preferencias tiene y el aparato no - lo
//  mismo que una tarjeta desmontada que sigue en la ruta de exportacion. La
//  llama el hilo de mensajes, no el de audio.
bool RutaAudio::porAltavoz()
{
    auto* env = juce::getEnv();
    if (env == nullptr) return true;      // sin JNI, lo prudente es el altavoz

    juce::LocalRef<jobject> am (env->CallObjectMethod (juce::getAppContext().get(),
                                                       juce::AndroidContext.getSystemService,
                                                       juce::javaString ("audio").get()));
    if (am.get() == nullptr) return true;

    constexpr jint kOutputs = 2;          // AudioManager.GET_DEVICES_OUTPUTS
    juce::LocalRef<jobjectArray> devs ((jobjectArray) env->CallObjectMethod (
        am.get(), juce::ZatiAudioManager.getDevices, kOutputs));
    if (devs.get() == nullptr) return true;

    //  Los tipos de AudioDeviceInfo que son escucha PERSONAL. Lo que no este
    //  en esta lista -el altavoz, el HDMI, el auricular de llamada- va al aire
    //  o no es una salida de monitorizacion.
    const int n = env->GetArrayLength (devs.get());
    for (int i = 0; i < n; ++i)
    {
        juce::LocalRef<jobject> d (env->GetObjectArrayElement (devs.get(), i));
        if (d.get() == nullptr) continue;

        switch ((int) env->CallIntMethod (d.get(), juce::ZatiAudioDeviceInfo.getType))
        {
            case 3:    // TYPE_WIRED_HEADSET
            case 4:    // TYPE_WIRED_HEADPHONES
            case 7:    // TYPE_BLUETOOTH_SCO
            case 8:    // TYPE_BLUETOOTH_A2DP
            case 11:   // TYPE_USB_DEVICE
            case 22:   // TYPE_USB_HEADSET
            case 23:   // TYPE_HEARING_AID
            case 26:   // TYPE_BLE_HEADSET
                return false;
            default:
                break;
        }
    }

    return true;
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

//  Y la ruta: en un escritorio no hay altavoz de telefono que realimentar, asi
//  que la respuesta honesta es que no estorba. El banco la fuerza con
//  `ZATI_RUTA=altavoz` para poder medir la guarda, que si no seria una regla
//  que solo existe en el telefono y que por tanto no mide nadie.
bool RutaAudio::porAltavoz()
{
    return juce::SystemStats::getEnvironmentVariable ("ZATI_RUTA", {})
             .trim().equalsIgnoreCase ("altavoz");
}

#endif
