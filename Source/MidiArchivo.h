#pragma once

#include <JuceHeader.h>
#include <vector>
#include <algorithm>

// ============================================================================
//  UN PATRON DE ESTA MAQUINA CONTRA UN FICHERO .MID
// ============================================================================
//
//  Se pidio asi: «en el piano roll se podria tanto exportar como importar midi
//  para que se lean y se apliquen bien en las cuadriculas, y lo mismo para los
//  midis creados poder aplicarlo».
//
//  ESTO NO CONOCE `AudioEngine`, y esa es la decision que lo hace medible: la
//  conversion es una funcion de una LISTA DE NOTAS a un fichero y al reves, asi
//  que el banco la puede cruzar sin arrancar un motor y la app la usa por los
//  dos lados. Escribirla dentro de la cara habria dejado la unica regla que
//  importa -que lo que sale y lo que entra digan lo mismo- sin nadie que la
//  compruebe. Es la misma extraccion que `barridoDe`, `bajaDb`, `wahCentro` y
//  `FxVisor::muestrea`, hecha antes de pagarla.
//
//  LAS TRES CONVENCIONES, que son lo unico que hay que elegir aqui y por tanto
//  lo unico que hay que escribir:
//
//   1. **MIDI 60 ES EL SEMITONO CERO.** El piano roll de esta app es de UN PAD y
//      su eje vertical es el semitono RELATIVO a la afinacion de ese pad
//      -`setStepNote` acota en ±24-; un fichero trae notas absolutas. El do
//      central es donde todo el mundo pone la raiz, asi que un fichero escrito
//      en do mayor cae sobre el pad sin transportar nada.
//
//   2. **UN PASO ES UNA SEMICORCHEA.** El secuenciador son dieciseis pasos por
//      compas de 4/4 pase lo que pase -`kNumSteps`, `kMinPatLen`- asi que cuatro
//      pasos son una negra. Es la misma cuenta que la cuenta atras, que se mide
//      en PASOS y no en milisegundos por lo mismo: cuantos milisegundos sean lo
//      decide el tempo, y cuantos pasos no lo decide nadie.
//
//   3. **SE ESCRIBE LO ESCRITO, NO LO TOCADO.** El swing y el empujon de
//      HUMANIZAR no viajan al fichero. Un .mid es la PARTITURA y el groove es
//      la interpretacion: llevarlo dentro haria que importar lo que acabas de
//      exportar cuantizara el groove y lo perdiera, o sea que la ida y vuelta
//      dejaria de ser una identidad — y esa identidad es lo unico que puede
//      decir que las dos mitades dicen lo mismo. Es la leccion del troceado que
//      volvia siendo N copias: sonaba igual y habia dejado de ser un troceado.
// ============================================================================
namespace MidiArchivo
{
    //  UNA NOTA, en las unidades de ESTA maquina y no en las del fichero: el
    //  paso, el semitono relativo, la fuerza 1..127 y el largo en CUARTOS de
    //  paso -`AudioEngine::kLenMax`-, con cero queriendo decir «suelta».
    struct Nota
    {
        int paso   = 0;
        int semis  = 0;
        int vel    = 100;
        int largo  = 0;      // cuartos de paso; 0 = suelta
    };

    //  Lo que una importacion deja sin poner, contado y dicho. Un fichero de
    //  fuera no tiene por que caber: notas a tres octavas del pad, compases
    //  de mas alla del patron, o mas de cuatro voces en la misma columna.
    //  Devolver solo las que caben seria decir que si a medias.
    struct Parte
    {
        int puestas   = 0;
        int fuera     = 0;   // el semitono no cabe en ±24
        int tarde     = 0;   // el paso cae mas alla de kNumSteps
        int apiladas  = 0;   // mas de cuatro a la vez en la misma columna
        int pasos     = 0;   // cuantos pasos ocupa lo que entro, para el largo
    };

    static constexpr int kRaizMidi  = 60;    // do central = semitono 0
    static constexpr int kPasoNegra = 4;     // cuatro pasos por negra
    static constexpr int kPpq       = 96;    // divisible por 4 y por 3: tresillos
    static constexpr int kTicksPaso = kPpq / kPasoNegra;

    // ------------------------------------------------------------------
    //  ESCRIBIR
    // ------------------------------------------------------------------
    //  Formato 0 con una pista: lo que hay dentro son las notas de UN pad, asi
    //  que una segunda pista seria una pista vacia. Con su meta de tempo, que
    //  es lo que hace que el fichero suene a la velocidad a la que lo escribiste
    //  y no a los 120 por defecto de quien lo abra.
    inline juce::MidiFile aMidi (const std::vector<Nota>& notas, double bpm)
    {
        juce::MidiMessageSequence pista;

        //  El tempo primero y en el tick cero. `juce::MidiMessage::tempoMetaEvent`
        //  toma MICROSEGUNDOS POR NEGRA, que es como lo guarda el formato.
        const int usPorNegra = (int) std::lround (60.0e6 / juce::jlimit (20.0, 300.0, bpm));
        pista.addEvent (juce::MidiMessage::tempoMetaEvent (usPorNegra), 0.0);
        pista.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);

        for (const auto& n : notas)
        {
            const int nota = juce::jlimit (0, 127, kRaizMidi + n.semis);
            const double t0 = (double) (n.paso * kTicksPaso);
            //  UNA NOTA SUELTA DURA UN PASO en el fichero, que es lo que dura
            //  en la maquina: `kGateAuto` es UN PASO, que es lo que una casilla
            //  dura. Escribir duracion cero daria un fichero con notas que no
            //  suenan en ningun sitio.
            const double dur = n.largo > 0 ? (double) n.largo * (double) kTicksPaso / 4.0
                                           : (double) kTicksPaso;
            pista.addEvent (juce::MidiMessage::noteOn  (1, nota, (juce::uint8) juce::jlimit (1, 127, n.vel)), t0);
            pista.addEvent (juce::MidiMessage::noteOff (1, nota), t0 + juce::jmax (1.0, dur));
        }

        pista.updateMatchedPairs();
        pista.addEvent (juce::MidiMessage::endOfTrack(), pista.getEndTime() + 1.0);

        juce::MidiFile mf;
        mf.setTicksPerQuarterNote (kPpq);
        mf.addTrack (pista);
        return mf;
    }

    inline bool escribe (const juce::File& destino, const std::vector<Nota>& notas, double bpm)
    {
        //  Por un temporal y un renombrado no: esto no es la sesion ni la lista
        //  de licencias, es un fichero que la persona pide y que si sale a
        //  medias se vuelve a pedir. Lo que SI se comprueba es que se haya
        //  escrito, que es la postcondicion de `ensureDirectory` otra vez: lo
        //  que importa no es lo que devuelve la orden sino si el fichero esta.
        destino.deleteFile();
        std::unique_ptr<juce::FileOutputStream> out (destino.createOutputStream());
        if (out == nullptr) return false;
        const bool ok = aMidi (notas, bpm).writeTo (*out);
        out->flush();
        out.reset();
        return ok && destino.existsAsFile() && destino.getSize() > 0;
    }

    // ------------------------------------------------------------------
    //  LEER
    // ------------------------------------------------------------------
    //  TODAS LAS PISTAS Y TODOS LOS CANALES EN UNA. Un .mid de fuera trae la
    //  melodia en la pista que le haya parecido a quien lo escribio -y a menudo
    //  la 1 es solo el tempo-, asi que quedarse con una es la forma mas barata
    //  de que un fichero perfectamente valido entre vacio. Lo que esta pagina
    //  edita son las notas de UN pad: se juntan y se cuantizan.
    inline std::vector<Nota> lee (const juce::File& origen, int maxPasos, Parte& parte)
    {
        parte = {};
        std::vector<Nota> salida;

        juce::FileInputStream in (origen);
        if (! in.openedOk()) return salida;

        juce::MidiFile mf;
        if (! mf.readFrom (in)) return salida;
        //  A NEGRAS Y NO A TICKS, que es lo unico que hace que la cuenta no
        //  dependa del PPQ del fichero: `convertTimestampTicksToSeconds` haria
        //  falta el tempo, y aqui el tempo no importa — lo que importa es la
        //  posicion MUSICAL. `getTimeFormat` positivo es ticks por negra.
        const int ppq = mf.getTimeFormat() > 0 ? mf.getTimeFormat() : kPpq;

        juce::MidiMessageSequence todo;
        for (int t = 0; t < mf.getNumTracks(); ++t)
            if (auto* s = mf.getTrack (t))
                todo.addSequence (*s, 0.0);
        todo.updateMatchedPairs();

        //  Cuantas hay ya en cada columna: el motor guarda una raiz y TRES
        //  notas de mas -`stepChord`-, o sea cuatro voces por paso. La quinta
        //  no se pierde en silencio.
        std::vector<int> enPaso ((size_t) juce::jmax (1, maxPasos), 0);

        for (int i = 0; i < todo.getNumEvents(); ++i)
        {
            auto* ev = todo.getEventPointer (i);
            if (ev == nullptr || ! ev->message.isNoteOn() || ev->message.getVelocity() == 0) continue;

            const double negras = ev->message.getTimeStamp() / (double) ppq;
            const int    paso   = (int) std::lround (negras * (double) kPasoNegra);
            const int    semis  = ev->message.getNoteNumber() - kRaizMidi;

            //  El orden importa: primero lo que no cabe en el patron y despues
            //  lo que no cabe en el pad, o una nota fuera de rango del compas
            //  noventa se contaria dos veces.
            if (paso < 0 || paso >= maxPasos) { ++parte.tarde; continue; }
            if (semis < -24 || semis > 24)    { ++parte.fuera; continue; }
            if (enPaso[(size_t) paso] >= 4)   { ++parte.apiladas; continue; }

            //  EL LARGO SALE DE SU PAREJA y no de la nota siguiente: un acorde
            //  tiene cuatro noteOn seguidos y cada uno se apaga cuando le toca.
            //  `updateMatchedPairs` es quien las empareja, y sin el una nota sin
            //  su noteOff -que un fichero cortado tiene- duraria hasta el final.
            double dur = (double) kTicksPaso;
            if (auto* off = ev->noteOffObject)
                dur = juce::jmax (1.0, off->message.getTimeStamp() - ev->message.getTimeStamp());

            Nota n;
            n.paso  = paso;
            n.semis = semis;
            n.vel   = juce::jlimit (1, 127, (int) ev->message.getVelocity());
            //  En CUARTOS de paso, y `kLenMax` es el tope del motor. Una redonda
            //  de cuatro compases no cabe en un paso de esta maquina y se acota
            //  en vez de envolverse: una nota que da la vuelta suena en el sitio
            //  equivocado, que es peor que una corta.
            n.largo = juce::jlimit (0, 63,
                        (int) std::lround (dur / (double) ppq * (double) (kPasoNegra * 4)));

            //  Y UN PASO TIENE UNA SOLA ORTOGRAFIA: el CERO.
            //
            //  `largo` cero y `largo` cuatro son el mismo sonido y el mismo
            //  dibujo -`Voice::gate` con `kGateAuto` dura UN PASO, que es lo
            //  que una casilla dura, y `PianoRoll` pinta la barra del ancho de
            //  una celda con los dos-, asi que son dos formas de escribir lo
            //  mismo y el fichero solo puede llevar una. La canonica es el
            //  cero, que es como NACE un paso: sin esto, importar lo que
            //  acabas de exportar escribe un cuatro en cada casilla que nadie
            //  habia tocado — mismo sonido, y el patron deja de ser el que
            //  era. Es la identidad de la ida y vuelta, que es lo unico que
            //  dice que las dos mitades hablan el mismo idioma.
            if (n.largo == kPasoNegra) n.largo = 0;

            ++enPaso[(size_t) paso];
            ++parte.puestas;
            parte.pasos = juce::jmax (parte.pasos, paso + 1);
            salida.push_back (n);
        }

        //  Por paso y por orden de llegada: la primera de cada columna es la
        //  RAIZ -la que `setStepNote` recibe- y las otras tres van aparte. Sin
        //  ordenar, cual es la raiz dependeria del orden de las pistas del
        //  fichero, que no significa nada.
        std::stable_sort (salida.begin(), salida.end(),
                          [] (const Nota& a, const Nota& b) { return a.paso < b.paso; });
        return salida;
    }
}
