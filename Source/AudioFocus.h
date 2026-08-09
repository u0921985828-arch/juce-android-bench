#pragma once

#include <JuceHeader.h>

// ============================================================================
//  AudioFocus — ask Android whether we are allowed to be the one making noise.
//
//  Android arbitrates the speaker between apps, and an app that neither asks
//  for the focus nor listens for losing it behaves badly in ways that are
//  invisible from the inside:
//
//    * A call or an alarm arrives and ZATI keeps playing over it. Many OEM
//      builds silence or duck us without telling us, so the meters go on
//      moving while nothing comes out - which reads as "the app is broken".
//    * Another app takes exclusive focus and our stream can stay mute
//      indefinitely, because nobody is listening for the event that says so.
//
//  What we do about each loss is the conventional contract:
//
//    LOSS (-1)                 someone else owns the speaker now. Stop, and
//                              do not resume by ourselves.
//    LOSS_TRANSIENT (-2)       a notification, a call. Pause; resume on GAIN.
//    LOSS_TRANSIENT_CAN_DUCK   we may keep playing quietly underneath, and
//                    (-3)      that is exactly what we do.
//
//                              This used to PAUSE, on the argument that a
//                              sampler ducked to a third is not useful to
//                              play. The argument is about the wrong event.
//                              CAN_DUCK is what a NOTIFICATION sends - a
//                              battery warning, a message ping - and it lasts
//                              a third of a second. Answering it by stopping
//                              the sequencer and tearing the audio device
//                              down turned a chime into "the app stopped and
//                              did not come back", which is what happened at
//                              5% battery, mid-take.
//
//                              Ducking costs one multiply. Nothing is torn
//                              down, so nothing has to survive being rebuilt.
//    GAIN (1)                  resume, but only if it was us who paused.
//
//  Everything is deprecated-but-working API: requestAudioFocus with a stream
//  type rather than the API 26 AudioFocusRequest builder. That builder needs
//  AudioAttributes plumbing for behaviour we do not use, and the three
//  argument call is still honoured on current Android. If that ever changes
//  it changes in one function.
//
//  Off Android this is a pair of empty calls, so the caller carries no
//  platform branches.
// ============================================================================
class AudioFocus
{
public:
    //  Both are called on the message thread, and neither is allowed to touch
    //  audio directly - they hand back to the owner, which decides.
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void audioFocusLost (bool permanently) = 0;
        //  Keep playing, quietly. Not a loss: nothing stops, nothing is
        //  released, and the level comes back on GAIN.
        virtual void audioFocusDucked() = 0;
        virtual void audioFocusGained() = 0;
    };

    explicit AudioFocus (Listener& l);
    ~AudioFocus();

    //  Returns false when Android refused. We start the stream anyway: a
    //  refusal is not a crash, and a silent instrument would be a worse
    //  answer than one the system happens to be ducking.
    bool request();
    void abandon();

    bool isHeld() const noexcept { return held; }

private:
    Listener& listener;
    bool held = false;

   #if JUCE_ANDROID
    struct Impl;
    std::unique_ptr<Impl> impl;
   #endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioFocus)
};
