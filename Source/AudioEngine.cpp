#include "AudioEngine.h"
#include <cmath>
#include <limits>

//  `fastTanh` VIVE AHORA EN LA CABECERA (`AudioEngine::fastTanh`).
//
//  Estaba en un namespace anonimo aqui, que es correcto mientras el unico
//  cliente sea esta unidad. Desde que el visor del plato DIBUJA la curva de
//  DRV hay un segundo, y dibujarla con `std::tanh` seria dibujar una curva que
//  el motor no hace: este es un Pade acotado en +-5, y esa cota es una de las
//  cuatro barreras que impiden que una muestra de 1e30 apague la maquina.
using juce::jlimit;


// ============================================================================
//  AudioEngine implementation. See AudioEngine.h for the threading contract.
// ============================================================================

AudioEngine::AudioEngine()
{
    //  LOS PARAMETROS DE LOS EFECTOS, DE SU TABLA. Estaban en diecinueve
    //  llaves de inicializacion repartidas por la cabecera mas dos arrays de
    //  cuatro, y ahora son una tabla al lado de `fxP`, que es lo que hace que
    //  un tipo nuevo sea una fila. Y aqui y no en `prepareToPlay`: esa se
    //  vuelve a llamar en cada cambio de ruta, asi que devolver un efecto a su
    //  valor de fabrica ahi significaria que enchufar unos cascos deshace el
    //  ecualizador en mitad de una sesion.
    for (int f = 0; f < kNumFx; ++f)
        for (int par = 0; par < 3; ++par)
            fxP[(size_t) f][(size_t) par].store (kFxDef[f][par], std::memory_order_relaxed);
    //  Y el EQ, que ademas los APLICA: ver setFxParam.
    eqFx.ponAncho  (kFxDef[kFxEq][0]);
    eqFx.ponSalida (kFxDef[kFxEq][1]);

    for (auto& l : patternLength) l.store (kMinPatLen, std::memory_order_relaxed);

    //  -1 is "silent". Zero-initialised would mean "parked at the very start",
    //  and the UI would draw a read head on a pad that has never played.
    for (auto& p : padPos) p.store (-1.0f, std::memory_order_relaxed);

    //  AUTOCUT on, on every pad. Retriggering a pad over its own tail is the
    //  exception, not the rule: it is what a held chord wants and what a hat,
    //  a stab or a vocal played fast does not. The switch is still there to
    //  turn it off per pad.
    for (auto& c : padSelfCut) c.store (true, std::memory_order_relaxed);

    //  ANCHO a uno, o sea "como viene la muestra". Aqui y no en prepareToPlay:
    //  esa se llama otra vez cada vez que el telefono cambia de ruta, y con
    //  esto dentro, enchufar unos cascos devolveria los sesenta y cuatro pads a
    //  su ancho de fabrica en mitad de una sesion. Cero -que es lo que deja un
    //  array de atomicos- seria peor todavia: la maquina entera en mono.
    for (auto& w : padAncho) w.store (1.0f, std::memory_order_relaxed);

    //  NINGUN PAD MANDA A NINGUN EFECTO, y el cero es el sitio del que se sale.
    //
    //  Estaba al reves - los 64 pads a tope en los seis envios - con el
    //  argumento de que asi encender un efecto se oye en todo el kit. Y se oye:
    //  en TODO el kit, que es justo el problema. Abrir el delay metia en la cola
    //  el bombo, la caja y los sesenta y dos sonidos restantes, asi que la
    //  primera media hora con la maquina se iba en BAJAR cinco envios por cada
    //  uno que querias. Una mezcla se hace subiendo lo que quieres, no apagando
    //  lo que no.
    //
    //  Y de paso sale gratis en CPU: con la mascara a cero el camino de envios
    //  por pad no se recorre hasta que alguien sube el primero.
    //  Y AHORA EL DUEÑO ES EL CANAL, asi que el cero vive en `canalSend`. El
    //  recorte del pad nace en UNO —neutro—, que es lo que hace que un fichero
    //  SIN la propiedad `sends` -o sea uno nuevo- suene por lo que el canal
    //  diga y nada mas. Un cero aqui dejaria la maquina muda pasara lo que
    //  pasara con el canal, que es un valor por defecto que ademas es valido:
    //  el mismo fallo que el `brillo` del `Recipe` y el cero de `padAncho`.
    for (auto& pad : padRecorte) for (auto& s : pad) s.store (1.0f, std::memory_order_relaxed);
    for (auto& c : padCanal)   c.store (0, std::memory_order_relaxed);
    for (auto& ch : canalSend) for (auto& s : ch) s.store (0.0f, std::memory_order_relaxed);
    for (auto& g : canalGain)  g.store (1.0f, std::memory_order_relaxed);
    for (auto& m : canalMute)  m.store (false, std::memory_order_relaxed);
    padSendMask.store (0, std::memory_order_relaxed);
    //  The SMOOTHER, though, starts closed. What it follows is the pad send
    //  times the effect's own MIX, and every MIX starts at zero - starting it
    //  at the pad value instead opened all six sends for the first 20 ms of
    //  the app's life, which with the tone effects meant the dry path was
    //  nearly muted for exactly as long.
    for (auto& pad : smSend)   pad.fill (0.0f);
    //  Este si arranca en UNO y no en cero, que es lo contrario que el envio:
    //  es un fader, y un fader que arranca cerrado deja la maquina muda los
    //  primeros 20 ms de cada arranque.
    smCanalDePad.fill (1.0f);

    //  El filtro de cada pad, abierto del todo. Cero seria 0 Hz - los 64 pads
    //  mudos en el arranque - que es lo que pasa cuando un parametro cuyo
    //  valor neutro NO es cero se deja con el cero del constructor.
    for (auto& c : padCutoff)  c.store (kFiltOpenHz, std::memory_order_relaxed);
    //  Negativo es "ningun paso ha bloqueado esto": el cero seria 0 Hz, o sea
    //  un pad mudo, que es como un valor por defecto que ademas es valido apaga
    //  sesenta sonidos de golpe -ya paso con `brillo` en la fabrica-.
    for (auto& c : pasoCutoff) c.store (-1.0f, std::memory_order_relaxed);
    for (auto& r : padReso)   r.store (0.0f, std::memory_order_relaxed);

    //  Y el estado de las etapas que RETIENEN un valor. Un cambio de ruta
    //  vuelve a pasar por aqui con el motor cargado, y dejar la muestra
    //  retenida del dispositivo anterior es un escalon de continua en la
    //  primera muestra del nuevo.
    drvLp[0] = drvLp[1] = 0.0f;   drvWasActive = false;
    crHold[0] = crHold[1] = 0.0f; crPhase = 0.0f; crWasActive = false;
}

AudioEngine::~AudioEngine()
{
    for (auto*& p : padSample)
        if (p != nullptr) { p->decReferenceCount(); p = nullptr; }
    for (auto& slot : pendingPad)
        if (auto* p = slot.exchange (nullptr)) p->decReferenceCount();
    retired.drain ([] (SampleBuffer* p) { if (p) p->decReferenceCount(); });
}

// ---------------------------------------------------------------------------
//  Audio thread
// ---------------------------------------------------------------------------

void AudioEngine::prepareToPlay (double sampleRate, int maxBlockSize, int inputChannels) noexcept
{
    systemSampleRate = (sampleRate > 0.0) ? sampleRate : 44100.0;
    maxBlock         = (maxBlockSize > 0) ? maxBlockSize : 512;

    //  The record buffer, allocated here and never in the callback. This is
    //  the only moment it can safely change size: JUCE calls prepareToPlay
    //  before the stream starts, so no callback is inside it.
    //
    //  It used to be twenty seconds of mono, which is a hit and not a phrase.
    //  Now it is a minute, and it keeps both channels when the device actually
    //  gives us two - most phones have one microphone and hand back one, and
    //  the take is mono then, honestly rather than by duplication.
    //
    //  A bounce clone never records, so it does not pay for any of it.
    if (! offlineMode)
    {
        recordChannels = juce::jlimit (1, recordAllowStereo ? 2 : 1,
                                       inputChannels > 0 ? inputChannels : 1);

        //  A minute of stereo float at 48 kHz is 23 MB. If the allocation
        //  fails, fall back to something small rather than leaving the
        //  microphone with nowhere to write.
        try
        {
            recordBuffer.setSize (recordChannels, (int) (recordSeconds * systemSampleRate));
        }
        catch (const std::bad_alloc&)
        {
            recordChannels = 1;
            recordBuffer.setSize (1, (int) (5.0 * systemSampleRate));
        }

        recordBuffer.clear();

        //  Y EL BUFFER DEL MONITOR, por lo mismo y aqui mismo: la entrada hay
        //  que guardarla en la etapa 0 porque el `out.clear` de la etapa 2 la
        //  destruye, y la suma ocurre nueve etapas mas abajo. Un bloque, no un
        //  minuto: esto no guarda nada, solo cruza la funcion.
        monitorInChans = juce::jlimit (0, 2, inputChannels);
        monitorBuf.setSize (juce::jmax (1, monitorInChans), maxBlock);
        monitorBuf.clear();
        smMonitor = 0.0f;
    }

    //  How many frames one silhouette column covers. The earlier project put the
    //  analyser's whole window on screen at 1x, and its window is fftSize -
    //  32768 frames, about 0.74 s at 44.1 kHz. Same span here, expressed as a
    //  duration so it holds at any device rate.
    scopeColLen = juce::jmax (1, (int) (0.74 * systemSampleRate / (double) kScopeCols));

    juce::dsp::ProcessSpec spec { systemSampleRate, (juce::uint32) juce::jmax (1, maxBlock), 2 };
    masterFilter.prepare (spec);
    masterFilter.reset();

    hpFilter.prepare (spec);
    hpFilter.reset();
    hpFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);

    //  La FDN reserva sus cuatro lineas y sus dos difusores aqui, que es el
    //  unico sitio donde puede reservar: en el render no se toca memoria.
    reverb.prepare (sampleRate, 2);
    reverb.reset();


    //  One buffer per effect bus plus the scratch a single pad is rendered
    //  into before it is split between the dry path and its sends. Allocated
    //  here for the same reason as everything else in this function: the
    //  callback is not allowed to.
    padScratch.setSize (2, juce::jmax (1, maxBlock));
    padScratch.clear();
    for (auto& b : fxBus) { b.setSize (2, juce::jmax (1, maxBlock)); b.clear(); }
    busRinging.fill (false);

    delayLine.prepare (spec);
    delayLine.setMaximumDelayInSamples (juce::jmax (1, (int) (systemSampleRate * 1.0)));
    delayLine.reset();

    //  Las dos lineas de la familia de modulacion. Cortas: 30 ms de coro y 10
    //  de flanger. Ver AudioEngine.h.
    choLine.prepare (spec);
    choLine.setMaximumDelayInSamples ((int) (systemSampleRate * 0.030) + 4);
    choLine.reset();
    flaLine.prepare (spec);
    flaLine.setMaximumDelayInSamples ((int) (systemSampleRate * 0.010) + 4);
    flaLine.reset();
    for (auto& fila : phaZ) for (auto& z : fila) z = 0.0f;
    flaFbZ[0] = flaFbZ[1] = 0.0f;
    modWasActive.fill (false);
    for (auto& l : mod) l.reinicia();

    //  Y la familia de CARACTER. La linea de PIT se dimensiona al grano mas
    //  largo por DOS -las dos cabezas van desfasadas medio grano y la de atras
    //  lee hasta un grano entero por detras-, y la ventana de FRZ al tope de su
    //  mando: reservar en el hilo de audio esta prohibido, asi que se reserva
    //  aqui y el mando solo mueve CUANTO se usa.
    pitLine.prepare (spec);
    pitLine.setMaximumDelayInSamples ((int) (systemSampleRate * kPitGranoMax * 2.0) + 4);
    pitLine.reset();
    pitFase = 0.0f;
    rngFase = 0.0f;
    for (auto& fila : widAlta) for (auto& st : fila) st = {};
    for (auto& fila : widBaja) for (auto& st : fila) st = {};
    for (auto& fila : excAlta) for (auto& st : fila) st = {};
    for (auto& fila : excBaja) for (auto& st : fila) st = {};
    trnRapido = trnLento = 0.0f;
    for (auto& c : frzVent) { c.assign ((size_t) (systemSampleRate * kFrzVentanaMax) + 4, 0.0f); }
    frzEscritas = 0; frzLee = 0.0f; frzLlena = false; frzOyo = false;
    frzLargo = (int) (systemSampleRate * 0.180);
    carWasActive.fill (false);

    //  El EQ toma la frecuencia nueva y limpia sus diez estados; las bandas NO
    //  se tocan, que esto corre en cada cambio de ruta. Ver Eq5::prepare.
    eqFx.prepare (systemSampleRate);
    //  Y los cuatro de dinamica. `prepare` aqui SI vacia el estado -es una
    //  envolvente y una ganancia suavizada, o sea el pasado de la señal- a
    //  diferencia de `Eq5::prepare`, que no toca las bandas: alli lo que
    //  sobreviviria a un cambio de ruta es el ajuste de la persona, y aqui lo
    //  que sobreviviria seria la cola de un detector que ya no vale.
    for (auto& d : dyn) d.prepare (systemSampleRate);
}

void AudioEngine::releaseResources() noexcept
{
    for (auto& v : voices)
        v.kill();

    //  A tap that arrived while the stream was going away must not fire into
    //  the one that replaces it, seconds later and out of nowhere.
    fallbackTriggers.store (0, std::memory_order_relaxed);
}

void AudioEngine::triggerPad (int slot, int extraSemis, float vel, float from01, bool cortaSuCola,
                              int gate, std::uint32_t plock) noexcept
{
    if (slot < 0 || slot >= kNumPads)
        return;
    auto* sb = padSample[(size_t) slot];
    if (sb == nullptr)
        return;

    //  LA NOTA SALE DE AQUI Y NO DE OTRO SITIO.
    //
    //  triggerPad es el embudo por el que pasan TODOS los disparos - el dedo,
    //  el secuenciador, la cadena, una celda de la cancion, el MIDI que entra -
    //  asi que poner el envio aqui es lo unico que garantiza que no haya un
    //  camino que suene por dentro y no salga por el cable. Ponerlo en
    //  handleCommand, que es donde apetece, se habria dejado fuera al
    //  secuenciador, que es justo lo que la gente quiere mandar al hardware.
    //
    //  Va DESPUES de comprobar que el pad tiene sonido, a proposito: un pad
    //  vacio no suena, y mandar su nota haria que el aparato de al lado tocara
    //  algo que en esta app no se oye.
    //
    //  Y aqui no se envia nada. Se deja escrito - cuatro bytes en una cola sin
    //  cerrojos - y sigue. Enviar reserva memoria y habla con el sistema; esto
    //  es el hilo de audio.
    if (midiOutOn.load (std::memory_order_relaxed))
        midiOut.push ({ (std::uint8_t) slot,
                        (std::uint8_t) juce::jlimit (1, 127, (int) std::lround (vel * 127.0f)),
                        true });

    // Choke group: fade out any other pad's voices sharing this pad's group.
    const int group = padChoke[(size_t) slot].load (std::memory_order_relaxed);
    if (group > 0)
        for (auto& v : voices)
            if (v.active && v.slot >= 0 && v.slot != slot
                && padChoke[(size_t) v.slot].load (std::memory_order_relaxed) == group)
                v.release();

    //  AUTOCUT: this pad cuts its own tail. steal() rather than release() -
    //  the point is that the previous hit is GONE by the time the new one
    //  speaks, so it gets the 1.5 ms declick fade and not the pad's musical
    //  release, which on a long tail would leave the two overlapping for as
    //  long as the release lasts and defeat the whole thing.
    if (cortaSuCola && padSelfCut[(size_t) slot].load (std::memory_order_relaxed))
        for (auto& v : voices)
            if (v.active && v.slot == slot)
                v.steal (systemSampleRate);

    //  LOS CUATRO BLOQUEOS DEL PASO, desempaquetados. Cero en un byte es "este
    //  paso no toca eso" y entonces manda el pad, que es lo que hace que un
    //  patron escrito antes de que esto existiera suene exactamente igual.
    const auto plockPct = [plock] (int cual) noexcept
    {
        const int b = (int) ((plock >> (cual * 8)) & 0xffu);
        return b <= 0 ? kNoPLock : b - 1;
    };
    const int pctAtaque = plockPct (plockAtaque);
    const int pctCaida  = plockPct (plockCaida);
    const int pctInicio = plockPct (plockInicio);
    const int pctPan    = plockPct (plockPan);

    const int len = sb->buffer.getNumSamples();
    int st = padStart[(size_t) slot].load (std::memory_order_relaxed);
    int en = padEnd[(size_t) slot].load (std::memory_order_relaxed);
    if (en <= 0 || en > len) en = len;
    if (st < 0 || st >= en)  st = 0;

    // ------------------------------------------------------------------------
    //  UN PAD QUE LLEVA INSTRUMENTO. Ver SampleBuffer::Zona y Sintes.h.
    //
    //  El mapa viaja DENTRO de la muestra, asi que esto no es una tabla nueva
    //  por pad ni un modo que haya que encender: si la muestra trae zonas, es
    //  un instrumento, y si no, todo sigue exactamente como estaba.
    //
    //  Se elige por CAPA primero y por raiz despues, y no al reves. La capa la
    //  decide la fuerza del golpe -es lo que hace que el instrumento responda
    //  al toque- y dentro de ella se coge la raiz que menos hay que estirar:
    //  con raices cada doce semitonos, seis es lo peor que puede tocar, contra
    //  los veinticuatro de una muestra sola.
    //
    //  Y aqui no se reserva, ni se bloquea, ni se toca un contador de
    //  referencias: es aritmetica sobre un array fijo que ya estaba en memoria.
    const int    zonas   = sb->nZonas;
    const bool   instrum = (zonas > 0);
    float        semis   = padPitch[(size_t) slot].load (std::memory_order_relaxed) + (float) extraSemis;
    bool         bucle   = padLoop[(size_t) slot].load (std::memory_order_relaxed);
    int          vuelta  = -1;

    if (instrum)
    {
        //  La capa: dos, y el corte a la mitad. Un cruce entre capas seria mas
        //  suave y necesita dos voces por nota, o sea el doble de pool por un
        //  matiz - y este motor tiene dieciseis voces en un telefono flojo.
        const int capaQuiere = (vel >= 0.5f) ? 1 : 0;

        int mejor = 0; int coste = 1 << 30;
        for (int z = 0; z < zonas && z < SampleBuffer::kMaxZonas; ++z)
        {
            const auto& Z = sb->zonas[(size_t) z];
            //  La capa pesa mas que la raiz: mil por capa equivocada contra
            //  como mucho veinticuatro de estiramiento, asi que nunca se coge
            //  la capa contraria por estar mas cerca de nota.
            const int c = std::abs ((int) std::lround (semis) - Z.raiz)
                        + (Z.capa != capaQuiere ? 1000 : 0);
            if (c < coste) { coste = c; mejor = z; }
        }

        const auto& Z = sb->zonas[(size_t) mejor];
        semis -= (float) Z.raiz;

        //  EL RECORTE DE UN INSTRUMENTO ES RELATIVO A SU ZONA.
        //
        //  Antes se ignoraba, con este argumento: "una zona no se recorta". Es
        //  falso desde el dedo - INICIO y FIN son de la MUESTRA que el pad
        //  toca, y lo que la persona pide al acortar es exactamente lo que
        //  cualquier sampler hace: si acorto el sonido, lo que da vueltas es
        //  el sonido acortado. Ignorarlo dejaba dos mandos que se movian y no
        //  hacian nada, que es peor que no tenerlos.
        //
        //  En FRACCION y no en muestras porque un pad de instrumento son diez
        //  zonas pegadas: el mismo numero absoluto cae dentro de la primera
        //  octava y fuera de la quinta. Asi el mismo recorte significa lo
        //  mismo toque la nota que toque.
        const int zLen = juce::jmax (4, Z.fin - Z.ini);
        const double f0 = (double) st / (double) juce::jmax (1, len);
        const double f1 = (double) en / (double) juce::jmax (1, len);
        st = Z.ini + (int) (f0 * (double) zLen);
        en = Z.ini + (int) (f1 * (double) zLen);
        if (en <= st + 4) en = juce::jmin (Z.fin, st + 4);

        bucle = (Z.bucleFin > Z.bucleIni);

        //  Y EL BUCLE VIVE DENTRO DEL RECORTE. La zona vuelve a su punto de
        //  bucle -que es donde acaba el ataque, para no repetir la pua en cada
        //  vuelta- pero si el recorte se ha comido ese punto, lo que se repite
        //  es el trozo entero: es lo unico que "hazme un bucle de ESTO" puede
        //  significar. La costura de ahi la suaviza SUAVE IN/OUT, que por esto
        //  vuelve a estar viva en un pad de instrumento.
        vuelta = bucle ? ((Z.bucleIni >= st && Z.bucleIni < en) ? Z.bucleIni : st)
                       : -1;

        //  `from01` si se ignora: audicionar desde un punto de la onda es de
        //  una muestra, y la onda de un instrumento son diez zonas pegadas.
        from01 = -1.0f;
    }

    //  EL LARGO QUE NADIE DIJO. Ver kGateAuto.
    //
    //  Solo donde hace falta, que son las zonas que DAN VUELTAS: esas no
    //  terminan nunca, y un paso -lo que dura una casilla- es lo que un
    //  secuenciador escribe cuando no dice otra cosa. Las siete familias que no
    //  sostienen -piano, plucks, campanas, guitarra, mazos, claves, arpas- se
    //  acaban solas como una muestra cualquiera, y ponerles un paso habria
    //  cortado una campana de dos segundos a los 125 ms. En percusion, igual:
    //  aqui no cambia nada de lo que habia.
    if (gate == kGateAuto || gate == kGateAudicion)
        gate = (instrum && bucle)
                 ? (gate == kGateAuto ? juce::jmax (32, (int) samplesPerStepNow())
                                      : juce::jmax (32, (int) (kAudicionSeg * systemSampleRate)))
                 : kGateSuelta;

    //  EL BLOQUEO DEL INICIO entra por la misma puerta que la audicion desde
    //  la onda -empezar en un punto y conservar el final del pad- porque es
    //  literalmente lo mismo. Solo si nadie ha pedido ya un punto: un toque
    //  sobre la onda es de la persona y manda sobre lo que diga el paso.
    if (from01 < 0.0f && pctInicio != kNoPLock)
        from01 = (float) pctInicio / 100.0f;

    //  Auditioning from a point in the waveform: start there and keep the
    //  pad's end, so a tap plays the rest of the sound and not a slice of it.
    //  A tap past the end would start a voice with nothing left to read.
    if (from01 >= 0.0f)
    {
        const int at = (int) (juce::jlimit (0.0f, 1.0f, from01) * (float) len);
        if (at < en - 2) st = juce::jmax (0, at);
    }

    triggeredMask.fetch_or ((std::uint64_t) 1u << slot, std::memory_order_relaxed);

    //  Pick a voice out of the shared pool. A free one if there is one, and
    //  otherwise the oldest - by serial, so "oldest" means the one that has
    //  been sounding longest rather than whichever slot the counter happens
    //  to be pointing at. Two caps decide when we steal: the pad's own share
    //  of the pool, so one held pad cannot starve the other fifteen, and the
    //  pool itself.
    //
    //  A stolen voice gets steal(), which is a fast fade rather than a cut -
    //  it keeps the same voice alive for a few milliseconds while the new
    //  note starts elsewhere. Only when the WHOLE pool is busy does the new
    //  note have to land on the voice being taken, and then the fade has
    //  nowhere to happen. At 48 voices that is rare and it is buried.
    Voice* chosen  = nullptr;
    Voice* oldest  = nullptr;
    Voice* oldestOnPad = nullptr;
    int    onPad   = 0;

    //  Only as far as the device's pool goes. The array is always 64; a
    //  low-tier phone plays the first sixteen of it and never pays for the
    //  rest, which is the whole point of the tier.
    for (int vi = 0; vi < voiceLimit; ++vi)
    {
        auto& v = voices[(size_t) vi];

        if (! v.active)
        {
            if (chosen == nullptr) chosen = &v;
            continue;
        }

        if (oldest == nullptr || v.serial < oldest->serial)
            oldest = &v;

        if (v.slot == slot)
        {
            ++onPad;
            if (oldestOnPad == nullptr || v.serial < oldestOnPad->serial)
                oldestOnPad = &v;
        }
    }

    if (onPad >= maxVoicesOnPad && oldestOnPad != nullptr)
    {
        oldestOnPad->steal (systemSampleRate);
        if (chosen == nullptr) chosen = oldestOnPad;
    }
    else if (chosen == nullptr && oldest != nullptr)
    {
        oldest->steal (systemSampleRate);
        chosen = oldest;
    }

    if (chosen == nullptr)
        return;

    //  El pad del bombeo abre la envolvente de golpe. Aqui, en triggerPad, y
    //  no en el secuenciador: asi vale igual tocado a dedo que disparado por
    //  un paso, que es lo que un sidechain tiene que hacer.
    if (slot == duckPad.load (std::memory_order_relaxed))
        duckEnv = 1.0f;

    chosen->serial = ++voiceSerial;
    chosen->start (slot,
                   semis,
                   effectiveGain (slot),
                   sb->sourceSampleRate, systemSampleRate,
                   st, en,
                   bucle,
                   ! instrum && padReverse[(size_t) slot].load (std::memory_order_relaxed),
                   len,
                   pctPan    != kNoPLock ? plockPanPos  (pctPan)
                                         : padPan[(size_t) slot].load (std::memory_order_relaxed),
                   pctAtaque != kNoPLock ? plockAtaqueMs (pctAtaque)
                                         : padAttack[(size_t) slot].load (std::memory_order_relaxed),
                   pctCaida  != kNoPLock ? plockCaidaMs (pctCaida)
                                         : padRelease[(size_t) slot].load (std::memory_order_relaxed),
                   ! instrum && padKeepLength[(size_t) slot].load (std::memory_order_relaxed),
                   vel,
                   //  LOS DOS SUAVE VALEN TAMBIEN EN UN INSTRUMENTO desde que
                   //  el recorte lo recorta: la zona trae su cruce horneado
                   //  para SU bucle, y en cuanto la persona mueve INICIO o FIN
                   //  la costura pasa a ser suya y ese cruce ya no la tapa.
                   padFadeIn[(size_t) slot].load (std::memory_order_relaxed),
                   padFadeOut[(size_t) slot].load (std::memory_order_relaxed),
                   vuelta,
                   padAncho[(size_t) slot].load (std::memory_order_relaxed));

    //  DESPUES de start, por lo mismo que el gate: la pone a false y el
    //  bloqueo de pan es del PASO. Sin esto el pan bloqueado dura un bloque -
    //  retarget vuelve a leer el del pad en el siguiente.
    chosen->panPropio = (pctPan != kNoPLock);

    //  DESPUES de start, que la pone a -1: el largo lo trae el paso y no el
    //  pad, asi que el mismo pad puede sonar corto en un sitio y largo en otro
    //  dentro del mismo patron. Es lo que separa una caja de ritmos de un
    //  instrumento.
    chosen->gate = gate;
}

void AudioEngine::renderNextBlock (juce::AudioBuffer<float>& out,
                                   int startSample, int numSamples) noexcept
{
    juce::ScopedNoDenormals noDenormals;

    //  0a. One renderer at a time. A phone changes audio route by tearing the
    //      stream down and building a new one, and the old callback thread can
    //      still be in here when the new one arrives. Everything below assumes
    //      it is alone: the command queue is single-consumer by contract, and
    //      two consumers do not glitch it, they WEDGE it - permanently, for
    //      the life of the process. This is the whole bug behind "I unplugged
    //      my headphones and the pads went dead while TEST still beeped": the
    //      test tone is a lone atomic and survived, the pads went through the
    //      queue and did not.
    //
    //      Losing one block during a route change is inaudible. Losing the
    //      transport is the app.
    if (inRender.exchange (true, std::memory_order_acquire))
    {
        out.clear (startSample, numSamples);
        return;
    }

    struct RenderGuard
    {
        std::atomic<bool>& flag;
        ~RenderGuard() { flag.store (false, std::memory_order_release); }
    } renderGuard { inRender };

    // 0. Capture mic input BEFORE clearing (input is in channel 0 on entry).
    //    Only when the take is coming from the microphone: a resample reads
    //    the master at the bottom of this function instead.
    if (recording.load (std::memory_order_acquire)
        && ! recordFromMaster.load (std::memory_order_acquire)
        && out.getNumChannels() > 0)
    {
        const int cap = recordBuffer.getNumSamples();
        int rp = recordPos.load (std::memory_order_relaxed);
        const int n = juce::jmin (numSamples, cap - rp);
        if (n > 0)
        {
            //  However many channels the device is giving us, up to what the
            //  buffer was sized for. On a phone with one microphone that is
            //  one, and the take stays mono.
            const int chans = juce::jmin (recordBuffer.getNumChannels(), out.getNumChannels());
            for (int ch = 0; ch < chans; ++ch)
                recordBuffer.copyFrom (ch, rp, out.getReadPointer (ch, startSample), n);

            rp += n;
            recordPos.store (rp, std::memory_order_relaxed);
        }
        if (rp >= cap)
            recording.store (false, std::memory_order_release);   // full -> auto stop
    }

    //  0-mon. Y LA COPIA PARA EL MONITOR, aqui y no mas abajo: la entrada vive
    //        en `out` hasta la etapa 2, que la borra. La suma se hace en 5d-mon
    //        -antes del master- y para entonces esto ya no existe.
    //
    //        Se copia si el monitor esta pedido O si todavia se esta apagando:
    //        sin la segunda mitad, apagarlo cortaria la rampa a la mitad y eso
    //        es un click, que es justo lo que la rampa existe para no dar.
    const bool monPedido = monitorTarget.load (std::memory_order_relaxed) > 0.0f
                           || smMonitor > 1.0e-6f;
    if (monPedido && monitorInChans > 0)
    {
        const int chans = juce::jmin (monitorInChans, out.getNumChannels());
        const int n     = juce::jmin (numSamples, monitorBuf.getNumSamples());
        for (int ch = 0; ch < chans; ++ch)
            monitorBuf.copyFrom (ch, 0, out.getReadPointer (ch, startSample), n);
    }

    // 0b. Latency probe: arm on the first block after the request, so the
    //     record index and the click's frame share one timeline (the capture
    //     above and this counter both advance by numSamples per callback).
    if (probeArm.exchange (false, std::memory_order_acq_rel))
    {
        probeCounter = 0;
        probeClickAt = (int) (0.30 * systemSampleRate);   // let the stream settle first
        probeLength  = (int) (1.20 * systemSampleRate);
        recordPos.store (0, std::memory_order_relaxed);
        recording.store (true, std::memory_order_release);
        probing.store (true, std::memory_order_release);
    }

    // 1. Adopt freshly published per-pad samples, and let go of cleared ones.
    //    A clear is consumed FIRST so that "empty this pad, then load a new
    //    sound into it" in the same block ends with the new sound rather than
    //    with nothing.
    for (int slot = 0; slot < kNumPads; ++slot)
    {
        if (pendingClear[(size_t) slot].exchange (false, std::memory_order_acquire))
        {
            for (auto& v : voices)
                if (v.active && v.slot == slot)
                    v.kill();

            retired.push (padSample[(size_t) slot]);
            padSample[(size_t) slot] = nullptr;
            padStart[(size_t) slot].store (0, std::memory_order_relaxed);
            padEnd[(size_t) slot].store (0, std::memory_order_relaxed);
        }

        if (auto* incoming = pendingPad[(size_t) slot].exchange (nullptr, std::memory_order_acquire))
        {
            retired.push (padSample[(size_t) slot]);
            padSample[(size_t) slot] = incoming;
        }
    }

    //  Y LA TABLA DE CLIPS, por el mismo camino y por la misma razon: un
    //  intercambio de puntero, la vieja a retirar, y el hilo de audio sin
    //  reservar ni soltar nada. Ver publicaClips.
    if (auto* nueva = pendingClips.exchange (nullptr, std::memory_order_acquire))
    {
        //  La que sale va a la cola: soltarla aqui seria un `delete` en el hilo
        //  de audio, que es lo unico que este hilo no puede hacer.
        clipsRetiradas.push (clips);
        clips = nueva;
        clipsVivos.store (nueva->n, std::memory_order_relaxed);
    }

    //  Y LA DE AUTOMATIZACION, por el mismo camino y con la misma cola: la que
    //  sale se retira para que la suelte el hilo de mensajes, porque `delete`
    //  aqui es lo unico que este hilo no puede hacer. Una cola y no un hueco
    //  por lo mismo que la de clips - dos publicaciones adoptadas dentro del
    //  mismo tic del temporizador perderian una tabla entera.
    if (auto* nueva = pendingAuto.exchange (nullptr, std::memory_order_acquire))
    {
        autoRetiradas.push (autom);
        autom = nueva;
        autoVivos.store (nueva->n, std::memory_order_relaxed);
    }

    // 2. Clear output.
    out.clear (startSample, numSamples);

    // 2b. Work out, once per block, where each pad's signal is going: how
    //     much of it into each effect bus, and how much is left for the dry
    //     path. The tone effects take a pad off dry by the same amount they
    //     take it on to their own bus, so a fully-sent filter replaces the
    //     sound instead of sitting beside it. Delay and reverb add on top.
    //
    //     Sends are smoothed per block for the same reason every other knob
    //     here is: a raw jump in a gain that is being summed is a click.
    const int  busChans = juce::jmin (2, out.getNumChannels());
    const float kSend   = 1.0f - std::exp ((float) -numSamples / (0.020f * (float) systemSampleRate));
    //  La mezcla de cada tipo, que es siempre `param[2]`. Era una lista
    //  literal de once cargas atomicas, o sea un sitio mas que escribir a mano
    //  por cada tipo nuevo — y el peor de los tres, porque lo que falta se
    //  inicializa a 0.0f y un efecto MUDO no da ningun aviso. Ver `fxP`.
    float fxMixNow[kNumFx];
    for (int f = 0; f < kNumFx; ++f)
        fxMixNow[f] = juce::jlimit (0.0f, 1.0f, fxP[(size_t) f][2].load (std::memory_order_relaxed));

    float sendGain[kNumPads][kNumFx];
    float dryGain[kNumPads];
    //  Lo que el CANAL escala, aparte del seco: el medidor mide lo que pasa por
    //  el canal y no lo que le queda al pad despues de que los insertos se
    //  lleven su parte -con `dryGain` un inserto al 100% dejaria el medidor a
    //  cero justo cuando el canal mas trabaja-.
    float canGain[kNumPads];
    bool  busFed[kNumFx] = {};
    bool  padSplit[kNumPads];

    //  Quien decide si un pad se salta el bucle largo es la MASCARA: si el pad
    //  no manda a nadie, `setPadSend` garantiza que sus envios valen cero y el
    //  producto de mas abajo sale cero pase lo que pase con las mezclas. Aqui
    //  vivia ademas un `anyFxOpen` que sumaba las siete mezclas, y salio de la
    //  condicion hace tandas por lo que dice el parrafo de abajo: se quedo
    //  calculado y sin leer, o sea una cuenta por bloque que no decide nada.
    const std::uint64_t sendMask = padSendMask.load (std::memory_order_relaxed);
    //  Y la de los filtros, que decide lo mismo: un pad filtrado tiene que
    //  renderizarse APARTE aunque no mande a ningun efecto, porque no se puede
    //  filtrar una senal que ya se sumo con otras quince.
    const std::uint64_t filtMask = padFiltMask.load (std::memory_order_relaxed);
    //  UNA vez por bloque, no una por pad: es un atomico que la cara mueve con
    //  el dedo y el bucle lo consulta sesenta y cuatro veces.
    const int miraCan = canalMirado.load (std::memory_order_relaxed);
    float picoCan = 0.0f;

    for (int p = 0; p < kNumPads; ++p)
    {
        const bool filtered = (filtMask >> (unsigned) p) & 1ull;
        //  ...y el suavizado tiene que TERMINAR de bajar antes de saltarse el
        //  pad, o un envio que se cierra se queda congelado a medio camino en
        //  vez de irse a cero: silencio a medias que no se va nunca. Por eso
        //  el corte mira tambien smSendHot, que es el pad que aun se mueve.
        //  Y SIN `anyFxOpen`, que sobraba y ademas deshacia justo lo que la
        //  mascara existe para hacer. Si el pad no esta en la mascara,
        //  `setPadSend` garantiza que sus seis envios valen cero, asi que el
        //  producto de mas abajo vale cero pase lo que pase con las mezclas -
        //  y con el termino puesto bastaba abrir UN efecto para que los
        //  sesenta y cuatro pads volvieran al bucle largo: 384 cargas
        //  atomicas y 384 pasos de suavizado por bloque. Medido con la fila
        //  nueva de Tests/Cpu.cpp, que es la que faltaba para verlo.
        const bool listed = (sendMask >> (unsigned) p) & 1ull;

        //  EL CANAL DEL PAD, y su ganancia y su mute. Se leen una vez por pad y
        //  por bloque, como el pan y el ancho: multiplican lo que ya se
        //  calculaba -el seco y los envios- asi que no hay una etapa nueva.
        //
        //  Y el suavizado es el MISMO `kSend` de 20 ms que los envios: un fader
        //  sin suavizar da un salto de nivel en el borde del bloque, que es
        //  exactamente el chasquido que la constante existe para no tener.
        const int   canal = (int) padCanal[(size_t) p].load (std::memory_order_relaxed);
        const float gCan  = canalMute[(size_t) canal].load (std::memory_order_relaxed)
                              ? 0.0f
                              : canalGain[(size_t) canal].load (std::memory_order_relaxed);
        float& smCan = smCanalDePad[(size_t) p];
        smCan += kSend * (gCan - smCan);
        const bool canalHot = std::abs (smCan - gCan) > 0.0005f;
        //  Y si es el canal que la cara esta MIRANDO, el pad se aparta aunque
        //  no mande a nadie: no se puede medir lo que ya se sumo con otros
        //  quince. Ver `miraCanal` — son los pads de UN canal, no los 64.
        const bool medido = (canal == miraCan);
        canGain[p] = smCan;

        if (! listed && ! smSendHot[(size_t) p] && ! medido)
        {
            dryGain[p]  = smCan;
            //  Un canal con el fader en uno y sin mute no obliga a nada: el pad
            //  sigue yendo entero al master por el camino corto, que es el que
            //  la mascara existe para proteger. Solo se aparta si el canal lo
            //  esta MOVIENDO o lo ha bajado.
            //  Y «distinto de uno» por los DOS lados, que es el mismo fallo
            //  que el guardia del master tenia y por el mismo motivo: el
            //  camino corto renderiza las voces DIRECTAMENTE en la salida y no
            //  multiplica por `dryGain`, asi que solo vale cuando el canal
            //  esta en la unidad clavada. Con `smCan < 0.9995f` un canal por
            //  ENCIMA de 0 dB tomaba el camino corto y su ganancia no se
            //  aplicaba nunca: bajar el fader se oia y subirlo no.
            padSplit[p] = filtered || canalHot || std::abs (smCan - 1.0f) > 0.0005f;
            for (int f = 0; f < kNumFx; ++f) sendGain[p][f] = 0.0f;
            continue;
        }

        float dry = 1.0f;
        bool  any = false;
        bool  hot = canalHot;
        for (int f = 0; f < kNumFx; ++f)
        {
            //  EL PRODUCTO DE TRES: la mezcla del efecto, lo que el CANAL manda
            //  y el recorte con el que el pad se guardo. Los dos ultimos son la
            //  linea entera del cambio: el envio dejo de ser del pad.
            const float target = fxMixNow[f]
                                   * canalSend[(size_t) canal][(size_t) f].load (std::memory_order_relaxed)
                                   * padRecorte[(size_t) p][(size_t) f].load (std::memory_order_relaxed);
            float& sm = smSend[(size_t) p][(size_t) f];
            sm += kSend * (target - sm);
            const float g = (sm < 0.0005f && target < 0.0005f) ? 0.0f : sm;
            sendGain[p][f] = g * smCan;
            if (sendGain[p][f] > 0.0f) { any = true; busFed[f] = true; }
            if (sm != 0.0f) hot = true;      // aun no ha terminado de bajar
            //  La resta del seco va con la parte SIN la ganancia del canal: lo
            //  que el inserto se lleva es una fraccion del pad, y el fader del
            //  canal escala las dos mitades por igual mas abajo. Con `g * smCan`
            //  aqui, bajar el canal a la mitad devolveria seco al pad.
            if (fxSustituye[f]) dry *= (1.0f - g);
        }
        dryGain[p]  = dry * smCan;
        padSplit[p] = any || filtered || canalHot
                          || std::abs (smCan - 1.0f) > 0.0005f || medido;
        smSendHot[(size_t) p] = hot;
    }

    for (int f = 0; f < kNumFx; ++f)
        if (busFed[f] || busRinging[f])
            fxBus[(size_t) f].clear (startSample, numSamples);

    auto renderVoices = [&] (int s, int nn) noexcept
    {
        //  One pass over the pool: bucket the live voices by pad, and read
        //  each pad's gain and pan ONCE instead of once per voice that
        //  happens to be on it.
        padFirstVoice.fill (-1);

        float padGainNow[kNumPads], padPanNow[kNumPads], padAnchoNow[kNumPads];
        bool  padTouched[kNumPads] = {};

        for (int v = voiceLimit - 1; v >= 0; --v)
        {
            auto& vc = voices[(size_t) v];
            if (! vc.active || vc.slot < 0 || vc.slot >= kNumPads) continue;

            if (! padTouched[vc.slot])
            {
                padTouched[vc.slot] = true;
                padGainNow[vc.slot] = effectiveGain (vc.slot);
                padPanNow [vc.slot] = padPan[(size_t) vc.slot].load (std::memory_order_relaxed);
                padAnchoNow[vc.slot] = padAncho[(size_t) vc.slot].load (std::memory_order_relaxed);
            }

            // Control-rate retarget: a looping or long voice keeps following
            // its pad's VOLUME and PAN instead of freezing start()'s values.
            vc.retarget (padGainNow[vc.slot], padPanNow[vc.slot], padAnchoNow[vc.slot]);

            voiceNextInPad[(size_t) v] = padFirstVoice[(size_t) vc.slot];
            padFirstVoice[(size_t) vc.slot] = v;
        }

        //  A pad that sends nowhere goes straight to the master, exactly as
        //  before. One that does is rendered on its own first, because you
        //  cannot take a share of a signal that has already been summed with
        //  fifteen others.
        for (int p = 0; p < kNumPads; ++p)
        {
            if (padFirstVoice[(size_t) p] < 0)      // nothing of this pad is sounding
                continue;

            if (! padSplit[p])
            {
                for (int v = padFirstVoice[(size_t) p]; v >= 0; v = voiceNextInPad[(size_t) v])
                    voices[(size_t) v].render (out, s, nn, padSample[(size_t) p]);
                continue;
            }

            for (int ch = 0; ch < 2; ++ch)
                padScratch.clear (ch, s, nn);

            for (int v = padFirstVoice[(size_t) p]; v >= 0; v = voiceNextInPad[(size_t) v])
                voices[(size_t) v].render (padScratch, s, nn, padSample[(size_t) p]);

            //  EL FILTRO DEL PAD, sobre lo que el pad acaba de sonar y antes
            //  de repartirlo: el seco y los seis envios salen todos del mismo
            //  sitio, asi que filtrar aqui filtra las siete rutas de una vez.
            //  Filtrar despues habria querido decir siete filtros por pad.
            if ((filtMask >> (unsigned) p) & 1ull)
            {
                //  Coeficientes UNA VEZ POR BLOQUE, no por muestra: la tangente
                //  cuesta lo que cuesta y el corte lo mueve un dedo, no el
                //  audio. Y en float por muestra, en double por bloque.
                const float hz = juce::jlimit (20.0f, (float) (0.45 * systemSampleRate),
                                               corteVivo (p));
                const float rs = padReso[(size_t) p].load (std::memory_order_relaxed);
                //  Q de 0.707 (Butterworth, sin pico) a 8. Mas arriba el filtro
                //  se pone a oscilar solo, que es un sintetizador y no un
                //  sampler: 8 son unos 18 dB de realce, suficiente para que un
                //  barrido cante y poco para que se desmande.
                const float q = 0.707f + rs * (8.0f - 0.707f);
                const float g = (float) std::tan (juce::MathConstants<double>::pi * (double) hz / systemSampleRate);
                const float k = 1.0f / q;
                const float a1 = 1.0f / (1.0f + g * (g + k));
                const float a2 = g * a1;
                const float a3 = g * a2;

                for (int ch = 0; ch < busChans; ++ch)
                {
                    auto& st = padFiltState[(size_t) p][(size_t) juce::jmin (ch, 1)];
                    float ic1 = st.ic1, ic2 = st.ic2;
                    float* d = padScratch.getWritePointer (ch, s);

                    for (int i = 0; i < nn; ++i)
                    {
                        const float x  = d[i];
                        const float v3 = x - ic2;
                        const float v1 = a1 * ic1 + a2 * v3;
                        const float v2 = ic2 + a2 * ic1 + a3 * v3;
                        ic1 = 2.0f * v1 - ic1;
                        ic2 = 2.0f * v2 - ic2;
                        d[i] = v2;                    // paso bajo
                    }

                    //  Los integradores, no las muestras: si uno se va a NaN
                    //  -una muestra envenenada entrando con Q alta- se queda
                    //  ahi para siempre y el pad enmudece hasta reiniciar,
                    //  porque el estado se realimenta. La barrera del master
                    //  limpia la SALIDA y no puede limpiar esto.
                    if (! std::isfinite (ic1) || ! std::isfinite (ic2)) ic1 = ic2 = 0.0f;
                    st.ic1 = ic1; st.ic2 = ic2;
                }
            }

            //  EL PICO DEL CANAL MIRADO, aqui y no en el master: aqui el pad
            //  suena SOLO -para eso existe padScratch- y por el fader del canal
            //  ya ha pasado. Un barrido por pad medido y por bloque, y solo de
            //  los pads de UN canal. Ver `miraCanal`.
            if (miraCan >= 0 && (int) padCanal[(size_t) p].load (std::memory_order_relaxed) == miraCan)
            {
                float mn = 0.0f, mx = 0.0f;
                for (int ch = 0; ch < busChans; ++ch)
                {
                    const auto r = juce::FloatVectorOperations::findMinAndMax (
                                       padScratch.getReadPointer (ch, s), nn);
                    mn = juce::jmin (mn, r.getStart());
                    mx = juce::jmax (mx, r.getEnd());
                }
                picoCan = juce::jmax (picoCan,
                                      juce::jmax (std::abs (mn), std::abs (mx)) * canGain[p]);
            }

            if (dryGain[p] > 0.0005f)
                for (int ch = 0; ch < busChans; ++ch)
                    out.addFrom (ch, s, padScratch, ch, s, nn, dryGain[p]);

            for (int f = 0; f < kNumFx; ++f)
                if (sendGain[p][f] > 0.0f)
                    for (int ch = 0; ch < busChans; ++ch)
                        fxBus[(size_t) f].addFrom (ch, s, padScratch, ch, s, nn, sendGain[p][f]);
        }
    };

    // 3. Drain UI trigger commands (taps fire at block start — human jitter
    //    dwarfs one block; the sequencer below is the sample-accurate path).
    //  Y CADA COLA CON SU PRESUPUESTO.
    //
    //  Las dos vaciaban en el MISMO array y con la MISMA n, asi que la de la
    //  interfaz se comia el cubo entero: con 255 comandos de dedo en un bloque,
    //  a MIDI le quedaba UNO y el resto se CONSUMIA -finishedRead avanza- y se
    //  perdia en silencio, sin pasar siquiera por droppedCommands. Dos colas
    //  por contrato y un solo cubo entre las dos es media cola.
    //
    //  Sesenta y cuatro huecos para MIDI: son mas que las voces que caben, o
    //  sea mas de los que pueden sonar a la vez.
    constexpr int kMaxCmds  = 256;
    constexpr int kCupoMidi = 64;
    constexpr int kTopeUi   = kMaxCmds - kCupoMidi;
    Command local[kMaxCmds];
    int n = 0;
    int tirados = 0;
    commands.drain ([&local, &n, &tirados] (const Command& c) noexcept
                    { if (n < kTopeUi) local[n++] = c; else ++tirados; });
    //  ...y la de MIDI, en el mismo sitio y con el mismo trato: un teclado no
    //  es un ciudadano de segunda, dispara igual que un dedo. Dos colas, un
    //  consumidor.
    midiCommands.drain ([&local, &n, &tirados] (const Command& c) noexcept
                        { if (n < kMaxCmds) local[n++] = c; else ++tirados; });
    //  Y LO TIRADO SE CUENTA. Un tope que se supera en silencio no protege,
    //  esconde - es la misma frase que ya costo la novena tapa de
    //  layoutModuleBar.
    if (tirados > 0) droppedCommands.fetch_add (tirados, std::memory_order_relaxed);

    //  CUANTIZAR EL DISPARO EN DIRECTO. Ver setLiveQuantise.
    //
    //  Al SIGUIENTE paso, no al mas cercano: el mas cercano puede estar en el
    //  pasado y no hay forma de disparar hacia atras. Con una ventana en la
    //  mitad del paso - si acabas de pasar uno, suena ya; si estas llegando al
    //  siguiente, espera - que es lo que redondear al mas cercano significa
    //  cuando solo se puede esperar.
    const bool quantiseNow = liveQuant.load (std::memory_order_relaxed)
                          && playing.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        if (quantiseNow && local[i].type == Command::Type::NoteOn && numPending < (int) pending.size())
        {
            const double sps = samplesPerStepNow();
            if (sps > 1.0)
            {
                const double toNext = sps - stepAccum;
                if (toNext > sps * 0.5)
                {
                    pending[(size_t) numPending++] = { (int) toNext, local[i].slot,
                                                       (int) local[i].semitones, local[i].velocity };
                    continue;
                }
            }
        }
        handleCommand (local[i]);
    }

    //  3b. The lifeboat. Anything the queue refused arrives here instead, as
    //      one bit per pad. It fires at the pad's own settings because that is
    //      all a bit can carry, which is exactly enough to keep playing.
    if (const auto mask = fallbackTriggers.exchange (0, std::memory_order_acquire))
        for (int p = 0; p < kNumPads; ++p)
            if ((mask & ((std::uint64_t) 1u << p)) != 0)
                triggerPad (p);

    // 4+5. Sequencer transport + voice rendering, sample-accurate: the block
    //      is split at step boundaries, each step fires exactly on its frame
    //      (the old version quantised every step to frame 0 of the block —
    //      up to ~12 ms of jitter and flams at high BPM/large blocks).
    const bool isPlaying = playing.load (std::memory_order_relaxed);
    if (isPlaying && ! wasPlaying)
    {
        currentStep = -1; stepAccum = 0.0; chainPos = 0; numPending = 0;
        songStep = -1; songBar.store (-1, std::memory_order_relaxed);
        //  Y el paso de la automatizacion, que si no quien graba seguiria
        //  escribiendo en el ultimo paso que sono antes de parar.
        pasoAuto.store (-1, std::memory_order_relaxed);
        for (int ln = 0; ln < kSongLanes; ++ln) { lanePattern[ln] = -1; laneStartStep[ln] = 0; }
        playStep.store (-1, std::memory_order_relaxed);
        //  Y EL CLIC EMPIEZA EN EL PRIMER TIEMPO. Sin esto, el tono fuerte cae
        //  donde lo dejo la vez anterior y el metronomo dice que el compas
        //  empieza en un sitio que no es - que es peor que no tenerlo.
        clicPaso = 0;
        //  Y si hay cuenta atras armada, la cancion espera al borde de compas.
        arranqueEnBorde = cuentaPasos.load (std::memory_order_relaxed) > 0;
    }
    else if (! isPlaying && wasPlaying)
    {
        playStep.store (-1, std::memory_order_relaxed);
        //  Parar cancela la cuenta atras: si no, volver a dar a PLAY se comeria
        //  los pasos que quedaran de la anterior sin que nadie los hubiera
        //  pedido.
        cuentaPasos.store (0, std::memory_order_relaxed);
        //  Y AL PARAR, EL PAD VUELVE A SER EL PAD. El bloqueo de corte dura
        //  hasta que otro paso diga otra cosa, y con el transporte parado no va
        //  a decirlo nadie: sin esto, parar en mitad de un barrido dejaba el
        //  pad filtrado a 200 Hz y el mando CORTE diciendo 8 kHz.
        for (int p = 0; p < kNumPads; ++p)
            if (pasoCutoff[(size_t) p].load (std::memory_order_relaxed) > 0.0f)
            {
                pasoCutoff[(size_t) p].store (-1.0f, std::memory_order_relaxed);
                refreshFiltMask (p);
            }
    }
    wasPlaying = isPlaying;

    if (! isPlaying)
    {
        renderVoices (startSample, numSamples);
    }
    else
    {
        const int chainLen = chainLength.load (std::memory_order_relaxed);
        if (chainLen <= 0) chainPos = 0;
        int patternIdx = chainLen > 0 ? chainSlots[(size_t) chainPos].load (std::memory_order_relaxed)
                                      : editPattern.load (std::memory_order_relaxed);
        playingPattern.store (patternIdx, std::memory_order_relaxed);

        //  Cuanto dura un paso, en negras. Era 0.25 fijo - semicorcheas - y por
        //  eso no habia forma de escribir un tresillo ni de bajar a fusas: el
        //  patron entero estaba clavado a la rejilla de 1/16.
        const double beatsPerStep   = (double) stepBeats.load (std::memory_order_relaxed);
        const double secPerStep     = (60.0 / juce::jmax (20.0, (double) bpm.load (std::memory_order_relaxed))) * beatsPerStep;
        const double samplesPerStep = juce::jmax (1.0, secPerStep * systemSampleRate);

        //  A step no longer speaks once, on the beat. Swing pushes it late and
        //  a roll makes it speak several times, so what a step produces is a
        //  little list of hits with sample offsets, and the render loop below
        //  stops at each of them.
        auto firePatternStep = [this, samplesPerStep] (int bank, int stepInPattern) noexcept
        {
            const std::uint64_t mask = patternBank[(size_t) bank][(size_t) stepInPattern].load (std::memory_order_relaxed);
            if (mask == 0) return;

            //  Swing: the odd sixteenths arrive late by a fraction of a step.
            //  The even ones never move - that is what keeps the bar where it
            //  was while the feel changes.
            const float sw = swing.load (std::memory_order_relaxed);
            const int lateBy = ((stepInPattern & 1) != 0)
                                 ? (int) ((double) (sw - 0.5f) * 2.0 * samplesPerStep * 0.5)
                                 : 0;

            for (int p = 0; p < kNumPads; ++p)
            {
                if ((mask & ((std::uint64_t) 1u << p)) == 0) continue;

                const int semis = (int) stepNote[(size_t) bank][(size_t) stepInPattern][(size_t) p].load (std::memory_order_relaxed);

                //  Zero means "never set", which is every pattern made before
                //  these existed - so zero reads as full and as a single hit.
                const int rawV = (int) stepVel [(size_t) bank][(size_t) stepInPattern][(size_t) p].load (std::memory_order_relaxed);
                const int rawR = (int) stepRoll[(size_t) bank][(size_t) stepInPattern][(size_t) p].load (std::memory_order_relaxed);
                const float vel  = rawV <= 0 ? 1.0f : juce::jlimit (0.02f, 1.0f, (float) rawV / 127.0f);
                const int   hits = rawR <= 0 ? 1    : juce::jlimit (1, 8, rawR);

                //  LAS NOTAS DE MAS DEL ACORDE, leidas una vez por paso y no
                //  por repeticion: un redoble de cuatro golpes sobre un acorde
                //  de cuatro notas son dieciseis disparos, y la cola tiene 96
                //  huecos para los dieciseis pads.
                const std::uint32_t acorde = stepChord[(size_t) bank][(size_t) stepInPattern][(size_t) p]
                                                 .load (std::memory_order_relaxed);

                //  EL EMPUJON DE ESTE PASO, encima del swing. El swing es una
                //  regla para las corcheas pares; esto es lo que separa a ESTE
                //  golpe de la rejilla, y va sumado. Nunca hacia atras del
                //  bloque: un desplazamiento negativo en el primer paso caeria
                //  antes de que empiece el compas, donde no hay donde ponerlo.
                const int empuje = (int) ((double) stepNudge[(size_t) bank][(size_t) stepInPattern][(size_t) p]
                                              .load (std::memory_order_relaxed)
                                          * samplesPerStep / 100.0);

                //  EL BLOQUEO DEL CORTE, aplicado AQUI y no al disparar: el
                //  filtro se calcula una vez por bloque sobre lo que el pad
                //  acaba de sonar, asi que ponerlo cuando el paso se anota es
                //  lo que hace que el bloque que trae el golpe ya lo lleve.
                //  Ponerlo al consumir la cola lo habria dejado un bloque
                //  tarde - cinco milisegundos y medio a 128 muestras, que en
                //  un barrido rapido se oye como un escalon.
                const int cierre = (int) stepLock[(size_t) bank][(size_t) stepInPattern][(size_t) p]
                                       .load (std::memory_order_relaxed);
                if (cierre > 0)
                {
                    pasoCutoff[(size_t) p].store (lockToHz (cierre - 1), std::memory_order_relaxed);
                    //  Y LA MASCARA CON EL. Un pad sin filtro no pasa por el
                    //  camino separado, asi que escribir el corte y no encender
                    //  la mascara habria guardado el numero y no filtrado nada.
                    //  Son dos operaciones atomicas: vale para el hilo de audio.
                    refreshFiltMask (p);
                }

                //  EL LARGO DE LA NOTA, en muestras. Ver setStepLen: se guarda
                //  en cuartos de paso, asi que una nota puede durar menos que
                //  la casilla sin tocar la rejilla del patron. Cero es suelta,
                //  que es como suena un pad de percusion y como sonaba todo
                //  hasta ahora. Y si el paso REPITE, el largo es el de cada
                //  golpe y no el del paso: cuatro repeticiones de una nota
                //  larga se pisarian unas a otras.
                const int cuartos = (int) stepLen[(size_t) bank][(size_t) stepInPattern][(size_t) p]
                                       .load (std::memory_order_relaxed);
                const int gate = cuartos > 0
                                   ? juce::jmax (32, (int) (samplesPerStep * (double) cuartos
                                                            / (4.0 * (double) juce::jmax (1, hits))))
                                   : kGateAuto;

                //  Y LOS OTROS CUATRO BLOQUEOS, que a diferencia del corte NO
                //  se escriben en el pad: viajan con el disparo hasta
                //  Voice::start. Ver setStepPLock.
                const std::uint32_t plock = stepPLock[(size_t) bank][(size_t) stepInPattern][(size_t) p]
                                                .load (std::memory_order_relaxed);

                for (int h = 0; h < hits; ++h)
                {
                    if (numPending >= (int) pending.size())
                        { droppedCommands.fetch_add (1, std::memory_order_relaxed); break; }
                    const int at = juce::jmax (0, lateBy + empuje
                                                 + (int) (samplesPerStep * (double) h / (double) hits));
                    pending[(size_t) numPending++] = { at, p, semis, vel, true, gate, plock };

                    //  El acorde suena en el MISMO instante que su raiz: si se
                    //  repartieran, seria un arpegio, y el arpegio se escribe
                    //  en la rejilla poniendo las notas en pasos distintos.
                    for (int e = 0; e < kExtraNotes; ++e)
                    {
                        if ((acorde & (1u << (24u + (unsigned) e))) == 0) continue;
                        if (numPending >= (int) pending.size())
                            { droppedCommands.fetch_add (1, std::memory_order_relaxed); break; }
                        const int extra = (int) (std::int8_t) ((acorde >> ((unsigned) e * 8u)) & 0xFFu);
                        //  Sin cortar: el autocorte del pad esta puesto por
                        //  defecto y con el las tres notas de mas mueren antes
                        //  de sonar. Medido: cuatro notas daban UNA voz viva.
                        pending[(size_t) numPending++] = { at, p, extra, vel, false, gate, plock };
                    }
                }
            }
        };

        auto fireStep = [this, chainLen, &patternIdx, &firePatternStep]() noexcept
        {
            //  EL CLIC SE REDISPARA AQUI, en el borde de paso y ANTES de la
            //  puerta de la cuenta atras: durante la cuenta el transporte no
            //  avanza y el metronomo tiene que sonar igual - es justo para lo
            //  que existe.
            //
            //  Y con DOS tonos: el primer tiempo del compas mas agudo que los
            //  otros tres. Un metronomo de un solo tono dice que hay pulso y no
            //  dice DONDE estas, que es la mitad para la que se enciende antes
            //  de grabar.
            if (clickOn.load (std::memory_order_relaxed))
            {
                if (clicPaso % 4 == 0)
                {
                    clickHz    = (clicPaso % kBarSteps == 0) ? 1600.0f : 1050.0f;
                    clickPhase = 0.0f;
                    clickEnv   = 0.42f;
                }
            }
            ++clicPaso;

            //  LA CUENTA ATRAS: mientras dura, el clic suena y la cancion NO
            //  avanza. Ni songStep, ni los pasos del patron, ni los clips.
            //
            //  Aqui y no en la cara: un temporizador del hilo de mensajes late
            //  cada 60 ms, asi que la cuenta acabaria hasta 60 ms antes o
            //  despues de donde el clic dijo - en la app cuyo argumento entero
            //  es la latencia, y justo en el instante que decide si la toma
            //  entra a tiempo.
            if (int q = cuentaPasos.load (std::memory_order_relaxed); q > 0)
            {
                cuentaPasos.store (q - 1, std::memory_order_relaxed);
                //  Y al gastar el ultimo, la cancion NO arranca aqui sino en el
                //  borde siguiente: este paso todavia es de la cuenta. Soltar
                //  la espera es lo que deja que el proximo borde caiga por el
                //  camino normal, o sea exactamente en la linea de compas.
                if (q == 1) arranqueEnBorde = false;
                return;
            }

            //  Y LA TOMA EMPIEZA AQUI: en el primer paso que ya no es de la
            //  cuenta, o sea exactamente en la linea de compas. Es un store y
            //  nada mas - `recordBuffer` se reserva en prepareToPlay y nunca
            //  aqui - asi que el hilo de audio sigue sin reservar.
            if (grabarTrasCuenta.exchange (false, std::memory_order_acquire))
            {
                //  El compas que ESTE paso va a estrenar. La rama de cancion lo
                //  calcula igual dos lineas mas abajo, y aqui hace falta antes
                //  porque quien pregunta -la cara, al parar- necesita saber
                //  donde empezo y no donde acabo.
                const int bars  = juce::jlimit (1, kSongBars, songBars.load (std::memory_order_relaxed));
                const int total = bars * kBarSteps;
                compasGrabado.store (((songStep + 1) % total) / kBarSteps, std::memory_order_relaxed);
                recordPos.store (0, std::memory_order_relaxed);
                recording.store (true, std::memory_order_release);
            }

            //  Song mode: the timeline drives everything. Several lanes run at
            //  once, so a pattern, a break and a one-shot can all land on the
            //  same bar — which a single queue of banks could never express.
            if (songMode.load (std::memory_order_relaxed))
            {
                const int bars = juce::jlimit (1, kSongBars, songBars.load (std::memory_order_relaxed));
                const int total = bars * kBarSteps;

                //  EL BUCLE DE UN TRAMO. Ver setSongLoop. Se aplica sobre el
                //  paso YA avanzado y no sobre el compas: saltar al principio
                //  del tramo en cuanto el compas se sale dejaria sonar el
                //  primer paso del compas siguiente antes de volver, que es un
                //  golpe de mas en cada vuelta - justo el que se oye.
                songStep = (songStep + 1) % total;

                const int la = songLoopA.load (std::memory_order_relaxed);
                const int lb = songLoopB.load (std::memory_order_relaxed);
                if (lb > la)
                {
                    const int desde = juce::jmin (la, bars - 1) * kBarSteps;
                    const int hasta = juce::jmin (lb, bars)     * kBarSteps;
                    if (hasta > desde && (songStep < desde || songStep >= hasta))
                    {
                        songStep = desde;
                        //  Y los carriles sueltan lo que arrastraban: un patron
                        //  de cuatro compases que empezo antes del tramo se
                        //  quedaria sonando desde su compas tres para siempre.
                        for (int ln = 0; ln < kSongLanes; ++ln) lanePattern[ln] = -1;
                    }
                }

                const int bar = songStep / kBarSteps;
                songBar.store (bar, std::memory_order_relaxed);

                //  LA AUTOMATIZACION, EN EL BORDE DE PASO y no por bloque: si
                //  se aplicara al consumir la cola llegaria un bloque tarde -
                //  5.5 ms a 128 muestras- que en un barrido rapido se oye como
                //  un escalon. Es la misma razon por la que el bloqueo de corte
                //  se aplica donde el paso se ANOTA.
                //
                //  Y el paso se publica aqui, que es el unico sitio que lo
                //  sabe: quien graba necesita en QUE paso poner el evento, y
                //  preguntarselo al temporizador de la cara -que late cada
                //  60 ms- lo dejaria hasta medio paso corrido.
                pasoAuto.store (songStep, std::memory_order_relaxed);
                aplicaAutomacion (songStep);

                // At the top of a bar, read what each lane starts here.
                if (songStep % kBarSteps == 0)
                {
                    for (int ln = 0; ln < kSongLanes; ++ln)
                    {
                        //  Un carril silenciado ni adopta patron ni dispara su
                        //  disparo suelto. Se mira aqui, al empezar el compas,
                        //  y no en el bucle de abajo, para que silenciar deje
                        //  tambien de CONTAR el patron: si no, al quitar el
                        //  silencio el carril seguiria a mitad de un patron que
                        //  nadie ha oido empezar.
                        if (songLaneMute[(size_t) ln].load (std::memory_order_relaxed))
                        {
                            lanePattern[ln] = -1;
                            continue;
                        }

                        const int cell = songCell[(size_t) ln][(size_t) bar].load (std::memory_order_relaxed);
                        if (cell == kContinued)
                            continue;                         // a pattern from an earlier bar still owns this lane

                        //  Y EL SILENCIO DE ESTE BLOQUE, mirado donde se mira
                        //  el del carril y por la misma razon: en la CABEZA del
                        //  bloque. Silenciar a mitad cortaria un patron por la
                        //  mitad, y ademas asi el bloque silenciado no adopta
                        //  el patron - o sea que al quitarle el silencio no
                        //  entra a mitad de algo que nadie oyo empezar. Una
                        //  carga atomica y un desplazamiento, una vez por
                        //  compas y por carril.
                        if (isSongCellMuted (ln, bar))
                        {
                            lanePattern[ln] = -1;
                            continue;
                        }
                        if (cell > 0 && cell <= kNumPatterns)
                        {
                            lanePattern[ln]   = cell - 1;
                            laneStartStep[ln] = songStep;
                            //  Cuantos compases ocupa el bloque: el suyo mas la
                            //  cola de continuaciones. Se cuenta AQUI, una vez
                            //  por bloque, y no en cada paso.
                            int n = 1;
                            for (int b2 = bar + 1; b2 < bars; ++b2)
                            {
                                if (songCell[(size_t) ln][(size_t) b2].load (std::memory_order_relaxed) != kContinued) break;
                                ++n;
                            }
                            laneBars[ln] = n;
                        }
                        else if (cell < 0)
                        {
                            lanePattern[ln] = -1;             // a one-shot owns no lane time
                            triggerPad (-cell - 1);
                        }
                        else
                        {
                            lanePattern[ln] = -1;             // empty: this lane rests
                        }
                    }
                }

                for (int ln = 0; ln < kSongLanes; ++ln)
                {
                    const int bank = lanePattern[ln];
                    if (bank < 0) continue;
                    const int len = juce::jlimit (kMinPatLen, kMaxPatLen, patternLength[(size_t) bank].load (std::memory_order_relaxed));
                    const int off = songStep - laneStartStep[ln];
                    if (off < 0) { lanePattern[ln] = -1; continue; }

                    //  EL BLOQUE MANDA SOBRE EL PATRON.
                    //
                    //  Antes un bloque duraba exactamente lo que su patron, y
                    //  acortarlo obligaba a acortar el patron entero - o sea a
                    //  cambiarlo en los otros sitios donde estuviera puesto.
                    //  Ahora lo que manda es cuantos compases ocupa en la
                    //  linea de tiempo: si ocupa menos, el patron se corta ahi;
                    //  si ocupa mas, da la vuelta dentro del bloque, que es lo
                    //  unico que puede significar un bloque de ocho compases
                    //  con un patron de cuatro.
                    const int suyos = juce::jmax (1, laneBars[ln]) * kBarSteps;
                    if (off >= suyos) { lanePattern[ln] = -1; continue; }
                    firePatternStep (bank, off % len);
                    if (ln == 0) playingPattern.store (bank, std::memory_order_relaxed);
                }

                currentStep = songStep % kBarSteps;
                playStep.store (currentStep, std::memory_order_relaxed);
                return;
            }

            const int prevStep = currentStep;
            const int len = juce::jlimit (kMinPatLen, kMaxPatLen, patternLength[(size_t) patternIdx].load (std::memory_order_relaxed));
            currentStep = (currentStep + 1) % len;

            // This bank's pattern (its own length, not always 16) just
            // completed a full loop — advance the chain.
            if (currentStep == 0 && prevStep >= 0 && chainLen > 0)
            {
                chainPos   = (chainPos + 1) % chainLen;
                patternIdx = chainSlots[(size_t) chainPos].load (std::memory_order_relaxed);
                playingPattern.store (patternIdx, std::memory_order_relaxed);
            }

            firePatternStep (patternIdx, currentStep);
            playStep.store (currentStep, std::memory_order_relaxed);
        };

        //  EL PRIMER PASO, EN EL ARRANQUE DEL TRANSPORTE - salvo si hay cuenta
        //  atras, que entonces la cancion empieza en la linea de compas.
        //
        //  Esta linea corre una vez por BLOQUE mientras no haya sonado el
        //  primer paso, no una vez por paso: sin la guarda, la cuenta atras se
        //  gastaba a razon de un paso por bloque -medido: los dieciseis en
        //  8192 muestras, 0.17 s- y el metronomo zumbaba en vez de marcar.
        if (currentStep < 0 && ! arranqueEnBorde)
            fireStep();   // first step exactly at transport start

        //  Anything already due speaks before a sample is rendered.
        //
        //  Y SALE EN EL ORDEN EN QUE ENTRO, que no es un detalle de estilo:
        //  ESTA COLA ESTA ORDENADA EN EL TIEMPO Y REORDENARLA ROMPE LOS
        //  ACORDES.
        //
        //  firePatternStep encola la raiz y detras sus tres notas de mas, y las
        //  extras van con `corta = false` a proposito - el autocorte del pad
        //  esta puesto por defecto y con el mueren antes de sonar. Eso solo se
        //  sostiene si la raiz sale PRIMERA.
        //
        //  Esto sacaba el hueco cambiandolo por el ULTIMO elemento
        //  -pending[i] = pending[--numPending]-, que es lo barato y reordena.
        //  Con un pad solo en el paso la raiz seguia saliendo primera y no se
        //  notaba, que es exactamente el caso que medía el banco. Con otro pad
        //  de indice menor en el mismo paso -un acorde encima de un bombo, o
        //  sea lo normal- la traza era:
        //
        //      cola  [bombo, raiz, e0, e1, e2]   (los cinco con countdown 0)
        //       i=0  sale bombo  -> pending[0] = e2   [e2, raiz, e0, e1]
        //       i=0  sale e2     -> suena una extra ANTES que la raiz
        //       i=0  sale e1, luego e0
        //       i=0  sale la RAIZ -> corta = true -> se lleva las tres
        //
        //  Las notas de mas arrancaban y morian 1.5 ms despues, que es el
        //  fundido de steal(): el acorde sonaba a UNA nota, y solo cuando tenia
        //  compania. Medido: pad 0 mas un acorde de cuatro en el pad 1 daban
        //  2 voces donde tienen que salir 5.
        //
        //  Se compacta en su sitio: el indice de escritura nunca adelanta al de
        //  lectura, asi que no hay copia ni reserva - noventa y seis huecos como
        //  mucho, y vale para el hilo de audio.
        auto fireDueHits = [this]() noexcept
        {
            int w = 0;
            for (int i = 0; i < numPending; ++i)
            {
                if (pending[(size_t) i].countdown <= 0)
                {
                    const auto h = pending[(size_t) i];
                    triggerPad (h.pad, h.semis, h.vel, -1.0f, h.corta, h.gate, h.plock);
                }
                else
                    pending[(size_t) w++] = pending[(size_t) i];
            }
            numPending = w;
        };

        auto nextHitIn = [this]() noexcept
        {
            int best = std::numeric_limits<int>::max();
            for (int i = 0; i < numPending; ++i)
                best = juce::jmin (best, pending[(size_t) i].countdown);
            return best;
        };

        fireDueHits();

        int offset    = startSample;
        int remaining = numSamples;
        while (remaining > 0)
        {
            //  Stop at whichever comes first: the next step boundary, or the
            //  next hit inside the step this one already queued.
            const double toBoundary = samplesPerStep - stepAccum;
            int seg = juce::jlimit (1, remaining, (int) std::ceil (toBoundary));
            seg = juce::jlimit (1, seg, nextHitIn());

            renderVoices (offset, seg);

            //  Y LOS CLIPS DE LA LINEA DE TIEMPO, en el MISMO segmento.
            //
            //  Aqui dentro y no una vez por bloque: el bucle ya parte en los
            //  bordes de paso y ahi es donde `songStep` salta -al dar la vuelta
            //  o al entrar en el tramo en bucle-, asi que una posicion por
            //  bloque se comeria el salto y el clip sonaria corrido.
            //
            //  `songStep` vale -1 hasta que suena el primer paso, que es el
            //  mismo instante en que empieza el compas 0: un clip puesto ahi
            //  arranca con el patron y no antes.
            if (songMode.load (std::memory_order_relaxed) && songStep >= 0)
                renderClips (out, offset, seg,
                             (double) songStep * samplesPerStep + stepAccum,
                             samplesPerStep * (double) kBarSteps);

            //  Y EL METRONOMO, en el mismo segmento y por la misma razon: se
            //  redispara en el borde de paso, asi que pintarlo una vez por
            //  bloque lo dejaria hasta un bloque tarde - 2.7 ms a 128 muestras,
            //  que es exactamente lo que un clic existe para no tener.
            renderClick (out, offset, seg);

            stepAccum += seg;
            offset    += seg;
            remaining -= seg;

            for (int i = 0; i < numPending; ++i)
                pending[(size_t) i].countdown -= seg;

            if (stepAccum >= samplesPerStep - 1.0e-9)
            {
                stepAccum -= samplesPerStep;
                fireStep();   // voices started here render from the next segment on
            }

            fireDueHits();
        }
        stepPhase.store ((float) (stepAccum / samplesPerStep), std::memory_order_relaxed);
    }

    // 5b. The six effect buses: FLT, HPF, DRV, DLY, BIT, REV - the order of
    //     the fxDefs table, which is the one the face shows.
    //     Each one runs on its own input, made upstream out of the pads that
    //     were sent to it, and returns into the master at full level. The
    //     wet/dry balance that used to live here now lives in the send, which
    //     is what lets a single pad be soaked in delay while the rest stay
    //     dry - impossible while the effects were inserts across everything.
    //
    //     A bus runs while it is being fed AND for as long as it keeps making
    //     sound after the feed stops. That is the whole point of a send: shut
    //     it and the delay repeats already inside the line still come out and
    //     die away on their own, instead of being cut off mid-tail. When a
    //     bus finally falls silent it is left alone entirely, so effects
    //     nobody is using cost nothing.
    {
        const int chans = busChans;

        // Block-rate smoothing coefficient for a ~20 ms time constant.
        const float kBlock = 1.0f - std::exp ((float) -numSamples / (0.020f * (float) systemSampleRate));
        const float nyq    = (float) (systemSampleRate * 0.45);

        auto live = [&] (int f) noexcept { return busFed[f] || busRinging[f]; };

        auto blockFor = [this, startSample, numSamples, chans] (int f) noexcept
        {
            return juce::dsp::AudioBlock<float> (fxBus[(size_t) f].getArrayOfWritePointers(),
                                                 (size_t) chans, (size_t) startSample, (size_t) numSamples);
        };

        //  Return the bus to the master and decide whether it is still alive.
        //  The threshold is far below anything audible; it exists so a reverb
        //  tail is not processed forever after it has decayed to nothing.
        auto returnBus = [this, &out, startSample, numSamples, chans] (int f) noexcept
        {
            auto& bus = fxBus[(size_t) f];

            //  LO QUE SALE DEL BUS MIRADO, y AQUI porque es el unico sitio por
            //  el que pasan los once. Escrito en cada etapa serian once copias
            //  de la misma regla, y la que se quedara vieja seria un visor que
            //  dibuja la señal de otro efecto.
            //
            //  Con el MISMO indice de escritura que la entrada: dos indices son
            //  dos relojes, y lo que se dibuja delante saldria corrido respecto
            //  a lo de detras justo donde se comparan.
            if (f == mirado.load (std::memory_order_relaxed))
            {
                const float* l = bus.getReadPointer (0, startSample);
                const float* r = chans > 1 ? bus.getReadPointer (1, startSample) : l;
                const int wi = mirWrite.load (std::memory_order_relaxed);
                float pico = 0.0f;
                for (int i = 0; i < numSamples; ++i)
                {
                    const float m = 0.5f * (l[i] + r[i]);
                    mirPost[(size_t) ((wi + i) & (kFxScope - 1))] = m;
                    pico = juce::jmax (pico, std::abs (m));
                }
                mirWrite.store ((wi + numSamples) & (kFxScope - 1), std::memory_order_release);

                //  Vivo mientras haya señal, y a la baja: un booleano se
                //  apagaria en el primer bloque de silencio entre dos golpes y
                //  el dibujo parpadearia. Cien bloques a 128 muestras son
                //  0.27 s, o sea lo que dura un hueco entre semicorcheas.
                const int h = mirHot.load (std::memory_order_relaxed);
                mirHot.store (pico > 1.0e-5f ? 100 : juce::jmax (0, h - 1),
                              std::memory_order_relaxed);
            }

            for (int ch = 0; ch < chans; ++ch)
                out.addFrom (ch, startSample, bus, ch, startSample, numSamples);
            busRinging[(size_t) f] = (bus.getMagnitude (startSample, numSamples) > 1.0e-5f);
        };

        //  LO QUE ENTRA AL BUS MIRADO, antes de que ninguna etapa lo toque.
        //
        //  Y aqui y no dentro de cada etapa por lo mismo que la salida: en
        //  este punto los once buses llevan exactamente lo que los pads les
        //  mandaron, asi que una sola linea vale para los once.
        //
        //  MONO -la media de los dos canales- porque nada de lo que se dibuja
        //  aqui habla de la imagen estereo, y dos anillos por lado serian el
        //  doble de memoria y el doble de analisis para pintar lo mismo.
        {
            const int fm = mirado.load (std::memory_order_relaxed);
            if (fm >= 0)
            {
                if (live (fm))
                {
                    const float* l = fxBus[(size_t) fm].getReadPointer (0, startSample);
                    const float* r = chans > 1 ? fxBus[(size_t) fm].getReadPointer (1, startSample) : l;
                    const int wi = mirWrite.load (std::memory_order_relaxed);
                    for (int i = 0; i < numSamples; ++i)
                        mirPre[(size_t) ((wi + i) & (kFxScope - 1))] = 0.5f * (l[i] + r[i]);
                }
                else
                {
                    //  Y CON EL BUS MUERTO LO QUE SALE DE EL ES SILENCIO, Y HAY
                    //  QUE ESCRIBIRLO.
                    //
                    //  `returnBus` es quien avanza el anillo, y no se llama
                    //  cuando el bus no esta vivo: los dos anillos se quedaban
                    //  congelados con lo ULTIMO que paso, asi que el visor
                    //  seguia dibujando esa cola para siempre. Con casi todos
                    //  no se notaba porque lo ultimo ya era casi silencio -el
                    //  envio se suaviza en 20 ms y la nota se apaga-, y FRZ lo
                    //  saco: se corta a nivel PLENO, asi que el dibujo se
                    //  quedaba en su ultimo trozo. El banco lo canto con su
                    //  nombre: `ruidosos [20]`, la columna 1 a 0.4095.
                    const int wi = mirWrite.load (std::memory_order_relaxed);
                    for (int i = 0; i < numSamples; ++i)
                    {
                        mirPre [(size_t) ((wi + i) & (kFxScope - 1))] = 0.0f;
                        mirPost[(size_t) ((wi + i) & (kFxScope - 1))] = 0.0f;
                    }
                    mirWrite.store ((wi + numSamples) & (kFxScope - 1), std::memory_order_release);

                    //  Y EL TESTIGO BAJA TAMBIEN CON EL BUS MUERTO, que es lo
                    //  que le faltaba a la version del EQ: la cuenta atras
                    //  vivia DENTRO de su rama, asi que en cuanto el bus se
                    //  declaraba muerto dejaba de bajar y el analizador se
                    //  quedaba diciendo «vivo» para siempre sobre un dibujo
                    //  congelado.
                    const int h = mirHot.load (std::memory_order_relaxed);
                    mirHot.store (juce::jmax (0, h - 1), std::memory_order_relaxed);
                }
            }
        }

        // --- 1. FLT: el barrido, en las dos direcciones. -------------------
        //
        //  Ver setFltSweep. -1 cierra por arriba, +1 abre por abajo, y el
        //  centro no procesa: en la zona muerta la etapa se salta entera, que
        //  es lo unico que hace que "neutro" sea de verdad neutro y no un paso
        //  bajo a 20 kHz con su fase y su resonancia puestas encima.
        {
            const float swT  = juce::jlimit (-1.0f, 1.0f, fltSweep.load (std::memory_order_relaxed));
            const float resT = juce::jlimit (0.1f, 4.0f, fxReso.load (std::memory_order_relaxed));
            smSweep += kBlock * (swT - smSweep);
            smReso  += kBlock * (resT - smReso);

            //  Exponencial, no lineal: el oido oye octavas. Repartido lineal,
            //  la mitad del recorrido se gasta entre 10 y 20 kHz, donde no pasa
            //  nada, y todo lo que importa cae en el ultimo centimetro.
            //  Ver AudioEngine::barridoDe: el reparto vive alli desde que la
            //  fila del rack lo dibuja, o serian dos reglas.
            const auto  barr  = barridoDe (smSweep);
            const bool  swept = barr.activo;
            const float freq  = barr.hz;
            const auto  type  = barr.alto ? juce::dsp::StateVariableTPTFilterType::highpass
                                          : juce::dsp::StateVariableTPTFilterType::lowpass;

            //  EL BUS SE DEVUELVE SIEMPRE QUE ESTE VIVO, SE FILTRE O NO.
            //
            //  FLT es de los que RESTAN SECO - fxSustituye[0] - porque un filtro
            //  es un inserto y no un envio: lo que un pad manda a este bus deja
            //  de ir por el camino seco. Saltarse returnBus en la zona muerta
            //  dejaba entonces al pad SIN camino: el seco quitado y el bus sin
            //  devolver. Silencio total con el efecto encendido y el barrido en
            //  el centro, que es donde queda la mitad de las veces.
            //
            //  Asi que "swept" decide si se PROCESA y "fed" decide si se
            //  DEVUELVE. De paso desaparece el salto de nivel al cruzar el
            //  centro: el bus no se va, solo deja de filtrarse - y en el borde
            //  de la zona muerta el filtro ya esta en su extremo, donde es casi
            //  transparente.
            const bool fed = live (0);
            if (fed)
            {
                if (swept)
                {
                    //  Reset al entrar Y al cambiar de lado. Un paso bajo
                    //  cargado con energia grave que de pronto se declara paso
                    //  alto suelta su estado de golpe: un golpe seco justo al
                    //  cruzar el centro, que es por donde pasa el dedo cada vez
                    //  que vuelve.
                    if (! filterWasActive || fltWasHigh != (smSweep > 0.0f))
                        masterFilter.reset();
                    masterFilter.setType (type);
                    masterFilter.setCutoffFrequency (juce::jlimit (20.0f, nyq, freq));
                    masterFilter.setResonance (smReso);
                    auto b = blockFor (0);
                    juce::dsp::ProcessContextReplacing<float> ctx (b);
                    masterFilter.process (ctx);
                    fltWasHigh = (smSweep > 0.0f);
                }
                returnBus (0);
            }
            filterWasActive = fed && swept;
        }

        // --- 2. HPF: its own filter, so ISO + HPF = band-pass. ------------
        {
            const float frqT = juce::jlimit (20.0f, nyq, hpFreq.load (std::memory_order_relaxed));
            const float resT = juce::jlimit (0.1f, 4.0f, hpReso.load (std::memory_order_relaxed));
            smHpFreq += kBlock * (frqT - smHpFreq);
            smHpReso += kBlock * (resT - smHpReso);

            const bool active = live (1);
            if (active)
            {
                if (! hpWasActive) hpFilter.reset();
                hpFilter.setCutoffFrequency (smHpFreq);
                hpFilter.setResonance (smHpReso);
                auto b = blockFor (1);
                juce::dsp::ProcessContextReplacing<float> ctx (b);
                hpFilter.process (ctx);
                returnBus (1);
            }
            hpWasActive = active;
        }

        // --- 3. DRIVE: tanh, then a tone control. -------------------------
        {
            const float drvT  = juce::jlimit (0.0f, 1.0f, fxDrive.load (std::memory_order_relaxed));
            const float toneT = juce::jlimit (200.0f, 20000.0f, drvTone.load (std::memory_order_relaxed));
            smDrive   += kBlock * (drvT  - smDrive);
            smDrvTone += kBlock * (toneT - smDrvTone);

            //  ESTADO QUE SOBREVIVE A UN BUS MUERTO ES UN GOLPE ESPERANDO.
            //
            //  `drvLp` es el estado del paso bajo de salida y no se reiniciaba
            //  nunca: ni en prepareToPlay - que solo limpiaba smSend - ni al
            //  volver a encenderse. Un bus se declara muerto (busRinging
            //  falso), la etapa deja de correr, pasan diez segundos, se vuelve
            //  a mandar un pad: la primera muestra sale del valor que se quedo
            //  guardado, que es DC. Un escalon de continua entrando al master,
            //  o sea un golpe seco cada vez que se reactiva el efecto. Y
            //  sobrevivia a un cambio de ruta de audio, que es cuando mas se
            //  nota porque coincide con enchufar los cascos.
            //
            //  Se limpia AL ENTRAR y no al preparar, porque cambiar un envio
            //  no pasa por prepareToPlay.
            const bool drvNow = live (2);
            if (drvNow && ! drvWasActive) { drvLp[0] = drvLp[1] = 0.0f; }
            drvWasActive = drvNow;

            if (drvNow)
            {
                //  Ver AudioEngine::driveDe: los dos numeros viven alli desde
                //  que el visor del plato dibuja esta misma curva. `mk`
                //  compensa por la ganancia que ENTRA y no por el techo del
                //  tanh, que para cualquier k util vale ~1.
                const auto  dr = driveDe (smDrive);
                const float a  = juce::jlimit (0.0f, 1.0f,
                                    1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi
                                                     * smDrvTone / (float) systemSampleRate));
                for (int ch = 0; ch < chans; ++ch)
                {
                    float* w = fxBus[2].getWritePointer (ch, startSample);
                    float lp = drvLp[ch];
                    for (int i = 0; i < numSamples; ++i)
                    {
                        lp += a * (saturaDe (w[i], dr) - lp);
                        w[i] = lp;
                    }
                    drvLp[ch] = lp;
                }
                returnBus (2);
            }
        }

        // --- 4. CRUSH: bit depth and sample-and-hold, the two halves of lo-fi.
        {
            //  Mismo agujero que DRV, y aqui peor: `crHold` es literalmente la
            //  muestra retenida, asi que al reactivarse el bus salia el ultimo
            //  valor cuantizado de hace diez segundos, mantenido hasta que la
            //  fase volviera a disparar - con crRate alto, cientos de muestras
            //  de continua seguidas.
            const bool crNow = live (4);
            if (crNow && ! crWasActive) { crHold[0] = crHold[1] = 0.0f; crPhase = 0.0f; }
            crWasActive = crNow;

            if (crNow)
            {
                //  Ver AudioEngine::nivelesDe y AudioEngine::crush: las dos
                //  mitades de un crusher -la amplitud y el TIEMPO- viven alli
                //  desde que el visor las dibuja.
                const float levels = nivelesDe (crBits.load (std::memory_order_relaxed));
                const float step   = juce::jmax (1.0f, crRate.load (std::memory_order_relaxed));

                //  El canal por FUERA y la muestra por dentro. Estaba al reves,
                //  con un `w = (ch == 0) ? w0 : w1` que es una rama por muestra
                //  y por canal dentro del bucle mas caliente de la etapa: nada
                //  de eso se puede vectorizar, y el compilador no puede saber
                //  que los dos punteros no se solapan.
                //
                //  La fase es del EFECTO y no del canal, asi que se avanza una
                //  sola vez: se guarda al terminar el primer canal y los demas
                //  la reproducen desde el mismo sitio, que es lo que hacia el
                //  bucle anterior y lo que mantiene los dos canales retenidos
                //  a la vez - que es de donde sale el sonido de un crusher y
                //  no de dos.
                const float phase0 = crPhase;
                for (int ch = 0; ch < chans; ++ch)
                {
                    float* w    = fxBus[4].getWritePointer (ch, startSample);
                    float phase = phase0;
                    float hold  = crHold[ch];

                    crush (w, numSamples, levels, step, phase, hold);

                    crHold[ch] = hold;
                    if (ch == 0) crPhase = phase;
                }
                returnBus (4);
            }
        }

        // --- 5. DELAY. Time is smoothed PER SAMPLE: a per-block jump through
        //        a linear-interp line is a hard discontinuity (crackle on
        //        every TIME move).
        {
            const float fbT  = juce::jlimit (0.0f, 0.95f, dlyFb.load (std::memory_order_relaxed));
            const float dsT  = juce::jlimit (1.0f, (float) (systemSampleRate - 1.0),
                                             dlyTime.load (std::memory_order_relaxed) * (float) systemSampleRate / 1000.0f);
            smDlyFb  += kBlock * (fbT  - smDlyFb);
            if (smDlySamp <= 0.0f) smDlySamp = dsT;            // first block: no sweep from 0
            const float kSamp = 1.0f - std::exp (-1.0f / (0.020f * (float) systemSampleRate));

            if (live (3))
            {
                float* w0 = fxBus[3].getWritePointer (0, startSample);
                float* w1 = (chans > 1) ? fxBus[3].getWritePointer (1, startSample) : w0;
                for (int i = 0; i < numSamples; ++i)
                {
                    smDlySamp += kSamp * (dsT - smDlySamp);
                    delayLine.setDelay (smDlySamp);
                    for (int ch = 0; ch < chans; ++ch)
                    {
                        float* w = (ch == 0) ? w0 : w1;
                        const float in = w[i];
                        const float d  = delayLine.popSample (ch);
                        //  LA CUARTA BARRERA, que faltaba: esta linea se
                        //  realimenta, asi que un NaN que entre una vez da
                        //  vueltas para siempre y el delay se queda mudo hasta
                        //  que alguien cambie de ruta -prepareToPlay es lo
                        //  unico que lo limpia-. Las otras tres protegen lo que
                        //  sale; un estado con memoria hay que protegerlo por
                        //  dentro. Se pregunta por lo finito porque comparar
                        //  con NaN siempre es falso.
                        const float realim = in + d * smDlyFb;
                        delayLine.pushSample (ch, std::isfinite (realim) ? realim : 0.0f);
                        w[i] = std::isfinite (d) ? d : 0.0f;
                    }
                }
                returnBus (3);
            }
        }

        // --- 6. REVERB. Wet only: the dry it would mix back already reached
        //        the master by the direct path, and adding it twice would
        //        only comb-filter the sound.
        {
            //  live(5) OR la energia interna de la FDN, y no dentro del if:
            //  poner al dia busRinging solo cuando ya se procesa es un candado
            //  - en cuanto el bus se declara muerto una vez, no vuelve a
            //  procesarse y no puede volver a declararse vivo. Con Freeverb no
            //  se notaba porque siempre sacaba algo en la primera muestra.
            if (live (5) || reverb.ringing())
            {
                reverb.setParameters (rvSize.load (std::memory_order_relaxed),
                                      rvDamp.load (std::memory_order_relaxed));
                reverb.process (fxBus[5], startSample, numSamples);
                returnBus (5);
                //  ...y la reverb manda sobre lo que returnBus acaba de
                //  deducir: la cola esta dentro de las lineas antes de estar en
                //  la salida. Ver Fdn::ringing.
                busRinging[5] = busRinging[5] || reverb.ringing();
            }
        }

        // --- 7. EQ: cinco biquads por canal sobre su propio bus. -----------
        //
        //  Es la etapa mas barata de las siete cuando la curva esta plana, y
        //  eso no es una casualidad: `Eq5::procesa` se salta la banda que vale
        //  0 dB, y a 0 dB el biquad del cookbook es paso directo EXACTO. Un EQ
        //  recien puesto no cuesta nada hasta que alguien mueve un nodo.
        //
        //  Y AQUI NO HAY `WasActive` que limpiar, a diferencia de DRV y de los
        //  dos filtros. `Eq5::reset` solo se llama en `prepare`, asi que el
        //  estado de los diez biquads sobrevive a que el bus se declare muerto.
        //  No es un descuido: lo que se queda ahi es la cola de un filtro, o
        //  sea muestras de audio de verdad -no continua, como el paso bajo de
        //  DRV, que era lo que hacia falta limpiar-. Con la curva plana el
        //  estado ni siquiera se toca.
        {
            if (live (6))
            {
                //  La captura de lo que entra y de lo que sale ya no vive aqui:
                //  el analizador del EQ era una respuesta a la misma pregunta
                //  que ahora se le hace a los once -«que esta pasando por este
                //  bus»- asi que la hace `returnBus` y el bloque de arriba.
                eqFx.procesa (fxBus[6].getArrayOfWritePointers(), chans, startSample, numSamples);
                returnBus (6);
            }
        }

        // --- 8. DINAMICA: CMP, GTE, DSS y LIM, cuatro buses y una pieza. ----
        //
        //  Un bucle y no cuatro bloques copiados: las cuatro son un detector y
        //  un calculador de ganancia, y lo unico que cambia es la curva. Ver
        //  Source/Dinamica.h.
        for (int d = 0; d < 4; ++d)
        {
            const int f = kFxCmp + d;
            if (! live (f)) continue;

            dyn[(size_t) d].procesa (fxBus[f].getArrayOfWritePointers(), chans,
                                     startSample, numSamples,
                                     (Dinamica::Modo) d,
                                     fxP[(size_t) f][0].load (std::memory_order_relaxed),
                                     fxP[(size_t) f][1].load (std::memory_order_relaxed));
            //  Y lo que baja, para la casilla de lectura. Un compresor que no
            //  dice cuanto comprime es un compresor invisible.
            dynRed[(size_t) d].store (dyn[(size_t) d].reduccionDb(),
                                      std::memory_order_relaxed);
            returnBus (f);
        }

        // --- 9. MODULACION: CHO, FLA, PHA y TRM. ---------------------------
        //
        //  Cuatro topologias distintas y por eso cuatro bloques y no un bucle
        //  -al reves que la dinamica, que si son la misma pieza-. Lo que SI
        //  comparten es el oscilador (`Source/Lfo.h`), y de ahi sale la unica
        //  regla que no puede estar escrita dos veces: el visor dibuja con
        //  `Lfo::valorEn`, o sea con esta misma funcion.
        //
        //  Y EL LFO SOLO AVANZA CON EL BUS VIVO, dentro del `if (live)`. Es lo
        //  que hace que la capa viva del visor se quede quieta sin señal, que
        //  es la mitad que `Tests/rack.py` exige -una capa que se mueve sin
        //  señal esta dibujando ruido-.
        {
            const float fsF = (float) systemSampleRate;

            //  Y AL CALLARSE, LA FASE VUELVE A CERO. El LFO solo avanza con el
            //  bus vivo -eso es lo que hace que el punto del visor se pare sin
            //  señal- y sin esto se queda parado DONDE LA MUSICA LO DEJO, que
            //  es un punto a media curva sin nada pasando. Lo canto el banco:
            //  `4 de 14 capas vivas se mueven sin señal`, comparando la lectura
            //  de antes de sonar con la de despues de callar. No dibujaban
            //  ruido — las dos estaban quietas y en sitios distintos.
            //
            //  Cada arranque empieza en cero de todas formas -lo hace el flanco
            //  de subida- asi que esto no cambia como suena: cambia lo que el
            //  visor dice mientras no pasa nada, que es «el LFO esta en su
            //  sitio» en vez de «esta a mitad de vuelta».
            for (int m = 0; m < 4; ++m)
                if (! live (modIdx (m)) && modWasActive[(size_t) m])
                    modFase[(size_t) m].store (0.0f, std::memory_order_relaxed);

            // --- CHO: retardo corto barrido, sin realimentacion. -----------
            {
                const int m = modDe (kFxCho);
                const float prof = juce::jlimit (0.0f, 1.0f,
                                     fxP[(size_t) kFxCho][1].load (std::memory_order_relaxed));
                smChoProf += kBlock * (prof - smChoProf);

                const bool ahora = live (kFxCho);
                //  El flanco limpia la linea: sin esto, volver a abrir el coro
                //  suena con la cola de la vez anterior. Es lo mismo que hacen
                //  DRV con su paso bajo y BIT con su retenedor.
                if (ahora && ! modWasActive[(size_t) m]) { choLine.reset(); mod[(size_t) m].reinicia(); }
                modWasActive[(size_t) m] = ahora;

                if (ahora)
                {
                    mod[(size_t) m].ponPaso (fxP[(size_t) kFxCho][0].load (std::memory_order_relaxed), systemSampleRate);
                    //  Centro y recorrido en MILISEGUNDOS y convertidos aqui:
                    //  doce milisegundos es un coro en cualquier aparato, y en
                    //  muestras seria un numero que cambia con la ruta.
                    const float centro = 0.012f * fsF;
                    const float amp    = 0.005f * fsF * smChoProf;
                    float* w0 = fxBus[kFxCho].getWritePointer (0, startSample);
                    float* w1 = (chans > 1) ? fxBus[kFxCho].getWritePointer (1, startSample) : w0;

                    for (int i = 0; i < numSamples; ++i)
                    {
                        const float v = mod[(size_t) m].avanza();
                        const float q = (chans > 1) ? mod[(size_t) m].enCuadratura() : v;
                        const float x0 = w0[i], x1 = w1[i];

                        choLine.pushSample (0, std::isfinite (x0) ? x0 : 0.0f);
                        choLine.setDelay (juce::jmax (1.0f, centro + amp * v));
                        const float d0 = choLine.popSample (0);

                        if (chans > 1)
                        {
                            choLine.pushSample (1, std::isfinite (x1) ? x1 : 0.0f);
                            choLine.setDelay (juce::jmax (1.0f, centro + amp * q));
                            const float d1 = choLine.popSample (1);
                            w1[i] = std::isfinite (d1) ? d1 : 0.0f;
                        }
                        w0[i] = std::isfinite (d0) ? d0 : 0.0f;
                    }
                    modFase[(size_t) m].store (mod[(size_t) m].fase, std::memory_order_relaxed);
                    returnBus (kFxCho);
                }
            }

            // --- FLA: retardo mas corto, con realimentacion con signo. -----
            {
                const int m = modDe (kFxFla);
                const float fb = juce::jlimit (-kFlaFbMax, kFlaFbMax,
                                   (fxP[(size_t) kFxFla][1].load (std::memory_order_relaxed) * 2.0f - 1.0f) * kFlaFbMax);
                smFlaFb += kBlock * (fb - smFlaFb);

                const bool ahora = live (kFxFla);
                if (ahora && ! modWasActive[(size_t) m])
                {
                    flaLine.reset(); flaFbZ[0] = flaFbZ[1] = 0.0f; mod[(size_t) m].reinicia();
                }
                modWasActive[(size_t) m] = ahora;

                if (ahora)
                {
                    mod[(size_t) m].ponPaso (fxP[(size_t) kFxFla][0].load (std::memory_order_relaxed), systemSampleRate);
                    //  De 0.5 a 6 ms: por debajo de un milisegundo la primera
                    //  muesca se va por encima de la banda y el peine deja de
                    //  oirse; por encima de seis ya es un coro.
                    const float centro = 0.00325f * fsF;
                    const float amp    = 0.00275f * fsF;
                    float* w0 = fxBus[kFxFla].getWritePointer (0, startSample);
                    float* w1 = (chans > 1) ? fxBus[kFxFla].getWritePointer (1, startSample) : w0;

                    for (int i = 0; i < numSamples; ++i)
                    {
                        const float v = mod[(size_t) m].avanza();
                        const float dl = juce::jmax (1.0f, centro + amp * v);

                        for (int ch = 0; ch < chans; ++ch)
                        {
                            float* w = (ch == 0) ? w0 : w1;
                            const float in = w[i] + smFlaFb * flaFbZ[ch];
                            flaLine.pushSample (ch, std::isfinite (in) ? in : 0.0f);
                            flaLine.setDelay (dl);
                            const float d = flaLine.popSample (ch);
                            flaFbZ[ch] = std::isfinite (d) ? d : 0.0f;
                            w[i] = flaFbZ[ch];
                        }
                    }
                    modFase[(size_t) m].store (mod[(size_t) m].fase, std::memory_order_relaxed);
                    returnBus (kFxFla);
                }
            }

            // --- PHA: cuatro allpass de primer orden con la esquina barrida.
            //
            //  De PRIMER orden y no el allpass de retardo de `Fdn.h`: aquel
            //  desplaza en el tiempo -es un difusor- y este gira la FASE con la
            //  frecuencia, que es lo unico que puede producir muescas al
            //  sumarse con el seco.
            {
                const int m = modDe (kFxPha);
                const float prof = juce::jlimit (0.0f, 1.0f,
                                     fxP[(size_t) kFxPha][1].load (std::memory_order_relaxed));
                smPhaProf += kBlock * (prof - smPhaProf);

                const bool ahora = live (kFxPha);
                if (ahora && ! modWasActive[(size_t) m])
                {
                    for (auto& fila : phaZ) for (auto& z : fila) z = 0.0f;
                    mod[(size_t) m].reinicia();
                }
                modWasActive[(size_t) m] = ahora;

                if (ahora)
                {
                    mod[(size_t) m].ponPaso (fxP[(size_t) kFxPha][0].load (std::memory_order_relaxed), systemSampleRate);
                    for (int i = 0; i < numSamples; ++i)
                    {
                        const float v = mod[(size_t) m].avanza();
                        //  La esquina se barre en OCTAVAS y no en Hz: 300 Hz
                        //  arriba de 300 son una octava y 300 arriba de 3000
                        //  no se oyen, que es la misma razon por la que
                        //  `barridoDe` reparte el filtro exponencialmente.
                        const float hz = 300.0f * std::pow (8.0f, 0.5f * smPhaProf * (v + 1.0f));
                        const float t  = std::tan (juce::MathConstants<float>::pi
                                                     * juce::jlimit (20.0f, fsF * 0.45f, hz) / fsF);
                        const float a  = (t - 1.0f) / (t + 1.0f);

                        for (int ch = 0; ch < chans; ++ch)
                        {
                            float* w = fxBus[kFxPha].getWritePointer (ch, startSample);
                            float x = w[i];
                            for (int e = 0; e < kPhaEtapas; ++e)
                            {
                                const float y = a * x + phaZ[ch][e];
                                phaZ[ch][e] = x - a * y;
                                x = y;
                            }
                            w[i] = std::isfinite (x) ? x : 0.0f;
                        }
                    }
                    modFase[(size_t) m].store (mod[(size_t) m].fase, std::memory_order_relaxed);
                    returnBus (kFxPha);
                }
            }

            // --- TRM: ganancia por muestra. --------------------------------
            {
                const int m = modDe (kFxTrm);
                const float prof = juce::jlimit (0.0f, 1.0f,
                                     fxP[(size_t) kFxTrm][1].load (std::memory_order_relaxed));
                smTrmProf += kBlock * (prof - smTrmProf);

                const bool ahora = live (kFxTrm);
                if (ahora && ! modWasActive[(size_t) m]) mod[(size_t) m].reinicia();
                modWasActive[(size_t) m] = ahora;

                if (ahora)
                {
                    mod[(size_t) m].ponPaso (fxP[(size_t) kFxTrm][0].load (std::memory_order_relaxed), systemSampleRate);
                    for (int i = 0; i < numSamples; ++i)
                    {
                        //  Solo hacia ABAJO: la ganancia va de 1 a 1-prof y
                        //  nunca por encima de uno. Subirla metería golpes por
                        //  encima de lo que la persona puso y el margen del
                        //  master no es nuestro para gastarlo -es la misma
                        //  regla que ya tiene HUMANIZAR con la fuerza-.
                        const float g = 1.0f - smTrmProf * 0.5f * (1.0f - mod[(size_t) m].avanza());
                        for (int ch = 0; ch < chans; ++ch)
                        {
                            float* w = fxBus[kFxTrm].getWritePointer (ch, startSample);
                            w[i] *= g;
                        }
                    }
                    modFase[(size_t) m].store (mod[(size_t) m].fase, std::memory_order_relaxed);
                    returnBus (kFxTrm);
                }
            }
        }

        // --- 10. CARACTER: RNG, PIT, WID, EXC, TRN y FRZ. ------------------
        //
        //  Seis topologias y por eso seis bloques, como la modulacion. Lo que
        //  las agrupa no es una pieza compartida sino lo contrario: TRES son
        //  extracciones de codigo que ya existia -`Estereo::ancho` salia de
        //  dos copias en linea de `Voice.h`, y `Dinamica::cruceEn` y
        //  `Dinamica::cruza` eran privadas- y tres se escriben de cero.
        //
        //  Las SEIS SUSTITUYEN, y es una decision y no una copia: un ancho, un
        //  excitador o un moldeador de transitorios que deja el original al
        //  lado no hace absolutamente nada -la suma devuelve lo que habia-, y
        //  un afinador o un congelador con el seco encima suenan a las dos
        //  cosas a la vez. Ver `fxSustituye`.
        {
            const float fsF = (float) systemSampleRate;
            const float kBlockC = kBlock;

            //  El flanco de los seis, igual que `modWasActive`: lo que se
            //  limpia va en cada bloque porque un efecto que se reabre con la
            //  cola de la vez anterior suena a otra cosa.
            //  Y FRZ PREGUNTA POR `busFed` Y NO POR `live`, que es lo unico
            //  de los seis que no se sostiene solo.
            //
            //  Un congelador que da vueltas escribe en su bus, asi que
            //  `busRinging` se queda puesto por su propia salida y `live`
            //  vuelve a ser cierto: el efecto no se apaga NUNCA — ni cerrando
            //  el envio, ni parando la maquina, ni quitando el pad— y el bus
            //  se queda vivo para siempre con su coste por bloque. Lo canto el
            //  banco: `1 de 20 capas vivas se mueven sin señal`, y no dibujaba
            //  ruido: seguia sonando. Se congela lo que ENTRA mientras entra
            //  algo, que ademas es lo que un pedal de freeze hace con el pie
            //  encima.
            const bool carVivo[6] = { live (kFxRng), live (kFxPit), live (kFxWid),
                                      live (kFxExc), live (kFxTrn), live (kFxFrz) };

            // --- RNG: modulador en anillo. ----------------------------------
            //
            //  Su oscilador NO es un `Lfo`: `ponPaso` acota a 40 Hz a proposito
            //  y aqui el recorrido llega a 4 kHz. Lo que se reutiliza es la
            //  FORMA -`Lfo::valorEn`, la misma que dibuja el visor-, que es
            //  para lo que nacio estatica.
            {
                const int c = carDe (kFxRng);
                const float an = juce::jlimit (0.0f, 1.0f,
                                   fxP[(size_t) kFxRng][1].load (std::memory_order_relaxed));
                smRngAnillo += kBlockC * (an - smRngAnillo);

                if (carVivo[c] && ! carWasActive[(size_t) c]) rngFase = 0.0f;
                carWasActive[(size_t) c] = carVivo[c];

                if (carVivo[c])
                {
                    const float hz   = juce::jlimit (20.0f, 4000.0f,
                                         fxP[(size_t) kFxRng][0].load (std::memory_order_relaxed));
                    const float paso = hz / fsF;

                    for (int i = 0; i < numSamples; ++i)
                    {
                        const float p = Lfo::valorEn (rngFase);
                        //  ANILLO es lo que separa un anillo de una amplitud
                        //  modulada, que es el mismo aparato con la portadora
                        //  desplazada: a 1 la portadora cruza el cero -el tono
                        //  original desaparece y quedan las dos bandas
                        //  laterales, que es la definicion- y a 0 no lo cruza
                        //  nunca, o sea que el original sigue ahi con un
                        //  temblor encima.
                        const float g = smRngAnillo * p
                                      + (1.0f - smRngAnillo) * (0.5f + 0.5f * p);
                        for (int ch = 0; ch < chans; ++ch)
                            fxBus[kFxRng].getWritePointer (ch, startSample)[i] *= g;

                        rngFase += paso;
                        if (rngFase >= 1.0f) rngFase -= 1.0f;
                    }
                    returnBus (kFxRng);
                }
            }

            // --- PIT: afinador por granos, con DOS cabezas. -----------------
            //
            //  Una sola cabeza da un salto audible cada vez que da la vuelta
            //  -eso es un glitch y no un afinador-. Las dos van desfasadas
            //  MEDIO grano y se cruzan con una ventana de Hann, que sumada a
            //  si misma medio periodo mas alla vale exactamente uno: la
            //  costura no cambia el nivel.
            {
                const int c = carDe (kFxPit);
                const float semis = juce::jlimit (-12.0f, 12.0f,
                                      fxP[(size_t) kFxPit][0].load (std::memory_order_relaxed));
                smPitSemis += kBlockC * (semis - smPitSemis);

                if (carVivo[c] && ! carWasActive[(size_t) c]) { pitLine.reset(); pitFase = 0.0f; }
                carWasActive[(size_t) c] = carVivo[c];

                if (carVivo[c])
                {
                    const float ratio = std::pow (2.0f, smPitSemis / 12.0f);
                    const float gran  = juce::jlimit (0.010f, (float) kPitGranoMax,
                                          fxP[(size_t) kFxPit][1].load (std::memory_order_relaxed) * 0.001f) * fsF;
                    //  El retardo avanza a `1 - ratio` por muestra: la fuente
                    //  se recorre a `ratio`, que es la definicion de afinar.
                    const float dp = (1.0f - ratio) / juce::jmax (1.0f, gran);

                    for (int i = 0; i < numSamples; ++i)
                    {
                        const float pA = pitFase;
                        const float pB = (pitFase >= 0.5f) ? pitFase - 0.5f : pitFase + 0.5f;
                        //  Hann por cabeza. La de atras entra mientras la de
                        //  delante sale, y las dos suman uno.
                        const float wA = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * pA);
                        const float wB = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * pB);

                        for (int ch = 0; ch < chans; ++ch)
                        {
                            float* w = fxBus[kFxPit].getWritePointer (ch, startSample);
                            const float x = w[i];
                            pitLine.pushSample (ch, std::isfinite (x) ? x : 0.0f);
                            pitLine.setDelay (juce::jmax (1.0f, 1.0f + pA * gran));
                            const float a = pitLine.popSample (ch, -1.0f, false);
                            pitLine.setDelay (juce::jmax (1.0f, 1.0f + pB * gran));
                            const float b = pitLine.popSample (ch, -1.0f, true);
                            const float y = wA * a + wB * b;
                            w[i] = std::isfinite (y) ? y : 0.0f;
                        }

                        pitFase += dp;
                        while (pitFase >= 1.0f) pitFase -= 1.0f;
                        while (pitFase <  0.0f) pitFase += 1.0f;
                    }
                    returnBus (kFxPit);
                }
            }

            // --- WID: ancho estereo con los graves en MONO. -----------------
            //
            //  `Estereo::ancho` es la misma funcion que usa un pad, que es por
            //  lo que salio de las dos copias en linea de `Voice.h`. Lo que
            //  esta etapa anade es el CRUCE: sin el, abrir el ancho de una
            //  mezcla con bajo desplaza el bajo, y el sub es lo unico que no
            //  puede moverse -en un sistema grande es mono por construccion-.
            {
                const int c = carDe (kFxWid);
                const float anc = juce::jlimit (0.0f, 2.0f,
                                    fxP[(size_t) kFxWid][0].load (std::memory_order_relaxed));
                smWidAncho += kBlockC * (anc - smWidAncho);

                if (carVivo[c] && ! carWasActive[(size_t) c])
                {
                    for (auto& fila : widAlta) for (auto& st : fila) st = {};
                    for (auto& fila : widBaja) for (auto& st : fila) st = {};
                }
                carWasActive[(size_t) c] = carVivo[c];

                if (carVivo[c] && chans > 1)
                {
                    const auto cr = Dinamica::cruceEn (
                        fxP[(size_t) kFxWid][1].load (std::memory_order_relaxed), systemSampleRate);
                    float* w0 = fxBus[kFxWid].getWritePointer (0, startSample);
                    float* w1 = fxBus[kFxWid].getWritePointer (1, startSample);

                    for (int i = 0; i < numSamples; ++i)
                    {
                        float bL = 0.0f, aL = 0.0f, bR = 0.0f, aR = 0.0f;
                        Dinamica::cruza (w0[i], widAlta[0], widBaja[0], cr.a1, cr.a2, cr.a3, cr.k, bL, aL);
                        Dinamica::cruza (w1[i], widAlta[1], widBaja[1], cr.a1, cr.a2, cr.a3, cr.k, bR, aR);
                        //  El grave se suma a mono ANTES de nada; el agudo es
                        //  el unico que se abre.
                        const float mono = 0.5f * (bL + bR);
                        Estereo::ancho (aL, aR, smWidAncho);
                        w0[i] = mono + aL;
                        w1[i] = mono + aR;
                    }
                    returnBus (kFxWid);
                }
                else if (carVivo[c])
                {
                    //  Una fuente MONO no tiene lado que abrir ni cerrar, que
                    //  es la misma regla que ya tiene el ancho de un pad.
                    returnBus (kFxWid);
                }
            }

            // --- EXC: excitador de agudos. ----------------------------------
            //
            //  El MISMO cruce, y el mismo argumento que el de-esser: se satura
            //  la banda alta SOLA y se vuelve a sumar con la baja intacta.
            //  Saturar la mezcla entera es DRV, que ya existe.
            {
                const int c = carDe (kFxExc);
                const float fue = juce::jlimit (0.0f, 1.0f,
                                    fxP[(size_t) kFxExc][1].load (std::memory_order_relaxed));
                smExcFuerza += kBlockC * (fue - smExcFuerza);

                if (carVivo[c] && ! carWasActive[(size_t) c])
                {
                    for (auto& fila : excAlta) for (auto& st : fila) st = {};
                    for (auto& fila : excBaja) for (auto& st : fila) st = {};
                }
                carWasActive[(size_t) c] = carVivo[c];

                if (carVivo[c])
                {
                    const auto cr = Dinamica::cruceEn (
                        fxP[(size_t) kFxExc][0].load (std::memory_order_relaxed), systemSampleRate);
                    //  Cuanto se empuja la banda alta contra el saturador. Los
                    //  armonicos que salen son lo que se anade; el nivel de la
                    //  banda BAJA no se toca, que es lo que separa un
                    //  excitador de una distorsion.
                    const float emp = 1.0f + 8.0f * smExcFuerza;

                    for (int ch = 0; ch < chans; ++ch)
                    {
                        float* w = fxBus[kFxExc].getWritePointer (ch, startSample);
                        for (int i = 0; i < numSamples; ++i)
                        {
                            float b = 0.0f, a = 0.0f;
                            Dinamica::cruza (w[i], excAlta[ch], excBaja[ch],
                                             cr.a1, cr.a2, cr.a3, cr.k, b, a);
                            const float sat = AudioEngine::fastTanh (a * emp) / emp;
                            const float y = b + a + smExcFuerza * (sat - a) * 4.0f;
                            w[i] = std::isfinite (y) ? y : 0.0f;
                        }
                    }
                    returnBus (kFxExc);
                }
            }

            // --- TRN: moldeador de transitorios. ----------------------------
            //
            //  DOS seguidores de envolvente y su DIFERENCIA. El rapido sube
            //  con el golpe y el lento no llega, asi que mientras dura el
            //  ataque el rapido va por encima; en la cola es al reves. Por eso
            //  un solo mando no bastaria: son dos tramos distintos del mismo
            //  sonido y cada uno tiene el suyo.
            //
            //  Y ENLAZADOS, o sea sobre el maximo de los dos canales: con un
            //  detector por canal el lado que pega sube y el otro se queda, y
            //  la imagen estereo se mueve con cada golpe. Es la misma leccion
            //  que la deteccion de `Dinamica`.
            {
                const int c = carDe (kFxTrn);
                const float at = juce::jlimit (-1.0f, 1.0f,
                                   fxP[(size_t) kFxTrn][0].load (std::memory_order_relaxed));
                const float ca = juce::jlimit (-1.0f, 1.0f,
                                   fxP[(size_t) kFxTrn][1].load (std::memory_order_relaxed));
                smTrnAtaque += kBlockC * (at - smTrnAtaque);
                smTrnCaida  += kBlockC * (ca - smTrnCaida);

                if (carVivo[c] && ! carWasActive[(size_t) c]) trnRapido = trnLento = 0.0f;
                carWasActive[(size_t) c] = carVivo[c];

                if (carVivo[c])
                {
                    //  Los cuatro coeficientes salen de `Dinamica::coefDe`, que
                    //  es la misma cuenta que usan los cuatro de dinamica: una
                    //  constante de tiempo escrita dos veces son dos reglas.
                    const float aRap = Dinamica::coefDe (1.0f,   systemSampleRate);
                    const float rRap = Dinamica::coefDe (25.0f,  systemSampleRate);
                    const float aLen = Dinamica::coefDe (35.0f,  systemSampleRate);
                    const float rLen = Dinamica::coefDe (300.0f, systemSampleRate);

                    float* w0 = fxBus[kFxTrn].getWritePointer (0, startSample);
                    float* w1 = (chans > 1) ? fxBus[kFxTrn].getWritePointer (1, startSample) : w0;

                    for (int i = 0; i < numSamples; ++i)
                    {
                        const float pico = juce::jmax (std::abs (w0[i]), std::abs (w1[i]));
                        trnRapido = (pico > trnRapido) ? aRap * trnRapido + (1.0f - aRap) * pico
                                                       : rRap * trnRapido + (1.0f - rRap) * pico;
                        trnLento  = (pico > trnLento)  ? aLen * trnLento  + (1.0f - aLen) * pico
                                                       : rLen * trnLento  + (1.0f - rLen) * pico;

                        const float dif = juce::Decibels::gainToDecibels (trnRapido, -100.0f)
                                        - juce::Decibels::gainToDecibels (trnLento,  -100.0f);
                        //  Positiva es ataque y negativa es cola, asi que cada
                        //  mando actua sobre SU tramo y en el otro vale cero.
                        const float dB = juce::jlimit (-18.0f, 18.0f,
                                            smTrnAtaque * juce::jmax (0.0f,  dif)
                                          + smTrnCaida  * juce::jmax (0.0f, -dif));
                        const float g = juce::Decibels::decibelsToGain (dB);
                        for (int ch = 0; ch < chans; ++ch)
                            fxBus[kFxTrn].getWritePointer (ch, startSample)[i] *= g;
                    }
                    returnBus (kFxTrn);
                }
            }

            // --- FRZ: congelador. -------------------------------------------
            //
            //  CAPTURA EN EL FLANCO y despues da vueltas. Es lo unico que
            //  «congelar» puede significar: capturar continuamente seria un
            //  retardo, y capturar al cerrar seria capturar lo que ya no esta.
            //  El flanco ya existe para limpiar estado y aqui ademas ARMA.
            //
            //  Y SUAVE es el cruce de la costura, que es la misma leccion que
            //  el bucle de un instrumento: un bucle sin fundido chasquea en
            //  cada vuelta, y aqui la vuelta llega ocho veces por segundo.
            {
                const int c = carDe (kFxFrz);
                const float suave = juce::jlimit (0.0f, 1.0f,
                                      fxP[(size_t) kFxFrz][1].load (std::memory_order_relaxed));
                smFrzSuave += kBlockC * (suave - smFrzSuave);

                if (carVivo[c] && ! carWasActive[(size_t) c])
                {
                    frzEscritas = 0;
                    frzLee      = 0.0f;
                    frzLlena    = false;
                    frzOyo      = false;
                    frzLargo    = juce::jlimit (1, (int) frzVent[0].size() - 1,
                                    (int) (fsF * juce::jlimit (0.020f, (float) kFrzVentanaMax,
                                             fxP[(size_t) kFxFrz][0].load (std::memory_order_relaxed) * 0.001f)));
                }
                carWasActive[(size_t) c] = carVivo[c];

                //  Y SIN NADA QUE ENTRE, EL BUS SE APAGA — que es lo unico de
                //  los seis que no se apagaria solo.
                //
                //  Un congelador que da vueltas escribe en su propio bus, asi
                //  que `returnBus` deja `busRinging` puesto por su propia
                //  salida y `live` vuelve a ser cierto: el efecto no se apaga
                //  NUNCA —ni cerrando el envio, ni parando la maquina— y el bus
                //  se queda vivo para siempre con su coste por bloque. Lo canto
                //  el banco, y no dibujaba ruido: seguia sonando. Se congela lo
                //  que ENTRA mientras entra algo, que ademas es lo que un pedal
                //  de freeze hace con el pie encima.
                //
                //  Y se DEVUELVE el bus vacio en vez de saltarse la etapa: sin
                //  eso `busRinging` se queda en el valor de la ultima vuelta y
                //  el bus no muere, que es el mismo fallo por la puerta de
                //  atras. Ademas es lo que escribe el silencio en el anillo del
                //  visor.
                if (carVivo[c] && ! busFed[kFxFrz])
                {
                    carWasActive[(size_t) c] = false;
                    returnBus (kFxFrz);
                }
                else if (carVivo[c])
                {
                    //  El cruce mide como mucho un cuarto de la ventana: mas
                    //  alla el trozo que se oye dos veces es mayor que el que
                    //  se oye una y deja de ser un bucle.
                    const int cruce = juce::jlimit (1, frzLargo / 4,
                                        (int) (smFrzSuave * (float) frzLargo * 0.25f) + 1);

                    for (int i = 0; i < numSamples; ++i)
                    {
                        for (int ch = 0; ch < chans; ++ch)
                        {
                            float* w = fxBus[kFxFrz].getWritePointer (ch, startSample);
                            const float x = std::isfinite (w[i]) ? w[i] : 0.0f;

                            if (! frzLlena)
                            {
                                frzVent[(size_t) ch][(size_t) frzEscritas] = x;
                                w[i] = x;
                                if (std::abs (x) > 1.0e-5f) frzOyo = true;
                            }
                            else
                            {
                                const int p = (int) frzLee;
                                float y = frzVent[(size_t) ch][(size_t) p];
                                //  El final se cruza con el principio: en la
                                //  costura las dos mitades son la MISMA
                                //  ventana, asi que no hay salto.
                                if (p >= frzLargo - cruce)
                                {
                                    const float t = (float) (p - (frzLargo - cruce)) / (float) cruce;
                                    y = (1.0f - t) * y + t * frzVent[(size_t) ch][(size_t) (p - (frzLargo - cruce))];
                                }
                                w[i] = y;
                            }
                        }

                        //  Y LA CAPTURA NO EMPIEZA HASTA QUE LLEGA SEÑAL.
                        //
                        //  El flanco ARMA y el primer sonido llena, que no es
                        //  lo mismo: un envio se abre antes de que suene nada
                        //  -en la app, tocando la ranura entre dos golpes; en
                        //  el banco, los treinta bloques de asentado- y sin
                        //  esta guarda el congelador captura SILENCIO y lo da
                        //  vueltas para siempre. Lo canto la primera corrida:
                        //  `al segundo 0.00000` con la etapa entera correcta.
                        if (! frzLlena && frzOyo)
                        {
                            if (++frzEscritas >= frzLargo) { frzLlena = true; frzEscritas = frzLargo; }
                        }
                        else
                        {
                            frzLee += 1.0f;
                            if (frzLee >= (float) frzLargo) frzLee -= (float) frzLargo;
                        }
                    }
                    returnBus (kFxFrz);
                }
            }
        }
    }

    //  5d-mon. EL MONITOR, justo encima de la barrera y del limitador.
    //
    //  Ver `setMonitor`: aqui es donde se decide que un NaN que entre por el
    //  aparato no salga por los altavoces, porque la etapa de abajo lo filtra.
    //  Un monitor colgado DESPUES del master -que es lo corto- se salta las
    //  dos cosas.
    //
    //  Con rampa por bloque y no con el valor crudo, que es la misma razon por
    //  la que la lleva cualquier otra ganancia que se suma aqui: un salto en
    //  una ganancia sumada es un click. La constante es la de los envios.
    {
        const float objetivo = monitorTarget.load (std::memory_order_relaxed);
        const float previo   = smMonitor;
        smMonitor += (objetivo - smMonitor) * kSend;
        if (smMonitor < 1.0e-6f && objetivo <= 0.0f) smMonitor = 0.0f;

        if ((previo > 0.0f || smMonitor > 0.0f) && monitorInChans > 0)
        {
            const int salidas = juce::jmin (2, out.getNumChannels());
            const int n       = juce::jmin (numSamples, monitorBuf.getNumSamples());
            for (int ch = 0; ch < salidas; ++ch)
            {
                //  Un microfono de telefono da UN canal, y ese uno va a los
                //  dos: repartirlo dejaria la voz pegada al oido izquierdo.
                const int src = juce::jmin (ch, monitorInChans - 1);
                out.addFromWithRamp (ch, startSample, monitorBuf.getReadPointer (src), n,
                                     previo, smMonitor);
            }
        }
    }

    // 5d. Master safety. Sixteen pads at full level plus a delay with
    //     feedback and a reverb tail will pass 0 dBFS, and what comes out of
    //     an integer DAC then is hard clipping: the ugliest sound a sampler
    //     can make, and one the user cannot see coming.
    //
    //     This is NOT a loudness stage. Below -0.5 dBFS it is mathematically
    //     transparent — the branch does nothing at all — and above it the
    //     signal is bent rather than cut, which is audible as saturation
    //     instead of as tearing. The export already measured and compensated;
    //     the thing you actually listen to had nothing.
    {
        //  LA BARRERA NO ES EL LIMITADOR, Y ATARLAS FUE UN AGUJERO.
        //
        //  Esto eran dos decisiones metidas en un solo `if`: el limitador es
        //  de SONIDO y es una preferencia; el filtro de no-finitos es de
        //  INTEGRIDAD y no lo es. Con las dos juntas, `outCh` valia cero
        //  cuando el limitador estaba apagado - y Exporter.h lo apaga a
        //  proposito en el motor del rebote, para poder medir el pico de
        //  verdad antes de compensarlo. O sea que el unico camino en el que
        //  un NaN se ESCRIBE A DISCO era exactamente el que se quedaba sin
        //  guardia.
        //
        //  Y no se notaba mirando: `bufferPeak` devuelve NaN, `(peak > 1.0f)`
        //  con NaN es falso, asi que el rebote decidia que no habia que bajar
        //  nada y escribia el fichero entero envenenado sin decir una palabra.
        //  El banco tampoco lo veia, porque solo ejercia el motor VIVO, que
        //  siempre lleva el limitador puesto.
        constexpr float thresh = 0.944f;      // -0.5 dBFS
        const int  guardCh = juce::jmin (2, out.getNumChannels());
        const bool limit   = safetyLimiter.load (std::memory_order_relaxed);
        for (int ch = 0; ch < guardCh; ++ch)
        {
            float* w = out.getWritePointer (ch, startSample);
            for (int i = 0; i < numSamples; ++i)
            {
                const float v = w[i];

                //  AQUI NO SALE UN NaN, VENGA DE DONDE VENGA.
                //
                //  Un NaN no se atenua ni se satura: se propaga. Entra por una
                //  muestra y sale por el bus, por el delay - que se realimenta
                //  y ya no vuelve nunca -, por la reverb y por el master, y la
                //  app se queda MUDA hasta que se reinicia. Y entra facil: un
                //  WAV de coma flotante corrupto trae NaN dentro, y un valor
                //  de 1e30 se convierte en infinito en cuanto se multiplica
                //  por algo, y el saturador de un infinito da NaN.
                //
                //  Medido en Tests/StressTest: de ocho muestras hostiles,
                //  CUATRO apagaban la maquina entera. Este if cuesta una
                //  comparacion por muestra - dos por bloque estereo de 512 - y
                //  convierte "la app se queda muda" en "ese pad no suena".
                if (! std::isfinite (v)) { w[i] = 0.0f; continue; }

                if (limit && (v > thresh || v < -thresh))
                {
                    const float sign = (v < 0.0f) ? -1.0f : 1.0f;
                    const float over = (v * sign - thresh) / (1.0f - thresh);
                    w[i] = sign * (thresh + (1.0f - thresh) * fastTanh (over));
                }
            }
        }
    }

    //  5e-vivo. EL REBOTE EN VIVO, en el MISMO sitio que el remuestreo y por
    //           la misma razon: despues de los efectos y del saturador, antes
    //           del fader del master y del ducking. Ver `vivoArma`.
    //
    //           El hilo de audio EMPUJA y se va. Si el anillo esta lleno -el
    //           hilo escritor no ha vuelto- se cuenta y no se bloquea: parar
    //           aqui a esperar un disco es exactamente lo que este hilo no
    //           puede hacer, y lo tirado se dice en vez de esconderse.
    //  Y LA CUENTA ATRAS NO ENTRA EN EL FICHERO, que es la mitad que se
    //  olvida: el clic es una referencia para tocar, no parte de la cancion.
    //  El anillo se arma antes de que el transporte ruede -si no, los primeros
    //  bloques se pierden- asi que quien tiene que callarse es la captura y no
    //  el armado. Es el mismo argumento por el que el remuestreo se escribe
    //  ANTES del fader del master: lo que se manda no lleva dentro lo que solo
    //  servia para tocarlo.
    if (vivoOn.load (std::memory_order_acquire) && ! enCuentaAtras()
        && out.getNumChannels() > 0)
    {
        int i1 = 0, n1 = 0, i2 = 0, n2 = 0;
        vivoFifo.prepareToWrite (numSamples, i1, n1, i2, n2);
        const int chans = juce::jmin (2, out.getNumChannels());
        for (int ch = 0; ch < 2; ++ch)
        {
            //  Un solo canal de salida se copia a los dos: el fichero es
            //  estereo pase lo que pase, como el del rebote offline.
            const int src = juce::jmin (ch, chans - 1);
            if (n1 > 0) vivoRing.copyFrom (ch, i1, out.getReadPointer (src, startSample), n1);
            if (n2 > 0) vivoRing.copyFrom (ch, i2, out.getReadPointer (src, startSample + n1), n2);
        }
        vivoFifo.finishedWrite (n1 + n2);
        if (n1 + n2 < numSamples)
            vivoPerdidas.fetch_add (numSamples - n1 - n2, std::memory_order_relaxed);
    }

    // 5e. RESAMPLE. The master, after everything, which is the whole point:
    //     what lands on the pad is what you just heard - the effects and the
    //     master saturation, printed. Written before the probe click so a
    //     latency measurement never ends up inside a take.
    //
    //     Y NO EL NIVEL DEL MASTER, que este comentario decia y hace tiempo
    //     que no es verdad: el fader se movio a 5c-duck, DESPUES de aqui, para
    //     que un aviso del sistema no imprimiera su bache dentro de la toma.
    //     Al moverlo se dejo esta linea diciendo "el nivel, todo impreso", y
    //     un comentario que va por detras del codigo es peor que no tenerlo -
    //     el codigo se lee una vez y el comentario se cree siempre. Que no se
    //     imprima es ademas lo correcto: bajar el master para no despertar a
    //     nadie es monitorizacion y dura un segundo; el remuestreo se queda.
    if (recording.load (std::memory_order_acquire)
        && recordFromMaster.load (std::memory_order_acquire)
        && out.getNumChannels() > 0)
    {
        const int cap = recordBuffer.getNumSamples();
        int rp = recordPos.load (std::memory_order_relaxed);
        const int n = juce::jmin (numSamples, cap - rp);
        if (n > 0)
        {
            //  A mono record buffer fed by a stereo master must print the
            //  AVERAGE of the two, not the left plus half the right: measured,
            //  that came back a ratio of 1.487 against what was heard, which
            //  is a resample that arrives louder than the thing it copied and
            //  clips a layer earlier every time round.
            if (recordBuffer.getNumChannels() == 1 && out.getNumChannels() > 1)
            {
                recordBuffer.copyFrom (0, rp, out.getReadPointer (0, startSample), n, 0.5f);
                recordBuffer.addFrom  (0, rp, out.getReadPointer (1, startSample), n, 0.5f);
            }
            else
            {
                const int chans = juce::jmin (recordBuffer.getNumChannels(), out.getNumChannels());
                for (int ch = 0; ch < chans; ++ch)
                    recordBuffer.copyFrom (ch, rp, out.getReadPointer (ch, startSample), n);
            }

            rp += n;
            recordPos.store (rp, std::memory_order_relaxed);
        }
        if (rp >= cap)
            recording.store (false, std::memory_order_release);
    }

    // 5c-bombeo. El sidechain del pad elegido, antes del ducking del sistema.
    //
    //  Va aqui y no en 5b porque tiene que agachar el MASTER entero, colas de
    //  delay y reverb incluidas - un bombeo que deja la reverb a tope no abre
    //  ningun hueco. Y va antes del saturador por la misma razon que el duck
    //  del sistema: bajar lo que ENTRA al limitador, no lo que sale.
    //  Y SOLO CON ENVOLVENTE. Estaba tambien con «o hay un pad armado», y con
    //  el pad armado y quieto la envolvente vale CERO: la ganancia sale 1.0 y
    //  se recorria la salida entera, los dos canales, multiplicando por uno.
    //  Quien arma el bombeo lo deja armado toda la sesion, asi que era en cada
    //  bloque y para siempre. Lo que el pad armado decide es si la envolvente
    //  se DISPARA, y eso pasa en triggerPad, no aqui.
    if (duckEnv > 0.0001f)
    {
        const float amt = juce::jlimit (0.0f, 1.0f, duckAmt.load (std::memory_order_relaxed));
        const float rel = juce::jmax (20.0f, duckRel.load (std::memory_order_relaxed));
        //  Recuperacion exponencial: es la forma de una envolvente de
        //  compresor y la que no deja escalon al volver.
        const float k = 1.0f - std::exp (-1000.0f / (rel * (float) juce::jmax (8000.0, systemSampleRate)));
        const int   outCh = juce::jmin (2, out.getNumChannels());

        float* w[2] = { nullptr, nullptr };
        for (int ch = 0; ch < outCh; ++ch) w[ch] = out.getWritePointer (ch, startSample);

        float env = duckEnv;
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = 1.0f - amt * env;
            for (int ch = 0; ch < outCh; ++ch) w[ch][i] *= g;
            env -= k * env;
        }
        duckEnv = (env < 1.0e-5f) ? 0.0f : env;
    }

    // 5c-duck. The master level, ramped.
    //
    //  DESPUES del saturador Y del remuestreo, no antes.
    //
    //  Estaba antes para que el limitador viera menos señal mientras se atenua.
    //  El precio era que el remuestreo, que captura el master en 5e, imprimia
    //  la atenuacion DENTRO de la toma: llega una notificacion mientras
    //  remuestreas y te llevas su bache de medio segundo grabado para siempre.
    //  Atenuar es monitorizacion y dura un segundo; la toma se queda.
    {
        const float target = masterTarget.load (std::memory_order_relaxed);

        //  Y LA MITAD DE ARRIBA DEL FADER NO SE APLICABA NUNCA.
        //
        //  Aqui ponia `target < 0.99999f || masterGain < 0.99999f`, y esa
        //  condicion era CORRECTA el dia que se escribio: `masterTarget` solo
        //  valia 0.28 -la atenuacion de un aviso del sistema- o 1.0, asi que
        //  «distinto de uno» y «menor que uno» eran lo mismo. Dejo de serlo el
        //  dia que el fader de la persona entro en el producto y nadie volvio
        //  a mirar el guardia: de 0 dB a +12 el fader se movia, la casilla
        //  decia «+12.0 dB» y **el audio salia intacto**. Es el mismo fallo de
        //  siempre -un control y su lectura contando cosas distintas- por el
        //  camino mas barato que tiene: una condicion que se quedo vieja.
        //
        //  Se pregunta por lo que se quiere decir -«no es la unidad»- y no por
        //  un lado solo. El coste de equivocarse hacia el otro lado es cero:
        //  con el master en su sitio el bloque se salta igual que antes.
        if (std::abs (target - 1.0f) > 1.0e-5f || std::abs (masterGain - 1.0f) > 1.0e-5f)
        {
            //  A 12 ms time constant: settled in about forty milliseconds,
            //  which is fast enough to be under the chime it is making room
            //  for and a hundred times too slow to click. Measured: 0.271 of
            //  level at the bottom, back to 1.000, over 13 blocks of 128.
            const float k = 1.0f - std::exp (-1.0f / (0.012f * (float) juce::jmax (8000.0, systemSampleRate)));
            const int outCh = juce::jmin (2, out.getNumChannels());

            float* w[2] = { nullptr, nullptr };
            for (int ch = 0; ch < outCh; ++ch) w[ch] = out.getWritePointer (ch, startSample);

            float gain = masterGain;
            for (int i = 0; i < numSamples; ++i)
            {
                gain += (target - gain) * k;
                for (int ch = 0; ch < outCh; ++ch) w[ch][i] *= gain;
            }
            masterGain = gain;
        }
    }

    // 5b-probe. The click, written AFTER the effects so nothing colours or
    //     delays it. A single sample would never leave a phone speaker, so it
    //     is a short decaying 3 kHz burst: a hard onset the microphone can
    //     find, and high enough to sit clear of room rumble.
    if (probing.load (std::memory_order_acquire))
    {
        const int outCh = juce::jmin (2, out.getNumChannels());
        constexpr int burst = 192;
        for (int i = 0; i < numSamples; ++i)
        {
            const int t = probeCounter + i - probeClickAt;
            if (t >= 0 && t < burst)
            {
                const float env = std::exp (-(float) t / 45.0f);
                const float v = 0.9f * env * std::sin (2.0f * juce::MathConstants<float>::pi
                                                       * 3000.0f * (float) t / (float) systemSampleRate);
                for (int ch = 0; ch < outCh; ++ch)
                    out.getWritePointer (ch, startSample)[i] = v;
            }
        }

        probeCounter += numSamples;
        if (probeCounter >= probeLength)
        {
            recording.store (false, std::memory_order_release);
            probing.store (false, std::memory_order_release);
        }
    }

    // 5c. Feed the scope ring (post-FX mono sum) for the LCD.
    //
    //     Two rings, because the screen wants two different things. The plain
    //     sample ring is 2048 frames - 43 ms, an instant - and that is all the
    //     old display ever needed.
    //
    //     The waveform silhouette wants nearly a second of signal on screen at
    //     once, which is 35000 frames. Copying THAT to the message thread every
    //     tick would be 140 KB a frame, and it is the largest recurring cost in
    //     the whole UI. So the audio thread decimates as it goes: a running
    //     min/max per column, one column emitted every scopeColLen frames. The
    //     UI copies 256 columns instead of 35000 samples, and re-bucketing
    //     columns into however many the screen is wide is exact - the min of
    //     mins is the min.
    {
        int wi = scopeWrite.load (std::memory_order_relaxed);
        const int outCh = out.getNumChannels();
        const float* l = out.getReadPointer (0, startSample);
        const float* r = (outCh > 1) ? out.getReadPointer (1, startSample) : l;

        int   ci = scopeColWrite.load (std::memory_order_relaxed);
        float mn = colMin, mx = colMax;
        int   ct = colCount;

        for (int i = 0; i < numSamples; ++i)
        {
            const float m = 0.5f * (l[i] + r[i]);
            scope[(size_t) wi] = m;
            wi = (wi + 1) & (kScopeSize - 1);

            mn = juce::jmin (mn, m);
            mx = juce::jmax (mx, m);

            if (++ct >= scopeColLen)
            {
                scopeColMin[(size_t) ci] = mn;
                scopeColMax[(size_t) ci] = mx;
                ci = (ci + 1) & (kScopeCols - 1);
                mn =  1.0e9f; mx = -1.0e9f; ct = 0;
            }
        }

        colMin = mn; colMax = mx; colCount = ct;
        scopeWrite.store (wi, std::memory_order_release);
        scopeColWrite.store (ci, std::memory_order_release);
    }

    // 6. Diagnostic test tone.
    int tt = testToneRemaining.load (std::memory_order_relaxed);
    if (tt > 0)
    {
        const int total = juce::jmax (1, (int) (0.4 * systemSampleRate));
        const int fade  = juce::jmax (1, (int) (0.005 * systemSampleRate));
        const double inc = 2.0 * juce::MathConstants<double>::pi * 440.0 / systemSampleRate;
        const int outCh = out.getNumChannels();
        float* dL = out.getWritePointer (0);
        float* dR = (outCh > 1) ? out.getWritePointer (1) : dL;
        const int nOut = juce::jmin (tt, numSamples);
        for (int i = 0; i < nOut; ++i)
        {
            const int done = total - tt;
            float amp = 0.2f;
            if (done < fade)    amp *= (float) done / (float) fade;
            else if (tt < fade) amp *= (float) tt   / (float) fade;
            const float s = amp * (float) std::sin (testPhase);
            dL[startSample + i] += s;
            if (outCh > 1) dR[startSample + i] += s;
            testPhase += inc;
            if (testPhase > 2.0 * juce::MathConstants<double>::pi) testPhase -= 2.0 * juce::MathConstants<double>::pi;
            --tt;
        }
        testToneRemaining.store (tt, std::memory_order_relaxed);
    }

    //  Where each pad's read head is, as a fraction of its whole source. The
    //  UI draws it over the waveform, so what you hear and what you see are
    //  the same thing moving. Sixteen relaxed stores a block; nothing reads
    //  back, so there is no ordering to get wrong.
    {
        float p[(size_t) kNumPads];
        for (auto& x : p) x = -1.0f;

        for (const auto& v : voices)
        {
            if (! v.active || v.slot < 0 || v.slot >= kNumPads) continue;

            if (auto* sb = padSample[(size_t) v.slot])
            {
                const int srcLen = sb->buffer.getNumSamples();
                if (srcLen > 1)
                    p[(size_t) v.slot] = juce::jmax (p[(size_t) v.slot],
                                                     (float) (v.pos / (double) srcLen));
            }
        }

        for (int i = 0; i < kNumPads; ++i)
            padPos[(size_t) i].store (p[(size_t) i], std::memory_order_relaxed);
    }

    // 7. Output peaks for the VU (max-hold until the UI reads) — last stage,
    //    after every contributor including the test tone.
    {
        const int outCh = out.getNumChannels();
        const float pl = out.getMagnitude (0, startSample, numSamples);
        const float pr = (outCh > 1) ? out.getMagnitude (1, startSample, numSamples) : pl;
        float prev = outPeakL.load (std::memory_order_relaxed);
        if (pl > prev) outPeakL.store (pl, std::memory_order_relaxed);
        prev = outPeakR.load (std::memory_order_relaxed);
        if (pr > prev) outPeakR.store (pr, std::memory_order_relaxed);

        //  Y el del canal mirado, con el mismo trato -max-hold hasta que la
        //  cara lo lea- pero medido MUCHO antes: alli el canal suena solo, y
        //  aqui ya se ha sumado con los otros quince y ha pasado el master.
        if (picoCan > 0.0f)
        {
            prev = canalPico.load (std::memory_order_relaxed);
            if (picoCan > prev) canalPico.store (picoCan, std::memory_order_relaxed);
        }
    }
}

void AudioEngine::handleCommand (const Command& c) noexcept
{
    switch (c.type)
    {
        //  El semitono del comando SE USA. Estaba clavado a cero, asi que el
        //  campo que la cola ya llevaba - y que la rama de cuantizar SI leia -
        //  se perdia en el camino corto: el piano roll no podia oir una nota
        //  sin desafinar el pad. Cero sigue siendo el valor por defecto, asi
        //  que un dedo en un pad suena exactamente igual que antes.
        case Command::Type::NoteOn:
        {
            //  EL LARGO VIAJA EN EL COMANDO. -1 la sostiene quien la disparo y
            //  mandara su NoteOff -un dedo en un pad, una tecla del teclado de
            //  la ficha-; -2 que la decida el motor. Sin esto, cualquier puerta
            //  de la interfaz que se olvide del NoteOff deja una nota de
            //  instrumento sonando para siempre, que es lo que le pasaba a la
            //  audicion de presets: se cambiaba de preset y se acumulaban.
            triggerPad (c.slot, (int) c.semitones, c.velocity, c.from01, true, c.gate);
            break;
        }
        case Command::Type::NoteOff:
            if (c.slot >= 0 && c.slot < kNumPads)
                for (auto& v : voices)
                    if (v.active && v.slot == c.slot)
                        v.release();
            break;
        case Command::Type::Panic:   for (auto& v : voices) v.kill(); break;
    }
}

// ---------------------------------------------------------------------------
//  Message thread
// ---------------------------------------------------------------------------

//  A refused trigger used to be a trigger that never happened: push() has
//  always returned false when the queue cannot take it, and nobody has ever
//  looked at the answer. So the one failure the transport can actually have
//  was also the one it reported to nobody - the pad simply made no sound, and
//  the app looked broken with nothing in it to see.
//
//  Now a refusal falls back to a single atomic word (see fallbackTriggers).
//  It loses the velocity and the audition point, which is the right thing to
//  lose: a pad that speaks at its own level beats a pad that does not speak.
void AudioEngine::postNoteOn (int slot, float vel) noexcept
{
    Command c; c.type = Command::Type::NoteOn; c.slot = slot; c.velocity = vel;
    //  Un dedo en un pad que NO esta en modo tecla no manda "suelta": es un
    //  golpe. Con una zona que da vueltas debajo, eso seria una nota eterna.
    c.gate = kGateAudicion;
    if (! commands.push (c))
        noteOnByLifeboat (slot);
}

void AudioEngine::postNoteOnAt (int slot, int semis, float vel, int gate) noexcept
{
    Command c; c.type = Command::Type::NoteOn; c.slot = slot; c.velocity = vel;
    c.semitones = (float) semis;
    c.gate      = gate;
    if (! commands.push (c))
        noteOnByLifeboat (slot);      // el bote solo lleva el pad; mejor la nota del pad que nada
}

void AudioEngine::postNoteOnFrom (int slot, float from01, float vel) noexcept
{
    Command c; c.type = Command::Type::NoteOn; c.slot = slot; c.velocity = vel;
    c.from01 = from01;
    c.gate   = kGateAudicion;
    if (! commands.push (c))
        noteOnByLifeboat (slot);
}

void AudioEngine::noteOnByLifeboat (int slot) noexcept
{
    if (slot < 0 || slot >= kNumPads)
        return;

    droppedCommands.fetch_add (1, std::memory_order_relaxed);
    fallbackTriggers.fetch_or ((std::uint64_t) 1u << slot, std::memory_order_release);
}

//  ENTRADA MIDI. Su propia cola, por el contrato de un solo productor - ver
//  el comentario de midiCommands. El bote salvavidas es el mismo: una nota que
//  no cabe suena igual, solo que al nivel del pad y sin dinamica, que es lo
//  correcto que perder.
void AudioEngine::postNoteOnFromMidi (int slot, float vel) noexcept
{
    Command c; c.type = Command::Type::NoteOn; c.slot = slot; c.velocity = vel;
    //  El MIDI manda su propio NoteOff, asi que esta la sostiene el teclado.
    c.gate = kGateSuelta;
    if (! midiCommands.push (c))
        noteOnByLifeboat (slot);
}

void AudioEngine::postNoteOffFromMidi (int slot) noexcept
{
    Command c; c.type = Command::Type::NoteOff; c.slot = slot;
    midiCommands.push (c);
}

void AudioEngine::postNoteOff (int slot) noexcept
{
    Command c; c.type = Command::Type::NoteOff; c.slot = slot;
    commands.push (c);
}

void AudioEngine::postPanic() noexcept
{
    Command c; c.type = Command::Type::Panic;
    if (! commands.push (c))
        droppedCommands.fetch_add (1, std::memory_order_relaxed);
}

void AudioEngine::postTestTone() noexcept
{
    testToneRemaining.store ((int) (0.4 * systemSampleRate), std::memory_order_relaxed);
}

int AudioEngine::getSampleLength (int slot) const noexcept
{
    if (slot < 0 || slot >= kNumPads) return 0;
    auto* sb = padSample[(size_t) slot];
    return sb != nullptr ? sb->buffer.getNumSamples() : 0;
}

void AudioEngine::publishSample (int slot, SampleBuffer::Ptr newBuffer) noexcept
{
    if (slot < 0 || slot >= kNumPads || newBuffer == nullptr)
        return;

    // Reset this pad's trim window to the full new sample.
    padStart[(size_t) slot].store (0, std::memory_order_relaxed);
    padEnd[(size_t) slot].store (newBuffer->buffer.getNumSamples(), std::memory_order_relaxed);

    newBuffer->incReferenceCount();
    if (auto* old = pendingPad[(size_t) slot].exchange (newBuffer.get(), std::memory_order_release))
        old->decReferenceCount();
}

//  LOS CLIPS QUE CAEN DENTRO DE ESTE SEGMENTO.
//
//  Un clip suena a velocidad 1.0 - no hay estirado ni afinacion - asi que esto
//  es una copia con ganancia y no una voz: ni acumulador de fase, ni Hermite,
//  ni interpolacion. Cuesta lo que cuesta leer memoria, que es la razon por la
//  que cuatro pistas no mueven la aguja del banco de CPU.
//  EL METRONOMO: un seno que decae, y nada mas.
//
//  Sin tabla y sin fichero, por lo mismo que los iconos y la fabrica: cero
//  bytes de instalacion y el tono se elige con un numero en vez de con un WAV
//  que alguien tendria que licenciar.
//
//  La caida es exponencial y corta -unos 35 ms- porque un clic largo se
//  solapa con el siguiente a tempos rapidos y deja de leerse como un pulso.
void AudioEngine::renderClick (juce::AudioBuffer<float>& out, int offset, int n) noexcept
{
    if (clickEnv <= 1.0e-4f) return;

    const float sr    = (float) juce::jmax (8000.0, systemSampleRate);
    const float paso  = juce::MathConstants<float>::twoPi * clickHz / sr;
    //  35 ms hasta caer a 1/e, contado en MUESTRAS y no en bloques: cuantos
    //  bloques sean lo decide el aparato, que es el fallo que este fichero ya
    //  tiene documentado tres veces con los ticks.
    const float caida = std::exp (-1.0f / (0.035f * sr));

    const int canales = out.getNumChannels();
    for (int i = 0; i < n; ++i)
    {
        const float v = std::sin (clickPhase) * clickEnv;
        clickPhase += paso;
        if (clickPhase > juce::MathConstants<float>::twoPi)
            clickPhase -= juce::MathConstants<float>::twoPi;
        clickEnv *= caida;

        for (int ch = 0; ch < canales; ++ch)
            out.addSample (ch, offset + i, v);

        if (clickEnv <= 1.0e-4f) { clickEnv = 0.0f; break; }
    }
}

void AudioEngine::renderClips (juce::AudioBuffer<float>& out, int offset, int n,
                               double pos, double porCompas) noexcept
{
    if (clips == nullptr || clips->n <= 0) return;

    const int canales = out.getNumChannels();
    const auto ini = (std::int64_t) pos;
    const auto fin = ini + n;

    for (int i = 0; i < clips->n; ++i)
    {
        const ClipAudio& c = clips->c[(size_t) i];
        if (c.fuente == nullptr || c.largo <= 0) continue;
        if (pistaMute[(size_t) juce::jlimit (0, kAudioTracks - 1, c.pista)]
                .load (std::memory_order_relaxed)) continue;

        const auto cIni = (std::int64_t) ((double) c.compas * porCompas);
        const auto cFin = cIni + c.largo;
        if (fin <= cIni || ini >= cFin) continue;          // no toca este segmento

        //  El trozo que se solapa, en coordenadas de la cancion y de la fuente.
        const auto desdeCancion = juce::jmax (ini, cIni);
        const auto hastaCancion = juce::jmin (fin, cFin);
        const int  cuantas      = (int) (hastaCancion - desdeCancion);
        if (cuantas <= 0) continue;

        const auto  enFuente = (std::int64_t) c.desde + (desdeCancion - cIni);
        const auto& src      = c.fuente->buffer;
        const int   srcLen   = src.getNumSamples();
        if (enFuente < 0 || enFuente >= srcLen) continue;

        const int  copiar  = juce::jmin (cuantas, (int) (srcLen - enFuente));
        const int  destino = offset + (int) (desdeCancion - ini);
        const int  srcCh   = juce::jmax (1, src.getNumChannels());

        for (int ch = 0; ch < canales; ++ch)
            out.addFrom (ch, destino, src, juce::jmin (ch, srcCh - 1),
                         (int) enFuente, copiar, c.gain);
    }
}

//  LOS PARAMETROS DE UN EFECTO, EN UN SOLO SITIO — y ahora de verdad.
//
//  Esta funcion era un `switch (fx * 3 + par)` con TREINTA Y TRES casos
//  escritos uno a uno, bajo una cabecera que decia «los veintiun parametros» y
//  ya mentia. Con veintiun tipos serian sesenta y tres, y lo que hace caro ese
//  numero no es escribirlo: es que un caso que falte cae en el `default` y
//  entonces el mando se mueve y no pasa nada, sin un aviso de nadie.
//
//  Con `fxP` es una escritura. Quedan DOS excepciones y las dos son reales:
//
//   · El EQ guarda su ANCHO y su SALIDA dentro de `Eq5` porque alli no son un
//     numero sino un estado — `ponAncho` marca los coeficientes por recalcular
//     y `ponSalida` a proposito no lo hace—. `fxP` sigue siendo el dueno del
//     valor (es lo que se guarda y lo que viaja al rebote); `eqFx` es donde se
//     APLICA. Lo dice ya el comentario de `Eq5::ponAncho`: «los dos viven en
//     fxParams como los de cualquier otro efecto; aqui solo se aplican».
//   · La mezcla se acota a 0..1 en la puerta, como hacia `setDynMix`.
void AudioEngine::setFxParam (int fx, int par, float v) noexcept
{
    if (! juce::isPositiveAndBelow (fx, kNumFx) || ! juce::isPositiveAndBelow (par, 3)) return;

    fxP[(size_t) fx][(size_t) par].store (par == 2 ? juce::jlimit (0.0f, 1.0f, v) : v,
                                          std::memory_order_relaxed);

    if (fx == kFxEq)
    {
        if (par == 0) eqFx.ponAncho  (v);
        else if (par == 1) eqFx.ponSalida (v);
    }
}

//  LOS EVENTOS DE ESTE PASO, del hilo de audio. Un barrido lineal de la tabla
//  entera y no un cursor: un cursor hay que re-buscarlo en cada salto -el
//  bucle de un tramo, volver al compas cero, el arranque- y un cursor mal
//  colocado deja la automatizacion muda sin que nada falle. Cuatro mil enteros
//  comparados ocho veces por segundo es lo que cuesta no tener ese fallo.
void AudioEngine::aplicaAutomacion (int paso) noexcept
{
    if (autom == nullptr || autom->n <= 0) return;
    if (autoEscribe.load (std::memory_order_relaxed)) return;   // escribir apaga leer

    for (int i = 0; i < autom->n; ++i)
    {
        const auto& ev = autom->e[(size_t) i];
        if (ev.paso == paso) setFxParam (ev.fx, ev.par, ev.valor);
    }
}

//  Se publica igual que la de clips y por lo mismo. Sin referencias que
//  contar: los eventos son POD.
void AudioEngine::publicaAutomacion (const EventoAuto* entrada, int cuantos) noexcept
{
    auto* t = new TablaAuto();
    t->n = juce::jlimit (0, kMaxAuto, cuantos);
    for (int i = 0; i < t->n; ++i) t->e[(size_t) i] = entrada[i];

    //  Y se recoge lo anterior ANTES de publicar, como con los clips.
    autoRetiradas.drain ([] (TablaAuto* v) { delete v; });
    if (auto* anterior = pendingAuto.exchange (t, std::memory_order_release)) delete anterior;
}

//  LA TABLA SE CONSTRUYE ENTERA Y SE PUBLICA DE UNA VEZ. Modificarla en su
//  sitio seria una lectura rota a medio bloque; un cerrojo esta prohibido.
void AudioEngine::publicaClips (const ClipAudio* entrada, int cuantos) noexcept
{
    auto* t = new TablaClips();
    t->n = juce::jlimit (0, kMaxClips, cuantos);
    for (int i = 0; i < t->n; ++i)
    {
        t->c[(size_t) i] = entrada[i];
        //  La tabla se queda una referencia de cada fuente MIENTRAS VIVA: sin
        //  esto, soltar el pad del que salio el clip dejaria al hilo de audio
        //  leyendo memoria liberada.
        if (t->c[(size_t) i].fuente != nullptr)
            t->c[(size_t) i].fuente->incReferenceCount();
    }

    //  Y SE RECOGE ANTES DE PUBLICAR, no solo en el temporizador: asi la cola
    //  de retiradas esta vacia casi siempre y la unica forma de llenarla es
    //  publicar dieciseis veces dentro de un mismo bloque de audio.
    clipsRetiradas.drain ([] (TablaClips* v) { sueltaTabla (v); });

    if (auto* anterior = pendingClips.exchange (t, std::memory_order_release))
        sueltaTabla (anterior);          // nadie llego a adoptarla: es nuestra
}

//  Suelta una tabla y las referencias que se quedo. SIEMPRE en el hilo de
//  mensajes: `decReferenceCount` puede acabar en un `delete`, que es lo que el
//  hilo de audio no puede hacer.
void AudioEngine::sueltaTabla (TablaClips* t) noexcept
{
    if (t == nullptr) return;
    for (int i = 0; i < t->n; ++i)
        if (t->c[(size_t) i].fuente != nullptr)
            t->c[(size_t) i].fuente->decReferenceCount();
    delete t;
}

void AudioEngine::collectRetiredSamples() noexcept
{
    retired.drain ([] (SampleBuffer* p) { if (p) p->decReferenceCount(); });
    clipsRetiradas.drain ([] (TablaClips* t) { sueltaTabla (t); });
}

//  The silhouette's columns, oldest first. Cosmetic like copyScope: a torn
//  column while the audio thread writes one is a pixel, not a fault.
int AudioEngine::copyScopeColumns (float* dstMin, float* dstMax, int n) noexcept
{
    n = juce::jlimit (0, kScopeCols, n);
    const int ci = scopeColWrite.load (std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
    {
        const int k = (ci - n + i) & (kScopeCols - 1);
        dstMin[i] = scopeColMin[(size_t) k];
        dstMax[i] = scopeColMax[(size_t) k];
    }
    return n;
}

void AudioEngine::copyScope (float* dst, int n) noexcept
{
    const int wi = scopeWrite.load (std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
        dst[i] = scope[(size_t) ((wi - n + i) & (kScopeSize - 1))];
}

//  Los dos del efecto MIRADO, del hilo de mensajes y por el mismo camino que
//  copyScope. Ver AudioEngine::miraFx.
void AudioEngine::copyFxScope (float* pre, float* post, int n) noexcept
{
    const int wi = mirWrite.load (std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
    {
        const size_t k = (size_t) ((wi - n + i) & (kFxScope - 1));
        pre[i]  = mirPre[k];
        post[i] = mirPost[k];
    }
}

void AudioEngine::setStep (int patternIdx, int step, int pad, bool on) noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps || pad < 0 || pad >= kNumPads) return;
    const std::uint64_t bit = (std::uint64_t) 1u << pad;
    std::uint64_t cur = patternBank[(size_t) patternIdx][(size_t) step].load (std::memory_order_relaxed);
    cur = on ? (cur | bit) : (cur & ~bit);
    patternBank[(size_t) patternIdx][(size_t) step].store (cur, std::memory_order_relaxed);
}

void AudioEngine::clearPattern (int patternIdx) noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns) return;
    for (auto& m : patternBank[(size_t) patternIdx]) m.store (0, std::memory_order_relaxed);
    //  Y las notas de mas del acorde: vaciar un patron y que siguiera sonando
    //  un acorde de tres notas encima de nada es lo que pasaba sin esto.
    for (auto& fila : stepChord[(size_t) patternIdx])
        for (auto& celda : fila) celda.store (0, std::memory_order_relaxed);
    for (auto& fila : stepNudge[(size_t) patternIdx])
        for (auto& celda : fila) celda.store (0, std::memory_order_relaxed);
    for (auto& fila : stepLock[(size_t) patternIdx])
        for (auto& celda : fila) celda.store (0, std::memory_order_relaxed);
    for (auto& fila : stepLen[(size_t) patternIdx])
        for (auto& celda : fila) celda.store (0, std::memory_order_relaxed);
    for (auto& fila : stepPLock[(size_t) patternIdx])
        for (auto& celda : fila) celda.store (0, std::memory_order_relaxed);
    //  Y LA NOTA, LA FUERZA Y LA REPETICION, que faltaban.
    //
    //  Un paso son NUEVE campos -la lista canonica esta en copiarFila- y esta
    //  funcion vaciaba seis: la nota, la fuerza y el redoble se quedaban
    //  puestos. No se oye mientras la casilla esta apagada, y por eso duro:
    //  el dia que vuelves a encender ese paso suena con la nota del patron que
    //  borraste. VACIAR tiene que dejar el patron como uno recien nacido.
    for (auto& fila : stepNote[(size_t) patternIdx])
        for (auto& celda : fila) celda.store (0, std::memory_order_relaxed);
    for (auto& fila : stepVel[(size_t) patternIdx])
        for (auto& celda : fila) celda.store (127, std::memory_order_relaxed);
    for (auto& fila : stepRoll[(size_t) patternIdx])
        for (auto& celda : fila) celda.store (1, std::memory_order_relaxed);
}

void AudioEngine::setPatternLength (int patternIdx, int len) noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns) return;
    patternLength[(size_t) patternIdx].store (juce::jlimit (kMinPatLen, kMaxPatLen, len), std::memory_order_relaxed);
}

int AudioEngine::getPatternLength (int patternIdx) const noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns) return kMinPatLen;
    return patternLength[(size_t) patternIdx].load (std::memory_order_relaxed);
}

void AudioEngine::setStepNote (int patternIdx, int step, int pad, int semis) noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps || pad < 0 || pad >= kNumPads) return;
    stepNote[(size_t) patternIdx][(size_t) step][(size_t) pad].store ((std::int8_t) juce::jlimit (-24, 24, semis), std::memory_order_relaxed);
}

//  EL EMPUJON DE CADA PASO. Ver setStepNudge.
void AudioEngine::setStepNudge (int patternIdx, int step, int pad, int centesimas) noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps
        || pad < 0 || pad >= kNumPads) return;
    //  Acotado a media casilla: mas que eso no es un empujon, es escribir el
    //  paso en otro sitio, y para eso esta la rejilla.
    stepNudge[(size_t) patternIdx][(size_t) step][(size_t) pad]
        .store ((std::int8_t) juce::jlimit (-50, 50, centesimas), std::memory_order_relaxed);
}

//  EL LARGO DE LA NOTA. Ver la cabecera: en CUARTOS de paso, y cero es
//  "suelta", que es lo que vale un patron escrito antes de que esto existiera.
void AudioEngine::setStepLen (int patternIdx, int step, int pad, int cuartos) noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps
        || pad < 0 || pad >= kNumPads) return;
    stepLen[(size_t) patternIdx][(size_t) step][(size_t) pad]
        .store ((std::uint8_t) juce::jlimit (0, kLenMax, cuartos), std::memory_order_relaxed);
}

int AudioEngine::getStepLen (int patternIdx, int step, int pad) const noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps
        || pad < 0 || pad >= kNumPads) return kLenSuelto;
    return (int) stepLen[(size_t) patternIdx][(size_t) step][(size_t) pad].load (std::memory_order_relaxed);
}

int AudioEngine::getStepNudge (int patternIdx, int step, int pad) const noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps
        || pad < 0 || pad >= kNumPads) return 0;
    return (int) stepNudge[(size_t) patternIdx][(size_t) step][(size_t) pad].load (std::memory_order_relaxed);
}

//  EL BLOQUEO DEL CORTE. Ver setStepLock. Guardado desplazado un uno, que el
//  cero es lo que vale un patron escrito antes de que esto existiera y tiene
//  que seguir queriendo decir "este paso no toca el filtro".
void AudioEngine::setStepLock (int patternIdx, int step, int pad, int porCiento) noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps
        || pad < 0 || pad >= kNumPads) return;
    const int v = porCiento < 0 ? 0 : juce::jlimit (0, 100, porCiento) + 1;
    stepLock[(size_t) patternIdx][(size_t) step][(size_t) pad]
        .store ((std::int8_t) v, std::memory_order_relaxed);
}

int AudioEngine::getStepLock (int patternIdx, int step, int pad) const noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps
        || pad < 0 || pad >= kNumPads) return kNoLock;
    const int v = (int) stepLock[(size_t) patternIdx][(size_t) step][(size_t) pad]
                      .load (std::memory_order_relaxed);
    return v <= 0 ? kNoLock : v - 1;
}

//  LOS OTROS CUATRO BLOQUEOS. Ver setStepPLock: un byte por bloqueo dentro de
//  un uint32, desplazados un uno por la misma razon que el del corte.
std::uint32_t AudioEngine::getStepPLockRaw (int patternIdx, int step, int pad) const noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps
        || pad < 0 || pad >= kNumPads) return 0;
    return stepPLock[(size_t) patternIdx][(size_t) step][(size_t) pad].load (std::memory_order_relaxed);
}

void AudioEngine::setStepPLockRaw (int patternIdx, int step, int pad, std::uint32_t v) noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps
        || pad < 0 || pad >= kNumPads) return;
    stepPLock[(size_t) patternIdx][(size_t) step][(size_t) pad].store (v, std::memory_order_relaxed);
}

void AudioEngine::setStepPLock (int patternIdx, int step, int pad, int cual, int porCiento) noexcept
{
    if (cual < 0 || cual >= kNumPLocks) return;
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps
        || pad < 0 || pad >= kNumPads) return;
    const std::uint32_t byte = (std::uint32_t) (porCiento < 0 ? 0 : juce::jlimit (0, 100, porCiento) + 1);
    const int desp = cual * 8;
    auto& celda = stepPLock[(size_t) patternIdx][(size_t) step][(size_t) pad];
    //  Lectura, modificacion y escritura de un atomico que solo escribe el
    //  hilo de mensajes: el de audio SOLO lee este paquete, asi que no hay
    //  carrera que resolver y no hace falta un CAS.
    const std::uint32_t v = celda.load (std::memory_order_relaxed);
    celda.store ((v & ~((std::uint32_t) 0xff << desp)) | (byte << desp), std::memory_order_relaxed);
}

int AudioEngine::getStepPLock (int patternIdx, int step, int pad, int cual) const noexcept
{
    if (cual < 0 || cual >= kNumPLocks) return kNoPLock;
    const std::uint32_t v = getStepPLockRaw (patternIdx, step, pad);
    const int byte = (int) ((v >> (cual * 8)) & 0xff);
    return byte <= 0 ? kNoPLock : byte - 1;
}

//  LAS TRES NOTAS DE MAS. Ver stepChord: un byte por nota en los bits bajos y
//  un bit de presencia por nota en los bits 24..26, porque el cero es un
//  semitono valido -la nota tal cual- y no puede significar "ninguna".
void AudioEngine::setStepExtra (int patternIdx, int step, int pad, int indice, int semis, bool puesta) noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps
        || pad < 0 || pad >= kNumPads || indice < 0 || indice >= kExtraNotes) return;

    auto& celda = stepChord[(size_t) patternIdx][(size_t) step][(size_t) pad];
    std::uint32_t v = celda.load (std::memory_order_relaxed);
    const unsigned sh = (unsigned) indice * 8u;
    v &= ~(0xFFu << sh);
    v |= ((std::uint32_t) (std::uint8_t) (std::int8_t) juce::jlimit (-24, 24, semis)) << sh;
    const std::uint32_t bit = 1u << (24u + (unsigned) indice);
    if (puesta) v |= bit; else v &= ~bit;
    celda.store (v, std::memory_order_relaxed);
}

int AudioEngine::getStepExtra (int patternIdx, int step, int pad, int indice) const noexcept
{
    if (indice < 0 || indice >= kExtraNotes) return -128;
    const auto v = getStepChordRaw (patternIdx, step, pad);
    if ((v & (1u << (24u + (unsigned) indice))) == 0) return -128;
    return (int) (std::int8_t) ((v >> ((unsigned) indice * 8u)) & 0xFFu);
}

void AudioEngine::clearStepExtras (int patternIdx, int step, int pad) noexcept
{
    setStepChordRaw (patternIdx, step, pad, 0);
}

//  UN PASO VACIO, EN UN SOLO SITIO.
//
//  Un paso son NUEVE campos y habia TRES sitios vaciando tres subconjuntos
//  distintos: clearPattern se dejaba nota, fuerza y redoble; el VACIAR del
//  piano se dejaba fuerza, redoble, largo, empujon, bloqueo y los cuatro
//  empaquetados; y EUCLIDES reescribia la fila sin tocar largo, bloqueo,
//  acorde ni empaquetados - bajo un comentario que dice que dejar pasos a
//  medias convierte "cinco golpes" en "cinco golpes y lo que hubiera".
//
//  La lista canonica ya existia en copiarFila, que es quien tiene que llevarse
//  el paso entero para que una copia siga siendo la misma figura. Aqui esta
//  escrita una vez y la usan los tres.
//
//  No apaga la casilla: quien llama decide si el paso suena. EUCLIDES enciende
//  justo despues y el VACIAR del piano apaga; mezclarlo aqui obligaria a los
//  dos a deshacer la mitad de lo que esto hace.
void AudioEngine::vaciaPaso (int patternIdx, int step, int pad) noexcept
{
    setStepNote    (patternIdx, step, pad, 0);
    setStepVel     (patternIdx, step, pad, 127);
    setStepRoll    (patternIdx, step, pad, 1);
    setStepLen     (patternIdx, step, pad, kLenSuelto);
    setStepNudge   (patternIdx, step, pad, 0);
    setStepLock    (patternIdx, step, pad, kNoLock);
    setStepChordRaw (patternIdx, step, pad, 0);
    setStepPLockRaw (patternIdx, step, pad, 0);
}

//  Velocity and roll, same shape as the note. Zero means "never set" in both,
//  which is what every pattern written before they existed says - and it has
//  to keep meaning full level and one hit, or old patterns would come back
//  silent or stuttering.
void AudioEngine::setStepVel (int patternIdx, int step, int pad, int vel) noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps || pad < 0 || pad >= kNumPads) return;
    stepVel[(size_t) patternIdx][(size_t) step][(size_t) pad].store ((std::uint8_t) juce::jlimit (1, 127, vel), std::memory_order_relaxed);
}

void AudioEngine::setStepRoll (int patternIdx, int step, int pad, int hits) noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps || pad < 0 || pad >= kNumPads) return;
    stepRoll[(size_t) patternIdx][(size_t) step][(size_t) pad].store ((std::uint8_t) juce::jlimit (1, 8, hits), std::memory_order_relaxed);
}

int AudioEngine::getStepVel (int patternIdx, int step, int pad) const noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps || pad < 0 || pad >= kNumPads) return 127;
    const int v = (int) stepVel[(size_t) patternIdx][(size_t) step][(size_t) pad].load (std::memory_order_relaxed);
    return v <= 0 ? 127 : v;
}

int AudioEngine::getStepRoll (int patternIdx, int step, int pad) const noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps || pad < 0 || pad >= kNumPads) return 1;
    const int r = (int) stepRoll[(size_t) patternIdx][(size_t) step][(size_t) pad].load (std::memory_order_relaxed);
    return r <= 0 ? 1 : r;
}

int AudioEngine::getStepNote (int patternIdx, int step, int pad) const noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps || pad < 0 || pad >= kNumPads) return 0;
    return stepNote[(size_t) patternIdx][(size_t) step][(size_t) pad].load (std::memory_order_relaxed);
}

bool AudioEngine::addToChain (int patternIdx) noexcept
{
    if (patternIdx < 0 || patternIdx >= kNumPatterns) return false;
    const int len = chainLength.load (std::memory_order_relaxed);
    if (len >= kMaxChain) return false;
    chainSlots[(size_t) len].store (patternIdx, std::memory_order_relaxed);
    chainLength.store (len + 1, std::memory_order_relaxed);
    return true;
}

// ---------------------------------------------------------------------------
//  Recording (message thread)
// ---------------------------------------------------------------------------

void AudioEngine::startRecording (int slot, bool fromMaster) noexcept
{
    if (slot < 0 || slot >= kNumPads) return;
    recordSlot = slot;
    recordPos.store (0, std::memory_order_relaxed);
    //  Set the SOURCE before arming, or a block that lands between the two
    //  records the wrong thing.
    recordFromMaster.store (fromMaster, std::memory_order_release);
    recording.store (true, std::memory_order_release);
}

SampleBuffer::Ptr AudioEngine::finishRecording() noexcept
{
    recording.store (false, std::memory_order_release);
    SampleBuffer::Ptr sb;
    const int len = recordPos.load (std::memory_order_acquire);
    if (len > 4)
    {
        //  Two channels that are bit-for-bit identical are one channel that
        //  the device duplicated, which is what a single microphone routed
        //  into a stereo stream looks like. Keeping both would double the
        //  file, the project and the memory to say the same thing twice.
        int chans = juce::jmax (1, recordBuffer.getNumChannels());

        if (chans == 2)
        {
            const float* l = recordBuffer.getReadPointer (0);
            const float* r = recordBuffer.getReadPointer (1);
            bool identical = true;

            for (int i = 0; i < len && identical; ++i)
                identical = (l[i] == r[i]);

            if (identical) chans = 1;
        }

        sb = new SampleBuffer();
        sb->buffer.setSize (chans, len);
        for (int ch = 0; ch < chans; ++ch)
            sb->buffer.copyFrom (ch, 0, recordBuffer, ch, 0, len);

        sb->sourceSampleRate = systemSampleRate;
        publishSample (recordSlot, sb);
    }
    return sb;
}

float AudioEngine::getRecordSeconds() const noexcept
{
    return (float) (recordPos.load (std::memory_order_relaxed) / systemSampleRate);
}

// ---------------------------------------------------------------------------
//  Offline bounce (message thread)
// ---------------------------------------------------------------------------

void AudioEngine::copyStateFrom (const AudioEngine& s) noexcept
{
    // Straight atomic-to-atomic copies. Both engines are touched from the
    // message thread here; the source may be sounding, which only means a
    // knob moved mid-copy could land on either side of the export. Nothing
    // tears — every field is an independent atomic.
    auto copyArr = [] (auto& dst, const auto& src)
    {
        for (size_t i = 0; i < dst.size(); ++i)
            dst[i].store (src[i].load (std::memory_order_relaxed), std::memory_order_relaxed);
    };

    copyArr (padPitch,   s.padPitch);
    copyArr (padGain,    s.padGain);
    copyArr (padStart,   s.padStart);
    copyArr (padEnd,     s.padEnd);
    copyArr (padLoop,    s.padLoop);
    copyArr (padReverse, s.padReverse);
    copyArr (padKeepLength, s.padKeepLength);
    copyArr (padChoke,   s.padChoke);
    copyArr (padPan,     s.padPan);
    copyArr (padAttack,  s.padAttack);
    copyArr (padRelease, s.padRelease);
    //  Y el filtro CON SU MASCARA, por lo mismo que los envios: el motor del
    //  rebote no pasa por setPadCutoff, se le copia el estado entero, y sin la
    //  mascara exportaria la cancion con los 64 pads sin filtrar.
    copyArr (padFadeIn,  s.padFadeIn);
    copyArr (padFadeOut, s.padFadeOut);
    copyArr (padCutoff,  s.padCutoff);
    copyArr (padReso,    s.padReso);
    padFiltMask.store (s.padFiltMask.load (std::memory_order_relaxed), std::memory_order_relaxed);
    copyArr (padMute,    s.padMute);
    copyArr (padSolo,    s.padSolo);
    for (size_t i = 0; i < padRecorte.size(); ++i) copyArr (padRecorte[i], s.padRecorte[i]);
    for (size_t i = 0; i < smSend.size();     ++i) smSend[i] = s.smSend[i];
    //  Y LA MESA ENTERA, que es lo que decide cuanto llega a cada bus desde que
    //  el envio es del CANAL: sin `padCanal` los 64 pads del rebote caerian en
    //  el canal 0 y la cancion saldria con los efectos de un canal que nadie
    //  uso, y sin `canalSend` saldria sin ninguno.
    copyArr (padCanal,  s.padCanal);
    for (size_t i = 0; i < canalSend.size(); ++i) copyArr (canalSend[i], s.canalSend[i]);
    copyArr (canalGain, s.canalGain);
    copyArr (canalMute, s.canalMute);
    smCanalDePad = s.smCanalDePad;
    //  Y LA MASCARA CON ELLOS. El motor del rebote no pasa nunca por
    //  setCanalSend - se le copia el estado entero de golpe - asi que sin esta
    //  linea arrancaba con la mascara a cero, se saltaba los 64 pads y
    //  exportaba la cancion sin un solo efecto. Todo dato que decida si algo
    //  se PROCESA tiene que viajar con el que dice cuanto.
    smSendHot = s.smSendHot;
    padSendMask.store (s.padSendMask.load (std::memory_order_relaxed), std::memory_order_relaxed);
    refreshSolo();

    bpm.store (s.bpm.load (std::memory_order_relaxed), std::memory_order_relaxed);
    stepBeats.store (s.stepBeats.load (std::memory_order_relaxed), std::memory_order_relaxed);
    editPattern.store (s.editPattern.load (std::memory_order_relaxed), std::memory_order_relaxed);
    copyArr (patternLength, s.patternLength);
    copyArr (chainSlots,    s.chainSlots);
    chainLength.store (s.chainLength.load (std::memory_order_relaxed), std::memory_order_relaxed);

    //  The three things a step SAYS, not just which steps exist.
    //
    //  Velocity, rolls and swing were never copied, so a bounce came out
    //  straight, at full level on every step and with every roll dropped -
    //  a different performance from the one you had been listening to. Same
    //  for AUTOCUT, which the clone's constructor defaults to true, so a pad
    //  deliberately left to stack was cut in the export and nowhere else.
    copyArr (padSelfCut, s.padSelfCut);
    swing.store (s.swing.load (std::memory_order_relaxed), std::memory_order_relaxed);

    for (size_t b = 0; b < patternBank.size(); ++b)
    {
        copyArr (patternBank[b], s.patternBank[b]);
        for (size_t st = 0; st < stepNote[b].size(); ++st)
        {
            copyArr (stepNote[b][st], s.stepNote[b][st]);
            copyArr (stepVel [b][st], s.stepVel [b][st]);
            copyArr (stepRoll[b][st], s.stepRoll[b][st]);
        }
    }

    songMode.store (s.songMode.load (std::memory_order_relaxed), std::memory_order_relaxed);
    songBars.store (s.songBars.load (std::memory_order_relaxed), std::memory_order_relaxed);
    for (size_t b2 = 0; b2 < stepChord.size(); ++b2)
        for (size_t s2 = 0; s2 < stepChord[b2].size(); ++s2)
        {
            copyArr (stepChord[b2][s2], s.stepChord[b2][s2]);
            copyArr (stepNudge[b2][s2], s.stepNudge[b2][s2]);
            copyArr (stepLen[b2][s2],   s.stepLen[b2][s2]);
            copyArr (stepLock[b2][s2],  s.stepLock[b2][s2]);
            copyArr (stepPLock[b2][s2], s.stepPLock[b2][s2]);
        }

    for (size_t ln = 0; ln < songCell.size(); ++ln)
        copyArr (songCell[ln], s.songCell[ln]);
    //  Y el silenciado de carriles y el tramo en bucle, que son estado de la
    //  cancion igual que las celdas: sin esto, exportar una mezcla que se
    //  monta con un motor aparte sonaria con los cuatro carriles y la cancion
    //  entera, que no es lo que la persona esta oyendo.
    copyArr (songLaneMute, s.songLaneMute);
    copyArr (songCellMute, s.songCellMute);
    songLoopA.store (s.songLoopA.load (std::memory_order_relaxed), std::memory_order_relaxed);
    songLoopB.store (s.songLoopB.load (std::memory_order_relaxed), std::memory_order_relaxed);

    auto copyOne = [] (auto& dst, const auto& src)
    {
        dst.store (src.load (std::memory_order_relaxed), std::memory_order_relaxed);
    };
    //  LOS PARAMETROS DE LOS EFECTOS, EN UN BUCLE.
    //
    //  Aqui habia VEINTIDOS pares escritos a mano, y el comentario que estaba
    //  en medio contaba que eso ya se habia olvidado tres veces -los recortes,
    //  el swing con la velocidad y los redobles, y `fltSweep`- y que las tres
    //  el rebote salio siendo una interpretacion distinta de la que se estaba
    //  escuchando. Con `fxP` no hay nada que olvidar: un tipo nuevo entra sin
    //  tocar esta funcion.
    for (int f = 0; f < kNumFx; ++f)
        for (int par = 0; par < 3; ++par)
            copyOne (fxP[(size_t) f][(size_t) par], s.fxP[(size_t) f][(size_t) par]);

    //  Y el ducking, que no es de ningun efecto.
    copyOne (duckAmt, s.duckAmt);
    copyOne (duckRel, s.duckRel);

    //  Y LA AUTOMATIZACION, que es la mitad de por que existe: el rebote tiene
    //  que sonar como lo tocaste, y sin esta linea sale con el numero que
    //  estuviera puesto al exportar - o sea justo lo que la automatizacion
    //  existe para arreglar. Se copia la tabla que el motor de escucha tiene
    //  ADOPTADA, no la que espera: la publicada puede no haberse adoptado
    //  todavia si nadie ha renderizado un bloque desde el ultimo cambio, asi
    //  que se miran las dos y manda la mas nueva.
    {
        const TablaAuto* fuente = s.pendingAuto.load (std::memory_order_acquire);
        if (fuente == nullptr) fuente = s.autom;
        if (fuente != nullptr && fuente->n > 0)
            publicaAutomacion (fuente->e.data(), fuente->n);
    }
    //  Y el modo de ESCRITURA no viaja: un rebote no graba automatizacion, la
    //  reproduce. Con el armado, `aplicaAutomacion` se rinde y el fichero
    //  saldria plano - que es el fallo mas caro posible aqui, porque solo se
    //  descubre escuchando lo exportado.
    setAutoEscribe (false);

    //  Y LAS CINCO BANDAS DEL EQ, que no son atomicos sueltos sino la tabla de
    //  `Eq5`: sin esto el rebote sale con la curva PLANA mientras la persona
    //  esta oyendo el EQ puesto, que es exactamente el fallo que ya se pago
    //  tres veces aqui -los recortes, el swing, el barrido del filtro-. Y con
    //  los dos mandos que la curva no dice, que sin ellos el ancho y la salida
    //  volverian a su valor de fabrica en el fichero que se manda.
    for (int b = 0; b < Eq5::kBands; ++b)
    {
        eqFx.ponBanda (b, s.eqFx.freqDe (b), s.eqFx.gainDe (b));
        //  Y el TIPO y la Q de cada banda: sin ellos el rebote sale con
        //  campanas donde la persona puso pasos, o sea con la mitad del
        //  ecualizador cambiada de sitio.
        eqFx.ponTipo (b, (int) s.eqFx.tipoDe (b));
        eqFx.ponQ    (b, s.eqFx.qDe (b));
    }
    eqFx.ponAncho  (s.eqFx.anchoDe());
    eqFx.ponSalida (s.eqFx.salidaDe());

    //  Los cuatro de DINAMICA ya han viajado en el bucle de `fxP` de arriba,
    //  que es la mitad de lo que esa tabla existe para arreglar. Lo que NO se
    //  copia sigue siendo el ESTADO del detector: el rebote empieza en
    //  silencio y una envolvente heredada le meteria una compresion de la nada
    //  en el primer bloque.

    // Start the FX smoothers already AT their targets. A live engine glides
    // over ~20 ms because a knob just moved; a bounce has no such history,
    // and gliding from the defaults would fade the filter in over the first
    // bar of every export.
    smSweep   = fltSweep.load (std::memory_order_relaxed);
    duckPad.store (s.duckPad.load (std::memory_order_relaxed), std::memory_order_relaxed);
    smReso    = fxReso.load   (std::memory_order_relaxed);
    smHpFreq  = hpFreq.load   (std::memory_order_relaxed);
    smHpReso  = hpReso.load   (std::memory_order_relaxed);
    smHpMix   = hpMix.load    (std::memory_order_relaxed);
    smDrive   = fxDrive.load  (std::memory_order_relaxed);
    smDrvTone = drvTone.load  (std::memory_order_relaxed);
    smDrvMix  = drvMix.load   (std::memory_order_relaxed);
    smDlyMix  = dlyMix.load   (std::memory_order_relaxed);
    smDlyFb   = dlyFb.load    (std::memory_order_relaxed);
    smDlySamp = (float) (dlyTime.load (std::memory_order_relaxed) * 0.001 * systemSampleRate);
    smChoProf = fxP[(size_t) kFxCho][1].load (std::memory_order_relaxed);
    smFlaFb   = (fxP[(size_t) kFxFla][1].load (std::memory_order_relaxed) * 2.0f - 1.0f) * kFlaFbMax;
    smPhaProf = fxP[(size_t) kFxPha][1].load (std::memory_order_relaxed);
    smTrmProf = fxP[(size_t) kFxTrm][1].load (std::memory_order_relaxed);
}

int AudioEngine::lengthInSteps() const noexcept
{
    if (songMode.load (std::memory_order_relaxed))
    {
        // The song is as long as its LAST occupied bar, not as long as the
        // slider says: exporting eight bars of silence after the track ends
        // is the kind of thing you only notice once the file is uploaded.
        const int bars = juce::jlimit (1, kSongBars, songBars.load (std::memory_order_relaxed));
        int last = -1;
        for (int ln = 0; ln < kSongLanes; ++ln)
            for (int b = 0; b < bars; ++b)
                if (songCell[(size_t) ln][(size_t) b].load (std::memory_order_relaxed) != 0)
                    last = juce::jmax (last, b);
        return (last < 0) ? 0 : (last + 1) * kBarSteps;
    }

    const int chainLen = chainLength.load (std::memory_order_relaxed);
    if (chainLen > 0)
    {
        int total = 0;
        for (int i = 0; i < chainLen; ++i)
        {
            const int bank = juce::jlimit (0, kNumPatterns - 1, chainSlots[(size_t) i].load (std::memory_order_relaxed));
            total += juce::jlimit (kMinPatLen, kMaxPatLen, patternLength[(size_t) bank].load (std::memory_order_relaxed));
        }
        return total;
    }

    const int bank = juce::jlimit (0, kNumPatterns - 1, editPattern.load (std::memory_order_relaxed));
    return juce::jlimit (kMinPatLen, kMaxPatLen, patternLength[(size_t) bank].load (std::memory_order_relaxed));
}

bool AudioEngine::hasContentToRender() const noexcept
{
    auto bankHasNotes = [this] (int bank) noexcept
    {
        const int len = juce::jlimit (kMinPatLen, kMaxPatLen, patternLength[(size_t) bank].load (std::memory_order_relaxed));
        for (int s = 0; s < len; ++s)
            if (patternBank[(size_t) bank][(size_t) s].load (std::memory_order_relaxed) != 0)
                return true;
        return false;
    };

    if (songMode.load (std::memory_order_relaxed))
    {
        const int bars = juce::jlimit (1, kSongBars, songBars.load (std::memory_order_relaxed));
        for (int ln = 0; ln < kSongLanes; ++ln)
            for (int b = 0; b < bars; ++b)
            {
                const int cell = songCell[(size_t) ln][(size_t) b].load (std::memory_order_relaxed);
                if (cell < 0) return true;                                   // a one-shot always sounds
                if (cell > 0 && cell <= kNumPatterns && bankHasNotes (cell - 1)) return true;
            }
        return false;
    }

    const int chainLen = chainLength.load (std::memory_order_relaxed);
    if (chainLen > 0)
    {
        for (int i = 0; i < chainLen; ++i)
            if (bankHasNotes (juce::jlimit (0, kNumPatterns - 1, chainSlots[(size_t) i].load (std::memory_order_relaxed))))
                return true;
        return false;
    }

    return bankHasNotes (juce::jlimit (0, kNumPatterns - 1, editPattern.load (std::memory_order_relaxed)));
}

// ---------------------------------------------------------------------------
//  Latency probe (message thread)
// ---------------------------------------------------------------------------

void AudioEngine::startLatencyProbe() noexcept
{
    probeArm.store (true, std::memory_order_release);
}

//  Find the click in what the microphone heard. The threshold is derived from
//  the room's own noise in the 300 ms BEFORE the click rather than being a
//  constant: a quiet room and a busy street need different bars, and a fixed
//  one would either miss the click or trigger on a passing car.
float AudioEngine::finishLatencyProbe() const noexcept
{
    if (probing.load (std::memory_order_acquire)) return -1.0f;

    const int captured = recordPos.load (std::memory_order_acquire);
    const int emitted  = probeClickAt;
    if (captured <= emitted + 64 || recordBuffer.getNumSamples() <= emitted) return -1.0f;

    const float* r = recordBuffer.getReadPointer (0);

    double noise = 0.0;
    const int noiseTo = juce::jmax (1, emitted - 1024);
    for (int i = 0; i < noiseTo; ++i) noise += (double) r[i] * r[i];
    const float floorRms = (float) std::sqrt (noise / (double) noiseTo);
    const float thresh   = juce::jmax (8.0f * floorRms, 0.02f);

    // A round trip past half a second is not a measurement, it is a car door.
    const int limit = juce::jmin (captured, emitted + (int) (0.5 * systemSampleRate));
    for (int i = emitted; i < limit; ++i)
        if (std::abs (r[i]) > thresh)
            return (float) ((double) (i - emitted) * 1000.0 / systemSampleRate);

    return -1.0f;
}
