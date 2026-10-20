//  Ficheros hostiles por los dos lectores de la app -SampleLoader::loadAsync y
//  ProjectStore::readSample- y despues por el motor y por QUITAR RUIDO. Una
//  linea por fichero y lector; el veredicto lo da Tests/muestras.py.
#include <JuceHeader.h>
#include "../Source/AudioEngine.h"
#include "../Source/SampleLoader.h"
#include "../Source/ProjectStore.h"
#include "../Source/Denoise.h"
#include <cmath>
#include <cstdio>

static void mide (AudioEngine& e, int slot, SampleBuffer::Ptr sb, const char* via, const juce::String& nombre)
{
    int noFinitas = 0; float pico = 0.0f;
    for (int c = 0; c < sb->buffer.getNumChannels(); ++c)
        for (int i = 0; i < sb->buffer.getNumSamples(); ++i)
        {
            const float v = sb->buffer.getSample (c, i);
            if (std::isfinite (v)) pico = std::max (pico, std::abs (v)); else ++noFinitas;
        }
    int ruidoNoFinitas = -1;
    if (sb->buffer.getNumSamples() >= 2048 && sb->buffer.getNumSamples() < 48000 * 20)
    {
        juce::AudioBuffer<float> c; c.makeCopyOf (sb->buffer);
        Denoise::process (c, 0.6f);
        ruidoNoFinitas = 0;
        for (int ch = 0; ch < c.getNumChannels(); ++ch)
            for (int i = 0; i < c.getNumSamples(); ++i) if (! std::isfinite (c.getSample (ch, i))) ++ruidoNoFinitas;
    }
    e.publishSample (slot, sb);
    std::printf ("{\"f\":\"%s\",\"via\":\"%s\",\"ok\":1,\"ch\":%d,\"sr\":%.0f,\"no_finitas\":%d,\"pico\":%.3g,\"ruido_no_finitas\":%d}\n",
                 nombre.toRawUTF8(), via, sb->buffer.getNumChannels(), sb->sourceSampleRate,
                 noFinitas, (double) pico, ruidoNoFinitas);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2) { std::printf ("uso: Carga <carpeta>\n"); return 2; }
    AudioEngine e; e.prepareToPlay (48000.0, 256); e.setPolyphony (32, 8);
    SampleLoader loader (e);

    {   //  MP3 se registra una vez (estabilidad#16).
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        int mp3 = 0;
        for (auto* f : fm) if (f->getFileExtensions().contains (".mp3")) ++mp3;
        std::printf ("{\"mp3_basicos\":%d,\"mp3_cargador\":%d}\n", mp3, loader.formatosMp3());
    }

    {   //  Cerrar a media carga no espera a la decodificacion entera (estabilidad#24).
        const auto largo = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("zati-largo", ".wav");
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> flujo (new juce::FileOutputStream (largo));
            auto w = wav.createWriterFor (flujo, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (16));
            juce::AudioBuffer<float> b (2, 48000);
            for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, 0.3f * std::sin (0.03f * (float) i)), b.setSample (1, i, b.getSample (0, i));
            for (int seg = 0; seg < 340; ++seg) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }
        double entero = 0.0, cerrar = 0.0;
        {
            SampleLoader l (e); bool hecho = false;
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            l.loadAsync (juce::URL (largo), 5, [&] (bool, juce::String, SampleBuffer::Ptr) { hecho = true; });
            while (! hecho) juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
            entero = juce::Time::getMillisecondCounterHiRes() - t0;
        }
        {
            auto l = std::make_unique<SampleLoader> (e);
            l->loadAsync (juce::URL (largo), 5, [] (bool, juce::String, SampleBuffer::Ptr) {});
            juce::Thread::sleep (juce::jmax (1, (int) (entero / 8.0)));
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            l.reset();
            cerrar = juce::Time::getMillisecondCounterHiRes() - t0;
        }
        largo.deleteFile();
        std::printf ("{\"carga_entera_ms\":%.0f,\"cerrar_ms\":%.0f}\n", entero, cerrar);
    }

    juce::Array<juce::File> fs;
    juce::File (juce::String (argv[1])).findChildFiles (fs, juce::File::findFiles, false);
    fs.sort();
    for (auto& f : fs)
    {
        bool hecho = false, ok = false; juce::String det; SampleBuffer::Ptr got;
        loader.loadAsync (juce::URL (f), 3, [&] (bool o, juce::String d, SampleBuffer::Ptr s) { hecho = true; ok = o; det = d; got = s; });
        for (int k = 0; k < 600 && ! hecho; ++k) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        std::printf ("{\"f\":\"%s\",\"via\":\"detalle\",\"ok\":%d,\"detalle\":\"%s\"}\n",
                     f.getFileName().toRawUTF8(), ok ? 1 : 0, det.toRawUTF8());
        if (ok && got != nullptr) mide (e, 3, got, "cargador", f.getFileName());
        if (auto rs = ProjectStore::readSample (f)) mide (e, 4, rs, "sesion", f.getFileName());
        else std::printf ("{\"f\":\"%s\",\"via\":\"sesion\",\"ok\":0}\n", f.getFileName().toRawUTF8());
        std::fflush (stdout);
    }
    return 0;
}
