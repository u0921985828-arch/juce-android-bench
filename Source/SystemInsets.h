#pragma once

#include <JuceHeader.h>

// ============================================================================
//  SystemInsets — how much of the window the system is drawing on top of.
//
//  From Android 15 an app that targets API 35 cannot opt out of edge to edge:
//  the window is the whole screen and the status bar and the navigation bar
//  are painted over it. Nothing warns you. The face simply moves up under the
//  clock, and the status line ends up under the gesture pill.
//
//  JUCE reports safe-area insets on Android, but only the display CUTOUT -
//  the notch - and not the bars, so it cannot answer this on its own.
//
//  This asks the window itself, and only on API 35 and up. On anything older
//  the system still lays the window out below the bars, and subtracting them
//  again would carve a second status bar's worth of nothing out of the top.
//
//  Off Android it returns zero, so the caller has no platform branches.
// ============================================================================
namespace SystemInsets
{
    //  In logical (JUCE) units, not physical pixels.
    juce::BorderSize<int> get();
}
