#include "DeviceTier.h"
#include "Lang.h"

namespace DeviceTier
{
namespace
{
    Tier classify (int cores, int ramMB, int mhz)
    {
        //  Cores first, because the thread that must never miss a deadline is
        //  the audio one and everything else is competing with it. The RAM
        //  figures are the ones that separate the tiers on real hardware:
        //  entry-level Android has shipped at 3-4 GB for years, the middle at
        //  6-8, and anything with 12 or more is a flagship.
        if (cores >= 8 && ramMB >= 11000 && mhz >= 2400) return Tier::ultra;
        if (cores >= 8 && ramMB >= 7000)                 return Tier::high;
        if (cores >= 6 && ramMB >= 5000)                 return Tier::mid;

        //  Few cores but plenty of memory is not an entry-level phone, it is a
        //  tablet or a desktop, and treating it as one would leave most of the
        //  instrument switched off on a machine that can clearly carry it.
        if (cores >= 4 && ramMB >= 7000)                 return Tier::mid;

        return Tier::low;
    }

    Profile build()
    {
        const int cores = juce::jmax (1, juce::SystemStats::getNumCpus());
        const int ramMB = juce::jmax (512, juce::SystemStats::getMemorySizeInMegabytes());
        const int mhz   = juce::jmax (0, juce::SystemStats::getCpuSpeedInMegaherz());

        Profile p;
        p.tier = classify (cores, ramMB, mhz);

        switch (p.tier)
        {
            case Tier::low:
                //  Sixteen voices still covers a four-on-the-floor pattern
                //  with a break under it; the UI drops to ten frames a second,
                //  which looks fine on meters and costs a third of what
                //  sixteen did; and the pad art goes, because sixteen redrawn
                //  envelopes is the one cost with no musical return.
                p.voices = 16;  p.voicesPerPad = 4;
                p.uiIntervalMs = 100;
                p.scopePoints = 256;
                p.recordSeconds = 20.0;  p.recordStereo = false;
                p.sampleBudgetMB = 64;
                p.bufferBursts = 2;
                p.padWaveformArt = false;
                break;

            case Tier::mid:
                p.voices = 32;  p.voicesPerPad = 6;
                p.uiIntervalMs = 60;
                p.scopePoints = 512;
                p.recordSeconds = 45.0;  p.recordStereo = true;
                p.sampleBudgetMB = 128;
                p.bufferBursts = 1;
                p.padWaveformArt = true;
                break;

            case Tier::high:
                p.voices = 48;  p.voicesPerPad = 8;
                p.uiIntervalMs = 40;
                p.scopePoints = 1024;
                p.recordSeconds = 60.0;  p.recordStereo = true;
                p.sampleBudgetMB = 192;
                p.bufferBursts = 1;
                p.padWaveformArt = true;
                break;

            case Tier::ultra:
                //  The ceiling, not a guess: 64 is the size of the pool array,
                //  and thirty frames a second is the point past which nothing
                //  in this interface moves fast enough to notice.
                p.voices = 64;  p.voicesPerPad = 12;
                p.uiIntervalMs = 33;
                p.scopePoints = 1024;
                p.recordSeconds = 120.0; p.recordStereo = true;
                p.sampleBudgetMB = 384;
                p.bufferBursts = 1;
                p.padWaveformArt = true;
                break;
        }

        return p;
    }
}

const Profile& profile()
{
    static const Profile p = build();
    return p;
}

juce::String tierName (Tier t)
{
    switch (t)
    {
        case Tier::low:   return T ("basica");
        case Tier::mid:   return T ("media");
        case Tier::high:  return T ("alta");
        case Tier::ultra: return T ("muy alta");
        default:          return {};
    }
}

juce::String describe()
{
    const int cores = juce::jmax (1, juce::SystemStats::getNumCpus());
    const int ramMB = juce::jmax (512, juce::SystemStats::getMemorySizeInMegabytes());

    return T ("%1 nucleos", juce::String (cores))
             + " · " + juce::String (ramMB / 1024.0, 1) + " GB · "
             + tierName (profile().tier)
             + " · " + T ("%1 voces", juce::String (profile().voices));
}
}
