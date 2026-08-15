#include "DeviceTier.h"
#include "Lang.h"

namespace DeviceTier
{
namespace
{
    Tier classify (int cores, int ramMB, int mhz)
    {
        //  Los nucleos primero, porque el hilo que no puede llegar tarde nunca
        //  es el de audio y todo lo demas compite con el. Las cifras de memoria
        //  son las que separan las gamas en hardware real: la entrada de Android
        //  lleva anos saliendo con 3-4 GB, la media con 6-8, y cualquier cosa con
        //  12 o mas es gama alta.
        if (cores >= 8 && ramMB >= 11000 && mhz >= 2400) return Tier::ultra;
        if (cores >= 8 && ramMB >= 7000)                 return Tier::high;
        if (cores >= 6 && ramMB >= 5000)                 return Tier::mid;

        //  Pocos nucleos y mucha memoria no es un telefono de entrada, es una
        //  tableta o un escritorio, y tratarlo como tal dejaria medio instrumento
        //  apagado en una maquina que claramente lo aguanta.
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
                //  Dieciseis voces siguen cubriendo un patron a negras con un
                //  break debajo; la interfaz baja a diez fotogramas por segundo,
                //  que en unos medidores se ve bien y cuesta un tercio de lo que
                //  costaban dieciseis; y se va la onda de las tapas, que es el
                //  unico gasto sin retorno musical.
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
                //  El techo, no una suposicion: 64 es el tamano del array del
                //  deposito, y treinta fotogramas por segundo es el punto a
                //  partir del cual nada de esta interfaz se mueve lo bastante
                //  rapido como para notarlo.
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

    //  "15.7 GB" es una tirada latina dentro de una frase que puede ser arabe,
    //  asi que va cercada: sin eso el bidi mueve el punto decimal y la unidad, y
    //  la linea deja de significar un tamano.
    return T ("%1 nucleos", juce::String (cores))
             + " · " + Lang::ltr (juce::String (ramMB / 1024.0, 1) + " GB") + " · "
             + tierName (profile().tier)
             + " · " + T ("%1 voces", juce::String (profile().voices));
}
}
