#include "MainComponentInterno.h"

// ==========================================================================
//  LO QUE EL BANCO ABRE, APRIETA Y MIDE.
//
//  Catorce funciones que solo corren con `ZATI_AUDIT`, `ZATI_ARR`, `ZATI_DLC`,
//  `ZATI_PIANO` y las demas entradas del banco: montan un estado conocido,
//  pulsan el control DE VERDAD -y no llaman al callback por dentro, que es
//  justo donde los fallos no existen- e imprimen la linea JSON que juzga el
//  script. Nada del camino normal de la app las llama.
//
//  Estaban en medio del fichero, entre el maquetado y los pintores, que es la
//  peor vecindad posible: el codigo que MIDE mezclado con el que se mide.
// ==========================================================================

void MainComponent::auditArrange()
{
    auto fila = [this] (const char* que)
    {
        std::cout << "{\"arr\":\"" << que << "\",\"largo\":" << engine.getSongLength()
                  << ",\"carriles\":[";
        for (int ln = 0; ln < AudioEngine::kSongLanes; ++ln)
        {
            std::cout << (ln ? "," : "") << "[";
            for (int b = 0; b < engine.getSongLength(); ++b)
                std::cout << (b ? "," : "") << engine.getSongCell (ln, b);
            std::cout << "]";
        }
        std::cout << "]}" << std::endl;
    };

    //  UNA CANCION QUE SE LEE DE UN VISTAZO: el carril n lleva el valor n+1 en
    //  el compas n, y nada mas. Asi cualquier desplazamiento se ve en la
    //  posicion Y en el valor, y una operacion que mueve la fila equivocada no
    //  se puede confundir con una que mueve la columna equivocada.
    engine.clearSong();
    engine.setSongLength (8);
    for (int ln = 0; ln < AudioEngine::kSongLanes; ++ln)
        engine.setSongCell (ln, ln * 2, ln + 1);
    songCursor = 2;
    fila ("inicial");

    insertSongBar();   fila ("insertar en 2");
    removeSongBar();   fila ("quitar el 2");
    copySongBar();
    songCursor = 5;
    pasteSongBar();    fila ("pegar el 2 en el 5");

    //  EL PATRON. Un golpe en el paso 0 del pad 0 y otro en el 3 del pad 1,
    //  con nota y fuerza distintas de las de por defecto, para que se vea si
    //  el desplazamiento se lleva TODO lo que un paso lleva o solo el "suena".
    selectedPattern = 0;
    engine.clearPattern (0);
    for (int st = 0; st < AudioEngine::kNumSteps; ++st)
        for (int p = 0; p < kNumPads; ++p)
            pattern[0][(size_t) st][(size_t) p] = false;

    engine.setPatternLength (0, 16);
    pattern[0][0][0] = true;  engine.setStep (0, 0, 0, true);
    engine.setStepNote (0, 0, 0, 5);  engine.setStepVel (0, 0, 0, 90);
    pattern[0][3][1] = true;  engine.setStep (0, 3, 1, true);
    engine.setStepRoll (0, 3, 1, 4);

    auto patron = [this] (const char* que)
    {
        const int len = engine.getPatternLength (0);
        std::cout << "{\"pat\":\"" << que << "\",\"largo\":" << len << ",\"pasos\":[";
        bool first = true;
        for (int st = 0; st < len; ++st)
            for (int p = 0; p < kNumPads; ++p)
                if (pattern[0][(size_t) st][(size_t) p])
                {
                    std::cout << (first ? "" : ",") << "[" << st << "," << p << ","
                              << engine.getStepNote (0, st, p) << ","
                              << engine.getStepVel  (0, st, p) << ","
                              << engine.getStepRoll (0, st, p) << "]";
                    first = false;
                }
        std::cout << "]}" << std::endl;
    };

    patron ("inicial");
    rotatePattern (+1);   patron ("adelante");
    rotatePattern (-1);   patron ("atras");
    doublePattern();      patron ("doblado");

    //  RECORTAR Y ALARGAR UN BLOQUE. Un patron de dos compases puesto en el
    //  compas 1: acortarlo a uno tiene que tirar la cola, alargarlo a tres
    //  tiene que ponerla, y alargarlo contra el bloque de al lado no tiene que
    //  hacer nada - comerse lo del vecino seria borrar algo que nadie ha
    //  pedido borrar.
    engine.clearSong();
    engine.setSongLength (8);
    engine.setSongCell (0, 1, 1);
    engine.setSongCell (0, 2, AudioEngine::kContinued);
    engine.setSongCell (0, 4, 2);          // el vecino, para que ALARGAR choque
    songCursor = 1;
    fila ("bloque inicial");

    resizeSongBlock (-1);  fila ("acortado");
    resizeSongBlock (+1);  fila ("alargado");
    resizeSongBlock (+1);  fila ("alargado otra vez");
    resizeSongBlock (+1);  fila ("y contra el vecino");

    //  Y QUE LO NUEVO VUELVA. El silenciado de carriles y el tramo en bucle
    //  son estado del arreglo igual que las celdas, y lo que no se guarda se
    //  pierde sin avisar: la persona deja tres carriles callados, cierra, y
    //  al volver suena todo. Se comprueba por el MISMO arbol que escribe el
    //  fichero de proyecto -captureState y applyState-, que es el camino que
    //  de verdad recorre un proyecto al guardarse.
    engine.setSongLaneMute (0, true);
    engine.setSongLaneMute (3, true);
    engine.setSongLoop (2, 5);

    const auto arbol = captureState();
    engine.setSongLaneMute (0, false);
    engine.setSongLaneMute (3, false);
    engine.clearSongLoop();
    applyState (arbol);

    std::cout << "{\"vuelta\":1,\"mudos\":[";
    for (int ln = 0; ln < AudioEngine::kSongLanes; ++ln)
        std::cout << (ln ? "," : "") << (engine.isSongLaneMuted (ln) ? 1 : 0);
    std::cout << "],\"bucle\":[" << engine.getSongLoopFrom() << ","
              << engine.getSongLoopTo() << "]}" << std::endl;

    //  --- UN GOLPE SUELTO DE UN PAD QUE NO ESTA EN LA REJILLA -------------
    //
    //  El pincel de un solo golpe guarda -(pad+1) y `selectedPad` va de 0 a 63,
    //  pero a Playlist se le pasaba la tabla de colores del banco QUE SE VE:
    //  dieciseis huecos. Cualquier golpe de los bancos B, C o D leia fuera del
    //  array en CADA repintado de la ficha CANCION.
    //
    //  NINGUNA de las ocho reglas del banco puede verlo: es un fallo de INDICE
    //  y no de geometria — el bloque se maqueta perfecto, no solapa, no se sale
    //  y no lleva texto. Y no se cae hoy, porque lo leido acaba en
    //  Zati::colour, que envuelve con un modulo: el sintoma era un bloque del
    //  color de otro pad, y una lectura fuera de rango que un ASan, un
    //  asignador endurecido o MTE convierten en un cierre.
    //
    //  Se mide PINTANDO, que es donde vive: se escribe el golpe, se pide un
    //  fotograma sobre una imagen y se cuenta cuantas veces blockColour tuvo
    //  que pedir un pad que su tabla no tenia. Preguntarselo a la tabla por
    //  dentro seria repetir la constante en vez de medir.
    engine.clearSong();
    engine.setSongLength (8);
    engine.setSongCell (0, 0, -(33 + 1));      // el pad 33: banco C, fuera de la rejilla
    engine.setSongCell (1, 1, -(63 + 1));      // y el ultimo de los sesenta y cuatro
    Playlist::zatisFueraDeRango = 0;
    refreshSong();
    {
        juce::Image img (juce::Image::ARGB, juce::jmax (1, songGrid.getWidth()),
                         juce::jmax (1, songGrid.getHeight()), true);
        juce::Graphics g (img);
        songGrid.paintEntireComponent (g, false);
    }
    std::cout << "{\"golpe\":\"suelto\",\"celdas\":["
              << engine.getSongCell (0, 0) << "," << engine.getSongCell (1, 1)
              << "],\"zatis\":" << songGrid.numZatis()
              << ",\"fuera\":" << Playlist::zatisFueraDeRango << "}" << std::endl;

    //  Y LO QUE EL FICHERO DE PROYECTO PUEDE METER EN UNA CELDA. `setSongCell`
    //  comprobaba lane y bar y guardaba el valor tal cual, y ese valor sale de
    //  `toks[b].getIntValue()`: un project.xml corrupto metia cualquier entero
    //  y cada consumidor tenia que volver a validarlo. Se acota en la puerta.
    engine.setSongCell (2, 0, -9999);
    engine.setSongCell (2, 1, 999999);
    engine.setSongCell (2, 2, -(AudioEngine::kNumPads));   // valido: el pad 63
    std::cout << "{\"celda\":\"acotada\",\"puestas\":["
              << engine.getSongCell (2, 0) << "," << engine.getSongCell (2, 1)
              << "," << engine.getSongCell (2, 2) << "]}" << std::endl;
}

// ============================================================================
//  Going to the background, and coming back.
//
//  Nothing used to happen here at all, and three things went wrong for it.
//  The Oboe stream stayed open, so a real-time thread and its wakeups kept
//  running behind whatever the phone was doing. If REC was on, the microphone
//  stayed open too - and from Android 12 the system cuts background capture
//  without telling the app, so the recording kept "running" and recorded
//  silence. And nothing was written anywhere, so a process the system decided
//  to reclaim took the session with it.
//
//  A phone call, another app, or the screen going off all pause the activity,
//  which is why stopping here also covers the case that reads worst in a demo:
//  ZATI playing on top of a call.
// ============================================================================
//  UNA MAQUINA CON TRABAJO DENTRO.
//
//  Las fotos de la ficha de Play tienen que enseñar lo que hace la caja, y una
//  caja recien abierta son dieciseis huecos grises, una onda vacia y una
//  rejilla en blanco. Esto le pone doce sonidos sinteticos con sus nombres,
//  sus colores y un patron escrito, que es el estado en el que la app se usa.
//
//  Sinteticos y no grabados: un WAV de verdad en el repositorio es peso, es
//  una licencia que aclarar y es una foto que deja de poder rehacerse en
//  cuanto el fichero se mueve. Estos salen de cuatro lineas de matematicas y
//  suenan de verdad - se pueden disparar en la foto y en la app.
//
//  Solo desde el arranque de auditoria. No hay ningun camino desde la interfaz
//  que llegue aqui.
void MainComponent::auditDemo()
{
    struct Piece { const char* name; int zati; int kind; float hz; float decay; };
    //  kind 0 = golpe con tono que cae, 1 = ruido con cuerpo, 2 = ruido corto,
    //  3 = nota mantenida. Doce piezas: una caja de ritmos con su bajo y sus
    //  acordes, que es lo que se monta de verdad.
    //  Dieciseis y no doce: la rejilla que se ve en la foto tiene dieciseis
    //  huecos y cuatro vacios al fondo se leen como una app a medio hacer.
    static const Piece kit[16] =
    {
        { "KICK",  0, 0,  58.0f, 0.30f }, { "SNARE", 1, 1, 190.0f, 0.16f },
        { "HAT",   2, 2,   0.0f, 0.04f }, { "CLAP",  3, 1, 320.0f, 0.12f },
        { "RIM",   4, 2,   0.0f, 0.03f }, { "TOM",   5, 0, 120.0f, 0.22f },
        { "BASS",  6, 3,  55.0f, 0.45f }, { "CHORD", 7, 3, 220.0f, 0.60f },
        { "PERC",  1, 2,   0.0f, 0.06f }, { "RIDE",  2, 2,   0.0f, 0.25f },
        { "VOX",   3, 3, 330.0f, 0.35f }, { "SUB",   6, 0,  42.0f, 0.40f },
        { "STAB",  5, 3, 440.0f, 0.28f }, { "SHAKE", 4, 2,   0.0f, 0.05f },
        { "CONGA", 0, 0, 180.0f, 0.18f }, { "PAD",   7, 3, 165.0f, 0.80f }
    };

    juce::Random rnd (404);
    constexpr double fs = 44100.0;

    //  EN EL BANCO QUE PIDAN, no siempre en el A.  ZATI_BANK=0|1|2|3.
    //
    //  La maqueta escribia sus dieciseis piezas en los pads 1..16 y se
    //  quedaba en el banco A, asi que el banco jamas ha medido B, C ni D - y
    //  ahi vivia el fallo que motiva esto: el canalon de la rejilla de pasos
    //  pintaba el numero del CARRIL, o sea 01..16 en los cuatro bancos,
    //  mientras la cabecera de la misma pista decia "PAD 17". Un fallo que
    //  solo existe fuera del banco A no lo ve una prueba que solo mira el A.
    const int demoBank = juce::jlimit (0, kNumBanks - 1,
                                       UiAudit::env ("ZATI_BANK").getIntValue());
    const int demoBase = demoBank * kPadsPerBank;

    for (int i = demoBase; i < demoBase + 16; ++i)
    {
        const auto& p = kit[(size_t) (i - demoBase)];
        const int len = juce::jmax (1024, (int) (fs * (p.decay * 2.5f)));

        SampleBuffer::Ptr sb = new SampleBuffer();
        sb->sourceSampleRate = fs;
        sb->buffer.setSize (1, len);
        float* d = sb->buffer.getWritePointer (0);

        double ph = 0.0;
        for (int n = 0; n < len; ++n)
        {
            const float t   = (float) n / (float) fs;
            const float env = std::exp (-t / juce::jmax (0.005f, p.decay));
            float v = 0.0f;

            switch (p.kind)
            {
                case 0:   // el tono baja mientras cae: eso es un bombo
                {
                    const double f = p.hz * (1.0 + 1.6 * std::exp (-t / 0.03f));
                    ph += 2.0 * juce::MathConstants<double>::pi * f / fs;
                    v = std::sin ((float) ph) * env;
                    break;
                }
                case 1:   // ruido con un cuerpo afinado debajo
                    ph += 2.0 * juce::MathConstants<double>::pi * p.hz / fs;
                    v = (0.6f * (rnd.nextFloat() * 2.0f - 1.0f) + 0.4f * std::sin ((float) ph)) * env;
                    break;
                case 2:   // ruido a secas, y la diferencia lo aclara
                    v = (rnd.nextFloat() * 2.0f - 1.0f) * env;
                    break;
                default:  // nota mantenida con dos armonicos
                    ph += 2.0 * juce::MathConstants<double>::pi * p.hz / fs;
                    v = (std::sin ((float) ph) + 0.4f * std::sin (2.0f * (float) ph)
                                               + 0.2f * std::sin (3.0f * (float) ph)) * env * 0.5f;
                    break;
            }
            d[n] = juce::jlimit (-0.98f, 0.98f, v * 0.9f);
        }

        //  El charles y el rim se aclaran con una diferencia de primer orden,
        //  que es un paso alto de un polo y cuesta una resta.
        if (p.kind == 2)
            for (int n = len - 1; n > 0; --n) d[n] = 0.7f * (d[n] - d[n - 1]);

        assignSampleToPad (i, sb, {});
        padName[(size_t) i] = kit[(size_t) (i - demoBase)].name;
        padZati[(size_t) i] = kit[(size_t) (i - demoBase)].zati;
        if (auto* pad = pads[i])
            pad->setSampleInfo (uiSample[(size_t) i], padName[(size_t) i],
                                padStart01[(size_t) i], padEnd01[(size_t) i]);
    }

    //  Y un patron escrito: cuatro por cuatro con el charles a corcheas y la
    //  caja en el dos y el cuatro. Una rejilla vacia no enseña un secuenciador.
    static const int kicks[]  = { 0, 6, 8, 14 };
    static const int snares[] = { 4, 12 };
    static const int hats[]   = { 0, 2, 4, 6, 8, 10, 12, 14 };
    static const int bass[]   = { 0, 3, 8, 11 };
    //  En el MOTOR y en el espejo de la interfaz. La rejilla dibuja desde
    //  pattern[][][], no desde el motor, asi que escribir solo en el motor
    //  dejaba la foto del secuenciador con la rejilla VACIA - un patron que
    //  suena y no se ve.
    auto write = [this, demoBase] (const int* steps, int n, int pad)
    {
        for (int i = 0; i < n; ++i)
        {
            engine.setStep (0, steps[i], demoBase + pad, true);
            pattern[0][(size_t) steps[i]][(size_t) (demoBase + pad)] = true;
        }
    };
    write (kicks,  (int) (sizeof (kicks)  / sizeof (int)), 0);
    write (snares, (int) (sizeof (snares) / sizeof (int)), 1);
    write (hats,   (int) (sizeof (hats)   / sizeof (int)), 2);
    write (bass,   (int) (sizeof (bass)   / sizeof (int)), 6);

    //  El banco DESPUES de escribir: selectBank vuelve a maquetar y a pedirle
    //  la rejilla al patron, y hacerlo antes dejaba la foto con la rejilla del
    //  banco nuevo dibujada sobre un patron que aun no existia.
    selectBank (demoBank);
    refreshStepGrid();

    selectPad (demoBase);

    //  Y con el aumento puesto, si el banco lo pide: la unica forma de mirar
    //  una foto del zoom es que la sonda pueda ponerlo.
    //  La barra de trabajo, puesta a mano: es la unica forma de mirar una foto
    //  de algo que dura dos segundos.
    //  La tira del medidor, puesta a mano. Igual que la barra de trabajo: en
    //  una maqueta estatica el nivel es cero, y una tira apagada no ensena si
    //  el verde se ve.  ZATI_VU=0.7,1.0 - izquierda,derecha.
    if (const auto v = UiAudit::env ("ZATI_VU"); v.contains (","))
    {
        vuL = (float) v.upToFirstOccurrenceOf (",", false, false).getDoubleValue();
        vuR = (float) v.fromFirstOccurrenceOf (",", false, false).getDoubleValue();
        //  Y se queda: el primer intento solo llamaba a setVu y la foto salia
        //  con el nivel REAL de la maqueta -los pads de ZATI_DEMO suenan- que
        //  son -16 dB y todo verde. Una sonda que la app pisa a la vuelta
        //  siguiente no es una sonda.
        vuHeld = true;
        cristal.setVu (vuL, vuR);
    }

    //  Y UN TROCEADO DE VERDAD, si lo piden: ZATI_CHOPGO=8 corta el primer pad
    //  del banco en ocho. Es la unica forma de que el banco mida lo que un
    //  troceado deja en disco y lo que recupera, que es donde estaba el fallo.
    if (const auto n = UiAudit::env ("ZATI_CHOPGO"); n.getIntValue() >= 2)
    {
        chopSlices    = juce::jlimit (2, 16, n.getIntValue());
        chopOnlyEmpty = false;          // la maqueta llena los dieciseis
        chopByHits    = false;
        selectPad (demoBase);
        applyAutoChop();
    }

    //  ZATI_STEPS=0,32 escribe un paso en esos pads del patron 1. Es la unica
    //  forma de que el banco mida si un patron se copia solo de un banco a otro
    //  al guardar y volver: un patron no es un componente y no sale en el
    //  volcado del arbol, que es como ese fallo vivio sin que nada lo viera.
    if (const auto ps = UiAudit::env ("ZATI_STEPS"); ps.isNotEmpty())
    {
        for (int b2 = 0; b2 < kNumPatterns; ++b2)
            for (int st = 0; st < kNumSteps; ++st)
                for (int p2 = 0; p2 < kNumPads; ++p2)
                    if (pattern[(size_t) b2][(size_t) st][(size_t) p2])
                    {
                        pattern[(size_t) b2][(size_t) st][(size_t) p2] = false;
                        engine.setStep (b2, st, p2, false);
                    }

        juce::StringArray cual;
        cual.addTokens (ps, ",", "");
        for (const auto& t : cual)
        {
            const int pad = t.trim().getIntValue();
            if (! juce::isPositiveAndBelow (pad, kNumPads)) continue;
            engine.setStep (0, 0, pad, true);
            pattern[0][0][(size_t) pad] = true;
        }
        refreshStepGrid();
    }

    if (const auto b = UiAudit::env ("ZATI_BUSY"); b.isNotEmpty())
    {
        beginBusy (T ("Cargando"));
        if (const double p = b.getDoubleValue(); p > 0.0 && p <= 1.0)
            setBusyProgress ((float) p);
    }

    if (const auto tr = UiAudit::env ("ZATI_TRIM"); tr.contains (","))
    {
        const float a = (float) tr.upToFirstOccurrenceOf (",", false, false).getDoubleValue();
        const float b = (float) tr.fromFirstOccurrenceOf (",", false, false).getDoubleValue();
        padStart01[(size_t) demoBase] = juce::jlimit (0.0f, 0.99f, a);
        padEnd01[(size_t) demoBase]   = juce::jlimit (padStart01[(size_t) demoBase] + 0.01f, 1.0f, b);
        const int len = padSourceLength (demoBase);
        engine.setPadStart (demoBase, (int) (padStart01[(size_t) demoBase] * (float) len));
        engine.setPadEnd   (demoBase, (int) (padEnd01[(size_t) demoBase]   * (float) len));
        selectPad (demoBase);
    }

    //  Con el aumento puesto y centrado donde lo centra el boton: en el medio
    //  del recorte. Es la unica forma de mirar una foto de esto.
    if (const auto z = UiAudit::env ("ZATI_ZOOM"); z.isNotEmpty())
        waveform.setZoom ((float) z.getDoubleValue(),
                          (padStart01[(size_t) demoBase] + padEnd01[(size_t) demoBase]) * 0.5f);

    repaint();
}

//  LO QUE EL CATALOGO DICE Y LO QUE EL CANDADO HACE. Ver Tests/dlc.py.
void MainComponent::auditDlc()
{
    plantaPacksDePrueba();
    instCatalogo = Instrumentos::lee();

    for (int i = 0; i < (int) instCatalogo.size(); ++i)
    {
        const auto& p = instCatalogo[(size_t) i];
        std::cout << "{\"dlc\":\"pack\",\"i\":" << i
                  << ",\"id\":\"" << p.id << "\",\"nombre\":\"" << p.nombre
                  << "\",\"dentro\":" << (p.dentro ? 1 : 0)
                  << ",\"pago\":" << (p.dePago ? 1 : 0)
                  << ",\"abierto\":" << (p.abierto ? 1 : 0)
                  << ",\"instr\":" << (int) p.instr.size() << "}" << std::endl;
    }

    auto buscaPack = [this] (const juce::String& id) -> int
    {
        for (int i = 0; i < (int) instCatalogo.size(); ++i)
            if (instCatalogo[(size_t) i].id == id) return i;
        return -1;
    };

    //  EL PACK CERRADO NO ENTRA AL REPARTO.
    //
    //  La primera version contaba PADS, y era una linea que imprimia OK: se
    //  rompio el candado a proposito y siguio saliendo verde. El reparto de un
    //  instrumento de disco es ASINCRONO -loader.loadAsync, hilo propio- y aqui
    //  no hay bucle de mensajes que lo recoja, asi que los pads salen a cero
    //  con el candado puesto y sin el. Una comprobacion que no puede decir que
    //  no vale menos que ninguna, porque ademas ocupa el sitio de la que si.
    //
    //  Lo que SI es sincrono es la primera linea del reparto: pushUndo. Se
    //  vacia la pila y se mira si crecio. Y de paso mide lo correcto, que no es
    //  "llegaron los ficheros" -eso ya lo mide CARGAR KIT- sino "el candado
    //  dejo pasar".
    currentBank = 0;
    undoStack.clear();
    instPack = buscaPack ("02 ESTUDIO");
    if (instPack >= 0)
    {
        //  Dos toques, que es lo que pide la confirmacion. Si el candado deja
        //  pasar, el segundo reparte. Y el candado se mira ANTES de armar, asi
        //  que con el puesto ni siquiera se arma.
        cargaInstrumento (0);
        cargaInstrumento (0);
    }
    std::cout << "{\"dlc\":\"cerrado\",\"reparto\":" << (int) undoStack.size() << "}" << std::endl;

    //  Y CON LA LICENCIA PUESTA, EL MISMO PACK CARGA. Es la otra mitad: un
    //  candado que nunca abre pasa la prueba de arriba y no sirve de nada.
    Instrumentos::concede ("02 ESTUDIO");
    instCatalogo = Instrumentos::lee();
    instPack = buscaPack ("02 ESTUDIO");
    const bool abiertoAhora = (instPack >= 0) && instCatalogo[(size_t) instPack].abierto;
    std::cout << "{\"dlc\":\"licencia\",\"abierto\":" << (abiertoAhora ? 1 : 0) << "}" << std::endl;

    //  EL PACK ABIERTO CARGA DIECISEIS. Sincrono: el reparto usa el cargador,
    //  que tiene su propio hilo, asi que se leen los ficheros aqui y se cuenta
    //  lo que el catalogo entrega - que es lo que esta prueba mide, no el
    //  cargador, que ya lo mide CARGAR KIT.
    instPack = buscaPack ("01 DEMO");
    int presets = 0, conNombre = 0;
    if (instPack >= 0)
    {
        const auto& p = instCatalogo[(size_t) instPack];
        for (const auto& in : p.instr)
        {
            if (! Instrumentos::presetsDe (in).isEmpty()) ++presets;
            //  Y QUE EL NUMERO DE ORDEN NO SE VEA: "01 SUBBASS" es el orden en
            //  el disco y "SUBBASS" es el nombre. Ensenar las dos cifras seria
            //  ensenar el andamio.
            if (in.nombre.isNotEmpty() && ! juce::CharacterFunctions::isDigit (in.nombre[0])) ++conNombre;
        }
    }
    std::cout << "{\"dlc\":\"abierto\",\"conAudio\":" << presets
              << ",\"conNombre\":" << conNombre << "}" << std::endl;

    //  Y LA FABRICA SIGUE SIENDO UN INSTRUMENTO. Cargarla llena el banco sin
    //  pasar por ningun fichero, que es su camino propio.
    for (int i = 0; i < kNumPads; ++i) { uiSample[(size_t) i] = nullptr; padHasSample[(size_t) i] = false; }
    //  Por NOMBRE y no por indice: desde que SINTES existe la fabrica ya no es
    //  el pack cero, y un indice a mano en un banco es como se mide otra cosa
    //  creyendo que se mide esta.
    instPack = buscaPack ("ZATI");
    currentBank = 0;

    //  Y EL BANCO SE ENSUCIA ANTES, que es la mitad que faltaba.
    //
    //  Un instrumento nuevo heredaba el pad que hubiera debajo:
    //  assignSampleToPad reiniciaba el recorte y NADA mas, asi que la
    //  afinacion, el corte del filtro, el reves, el choke y los seis envios se
    //  quedaban donde los dejo el sonido anterior. Cargar un banco encima de
    //  uno que habias ajustado te daba dieciseis sonidos nuevos que no se oian,
    //  y sin nada en la interfaz que dijera por que.
    //
    //  Y el corte se ensucia SOLO EN EL MOTOR a proposito -sin tocar padCut-
    //  porque asi es como llega de verdad: lo escribe el bloqueo de corte de un
    //  paso, desde el hilo de audio, y se queda puesto. assignSampleToPad no
    //  empujaba setPadCutoff nunca, asi que el mando decia "abierto" y el pad
    //  sonaba filtrado.
    for (int i = 0; i < kPadsPerBank; ++i)
    {
        padPitch[(size_t) i]   = 12.0f;  engine.setPadPitch   (i, 12.0f);
        padReverse[(size_t) i] = true;   engine.setPadReverse (i, true);
        padChokeUI[(size_t) i] = 3;      engine.setPadChoke   (i, 3);
        engine.setPadCutoff (i, 200.0f);                       // como un bloqueo de paso
        for (int f = 0; f < AudioEngine::kNumFx; ++f) engine.setPadSend (i, f, 1.0f);
    }

    cargaInstrumento (2);      // TEXTURA, que vive en el banco C
    cargaInstrumento (2);
    int deFabrica = 0;
    for (int i = 0; i < kPadsPerBank; ++i) if (padHasSample[(size_t) i]) ++deFabrica;
    std::cout << "{\"dlc\":\"fabrica\",\"pads\":" << deFabrica << "}" << std::endl;

    //  Lo peor de cada uno de los dieciseis, que es lo que hay que mirar: con
    //  el maximo, un solo pad que herede lo canta.
    float pitchMax = 0.0f, envioMax = 0.0f, corteMin = AudioEngine::kFiltOpenHz;
    int   revesN = 0, chokeN = 0, espejoMal = 0;
    for (int i = 0; i < kPadsPerBank; ++i)
    {
        pitchMax = juce::jmax (pitchMax, std::abs (padPitch[(size_t) i]));
        corteMin = juce::jmin (corteMin, engine.getPadCutoff (i));
        if (padReverse[(size_t) i]) ++revesN;
        if (padChokeUI[(size_t) i] != 0) ++chokeN;
        for (int f = 0; f < AudioEngine::kNumFx; ++f)
            envioMax = juce::jmax (envioMax, engine.getPadSend (i, f));
        //  Y QUE EL ESPEJO Y EL MOTOR DIGAN LO MISMO, que es la clase de fallo
        //  que no se ve mirando la pantalla: un pad filtrado con el mando
        //  abierto.
        if (std::abs (engine.getPadCutoff (i) - padCut[(size_t) i]) > 0.5f) ++espejoMal;
    }
    std::cout << "{\"dlc\":\"herencia\",\"pitch\":" << pitchMax
              << ",\"corte\":" << corteMin
              << ",\"reves\":" << revesN
              << ",\"choke\":" << chokeN
              << ",\"envio\":" << envioMax
              << ",\"espejo_mal\":" << espejoMal << "}" << std::endl;
}

//  EL REBOTE, MEDIDO.
//
//  Es la unica funcion de la app cuyo resultado sale del telefono, y la unica
//  que no se puede juzgar mirando la pantalla: la barra llega al final igual
//  cuando el fichero esta bien que cuando tiene medio segundo. Se hace el
//  rebote en el hilo que llama - Exporter::run es publico a proposito - y se
//  cuentan ficheros y bytes.
void MainComponent::auditExport()
{
    selectedPattern = 0;
    engine.setEditPattern (0);
    engine.setSongMode (false);
    engine.clearPattern (0);
    engine.setPatternLength (0, 16);
    for (int st = 0; st < kNumSteps; ++st)
        for (int p = 0; p < kNumPads; ++p)
            pattern[0][(size_t) st][(size_t) p] = false;

    //  Cuatro pads en un patron de dieciseis, que es lo que hay en cualquier
    //  sesion a los dos minutos.
    for (int p = 0; p < 4; ++p)
        for (int st = p; st < 16; st += 4)
        {
            pattern[0][(size_t) st][(size_t) p] = true;
            engine.setStep (0, st, p, true);
        }

    int cargados = 0;
    for (int p = 0; p < kNumPads; ++p) if (uiSample[(size_t) p] != nullptr) ++cargados;

    auto dir = ProjectStore::exports().getChildFile ("BANCO_EXPORT");
    dir.deleteRecursively();

    //  LA CANCION LARGA, que es la que cerraba la app: 64 compases -el tope-
    //  son 128 s a 120 BPM, o sea 49 MB de un solo AudioBuffer con el codigo
    //  viejo. Se mide con la memoria del proceso desde fuera; aqui lo unico
    //  que hace falta es que la cancion sea la mas larga que la app admite.
    const bool largo = UiAudit::env ("ZATI_EXPORT") == "largo";
    if (largo)
    {
        engine.setSongMode (true);
        engine.setSongLength (AudioEngine::kSongBars);
        for (int b = 0; b < AudioEngine::kSongBars; ++b)
            engine.setSongCell (0, b, 1);          // el patron 1 en los 64 compases
    }

    for (int ronda = 0; ronda < (largo ? 1 : 3); ++ronda)
    {
        const bool pistas = (ronda == 1);
        //  Y la tercera ronda en OGG, que es otro escritor y otro fichero: sin
        //  medirlo, "exportar comprimido" es una tapa que cambia un rotulo.
        const bool comprimido = (ronda == 2);
        Exporter job (engine, uiSample, padName, dir, comprimido ? "BANCOOGG" : "BANCO",
                      pistas, 48000.0, comprimido);
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        job.run();                       // en ESTE hilo: el banco no espera a nadie
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;

        juce::Array<juce::File> hechos;
        dir.findChildFiles (hechos, juce::File::findFiles, false, comprimido ? "*.ogg" : "*.wav");
        juce::int64 bytes = 0;
        for (auto& f : hechos) bytes += f.getSize();

        std::cout << "{\"export\":\"" << (comprimido ? "ogg" : pistas ? "pistas" : "master")
                  << "\",\"pads\":" << cargados
                  << ",\"ok\":" << (job.resultOk ? 1 : 0)
                  << ",\"ficheros\":" << hechos.size()
                  << ",\"bytes\":" << bytes
                  << ",\"ms\":" << (int) ms
                  << ",\"motor_mb\":" << (double) sizeof (AudioEngine) / 1048576.0
                  << ",\"parte\":\"" << job.resultText << "\"}" << std::endl;
    }

    //  Y LA CARPETA ELEGIDA, que es lo nuevo y lo que no se puede comprobar
    //  mirando: se apunta una que SI acepta escritura y se comprueba que
    //  ProjectStore la devuelve; se apunta una que NO -un hijo de un fichero,
    //  que nunca puede ser carpeta- y se comprueba que la rechaza Y que sigue
    //  valiendo la anterior. Un ajuste que se guarda mal deja el rebote en un
    //  sitio que no es, y eso solo se ve cuando ya has cerrado la app.
    {
        const auto porDefecto = ProjectStore::exportsPorDefecto();
        auto mia = ProjectStore::home().getChildFile ("BANCO_DESTINO");
        const bool acepta = ProjectStore::setExports (mia);
        const bool vuelve = (ProjectStore::exports() == mia);

        //  Una ruta imposible: hijo de un FICHERO, que ningun sistema deja
        //  crear como carpeta. Es la unica forma de probar el rechazo sin
        //  depender de los permisos de la maquina donde corre el banco.
        auto fichero = mia.getChildFile ("no-soy-carpeta.txt");
        fichero.replaceWithText ("z");
        const bool rechaza = ! ProjectStore::setExports (fichero.getChildFile ("dentro"));
        const bool aguanta = (ProjectStore::exports() == mia);

        ProjectStore::clearExports();
        const bool limpia = (ProjectStore::exports() == porDefecto);

        std::cout << "{\"export\":\"destino\",\"acepta\":" << (acepta ? 1 : 0)
                  << ",\"vuelve\":" << (vuelve ? 1 : 0)
                  << ",\"rechaza\":" << (rechaza ? 1 : 0)
                  << ",\"aguanta\":" << (aguanta ? 1 : 0)
                  << ",\"limpia\":" << (limpia ? 1 : 0) << "}" << std::endl;
        mia.deleteRecursively();
    }
}

//  Y EL CAMINO DE VERDAD, que no es el de arriba: el hilo aparte, el
//  temporizador de la app preguntando, la ficha repintandose encima y el boton
//  de cancelar. Lo que la persona toca es esto, y lo que se cierra tambien - un
//  rebote hecho en el hilo que llama no pasa por ninguno de los tres.
void MainComponent::auditExportAsync (bool cancelar)
{
    destinoCache = juce::File();
    openSheet (exportSheet, setButton);
    startExport (true);

    if (cancelar && exportJob != nullptr)
        juce::Timer::callAfterDelay (40, [this] { if (exportJob != nullptr) exportJob->signalThreadShouldExit(); });

    esperaExport (cancelar, 0);
}

// ============================================================================
//  LOS DOSCIENTOS CINCUENTA Y SEIS INSTRUMENTOS. Ver Tests/instr.py.
//
//  QUE SE ESCRIBE Y POR QUE NO SE MIDE AQUI. Esta funcion no juzga nada: rinde
//  y deja ficheros. La ponderacion K, la deteccion de chasquidos y el
//  descriptor de parecido ya estan escritos en Python -Tests/kits.py- y
//  volverlos a escribir en C++ seria la misma regla en dos sitios, que es el
//  fallo que este banco lleva encontrando desde el principio.
//
//  Y no se escriben los 256 enteros: son 2.3 MB cada uno, o sea 590 MB de
//  temporales. Se escribe
//
//    - LA ZONA DE REFERENCIA de los 256 (raiz 0, capa fuerte): con eso se mide
//      que ninguno este mudo, que los 256 esten igualados y que no haya dos que
//      sean el mismo sonido, que son las cuatro que necesitan a TODOS.
//    - EL PRESET ENTERO del primero de cada familia: con eso se miden las
//      cosas que son ESTRUCTURA -la costura entre octavas, la del bucle y las
//      dos capas- y para eso no hacen falta los 256, hace falta uno por
//      algoritmo.
// ============================================================================
void MainComponent::auditInstr()
{
    const auto dir = juce::File (UiAudit::env ("ZATI_INSTR"));
    if (! dir.isDirectory() && ! dir.createDirectory().wasOk())
    {
        std::cout << "{\"instr\":\"error\",\"que\":\"no hay carpeta\"}" << std::endl;
        return;
    }

    for (int f = 0; f < Sintes::kFamilias; ++f)
    {
        const auto& F = Sintes::tabla()[f];
        for (int pr = 0; pr < Sintes::kPresets; ++pr)
        {
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            auto sb = Sintes::sintetiza (f, pr);
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            if (sb == nullptr) continue;

            const auto& Z = sb->zonas[(size_t) Sintes::kZonaRef];
            juce::AudioBuffer<float> ref (1, Z.fin - Z.ini);
            ref.copyFrom (0, 0, sb->buffer, 0, Z.ini, Z.fin - Z.ini);
            ProjectStore::writeSample (dir.getChildFile (juce::String::formatted ("ref-%02d-%02d.wav", f, pr)),
                                       ref, sb->sourceSampleRate);

            if (pr == 0)
                ProjectStore::writeSample (dir.getChildFile (juce::String::formatted ("todo-%02d.wav", f)),
                                           sb->buffer, sb->sourceSampleRate);

            std::cout << "{\"instr\":\"preset\",\"fam\":" << f << ",\"pre\":" << pr
                      << ",\"familia\":\"" << F.nombre << "\""
                      << ",\"nombre\":\"" << F.p[pr].nombre << "\""
                      << ",\"sostiene\":" << (F.sostiene ? 1 : 0)
                      << ",\"zonas\":" << sb->nZonas
                      << ",\"muestras\":" << sb->buffer.getNumSamples()
                      << ",\"ms\":" << juce::String (ms, 1)
                      << ",\"mapa\":[";
            for (int z = 0; z < sb->nZonas; ++z)
            {
                const auto& q = sb->zonas[(size_t) z];
                std::cout << (z ? "," : "") << "[" << q.raiz << "," << q.capa << ","
                          << q.ini << "," << q.fin << "," << q.bucleIni << "," << q.bucleFin << "]";
            }
            std::cout << "]}" << std::endl;
        }
    }

    // ------------------------------------------------------------------
    //  Y QUE VUELVA SIENDO UN INSTRUMENTO.
    //
    //  Es la comprobacion que el troceado enseno a hacer: alli volvian los
    //  dieciseis trozos y sonaban bien, y lo que no volvia era la RELACION.
    //  Aqui pasaria lo mismo con el audio escrito a WAV: el pad sonaria
    //  parecido y habria dejado de ser un instrumento -una zona en vez de diez,
    //  sin capas y sin las otras cuatro octavas-.
    //
    //  Con el MISMO arbol que escribe el fichero de proyecto, y BORRANDO EL PAD
    //  A MANO entre medias: si al volver sigue puesto no es que se haya
    //  guardado, es que nadie lo quito.
    {
        const int pad = kBancoInstr * kPadsPerBank + 3;
        ponInstrumentoEnPad (pad, 3, 5);
        const auto arbol = captureState();

        uiSample[(size_t) pad] = nullptr;
        padHasSample[(size_t) pad] = false;
        engine.clearPad (pad);

        std::array<int, kNumPads> mapa {};
        readInstMap (arbol, mapa);
        const int receta = mapa[(size_t) pad];
        if (receta >= 0)
            ponInstrumentoEnPad (pad, receta / Sintes::kPresets, receta % Sintes::kPresets);

        auto* sb = uiSample[(size_t) pad].get();
        std::cout << "{\"instr\":\"vuelta\",\"receta\":" << receta
                  << ",\"fam\":" << (sb ? sb->familia : -1)
                  << ",\"pre\":" << (sb ? sb->preset : -1)
                  << ",\"zonas\":" << (sb ? sb->nZonas : 0)
                  //  Y QUE NO SE ESCRIBA EL AUDIO: el que decide eso es este
                  //  mismo campo, en SessionKeeper y en stepPadSaveJob.
                  << ",\"escribeWav\":" << ((sb && sb->familia >= 0) ? 0 : 1)
                  << "}" << std::endl;
    }

    // ------------------------------------------------------------------
    //  Y QUE EL DESTINO SE ELIJA DE VERDAD.
    //
    //  Era el pad del mismo numero que la familia dentro del banco D, asi que
    //  CUALQUIER otra comprobacion sale verde con el destino clavado: el
    //  instrumento carga, suena, vuelve del proyecto y ocupa un pad. Lo unico
    //  que lo separa es pedir un pad que NO sea el suyo y mirar donde acaba.
    //
    //  Por el camino de verdad -cargaInstrumento, que es lo que toca el dedo- y
    //  no llamando a ponInstrumentoEnPad, que es justo donde el fallo no
    //  estaria: el pad se lo pasa quien llama.
    {
        instCatalogo = Instrumentos::lee();
        instPack = 0;                                   // SINTES va el primero
        const int familia = 11;                         // CUERDA PULS
        const int clavado = kBancoInstr * kPadsPerBank + familia;
        const int pedido  = 2 * kPadsPerBank + 5;       // otro banco Y otra casilla
        instDestPad   = pedido;
        instBancoDest = pedido / kPadsPerBank;
        cargaInstrumento (familia);

        int fue = -1;
        for (int i = 0; i < kNumPads; ++i)
            if (padEsInstrumento (i) && uiSample[(size_t) i]->familia == familia) { fue = i; break; }

        std::cout << "{\"instr\":\"destino\",\"pedido\":" << pedido
                  << ",\"fue\":" << fue << ",\"clavado\":" << clavado << "}" << std::endl;
    }

    //  Y LO QUE CUESTA LLENAR EL BANCO D, que es la cifra que decide si los
    //  dieciseis pueden venir puestos de fabrica o hay que ir a buscarlos.
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    for (int f = 0; f < Sintes::kFamilias; ++f) Sintes::sintetiza (f, 0);
    std::cout << "{\"instr\":\"bancoD\",\"ms\":"
              << juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 1) << "}" << std::endl;
}

//  GUARDAR EL BANCO DE DELANTE COMO KIT.
//
//  La otra mitad de CARGAR KIT, y la que faltaba: se podia repartir una carpeta
//  de sonidos por un banco y no se podia guardar el banco que habias montado.
//  Un kit hecho aqui se quedaba dentro del proyecto que lo tenia, o sea que
//  para usarlo en otro habia que volver a buscar los mismos dieciseis ficheros.
//
//  SALE CON LA FORMA DE UN BANCO DESCARGADO y no con un formato propio: una
//  carpeta con dieciseis WAV numerados. Asi el kit que guardas y el pack que te
//  bajas entran por la MISMA puerta -CARGAR KIT, que ordena por nombre- y el
//  numero de delante ES el orden. Un formato propio habria sido una segunda
//  forma de hacer lo mismo, y ademas la unica que no sabria abrir nadie mas.
//
//  Y SE ESCRIBE LO QUE SUENA, o sea el trozo RECORTADO y no el fichero entero.
//  Un pad de un break de cuatro minutos con el recorte puesto en un golpe es
//  ese golpe: guardar los cuatro minutos dieciseis veces son 300 MB de kit para
//  dieciseis sonidos que duran medio segundo. Los fundidos y el reves no van -
//  son ajustes del pad y viven en el proyecto; un kit es el material.
//  Ver Tests/kit.py.
void MainComponent::auditKit (const juce::String& nombre)
{
    //  Con el recorte a la mitad en el primer pad, para que la prueba pueda ver
    //  si lo que se escribio es el TROZO o el fichero entero: sin mover ningun
    //  recorte, las dos respuestas darian el mismo numero.
    const int base = currentBank * kPadsPerBank;
    padStart01[(size_t) base] = 0.25f;
    padEnd01[(size_t) base]   = 0.75f;

    guardarKit (nombre);

    const auto dir = ProjectStore::kits().getChildFile (juce::File::createLegalFileName (nombre));
    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.wav");
    files.sort();

    //  Y SE LEE DE VUELTA LO QUE SE ESCRIBIO, que es lo que el tamaño no puede
    //  decir. Un fichero del largo correcto sacado del sitio equivocado pesa
    //  exactamente lo mismo: la primera version de esta prueba comparaba bytes
    //  y dio OK con el recorte ignorado a proposito, porque copiar desde cero
    //  cambia QUE muestras se escriben y no CUANTAS.
    //
    //  Dos numeros que vienen por caminos distintos - uno del disco y otro de
    //  la memoria - y que solo coinciden si el trozo salio de donde tenia que
    //  salir. Compararlos no es circular: el break los separa.
    double rmsDisco = 0.0, rmsEsperado = 0.0;
    if (! files.isEmpty())
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        if (std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (files[0])); rd != nullptr)
        {
            juce::AudioBuffer<float> leido ((int) rd->numChannels, (int) rd->lengthInSamples);
            rd->read (&leido, 0, (int) rd->lengthInSamples, 0, true, true);
            rmsDisco = leido.getRMSLevel (0, 0, leido.getNumSamples());
        }
        if (const auto sb0 = uiSample[(size_t) base]; sb0 != nullptr)
        {
            const int tot = sb0->buffer.getNumSamples();
            const int de  = juce::jlimit (0, tot - 1, (int) (0.25f * (float) tot));
            const int ha  = juce::jlimit (de + 1, tot, (int) (0.75f * (float) tot));
            rmsEsperado = sb0->buffer.getRMSLevel (0, de, ha - de);
        }
    }

    std::cout << "{\"kit\":\"" << nombre << "\",\"carpeta\":\"" << dir.getFullPathName()
              << "\",\"ficheros\":" << files.size() << ",\"lista\":[";
    for (int i = 0; i < files.size(); ++i)
    {
        //  Nombre y TAMANO: un WAV de cabecera sola existe, se lista y no suena.
        std::cout << (i ? "," : "") << "{\"n\":\"" << files[i].getFileName()
                  << "\",\"bytes\":" << files[i].getSize() << "}";
    }
    //  Y cuanto dura la fuente del primer pad, para poder comparar: el trozo
    //  escrito tiene que ser la mitad de eso y no todo.
    const auto sb = uiSample[(size_t) base];
    std::cout << "],\"fuente_muestras\":" << (sb != nullptr ? sb->buffer.getNumSamples() : 0)
              << ",\"fuente_hz\":" << (sb != nullptr ? sb->sourceSampleRate : 0.0)
              //  Y CUANTOS CANALES, que la prueba no puede adivinar. Los
              //  probaba los dos hasta que alguno cuadrara, y asi el fichero
              //  ENTERO en mono colaba como "la mitad en estereo": una regla
              //  que acierta por accidente no es una regla.
              << ",\"fuente_canales\":" << (sb != nullptr ? sb->buffer.getNumChannels() : 0)
              << ",\"rms_disco\":" << rmsDisco
              << ",\"rms_esperado\":" << rmsEsperado
              << "}" << std::endl;
}

//  LAS SEIS OPERACIONES DE ARREGLO, sobre entradas conocidas.
//
//  Cada bloque monta un estado que se puede escribir en una linea, ejecuta
//  UNA operacion y saca lo que quedo. El juicio lo hace Tests/arr.py, que es
//  donde estan escritas las respuestas: aqui no se comprueba nada, se MIDE -
//  una prueba que se juzga a si misma dentro del codigo que prueba tiende a
//  cambiar de opinion a la vez que el fallo.
//  CON QUE ABRE LA MAQUINA, medido en sus dos caminos.
//
//  Un proyecto vacio no es un proyecto sin nada: es el que sale al instalar y
//  el que sale al pulsar NUEVO, y los dos tienen que decir lo mismo. Se vuelca
//  lo que define ese estado -cuantos pads traen sonido, que hay en la linea de
//  tiempo y cuanto mandan los 64 pads a los seis efectos- primero tal y como
//  la app acaba de arrancar y despues de NUEVO.
//
//  Las dos veces, porque son dos codigos distintos: restoreSession sin sesion
//  por un lado y newProject por otro, y ya se ha pagado una vez que uno de los
//  dos se dejara la mitad del estado sin tocar.
//  DIECISEIS NIVELES, MEDIDO POR LA MISMA CUENTA QUE DISPARA. Ver disparoDe:
//  el banco pregunta lo que el dedo hace, no una copia de la cuenta.
void MainComponent::auditNiveles()
{
    auto linea = [this] (const char* modo)
    {
        std::cout << "{\"niveles\":\"" << modo << "\",\"pads\":[";
        for (int i = 0; i < 32; ++i)
            std::cout << (i ? "," : "") << disparoDe (i).pad;
        std::cout << "],\"vels\":[";
        for (int i = 0; i < 32; ++i)
            std::cout << (i ? "," : "") << juce::String (disparoDe (i).vel, 4);
        std::cout << "]}" << std::endl;
    };

    nivel16 = false;
    linea ("off");

    //  Se enciende POR LA TAPA, que es como se enciende de verdad: llamar a
    //  nivel16 = true a mano se saltaria justo la linea que captura el pad, que
    //  es la mitad de la funcion.
    selectedPad = 21;
    nivelesButton.setToggleState (true, juce::sendNotificationSync);
    linea ("on");

    //  Y cambiar de pad elegido NO mueve el destino: se capturo al encender.
    selectedPad = 3;
    linea ("tras cambiar de pad");

    nivelesButton.setToggleState (false, juce::sendNotificationSync);
    linea ("apagado");

    // ------------------------------------------------------------------
    //  EL GOLPE SALE AL APOYAR EL DEDO, y esto no lo medi­a nadie.
    //
    //  Un pad de percusion disparaba por `onClick`, que en JUCE llega al
    //  SOLTAR, asi que cada golpe llevaba encima el tiempo que el dedo pasara
    //  sobre el pad. No hay medida de latencia que lo vea -la sonda del
    //  microfono empieza a contar cuando la app EMITE- porque el retraso esta
    //  antes, entre el dedo y la app.
    //
    //  Se mide por el camino de verdad: se construye un `MouseEvent` y se
    //  llama a `PadButton::mouseDown`, que es donde vive el gesto. Llamar a
    //  `padClicked` por dentro se salta justo la linea que decide CUANDO.
    //  Y con las DOS mitades: que suene al apoyar Y que no vuelva a sonar al
    //  levantar, porque «suena antes» lo cumple tambien un pad que dispara dos
    //  veces.
    {
        //  Un pad con sonido y el motor listo, que en un escritorio sin
        //  tarjeta no hay aparato y nadie ha llamado a prepareToPlay.
        engine.prepareToPlay (48000.0, 128);
        juce::AudioBuffer<float> b (2, 128);

        auto vivas = [&]
        {
            b.clear();
            engine.renderNextBlock (b, 0, 128);
            return engine.getActiveVoiceCount();
        };

        //  SIN AUTOCORTE, que es lo que separa las dos formas de fallar: con
        //  el puesto -que es como nace un pad- un segundo disparo se come al
        //  primero y la cuenta de voces sale 1 igual, o sea que «1 y 1»
        //  pasaria tanto con el arreglo como con un pad que dispara dos veces.
        const int idx = 0;
        engine.setPadSelfCut (idx, false);
        auto* pad = pads[idx];
        const auto punto = juce::Point<float> ((float) (pad->getWidth() / 2),
                                               (float) (pad->getHeight() / 2));
        const auto ahora = juce::Time::getCurrentTime();
        juce::MouseEvent ev (juce::Desktop::getInstance().getMainMouseSource(),
                             punto, juce::ModifierKeys(), 1.0f,
                             0.0f, 0.0f, 0.0f, 0.0f,
                             pad, pad, ahora, punto, ahora, 1, false);

        engine.postPanic();
        vivas();
        pad->mouseDown (ev);
        const int alApoyar = vivas();
        pad->mouseUp (ev);
        const int alLevantar = vivas();

        std::cout << "{\"niveles\":\"golpe\",\"al_apoyar\":" << alApoyar
                  << ",\"al_levantar\":" << alLevantar << "}" << std::endl;
    }
}

void MainComponent::auditNuevo()
{
    auto fila = [this] (const char* que)
    {
        int conSonido = 0;
        for (int i = 0; i < kNumPads; ++i) if (padHasSample[(size_t) i]) ++conSonido;

        float envMax = 0.0f;
        double envSuma = 0.0;
        for (int i = 0; i < kNumPads; ++i)
            for (int f = 0; f < AudioEngine::kNumFx; ++f)
            {
                const float v = engine.getPadSend (i, f);
                envMax = juce::jmax (envMax, v);
                envSuma += v;
            }

        std::cout << "{\"nuevo\":\"" << que << "\",\"pads\":" << conSonido
                  << ",\"envmax\":" << envMax << ",\"envsuma\":" << envSuma
                  << ",\"largo\":" << engine.getSongLength() << ",\"carriles\":[";
        for (int ln = 0; ln < AudioEngine::kSongLanes; ++ln)
        {
            std::cout << (ln ? "," : "") << "[";
            for (int b = 0; b < engine.getSongLength(); ++b)
                std::cout << (b ? "," : "") << engine.getSongCell (ln, b);
            std::cout << "]";
        }
        std::cout << "]}" << std::endl;
    };

    fila ("arranque");
    newProject();
    fila ("nuevo");
}

void MainComponent::auditOpen (const juce::String& which)
{
    //  Let the bench ask the ENGINE what it is holding, not just the tile.
    //  A pad that looks loaded and is silent is the failure this whole round
    //  was about, and a dump that only reports the tile cannot see it.
    UiAudit::engineLength = [this] (int pad) { return engine.hasSampleFor (pad) ? 1 : 0; };

    //  Y de que pad sale el audio de cada uno, que es lo unico que dice si un
    //  troceado sigue siendo un troceado despues de guardar y volver.
    UiAudit::stepOn = [this] (int pad)
    {
        return juce::isPositiveAndBelow (pad, kNumPads) && pattern[0][0][(size_t) pad];
    };

    UiAudit::padSource = [this] (int pad) -> int
    {
        if (! juce::isPositiveAndBelow (pad, kNumPads) || uiSample[(size_t) pad] == nullptr) return -1;
        for (int j = 0; j < pad; ++j)
            if (uiSample[(size_t) j] == uiSample[(size_t) pad]) return j;
        return pad;
    };

    if (which.isEmpty()) return;

    if      (which == "pads") { showPadPage (padPageSound); openSheet (padSheet, padsButton); }
    else if (which == "pad2") { showPadPage (padPageTrim);  openSheet (padSheet, padsButton); }
    else if (which == "pad3") { showPadPage (padPageRig);   openSheet (padSheet, padsButton); }
    else if (which == "sec")  { showSeqPage (seqPageGrid); openSheet (seqSheet, secButton); }
    else if (which == "xy")   { closeAllSheets(); toggleXyPanel(); }
    else if (which == "paso") { showSeqPage (seqPageStep); openSheet (seqSheet, secButton); }
    //  PASOS CON UN PASO TOCADO, que es otro estado y no el mismo: la tira de
    //  mandos de debajo de la rejilla solo existe cuando hay paso elegido, asi
    //  que sin esta entrada el banco medía la pagina a la que le falta justo lo
    //  que se acaba de anadir.
    else if (which == "secp") { showSeqPage (seqPageGrid); openSheet (seqSheet, secButton); stepCellToggled (0, 4); }
    else if (which == "song") openSheet (songSheet, songButton);
    else if (which == "piano") { openSheet (seqSheet, secButton); showSeqPage (seqPagePiano); refreshPiano(); }
    //  EL PIANO CON NOTAS DE LARGOS DISTINTOS, que es otro estado: la barra de
    //  una nota de dos pasos y la de un cuarto de paso no se dibujan igual, y
    //  sin escribirlas el banco mide una rejilla vacia.
    else if (which == "pianod")
    {
        selectedPattern = 0;
        engine.setPatternLength (0, 16);
        selectPad (0);
        const struct { int paso, semi, cuartos; } melodia[5] =
            { { 0, 0, 8 }, { 4, 4, 2 }, { 6, 7, 4 }, { 8, 12, 16 }, { 13, -5, 1 } };
        for (auto& n : melodia)
        {
            pattern[0][(size_t) n.paso][(size_t) selectedPad] = true;
            engine.setStep     (0, n.paso, selectedPad, true);
            engine.setStepNote (0, n.paso, selectedPad, n.semi);
            engine.setStepLen  (0, n.paso, selectedPad, n.cuartos);
        }
        engine.setStepExtra (0, 0, selectedPad, 0, 4, true);   // y un acorde en la primera
        engine.setStepExtra (0, 0, selectedPad, 1, 7, true);
        openSheet (seqSheet, secButton); showSeqPage (seqPagePiano); refreshPiano();
    }
    //  LA REJILLA DE DIECISEIS PARA ELEGIR PAD, que se dibuja ENCIMA de la
    //  ficha del secuenciador y por tanto es un estado propio: sin esta entrada
    //  el banco no la mide nunca, y una capa que se pone sobre otra es
    //  exactamente donde vive el residuo que ZATI_PAGES existe para cazar.
    else if (which == "pick")
    { openSheet (seqSheet, secButton); showSeqPage (seqPagePiano); abrePadPicker (true); }
    else if (which == "mix")  { refreshMixStrip(); openSheet (mixSheet, mixButton); }
    else if (which == "set")  { showSetPage (pageAudio);    refreshAudioOptions(); openSheet (setSheet, setButton); }
    else if (which == "proj") { showSetPage (pageProjects); refreshProjectList(); openSheet (setSheet, setButton); }
    else if (which == "gest") { showSetPage (pageGestures); openSheet (setSheet, setButton); }
    else if (which == "asp")  { showSetPage (pageAspecto);  openSheet (setSheet, setButton); }
    //  CAMBIAR DE IDIOMA CON AJUSTES DELANTE, que es la otra mitad del mismo
    //  fallo que la bienvenida en cada arranque: retranslateUi llamaba a
    //  showTour, showTour pasa por tourPrepara y su caso por defecto empieza
    //  por closeAllSheets. O sea que tocar un idioma cerraba la unica ficha
    //  desde la que se cambia el idioma y dejaba la bienvenida encima.
    //
    //  Se pulsa la tapa DE VERDAD -su onClick- y no se llama a retranslateUi
    //  por dentro, que es justo donde el fallo no existe: quien encadena
    //  Lang::set, retranslateUi y resized es el callback.
    //  EL IDIOMA CAMBIADO EN CALIENTE, ida Y VUELTA.
    //
    //  La primera version pulsaba SIEMPRE el boton 1 -ENGLISH- y con eso la
    //  regla de traduccion, que es COMPARATIVA entre la corrida `es` y la `en`,
    //  saco 44 hallazgos falsos: las dos corridas acababan en ingles, asi que
    //  la misma cadena en la misma ruta del arbol no probaba nada. Primero se
    //  duda de la prueba.
    //
    //  Se va a OTRO idioma y se VUELVE al de la corrida, que es lo unico que
    //  deja el estado final en el idioma que `ZATI_LANG` pidio y por tanto la
    //  comparacion en pie.
    //
    //  Lo que esta entrada anade es la GEOMETRIA despues de un cambio en
    //  caliente: `retranslateUi` vuelve a poner los rotulos y llama a
    //  `resized()`, y en arabe eso mueve los muebles de la ficha y no solo las
    //  palabras. Ningun otro arranque llega a ese estado -los demas maquetan
    //  una vez, en el idioma con el que nacieron- y es el estado en el que se
    //  queda cualquiera que toque un idioma. Lo que NO anade es traduccion: un
    //  control que no se retraduce se queda en el idioma con el que se
    //  construyo, que es el de la corrida, asi que la regla comparativa no
    //  puede verlo por aqui.
    else if (which == "lang")
    {
        showSetPage (pageAspecto);
        openSheet (setSheet, setButton);

        const int mio   = (int) Lang::current();
        const int otro  = (mio == 0 ? 1 : 0);
        auto pulsa = [this] (int i)
        {
            if (juce::isPositiveAndBelow (i, langButtons.size())
                && langButtons[i] != nullptr && langButtons[i]->onClick)
                langButtons[i]->onClick();
        };
        pulsa (otro);
        pulsa (mio);

        std::cout << "{\"idioma\":\"cambiado\",\"paso_por\":" << otro
                  << ",\"quedo\":" << (int) Lang::current()
                  << ",\"ajustes\":" << (setSheet.isVisible() ? 1 : 0)
                  << ",\"tour\":" << (tourSheet.isVisible() ? 1 : 0) << "}" << std::endl;
    }
    else if (which == "midi") { showSetPage (pageMidi); refreshMidiDevices(); openSheet (setSheet, setButton); }
    else if (which == "rack") { rackPad = 0; openSheet (rackSheet, mixButton); refreshRack(); }
    //  LA FICHA DE INSTRUMENTOS, en sus dos estados: con el pack de dentro
    //  -cuatro tapas de cuatro- y con uno de disco lleno y CERRADO, que es
    //  donde los rotulos llevan el candado delante y por tanto miden otra cosa.
    else if (which == "inst")  { instPack = 0; openInstSheet(); }
    //  Y EL SEGUNDO NIVEL, que es una ficha distinta aunque comparta tapas: la
    //  fila del pack cambia de contenido -VOLVER en vez de menos y mas-, el
    //  renglon dice el instrumento y las dieciseis tapas llevan otros rotulos.
    //  Sin esta entrada se medira siempre la lista de fuera, que es justo la
    //  que no cambio. Es lo mismo que hizo falta con secp.
    //  Y LA FICHA DEL INSTRUMENTO, que solo existe si el pad lleva uno: sin
    //  plantarlo antes, el banco mediria una ficha vacia.
    //  LA REJILLA, con un destino MOVIDO y un pad ya ocupado: sin las dos
    //  cosas se mediria una rejilla vacia y con el destino de fabrica, que es
    //  justo el estado que no hay que medir. Lo mismo que hizo falta con secp.
    else if (which == "instg")
    {
        ponInstrumentoEnPad (kBancoInstr * kPadsPerBank + 6, 11, 0);   // CUERDA PULS
        instPack = 0;
        openInstSheet();
        instDestPad = kBancoInstr * kPadsPerBank + 9;
        refreshInst();
        resized();

        //  Y LO QUE EL MENU DEJA ESCRITO, medido donde se decide.
        //
        //  Se cuenta lo que se PINTA -reparteTapa, la misma funcion con la que
        //  se dibuja- y no lo que la tapa tiene guardado: el rotulo puede
        //  estar puesto y no salir, que es como dieciseis celdas estuvieron
        //  mudas una tanda entera sin que ninguna regla lo dijera. Es lo mismo
        //  que ya hizo falta con captionOf: lo que se mide es lo que se pinta.
        //  Y las DOS cifras, que es lo unico que separa los dos arreglos que
        //  parecen uno: solo el nombre lo cumple una lista sin dibujos, y solo
        //  el dibujo lo cumplia la rejilla muda que motivo todo esto.
        int conNombre = 0, conDibujo = 0;
        for (auto* b : instBtns)
        {
            if (b == nullptr || ! b->isVisible() || b->getButtonText().isEmpty()) continue;
            const auto r = ZatiLookAndFeel::reparteTapa (*b);
            if (! r.texto.isEmpty())               ++conNombre;
            if (r.id != Iconos::Id::ninguno)       ++conDibujo;
        }
        std::cout << "{\"inst\":\"menu\",\"conNombre\":" << conNombre
                  << ",\"conDibujo\":" << conDibujo
                  << ",\"celdaW\":" << (instBtns.isEmpty() ? 0 : instBtns[0]->getWidth())
                  << ",\"celdaH\":" << (instBtns.isEmpty() ? 0 : instBtns[0]->getHeight())
                  << ",\"pieAbajo\":" << instPieArea.getBottom()
                  << ",\"cuerpo\":" << instSheet.cuerpo.getHeight() << "}" << std::endl;
    }
    else if (which == "vst")
    {
        //  CUERDA PULS, que es el nombre de familia mas largo de los dieciseis.
        const int pad = kBancoInstr * kPadsPerBank + 11;
        ponInstrumentoEnPad (pad, 11, 0);
        selectBank (kBancoInstr);
        selectPad (pad);
        abreVst();
    }
    else if (which == "instd")
    {
        //  Y ESTE PLANTA LOS PACKS ANTES DE ABRIR. Sin eso el catalogo de una
        //  corrida limpia es solo el de dentro y las dos entradas medirian la
        //  misma ficha - cuatro tapas de cuatro, sin candado y sin rotulos
        //  largos, que es exactamente lo que hay que medir aqui.
        plantaPacksDePrueba();
        openInstSheet();
        instPack = juce::jmax (0, (int) instCatalogo.size() - 1);
        refreshInst();
        resized();
    }
    else if (which == "chop")
    {
        //  ZATI_CHOP=golpes abre la ficha en el otro modo. Sin esto el banco
        //  solo puede fotografiar la mitad de la ficha, que es como no medirla:
        //  el modo cambia dos rotulos y una linea del plan.
        chopByHits = UiAudit::env ("ZATI_CHOP") == "golpes";
        openChopSheet();
    }
    //  LA FICHA DE EXPORTAR, que es la unica que el banco no abria nunca - y es
    //  la unica funcion de la app cuyo resultado sale del telefono.
    else if (which == "expo") { exportStatus.clear(); exportOk = false; destinoCache = juce::File();
                                openSheet (exportSheet, setButton); }
    else if (which == "manual") { closeAllSheets(); openSheet (manualSheet, setButton); }
    //  EL TOUR, en su primera tarjeta y en la ultima: la fila de tapas cambia
    //  -ATRAS se enciende, SIGUIENTE pasa a EMPEZAR- y "EMPEZAR" no mide lo
    //  mismo, asi que medir solo la primera es medir media ficha.
    //  ZATI_TOUR=n abre el tour por el paso n. Son quince estados distintos -cada
    //  uno abre una ficha y pone el muelle en un lado- y sin esta entrada el
    //  banco mediria quince veces el primero.
    else if (which == "tour")
    {
        closeAllSheets();
        const auto n = UiAudit::env ("ZATI_TOUR");
        showTour (n.isNotEmpty() ? n.getIntValue() : 0);
        openSheet (tourSheet, setButton);
        showTour (n.isNotEmpty() ? n.getIntValue() : 0);   // openSheet cierra: se repite tras el
    }
    //  Y "tourN" para el paso N, que es como el banco pide los quince sin
    //  quince variables de entorno.
    else if (which.startsWith ("tour") && which.substring (4).containsOnly ("0123456789"))
    {
        closeAllSheets();
        const int n = which.substring (4).getIntValue();
        showTour (n);
        openSheet (tourSheet, setButton);
        showTour (n);
    }
    else if (which == "tourf") { closeAllSheets(); showTour (kTourPasos - 1); openSheet (tourSheet, setButton); }
    else if (which == "browse") openBrowseForPad (0);
    //  EL MISMO NAVEGADOR ELIGIENDO CARPETA, que es OTRO estado y no el mismo:
    //  la fila de acciones cambia de cuatro tapas a una, y una fila que solo
    //  existe en un modo es una fila que el banco no mide si no se la pide.
    else if (which == "browsedir") openBrowseForExportDir();
}

//  EL PIANO ROLL, MEDIDO.
//
//  Tres promesas y ninguna se ve en una captura: que el acorde que se escribe
//  en la rejilla es el que queda en (patron, paso, pad) del motor, que la ficha
//  cambia de pad sin cerrarse y saltando los vacios, y que tocar el teclado
//  SUENA sin afinar el pad. La tercera es la que estaba rota - la unica forma
//  de oir un semitono era setPadPitch, que deja el pad afinado en la ultima
//  tecla que se paseo - y es exactamente la que no se ve mirando la pantalla.
void MainComponent::auditPiano()
{
    selectedPattern = 0;
    engine.clearPattern (0);
    for (int st = 0; st < kNumSteps; ++st)
        for (int p = 0; p < kNumPads; ++p)
            pattern[0][(size_t) st][(size_t) p] = false;
    engine.setPatternLength (0, 16);

    //  Un kit escaso a proposito: 0, 5 y 33. El 33 esta en el banco C, asi que
    //  avanzar hasta el tiene que arrastrar tambien la vista de bancos.
    padHasSample.fill (false);
    for (int p : { 0, 5, 33 }) padHasSample[(size_t) p] = true;
    selectBank (0);
    selectPad (0);

    auto notasDe = [this] (int paso, int p)
    {
        juce::String s = "[";
        bool first = true;
        if (pattern[0][(size_t) paso][(size_t) p])
        {
            s << engine.getStepNote (0, paso, p); first = false;
        }
        for (int e = 0; e < AudioEngine::kExtraNotes; ++e)
            if (const int v = engine.getStepExtra (0, paso, p, e); v != -128)
            { s << (first ? "" : ",") << v; first = false; }
        return s + "]";
    };

    //  UN ACORDE, por la rejilla y no por la API: 0, 4 y 7 es una triada mayor
    //  y las tres notas tienen que caber en el mismo paso.
    for (int semi : { 0, 4, 7 }) pianoCellToggled (4, semi);
    std::cout << "{\"piano\":\"acorde\",\"pad\":" << selectedPad
              << ",\"notas\":" << notasDe (4, selectedPad) << "}" << std::endl;

    //  QUITAR LA RAIZ no puede dejar el acorde huerfano: asciende la siguiente.
    pianoCellToggled (4, 0);
    std::cout << "{\"piano\":\"sin raiz\",\"notas\":" << notasDe (4, selectedPad) << "}" << std::endl;

    //  CAMBIAR DE PAD CON LA FICHA ABIERTA, saltando los sesenta huecos.
    showSeqPage (seqPagePiano);
    pianoStepPad (1);
    const int tras1 = selectedPad;
    pianoStepPad (1);
    const int tras2 = selectedPad, banco2 = currentBank;
    pianoStepPad (-1);
    const int atras = selectedPad;

    //  Y que la rejilla ensene las notas del pad NUEVO: se escribe una sola
    //  nota en el 5 y se vuelve al 0, que lleva el acorde.
    std::cout << "{\"piano\":\"pads\",\"tras1\":" << tras1 << ",\"tras2\":" << tras2
              << ",\"banco2\":" << banco2 << ",\"atras\":" << atras << "}" << std::endl;

    //  EUCLIDES: cinco golpes en dieciseis tienen que salir en 0 4 7 10 13, que
    //  es la clave de tresillo de toda la vida. Se mide por el camino que usa
    //  la persona -el mando- y no llamando a la funcion por dentro.
    selectPad (0);
    engine.setPatternLength (0, 16);
    for (int n : { 5, 4, 3 })
    {
        euclidSlider.setValue ((double) n, juce::sendNotificationSync);
        std::cout << "{\"piano\":\"euclides\",\"golpes\":" << n << ",\"pasos\":[";
        bool primero = true;
        for (int st = 0; st < 16; ++st)
            if (pattern[0][(size_t) st][(size_t) selectedPad])
            { std::cout << (primero ? "" : ",") << st; primero = false; }
        std::cout << "]}" << std::endl;
    }

    //  OIR UNA TECLA NO AFINA EL PAD. Se apunta lo que tenia, se pasean doce
    //  semitonos y se vuelve a leer: si cambio, el paseo ha reafinado el pad.
    selectPad (0);
    engine.setPadPitch (0, 3.0f);
    const float antes = engine.getPadPitch (0);
    for (int semi = -12; semi <= 12; ++semi)
        if (pianoGrid.onTecla) pianoGrid.onTecla (semi);
    std::cout << "{\"piano\":\"teclado\",\"pitch_antes\":" << antes
              << ",\"pitch_despues\":" << engine.getPadPitch (0) << "}" << std::endl;

    //  EL COMPAS, que es donde esta pagina fallo CINCO veces seguidas y ninguna
    //  de las seis reglas del banco podia verlo: son fallos de INDICE y no de
    //  geometria. Un piano que escribe en el compas de al lado se maqueta
    //  perfecto, no solapa nada, no corta ningun rotulo y esta traducido.
    //
    //  Los cinco, por orden de aparicion: onCelda pasaba la COLUMNA donde va el
    //  paso; la tapa de compas solo refrescaba la rejilla de pasos; refreshPiano
    //  vaciaba las celdas DENTRO del bucle de columnas validas; el jmax(1,...)
    //  daba una columna donde tocaban cero; y la goma seguia pasando la columna
    //  despues de que onCelda ya estuviera arreglada. Los cinco dan el mismo
    //  sintoma - "la rejilla no responde" - y por eso se midan aqui juntos.
    selectPad (0);
    engine.clearPattern (0);
    for (int st = 0; st < kNumSteps; ++st)
        for (int p = 0; p < kNumPads; ++p)
            pattern[0][(size_t) st][(size_t) p] = false;
    engine.setPatternLength (0, 32);          // dos compases
    showSeqPage (seqPagePiano);
    resized();

    //  Una nota testigo en el compas 0, misma columna: es la que delata que se
    //  escribe o se borra en el compas equivocado.
    if (pianoGrid.onCelda) pianoGrid.onCelda (3, 9);

    //  Y ahora al compas 1, por la tapa y no moviendo selectedBar a mano.
    if (barButtons.size() > 1 && barButtons[1]->onClick) barButtons[1]->onClick();
    if (pianoGrid.onCelda) pianoGrid.onCelda (3, 5);

    std::cout << "{\"piano\":\"compas\",\"sel\":" << selectedBar
              << ",\"paso19\":" << (pattern[0][19][0] ? 1 : 0)
              << ",\"nota19\":" << engine.getStepNote (0, 19, 0)
              << ",\"paso3\":" << (pattern[0][3][0] ? 1 : 0)
              << ",\"nota3\":" << engine.getStepNote (0, 3, 0) << "}" << std::endl;

    //  LA VISTA. La tapa de compas tiene que haber repintado el piano, asi que
    //  la columna 3 lleva el 5 del compas 1 y no el 9 del 0.
    std::cout << "{\"piano\":\"vista\",\"col3\":" << (int) pianoCells[3 * PianoRoll::kMaxNotas]
              << "}" << std::endl;

    //  LA GOMA, montada APARTE y con el paso absoluto en vez de con el gesto.
    //
    //  Si se apoyara en lo que acaba de escribir onCelda, romper el fallo 1
    //  dejaria la nota sin poner y la goma saldria "bien" por no tener nada que
    //  borrar. Una comprobacion que pasa porque la de al lado fallo no mide
    //  nada, y eso se descubrio validando esta: con los cinco fallos puestos a
    //  la vez, la goma era el unico que salia verde.
    engine.clearPattern (0);
    for (int st = 0; st < kNumSteps; ++st)
        for (int p = 0; p < kNumPads; ++p)
            pattern[0][(size_t) st][(size_t) p] = false;
    pianoCellToggled (3, 9);      // testigo, compas 0
    pianoCellToggled (19, 5);     // el que se frota, compas 1
    refreshPiano();
    if (pianoGrid.onBorrar) pianoGrid.onBorrar (3, 5);
    std::cout << "{\"piano\":\"goma\",\"paso19\":" << (pattern[0][19][0] ? 1 : 0)
              << ",\"paso3\":" << (pattern[0][3][0] ? 1 : 0) << "}" << std::endl;

    //  Y UN PATRON QUE ENCOGE con el compas 1 puesto: el compas se acota en
    //  refreshPiano -que corre treinta veces por segundo- y no solo en resized.
    //  Sin eso `base` apunta fuera de la tabla y quedan quince columnas viejas.
    engine.setPatternLength (0, 16);
    refreshPiano();
    int viejas = 0;
    for (int c = 0; c < AudioEngine::kBarSteps; ++c)
        for (int k = 0; k < PianoRoll::kMaxNotas; ++k)
            if (pianoCells[c * PianoRoll::kMaxNotas + k] != -128) ++viejas;
    std::cout << "{\"piano\":\"encoge\",\"sel\":" << selectedBar
              << ",\"puestas\":" << viejas
              << ",\"col3\":" << (int) pianoCells[3 * PianoRoll::kMaxNotas] << "}" << std::endl;

    // ------------------------------------------------------------------
    //  EL ARRASTRE NO CAMBIA DE FILA, Y EL LAPIZ NO BORRA.
    //
    //  Las dos se miden por el GESTO en pixeles -pianoGrid.gesto- y no por
    //  onCelda, que es el callback: llamar al callback salta justo el codigo
    //  que decide que celda es, o sea el sitio donde vive el fallo. Es la misma
    //  leccion que los cinco del compas, contada desde el otro lado.
    //
    //  El fallo: bajar el dedo por la rejilla dejaba una nota en CADA fila por
    //  la que pasaba, porque estaba escrito que cambiar de fila arrastrando es
    //  "escribir un acorde". Un acorde son dos toques; esto era una escalera
    //  que nadie pidio.
    selectPad (0);
    engine.setPatternLength (0, 16);
    engine.clearPattern (0);
    for (int st = 0; st < kNumSteps; ++st)
        for (int p = 0; p < kNumPads; ++p)
            pattern[0][(size_t) st][(size_t) p] = false;
    selectedBar = 0;
    showSeqPage (seqPagePiano);
    resized();
    refreshPiano();

    //  El punto de una celda, en coordenadas de la rejilla. La canaleta del
    //  teclado se salta a proposito: ahi el gesto suena y no escribe.
    const int filas = pianoGrid.getFilas();
    const float altoFila = (float) pianoGrid.getHeight() / (float) juce::jmax (1, filas);
    const float anchoCol = (float) (pianoGrid.getWidth() - PianoRoll::kGutter)
                             / (float) AudioEngine::kBarSteps;
    auto punto = [&] (int col, int fila, float& x, float& y)
    {
        x = (float) PianoRoll::kGutter + ((float) col + 0.5f) * anchoCol;
        y = ((float) fila + 0.5f) * altoFila;
    };

    auto notasEnPaso = [this] (int st)
    {
        int n = pattern[0][(size_t) st][(size_t) selectedPad] ? 1 : 0;
        for (int e = 0; e < AudioEngine::kExtraNotes; ++e)
            if (engine.getStepExtra (0, st, selectedPad, e) != -128) ++n;
        return n;
    };

    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    punto (2, filas / 2,     x0, y0);
    punto (2, filas / 2 + 3, x1, y1);
    pianoGrid.gesto (x0, y0, false);
    pianoGrid.gesto (x0, (y0 + y1) * 0.5f, true);
    pianoGrid.gesto (x1, y1, true);
    pianoGrid.suelta();

    //  Cuantas filas quedaron escritas en esa columna: una. Y cuantas casillas
    //  se encendieron en TODA la rejilla, que es la otra mitad - un arrastre
    //  horizontal que se pierde escribiria en otra columna.
    int filasEscritas = notasEnPaso (2), pasosPuestos = 0;
    for (int st = 0; st < 16; ++st) if (pattern[0][(size_t) st][(size_t) selectedPad]) ++pasosPuestos;
    std::cout << "{\"piano\":\"arrastre\",\"filas\":" << filasEscritas
              << ",\"pasos\":" << pasosPuestos << "}" << std::endl;

    //  EL LAPIZ: tocar encima de una nota que ya esta NO la quita. Con la
    //  herramienta apagada -que es como sigue naciendo la pagina- el mismo
    //  toque la alterna, que es lo corto para corregir una nota suelta.
    pianoGrid.setHerramienta (PianoRoll::lapiz);
    refreshPiano();
    pianoGrid.gesto (x0, y0, false);
    pianoGrid.suelta();
    const int conLapiz = notasEnPaso (2);

    pianoGrid.setHerramienta (PianoRoll::dibujar);
    refreshPiano();
    pianoGrid.gesto (x0, y0, false);
    pianoGrid.suelta();
    const int alternando = notasEnPaso (2);

    std::cout << "{\"piano\":\"lapiz\",\"con\":" << conLapiz
              << ",\"sin\":" << alternando << "}" << std::endl;

    // ------------------------------------------------------------------
    //  EL MODO: UN ESTADO Y TRES TAPAS.
    //
    //  La cara, la ficha de la cancion y la del secuenciador tienen cada una su
    //  fila de transporte, y las tres llevan el mismo interruptor - como ya
    //  pasa con PLAY. Lo que puede pudrirse en silencio no es el motor sino la
    //  SINCRONIA: tocar una y que las otras dos sigan diciendo lo contrario se
    //  lee como que el aparato no se ha enterado.
    //
    //  Se mide por el camino de verdad -el onClick de la tapa, con su
    //  toggle puesto como lo pondria el dedo- y no llamando a ponModoCancion.
    auto pulsa = [this] (juce::TextButton& b, bool on)
    {
        b.setToggleState (on, juce::dontSendNotification);
        if (b.onClick) b.onClick();
        std::cout << "{\"piano\":\"modo\",\"motor\":" << (engine.isSongMode() ? 1 : 0)
                  << ",\"cara\":"   << (modoBtn.getToggleState()     ? 1 : 0)
                  << ",\"sec\":"    << (seqModoBtn.getToggleState()  ? 1 : 0)
                  << ",\"cancion\":"<< (songModeBtn.getToggleState() ? 1 : 0) << "}"
                  << std::endl;
    };
    pulsa (seqModoBtn, true);      // desde la ficha del secuenciador
    pulsa (modoBtn,    false);     // y de vuelta desde la cara

    // ------------------------------------------------------------------
    //  Y EL TRANSPORTE, QUE ES LA MISMA PREGUNTA Y NO LA MEDIA NADIE.
    //
    //  El comentario de `ponModoCancion` daba por hecho que PLAY ya tenia
    //  embudo -«playButton, songPlayBtn y seqPlayBtn son tres tapas de un
    //  estado»- y no lo tenia: eran DIEZ escritores sincronizando cada uno las
    //  tapas que se acordaba. Se mide por los tres caminos que un dedo puede
    //  tomar, mas los dos que no son un dedo y son los que peor estaban: armar
    //  REC con el transporte parado -que lo arranca- y volver de una llamada,
    //  que ponia el toggle y no el ROTULO, asi que la cara decia PLAY con la
    //  maquina rodando. Por eso se publica tambien el rotulo y no solo el
    //  estado: un boton que dice PLAY corriendo pasa cualquier prueba que solo
    //  mire toggles.
    auto transporteAhora = [this] (const char* quien)
    {
        std::cout << "{\"piano\":\"transporte\",\"como\":\"" << quien
                  << "\",\"motor\":" << (engine.isPlaying()           ? 1 : 0)
                  << ",\"cara\":"    << (playButton.getToggleState()  ? 1 : 0)
                  << ",\"sec\":"     << (seqPlayBtn.getToggleState()  ? 1 : 0)
                  << ",\"cancion\":" << (songPlayBtn.getToggleState() ? 1 : 0)
                  << ",\"rotulos\":[\"" << playButton.getButtonText()
                  << "\",\"" << seqPlayBtn.getButtonText()
                  << "\",\"" << songPlayBtn.getButtonText() << "\"]}"
                  << std::endl;
    };
    auto pulsaPlay = [this, &transporteAhora] (juce::TextButton& b, bool on, const char* quien)
    {
        b.setToggleState (on, juce::dontSendNotification);
        if (b.onClick) b.onClick();
        transporteAhora (quien);
    };
    pulsaPlay (seqPlayBtn,  true,  "sec");
    pulsaPlay (playButton,  false, "cara");
    pulsaPlay (songPlayBtn, true,  "cancion");
    pulsaPlay (playButton,  false, "cara");

    //  Y los dos que no son un dedo.
    if (! recArmed) toggleRecordArm();          // arma REC: arranca el transporte
    transporteAhora ("rec");
    if (recArmed) toggleRecordArm();
    ponTransporte (false);

    //  Y una llamada entrando, que es el camino que peor estaba: paraba el
    //  motor con `setToggleState` y sin `transporte`, asi que las tres tapas
    //  se quedaban diciendo STOP con la maquina parada.
    ponTransporte (true);
    audioFocusLost (false);
    transporteAhora ("llamada");
    ponTransporte (false);

    //  TOCAR UN PAD QUE ASOMA POR DEBAJO DE LA FICHA.
    //
    //  La tarjeta se centra al 78 % PARA QUE la maquina se siga viendo, y se
    //  veia y no se podia tocar: cualquier toque fuera de la tarjeta cerraba la
    //  ficha, asi que cambiar el pad que edita el secuenciador costaba cerrar,
    //  elegir y volver a abrir con la rejilla de pads delante todo el rato.
    //
    //  Se mide por el CAMINO DE VERDAD - Sheet::mouseDown, que es donde vive el
    //  fallo - y no llamando a tocaPadDetras por dentro, que es justo el sitio
    //  donde el fallo no existe. Y con las DOS cifras: que el pad cambie Y que
    //  la ficha siga abierta. Solo con la primera pasaria un codigo que
    //  selecciona y ademas cierra.
    {
        openSheet (seqSheet, secButton);
        showSeqPage (seqPagePiano);
        selectPad (0);
        resized();

        //  El pad de la esquina de abajo a la izquierda es el que mas asoma.
        int cual = -1;
        juce::Point<int> punto;
        for (int i = 0; i < pads.size(); ++i)
            if (auto* b = pads[i]; b != nullptr && b->isVisible() && ! b->getBounds().isEmpty())
                if (const auto c = b->getBounds().getCentre(); ! seqSheet.sheetBounds.contains (c))
                { cual = i; punto = c; break; }

        int quedo = -1, sigueAbierta = -1, antes = -1;
        if (cual >= 0)
        {
            //  Y SE PARTE DE OTRO PAD, o la medida no dice nada: si el elegido
            //  ya era ese, "elegido == tocado" sale verde con el toque cayendo
            //  al vacio. Es la misma trampa que el testigo del compas.
            selectPad (cual == 0 ? 5 : 0);
            antes = selectedPad;

            const auto ahora = juce::Time::getCurrentTime();
            juce::MouseEvent ev (juce::Desktop::getInstance().getMainMouseSource(),
                                 punto.toFloat(), juce::ModifierKeys(), 1.0f,
                                 0.0f, 0.0f, 0.0f, 0.0f,
                                 &seqSheet, &seqSheet, ahora,
                                 punto.toFloat(), ahora, 1, false);
            seqSheet.mouseDown (ev);
            quedo = selectedPad;
            sigueAbierta = seqSheet.isVisible() ? 1 : 0;
        }
        std::cout << "{\"piano\":\"detras\",\"pad\":" << cual
                  << ",\"antes\":" << antes
                  << ",\"elegido\":" << quedo
                  << ",\"abierta\":" << sigueAbierta << "}" << std::endl;
    }

    //  Y LA REJILLA DE DIECISEIS, que es lo que las flechas no pueden ser:
    //  llegar al pad 11 de un gesto en vez de pasear. Se abre, se toca su tapa
    //  y se comprueba que ademas se cierra sola - dejarla abierta despues de
    //  acertar es un segundo toque para volver a lo que se estaba haciendo.
    {
        openSheet (seqSheet, secButton);
        showSeqPage (seqPagePiano);
        selectPad (0);
        abrePadPicker (true);
        const int abierta = padPickAbierto ? 1 : 0;
        //  Su alto, para saber que se maqueto: una rejilla encendida y de 0x0
        //  es exactamente lo que la septima regla del banco existe para cazar.
        const auto celda = padPickBtns[10] != nullptr ? padPickBtns[10]->getBounds()
                                                      : juce::Rectangle<int>();
        if (padPickBtns[10] != nullptr && padPickBtns[10]->onClick) padPickBtns[10]->onClick();
        std::cout << "{\"piano\":\"rejilla\",\"abrio\":" << abierta
                  << ",\"celda_w\":" << celda.getWidth() << ",\"celda_h\":" << celda.getHeight()
                  << ",\"elegido\":" << selectedPad
                  << ",\"cerro\":" << (padPickAbierto ? 0 : 1) << "}" << std::endl;
    }
}

//  El mismo camino que el boton, no un atajo al motor: si arrancar la
//  reproduccion desde fuera se saltase el rotulo y el estado de la tapa, la
//  medida de CPU seria la de una cara que no existe.
void MainComponent::auditPlay (bool on)
{
    playButton.setToggleState (on, juce::dontSendNotification);
    if (playButton.onClick) playButton.onClick();
}

void MainComponent::auditProject()
{
    //  Un paso en el pad 0 de cada banco: 0, 16, 32 y 48. Si la mascara de la
    //  cancion vuelve a ser de 32 bits, los dos altos se pierden o salen
    //  clonados de los dos bajos, y esta lista lo dice sin interpretacion.
    const int marcados[4] = { 0, 16, 32, 48 };

    for (int b = 0; b < kNumPatterns; ++b)
        for (int st = 0; st < kNumSteps; ++st)
            for (int p = 0; p < kNumPads; ++p)
            {
                pattern[(size_t) b][(size_t) st][(size_t) p] = false;
                engine.setStep (b, st, p, false);
            }

    for (int p : marcados) { pattern[0][0][(size_t) p] = true; engine.setStep (0, 0, p, true); }
    //  Y uno en el paso 5 del banco D, para que un desplazamiento de paso -y
    //  no solo de pad- tambien se vea.
    pattern[0][5][48] = true; engine.setStep (0, 5, 48, true);

    //  Y LO QUE NO SE GUARDABA: acorde, empujon, bloqueo y largo. Cuatro cosas
    //  que la app sabia escribir y no sabia recordar - un acorde de cuatro
    //  notas volvia siendo una y un patron humanizado volvia recto. Se ponen
    //  valores distintos y reconocibles para que un cruce de campos se vea.
    engine.setStepNote  (0, 0, 0, 7);
    engine.setStepExtra (0, 0, 0, 0, 4, true);
    engine.setStepExtra (0, 0, 0, 1, 12, true);
    engine.setStepNudge (0, 0, 0, -25);
    engine.setStepLock  (0, 0, 0, 33);
    engine.setStepLen   (0, 0, 0, 9);
    //  Y los otros cuatro bloqueos, con cuatro valores distintos entre si: un
    //  cruce de bytes dentro del paquete sale a la vista y no se puede colar
    //  como "el orden da igual".
    engine.setStepPLock (0, 0, 0, AudioEngine::plockAtaque, 11);
    engine.setStepPLock (0, 0, 0, AudioEngine::plockCaida,  22);
    engine.setStepPLock (0, 0, 0, AudioEngine::plockInicio, 44);
    engine.setStepPLock (0, 0, 0, AudioEngine::plockPan,    88);

    saveProject ("BANCO_PRUEBA");
    //  Y SE ESPERA A QUE TERMINE, que es lo que faltaba y por lo que esta
    //  comprobacion salia verde o roja segun lo rapido que fuera el disco.
    //
    //  Guardar esta TROCEADO: `saveProject` corre una tajada de 25 ms y deja
    //  el resto al temporizador, y la cabecera -que es la que llama a
    //  `captureState()`- se escribe AL FINAL a proposito. Aqui no hay bucle de
    //  mensajes, asi que si los 64 ficheros no cabian en esa primera tajada la
    //  captura no llegaba a hacerse nunca... y las lineas de abajo, que vacian
    //  el patron para probar que vuelve, la dejaban vacia si llegaba tarde. En
    //  el runner de CI los 64 WAV caben en 25 ms y salia verde; en una maquina
    //  mas lenta, roja. Una prueba cuyo veredicto depende de la velocidad del
    //  disco no es una prueba. Se bombea lo mismo que bombearia el tic.
    while (padSaveJob != nullptr) stepPadSaveJob();

    for (int b = 0; b < kNumPatterns; ++b)
        for (int st = 0; st < kNumSteps; ++st)
            for (int p = 0; p < kNumPads; ++p)
            {
                pattern[(size_t) b][(size_t) st][(size_t) p] = false;
                engine.setStep (b, st, p, false);
            }

    //  Y lo disperso, borrado a mano: si al volver sigue puesto no es que se
    //  haya guardado, es que nadie lo quito.
    engine.clearStepExtras (0, 0, 0);
    engine.setStepNudge (0, 0, 0, 0);
    engine.setStepLock  (0, 0, 0, AudioEngine::kNoLock);
    engine.setStepLen   (0, 0, 0, AudioEngine::kLenSuelto);
    engine.setStepNote  (0, 0, 0, 0);
    engine.setStepPLockRaw (0, 0, 0, 0);

    loadProject ("BANCO_PRUEBA");
    while (padJob != nullptr) stepPadJob();

    std::cout << "{\"disperso\":1,\"nota\":" << engine.getStepNote (0, 0, 0)
              << ",\"acorde\":[" << engine.getStepExtra (0, 0, 0, 0) << ","
                                  << engine.getStepExtra (0, 0, 0, 1) << ","
                                  << engine.getStepExtra (0, 0, 0, 2) << "]"
              << ",\"empujon\":" << engine.getStepNudge (0, 0, 0)
              << ",\"bloqueo\":" << engine.getStepLock (0, 0, 0)
              << ",\"largo\":" << engine.getStepLen (0, 0, 0)
              << ",\"plock\":[" << engine.getStepPLock (0, 0, 0, AudioEngine::plockAtaque) << ","
                                  << engine.getStepPLock (0, 0, 0, AudioEngine::plockCaida)  << ","
                                  << engine.getStepPLock (0, 0, 0, AudioEngine::plockInicio) << ","
                                  << engine.getStepPLock (0, 0, 0, AudioEngine::plockPan)    << "]"
              << "}" << std::endl;

    std::cout << "{\"proyecto\":1,\"paso0\":[";
    bool first = true;
    for (int p = 0; p < kNumPads; ++p)
        if (pattern[0][0][(size_t) p]) { std::cout << (first ? "" : ",") << p; first = false; }
    std::cout << "],\"paso5\":[";
    first = true;
    for (int p = 0; p < kNumPads; ++p)
        if (pattern[0][5][(size_t) p]) { std::cout << (first ? "" : ",") << p; first = false; }
    std::cout << "]}" << std::endl;
}

//  UN PROYECTO DE OTRA EPOCA, ABIERTO CON EL BINARIO DE HOY.
//
//  La regla de la casa esta escrita ocho veces en applyState -"lo que no esta
//  en el fichero vale su defecto ANTIGUO y no el de hoy"- y no la comprobaba
//  nadie: no hay un solo project.xml congelado en Tests/, asi que todos los
//  caminos del banco guardan con el binario de hoy y leen con el binario de
//  hoy. Una regla que solo existe en un comentario dura hasta el primer
//  descuido.
//
//  Y de paso mide la herencia por el otro lado, que es lo que se acaba de
//  arreglar: abrir un proyecto SIN <song> dejaba sonando el arreglo del
//  anterior, y uno con dieciseis pads dejaba los otros cuarenta y ocho con la
//  ganancia y el filtro del anterior.
void MainComponent::auditViejos (const juce::String& carpeta)
{
    const juce::File dir (carpeta);
    auto ficheros = dir.findChildFiles (juce::File::findFiles, false, "*.xml");
    ficheros.sort();

    for (const auto& f : ficheros)
    {
        //  ANTES de abrir el viejo se abre uno "de ayer" con todo puesto: sin
        //  eso, los defectos de la maquina recien encendida y los del proyecto
        //  anterior son el mismo numero y la prueba diria que si a cualquier
        //  cosa. Es la corrida de control de Tests/nuevo.py contada aqui.
        for (int p = 0; p < kNumPads; ++p)
        {
            padGain[(size_t) p] = 0.2f;   engine.setPadGain (p, 0.2f);
            padPan[(size_t) p]  = 0.9f;   engine.setPadPan  (p, 0.9f);
            padCut[(size_t) p]  = 300.0f; engine.setPadCutoff (p, 300.0f);
            padReverse[(size_t) p] = true; engine.setPadReverse (p, true);
            for (int fx = 0; fx < kNumFx; ++fx) engine.setPadSend (p, fx, 0.75f);
        }
        engine.setSongLength (32);
        engine.setSongCell (0, 0, 3);
        engine.setSongCell (1, 4, 2);

        const auto destino = ProjectStore::folderFor (f.getFileNameWithoutExtension());
        ProjectStore::ensureDirectory (destino);
        f.copyFileTo (destino.getChildFile ("project.xml"));
        loadProject (f.getFileNameWithoutExtension());

        //  Cuantos compases de la cancion llevan algo: con <song> ausente tiene
        //  que ser CERO, y antes salia el arreglo del proyecto anterior.
        int celdasCancion = 0;
        for (int ln = 0; ln < AudioEngine::kSongLanes; ++ln)
            for (int bar = 0; bar < AudioEngine::kSongBars; ++bar)
                if (engine.getSongCell (ln, bar) != 0) ++celdasCancion;

        std::cout << "{\"viejo\":\"" << UiAudit::esc (f.getFileNameWithoutExtension()) << "\""
                  << ",\"envio0\":" << engine.getPadSend (0, 0)
                  << ",\"autocorte0\":" << (padSelfCut[0] ? 1 : 0)
                  << ",\"corte0\":" << padCut[0]
                  << ",\"gain20\":" << padGain[20]
                  << ",\"pan20\":" << padPan[20]
                  << ",\"corte20\":" << padCut[20]
                  << ",\"reves20\":" << (padReverse[20] ? 1 : 0)
                  << ",\"envio20\":" << engine.getPadSend (20, 0)
                  << ",\"cancion\":" << celdasCancion
                  << ",\"vel0\":" << engine.getStepVel (0, 0, 0)
                  << ",\"roll0\":" << engine.getStepRoll (0, 0, 0)
                  << "}" << std::endl;
    }
}
