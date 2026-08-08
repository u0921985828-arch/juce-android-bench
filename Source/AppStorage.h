#pragma once

#include <JuceHeader.h>

// ============================================================================
//  AppStorage — the one folder on an Android phone that is both writable by us
//  and reachable by you.
//
//  Scoped storage leaves an app three places to put things, and only one of
//  them is any good for a sampler:
//
//    * shared Music — where a musician would expect their sounds to be, and
//      where a modern Android will not let us write without going through
//      MediaStore. ProjectStore probes it by actually writing a file, and on
//      most phones now the probe fails.
//    * app INTERNAL data (/data/user/0/...) — always writable, and visible to
//      nothing. Not a file manager, not a USB cable, not you. A library you
//      cannot put samples into is not a library.
//    * app EXTERNAL files (Android/data/<package>/files) — writable with no
//      permission at all, and it shows up over USB from a computer, which is
//      how a pack actually gets onto a phone.
//
//  So this is the middle rung, and it is the one the library should sit on
//  whenever shared Music refuses us.
//
//  Empty on desktop and on anything that does not answer: the caller falls
//  back the same way it always did.
// ============================================================================
namespace AppStorage
{
    juce::File externalFilesDir();
}
