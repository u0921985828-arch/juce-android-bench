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

    //  Y CON UN CLIP PUESTO, que es lo que las dos vistas escondian.
    //
    //  INSERTAR corre las celdas de patron de los cuatro carriles, y desde que
    //  la rejilla es una sola tiene que correr tambien los clips: si no, meter
    //  un compas deja la toma de voz sonando un compas antes de la parte que
    //  acompaña — desincronizada, sin que nada falle y sin que se vea hasta que
    //  suena. Se pone en el compas 4, o sea DESPUES del cursor, que es el unico
    //  sitio donde la operacion tiene que moverlo.
    clips.clear();
    {
        ClipUI c;
        c.pad = 0; c.pista = 1; c.compas = 4; c.desde = 0;
        c.largo = (int) engine.muestrasPorCompas();
        c.gain = 1.0f;
        clips.push_back (c);
        publicaClips();
    }
    auto clipEn = [this] (const char* que)
    {
        std::cout << "{\"arr\":\"" << que << "\",\"clip\":"
                  << (clips.empty() ? -1 : clips[0].compas) << "}" << std::endl;
    };

    insertSongBar();   fila ("insertar en 2");   clipEn ("clip tras insertar");
    removeSongBar();   fila ("quitar el 2");     clipEn ("clip tras quitar");
    clips.clear();
    publicaClips();
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
    //  Y CON ELLOS EL SILENCIO POR BLOQUE, que se escribe y se lee desde que
    //  existe SILENCIAR y no lo cruzaba ninguna prueba: un bit por compas en
    //  `bmudos`, o sea exactamente la clase de propiedad que se publica el dia
    //  que deja de escribirse. Se marcan DOS bloques y no uno -y en carriles
    //  distintos- porque una mascara que vuelve con un solo bit puesto la
    //  cumple igual un lector que se quedo con el primer numero de la lista.
    engine.setSongLaneMute (0, true);
    engine.setSongLaneMute (3, true);
    engine.setSongLoop (2, 5);
    engine.setSongCellMute (0, 1, true);
    engine.setSongCellMute (2, 6, true);

    const auto arbol = captureState();
    engine.setSongLaneMute (0, false);
    engine.setSongLaneMute (3, false);
    engine.clearSongLoop();
    //  BORRADA A MANO ENTRE MEDIAS, que es lo unico que separa «se ha
    //  guardado» de «nadie lo quito».
    for (int ln = 0; ln < AudioEngine::kSongLanes; ++ln)
        engine.setSongCellMuteMask (ln, 0);
    applyState (arbol);

    std::cout << "{\"vuelta\":1,\"mudos\":[";
    for (int ln = 0; ln < AudioEngine::kSongLanes; ++ln)
        std::cout << (ln ? "," : "") << (engine.isSongLaneMuted (ln) ? 1 : 0);
    std::cout << "],\"bucle\":[" << engine.getSongLoopFrom() << ","
              << engine.getSongLoopTo() << "],\"bloques mudos\":[";
    {
        bool primero = true;
        for (int ln = 0; ln < AudioEngine::kSongLanes; ++ln)
            for (int b = 0; b < AudioEngine::kSongBars; ++b)
                if (engine.isSongCellMuted (ln, b))
                {
                    std::cout << (primero ? "" : ",") << "[" << ln << "," << b << "]";
                    primero = false;
                }
    }
    std::cout << "]}" << std::endl;

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

    //  LO QUE UN BLOQUE LLEVA DENTRO, con DOS cifras y no una.
    //
    //  Un bloque era un color con «P3» escrito en medio, asi que la pagina
    //  decia DONDE va cada patron y no decia ninguno. Ahora dibuja sus pasos, y
    //  eso se puede escribir mal de dos formas que se ven igual de bien:
    //
    //    - que no dibuje nada -«sigue siendo un color»-, y
    //    - que dibuje SIEMPRE el compas cero del patron.
    //
    //  La segunda es la que importa y la que ninguna prueba obvia caza: un
    //  bloque de cuatro compases con un patron de DOS da la vuelta dentro, asi
    //  que la celda 0 y la celda 2 tienen que salir IGUALES y la 0 y la 1
    //  DISTINTAS. Con el fallo puesto -leer siempre desde el paso cero- las
    //  tres salen iguales, y la primera cifra sigue diciendo que si.
    {
        openSheet (songSheet, songButton);
        resized();

        //  Un patron de DOS compases con contenido distinto en cada uno: sin
        //  eso las dos mitades son iguales por construccion y la prueba diria
        //  que si con el codigo roto.
        const int pat = 0;
        engine.setPatternLength (pat, 32);
        //  El espejo ES el lado de lectura -el motor no tiene getter de paso-
        //  asi que se escriben los dos, que es lo que hace `stepCellToggled`.
        auto pon = [this, pat] (int st, int pd)
        {
            engine.setStep (pat, st, pd, true);
            pattern[(size_t) pat][(size_t) st][(size_t) pd] = true;
        };
        for (int st = 0; st < AudioEngine::kNumSteps; ++st)
            for (int pd = 0; pd < AudioEngine::kNumPads; ++pd)
                pattern[(size_t) pat][(size_t) st][(size_t) pd] = false;
        pon (0,  0);   // primer compas: un pad en el paso 0
        pon (8,  2);
        pon (16, 4);   // segundo compas: otro pad, otro paso
        pon (20, 6);
        const auto espejo = pattern[(size_t) pat];

        //  Un bloque de CUATRO compases en el carril 0.
        for (int b = 0; b < AudioEngine::kSongBars; ++b)
            for (int ln = 0; ln < AudioEngine::kSongLanes; ++ln)
                engine.setSongCell (ln, b, 0);
        engine.setSongCell (0, 0, pat + 1);
        for (int b = 1; b < 4; ++b) engine.setSongCell (0, b, AudioEngine::kContinued);
        engine.setSongLength (8);
        songPrimerCompas = 0;
        //  Y SIN CURSOR NI CABEZAL: los dos dibujan un marco sobre UNA celda,
        //  asi que con el cursor en el compas 1 o en el 3 las dos colas dejan
        //  de ser identicas por algo que no es su contenido. La primera
        //  version lo pago con `c1c3 240` donde tenia que salir cero.
        songCursor = -1;
        engine.setPlaying (false);
        refreshSong();
        resized();

        const auto conPasos = zatiPinta (songGrid);

        //  Y la misma rejilla con el patron VACIO: si el bloque no dibuja lo
        //  que lleva dentro, las dos imagenes son identicas.
        for (int st = 0; st < AudioEngine::kNumSteps; ++st)
            for (int pd = 0; pd < AudioEngine::kNumPads; ++pd)
                pattern[(size_t) pat][(size_t) st][(size_t) pd] = false;
        refreshSong();
        const auto sinPasos = zatiPinta (songGrid);

        //  Vuelta a poner, para las cifras de la vuelta.
        pattern[(size_t) pat] = espejo;
        refreshSong();

        //  Las tres celdas, recortadas de la imagen ya pintada: la 0, la 1 y la
        //  2 del mismo bloque.
        //  Y LA VUELTA SE MIDE CAMBIANDO EL LARGO DEL PATRON, no recortando
        //  celdas de la imagen.
        //
        //  La primera version recortaba la celda 1 y la 3 -las dos son COLAS,
        //  asi que se dibujan igual y solo las separa su contenido- y saco
        //  `c1c3 240` donde tenia que salir cero. No era el codigo: el ancho de
        //  celda es un FLOAT -(ancho - canaleta) / vista- y el recorte se hacia
        //  con division entera, asi que las dos ventanas caian en fases
        //  distintas del mismo dibujo y comparaban pixeles corridos. Primero se
        //  duda de la prueba.
        //
        //  Sin recortar: el MISMO bloque con el patron de DOS compases y con el
        //  mismo patron declarado de UNO. Con la vuelta bien, las celdas 1 y 3
        //  enseñan los pasos 16..31 en el primer caso y los 0..15 en el
        //  segundo, asi que las dos imagenes difieren. La clave es `giro` y no
        //  `vuelta` porque esa ya la usa la linea de la ida y vuelta del
        //  proyecto, y `arr.py` reparte por clave: dos lineas con la misma se
        //  pisan y la segunda gana. Con el fallo puesto
        //  -leer siempre desde el paso cero- las dos son identicas y la cifra
        //  sale CERO.
        const auto dosCompases = zatiPinta (songGrid);
        engine.setPatternLength (pat, 16);
        refreshSong();
        const auto unCompas = zatiPinta (songGrid);
        engine.setPatternLength (pat, 32);
        refreshSong();

        std::cout << "{\"arr\":\"miniatura\",\"pasos\":" << zatiDifieren (conPasos, sinPasos)
                  << ",\"giro\":" << zatiDifieren (dosCompases, unCompas) << "}" << std::endl;
    }

    //  ------------------------------------------------------------------
    //  EL ZOOM DE LA LINEA DE TIEMPO
    //  ------------------------------------------------------------------
    //
    //  DOS cifras, y la segunda es la que hace falta. «El ciclo pasa por
    //  8, 16 y 4» lo cumple igual un zoom que salta al compas cero en cada
    //  toque, y entonces mirar la cancion mas ancha te deja mirando otra parte
    //  de la cancion: el compas que tenias delante tiene que seguir delante.
    //
    //  Y SE MIDE POR LA TAPA -`songZoomBtn.onClick()`- y no llamando a
    //  `ponVistaCompases`, que es lo unico que ve la ESCALERA: poniendo el
    //  numero por dentro el paso que no cabe no se salta nunca, que es
    //  exactamente donde vive la unica decision de esta funcion.
    {
        engine.setSongLength (AudioEngine::kSongBars);
        resized();

        //  Se parte del compas 16. Con el primer compas en cero las dos
        //  cifras salen bien de las dos formas, asi que la prueba no diria
        //  nada: lo que se mide es que el zoom lo CONSERVE.
        songPrimerCompas = 16;
        refreshSong();

        juce::String vistas, anchos, primeros;
        for (int i = 0; i < 4; ++i)
        {
            if (i > 0) { vistas << ","; anchos << ","; primeros << ","; }
            const int v = songGrid.getCompasesVista();
            vistas   << v;
            anchos   << juce::String ((songGrid.getWidth() - Playlist::kGutter) / juce::jmax (1, v));
            primeros << songPrimerCompas;
            songZoomBtn.onClick();
        }

        std::cout << "{\"arr\":\"zoom\",\"vistas\":[" << vistas
                  << "],\"anchos\":[" << anchos
                  << "],\"primeros\":[" << primeros
                  << "],\"suelo\":" << Metrics::celdaCancion << "}" << std::endl;
    }

    //  ------------------------------------------------------------------
    //  ESTIRAR UN BLOQUE ARRASTRANDO SU FILO
    //  ------------------------------------------------------------------
    //
    //  POR EL GESTO Y NO POR EL CALLBACK. Llamar a `ponLargoBloque` por dentro
    //  se salta exactamente el codigo que decide si el dedo cayo sobre un asa,
    //  sobre el medio o sobre un hueco - que es donde vive todo lo nuevo. Es la
    //  leccion de los cinco fallos del compas del piano.
    //
    //  Y CON EL ARRASTRE PARTIDO EN CUATRO EVENTOS, que es lo unico que separa
    //  «una entrada de deshacer» de «una por movimiento»: de un salto la cifra
    //  sale igual de las dos formas. Es el fallo que ya se midio en el piano.
    {
        engine.setSongLength (16);
        for (int ln = 0; ln < AudioEngine::kSongLanes; ++ln)
            for (int b = 0; b < 16; ++b)
                engine.setSongCell (ln, b, 0);
        //  Un bloque de TRES compases en el carril 1, del 2 al 4: tres es el
        //  minimo que lleva asas, asi que con dos la prueba pasaria sin haber
        //  medido nada.
        engine.setSongCell (1, 2, 1);
        engine.setSongCell (1, 3, AudioEngine::kContinued);
        engine.setSongCell (1, 4, AudioEngine::kContinued);
        songPrimerCompas = 0;
        resized();
        refreshSong();

        auto& rej = songGrid;
        const float pistaH = (float) rej.getHeight() / (float) Playlist::kLanes;
        const float barW   = (float) (rej.getWidth() - Playlist::kGutter)
                           / (float) rej.getCompasesVista();
        auto centro = [&] (int carril, int compas)
        {
            return juce::Point<float> ((float) Playlist::kGutter + barW * ((float) compas + 0.5f),
                                       pistaH * ((float) carril + 0.5f));
        };
        auto evento = [&] (juce::Point<float> pt)
        {
            const auto ahora = juce::Time::getCurrentTime();
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                     pt, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     &rej, &rej, ahora, pt, ahora, 1, false);
        };
        auto bloque = [&] (int carril)
        {
            int cabeza = -1, fin = -1;
            for (int b = 0; b < engine.getSongLength(); ++b)
            {
                const int v = engine.getSongCell (carril, b);
                if (v != 0 && v != AudioEngine::kContinued) { cabeza = b; fin = b + 1; }
                else if (v == AudioEngine::kContinued && cabeza >= 0) fin = b + 1;
            }
            return juce::String ("[") + juce::String (cabeza) + ","
                                      + juce::String (fin - cabeza) + "]";
        };

        const juce::String antes = bloque (1);

        //  CON LA MANO ARMADA, que es donde vive el asa desde que las
        //  herramientas son modos: con el lapiz este mismo arrastre PINTA, que
        //  es lo que tiene que hacer. La primera version de esta medida se
        //  quedo con el lapiz y saco `estirado [7,2]` - el bloque pintado
        //  encima - con el codigo correcto.
        if (songToolBtns.size() > 0) songToolBtns[0]->onClick();

        //  Se coge por el FILO DERECHO -el compas 4- y se lleva al 7, en cuatro
        //  eventos como los emite un dedo.
        const int undoAntes = (int) undoStack.size();
        rej.mouseDown (evento (centro (1, 4)));
        for (int b = 5; b <= 7; ++b) rej.mouseDrag (evento (centro (1, b)));
        rej.mouseUp (evento (centro (1, 7)));
        const juce::String estirado = bloque (1);
        const int entradas = (int) undoStack.size() - undoAntes;

        //  Y CON LA GOMA, EL MISMO SITIO SE BORRA. Es la otra mitad: «el asa
        //  estira» lo cumple igual una rejilla que ha dejado de responder a
        //  todo lo demas, asi que se comprueba que otra herramienta hace lo
        //  suyo en la misma celda.
        if (songToolBtns.size() > 2) songToolBtns[2]->onClick();
        rej.mouseDown (evento (centro (1, 2)));
        rej.mouseUp   (evento (centro (1, 2)));
        const juce::String traselToque = bloque (1);

        std::cout << "{\"arr\":\"asa\",\"antes\":" << antes
                  << ",\"estirado\":" << estirado
                  << ",\"entradas\":" << entradas
                  << ",\"tras el toque\":" << traselToque << "}" << std::endl;

        //  ------------------------------------------------------------------
        //  LAS CUATRO HERRAMIENTAS
        //  ------------------------------------------------------------------
        //
        //  POR LA TAPA Y POR EL GESTO. Llamar a `ponHerramienta` o a
        //  `setSongCellMute` por dentro se salta lo unico que hay que medir:
        //  que la tapa arma el modo y que el modo cambia lo que hace el dedo.
        //
        //  Con DOS cifras donde una se engana. «Mover mueve» lo cumple igual un
        //  codigo que ademas PINTA por el camino, asi que se mira que el bloque
        //  llegue Y que el carril de origen quede vacio. Y «silenciar
        //  silencia» lo cumple una tapa que escribe el bit y no lo lee nadie,
        //  asi que se mira ademas que con el LAPIZ armado ese mismo toque
        //  PINTE y no silencie - o sea que la herramienta decide.
        engine.setSongLength (16);
        for (int ln = 0; ln < AudioEngine::kSongLanes; ++ln)
        {
            engine.setSongCellMuteMask (ln, 0);
            for (int b = 0; b < 16; ++b) engine.setSongCell (ln, b, 0);
        }
        engine.setSongCell (1, 2, 1);
        engine.setSongCell (1, 3, AudioEngine::kContinued);
        engine.setSongCell (1, 4, AudioEngine::kContinued);
        songPrimerCompas = 0;
        resized();
        refreshSong();

        //  MOVER: se arma por la tapa y se arrastra del compas 3 al 8, o sea
        //  agarrando por el SEGUNDO compas del bloque. Tiene que quedar en el
        //  7 -el dedo menos el agarre- y no en el 8: sin el agarre el bloque
        //  se mueve un trozo que la persona no pidio.
        if (songToolBtns.size() > 0) songToolBtns[0]->onClick();
        const int undoA = (int) undoStack.size();
        //  DENTRO DE LA PAGINA VISIBLE. Con ocho compases a la vista, un
        //  evento en el compas 9 lo acota `jlimit` al 7 y el gesto mide otra
        //  cosa: la primera version arrastro al 9 y saco `movido [-1,0]` con
        //  el codigo correcto. Primero se duda de la prueba.
        rej.mouseDown (evento (centro (1, 3)));
        for (int b = 4; b <= 6; ++b) rej.mouseDrag (evento (centro (1, b)));
        rej.mouseUp (evento (centro (1, 6)));
        const juce::String movido = bloque (1);
        const int entradasMov = (int) undoStack.size() - undoA;

        //  SILENCIAR: se arma por su tapa y se toca el bloque. Y despues, con
        //  el LAPIZ armado, el MISMO toque tiene que pintar y no silenciar.
        if (songToolBtns.size() > 3) songToolBtns[3]->onClick();
        int cab = -1;
        for (int b = 0; b < engine.getSongLength(); ++b)
        {
            const int v = engine.getSongCell (1, b);
            if (v != 0 && v != AudioEngine::kContinued) { cab = b; break; }
        }
        rej.mouseDown (evento (centro (1, cab >= 0 ? cab : 0)));
        rej.mouseUp   (evento (centro (1, cab >= 0 ? cab : 0)));
        const int mudo1 = (cab >= 0 && engine.isSongCellMuted (1, cab)) ? 1 : 0;
        rej.mouseDown (evento (centro (1, cab >= 0 ? cab : 0)));
        rej.mouseUp   (evento (centro (1, cab >= 0 ? cab : 0)));
        const int mudo2 = (cab >= 0 && engine.isSongCellMuted (1, cab)) ? 1 : 0;

        //  Y con el LAPIZ, el mismo toque escribe: la herramienta decide.
        if (songToolBtns.size() > 1) songToolBtns[1]->onClick();
        songBrush = 5;
        rej.mouseDown (evento (centro (1, 12)));
        rej.mouseUp   (evento (centro (1, 12)));
        const int pintado = engine.getSongCell (1, 12);

        std::cout << "{\"arr\":\"herramientas\",\"movido\":" << movido
                  << ",\"entradas\":" << entradasMov
                  << ",\"mudo\":[" << mudo1 << "," << mudo2 << "]"
                  << ",\"con lapiz pinta\":" << pintado << "}" << std::endl;
    }
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
        //  Y EL REPARTO: el canal 7 y el recorte a cero, que son los dos
        //  numeros del pad que un banco nuevo tiene que devolver a su sitio.
        engine.setPadCanal (i, 7);
        for (int f = 0; f < AudioEngine::kNumFx; ++f) engine.setPadRecorte (i, f, 0.0f);
    }

    cargaInstrumento (2);      // TEXTURA, que vive en el banco C
    cargaInstrumento (2);
    int deFabrica = 0;
    for (int i = 0; i < kPadsPerBank; ++i) if (padHasSample[(size_t) i]) ++deFabrica;
    std::cout << "{\"dlc\":\"fabrica\",\"pads\":" << deFabrica << "}" << std::endl;

    //  Lo peor de cada uno de los dieciseis, que es lo que hay que mirar: con
    //  el maximo, un solo pad que herede lo canta.
    float pitchMax = 0.0f, envioMax = 1.0f, corteMin = AudioEngine::kFiltOpenHz;
    int   revesN = 0, chokeN = 0, espejoMal = 0, canalMax = 0;
    for (int i = 0; i < kPadsPerBank; ++i)
    {
        pitchMax = juce::jmax (pitchMax, std::abs (padPitch[(size_t) i]));
        //  Y EL CANAL, aparte y no dentro de `espejo_mal`: son dos preguntas
        //  distintas -«hereda el reparto de ayer» y «el mando dice lo que el
        //  motor tiene»- y meterlas en un contador deja una sin poder fallar
        //  sola.
        canalMax = juce::jmax (canalMax, engine.getPadCanal (i));
        corteMin = juce::jmin (corteMin, engine.getPadCutoff (i));
        if (padReverse[(size_t) i]) ++revesN;
        if (padChokeUI[(size_t) i] != 0) ++chokeN;
        //  El RECORTE al reves que antes: hereda si sigue en CERO, que es lo
        //  que el proyecto de ayer le dejo. Se mide el minimo por eso.
        for (int f = 0; f < AudioEngine::kNumFx; ++f)
            envioMax = juce::jmin (envioMax, engine.getPadRecorte (i, f));
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
              << ",\"canal\":" << canalMax
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
    //  Y QUE LA RECETA SEA DEL PAD: que se pueda mover, que se OIGA, que
    //  VOLVER la devuelva y que vuelva del fichero.
    //
    //  Los dieciseis por dieciseis eran una tabla de SOLO LECTURA: la ficha
    //  podia pasar de un preset al siguiente y no habia una sola forma de
    //  tocar ninguno. Un instrumento que no se toca es un sample con nombre.
    //
    //  CON DOS CIFRAS Y NO UNA, que es lo que separa las dos formas de
    //  escribirlo mal: «mover un mando cambia el audio» lo cumple tambien un
    //  codigo que rinde otra cosa cada vez -y entonces VOLVER no devolveria
    //  nada-, y «volver deja el mismo audio» lo cumple un mando que no esta
    //  conectado. Bit a bit y no por nivel: `Sintes::sintetiza` iguala la
    //  sonoridad por octava, asi que «casi el mismo pico» es justo lo que
    //  dejaria pasar una receta que no llega al oscilador.
    {
        const int pad = kBancoInstr * kPadsPerBank + 7;
        const int fam = 0, pre = 2;

        auto difieren = [] (const SampleBuffer* a, const SampleBuffer* b) -> int
        {
            if (a == nullptr || b == nullptr) return -1;
            const int n = juce::jmin (a->buffer.getNumSamples(), b->buffer.getNumSamples());
            if (n <= 0) return -1;
            int d = std::abs (a->buffer.getNumSamples() - b->buffer.getNumSamples());
            const auto* x = a->buffer.getReadPointer (0);
            const auto* y = b->buffer.getReadPointer (0);
            for (int i = 0; i < n; ++i) if (x[i] != y[i]) ++d;
            return d;
        };

        ponInstrumentoEnPad (pad, fam, pre);
        auto tabla = uiSample[(size_t) pad];

        //  Al extremo MAS LEJANO del valor de hoy y no a un tope escrito: si
        //  el preset ya estuviera en ese tope, «moverlo» no moveria nada y la
        //  prueba saldria verde sin haber medido.
        Sintes::Preset r = Sintes::tabla()[fam].p[pre];
        const auto rg = Sintes::rango (fam, 0);
        const float v0 = Sintes::valor (r, 0);
        Sintes::ponValor (r, 0, std::abs (v0 - rg.lo) > std::abs (v0 - rg.hi) ? rg.lo : rg.hi);
        ponInstrumentoEnPad (pad, fam, pre, &r, true);
        auto movido = uiSample[(size_t) pad];
        const int suena = difieren (tabla.get(), movido.get());

        //  Y VOLVER devuelve la fila, bit a bit.
        ponInstrumentoEnPad (pad, fam, pre);
        const int vuelve = difieren (tabla.get(), uiSample[(size_t) pad].get());

        //  Y AHORA LA IDA Y VUELTA POR EL FICHERO, con el mismo arbol que lo
        //  escribe y POR EL CAMINO DE VERDAD -el trabajo troceado, que es
        //  quien rinde los pads al abrir un proyecto-: `applyState` corre en
        //  su `onDone`, o sea DESPUES, asi que leer la receta alli habria
        //  devuelto el instrumento sonando con la fila de la tabla.
        //
        //  Y BORRANDO EL PAD Y SU RECETA A MANO entre medias: si al volver
        //  siguen puestos no es que se hayan guardado, es que nadie los quito.
        ponInstrumentoEnPad (pad, fam, pre, &r, true);
        const auto arbol = captureState();

        uiSample[(size_t) pad] = nullptr;
        padHasSample[(size_t) pad] = false;
        engine.clearPad (pad);
        padReceta[(size_t) pad] = Sintes::tabla()[fam].p[pre];
        padRecetaMovida[(size_t) pad] = false;

        padJob = std::make_unique<PadLoadJob>();
        padJob->folder = juce::File();
        padJob->clearMissing = false;
        readSourceMap (arbol, padJob->source);
        readInstMap   (arbol, padJob->inst);
        readRecetaMap (arbol, padJob->receta);
        while (padJob != nullptr) stepPadJob();

        int iguales = 0;
        for (int i = 0; i < Sintes::kMandos; ++i)
            if (juce::approximatelyEqual (Sintes::valor (padReceta[(size_t) pad], i),
                                          Sintes::valor (r, i)))
                ++iguales;

        std::cout << "{\"instr\":\"receta\",\"suena\":" << suena
                  << ",\"vuelve\":" << vuelve
                  << ",\"mandos\":\"" << iguales << "/" << Sintes::kMandos << "\""
                  << ",\"movida\":" << (padRecetaMovida[(size_t) pad] ? 1 : 0)
                  << ",\"audio\":" << difieren (movido.get(), uiSample[(size_t) pad].get())
                  << "}" << std::endl;
    }

    // ------------------------------------------------------------------
    //  Y QUE LA PESTAÑA PAD ABRA LO QUE EL PAD ES.
    //
    //  Se pidio asi, y ninguna de las catorce reglas de `expo.py` puede verlo:
    //  una pestaña que abre la ficha equivocada se maqueta perfecta -no
    //  solapa, no se sale, no corta un rotulo, no mide cero y esta traducida-.
    //
    //  POR LA TAPA -`padsButton.onClick()`- y no llamando a
    //  `abreFichaDelPad`, que es justo donde el fallo no existe: lo que se
    //  mide es el reparto que el dedo dispara.
    //
    //  Y con DOS cifras, que una se engaña: QUE ficha queda abierta *y* si la
    //  pestaña se queda ENCENDIDA. «Abre la del instrumento» lo cumple igual
    //  un camino que se salta `openSheet`, y entonces la fila de la cara dice
    //  que no hay ninguna ficha abierta con una delante.
    {
        auto abre = [this] (int pad)
        {
            closeAllSheets();
            selectPad (pad);
            if (padsButton.onClick) padsButton.onClick();
        };

        const int normal = 0;                                  // fabrica: una muestra
        const int instr  = kBancoInstr * kPadsPerBank + 7;

        abre (normal);
        const juce::String qn = padSheet.isVisible() ? "pad" : (vstSheet.isVisible() ? "vst" : "ninguna");
        const int tn = padsButton.getToggleState() ? 1 : 0;

        abre (instr);
        const juce::String qi = padSheet.isVisible() ? "pad" : (vstSheet.isVisible() ? "vst" : "ninguna");
        const int ti = padsButton.getToggleState() ? 1 : 0;
        //  Y la puerta de vuelta, que es lo que hace que no se pierda nada:
        //  un instrumento tiene ganancia, pan, filtro y envolvente igual que
        //  una muestra.
        if (vstPadBtn.onClick) vstPadBtn.onClick();
        const juce::String qv = padSheet.isVisible() ? "pad" : (vstSheet.isVisible() ? "vst" : "ninguna");
        closeAllSheets();

        std::cout << "{\"instr\":\"pestana\",\"normal\":\"" << qn << "\",\"tapaN\":" << tn
                  << ",\"instrumento\":\"" << qi << "\",\"tapaI\":" << ti
                  << ",\"vuelta\":\"" << qv << "\"}" << std::endl;
    }

    // ------------------------------------------------------------------
    //  Y QUE MANTENER ABRA ESA MISMA FICHA, que es la mitad que faltaba.
    //
    //  Llego del telefono: «cuando mantienes el pad para entrar en ajustes,
    //  como en pad cuando es un sonido, pero cuando es un instrumento, no
    //  funciona, no es la misma logica». Y ninguna de las quince reglas de
    //  `expo.py` puede verlo: un pad al que le falta un gesto se maqueta
    //  perfecto -no solapa, no se sale, no corta un rotulo, no mide cero y esta
    //  traducido-. Es la familia de los cinco fallos del compas del piano.
    //
    //  POR EL GESTO: se construye un `MouseEvent` y se llama a
    //  `PadButton::mouseDown`, que es donde vive el `startTimer` que decide, y
    //  se vence el reloj como lo venceria el sistema. Llamar a `onHold` por
    //  dentro se salta exactamente la linea del fallo, o sea que saldria verde
    //  con el codigo roto.
    //
    //  Y CON CUATRO CIFRAS, que una sola se engaña por los dos lados: que el
    //  pad este en MODO TECLA -o sea que se mide el caso que fallaba y no un
    //  pad de muestra-, que el reloj se ARME, que abra la ficha del INSTRUMENTO
    //  y que la NOTA no se pague por ello - suena con la ficha delante y se
    //  suelta al levantar. «Abre la ficha» lo cumple igual un arreglo que corte
    //  la nota a los 420 ms, y «la nota sigue» lo cumple el codigo de ayer, que
    //  no abria nada.
    {
        engine.prepareToPlay (48000.0, 128);
        juce::AudioBuffer<float> b (2, 128);
        auto vivas = [&]
        {
            b.clear();
            engine.renderNextBlock (b, 0, 128);
            return engine.getActiveVoiceCount();
        };

        closeAllSheets();
        const int pad = kBancoInstr * kPadsPerBank + 7;
        selectPad (pad);
        refreshModoNota();
        engine.postPanic();
        vivas();

        auto* p = pads[pad];
        const int tecla = (p != nullptr && p->enModoNota()) ? 1 : 0;
        const auto punto = juce::Point<float> ((float) (p->getWidth()  / 2),
                                               (float) (p->getHeight() / 2));
        const auto ahora = juce::Time::getCurrentTime();
        juce::MouseEvent ev (juce::Desktop::getInstance().getMainMouseSource(),
                             punto, juce::ModifierKeys(), 1.0f,
                             0.0f, 0.0f, 0.0f, 0.0f,
                             p, p, ahora, punto, ahora, 1, false);

        p->mouseDown (ev);
        const int armado = p->mantenerArmado() ? 1 : 0;
        p->venceElMantener();
        const juce::String q = vstSheet.isVisible() ? "vst"
                                                    : (padSheet.isVisible() ? "pad" : "ninguna");
        const int sonando = vivas();
        p->mouseUp (ev);
        //  Y SE DEJA CAER ANTES DE CONTAR, que es la lección que este banco ya
        //  tiene escrita con otra pieza: soltar una nota ABRE LA CAIDA, no
        //  corta - y un pad de instrumento nace con 180 ms de suelta, o sea 68
        //  bloques de 128. Preguntar en el bloque siguiente devuelve 1 con el
        //  codigo perfecto. Trescientos milisegundos, que es de sobra.
        for (int i = 0; i < 120; ++i) vivas();
        const int alSoltar = vivas();
        closeAllSheets();

        std::cout << "{\"instr\":\"mantener\",\"tecla\":" << tecla
                  << ",\"armado\":" << armado << ",\"ficha\":\"" << q
                  << "\",\"sonando\":" << sonando
                  << ",\"al_soltar\":" << alSoltar << "}" << std::endl;
    }

    // ------------------------------------------------------------------
    //  Y LAS TRES PUERTAS DEL REPARTO, que era la otra mitad de lo que se
    //  pidio: «la mayoria de opciones y botones de envios o cosas que hay en
    //  pad settings y no hay en la pantalla del plugin instrumento».
    //
    //  POR LA TAPA -`onClick`- y no llamando a `abreRackDelPad` por dentro, que
    //  es justo donde no existe el fallo: lo que se mide es que la tapa ESTE y
    //  lleve donde dice. Y las tres desde la ficha del instrumento abierta, que
    //  es el estado en el que se pidieron.
    {
        const int pad = kBancoInstr * kPadsPerBank + 7;
        auto abre = [this, pad] { closeAllSheets(); selectPad (pad); abreVst(); };

        abre();
        const int hay = (vstRackBtn.isVisible() && vstPianoBtn.isVisible()
                             && vstCanalBtn.isVisible()) ? 1 : 0;
        //  Y QUE EL CANAL DIGA EL DEL PAD: las dos tapas de canal son dos
        //  puertas a la misma rejilla, asi que un rotulo escrito dos veces es
        //  el que un dia se queda viejo.
        const juce::String canal = vstCanalBtn.getButtonText();
        const juce::String canalPad = padCanalBtn.getButtonText();

        if (vstRackBtn.onClick)  vstRackBtn.onClick();
        const juce::String qr = rackSheet.isVisible() ? "rack" : "ninguna";

        abre();
        if (vstPianoBtn.onClick) vstPianoBtn.onClick();
        const juce::String qp = (seqSheet.isVisible() && seqPage == seqPagePiano)
                                    ? "piano" : "ninguna";

        abre();
        if (vstCanalBtn.onClick) vstCanalBtn.onClick();
        const int qc = canalSheet.isVisible() ? 1 : 0;
        abreCanalPicker (false);
        closeAllSheets();

        std::cout << "{\"instr\":\"puertas\",\"hay\":" << hay
                  << ",\"canal\":\"" << canal << "\",\"canalPad\":\"" << canalPad
                  << "\",\"rack\":\"" << qr
                  << "\",\"piano\":\"" << qp << "\",\"picker\":" << qc << "}" << std::endl;
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

//  ==========================================================================
//  CUANTO CUESTA EL PRIMER SONIDO. Ver Tests/carga.py.
//
//  Es la unica cifra de esta casa que mide el PRODUCTO y no una pieza: entre
//  que alguien instala esto y oye algo suyo no puede haber mas que un toque. Un
//  sampler que abre pidiendo una cuenta, o con un asistente de tres pasos, o
//  con los sesenta y cuatro pads vacios, se desinstala antes de sonar - y eso
//  no lo caza ninguna de las once reglas del banco, porque una pantalla de
//  bienvenida se maqueta perfecta.
//
//  Se mide con la maquina RECIEN INSTALADA -HOME limpio, sin sesion- y con la
//  BIENVENIDA puesta, que es el estado real de la primera vez y no el que sale
//  de abrir la app dos veces: la tarjeta del tour cubre la ventana entera, y si
//  algun dia se tragara el toque la maquina seria muda hasta que alguien
//  encuentre SALTAR.
//
//  Y POR EL GESTO, que es donde vive la respuesta: se construye un `MouseEvent`
//  y se llama a `PadButton::mouseDown`. Llamar a `padClicked` por dentro se
//  salta el `Sheet::mouseDown` de la tarjeta que hay delante, que es
//  exactamente lo que hay que comprobar.
void MainComponent::auditPrimerSonido()
{
    engine.prepareToPlay (48000.0, 128);
    juce::AudioBuffer<float> b (2, 128);
    auto vivas = [&]
    {
        b.clear();
        engine.renderNextBlock (b, 0, 128);
        return engine.getActiveVoiceCount();
    };

    int conSonido = 0;
    for (int i = 0; i < kNumPads; ++i) if (padHasSample[(size_t) i]) ++conSonido;

    //  Y LA BIENVENIDA SE LEVANTA AQUI, que es la unica forma de medirla: con
    //  `ZATI_AUDIT` la tarjeta no se enseña NUNCA a proposito -una tarjeta
    //  encima serian diecinueve fichas medidas a traves de ella-, asi que
    //  esperar a que salga sola es esperar a algo que el banco apaga. Se pone
    //  el estado que se quiere medir, igual que `ZATI_DLC` planta los packs.
    showTour (0);
    openSheet (tourSheet, setButton);
    const int bienvenida = tourSheet.isVisible() ? 1 : 0;

    auto* pad = pads[0];
    const auto punto = juce::Point<float> ((float) (pad->getWidth() / 2),
                                           (float) (pad->getHeight() / 2));
    const auto ahora = juce::Time::getCurrentTime();
    juce::MouseEvent ev (juce::Desktop::getInstance().getMainMouseSource(),
                         punto, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                         pad, pad, ahora, punto, ahora, 1, false);

    engine.postPanic();
    vivas();
    pad->mouseDown (ev);
    const int voces = vivas();
    pad->mouseUp (ev);

    std::cout << "{\"primer\":1"
              << ",\"bienvenida\":" << bienvenida
              << ",\"pads_con_sonido\":" << conSonido
              << ",\"voces\":" << voces
              << ",\"toques\":1}" << std::endl;
}

void MainComponent::auditNuevo()
{
    auto fila = [this] (const char* que)
    {
        int conSonido = 0;
        for (int i = 0; i < kNumPads; ++i) if (padHasSample[(size_t) i]) ++conSonido;

        //  LO QUE DE VERDAD LLEGA A UN BUS, que es el producto del envio del
        //  CANAL por el recorte del pad — `sendDePad`, escrito una vez y en el
        //  motor. Con el envio a cero da cero pase lo que pase con el recorte,
        //  que es lo que hace que la maquina nazca sin efectos.
        float envMax = 0.0f;
        double envSuma = 0.0;
        for (int i = 0; i < kNumPads; ++i)
            for (int f = 0; f < AudioEngine::kNumFx; ++f)
            {
                const float v = engine.sendDePad (i, f);
                envMax = juce::jmax (envMax, v);
                envSuma += v;
            }

        //  Y EL REPARTO ENTERO, que es lo que un camino puede heredar del otro:
        //  a que canal va cada pad, y el fader y el mute de los dieciseis.
        int canalMax = 0, muteN = 0;
        double ganSuma = 0.0;
        for (int i = 0; i < kNumPads; ++i) canalMax = juce::jmax (canalMax, engine.getPadCanal (i));
        for (int c = 0; c < kNumCanales; ++c)
        {
            ganSuma += engine.getCanalGain (c);
            if (engine.getCanalMute (c)) ++muteN;
        }

        //  Y LA FILA DE EFECTOS, que es lo que esta comprobacion no miraba y por
        //  eso los dos caminos podian discrepar sin que nada fallara: el
        //  arranque limpio enseñaba FLT HPF DRV DLY BIT REV -seis efectos que
        //  nadie ha puesto- y NUEVO dejaba seis huecos. Dos caras para «vacia».
        //  Y LAS DIECISEIS FILAS, no la del canal actual: vaciar la mitad de
        //  un proyecto es peor que no vaciar nada.
        juce::String ranuras;
        for (int c = 0; c < kNumCanales; ++c)
            for (int sr = 0; sr < kNumRanuras; ++sr)
                ranuras += ((c || sr) ? "," : "") + juce::String (slotFx[(size_t) c][(size_t) sr]);

        std::cout << "{\"nuevo\":\"" << que << "\",\"pads\":" << conSonido
                  << ",\"canales\":" << kNumCanales << ",\"ranurasPorCanal\":" << kNumRanuras
                  << ",\"envmax\":" << envMax << ",\"envsuma\":" << envSuma
                  << ",\"canalmax\":" << canalMax << ",\"cgansuma\":" << ganSuma
                  << ",\"cmuten\":" << muteN
                  << ",\"ranuras\":[" << ranuras << "]"
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

    //  Y EL PROYECTO DE AYER, PUESTO A MANO ANTES DE PULSAR NUEVO.
    //
    //  Sin esto las dos filas se miden sobre una maquina que nadie ha tocado,
    //  asi que «NUEVO deja la mesa vacia» lo cumple igual un NUEVO que no la
    //  toca — o sea la comprobacion no puede decir que no, que es exactamente
    //  el fallo que este mismo fichero cazo dos veces con los envios y con la
    //  linea de tiempo. Se mueve LO QUE SE PUEDE HEREDAR: a que canal va un
    //  pad, cuanto manda ese canal, su fader, su mute y su fila de ranuras.
    engine.setPadCanal (3, 7);
    engine.setCanalSend (7, AudioEngine::kFxDly, 0.8f);
    engine.setCanalGain (7, 0.25f);
    engine.setCanalMute (2, true);
    slotFx[7][0] = AudioEngine::kFxDly;

    newProject();
    fila ("nuevo");
}

void MainComponent::auditOpen (const juce::String& pedido)
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

    //  LAS TAPAS DE LA CARA QUE ESCONDEN UN GESTO. Ver UiAudit::gestoDe.
    //
    //  SE BUSCAN, no se enumeran. Una lista escrita a mano aqui seria la misma
    //  lista que la de `paintGesturesPage` escrita dos veces, y la que un dia
    //  se queda vieja es justo la que tiene que avisar: MANTEN SOLO y MANTEN
    //  AUTO llevaban dos tandas existiendo sin fila. Recorriendo los hijos de
    //  la cara, una tapa de mantener nueva aparece sola y sin fila que la
    //  nombre, y el banco la canta.
    //
    //  Con el ROTULO PUESTO y no con su nombre de variable: la fila de GESTOS
    //  esta traducida —«MANTEN CARGAR», «HOLD LOAD»— asi que lo unico que
    //  compara en los cuatro idiomas es el mismo `T()` por los dos lados.
    //
    //  Y las ranuras de efecto van marcadas como FAMILIA: su fila es «MANTEN
    //  UN EFECTO» y no las nombra una por una, que serian seis filas iguales.
    //  Y se vacia antes: `auditOpen` corre una vez por ficha en el ciclado de
    //  paginas y otra por accion en el fuzz, asi que sin esto la lista crece
    //  con cada llamada. Es el mismo cuidado que `resized()` tiene con
    //  `UiAudit::tarjetas`.
    UiAudit::mantener.clear();
    for (auto* hijo : getChildren())
        if (auto* h = dynamic_cast<HoldButton*> (hijo))
            UiAudit::gestoDe (h->getButtonText(), fxButtons.contains (h) ? 1 : 0);

    //  Y LA APP CON TRABAJO DENTRO, ANTES de abrir nada.
    //
    //  Antes de la ficha porque casi todo lo que esta entrada puede cazar se
    //  decide al MAQUETAR: un rotulo que cabia vacio, una celda que se reparte
    //  entre sesenta y cuatro compases en vez de entre ocho, una lista con
    //  filas de verdad. Llenar despues seria medir la ficha vacia y luego
    //  cambiarle el contenido.
    //
    //  Fuera del `if (which.isEmpty())` a proposito: la CARA es la pantalla
    //  que mas cambia con trabajo dentro -la linea de continuidad, los nombres
    //  de los pads, la tira de zatis- y es justo la que se mide con la ficha
    //  vacia.
    //  Y ES UN ESTADO DE LA LISTA, no una variable aparte. Empezo siendo
    //  `ZATI_LLENA` y eso dejaba la regla en DOS sitios: la lista de pantallas
    //  vive en `Tests/expo.py` y la leen ademas `planos.py`, `carga.py` y
    //  `desglose.py`, asi que un estado que solo se alcanza con otra variable
    //  es un estado que esas tres no saben abrir. Como prefijo -«llena» es la
    //  CARA con trabajo dentro, «llena-song» la linea de tiempo llena- lo
    //  abren las cuatro sin saber nada de esto, igual que `secp` o `rackf`.
    juce::String which = pedido;

    if (which == "llena" || which.startsWith ("llena-"))
    {
        llenaDePrueba();
        which = which.fromFirstOccurrenceOf ("llena", false, false)
                     .trimCharactersAtStart ("-");
    }

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
    //  LA CARA CON EL EQ DELANTE, que es OTRA pantalla: el plato deja de ser
    //  tres mandos y pasa a ser una curva, y sin esta entrada nadie mide -ni
    //  fotografia- el unico estado en el que un efecto trae su propia cara. Es
    //  lo mismo que `secp` con la tira del paso y `songa` con la banda de
    //  audio, y lo mismo que hacen `ZATI_SKIN` y `ZATI_DLC`: convertir en
    //  ENTRADA lo que si no seria «lo que hubiera».
    //
    //  Con una curva ESCRITA y no plana: una curva plana es una raya, o sea
    //  justo la foto en la que no se ve si los nodos estan donde deben.
    else if (which == "eq")
    {
        closeAllSheets();
        ponEnRanura (0, kFxEq);
        setFxEnabled (kFxEq, true);
        focusFx (kFxEq);
        const float dB[Eq5::kBands] = { 5.0f, -6.0f, 3.5f, -4.0f, 8.0f };
        for (int b = 0; b < Eq5::kBands; ++b)
            ponBandaEq (b, Eq5::kFreqDef[b], dB[b]);
    }
    //  Y LA FICHA DE UNA BANDA, que es la pantalla a la que se llega
    //  MANTENIENDO sobre un nodo y que ninguna otra entrada maqueta: sus cinco
    //  chips de tipo y su mando Q solo existen con ella abierta.
    else if (which == "eqb")
    {
        closeAllSheets();
        ponEnRanura (0, kFxEq);
        setFxEnabled (kFxEq, true);
        focusFx (kFxEq);
        abreBandaEq (2);
    }
    //  LA CARA CON UN EFECTO QUE NO ES EL EQ, que es la otra mitad del plato.
    //
    //  `eq` mide la cara con la curva ocupandolo entero y `""` la mide con la
    //  fila vacia, o sea que el reparto de los tres mandos MAS el visor no lo
    //  medía nadie: es el estado al que le falta justo lo que se acaba de
    //  anadir, que es la leccion de `secp`, `eqb`, `songa` e `instp`.
    else if (which == "plato")
    {
        closeAllSheets();
        ponEnRanura (0, AudioEngine::kFxDly);
        setFxEnabled (AudioEngine::kFxDly, true);
        focusFx (AudioEngine::kFxDly);
    }
    else if (which == "song") openSheet (songSheet, songButton);
    //  LA BANDA DE AUDIO ES OTRA PANTALLA y por eso es otra entrada. Sin ella
    //  el banco mediria siempre la vista de patrones, que es justo la que no
    //  cambio: es lo mismo que `secp` con la tira del paso y que `instp` con la
    //  lista de presets. Y con un clip PUESTO, que una banda vacia no tiene
    //  ningun bloque cuyo rotulo medir.
    else if (which == "songa")
    {
        openSheet (songSheet, songButton);
        selectedPad = 0;
        ponClip (0, 0);
        ponClip (2, 3);
    }
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
    //  LA MESA, Y SE CAMBIA DE BANCO CON EL DEDO ANTES DE MEDIRLA.
    //
    //  Sin el gesto, esta ficha se mide recien abierta -banco A, una encendida-
    //  y la reentrada que dejaba A y D encendidos a la vez no existe: hace
    //  falta VOLVER de un banco a otro. Es lo mismo que el selector de canal ya
    //  obligo a hacer, y por lo mismo.
    //
    //  Ida y vuelta, y se acaba en el banco A: asi la geometria medida es
    //  exactamente la de antes -las mismas dieciseis tiras- y el numero del
    //  banco no se mueve por haber anadido el gesto. Lo unico que cambia es que
    //  la fila de chips ha pasado por el camino de verdad.
    else if (which == "mix")
    {
        showMixPage (mixPagePads);
        openSheet (mixSheet, mixButton);
        pulsaTapa (mixBankBtns[kNumBanks - 1]);
        pulsaTapa (mixBankBtns[0]);
    }
    //  LA PAGINA DE CANALES DE LA MESA, que es un estado propio y no la misma
    //  ficha con otro contenido: la fila de chips lleva dos tapas en vez de
    //  cinco, las tiras pierden el pan y el solo, y el renglon dice la cuenta
    //  de pads en vez de un nombre. Es la leccion de `secp`, `eqb` e `instp`.
    else if (which == "mixc")
    {
        //  Con pads repartidos, o las dieciseis tiras dirian «—» y el renglon
        //  que mas puede romper un rotulo no se mediria nunca.
        for (int p = 0; p < kNumPads; ++p) engine.setPadCanal (p, p % kNumCanales);
        showMixPage (mixPageCanales);
        openSheet (mixSheet, mixButton);
    }
    //  Y LA REJILLA DE DIECISEIS CANALES, que se dibuja ENCIMA de la ficha del
    //  pad: una capa que se pone sobre otra es donde vive el residuo.
    else if (which == "canal")
    { showPadPage (padPageRig); openSheet (padSheet, padsButton); abreCanalPicker (true); }
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
        //  Y POR EL CAMINO DEL DEDO Y NO POR `onClick()` a pelo, que es lo
        //  que el chip de radio obliga a decir en voz alta: un dedo hace
        //  `setToggleState (true, sendNotification)`, y eso apaga a las
        //  hermanas DISPARANDO sus callbacks. Llamando al callback directo ese
        //  camino no se ejerce jamas, que es como la reentrada de la mesa pudo
        //  vivir en el codigo con el banco entero en verde.
        auto pulsa = [this] (int i)
        {
            if (juce::isPositiveAndBelow (i, langButtons.size()))
                pulsaTapa (langButtons[i]);
        };
        pulsa (otro);
        pulsa (mio);

        std::cout << "{\"idioma\":\"cambiado\",\"paso_por\":" << otro
                  << ",\"quedo\":" << (int) Lang::current()
                  << ",\"ajustes\":" << (setSheet.isVisible() ? 1 : 0)
                  << ",\"tour\":" << (tourSheet.isVisible() ? 1 : 0) << "}" << std::endl;
    }
    else if (which == "midi") { showSetPage (pageMidi); refreshMidiDevices(); openSheet (setSheet, setButton); }
    else if (which == "rack") { ponCanalActual (0); openSheet (rackSheet, mixButton); refreshRack(); }
    //  Y EL RACK CON LAS SEIS RANURAS LLENAS.
    //
    //  Desde que la maquina abre vacia, `rack` mide seis filas con «+» en el
    //  canalon, el fader apagado y ninguna miniatura — o sea la ficha a la que
    //  le falta justo lo que esta tanda anade. Es la leccion de `secp`, `eqb` e
    //  `instp`: sin la segunda entrada se mide siempre el estado que no cambio.
    //  Con los seis primeros tipos, que son los que caben en las seis ranuras.
    else if (which == "rackf")
    {
        for (int s = 0; s < kNumRanuras; ++s) ponEnRanura (s, s);
        //  Y CON LOS SEIS ENVIOS PUESTOS. Seis valores distintos, que con seis
        //  iguales un cruce de filas pasaria desapercibido.
        for (int s = 0; s < kNumRanuras; ++s)
            engine.setCanalSend (0, s, 0.15f + 0.15f * (float) s);
        ponCanalActual (0);
        //  Y CON LA ULTIMA ENFOCADA, que es lo que hace medible la cuña.
        //  `plato` la deja en la ranura 0 y sin un segundo estado «apunta a la
        //  ranura enfocada» lo cumple igual una cuña clavada en la primera
        //  tapa — y ademas seis ranuras llenas y NINGUNA enfocada no es un
        //  estado al que se llegue tocando. Cero corridas nuevas.
        focusFx (kNumRanuras - 1);
        openSheet (rackSheet, mixButton);
        refreshRack();
    }
    //  EL MENU DE UNA RANURA, en sus DOS estados, que es lo mismo que hizo
    //  falta con `secp` y con `instp`: sobre una ranura VACIA -que es como se
    //  llega desde la cara- y sobre una LLENA, que ademas enseña VACIAR y por
    //  tanto pide una fila mas y mide otra cosa.
    else if (which == "ranura")  { ponEnRanura (0, kSlotVacia); abreMenuRanura (0); }
    else if (which == "ranural") { ponEnRanura (0, 3);          abreMenuRanura (0); }
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
    else if (which == "vst" || which == "vstm")
    {
        //  LA FAMILIA MAS LARGA Y SU PRESET MAS LARGO, y las dos se PIDEN a la
        //  tabla en vez de escribirse aqui.
        //
        //  La familia decide dos cosas -si el conmutador del preset cabe en el
        //  renglon de la cabecera, y si el nombre se corta- y es CUERDA PULS,
        //  once caracteres. El preset decide el ancho de la pantalla, y el
        //  gancho pedia el 0: "1/16 NYLON", DOCE caracteres, cuando el peor de
        //  los 256 son DIECISIETE. O sea que la regla del rotulo cortado se le
        //  hacia a la cadena mas corta posible - una linea que imprime OK.
        //
        //  La cuenta es la del pintor MENOS lo que es constante: "/16" y los
        //  tres espacios valen lo mismo en las 256, asi que lo que separa a una
        //  de otra son las cifras del numero y las letras del nombre. Y sale 17
        //  clavados, o sea el peor de la tabla entera: "16/16   BRIGHT GT".
        int fam = 0;
        for (int f = 1; f < Sintes::kFamilias; ++f)
            if (juce::String (Sintes::tabla()[f].nombre).length()
                > juce::String (Sintes::tabla()[fam].nombre).length())
                fam = f;

        int pre = 0, peor = -1;
        for (int i = 0; i < Sintes::kPresets; ++i)
        {
            const int n = juce::String (i + 1).length()
                        + juce::String (Sintes::tabla()[fam].p[i].nombre).length();
            if (n > peor) { peor = n; pre = i; }
        }

        const int pad = kBancoInstr * kPadsPerBank + fam;
        ponInstrumentoEnPad (pad, fam, pre);
        selectBank (kBancoInstr);
        selectPad (pad);
        abreVst();

        //  Y `vstm` ES LA MISMA FICHA CON LA RECETA MOVIDA, que es el UNICO
        //  estado en el que VOLVER existe -`refreshVst` la apaga si no-. Sin
        //  esta entrada esa tapa no se maqueta en ninguna de las 1400 corridas,
        //  o sea que ninguna de las reglas la ha medido nunca. Precedente:
        //  `secp`, que mide la pagina del secuenciador con un paso tocado.
        //
        //  Y se mueve el MANDO con `sendNotificationSync` y no se escribe
        //  `padRecetaMovida` a mano: el callback es quien pone la marca, y
        //  llamarlo por dentro se salta justo el codigo que decide.
        if (which == "vstm" && vstMandos.size() == Sintes::kMandos)
        {
            auto* s0 = vstMandos[0];
            s0->setValue (s0->getMaximum(), juce::sendNotificationSync);
            refreshVst();
            resized();
        }
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
    //  LA PUERTA, PULSADA DE VERDAD. Se abre el paso de la bienvenida que la
    //  ofrece y se pulsa su `onClick`, que es donde vive la decision: llamar a
    //  `showTour (kTourBienvenida)` por dentro se salta justo el codigo que
    //  decide si esa tapa salta o sigue.
    else if (which == "tourpuerta")
    {
        closeAllSheets();
        showTour (kTourBienvenida - 1);
        openSheet (tourSheet, setButton);
        if (tourSkipBtn.onClick) tourSkipBtn.onClick();
        resized();
    }
    //  LA CARA EN MODO CANCION, que es un estado que el banco no abria y por
    //  eso no medía: alli la tapa de modo dice CANCION y las seis pestañas
    //  llevan una que tambien lo dice, y girado comparten renglon. «Un estado
    //  que el banco no abre es un estado sin medir» — la misma leccion que
    //  obligo a medir `secp` y `instd`.
    else if (which == "songm")
    {
        closeAllSheets();
        ponModoCancion (true);
        resized();
    }
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
//  LA BANDA DE AUDIO, POR EL GESTO Y NO POR EL CALLBACK.
//
//  Llamar a `ponClip`/`mueveClip` por dentro se salta exactamente el codigo que
//  decide QUE pista y QUE compas caen bajo el dedo, que es donde vivian los
//  cinco fallos del compas del piano. Aqui hay ademas una cuenta propia que no
//  existe en ningun otro sitio -el AGARRE: por que compas suyo se cogio el
//  clip- y esa solo se puede medir arrastrando de verdad.
//
//  Con TRES cifras, que es lo que separa «se movio» de «se movio donde tocaba»:
//  donde cae el clip que se pone, donde queda el que se arrastra por su primer
//  compas, y donde queda el que se arrastra POR EL TERCERO. Sin la tercera,
//  «arrastrar mueve» lo cumple igual un codigo que pega el bloque por su
//  principio de un salto - que es lo primero que se nota y lo que el agarre
//  existe para evitar.
void MainComponent::auditClips()
{
    //  Un pad con sonido, que sin el no hay clip que poner: `ponClip` lee el
    //  buffer que sostiene la CARA y se rinde si no hay ninguno.
    selectedPad = 0;
    engine.setSongLength (8);
    clips.clear();
    publicaClips();
    resized();

    auto& rej = songGrid;
    const int gutter = Playlist::kGutter;
    const float pistaH = (float) rej.getHeight() / (float) Playlist::kAudioLanes;
    const float barW   = (float) (rej.getWidth() - gutter) / (float) songGrid.getCompasesVista();

    auto punto = [&] (int pista, int compas)
    {
        return juce::Point<float> ((float) gutter + barW * ((float) compas + 0.5f),
                                   pistaH * ((float) pista + 0.5f));
    };
    auto evento = [&] (juce::Point<float> pt)
    {
        const auto ahora = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                 pt, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &rej, &rej, ahora, pt, ahora, 1, false);
    };
    auto fila = [&] (int i)
    {
        if (! juce::isPositiveAndBelow (i, (int) clips.size())) return juce::String ("[]");
        return "[" + juce::String (clips[(size_t) i].pista) + ","
                   + juce::String (clips[(size_t) i].compas) + "]";
    };
    auto compasesDe = [&] (int i)
    {
        if (! juce::isPositiveAndBelow (i, (int) songClipsVista.size())) return 0;
        return songClipsVista[(size_t) i].hasta - songClipsVista[(size_t) i].desde;
    };
    //  CADA MEDIDA PARTE DE UN ESTADO PUESTO A MANO, y no del que dejo la
    //  anterior. La primera version las encadenaba y en cuanto las asas
    //  existieron el agarre paso a medir un asa: tres cifras cruzadas de una
    //  vez, y ninguna decia cual era el fallo.
    auto pon = [&] (int pista, int compas, int compases)
    {
        clips.clear();
        ClipUI c;
        c.pad = 0; c.pista = pista; c.compas = compas; c.desde = 0;
        c.largo = (int) ((double) compases * engine.muestrasPorCompas());
        c.gain = 1.0f;
        clips.push_back (c);
        publicaClips();
        refreshSong (false);
    };
    auto arrastra = [&] (int p0, int c0, int p1, int c1)
    {
        auto d = evento (punto (p0, c0));  rej.mouseDown (d);
        auto m = evento (punto (p1, c1));  rej.mouseDrag (m);
        auto u = evento (punto (p1, c1));  rej.mouseUp (u);
    };

    //  1. PONER: un toque en un hueco deja el clip en ESA pista y ESE compas.
    //
    //  CON LA BROCHA EN **CLIP**, que es lo que cambio al fundir las dos
    //  vistas: antes esto se medi­a en la vista de audio, donde un toque en un
    //  hueco solo podia significar un clip. En una rejilla que lleva las dos
    //  familias, lo que significa un hueco lo dice la brocha - y por eso se
    //  pone aqui a mano y no se da por hecho: medir con la de patrones y
    //  esperar un clip es medir otra cosa.
    //  POR LA TAPA: la brocha cicla PATRON - SONIDO - CLIP con
    //  `songPadModeBtn`, asi que se pulsa dos veces. Escribir `songPincel` a
    //  mano se salta justo el codigo que traduce la brocha a lo que la rejilla
    //  lee (`songGrid.pincelClip`), que es donde vivia el fallo: el campo
    //  estaba escrito en la cara y muerto en la rejilla.
    ponHerramienta (Playlist::hLapiz);
    songPincel = 0;
    songPadModeBtn.onClick();
    songPadModeBtn.onClick();
    const int pincel = songPincel;
    clips.clear(); publicaClips(); refreshSong (false);
    { auto e = evento (punto (2, 3)); rej.mouseDown (e); }
    const auto puesto = fila (0);

    //  Y EL RECORTE HEREDADO: el clip nace con LO QUE SUENA en el pad y no con
    //  el buffer entero. Dos cifras -el largo del clip y el de la fuente-
    //  porque si fueran iguales la prueba diria que si a no hacer nada.
    padStart01[0] = 0.25f;
    padEnd01[0]   = 0.75f;
    clips.clear(); publicaClips();
    //  En un hueco DE VERDAD y no en (0,0): la cancion de un proyecto nuevo
    //  trae el patron 1 en el primer compas -lo pone `nuevo.py` y esta escrito
    //  ahi- y un clip solo entra donde no hay nada. Con las dos familias en la
    //  misma rejilla, «hueco» dejo de ser «cualquier celda».
    { auto e = evento (punto (3, 6)); rej.mouseDown (e); }
    const int largoFuente = (uiSample[0] != nullptr) ? uiSample[0]->buffer.getNumSamples() : 0;
    const int largoClip   = clips.empty() ? 0 : clips[0].largo;

    //  2. MOVER agarrando por su PRIMER compas: de (2,3) a (1,5).
    //
    //  CON LA MANO ARMADA, que es lo que cambio al fundir las dos vistas: en
    //  una rejilla que se pinta con el dedo arrastrado, «arrastrar» ya
    //  significa pintar, asi que mover es un MODO y no un gesto - la misma
    //  decision que el piano tomo con LAPIZ, GOMA, TIJERAS y SEL. Antes esto
    //  se medi­a en la vista de audio, donde arrastrar solo podia significar
    //  mover.
    ponHerramienta (Playlist::hMano);
    pon (2, 3, 1);
    arrastra (2, 3, 1, 5);
    const auto movido = fila (0);

    //  3. Y AGARRANDO POR EL TERCERO. Cuatro compases en (1,5), o sea [5,9):
    //  los filos -5 y 8- son asas, asi que se agarra por el 7, que es interior,
    //  y se suelta en el 2. Tiene que quedar en el 0: el dedo MENOS el agarre.
    //  Sin esta, «arrastrar mueve» lo cumple igual un codigo que pega el bloque
    //  por su principio de un salto, que es lo primero que se nota.
    pon (1, 5, 4);
    //  Y CUANTOS COMPASES MIDE, que es el control: agarrar «por el tercero»
    //  solo significa algo si el clip tiene tres. Sin esta cifra la prueba
    //  pasaria con un clip de uno, donde el agarre vale cero siempre.
    const int agarreCompases = compasesDe (0);
    arrastra (1, 7, 1, 2);
    const auto agarrado = fila (0);

    //  4. EL LARGO, arrastrando un FILO. Tres compases en (1,0), o sea [0,3):
    //  se coge su ultimo compas -el 2- y se lleva al 4, y tiene que quedar de
    //  CINCO compases SIN moverse de sitio. Las dos cifras, porque un asa que
    //  ademas mueve pasa cualquier prueba que solo mire el largo.
    pon (1, 0, 3);
    arrastra (1, 2, 1, 4);
    const auto trasAsa = fila (0);
    const int compasesTrasAsa = compasesDe (0);

    //  5. Y UN CLIP CORTO NO TIENE ASAS. Uno de un compas: arrastrar su filo
    //  -que es el clip entero- tiene que MOVERLO y dejarlo de un compas. Sin
    //  esta cifra, «aqui no caben asas» y «no hay asas» son la misma corrida en
    //  verde.
    pon (1, 0, 1);
    arrastra (1, 0, 1, 3);
    const auto cortoTrasFilo = fila (0);
    const int cortoCompases = compasesDe (0);

    //  6. Y QUITAR con la brocha VACIAR, que es la misma que borra en la otra
    //  vista: un gesto nuevo para borrar seria una segunda forma de lo mismo.
    //  Y CON LA GOMA ARMADA, que es su dueña desde que la fila de herramientas
    //  existe: la tapa VACIAR se retiro porque eran dos dueños de la misma
    //  funcion. `borrando` sigue valiendo -`ponHerramienta` lo pone- y por eso
    //  se arma la herramienta y no el booleano: escribirlo a mano se salta la
    //  traduccion, que es el mismo fallo que la brocha CLIP acaba de costar.
    pon (1, 2, 1);
    ponHerramienta (Playlist::hGoma);
    { auto e = evento (punto (1, 2)); rej.mouseDown (e); }
    const int trasBorrar = (int) clips.size();

    //  7. Y LA CANALETA SILENCIA LAS DOS COSAS.
    //
    //  Con las dos vistas fundidas un carril lleva bloques de patron Y clips,
    //  asi que su MUTE tiene que callar los dos: las mascaras son distintas a
    //  proposito -silenciar el carril 1 no puede callar la pista de audio 1 en
    //  una app donde fueran cosas separadas- y aqui, con la rejilla fundida,
    //  carril 1 y pista 1 SON el mismo carril, asi que la canaleta escribe las
    //  dos. Con una sola, la persona calla un carril y el audio sigue sonando.
    //
    //  Por el GESTO: un toque a la izquierda del canalon, que es donde
    //  `Playlist::toca` decide que es la canaleta y no una celda.
    {
        const juce::Point<float> pt ((float) gutter * 0.5f, pistaH * 2.5f);
        auto e = evento (pt);
        rej.mouseDown (e);
    }
    const int carrilMudo = engine.isSongLaneMuted (2) ? 1 : 0;
    const int pistaMuda  = engine.isPistaMute (2) ? 1 : 0;

    std::cout << "{\"clipsui\":1,\"puesto\":" << puesto
              << ",\"movido\":" << movido
              << ",\"agarrado\":" << agarrado
              << ",\"agarre_compases\":" << agarreCompases
              << ",\"tras_asa\":" << trasAsa
              << ",\"compases_tras_asa\":" << compasesTrasAsa
              << ",\"corto_tras_filo\":" << cortoTrasFilo
              << ",\"corto_compases\":" << cortoCompases
              << ",\"largo_fuente\":" << largoFuente
              << ",\"largo_clip\":" << largoClip
              << ",\"tras_borrar\":" << trasBorrar
              << ",\"pincel\":" << pincel
              << ",\"carril_mudo\":" << carrilMudo
              << ",\"pista_muda\":" << pistaMuda
              << ",\"celda\":[" << (int) barW << "," << (int) pistaH << "]"
              << "}" << std::endl;
}

//  UN TOQUE EN LA PISTA DE UNA BARRA, en la fraccion que se le diga de su
//  recorrido. Se construye un `MouseEvent` y se llama a `BarraVista::mouseDown`
//  y no a `onMueve`: llamar al callback salta justo el codigo que decide cuanto
//  avanza el toque y donde cae el pulgar, que es donde vive lo que hay que
//  medir. Es la misma leccion que los cinco fallos del compas del piano.
static void tocaBarra (BarraVista& b, float t)
{
    if (b.getWidth() <= 0 || b.getHeight() <= 0) return;
    const juce::Point<float> pt (b.esVertical() ? (float) b.getWidth() * 0.5f
                                                : (float) b.getWidth() * t,
                                 b.esVertical() ? (float) b.getHeight() * (1.0f - t)
                                                : (float) b.getHeight() * 0.5f);
    const auto ahora = juce::Time::getCurrentTime();
    juce::MouseEvent ev (juce::Desktop::getInstance().getMainMouseSource(),
                         pt, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                         &b, &b, ahora, pt, ahora, 1, false);
    b.mouseDown (ev);
    b.mouseUp (ev);
}

//  ARRASTRAR EL PULGAR HASTA UNA FRACCION DEL RECORRIDO, que es el otro gesto
//  de la barra y el unico que llega a los extremos: un toque en la pista salta
//  UNA pagina -a proposito, ver `BarraVista::mouseDown`- asi que con sesenta y
//  cuatro compases a la vista de ocho harian falta siete toques para llegar al
//  final, y lo que se quiere medir es que la barra ALCANCE, no cuantos toques
//  cuesta.
//
//  Se apoya sobre el pulgar -si no, `mouseDown` lo lee como un toque en la
//  pista y `agarrePx` se queda en -1, o sea que el arrastre no hace nada- y se
//  suelta en el extremo que se pida.
static void arrastraBarra (BarraVista& b, float t)
{
    if (b.getWidth() <= 0 || b.getHeight() <= 0) return;
    const bool v = b.esVertical();
    const float largo = v ? (float) b.getHeight() : (float) b.getWidth();
    const auto ahora = juce::Time::getCurrentTime();

    //  El centro del pulgar, que es donde un dedo lo cogeria. No se calcula
    //  aqui de donde esta -eso seria repetir la formula que se juzga- sino que
    //  se barre la barra buscando el punto que `mouseDown` acepta como agarre:
    //  se apoya, se arrastra un pixel y se mira si la vista se movio o no.
    auto punto = [&] (float f)
    {
        return v ? juce::Point<float> ((float) b.getWidth() * 0.5f, largo * (1.0f - f))
                 : juce::Point<float> (largo * f, (float) b.getHeight() * 0.5f);
    };
    auto evento = [&] (juce::Point<float> pt)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                 pt, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &b, &b, ahora, pt, ahora, 1, false);
    };

    //  Se agarra donde el pulgar esta AHORA -o sea en la fraccion que la vista
    //  ocupa- y se arrastra hasta la que se pide, en varios pasos: un dedo
    //  emite un evento por movimiento, y de un salto la cuenta del agarre no se
    //  ejercita.
    const int rec = juce::jmax (1, b.getTotal() - b.getVisibles());
    const float desde = (float) b.getPrimero() / (float) rec;
    auto d = evento (punto (desde));  b.mouseDown (d);
    for (int i = 1; i <= 4; ++i)
    {
        const float f = desde + (t - desde) * (float) i / 4.0f;
        auto m = evento (punto (f));  b.mouseDrag (m);
    }
    auto u = evento (punto (t));  b.mouseUp (u);
}

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

    //  Y AHORA AL COMPAS 1, POR LA BARRA y no moviendo `seqPrimerPaso` a
    //  mano: mover la variable por dentro se salta el codigo que decide cuanto
    //  avanza un toque y quien repinta despues, que es donde vivieron dos de
    //  los cinco fallos. Un toque en la PISTA, mas alla del pulgar, salta una
    //  pagina — o sea las columnas que se ven, que aqui son dieciseis.
    tocaBarra (seqBarra, 1.0f);
    if (pianoGrid.onCelda) pianoGrid.onCelda (3, 5);

    //  Y LA VENTANA SE PUBLICA, que es lo que esta medida daba por hecho y
    //  dejo de ser verdad.
    //
    //  Decia «un toque en la pista salta una pagina - o sea las columnas que se
    //  ven, que aqui son dieciseis», y con la ventana CONTINUA eso ya no es un
    //  compas: la barra avanza las columnas que quepan y se acota en
    //  `total - visibles`, que con un patron de 32 y veinte columnas a la vista
    //  deja el primer paso en DOCE. La comprobacion pedia el compas 1 y salia
    //  cero con el codigo perfecto — la undecima vez que en este banco falla la
    //  medida y no lo medido.
    //
    //  Se publica DONDE quedo la ventana y se comprueba contra eso: que se haya
    //  MOVIDO -sin eso «escribio en su sitio» lo cumple una barra muerta- y que
    //  la nota caiga en `primerPaso + columna`, que es la unica cuenta que el
    //  gesto promete. El testigo del principio sigue siendo lo que separa
    //  «escribio» de «escribio donde tocaba».
    const int baseTrasTocar = seqPrimerPaso;
    std::cout << "{\"piano\":\"compas\",\"base\":" << baseTrasTocar
              << ",\"escrito\":" << (pattern[0][(size_t) juce::jlimit (0, kNumSteps - 1, baseTrasTocar + 3)][0] ? 1 : 0)
              << ",\"nota\":" << engine.getStepNote (0, juce::jlimit (0, kNumSteps - 1, baseTrasTocar + 3), 0)
              << ",\"paso3\":" << (pattern[0][3][0] ? 1 : 0)
              << ",\"nota3\":" << engine.getStepNote (0, 3, 0) << "}" << std::endl;

    //  LA VISTA. La barra tiene que haber repintado el piano, asi que la
    //  columna 3 lleva el 5 que se acaba de escribir y no el 9 del principio.
    std::cout << "{\"piano\":\"vista\",\"col3\":" << (int) pianoCells[3 * PianoRoll::kMaxNotas]
              << "}" << std::endl;

    //  Y LA BARRA ALCANZA TODO, que es la pregunta que la fila de tapas de
    //  compas contestaba sola: con 1, 2, 3 y 4 dibujadas, «se llega al compas
    //  4» era evidente. Con una ventana continua deja de serlo — una barra que
    //  se queda a un paso del final no se ve, porque el pulgar SI llega al
    //  filo: el suelo de `pulgar()` lo pone en `kGrueso` cuando la proporcion
    //  daria menos, asi que el dibujo miente en cuanto la vista es pequena
    //  contra el total.
    //
    //  CON DOS CIFRAS, que una se engaña: «el ultimo paso se ve» lo cumple
    //  igual una barra clavada en el final, y «el primero se ve» una clavada
    //  en el principio. Se arrastra el pulgar a los dos extremos y se pregunta
    //  por el paso de mas a la derecha y el de mas a la izquierda.
    {
        arrastraBarra (seqBarra, 1.0f);
        const int alFinal = seqBarra.getUltimo();
        arrastraBarra (seqBarra, 0.0f);
        const int alPrincipio = seqBarra.getPrimero();
        std::cout << "{\"piano\":\"barra\",\"ultimo\":" << alFinal
                  << ",\"primero\":" << alPrincipio
                  << ",\"total\":" << seqBarra.getTotal()
                  << ",\"visibles\":" << seqBarra.getVisibles() << "}" << std::endl;
    }

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
    //  Y CONTRA LA VENTANA QUE HAYA, por lo mismo que la de arriba: la goma
    //  recibe una COLUMNA y el codigo la convierte con `seqPrimerPaso`, asi que
    //  el paso absoluto que borra depende de donde este la ventana. Con «19»
    //  escrito a mano la comprobacion medi­a otro paso en cuanto la ventana dejo
    //  de empezar en un borde de compas.
    //  Y CON LA VENTANA PUESTA A MANO, que es la regla de esta casa y lo que
    //  la comprobacion de la barra obligo a escribir: esta medida daba por
    //  buena la ventana que dejara la de arriba, y en cuanto la barra empezo a
    //  medirse a los dos extremos la ultima la dejaba en CERO — y con la
    //  ventana en cero el paso que se frota y el testigo son la misma casilla,
    //  o sea que la prueba se borraba su propio testigo. Cada medida parte de
    //  un estado puesto, no del que dejo la anterior.
    arrastraBarra (seqBarra, 1.0f);
    const int baseGoma = juce::jlimit (0, kNumSteps - 4, seqPrimerPaso);
    const int frotado  = baseGoma + 3;
    pianoCellToggled (3, 9);           // testigo, fuera de la ventana
    pianoCellToggled (frotado, 5);     // el que se frota, columna 3 de la vista
    refreshPiano();
    if (pianoGrid.onBorrar) pianoGrid.onBorrar (3, 5);
    std::cout << "{\"piano\":\"goma\",\"base\":" << baseGoma
              << ",\"frotado\":" << (pattern[0][(size_t) frotado][0] ? 1 : 0)
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
    std::cout << "{\"piano\":\"encoge\",\"sel\":" << (seqPrimerPaso / AudioEngine::kBarSteps)
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
    seqPrimerPaso = 0;
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
    //  LA SELECCION, MOVER EN BLOQUE, COPIAR Y PEGAR.
    //
    //  Todo por el GESTO, que es donde vive: `pianoBanda` y `pianoMueveSel`
    //  llamadas por dentro se saltan el codigo que decide si el arrastre
    //  empieza DENTRO de la seleccion, que es lo unico que separa mover de
    //  volver a seleccionar.
    {
        //  Una figura que se lee de un vistazo: tres notas en columnas 1, 2 y 3
        //  con LARGOS DISTINTOS, para que moverla sin el largo se vea en el
        //  numero y no haya que creerselo.
        engine.clearPattern (0);
        for (int st = 0; st < kNumSteps; ++st)
            for (int q = 0; q < kNumPads; ++q) pattern[0][(size_t) st][(size_t) q] = false;
        pianoSel.clear();
        pianoPortapapeles.clear();
        pianoGrid.setHerramienta (PianoRoll::dibujar);

        const int fMed = filas / 2;
        const int semiMed = pianoBase + (filas - 1 - fMed);
        const int largos[3] = { 4, 8, 12 };
        for (int i = 0; i < 3; ++i)
        {
            pianoEscribe (1 + i, semiMed + i, true, largos[i]);
        }
        refreshPiano();

        //  1. LA BANDA coge lo que cubre y SOLO eso: se pone una cuarta nota
        //  fuera del rectangulo, y si la banda la coge la cifra lo dice.
        pianoEscribe (10, semiMed, true, 4);
        refreshPiano();

        pianoGrid.setHerramienta (PianoRoll::sel);
        float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
        punto (1, fMed,     ax, ay);
        punto (3, fMed - 2, bx, by);
        pianoGrid.gesto (ax, ay, false);
        pianoGrid.gesto (bx, by, true);
        pianoGrid.suelta();
        const int seleccionadas = (int) pianoSel.size();

        //  2. MOVER: se agarra una nota YA seleccionada y se arrastra dos
        //  columnas a la derecha y una fila arriba. Tienen que llegar las tres,
        //  con sus tres largos y sus distancias intactas.
        const int undoAntes = (int) undoStack.size();

        float cx = 0.0f, cy = 0.0f, dx = 0.0f, dy = 0.0f;
        punto (1, fMed,     cx, cy);
        punto (3, fMed - 1, dx, dy);
        pianoGrid.gesto (cx, cy, false);
        //  EL ARRASTRE, PASO A PASO Y NO DE UN SALTO.
        //
        //  Un dedo de verdad emite un evento por movimiento, y con UNO solo la
        //  comprobacion de «una sola entrada de deshacer» no puede fallar:
        //  medido, con un pushUndo por evento seguia saliendo 1. Una prueba que
        //  no se ha visto fallar es una linea que imprime OK.
        for (int k = 1; k <= 4; ++k)
        {
            const float t = (float) k / 4.0f;
            pianoGrid.gesto (cx + (dx - cx) * t, cy + (dy - cy) * t, true);
        }
        pianoGrid.suelta();
        moviendoSel = false;

        juce::String trasMover = "[";
        int largosOk = 0;
        for (int i = 0; i < 3; ++i)
        {
            const int st = 3 + i;   // 1+i movido dos columnas
            const bool hay = pattern[0][(size_t) st][(size_t) selectedPad];
            trasMover << (i ? "," : "") << (hay ? 1 : 0);
            if (hay && engine.getStepLen (0, st, selectedPad) == largos[i]) ++largosOk;
        }
        trasMover << "]";

        //  Y LA CUARTA SIGUE DONDE ESTABA: mover el bloque no puede arrastrar
        //  lo que no se selecciono.
        const int fueraQuieta = pattern[0][10][(size_t) selectedPad] ? 1 : 0;

        //  3. UNA SOLA ENTRADA DE DESHACER para el bloque entero. Deshacer un
        //  movimiento de tres notas tres veces no es deshacer, es contar.
        const int undoTrasMover = (int) undoStack.size() - undoAntes;

        //  4. COPIAR Y PEGAR, en otro compas. Relativo: pegar cae donde se
        //  mira y no donde se copio.
        pianoCopiaSel();
        const int copiadas = (int) pianoPortapapeles.size();
        seqPrimerPaso = AudioEngine::kBarSteps;
        engine.setPatternLength (0, 32);
        refreshPiano();
        pianoPegaSel();
        int pegadas = 0;
        for (int st = 16; st < 32; ++st)
            if (pattern[0][(size_t) st][(size_t) selectedPad]) ++pegadas;

        pianoGrid.setHerramienta (PianoRoll::dibujar);
        seqPrimerPaso = 0;

        std::cout << "{\"piano\":\"sel\",\"seleccionadas\":" << seleccionadas
                  << ",\"tras_mover\":" << trasMover
                  << ",\"largos_ok\":" << largosOk
                  << ",\"fuera_quieta\":" << fueraQuieta
                  << ",\"undo\":" << undoTrasMover
                  << ",\"copiadas\":" << copiadas
                  << ",\"pegadas\":" << pegadas << "}" << std::endl;
    }

    // ------------------------------------------------------------------
    //  EL ZOOM HORIZONTAL: cuantas columnas se ven y cuanto mide su celda.
    {
        engine.setPatternLength (0, 32);
        seqPrimerPaso = 0;
        //  POR LA TAPA y no poniendo el numero a mano, que es lo unico que
        //  mide la ESCALERA: el ciclo salta el paso que no cabe, y llamando a
        //  `pianoCols = 32` por dentro eso no se ve nunca. Tres pulsaciones,
        //  que es la vuelta completa del ciclo.
        pianoCols = AudioEngine::kBarSteps;
        refreshPiano(); resized();
        juce::String anchos = "[";
        juce::String cols   = "[";
        for (int i = 0; i < 3; ++i)
        {
            if (pianoZoomBtn.onClick) pianoZoomBtn.onClick();
            const int nc = pianoGrid.numPasos();
            const int w  = (nc > 0) ? (pianoGrid.getWidth() - PianoRoll::kGutter) / nc : 0;
            anchos << (i ? "," : "") << w;
            cols   << (i ? "," : "") << nc;
        }
        anchos << "]"; cols << "]";
        pianoCols = AudioEngine::kBarSteps;
        refreshPiano();

        std::cout << "{\"piano\":\"zoom\",\"cols\":" << cols
                  << ",\"ancho\":" << anchos << "}" << std::endl;
    }

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

    //  Y LA CUADRICULA SE DESPLAZA CON LAS NOTAS.
    //
    //  «Cuando arrastras la barra lateral, se desplazan las notas pero no las
    //  cuadriculas, con lo que puede dar a confundirse donde pone uno las notas
    //  siguiendo los pasos de los beat». Era exacto y la causa cabe en una
    //  linea: el fondo se teñia por la COLUMNA -`c % 4`- y `StepGrid` lo hace
    //  por el PASO desde el dia que la ventana es continua, con el parrafo que
    //  lo explica escrito ahi mismo. El piano nunca recibio el paso de la
    //  primera columna, asi que no tenia con que.
    //
    //  Y NINGUNA DE LAS CATORCE REGLAS DE `expo.py` PUEDE VERLO: una
    //  cuadricula que marca el contratiempo se maqueta perfecta -no solapa, no
    //  se sale, no lleva rotulo, no mide cero y esta traducida-. Es la familia
    //  de los cinco fallos del compas.
    //
    //  Se mide PINTANDO y sobre el pixel, que es donde vive: la rejilla se
    //  dibuja DOS veces -con sus lineas de compas y sin ellas- y se restan, asi
    //  que lo que sale son las lineas y nada mas. Preguntarle a la app en que
    //  columnas CREE que hay pulso seria repetir la constante en vez de medir,
    //  que es el fallo que `icono.py` ya cometio dos veces con la mascara del
    //  lanzador.
    //
    //  CON DOS CIFRAS, que es lo que separa las dos formas de escribirlo mal:
    //  un dibujo que no mira la ventana no se mueve, y uno que se mueve por
    //  otra razon tampoco vale. Las lineas se publican en PASOS ABSOLUTOS
    //  -primerPaso + columna, que es la unica conversion que hace falta- asi
    //  que la pregunta es una identidad y no un numero escrito aqui: TODAS
    //  tienen que caer en multiplos de cuatro, en las dos ventanas. Con el
    //  fallo puesto y la ventana en 2 salen en 6, 10 y 14.
    {
        openSheet (seqSheet, secButton);
        showSeqPage (seqPagePiano);
        engine.setPatternLength (0, 32);          // dos compases, para que la barra tenga donde ir

        auto lineasCon = [this] (int desde)
        {
            if (seqBarra.onMueve) seqBarra.onMueve (desde);
            refreshPiano();

            //  Y SE RESTAN DOS RENDERS, que es la unica forma exacta.
            //
            //  Los dos primeros intentos midieron OTRA COSA y los dos daban
            //  numeros, que es la peor forma de fallar. El primero barria la
            //  fila y=0 con el alfa en 8: una celda va `reduced (0.8f)`, asi
            //  que en el pixel de arriba deja el 20 % de su tinte -ocho
            //  unidades para una fila blanca y trece para un pulso- y lo que
            //  encontraba eran las celdas TEÑIDAS, `[0, 4, 8, 8, 12]`, con un
            //  cero que no lleva linea y un ocho repetido. El segundo miro la
            //  junta entre columnas a media altura -1.6 px transparentes- y
            //  ahi pinta lo unico que de verdad hay a media altura: las NOTAS.
            //  `[1, 3, 4, 5, 7, 8, 10, 12, 14]`. No hay una sola fila de esta
            //  imagen donde no pinte nadie mas.
            //
            //  Asi que se pinta la rejilla DOS veces -con sus lineas y sin
            //  ellas- y se restan: lo que cambia son las lineas y nada mas, sin
            //  umbral que elegir y sin dar por hecho que hay una fila libre. Es
            //  la misma pieza que `ZATI_Z_RECTA` en su dia. Donde MIRAR lo dice
            //  la app -`celdaAnchoPx` y `canalIzq`, que son las que dibujan- y
            //  el PASO sale de `seqPrimerPaso + columna`, que es la unica
            //  conversion que hace falta.
            juce::String s = "[";
            const int w = juce::jmax (1, pianoGrid.getWidth());
            const int h = juce::jmax (1, pianoGrid.getHeight());
            juce::Image con (juce::Image::ARGB, w, h, true);
            juce::Image raso (juce::Image::ARGB, w, h, true);
            { juce::Graphics g (con); pianoGrid.paintEntireComponent (g, false); }
            pianoGrid.sinCompases = true;
            { juce::Graphics g (raso); pianoGrid.paintEntireComponent (g, false); }
            pianoGrid.sinCompases = false;

            const float ancho = pianoGrid.celdaAnchoPx();
            bool first = true;
            for (int c = 1; c < pianoGrid.numPasos(); ++c)
            {
                //  Los dos pixeles de la junta: la linea va centrada en el
                //  borde, asi que a media unidad puede caer en cualquiera.
                bool cambia = false;
                for (int d = -1; d <= 1 && ! cambia; ++d)
                {
                    const int x = (int) std::lround ((float) pianoGrid.canalIzq()
                                                     + ancho * (float) c) + d;
                    if (x < 0 || x >= w) continue;
                    for (int y = 0; y < h && ! cambia; y += juce::jmax (1, h / 8))
                        cambia = (con.getPixelAt (x, y) != raso.getPixelAt (x, y));
                }
                if (cambia)
                {
                    s << (first ? "" : ",") << (seqPrimerPaso + c);
                    first = false;
                }
            }
            return s + "]";
        };

        const auto en0 = lineasCon (0);
        const auto en2 = lineasCon (2);
        std::cout << "{\"piano\":\"cuadricula\",\"ventana0\":" << en0
                  << ",\"ventana2\":" << en2 << "}" << std::endl;
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

    //  Y DOS CLIPS DE AUDIO EN LA LINEA DE TIEMPO, con valores distintos entre
    //  si por lo mismo que los cuatro bloqueos: con dos iguales, un cruce de
    //  campos dentro de la fila pasaria desapercibido. Dos y no uno, porque
    //  una lista que solo sabe guardar el primero se lee igual que una que
    //  funciona.
    clips.clear();
    clips.push_back ({ /*pad*/ 0,  /*pista*/ 1, /*compas*/ 3, /*desde*/ 100, /*largo*/ 4800, 0.75f });
    clips.push_back ({ /*pad*/ 16, /*pista*/ 2, /*compas*/ 7, /*desde*/ 250, /*largo*/ 9600, 0.50f });
    publicaClips();

    //  Y UN MAPA DE RANURAS RECONOCIBLE, que no es la identidad ni el vacio:
    //  con la fila en orden, «volvio» lo cumple igual un lector que no lee
    //  nada y deja el defecto puesto.
    ponEnRanura (0, 5); ponEnRanura (1, kSlotVacia); ponEnRanura (2, 1);
    ponEnRanura (3, kSlotVacia); ponEnRanura (4, kSlotVacia); ponEnRanura (5, 2);

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
    clips.clear();
    publicaClips();
    //  Y las ranuras, borradas a mano por lo mismo: si al volver siguen
    //  puestas no es que se hayan guardado, es que nadie las quito.
    for (int s = 0; s < kNumRanuras; ++s) ponEnRanura (s, kSlotVacia);

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
              << ",\"ranuras\":[" << slotFx[0][0] << "," << slotFx[0][1] << "," << slotFx[0][2] << ","
                                   << slotFx[0][3] << "," << slotFx[0][4] << "," << slotFx[0][5] << "]"
              << "}" << std::endl;

    //  Y SE BOMBEA UN TICK DE AUDIO ANTES DE PREGUNTARLE AL MOTOR. La tabla de
    //  clips se publica por intercambio de punteros -el hilo de mensajes deja
    //  la nueva en `pendingClips` y quien la ADOPTA es el hilo de audio, que es
    //  tambien quien mueve `clipsVivos`-, asi que en un escritorio sin tarjeta
    //  ese contador vale cero pase lo que pase: la primera version de esta
    //  comprobacion saco `motor 0` con el codigo perfecto y con el roto.
    //  Primero se duda de la prueba. Con el mismo ayudante que usa el banco de
    //  CPU se corre el camino entero -publicar, adoptar, contar- que es lo que
    //  se queria preguntar.
    bombeaAudioDePrueba();

    std::cout << "{\"clips\":" << (int) clips.size() << ",\"filas\":[";
    for (size_t i = 0; i < clips.size(); ++i)
    {
        const auto& c = clips[i];
        std::cout << (i ? "," : "") << "[" << c.pad << "," << c.pista << "," << c.compas
                  << "," << c.desde << "," << c.largo << "," << c.gain << "]";
    }
    std::cout << "],\"motor\":" << engine.numClips() << "}" << std::endl;

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
            //  Y LA MESA DE AYER: el pad en otro canal y su recorte movido,
            //  con los envios del canal 0 puestos. Un proyecto de otra epoca
            //  tiene que devolver los tres a lo que aquel dia significaban.
            engine.setPadCanal (p, 5);
            for (int fx = 0; fx < kNumFx; ++fx) engine.setPadRecorte (p, fx, 0.75f);
        }
        for (int fx = 0; fx < kNumFx; ++fx) engine.setCanalSend (0, fx, 0.5f);
        //  Y LOS EFECTOS DE AYER, en un canal que no es el cero: `fxp` y
        //  `eqc` son las dos propiedades que nacieron con los canales, asi que
        //  hace falta dejarlas MOVIDAS antes de abrir el viejo o «volvio»
        //  lo cumple tambien no haber tocado nada. Un proyecto sin ellas tiene
        //  que devolver los dieciseis al numero del mando, y uno con ellas al
        //  que trae escrito.
        engine.setFxParam (3, AudioEngine::kFxFlt, 0, -0.90f);
        engine.setFxParam (3, AudioEngine::kFxCmp, 0, -55.0f);
        engine.setEqBand  (5, 2, Eq5::kFreqDef[2], -9.0f);
        //  Y LA FILA DE EFECTOS VACIA antes de abrir el viejo. Un proyecto de
        //  otra epoca no lleva la propiedad `slots`, asi que tiene que volver
        //  con la fila DE SIEMPRE -la ranura s con el tipo s- y no con el
        //  defecto de hoy, que es vacia: lo que manda no es cual es el defecto
        //  de hoy sino como sonaba el dia que se guardo. Sin vaciarla antes,
        //  «volvio en orden» lo cumple tambien no haber tocado nada.
        for (int c = 0; c < kNumCanales; ++c)
        {
            canalActual = c;
            for (int s = 0; s < kNumRanuras; ++s) ponEnRanura (s, kSlotVacia);
        }
        ponCanalActual (0);
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
                  << ",\"envio0\":" << engine.sendDePad (0, 0)
                  << ",\"autocorte0\":" << (padSelfCut[0] ? 1 : 0)
                  << ",\"corte0\":" << padCut[0]
                  << ",\"gain20\":" << padGain[20]
                  << ",\"pan20\":" << padPan[20]
                  << ",\"corte20\":" << padCut[20]
                  << ",\"reves20\":" << (padReverse[20] ? 1 : 0)
                  << ",\"envio20\":" << engine.sendDePad (20, 0)
                  //  Y LA MITAD DEL PAD POR SEPARADO, que es la unica que se
                  //  puede HEREDAR desde que los envios son del canal. Lo que
                  //  llega a un efecto es `canalSend[canal] x padRecorte[pad]`,
                  //  y el primero es de la MAQUINA: un fichero de la epoca de
                  //  «cada pad va entero a todos» deja el canal 0 en uno y eso
                  //  alcanza a los sesenta y cuatro, tengan o no fila propia.
                  //  Lo que no puede pasar es que el pad 20 -que no esta en
                  //  ninguno de los cinco- vuelva con el 0.75 que el proyecto
                  //  ANTERIOR le dejo puesto, y eso lo dice esta cifra y no la
                  //  de arriba.
                  << ",\"recorte20\":" << engine.getPadRecorte (20, 0)
                  //  EL ENVIO AL ULTIMO TIPO, que es el que separa las dos
                  //  respuestas. La lista `sends` es POSICIONAL: un proyecto de
                  //  la epoca de seis trae seis numeros, y los tipos que no
                  //  existian entonces no sonaban, o sea CERO. Con la rama del
                  //  1.0f -la de un proyecto SIN la propiedad- los 64 pads
                  //  abririan con los cinco nuevos a tope.
                  << ",\"envio_nuevo\":" << engine.sendDePad (0, kNumFx - 1)
                  //  Y LAS DOS MITADES POR SEPARADO, que es lo unico que separa
                  //  «suena igual» de «suena igual por casualidad»: el recorte
                  //  del pad es lo que el fichero traia y el envio del canal lo
                  //  que `applyState` dedujo de que el fichero no traia mesa.
                  << ",\"recorte0\":" << engine.getPadRecorte (0, 0)
                  << ",\"csend0\":" << engine.getCanalSend (0, 0)
                  << ",\"canal20\":" << engine.getPadCanal (20)
                  << ",\"cancion\":" << celdasCancion
                  << ",\"vel0\":" << engine.getStepVel (0, 0, 0)
                  << ",\"roll0\":" << engine.getStepRoll (0, 0, 0)
                  //  Y LOS EFECTOS POR CANAL, que es la sexta rama. Se lee un
                  //  canal que NO es el cero a proposito: con el cero,
                  //  «volvio» lo cumple igual un lector que coge la primera
                  //  fila y la reparte a los dieciseis — o sea la rama vieja.
                  << ",\"flt3\":" << engine.getFxParam (3, AudioEngine::kFxFlt, 0)
                  << ",\"cmp3\":" << engine.getFxParam (3, AudioEngine::kFxCmp, 0)
                  << ",\"eq5\":"  << engine.getEqGain (5, 2)
                  << ",\"ranuras\":[" << slotFx[0][0] << "," << slotFx[0][1] << "," << slotFx[0][2] << ","
                                       << slotFx[0][3] << "," << slotFx[0][4] << "," << slotFx[0][5] << "]"
                  << "}" << std::endl;
    }
}

// ============================================================================
//  LAS SEIS RANURAS DE LA FILA DE EFECTOS. Ver Tests/ranuras.py.
//
//  SE MIDE POR EL GESTO Y NO POR EL CALLBACK, que es la leccion que este banco
//  ya pago cinco veces con el compas del piano: llamar a `ponEnRanura` por
//  dentro se salta justo el codigo que decide si una tapa abre el menu o
//  enciende un efecto, que es donde vive todo lo que esta tanda anade. Se
//  pulsan las tapas de verdad -`fxButtons[s]->onClick`, `ranuraBtns[f]->
//  onClick`- y se lee lo que quedo.
// ============================================================================
//  LA MESA ENTRE LOS PADS Y LOS EFECTOS. Ver Tests/canales.py.
//
//  NINGUNA DE LAS ONCE REGLAS DE `expo.py` PUEDE VER NADA DE ESTO: son fallos
//  de INDICE y de ESTADO, y un pad que manda al canal equivocado se maqueta
//  perfecto -no solapa, no se sale, no corta un rotulo, no mide cero y esta
//  traducido-. Es la familia de los cinco fallos del compas del piano.
//
//  SE MIDE POR EL GESTO Y NO POR EL CALLBACK, que es la misma leccion: llamar a
//  `setPadCanal` por dentro se salta justo el codigo que decide si la fila de
//  la cara sigue al pad, que es donde vive todo lo que esta tanda anade.
// ============================================================================
void MainComponent::auditCanales()
{
    auto fila  = [this] (int c)
    {
        juce::StringArray r;
        for (int s = 0; s < kNumRanuras; ++s) r.add (juce::String (slotFx[(size_t) c][(size_t) s]));
        return "[" + r.joinIntoString (",") + "]";
    };

    //  1. LA REJILLA DE CANALES MUEVE EL PAD, y la fila de la cara VA CON EL.
    //
    //  Con DOS cifras: a que canal fue el pad *y* que la fila de la cara sea la
    //  de ese canal. Solo la primera la cumple un `setPadCanal` al que no le
    //  sigue nadie -que es exactamente como estaba la app antes de esta tanda,
    //  con las seis ranuras globales- y solo la segunda la cumple una cara que
    //  cambia de fila sin mover el pad.
    for (auto& f : slotFx) f.fill (kSlotVacia);
    ponCanalActual (0);
    slotFx[0][0] = AudioEngine::kFxFlt;      // el canal 0 lleva FLT
    slotFx[3][0] = AudioEngine::kFxBit;      // y el 3, BIT
    selectPad (5);
    pulsaTapa (canalBtns[3]);
    const int canalDelPad = engine.getPadCanal (5);
    const juce::String filaTrasMover = fila (canalActual);

    //  2. CAMBIAR DE PAD CAMBIA LA FILA. Por el GESTO -un toque en el pad, que
    //     es lo que `padClicked` encadena- y no llamando a `selectPad`, que es
    //     donde el fallo no existe. Dos pads en dos canales distintos: la fila
    //     de la cara tiene que decir dos cosas distintas.
    engine.setPadCanal (2, 0);
    juce::MouseEvent me (juce::Desktop::getInstance().getMainMouseSource(),
                         {}, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                         pads[2], pads[2], juce::Time::getCurrentTime(),
                         {}, juce::Time::getCurrentTime(), 1, false);
    if (pads[2] != nullptr) pads[2]->mouseDown (me);
    const juce::String filaPad2 = fila (canalActual);
    const int canalPad2 = canalActual;

    juce::MouseEvent me5 (juce::Desktop::getInstance().getMainMouseSource(),
                          {}, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                          pads[5], pads[5], juce::Time::getCurrentTime(),
                          {}, juce::Time::getCurrentTime(), 1, false);
    if (pads[5] != nullptr) pads[5]->mouseDown (me5);
    const juce::String filaPad5 = fila (canalActual);
    const int canalPad5 = canalActual;

    //  3. UN INSERTO ES DE UN CANAL Y UN ENVIO ES DE TODOS.
    //
    //  Las dos mitades, y las dos hacen falta: solo la primera la cumple una
    //  regla que mueve TODO -y entonces dos canales no pueden compartir un
    //  delay, que es lo contrario de lo que un envio significa- y solo la
    //  segunda la cumple una que no mueve nada, y entonces dos canales tendrian
    //  dos interruptores del mismo compresor.
    for (auto& f : slotFx) f.fill (kSlotVacia);
    ponCanalActual (0); ponEnRanura (0, AudioEngine::kFxCmp);
    ponCanalActual (4); ponEnRanura (0, AudioEngine::kFxCmp);
    const juce::String insertoDe0 = fila (0), insertoDe4 = fila (4);

    for (auto& f : slotFx) f.fill (kSlotVacia);
    ponCanalActual (0); ponEnRanura (0, AudioEngine::kFxDly);
    ponCanalActual (4); ponEnRanura (0, AudioEngine::kFxDly);
    const juce::String envioDe0 = fila (0), envioDe4 = fila (4);

    //  3c. Y EL AJUSTE ES DEL CANAL, que es lo que hace falta de verdad desde
    //      que un inserto puede estar en los dieciseis: con 3a y 3b dando ya la
    //      MISMA forma -`[7,...]` en los dos-, solas no separan nada. Lo que
    //      las separa es si el NUMERO viaja con la fila.
    //
    //      TRES cifras, y la tercera es la que impide que «sale a cero» lo
    //      cumpla un codigo que BORRA el ajuste al cambiar de canal. Y por el
    //      MANDO -`macroCtrl2` con `sendNotificationSync`, que es el
    //      equivalente de `->onClick` en una tapa- y no llamando a
    //      `setFxParam`: lo que se prueba es la ventana, y la ventana vive en
    //      el callback.
    for (auto& f : slotFx) f.fill (kSlotVacia);
    ponCanalActual (0); ponEnRanura (0, AudioEngine::kFxCmp);
    ponCanalActual (4); ponEnRanura (0, AudioEngine::kFxCmp);
    ponCanalActual (0);
    focusFx (AudioEngine::kFxCmp);
    macroCtrl2.setValue (6.0, juce::sendNotificationSync);
    const double ajusteEn0 = fxParam (AudioEngine::kFxCmp, 1).getValue();
    ponCanalActual (4);
    const double ajusteEn4 = fxParam (AudioEngine::kFxCmp, 1).getValue();
    ponCanalActual (0);
    const double ajusteVuelve = fxParam (AudioEngine::kFxCmp, 1).getValue();

    //  4. VACIAR UNA RANURA APAGA SU EFECTO **SOLO SI NO LE QUEDA OTRA**. Un
    //     envio puede vivir en tres canales, y quitarlo de uno no lo deja sin
    //     tapa: apagarlo ahi seria callar un delay que se sigue viendo.
    for (auto& f : slotFx) f.fill (kSlotVacia);
    ponCanalActual (0); ponEnRanura (0, AudioEngine::kFxDly);
    ponCanalActual (4); ponEnRanura (0, AudioEngine::kFxDly);
    ponCanalActual (0);
    setFxEnabled (AudioEngine::kFxDly, true);
    ponEnRanura (0, kSlotVacia);                       // sigue en el canal 4
    const int trasQuitarUna = fxEncendido (AudioEngine::kFxDly) ? 1 : 0;
    ponCanalActual (4);
    ponEnRanura (0, kSlotVacia);                       // ya no queda ninguna
    const int trasQuitarLaUltima = fxEncendido (AudioEngine::kFxDly) ? 1 : 0;

    //  Y LA OTRA MITAD: UN INSERTO SE APAGA SIEMPRE. Desde esta tanda puede
    //  estar en dos canales a la vez -es lo que la separa de un envio- y el
    //  que se va es el de ESTE canal, con su propia instancia en el motor:
    //  dejarlo encendido es una tapa menos y un compresor que sigue
    //  comprimiendo. Con la guardia del envio puesta tambien aqui, sale 1.
    for (auto& f : slotFx) f.fill (kSlotVacia);
    ponCanalActual (0); ponEnRanura (0, AudioEngine::kFxCmp);
    ponCanalActual (4); ponEnRanura (0, AudioEngine::kFxCmp);
    ponCanalActual (0);
    setFxEnabled (AudioEngine::kFxCmp, true);
    ponEnRanura (0, kSlotVacia);                       // sigue en el canal 4
    const int insertoTrasQuitar = fxEncendido (AudioEngine::kFxCmp) ? 1 : 0;
    ponCanalActual (4); setFxEnabled (AudioEngine::kFxCmp, false);

    //  5. EL FADER DEL CANAL LLEGA AL MOTOR, por la TIRA de la mesa y no
    //     llamando a `setCanalGain`: lo que se prueba es el camino.
    showMixPage (mixPageCanales);
    if (canFaders[6] != nullptr) canFaders[6]->setValue (-6.0, juce::sendNotificationSync);
    const float ganCanal6 = engine.getCanalGain (6);
    if (canMutes[6] != nullptr) { canMutes[6]->setToggleState (true, juce::dontSendNotification);
                                  if (canMutes[6]->onClick) canMutes[6]->onClick(); }
    const int muteCanal6 = engine.getCanalMute (6) ? 1 : 0;

    //  6. EL BANCO DE LA REJILLA: que se llegue a los dieciseis de detras.
    //
    //  Desde que hay treinta y dos canales la rejilla sigue siendo de cuatro
    //  por cuatro -dieciseis en fila estan medidos y no caben, 26 px en 280- y
    //  lo que crece es el numero de bancos. Con DOS cifras, que una se engaña:
    //  el chip mueve la rejilla *y* NO cambia el canal del pad. Solo la primera
    //  la cumple un chip que ademas reasigna -o sea pasear seria tocar- y solo
    //  la segunda la cumple un chip muerto.
    //
    //  Y POR EL GESTO, con la ficha ABIERTA: la rejilla la coloca `resized()`,
    //  asi que preguntar por los limites con el selector cerrado es preguntar
    //  por los que `abreCanalPicker` acaba de vaciar.
    selectPad (5);
    engine.setPadCanal (5, 0);
    abreCanalPicker (true);
    pulsaTapa (canalBankBtns[1]);
    const int canalTrasPasear = engine.getPadCanal (5);
    //  La primera celda VISIBLE de la rejilla: con el banco B tiene que ser el
    //  canal 17 (indice 16) y no el 01.
    int primeraVisible = -1;
    juce::Rectangle<int> celdaCanal;
    for (int i = 0; i < canalBtns.size(); ++i)
        if (auto* b = canalBtns[i])
            if (b->isVisible() && ! b->getBounds().isEmpty() && primeraVisible < 0)
            {
                primeraVisible = i;
                celdaCanal = b->getBounds();
            }
    pulsaTapa (canalBtns[20]);
    const int canalTrasElegir = engine.getPadCanal (5);
    abreCanalPicker (false);

    //  Y LA REJILLA SE ABRE DONDE ESTA EL PAD, que es la otra mitad y la que
    //  el chip no puede decir: el pad acaba de irse al canal 21 -indice 20, o
    //  sea banco B- asi que al VOLVER a abrir la primera celda tiene que ser
    //  la 16 y no la 0. Sin el arrastre de `abreCanalPicker`, el selector abre
    //  en el 1-16 con NINGUNA tapa encendida, que es un menu que no dice donde
    //  estas. Se mide reabriendo y no llamando a `ponCanalBanco`, que es justo
    //  donde el fallo no existe.
    //  Y EL BANCO SE DEVUELVE AL A ANTES DE REABRIR, o la medida no puede
    //  fallar: la comprobacion de arriba dejo el selector en el banco B, asi
    //  que reabrir lo encontraria alli con el arrastre puesto y sin el. Primero
    //  se duda de la prueba.
    pulsaTapa (canalBankBtns[0]);
    abreCanalPicker (true);
    int reabrePrimera = -1;
    for (int i = 0; i < canalBtns.size() && reabrePrimera < 0; ++i)
        if (auto* b = canalBtns[i])
            if (b->isVisible() && ! b->getBounds().isEmpty())
                reabrePrimera = i;

    //  Y CUANTAS TAPAS QUEDAN VIVAS, que es la mitad que NINGUNA regla de
    //  `expo.py` puede ver y por la que esta cifra existe. Son treinta y dos
    //  tapas para dieciseis celdas, asi que `resized()` tiene que APAGARLAS *Y*
    //  vaciarles los limites antes de colocar las del banco que toca — la regla
    //  que tuvo a SEGUIR visible y de 0x0 desde el primer dia.
    //
    //  El banco no la veia: su pantalla `canal` abre el selector en el banco A y
    //  no lo mueve nunca, y `abreCanalPicker (false)` vacia los limites al
    //  cerrar, asi que las dieciseis de detras jamas llegan a tener unas
    //  coordenadas que quedarse. Hace falta VOLVER de un banco al otro con la
    //  ficha abierta, y eso solo pasa aqui. Medido: quitando el `setBounds ({})`
    //  las 1400 corridas de `expo.py` dan CERO y RESIDUO a cero — o sea que la
    //  regla estaba escrita, era necesaria, y no la comprobaba nadie.
    pulsaTapa (canalBankBtns[0]);
    int bancoVivas = 0;
    for (auto* b : canalBtns)
        if (b != nullptr && b->isVisible() && ! b->getBounds().isEmpty())
            ++bancoVivas;
    abreCanalPicker (false);

    std::cout << "{\"canales\":" << kNumCanales
              << ",\"bancos\":" << kNumCanalBancos
              << ",\"banco_primera\":" << primeraVisible
              << ",\"banco_celda\":\"" << celdaCanal.getWidth() << "x" << celdaCanal.getHeight() << "\""
              << ",\"banco_pasear\":" << canalTrasPasear
              << ",\"banco_elegir\":" << canalTrasElegir
              << ",\"banco_reabre\":" << reabrePrimera
              << ",\"banco_vivas\":" << bancoVivas
              << ",\"canal_del_pad\":" << canalDelPad
              << ",\"fila_tras_mover\":" << filaTrasMover
              << ",\"canal_pad2\":" << canalPad2 << ",\"fila_pad2\":" << filaPad2
              << ",\"canal_pad5\":" << canalPad5 << ",\"fila_pad5\":" << filaPad5
              << ",\"inserto0\":" << insertoDe0 << ",\"inserto4\":" << insertoDe4
              << ",\"envio0\":" << envioDe0 << ",\"envio4\":" << envioDe4
              << ",\"ajuste0\":" << juce::String (ajusteEn0, 2)
              << ",\"ajuste4\":" << juce::String (ajusteEn4, 2)
              << ",\"ajuste_vuelve\":" << juce::String (ajusteVuelve, 2)
              << ",\"tras_quitar_una\":" << trasQuitarUna
              << ",\"tras_quitar_ultima\":" << trasQuitarLaUltima
              << ",\"inserto_tras_quitar\":" << insertoTrasQuitar
              << ",\"gan_canal6\":" << ganCanal6
              << ",\"mute_canal6\":" << muteCanal6
              << "}" << std::endl;
}

void MainComponent::auditRanuras()
{
    //  0. LOS DIECISEIS CANALES NACEN EN EL MISMO SITIO.
    //
    //  Desde que un inserto es de CADA canal, `fxP` son dieciseis filas de
    //  sesenta y tres numeros y el constructor las llena de `kFxDef`. Un
    //  `std::array` con `{}` deja las quince de detras a CERO, y cero es un
    //  valor valido en los tres parametros de casi todos los tipos: es el
    //  fallo de `notaViva` y el del cero de `padAncho`, contado en 1008
    //  casillas. El sintoma seria que el compresor del canal 7 abre con umbral
    //  0 dB y ratio 1, o sea SIN COMPRIMIR, mientras la ficha dice lo que dice
    //  el canal 0.
    //
    //  Y LO PRIMERO DE TODO, que es donde esta medida se equivoco antes de
//  acertar: al final de la funcion salio `30 de 336` con el codigo
//  perfecto — las comprobaciones de arriba mueven mandos, y un canal que
//  alguien acaba de tocar no dice ya como NACIO. Se pregunta antes de
//  tocar nada.
//
//  Se cuenta por (canal, tipo) y contra el canal CERO —que es donde
    //  aterriza todo lo que ya estaba medido— y no contra `kFxDef` copiada
    //  aqui: eso seria la tabla comparandose consigo misma, que es como
    //  `Tests/icono.py` dio verde dos veces con la mascara del lanzador rota.
    //  Los cinco ENVIOS resuelven al canal cero por `canalDeParam`, asi que
    //  cuentan y salen iguales por construccion — que es exactamente lo que
    //  dicen ser.
    int canalesRaros = 0;
    for (int c = 0; c < kNumCanales; ++c)
        for (int f = 0; f < kNumFx; ++f)
        {
            bool igual = true;
            for (int par = 0; par < 3; ++par)
                if (std::abs (engine.getFxParam (c, f, par)
                            - engine.getFxParam (0, f, par)) > 1.0e-6f)
                    igual = false;
            if (! igual) ++canalesRaros;
        }

    auto mapa = [this]
    {
        juce::StringArray r;
        for (int s = 0; s < kNumRanuras; ++s) r.add (juce::String (slotFx[0][(size_t) s]));
        return "[" + r.joinIntoString (",") + "]";
    };

    //  1. UNA RANURA VACIA ABRE EL MENU, y una llena NO.
    //
    //  Con DOS cifras y no una: «se abrio» lo cumple igual un menu que se abre
    //  siempre, que es como se escribe mal la primera version de esto - y
    //  entonces no habria forma de encender un efecto desde la cara.
    for (int s = 0; s < kNumRanuras; ++s) ponEnRanura (s, kSlotVacia);
    abreMenuRanura (-1);
    pulsaTapa (fxButtons[0]);
    const int menuTrasVacia = ranuraEditada;
    abreMenuRanura (-1);

    ponEnRanura (0, 0);
    pulsaTapa (fxButtons[0]);
    const int menuTrasLlena = ranuraEditada;
    const int encendioAlTocar = fxEncendido (0) ? 1 : 0;
    abreMenuRanura (-1);
    setFxEnabled (0, false);

    //  2. ELEGIR EN EL MENU LLENA LA RANURA, y desde la cara ya no se cambia:
    //     esa tapa pasa a encender y apagar. Es la ACCION UNICA que se pidio.
    for (int s = 0; s < kNumRanuras; ++s) ponEnRanura (s, kSlotVacia);
    pulsaTapa (fxButtons[2]);                       // el «+» de la ranura 2
    pulsaTapa (ranuraBtns[4]);                      // se elige BIT
    const juce::String trasElegir = mapa();
    const int menuTrasElegir = ranuraEditada;   // se cierra sola
    pulsaTapa (fxButtons[2]);                       // y ahora ese boton enciende
    const int enciendeDespues = fxEncendido (4) ? 1 : 0;
    const juce::String mapaDespues = mapa();    // que no ha cambiado
    setFxEnabled (4, false);

    //  3. UN TIPO, UNA RANURA. Poner en la 0 un tipo que ya estaba en la 3
    //     tiene que DEJAR LA 3 VACIA: dos ranuras del mismo tipo serian dos
    //     ventanas al mismo aparato del motor.
    for (int s = 0; s < kNumRanuras; ++s) ponEnRanura (s, s);
    abreMenuRanura (0);
    pulsaTapa (ranuraBtns[3]);
    const juce::String trasMover = mapa();

    //  4. VACIAR UNA RANURA APAGA SU EFECTO. Un efecto encendido cuya tapa
    //     desaparece sigue sonando y no hay donde tocarlo.
    for (int s = 0; s < kNumRanuras; ++s) ponEnRanura (s, s);
    setFxEnabled (3, true);
    const int antesDeVaciar = fxEncendido (3) ? 1 : 0;
    ponEnRanura (3, kSlotVacia);
    const int trasVaciar = fxEncendido (3) ? 1 : 0;

    //  5. LA REJILLA, Y NO SOLO LA DE HOY.
    //
    //  Con once tipos la rejilla son tres columnas y cuatro filas en las siete
    //  pantallas, o sea que la regla que la decide no se puede ver fallar. Y
    //  el dia que sean veintiuno son SIETE filas, que apaisado no caben: la
    //  tarjeta da 370 px y siete piden 460. Asi que se publica lo que
    //  `menuRanuraColumnas` contesta para el numero de HOY y para veintiuno,
    //  con la geometria de esta ventana — que es la funcion de verdad, la
    //  misma que llama `resized()`, y no una formula repetida en el script.
    const auto zonaR    = safeArea();
    const int  topeR    = altoTarjeta (zonaR);
    const int  anchoR   = anchoTarjetaInterior (zonaR.getWidth());
    auto forma = [&] (int n)
    {
        const int c = menuRanuraColumnas (n, topeR, anchoR, true);
        const int f = (n + c - 1) / c;
        return juce::String (c) + "x" + juce::String (f) + ":"
                 + juce::String (menuRanuraPide (f, true)) + ":"
                 + juce::String (anchoR / c);
    };

    //  Y QUE EL MOTOR Y SU MANDO ARRANQUEN EN EL MISMO NUMERO. `kFxDef` en el
    //  motor y `fxDefs[f].spec[p].def` en la cara son la misma regla escrita
    //  dos veces, y un control y su motor contando cosas distintas es el fallo
    //  que ya costo una medida con el corte del pad.
    int defectosQueNoCuadran = 0;
    for (int f = 0; f < kNumFx; ++f)
        for (int p = 0; p < 3; ++p)
            if (std::abs (AudioEngine::kFxDef[f][p] - (float) fxDefs[f].spec[p].def) > 0.001f)
                ++defectosQueNoCuadran;

    //  Y LOS NOMBRES DE PARAMETRO QUE LA APP VA A PEDIRLE A `T()`.
    //
    //  Pasan por `T()` desde `fxDefs[f].param[pi]`, o sea por VARIABLE, y
    //  `Tests/lang.py` recoge los literales escritos dentro de un `T ("...")`:
    //  no puede verlos. Asi estuvo «TONE» -CTRL 2 de DRV- sin fila en la tabla
    //  desde que existe ese efecto, diciendo lo mismo en las cuatro
    //  compilaciones. Con veinte nombres mas por venir, eso deja de ser un
    //  descuido y pasa a ser una clase de fallo.
    juce::String claves, nombres;
    for (int f = 0; f < kNumFx; ++f)
    {
        nombres += juce::String (fxDefs[f].name);
        if (f < kNumFx - 1) nombres += ",";
        for (int p = 0; p < 3; ++p)
        {
            claves += juce::String (fxDefs[f].param[p]);
            if (f < kNumFx - 1 || p < 2) claves += ",";
        }
    }

    std::cout << "{\"ranuras\":1"
              << ",\"tipos\":"     << kNumFx
              << ",\"params\":\""   << claves  << "\""
              << ",\"nombres\":\""  << nombres << "\""
              << ",\"forma\":\""          << forma (kNumFx) << "\""
              << ",\"forma21\":\""        << forma (21) << "\""
              << ",\"tope_tarjeta\":"    << topeR
              << ",\"defectos_cruzados\":" << defectosQueNoCuadran
              << ",\"canales\":"           << kNumCanales
              << ",\"canales_raros\":"     << canalesRaros
              << ",\"menu_tras_vacia\":"   << menuTrasVacia
              << ",\"menu_tras_llena\":"   << menuTrasLlena
              << ",\"enciende_al_tocar\":" << encendioAlTocar
              << ",\"tras_elegir\":\""     << trasElegir << "\""
              << ",\"menu_tras_elegir\":"  << menuTrasElegir
              << ",\"enciende_despues\":"  << enciendeDespues
              << ",\"mapa_despues\":\""    << mapaDespues << "\""
              << ",\"tras_mover\":\""      << trasMover << "\""
              << ",\"antes_de_vaciar\":"   << antesDeVaciar
              << ",\"tras_vaciar\":"       << trasVaciar
              << "}" << std::endl;
}

// ============================================================================
//  LA CUENTA ATRAS Y EL METRONOMO. Ver Tests/cuenta.py.
//
//  Las dos cosas existian a medias: el clic solo tenia tapa en la vista de
//  audio de CANCION y grabar lo FORZABA, y `armaCuentaAtras (1)` estaba escrito
//  UNA vez en toda la app -un compas, clavado, sin opcion- y solo en el camino
//  de grabar al arreglo. Grabar de normal no tenia ninguna de las dos.
//
//  Se mide POR LA TAPA y no poniendo el numero por dentro, que es lo unico que
//  recorre el camino de verdad: `cuentaButtons[i]->onClick` es lo que escribe
//  la preferencia, y llamar a `saveCuentaPref` a mano se lo salta.

// ============================================================================
//  LA FILA DEL RACK: de que familia es cada una, y que hay dentro.
//
//  Ninguna de las diez reglas de `expo.py` puede ver nada de esto. Una fila que
//  dibuja un envio donde hay un inserto se maqueta perfecta: no solapa, no se
//  sale, no corta el rotulo, no mide cero y esta traducida. Es la familia de
//  los cinco fallos del compas del piano, otra vez.
//
//  Se mide por lo que la fila ACABA teniendo puesto -la propiedad del fader,
//  que es lo que lee el pintor- y no llamando a `AudioEngine::sustituye` dos
//  veces: eso compararia la tabla consigo misma y saldria verde con el rack
//  dibujando lo que le diera la gana.
// ============================================================================
void MainComponent::auditRack()
{
    //  1. LA FAMILIA, EN LOS ONCE TIPOS. Se pone el tipo `f` en la ranura 0 y
    //     se mira que dice la fila del rack de ella — en palabras, que es lo
    //     que lee TalkBack y lo unico que la app afirma sobre esto.
    int mal = 0;
    juce::StringArray dibujo, marcas;
    for (int f = 0; f < kNumFx; ++f)
    {
        ponEnRanura (0, f);
        refreshRack();
        const bool dice = rackSends[0]->getTitle().contains (T ("SUSTITUYE"));
        dibujo.add (dice ? "1" : "0");
        if (dice != AudioEngine::sustituye (f)) ++mal;

        //  Y LA MARCA DEL CANALON, que es la mitad que se VE. Lo de arriba es
        //  el nombre accesible -lo que lee TalkBack- y lo escribe `refreshRack`;
        //  esto lo escribe `refrescaRanuras`, o sea otro camino. Que los dos
        //  digan lo mismo es lo unico que separa «la fila lo dice» de «una de
        //  las dos ventanas se quedo vieja».
        const int id = (int) rackSlotBtns[0]->getProperties().getWithDefault ("icono", 0);
        marcas.add (juce::String (id == (int) Iconos::Id::inserto ? 1
                                : id == (int) Iconos::Id::envio   ? 0 : -1));
    }

    //  2. LA MINIATURA LEE LOS NUMEROS DE AHORA, con DOS cifras.
    //
    //  Los once tienen que CAMBIAR al mover un mando y salir IDENTICOS sin
    //  tocar nada. Solo lo primero lo cumple una miniatura que dibuja ruido, y
    //  solo lo segundo un icono fijo — que es exactamente lo que habia antes.
    //  Y SE MIDE EN EL PLATO, que es donde vive: `refreshMacroValues` la
    //  alimenta con los tres mandos del efecto que tengas enfocado.
    //  Y MANDO A MANDO, que es lo que la version anterior no podia ver.
    //
    //  Movia los TRES y le bastaba con que UNO cambiara el dibujo, asi que
    //  CINCO mandos que no mueven nada llevaban ahi desde el primer dia sin que
    //  ninguna regla pudiera decirlo. Ahora se mueve uno cada vez y sale una
    //  LISTA, que se contrasta contra la que la app DECLARA
    //  (`FxVisor::mandosDe`): es la misma decision que la marca `valor` de los
    //  iconos — una lista de excepciones escrita en el script solo sabe medir
    //  una de las cuatro compilaciones, y ademas compararia la tabla consigo
    //  misma.
    //
    //  Solo el MANDO 0 y el 1: el 2 es MIX, que es el interruptor de la ranura
    //  y no una forma — un visor que cambiara con el no estaria dibujando el
    //  efecto sino su fader.
    int cambian = 0, quietos = 0, discrepan = 0;
    juce::StringArray medidos, dichos;
    for (int f = 0; f < kNumFx; ++f)
    {
        if (fxTraeCara (f))                       // el EQ trae la grande
        {
            ++cambian; ++quietos;
            medidos.add ("-1"); dichos.add ("-1");
            continue;
        }
        ponEnRanura (0, f);
        //  `focusFx` y no `focusedFx = f`: es quien le pone a los tres mandos
        //  el RANGO de este efecto. Sin el, un mando conserva el del tipo
        //  anterior y moverlo a su minimo y su maximo mide otra cosa.
        focusFx (f);

        const auto base = platoMini.puntos();
        refreshMacroValues();
        if (base == platoMini.puntos()) ++quietos;

        //  Y CON EL OTRO MANDO EN VARIOS SITIOS, que es donde esta medida se
        //  equivoco en su primera corrida — la undecima vez en este banco.
        //
        //  Saco que la RESONANCIA de FLT no mueve el dibujo y era verdad: se
        //  medía con el BARRIDO en su defecto, o sea dentro de la zona muerta
        //  de 0.03 que `barridoDe` declara, y ahi el filtro esta APAGADO y
        //  dibuja una raya plana pase lo que pase con la resonancia. Eso no es
        //  un mando muerto, es un mando medido con el efecto apagado. La
        //  pregunta que `mandosDe` contesta es «¿cabe en el eje de este
        //  visor?», asi que basta con que EXISTA un sitio del otro mando donde
        //  se vea. Primero se duda de la prueba.
        //  Y SE MUEVE EL MANDO, no el parametro. Esta medida escribia en
        //  `fxParam` -el deslizador escondido que guarda el valor- y llamaba
        //  despues a `refreshMacroValues`, o sea a la funcion que rehace la
        //  curva. Un dedo no hace eso: arrastra el mando, y de ahi sale
        //  `macroMoved`, que NO la rehacia. Asi que el visor llevaba tandas
        //  sin actualizarse al girar un mando -se veia en el telefono, y sólo
        //  cambiaba al salir del efecto y volver- con esta comprobacion en
        //  verde, porque preguntaba justo donde el fallo no existe.
        //
        //  `sendNotificationSync` es el equivalente de `->onClick` en una
        //  tapa: dispara el callback de la app, que es donde vive lo que hay
        //  que medir. Lo que se salta es el mapeo pixel->valor, que es de
        //  JUCE y no nuestro.
        juce::Slider* mk[2] = { &macroCtrl1, &macroCtrl2 };
        int mide = 0;
        for (int p = 0; p < 2; ++p)
        {
            auto& mando = *mk[p];
            auto& otro  = *mk[1 - p];
            const double antesM = mando.getValue(), antesO = otro.getValue();
            bool movio = false;

            for (double ctx : { antesO, otro.getMinimum(), otro.getMaximum() })
            {
                otro.setValue (ctx, juce::sendNotificationSync);
                const auto ref = platoMini.puntos();
                for (double v : { mando.getMinimum(), mando.getMaximum() })
                {
                    mando.setValue (v, juce::sendNotificationSync);
                    movio = movio || (platoMini.puntos() != ref);
                }
                mando.setValue (antesM, juce::sendNotificationSync);
            }
            otro.setValue (antesO, juce::sendNotificationSync);
            if (movio) mide |= (1 << p);
        }
        if (mide != 0) ++cambian;

        const auto md = FxVisor::mandosDe (f);
        const int  di = (md.p0 ? 1 : 0) | (md.p1 ? 2 : 0);
        if (mide != di) ++discrepan;
        medidos.add (juce::String (mide));
        dichos .add (juce::String (di));
    }

    //  3. LA CAPA VIVA: que se mueva con señal Y que se quede quieta sin ella.
    //
    //  Es la unica pregunta que separa las dos formas de escribir esto mal, y
    //  las dos se dibujan preciosas: una capa que pinta RUIDO se mueve con
    //  señal y tambien sin ella, y una que es un adorno no se mueve con
    //  ninguna. Con una sola cifra las dos pasan.
    //
    //  Y SE MIDE POR EL CAMINO DE VERDAD: se publica un sonido en el pad 0, se
    //  le abre el envio al tipo que toca, se bombean bloques -que es lo que el
    //  aparato habria entregado, y en un escritorio no hay aparato- y se lee lo
    //  que el visor acaba teniendo. Llamar a `setMuestras` con un vector
    //  inventado mediria el dibujo y no la captura, que es justo el eslabon
    //  nuevo.
    //  Mil veinticuatro, que es lo que `Analizador` necesita para su ventana:
    //  con menos, la rama de frecuencia se rinde y las dos lecturas saldrian
    //  identicas — o sea la prueba diria «no se mueve» por no haberla
    //  alimentado.
    constexpr int kFxScopeBanco = Analizador::kFft;
    int mueven = 0, quietosSinSenal = 0, medibles = 0;
    //  Y QUIENES, que un contador no dice cual redibujar. Es la misma
    //  leccion que el `1 @35,320 39x36` del residuo: el hallazgo nombra.
    juce::StringArray vivoMudo, vivoRuido;
    {
        //  Un ruido, que es lo unico que llena TODAS las columnas de un
        //  espectro: con un seno, cuarenta y siete de las cuarenta y ocho se
        //  quedarian en el suelo y «se movio» dependeria de en cual cayo.
        auto ruido = [] ()
        {
            auto* sb = new SampleBuffer();
            const int n = 24000;
            sb->buffer.setSize (2, n);
            juce::Random r (20260904);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                    sb->buffer.setSample (c, i, 0.6f * (r.nextFloat() * 2.0f - 1.0f));
            sb->sourceSampleRate = 48000.0;
            return SampleBuffer::Ptr (sb);
        };

        closeAllSheets();
        resized();

        std::vector<float> pre ((size_t) kFxScopeBanco), post ((size_t) kFxScopeBanco);

        for (int f = 0; f < kNumFx; ++f)
        {
            if (fxTraeCara (f)) continue;          // el EQ trae la curva grande
            ++medibles;

            ponEnRanura (0, f);
            focusedFx = f;
            for (int p = 0; p < 3; ++p)
                engine.setFxParam (canalActual, f, p, (float) fxParam (f, p).getValue());
            engine.setFxParam (canalActual, f, 2, 1.0f);
            //  `refreshMacroValues` y no `refrescaPlato`, que es donde esta
            //  medida se equivoco primero: la segunda enseña u oculta la curva
            //  grande del EQ y no toca el visor, asi que el plato se quedaba
            //  con el tipo de la comprobacion anterior y `miraFx` con el suyo.
            //  Lo canto la propia linea de diagnostico -«tipo 10 mirado 10» en
            //  las diez vueltas- y por eso solo se movia el ultimo.
            refrescaPlato();
            refreshMacroValues();
            resized();

            auto lee = [&] (bool conSenal, int tics)
            {
                //  Los 64 pads nacen en el canal 0, asi que cerrar el envio
                //  del canal los cierra a los 64: es lo que la mesa hace.
                engine.setCanalSend (0, f, 0.0f);
                if (conSenal)
                {
                    engine.setPadGain (0, 1.0f);
                    engine.setCanalSend (0, f, 1.0f);
                    engine.publishSample (0, ruido());
                }
                for (int i = 0; i < tics; ++i)
                {
                    if (conSenal) engine.postNoteOn (0, 1.0f);
                    bombeaAudioDePrueba();
                    engine.copyFxScope (pre.data(), post.data(), kFxScopeBanco);
                    const int dd = dinamicaDeFx (f);
                    platoMini.setMuestras (pre.data(), post.data(), kFxScopeBanco, 33.0,
                                           dd >= 0 ? engine.getDynReduccion (canalActual, dd) : 0.0f,
                                           engine.getLfoFase (canalActual, f));
                }
                platoMini.ponVivo (engine.fxScopeVivo());
                return std::make_pair (platoMini.vivos(),
                                       juce::Point<float> (platoMini.puntoX(), platoMini.puntoY()));
            };

            //  SE DEJA ASENTAR ANTES DE PREGUNTAR SI ESTA QUIETO, que es donde
            //  esta medida se equivoco: leia dos veces con cinco tics de por
            //  medio y sacaba «cinco de diez dibujan ruido» con el codigo
            //  perfecto. No dibujaban ruido — estaban CAYENDO. La caida del
            //  analizador es exponencial y no llega al suelo nunca, y la cola
            //  desplaza una columna cada 42 ms, asi que dos lecturas seguidas
            //  despues de un golpe salen distintas porque tienen que salirlo.
            //
            //  Ochenta tics son 2.6 s: mas que los dos segundos que la ventana
            //  de la cola tarda en vaciarse enteros y mas de veinte veces la
            //  constante de 115 ms del analizador. Lo que se mide asi es lo
            //  que de verdad importa — que se asiente y se PARE — que es
            //  ademas lo unico que impide que esto repinte para siempre, que
            //  es el fallo que `Tests/cpu.py` ya cazo en la curva del EQ.
            lee (false, 80);
            const auto callado = lee (false, 5);
            const auto sonando = lee (true, 12);
            auto distinto = [] (const std::pair<FxVisor::Curva, juce::Point<float>>& a,
                                const std::pair<FxVisor::Curva, juce::Point<float>>& b)
            {
                if (a.second.x >= 0.0f || b.second.x >= 0.0f)
                    return a.second.getDistanceFrom (b.second) > 0.01f;
                for (int i = 0; i < FxVisor::kPuntos; ++i)
                    if (std::abs (a.first[(size_t) i] - b.first[(size_t) i]) > 0.01f) return true;
                return false;
            };

            if (distinto (callado, sonando)) ++mueven; else vivoMudo.add (juce::String (f));

            //  Y QUIETA SIN SEÑAL, con el mismo asentado delante: una capa que
            //  dibuja ruido falla aqui, y una que se ha parado no.
            lee (false, 80);
            const auto otra = lee (false, 5);
            if (! distinto (callado, otra)) ++quietosSinSenal;
            else
            {
                vivoRuido.add (juce::String (f));
            }
        }

        for (int f = 0; f < kNumFx; ++f) engine.setCanalSend (0, f, 0.0f);
    }

    //  4. Y LA GEOMETRIA DE LA FILA, que es lo que la ficha paga por dibujar:
    //     el fader tiene que seguir midiendo un dedo y la miniatura tiene que
    //     tener sitio. Con la ficha ABIERTA, o los limites son los de la ultima
    //     vez que se maqueto.
    //     Y la del visor del plato, que no puede costarle un pixel a los tres
    //     mandos: son lo unico que se toca ahi.
    ponEnRanura (0, AudioEngine::kFxDly);
    focusedFx = AudioEngine::kFxDly;
    refrescaPlato();
    openSheet (rackSheet, mixButton);
    resized();
    const auto fader = rackSends[0]->getBounds();
    const auto mini  = platoMini.getBounds();
    const auto mando = macroCtrl1.getBounds();
    const auto mute  = rackMuteBtns[0]->getBounds();

    //  5. MUTEAR DESDE EL RACK, con DOS cifras y POR LA TAPA.
    //
    //  Se pidio «al lado del boton del plugin, una opcion para sustituirlo o
    //  MUTEARLO», y apagar seguia siendo exclusivo de la fila de la cara: el
    //  rack pintaba el estado y no dejaba tocarlo. Llamar a `fxTapped` por
    //  dentro se salta justo el `onClick`, que es donde vive lo que se ha
    //  anadido — la misma leccion de los cinco fallos del compas del piano.
    //
    //  Y las DOS mitades: que la tapa APAGUE y que el canalon SIGA abriendo el
    //  menu. Solo la primera la cumple un canalon convertido en interruptor,
    //  que es lo corto y se lleva por delante la unica puerta para cambiar o
    //  vaciar una ranura.

    setFxEnabled (AudioEngine::kFxDly, true);
    refrescaRanuras();
    const int muteAntes = fxEncendido (AudioEngine::kFxDly) ? 1 : 0;
    pulsaTapa (rackMuteBtns[0]);
    const int muteDespues = fxEncendido (AudioEngine::kFxDly) ? 1 : 0;
    pulsaTapa (rackMuteBtns[0]);
    const int muteVuelve = fxEncendido (AudioEngine::kFxDly) ? 1 : 0;

    //  Y sobre una ranura VACIA no hace nada: no hay efecto que sacar de en
    //  medio, y un toque que apaga «lo que hubiera» apagaria el tipo del
    //  vecino.
    const int ranuraLibre = kNumRanuras - 1;
    ponEnRanura (ranuraLibre, kSlotVacia);
    refrescaRanuras();
    auto encendidos = [this]
    {
        int n = 0;
        for (int i = 0; i < kNumFx; ++i) if (fxEncendido (i)) ++n;
        return n;
    };
    const int mudoAntes = encendidos();
    pulsaTapa (rackMuteBtns[ranuraLibre]);
    const int mudoDespues = encendidos();

    //  Y el canalon: sigue siendo la puerta del menu.
    abreMenuRanura (-1);
    pulsaTapa (rackSlotBtns[0]);
    const int menuAbre = ranuraSheet.isVisible() ? 1 : 0;
    abreMenuRanura (-1);

    //  6. EL CRISTAL DEL PLATO SIN EFECTO, MEDIDO EN PIXELES.
    //
    //  `FxMini::paint` se rendia arriba del todo con `fx < 0`, o sea en una
    //  instalacion limpia -ninguna ranura puesta-: el visor quedaba VISIBLE,
    //  colocado en 112x74 y sin pintar un pixel, asi que al lado de los tres
    //  mandos se veia el plato. Un rectangulo vacio se lee como una pieza que
    //  falta, y esa fue la queja.
    //
    //  DOS CIFRAS, que una sola se engaña por los dos lados: solo «vacio > 0»
    //  lo cumple tambien un visor que se quedo dibujando la curva del efecto
    //  anterior, y solo «vacio distinto de puesto» lo cumple el fallo de hoy
    //  -cero contra algo-. Juntas dicen que hay cristal y que el cristal no es
    //  la curva.
    //
    //  Y se cuenta lo PINTADO y no una bandera: un booleano que el codigo se
    //  pone a si mismo es el fallo de `caraLista`, que decia «tapada» con la
    //  cara entera a la vista.
    auto pintaVisor = [this]
    {
        juce::Image img (juce::Image::ARGB, juce::jmax (1, platoMini.getWidth()),
                         juce::jmax (1, platoMini.getHeight()), true);
        { juce::Graphics gg (img); platoMini.paint (gg); }
        return img;
    };
    auto difieren = [] (const juce::Image& a, const juce::Image& c)
    {
        if (a.getWidth() != c.getWidth() || a.getHeight() != c.getHeight()) return -1;
        int n = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt (x, y) != c.getPixelAt (x, y)) ++n;
        return n;
    };
    auto cuenta = [] (const juce::Image& img)
    {
        int n = 0;
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
                if (img.getPixelAt (x, y).getAlpha() > 0) ++n;
        return n;
    };

    closeAllSheets();
    for (int s = 0; s < kNumRanuras; ++s) ponEnRanura (s, kSlotVacia);
    focusFx (AudioEngine::kFxFlt);
    refrescaPlato();
    refrescaVisorPlato();
    resized();
    const auto imgVacio = pintaVisor();
    const int visorVacio = cuenta (imgVacio);

    ponEnRanura (0, AudioEngine::kFxFlt);
    focusFx (AudioEngine::kFxFlt);
    refrescaPlato();
    refrescaVisorPlato();
    resized();
    const auto imgPuesto = pintaVisor();
    const int visorPuesto = cuenta (imgPuesto);
    const int visorDif    = difieren (imgVacio, imgPuesto);

    std::cout << "{\"rack\":1"
              << ",\"mal\":" << mal
              << ",\"dibujo\":[" << dibujo.joinIntoString (",") << "]"
              << ",\"marcas\":[" << marcas.joinIntoString (",") << "]"
              << ",\"cambian\":" << cambian
              << ",\"quietos\":" << quietos
              << ",\"discrepan\":" << discrepan
              << ",\"medidos\":[" << medidos.joinIntoString (",") << "]"
              << ",\"vivo_mudo\":[" << vivoMudo.joinIntoString (",") << "]"
              << ",\"vivo_ruido\":[" << vivoRuido.joinIntoString (",") << "]"
              << ",\"mueven\":" << mueven
              << ",\"quietos_sin\":" << quietosSinSenal
              << ",\"medibles\":" << medibles
              << ",\"dichos\":["  << dichos .joinIntoString (",") << "]"
              << ",\"tipos\":" << kNumFx
              << ",\"insertos\":"  << AudioEngine::numInsertos()
              << ",\"canales\":"   << AudioEngine::kNumCanales
              << ",\"buses\":"     << AudioEngine::numBuses()
              << ",\"fader\":[" << fader.getWidth() << "," << fader.getHeight() << "]"
              << ",\"mini\":["  << mini.getWidth()  << "," << mini.getHeight()  << "]"
              << ",\"mando\":[" << mando.getWidth() << "," << mando.getHeight() << "]"
              << ",\"mute\":["  << mute.getWidth()  << "," << mute.getHeight()  << "]"
              << ",\"mute_antes\":"   << muteAntes
              << ",\"mute_despues\":" << muteDespues
              << ",\"mute_vuelve\":"  << muteVuelve
              << ",\"mute_vacia\":["  << mudoAntes << "," << mudoDespues << "]"
              << ",\"menu_abre\":"    << menuAbre
              << ",\"visor_vacio\":"  << visorVacio
              << ",\"visor_puesto\":" << visorPuesto
              << ",\"visor_dif\":"    << visorDif
              << "}" << std::endl;
}

// ============================================================================
//  EL EQ DE CINCO BANDAS Y SU CURVA. Ver Tests/eq.py.
//
//  NINGUNA de las nueve reglas de `expo.py` puede ver nada de esto. Una curva
//  es un LIENZO -se pinta entera y se acierta con el dedo, como la rejilla de
//  pasos, el piano y la linea de tiempo- asi que un nodo que escribe la banda
//  de al lado se maqueta perfecto: no solapa, no se sale, no lleva rotulo, no
//  mide cero y esta traducido. Es la familia de los cinco fallos del compas
//  del piano, otra vez.
//
//  SE MIDE POR EL GESTO EN PIXELES y no llamando a `ponBandaEq`: el callback se
//  salta exactamente el codigo que decide QUE nodo cae bajo el dedo y hasta
//  donde puede llegar, que es donde vive todo lo nuevo. Se construye un
//  `MouseEvent` y se llama a `EqCurve::mouseDown` / `mouseDrag`, igual que
//  hacen `Tests/clips.py` con la linea de tiempo y `Tests/piano.py` con el
//  arrastre del piano roll.
void MainComponent::auditEq()
{
    //  El plato se lo lleva la curva SOLO con el EQ puesto y delante. Las dos
    //  mitades, que «se ve» lo cumple igual una curva que se ve siempre - y
    //  entonces los tres mandos no volverian nunca.
    for (int s = 0; s < kNumRanuras; ++s) ponEnRanura (s, kSlotVacia);
    ponEnRanura (0, 0);                 // FLT en la ranura 0
    focusFx (0);
    const int platoConFlt  = eqCurva.isVisible() ? 1 : 0;
    const int mandosConFlt = macroCtrl1.getBounds().isEmpty() ? 0 : 1;

    ponEnRanura (1, kFxEq);
    focusFx (kFxEq);
    const int platoConEq  = eqCurva.isVisible() ? 1 : 0;
    const int mandosConEq = macroCtrl1.getBounds().isEmpty() ? 0 : 1;
    //  EN COORDENADAS DE LA CURVA y no del padre, que es donde esta prueba se
    //  equivoco antes de acertar: `getBounds()` las da en las del padre y
    //  `MouseEvent::position` es relativa al componente, asi que el primer
    //  intento apuntaba con la y del plato dentro de la cara -muy por debajo
    //  del alto de la curva- y `masCercana` devolvia -1: cero movido con el
    //  codigo perfecto. Primero se duda de la prueba.
    const auto caja = eqCurva.getLocalBounds();

    //  Y CUANTO LE TOCA A CADA NODO, que es el numero que decidio que la curva
    //  se lleva el plato ENTERO y no dos tercios: por debajo del dedo un nodo
    //  no se puede agarrar.
    const int porNodo = caja.getWidth() / Eq5::kBands;

    auto arrastra = [this] (juce::Point<int> desde, juce::Point<int> hasta)
    {
        auto ev = [this] (juce::Point<int> p)
        {
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                     p.toFloat(), juce::ModifierKeys(),
                                     1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     &eqCurva, &eqCurva, juce::Time::getCurrentTime(),
                                     p.toFloat(), juce::Time::getCurrentTime(), 1, false);
        };
        eqCurva.mouseDown (ev (desde));
        //  En cuatro pasos y no de un salto: un dedo de verdad emite un evento
        //  por movimiento, y de un salto un fallo de acumulacion pasaria - es
        //  la leccion que ya costo una medida con el deshacer del piano roll.
        for (int i = 1; i <= 4; ++i)
            eqCurva.mouseDrag (ev (desde + (hasta - desde) * i / 4));
        eqCurva.mouseUp (ev (hasta));
    };

    //  1. UN NODO ESCRIBE SU BANDA Y NO LA DE AL LADO, con un TESTIGO en otra:
    //     «escribio» lo cumple igual un codigo que escribe siempre en la 0.
    for (int b = 0; b < Eq5::kBands; ++b) ponBandaEq (b, Eq5::kFreqDef[b], 0.0f);
    ponBandaEq (4, Eq5::kFreqDef[4], -7.0f);            // el testigo

    const int y0 = caja.getCentreY();
    //  El nodo de la banda 2 vive donde la curva lo pinta, y su x se deduce de
    //  la misma escala logaritmica: el centro de los cinco cae en la mitad del
    //  ancho porque 1 kHz es la frecuencia central de fabrica.
    const int x2 = caja.getX() + caja.getWidth() / 2;
    //  Hacia ARRIBA, o sea ganancia positiva. Un cuarto del alto es la mitad
    //  del recorrido, o sea unos +6 dB.
    arrastra ({ x2, y0 }, { x2, y0 - caja.getHeight() / 4 });

    const float g2 = engine.getEqGain (canalActual, 2);
    const float g4 = engine.getEqGain (canalActual, 4);

    //  2. Y EL MOTOR SE ENTERA. El espejo y el motor son dos sitios y un
    //     camino: si solo se escribiera el espejo, la curva subiria y no
    //     sonaria nada - que es la forma exacta de que lo que se ve y lo que
    //     suena dejen de decir lo mismo.
    const float espejo2 = eqEspejo.gainDe (2);

    //  3. DOS BANDAS NO SE CRUZAN. Se arrastra la 2 hasta el borde derecho, o
    //     sea muy por encima de donde vive la 3: tiene que quedarse por debajo
    //     de ella con su tercio de octava de guarda. Sin el tope, la curva
    //     dibujada y la que suena dejan de estar de acuerdo y un nodo salta al
    //     otro lado de su vecino.
    arrastra ({ x2, y0 - caja.getHeight() / 4 }, { caja.getRight() + 40, y0 });
    const float f2 = engine.getEqFreq (canalActual, 2);
    const float f3 = engine.getEqFreq (canalActual, 3);
    const int   cruza = (f2 < f3) ? 0 : 1;

    //  4. UN TOQUE EN EL AIRE NO ARRASTRA NADA. `EqCurve` coge el nodo mas
    //     cercano SOLO dentro de un dedo, que es la misma regla que las asas
    //     del recorte: sin el limite, un toque en una esquina se lleva la banda
    //     del otro extremo.
    for (int b = 0; b < Eq5::kBands; ++b) ponBandaEq (b, Eq5::kFreqDef[b], 0.0f);
    arrastra ({ caja.getX() + caja.getWidth() / 2, caja.getY() + 1 },
              { caja.getX() + caja.getWidth() / 2, caja.getBottom() - 1 });
    float peorLejos = 0.0f;
    for (int b = 0; b < Eq5::kBands; ++b)
        peorLejos = juce::jmax (peorLejos, std::abs (engine.getEqGain (canalActual, b)));

    //  5. LA CURVA QUE SE DIBUJA NO ES PLANA CUANDO LAS BANDAS NO LO ESTAN.
    //     Es la regla que faltaba y la que habria cazado el fallo de la foto:
    //     `Eq5::recalcula` solo se llamaba desde `procesa` -o sea desde el hilo
    //     de audio- y el ESPEJO no procesa audio nunca, asi que su bandera
    //     `sucio` se quedaba puesta para siempre y `respuestaEnDb` evaluaba la
    //     tabla de coeficientes de la curva PLANA. Los cinco nodos movidos y
    //     una raya recta.
    //
    //     Se mide PINTANDO, que es el camino de verdad: quien pone los
    //     coeficientes al dia es `EqCurve::paint`, y preguntarle a
    //     `eqEspejo.refresca()` desde aqui seria hacer el arreglo dentro de la
    //     prueba. Recorrido entre el maximo y el minimo en las cinco
    //     frecuencias de fabrica: con el `refresca()` quitado, 0.00 dB.
    const float dBcurva[Eq5::kBands] = { 5.0f, -6.0f, 3.5f, -4.0f, 8.0f };
    for (int b = 0; b < Eq5::kBands; ++b) ponBandaEq (b, Eq5::kFreqDef[b], dBcurva[b]);
    {
        juce::Image lienzo (juce::Image::ARGB, juce::jmax (1, caja.getWidth()),
                            juce::jmax (1, caja.getHeight()), true);
        juce::Graphics gg (lienzo);
        eqCurva.paint (gg);
    }
    float lo = 1.0e9f, hi = -1.0e9f;
    for (int b = 0; b < Eq5::kBands; ++b)
    {
        const float r = eqEspejo.respuestaEnDb (Eq5::kFreqDef[b]);
        lo = juce::jmin (lo, r);
        hi = juce::jmax (hi, r);
    }
    const float recorrido = hi - lo;

    //  6. MANTENER SOBRE UN NODO ABRE SU FICHA, y arrastrar NO. Las dos
    //     mitades: «abre» lo cumple igual un gesto que abre siempre, y
    //     entonces cada arrastre acabaria con un menu delante al soltar.
    abreBandaEq (-1);
    auto evEn = [this] (juce::Point<int> p)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                 p.toFloat(), juce::ModifierKeys(),
                                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &eqCurva, &eqCurva, juce::Time::getCurrentTime(),
                                 p.toFloat(), juce::Time::getCurrentTime(), 1, false);
    };
    eqCurva.mouseDown (evEn ({ x2, y0 }));
    const int armadoAlApoyar = eqCurva.mantenerArmado() ? 1 : 0;
    eqCurva.venceElMantener();
    const int fichaTrasMantener = eqBandaSheet.isVisible() ? 1 : 0;
    const int bandaAbierta      = eqBandaSel;
    eqCurva.mouseUp (evEn ({ x2, y0 }));

    //  Y ARRASTRAR LO CANCELA, que es la otra mitad: sin ella «abre al
    //  mantener» lo cumple igual un gesto que abre siempre, y entonces cada
    //  arrastre acabaria con la ficha delante al soltar.
    abreBandaEq (-1);
    eqCurva.mouseDown (evEn ({ x2, y0 }));
    eqCurva.mouseDrag (evEn ({ x2, y0 - 30 }));
    const int armadoTrasArrastrar = eqCurva.mantenerArmado() ? 1 : 0;
    eqCurva.mouseUp (evEn ({ x2, y0 - 30 }));

    //  7. LOS CINCO TIPOS Y LA Q, por la TAPA y por el MANDO. Un tipo de PASO
    //     no tiene ganancia -su nodo se queda clavado en el cero- y eso es lo
    //     que dice `gainVisible`: con un realce de 8 dB puesto, la banda que
    //     pasa a PASO ALTO tiene que dibujar 0.
    abreBandaEq (2);
    ponBandaEq (2, Eq5::kFreqDef[2], 8.0f);
    if (auto* t = eqTipoBtns[Eq5::pasoAlto]) t->onClick();
    const int   tipoTrasChip = engine.getEqTipo (canalActual, 2);
    const float visibleTrasChip = eqEspejo.gainVisible (2);
    eqQKnob.setValue (4.5, juce::sendNotificationSync);
    const float qMotor  = engine.getEqQ (canalActual, 2);
    const float qEspejo = eqEspejo.qDe (2);

    //  8. LA BANDA MAS AGUDA LLEGA A DONDE DICE EL MANDO. El recorrido pasa a
    //     ser 20 Hz - 20 kHz, y el tope interno de `recalcula` era `fs * 0.45`
    //     -19 845 Hz a 44.1 kHz-: con el techo en 20 000 la banda se habria
    //     quedado 156 Hz por debajo de donde el nodo la dibuja, en silencio.
    //
    //     Con DOS cifras, porque una sola se engaña: **donde queda** -que lo
    //     cumple igual un numero guardado y no aplicado- y **cuanto sube la
    //     respuesta dibujada ahi arriba**, que es lo que dice que el filtro
    //     esta de verdad en esos 20 kHz.
    //     Y SE ESCRIBE, NO SE ARRASTRA. La primera version llevaba el nodo
    //     con el gesto y se equivoco por los dos lados a la vez -las dos
    //     pantallas dieron cosas distintas, que es lo que delata a la medida-:
    //     `mueve` escribe la frecuencia Y la ganancia, asi que arrastrar por el
    //     centro vertical dejaba la banda en 0 dB; y en 412x915 el nodo de
    //     10 kHz cae al 90 % del ancho, o sea a mas de un dedo del borde, y el
    //     agarre fallaba. Lo que esta regla mira es el TOPE y no el gesto -eso
    //     ya lo miden las comprobaciones 3 y 4-.
    abreBandaEq (-1);
    for (int b = 0; b < Eq5::kBands; ++b) ponBandaEq (b, Eq5::kFreqDef[b], 0.0f);
    ponBandaEq (4, Eq5::kFreqMax, 10.0f);
    const float fTope = engine.getEqFreq (canalActual, 4);
    {
        juce::Image lienzo (juce::Image::ARGB, juce::jmax (1, caja.getWidth()),
                            juce::jmax (1, caja.getHeight()), true);
        juce::Graphics gg (lienzo);
        eqCurva.paint (gg);
    }
    const float dbEnTope = eqEspejo.respuestaEnDb (Eq5::kFreqMax);

    //  9. Y LA CURVA ES DEL CANAL, que es la queja con la que empezo la tanda
    //     dicha en su efecto: «solo es posible que un ecualizador funcione y
    //     sea colocado en un canal solo». Hay DIECISEIS `Eq5`, uno por canal.
    //
    //     Con CUATRO cifras porque el EQ tiene dos lados y los dos pueden
    //     mentir por su cuenta: lo que el MOTOR guarda en el canal 0 y en el
    //     4, y lo que la curva DIBUJA al llegar al 4 y al volver al 0. Solo
    //     las dos primeras las cumple una app con dieciseis `Eq5` y un espejo
    //     que no se recarga -la curva se quedaria enseñando la del canal
    //     anterior sobre el filtro nuevo- y solo las dos ultimas una que
    //     recarga el espejo de un motor con un solo ecualizador.
    //
    //     La cuarta es ademas la que impide que «en el 4 sale plana» lo cumpla
    //     un codigo que BORRA la curva al cambiar de canal.
    abreBandaEq (-1);
    for (int c : { 0, 4 })
    {
        ponCanalActual (c);
        for (int b = 0; b < Eq5::kBands; ++b) ponBandaEq (b, Eq5::kFreqDef[b], 0.0f);
    }
    ponCanalActual (0);
    ponBandaEq (2, Eq5::kFreqDef[2], 9.0f);
    const float curvaEn0 = engine.getEqGain (0, 2);
    const float curvaEn4 = engine.getEqGain (4, 2);
    ponCanalActual (4);
    const float espejoEn4 = eqEspejo.gainDe (2);
    ponCanalActual (0);
    const float espejoVuelve = eqEspejo.gainDe (2);

    for (int c : { 4, 0 })
    {
        ponCanalActual (c);
        for (int b = 0; b < Eq5::kBands; ++b) ponBandaEq (b, Eq5::kFreqDef[b], 0.0f);
    }

    abreBandaEq (-1);
    for (int b = 0; b < Eq5::kBands; ++b)
    {
        ponBandaEq (b, Eq5::kFreqDef[b], 0.0f);
        ponTipoEq  (b, (int) Eq5::tipoDeFabrica (b));
        ponQEq     (b, Eq5::kQDef);
    }

    std::cout << "{\"eq\":1"
              << ",\"plato_con_flt\":"  << platoConFlt
              << ",\"mandos_con_flt\":" << mandosConFlt
              << ",\"plato_con_eq\":"   << platoConEq
              << ",\"mandos_con_eq\":"  << mandosConEq
              << ",\"por_nodo\":"       << porNodo
              << ",\"g2\":"             << juce::String (g2, 2)
              << ",\"g4\":"             << juce::String (g4, 2)
              << ",\"espejo2\":"        << juce::String (espejo2, 2)
              << ",\"f2\":"             << juce::String (f2, 1)
              << ",\"f3\":"             << juce::String (f3, 1)
              << ",\"cruza\":"          << cruza
              << ",\"lejos\":"          << juce::String (peorLejos, 2)
              << ",\"recorrido\":"      << juce::String (recorrido, 2)
              << ",\"armado_apoyar\":"  << armadoAlApoyar
              << ",\"armado_arrastre\":" << armadoTrasArrastrar
              << ",\"ficha_mantener\":" << fichaTrasMantener
              << ",\"banda_abierta\":"  << bandaAbierta
              << ",\"tipo_chip\":"      << tipoTrasChip
              << ",\"visible_paso\":"   << juce::String (visibleTrasChip, 2)
              << ",\"q_motor\":"        << juce::String (qMotor, 2)
              << ",\"q_espejo\":"       << juce::String (qEspejo, 2)
              << ",\"f_min\":"         << juce::String (Eq5::kFreqMin, 0)
              << ",\"f_max\":"         << juce::String (Eq5::kFreqMax, 0)
              << ",\"f_tope\":"        << juce::String (fTope, 1)
              << ",\"db_tope\":"       << juce::String (dbEnTope, 2)
              << ",\"curva_c0\":"      << juce::String (curvaEn0, 2)
              << ",\"curva_c4\":"      << juce::String (curvaEn4, 2)
              << ",\"espejo_c4\":"     << juce::String (espejoEn4, 2)
              << ",\"espejo_vuelve\":" << juce::String (espejoVuelve, 2)
              << "}" << std::endl;
}

// ============================================================================
//  LA AUTOMATIZACION. Ver Tests/auto.py.
//
//  NINGUNA de las nueve reglas de `expo.py` puede verla: es ESTADO. Una app que
//  se olvida de lo que tocaste se maqueta perfecta, no solapa, no corta un
//  rotulo y esta traducida.
//
//  SE MIDE POR LA TAPA Y POR EL MANDO -`autoBtn.onClick`, `macroCtrl1` con su
//  `onValueChange`- y no llamando a `anotaAutomacion` por dentro, que es justo
//  donde no existe ninguno de los fallos: si el evento no llega a
//  `pushFxParam`, o si el paso que se lee no es el del transporte, llamar a la
//  funcion por dentro pasa igual.
// ============================================================================
//  LA FAMILIA DE DINAMICA EN LA CARA. Ver Tests/dinamica.py.
//
//  Lo que suena lo mide el banco del motor -cuatro filas, dos cifras cada una-.
//  Lo que se mide aqui es lo que ninguna de las nueve reglas de `expo.py` puede
//  ver y el motor tampoco: que los cuatro tipos LLEGUEN a la fila, que su
//  reduccion se LEA, y que un proyecto vuelva con sus numeros. Son fallos de
//  INDICE y de estado — una tapa que enciende el efecto de al lado se maqueta
//  perfecta.
//
//  SE MIDE POR LA TAPA Y POR EL MANDO y no llamando a `setDynP0` por dentro,
//  que es justo donde el fallo no existe: el camino de verdad va del mando a
//  `pushFxParam`, de ahi a `AudioEngine::setFxParam` y de ahi al atomico, y es
//  ese switch de treinta y tres casos el que se equivoca de una fila.
void MainComponent::auditDinamica()
{

    //  1. LOS CUATRO TIPOS ESTAN EN EL MENU. Con once tipos y seis ranuras, el
    //     menu es la UNICA puerta a los cuatro nuevos: si la rejilla se hubiera
    //     quedado en siete celdas, CMP, GTE, DSS y LIM sonarian y no habria
    //     forma de ponerlos. Se cuenta lo que el menu OFRECE, no `kNumFx`.
    for (int s = 0; s < kNumRanuras; ++s) ponEnRanura (s, kSlotVacia);
    abreMenuRanura (0);
    int enMenu = 0;
    for (auto* b : ranuraBtns) if (b != nullptr && ! b->getBounds().isEmpty()) ++enMenu;
    //  Y la celda contra el DEDO: con once en dos columnas serian seis filas y
    //  la tarjeta no da; en tres son cuatro. Lo decide esta cifra.
    int celdaW = 0, celdaH = 0;
    if (auto* b = ranuraBtns[0]) { celdaW = b->getWidth(); celdaH = b->getHeight(); }
    abreMenuRanura (-1);

    //  2. CADA TIPO LLEGA A SU RANURA POR EL GESTO. Se pone CMP en la 0 y LIM
    //     en la 1 pulsando las tapas del menu, que es donde vive el indice.
    abreMenuRanura (0);
    pulsaTapa (ranuraBtns[AudioEngine::kFxCmp]);
    abreMenuRanura (1);
    pulsaTapa (ranuraBtns[AudioEngine::kFxLim]);
    const int enRanura0 = slotFx[0][0];
    const int enRanura1 = slotFx[0][1];

    //  3. LOS TRES MANDOS ESCRIBEN EN SU EFECTO Y NO EN EL DE AL LADO, con un
    //     TESTIGO: se le da a CMP un umbral y a LIM un techo DISTINTOS, y las
    //     dos tienen que quedarse donde se pusieron. Con un solo efecto, un
    //     switch corrido de una fila pasa la prueba.
    focusFx (AudioEngine::kFxCmp);
    macroCtrl1.setValue (-30.0, juce::sendNotificationSync);   // UMBRAL
    macroCtrl2.setValue (  6.0, juce::sendNotificationSync);   // RATIO
    focusFx (AudioEngine::kFxLim);
    macroCtrl1.setValue (-12.0, juce::sendNotificationSync);   // TECHO
    macroCtrl2.setValue (200.0, juce::sendNotificationSync);   // SOLTAR

    const float cmpUmbral = engine.getDynP0 (canalActual, 0);
    const float cmpRatio  = engine.getDynP1 (canalActual, 0);
    const float limTecho  = engine.getDynP0 (canalActual, 3);
    const float limSoltar = engine.getDynP1 (canalActual, 3);

    //  4. LA REDUCCION SE LEE, Y SOLO CON EL DEDO FUERA. Un compresor que no
    //     dice cuanto comprime es invisible; y una casilla que dice la
    //     reduccion mientras se mueve el mando es un control contando otra
    //     cosa que su propio numero. Las DOS mitades.
    engine.setFxParam (canalActual, AudioEngine::kFxLim, 2, 1.0f);
    engine.setCanalSend (0, AudioEngine::kFxLim, 1.0f);
    engine.setPadGain (0, 1.0f);

    //  UN TONO PLANO Y NO EL SONIDO DE FABRICA, que es donde esta medida se
    //  equivoco antes de acertar. El pad 0 trae un golpe de ~0.4 s, asi que al
    //  preguntar por la reduccion cuarenta ticks despues ya se habia apagado y
    //  la casilla decia «0 %» con el codigo perfecto. Es exactamente el fallo
    //  del pan medido sobre medio segundo de silencio. Primero se duda de la
    //  prueba.
    {
        auto* sb = new SampleBuffer();
        const int n = 48000;
        sb->buffer.setSize (2, n);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
                sb->buffer.setSample (c, i,
                    0.9f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) i / 48000.0f));
        sb->sourceSampleRate = 48000.0;
        engine.publishSample (0, SampleBuffer::Ptr (sb));
    }

    //  Y EL ENVIO SE ASIENTA ANTES DE DISPARAR: se cruza con el camino seco en
    //  20 ms, asi que disparar y leer enseguida mide la mitad de un pad que
    //  todavia no ha entrado en el efecto. Es el mismo arreglo que acaba de
    //  costar una medida en el banco del motor.
    //
    //  La reduccion la escribe el HILO DE AUDIO al procesar, asi que en un
    //  escritorio sin tarjeta vale cero pase lo que pase - el mismo agujero que
    //  ya costo una medida con `numClips`.
    for (int i = 0; i < 8; ++i) bombeaAudioDePrueba();
    engine.postNoteOn (0, 1.0f);
    //  Pocos ticks, y MIENTRAS SUENA: el tono dura un segundo y con cuarenta
    //  ticks se pregunta cuando ya no queda nada que limitar.
    for (int i = 0; i < 6; ++i) bombeaAudioDePrueba();

    focusFx (AudioEngine::kFxLim);
    setMacroTouched (2, false);
    const juce::String leeSuelto = macroReadout (2);
    setMacroTouched (2, true);
    const juce::String leeTocado = macroReadout (2);
    setMacroTouched (2, false);

    //  4b. Y LA REDUCCION ES DEL CANAL. Hay DIECISEIS limitadores, uno por
    //      canal, y el pad 0 vive en el canal cero: el del cuatro no ha visto
    //      una muestra en su vida. Con DOS cifras, o «el limitador reduce» lo
    //      cumple igual un motor con UN limitador compartido — que es
    //      exactamente lo que habia antes de esta tanda y lo que dejaria dos
    //      canales enseñando el mismo medidor de reduccion.
    const float redEn0 = engine.getDynReduccion (0, 3);
    const float redEn4 = engine.getDynReduccion (4, 3);

    //  5. Y VUELVEN DEL FICHERO DE PROYECTO. Se escribe con el MISMO arbol que
    //     escribe el fichero, se BORRA a mano —si al volver sigue puesto no es
    //     que se haya guardado, es que nadie lo quito— y se abre.
    auto estado = captureState();
    engine.setFxParam (canalActual, AudioEngine::kFxCmp, 0, -18.0f);
    engine.setFxParam (canalActual, AudioEngine::kFxLim, 0,  -1.0f);
    ponEnRanura (0, kSlotVacia);
    ponEnRanura (1, kSlotVacia);
    applyState (estado);
    const float cmpVuelve = engine.getDynP0 (canalActual, 0);
    const float limVuelve = engine.getDynP0 (canalActual, 3);
    const int   r0Vuelve  = slotFx[0][0];
    const int   r1Vuelve  = slotFx[0][1];

    //  CUANTOS TIPOS HAY lo dice la app y no una cuenta escrita en el script:
    //  el dia que entre el doce, lo que tiene que fallar es el menu y no una
    //  linea del banco que nadie obliga a decir lo mismo.
    std::cout << "{\"dyn\":1"
              << ",\"tipos\":"      << AudioEngine::kNumFx
              << ",\"en_menu\":"    << enMenu
              << ",\"celda_w\":"    << celdaW
              << ",\"celda_h\":"    << celdaH
              << ",\"ranura0\":"    << enRanura0
              << ",\"ranura1\":"    << enRanura1
              << ",\"cmp_umbral\":" << juce::String (cmpUmbral, 2)
              << ",\"cmp_ratio\":"  << juce::String (cmpRatio, 2)
              << ",\"lim_techo\":"  << juce::String (limTecho, 2)
              << ",\"lim_soltar\":" << juce::String (limSoltar, 2)
              << ",\"lee_suelto\":\"" << leeSuelto << "\""
              << ",\"lee_tocado\":\"" << leeTocado << "\""
              << ",\"red_c0\":"     << juce::String (redEn0, 2)
              << ",\"red_c4\":"     << juce::String (redEn4, 2)
              << ",\"cmp_vuelve\":" << juce::String (cmpVuelve, 2)
              << ",\"lim_vuelve\":" << juce::String (limVuelve, 2)
              << ",\"r0_vuelve\":"  << r0Vuelve
              << ",\"r1_vuelve\":"  << r1Vuelve
              << "}" << std::endl;
}

void MainComponent::auditAuto()
{

    //  La cancion rodando, que es el unico reloj que la automatizacion tiene.
    //  Sin esto `pasoDeCancion` vale -1 y no habria donde poner el evento -que
    //  es correcto y es la primera de las cifras-.
    ponModoCancion (true);
    engine.setSongLength (4);
    vaciaAutomacion();

    //  1. PARADO NO SE ESCRIBE. Un evento sin paso es un evento en cualquier
    //     sitio, y el sintoma seria un barrido que suena al principio de la
    //     cancion en vez de donde lo tocaste.
    pulsaTapa (&autoBtn);
    const int armadoTrasTocar = autoArmado ? 1 : 0;
    focusFx (3);                       // DLY, que tiene tres parametros de sobra
    macroCtrl1.setValue (400.0, juce::sendNotificationSync);
    const int paradoEscribe = (int) autoEventos.size();

    //  2. RODANDO SI. Se bombea audio hasta que el transporte publica un paso
    //     -en un escritorio sin tarjeta nadie llama al motor, que es la misma
    //     razon por la que `clips.py` tuvo que bombear para contar clips-.
    ponTransporte (true);
    ponAutoArmado (true);              // ponTransporte(false) desarma, y aqui se arma
    int pasoVisto = -1;
    for (int i = 0; i < 400 && pasoVisto < 0; ++i)
    {
        bombeaAudioDePrueba();
        pasoVisto = engine.pasoDeCancion();
    }
    macroCtrl1.setValue (600.0, juce::sendNotificationSync);
    const int rodandoEscribe = (int) autoEventos.size();
    const int pasoEscrito = autoEventos.empty() ? -1 : autoEventos.front().paso;

    //  3. UN EVENTO POR PASO Y POR PARAMETRO, y el ultimo gana. Tres valores
    //     seguidos en el mismo paso son UN evento con el ultimo, no tres: sin
    //     esto un arrastre de dos segundos escribe cientos de eventos en el
    //     mismo sitio y la tabla se llena con una sola frase.
    macroCtrl1.setValue (700.0, juce::sendNotificationSync);
    macroCtrl1.setValue (800.0, juce::sendNotificationSync);
    const int trasTres = (int) autoEventos.size();
    const float ultimo = autoEventos.empty() ? 0.0f : autoEventos.back().valor;

    //  4. Y PARAR DESARMA. Un modo de escritura que se queda puesto es como se
    //     borra una automatizacion buena en la pasada siguiente.
    ponTransporte (false);
    const int armadoTrasParar = autoArmado ? 1 : 0;

    //  5. Y VUELVE DEL FICHERO DE PROYECTO. Con el mismo arbol que lo escribe,
    //     BORRANDO los eventos a mano entre medias: si al volver siguen puestos
    //     no es que se hayan guardado, es que nadie los quito. Y con DOS
    //     cifras, que es lo que separa las dos formas de escribirlo mal: los
    //     que vuelven al espejo Y los que tiene el MOTOR. Solo lo primero lo
    //     cumple una lista que se lee del XML y no se publica nunca -la
    //     automatizacion volveria escrita y muda-.
    //  Y CON UN INSERTO EN UN CANAL QUE NO ES EL CERO, que es la mitad nueva:
    //  el 3 es DLY -un ENVIO, asi que su canal colapsa al cero escriba quien
    //  escriba- y el 0 es FLT en el canal 4. Con los dos en el cero, «vuelve el
    //  canal» lo cumpliria tambien un fichero que no lo guarda.
    autoEventos.clear();
    autoEventos.push_back ({ 17, 3, 2, 0, 0.42f });
    autoEventos.push_back ({ 48, 0, 0, 4, -0.75f });
    publicaAutomacion();
    const auto arbol = captureState();
    autoEventos.clear();
    publicaAutomacion();
    applyState (arbol);
    bombeaAudioDePrueba();          // que el motor adopte la tabla publicada

    juce::String vuelta;
    for (const auto& e : autoEventos)
        vuelta << e.paso << ":" << (int) e.fx << ":" << (int) e.par << ":"
               << juce::String (e.valor, 2) << ":" << (int) e.canal << ";";

    std::cout << "{\"auto\":1"
              << ",\"armado_tras_tocar\":" << armadoTrasTocar
              << ",\"parado_escribe\":"    << paradoEscribe
              << ",\"paso_visto\":"        << pasoVisto
              << ",\"rodando_escribe\":"   << rodandoEscribe
              << ",\"paso_escrito\":"      << pasoEscrito
              << ",\"tras_tres\":"         << trasTres
              << ",\"ultimo\":"            << juce::String (ultimo, 0)
              << ",\"armado_tras_parar\":" << armadoTrasParar
              << ",\"vuelta\":\""          << vuelta << "\""
              << ",\"motor\":"             << engine.numAuto()
              << "}" << std::endl;
}

// ==========================================================================
//  EL REBOTE EN VIVO. Ver Tests/export.py y RebotVivo en Exporter.h.
//
//  Se mide POR LA TAPA -`exportLiveButton.onClick`- y no llamando a
//  `alternaRebotVivo` por dentro: la tapa es la que arranca Y para, y es donde
//  vive el orden que importa -armar el anillo antes de que el transporte ruede-.
//
//  Y el audio lo bombea el banco, que aqui no hay tarjeta: `bombeaAudioDePrueba`
//  es el mismo ayudante que ya usan `cpu.py` y `clips.py`. Sin el, el anillo se
//  queda vacio, el hilo escritor no escribe nada y la prueba diria «no salio
//  fichero» con el codigo perfecto.
// ==========================================================================
void MainComponent::auditVivo()
{

    //  Una cancion que suena: un pad con sonido, un patron con un golpe y el
    //  patron 1 en los cuatro primeros compases.
    engine.publishSample (0, Kits::render (0));
    engine.setPadGain (0, 1.0f);
    engine.setStep (0, 0, 0, true);
    engine.setStep (0, 4, 0, true);
    engine.setStep (0, 8, 0, true);
    engine.setStep (0, 12, 0, true);
    engine.setSongMode (true);
    engine.setSongLength (4);
    for (int b = 0; b < 4; ++b) engine.setSongCell (0, b, 1);

    auto corre = [this] (int compases)
    {
        cuentaCompases = compases;
        engine.setClick (true);
        pulsaTapa (&exportLiveButton);

        //  Durante la cuenta el compas no avanza Y el anillo no recibe nada:
        //  el clic es una referencia para tocar, no parte de la cancion.
        //  Y CON AIRE ENTRE TICS, que es andamio y no la app: aqui el bombeo
        //  es un bucle sincrono y en el aparato el hilo de audio va en tiempo
        //  real. Sin la pausa se le meten dos segundos de audio al anillo en un
        //  instante, el hilo escritor no ha tenido ni un turno y lo que se
        //  cuenta como «perdido» es la prueba corriendo mas rapido que el
        //  disco. `tiradas` se IMPRIME igualmente: si el andamio pierde, se ve.
        //
        //  Y LO ESCRITO SE LEE CON LA CUENTA TODAVIA VIVA, que es donde esta
        //  medida se equivoco. Leerlo al SALIR del bucle lee tambien lo que el
        //  ULTIMO bombeo metio despues del borde: un tic no es un bloque, son
        //  los que el aparato habria entregado en `relojMs` -veintidos aqui y
        //  treinta y siete en el runner del CI, que clasifica en otra gama- asi
        //  que la cuenta se acaba a MITAD de un bombeo y lo que sigue es audio
        //  de la cancion, no del clic. Salio 0 en este contenedor y **256
        //  muestras, o sea dos bloques**, en el CI: la prueba llamando fallo a
        //  justo lo que tiene que sonar, con la app intacta.
        //
        //  Leido aqui arriba, el guardia acaba de decir que la cuenta sigue
        //  puesta, asi que TODO lo escrito hasta este instante se escribio con
        //  ella viva. Es la unica lectura que contesta la pregunta.
        int enCuenta = 0;
        juce::int64 trasCuenta = (vivoJob != nullptr ? vivoJob->escritas() : -1);
        for (int i = 0; i < 40 && engine.enCuentaAtras(); ++i)
        {
            trasCuenta = (vivoJob != nullptr ? vivoJob->escritas() : -1);
            bombeaAudioDePrueba();
            juce::Thread::sleep (8);
            ++enCuenta;
        }

        for (int i = 0; i < 60; ++i) { bombeaAudioDePrueba(); juce::Thread::sleep (8); }

        //  Y LOS ULTIMOS DIEZ SIN AIRE, a proposito: al parar tiene que quedar
        //  COLA dentro del anillo. Con el escritor siempre al dia -que es lo
        //  que pasa cuando el banco le deja ocho milisegundos por tic- la
        //  mitad del codigo que vacia la cola al salir no la ejercia nadie, y
        //  romperla a proposito seguia saliendo verde. En el telefono el
        //  anillo SI lleva dentro lo ultimo que sono cuando se toca PARAR.
        for (int i = 0; i < 10; ++i) bombeaAudioDePrueba();
        pulsaTapa (&exportLiveButton);

        const auto f = vivoFichero;
        return std::make_tuple (enCuenta, trasCuenta,
                                (juce::int64) (f.existsAsFile() ? f.getSize() : 0),
                                f.getFileName());
    };

    const auto sinCuenta = corre (0);
    const auto conCuenta = corre (1);

    std::cout << "{\"vivo\":1"
              << ",\"bytes\":"        << std::get<2> (sinCuenta)
              << ",\"nombre\":\""    << std::get<3> (sinCuenta) << "\""
              << ",\"cuenta_ticks\":" << std::get<0> (conCuenta)
              << ",\"tras_cuenta\":"  << std::get<1> (conCuenta)
              << ",\"bytes_cuenta\":" << std::get<2> (conCuenta)
              << ",\"tiradas\":"      << engine.vivoTiradas()
              << "}" << std::endl;
}

void MainComponent::auditCuenta()
{

    //  1. LOS TRES VALORES, por su tapa, y lo que arma cada uno.
    //
    //  Con DOS cifras: cuantos compases dice la preferencia Y si el motor se
    //  queda esperando. Solo la primera la cumple tambien una tapa que escribe
    //  el numero y no lo usa - que es exactamente como estaba antes en el
    //  camino del microfono.
    juce::String armados, esperas;
    for (int i = 0; i < 3; ++i)
    {
        pulsaTapa (cuentaButtons[i]);
        engine.setPlaying (false);
        const bool espera = armaCuentaSiToca (0);
        armados += juce::String (cuentaCompases) + (i < 2 ? "," : "");
        esperas += juce::String (espera ? 1 : 0) + (i < 2 ? "," : "");
        engine.armaCuentaAtras (0);
        engine.setPlaying (false);
    }

    //  2. GRABAR YA NO FUERZA EL CLIC.
    //
    //  Se apaga a mano, se arma la cuenta y se mira si sigue apagado. Con el
    //  `engine.setClick (true)` de antes esto sale 1 y la persona se encuentra
    //  el metronomo colandose en la toma por los cascos.
    engine.setClick (false);
    pulsaTapa (cuentaButtons[1]);
    armaCuentaSiToca (0);
    const int clicTrasArmar = engine.isClick() ? 1 : 0;
    engine.armaCuentaAtras (0);
    engine.setPlaying (false);

    //  3. Y SE RECUERDA. El fichero es de la PERSONA y no del proyecto, asi que
    //     lo que se comprueba es que la siguiente vez que alguien lo lea salga
    //     lo que se dejo puesto. Se borra el valor en memoria antes de leer: si
    //     al volver sigue puesto no es que se haya guardado, es que nadie lo
    //     quito.
    pulsaTapa (cuentaButtons[2]);
    engine.setClick (true);
    saveCuentaPref();
    cuentaCompases = 0;
    engine.setClick (false);
    loadCuentaPref();
    const int vuelve    = cuentaCompases;
    const int clicVuelve = engine.isClick() ? 1 : 0;

    //  4. EL MONITOR, que es la otra mitad de «como se prepara una toma».
    //
    //  Con DOS cifras y por la TAPA, como los tres de arriba: lo que la
    //  preferencia dice Y lo que el motor acaba teniendo. Solo la primera la
    //  cumple una casilla que escribe un booleano y no lo empuja - que es
    //  exactamente el fallo que ya costo una medida con la cuenta.
    pulsaTapa (monButtons[0]);
    const int monApagado = engine.getMonitor() > 0.0f ? 1 : 0;
    pulsaTapa (monButtons[1]);
    const int monPuesto  = engine.getMonitor() > 0.0f ? 1 : 0;

    //  Y LA GUARDA DE RUTA, que es lo que separa un monitor de un acople: sin
    //  cascos la produccion se cuela en la toma y el microfono cierra el lazo.
    //  `ZATI_RUTA=altavoz` la convierte en una ENTRADA del banco -lo mismo que
    //  ZATI_SKIN con la carcasa- porque en un escritorio no hay ruta que
    //  preguntar y esta regla solo existiria en el telefono.
    const int monAltavoz = RutaAudio::porAltavoz() ? 1 : 0;

    //  Y se recuerda, con el valor borrado a mano antes de leer.
    saveMonitorPref();
    monitorOn = false;
    loadMonitorPref();
    const int monVuelve = monitorOn ? 1 : 0;

    std::cout << "{\"cuenta\":1"
              << ",\"compases\":\"" << armados << "\""
              << ",\"espera\":\""   << esperas << "\""
              << ",\"clic_tras_armar\":" << clicTrasArmar
              << ",\"vuelve\":"          << vuelve
              << ",\"clic_vuelve\":"     << clicVuelve
              << ",\"mon_apagado\":"     << monApagado
              << ",\"mon_puesto\":"      << monPuesto
              << ",\"mon_altavoz\":"     << monAltavoz
              << ",\"mon_vuelve\":"      << monVuelve
              << "}" << std::endl;
}

// ==========================================================================
//  LA APP SE VE IGUAL A 60 QUE A 120. Ver Tests/fps.py.
//
//  Es la regla que hacia falta y no existia, y existe porque el fallo que
//  arregla estaba puesto HOY: las constantes de tiempo visuales estaban
//  escritas como un factor POR CUADRO y documentadas contra treinta cuadros
//  por segundo —`peak *= 0.72f`, `hold *= 0.985f`, `clipHold = 90` («~3 s a 30
//  cuadros»), `padFlash *= 0.8f`—. Treinta es lo que tenia la gama ALTA: en un
//  movil de gama basica el mismo aviso de clip duraba NUEVE segundos y la
//  aguja caia tres veces mas lento. Desde que el dibujo cuelga del vblank
//  serian ademas 60, 90 o 120 segun el panel, o sea el mismo fallo con mas
//  velocidades.
//
//  Se mide en MILISEGUNDOS DE RELOJ y no en cuadros, que es lo unico que
//  separa las dos formas de escribirlo: contar cuadros da el mismo numero con
//  el fallo puesto y sin el.
//
//  Y por el camino de verdad —`SpectrumDisplay::setSamples` y `pintaCuadro`—
//  y no repitiendo la formula aqui: *un banco que repite la constante del
//  codigo no prueba el codigo*, que es lo que esta casa ya pago con la mascara
//  del lanzador.
void MainComponent::auditBalistica()
{
    auto una = [this] (double dtMs)
    {
        float lleno[64], vacio[64] = {};
        for (auto& v : lleno) v = 1.0f;

        //  La aguja: un pico a fondo de escala y luego silencio, contando
        //  hasta que cae por debajo de la decima parte. Con 100 ms de
        //  constante son 100 * ln(10) = 230 ms, se pinte a la cadencia que
        //  se pinte.
        cristal.setSamples (lleno, 64, dtMs);
        double aguja = 0.0, clip = 0.0;
        for (int i = 0; i < 20000 && cristal.nivelAguja() >= 0.1f; ++i)
        {
            cristal.setSamples (vacio, 64, dtMs);
            aguja += dtMs;
        }

        //  Y el aviso de recorte, que es el otro extremo de la escala: tres
        //  segundos enteros, o sea lo que se tarda en levantar la vista.
        cristal.setSamples (lleno, 64, dtMs);
        for (int i = 0; i < 20000 && cristal.avisoClipMs() > 0.0; ++i)
        {
            cristal.setSamples (vacio, 64, dtMs);
            clip += dtMs;
        }

        //  Y EL DESTELLO DE UN PAD, que vive en `pintaCuadro` y no en una
        //  clase suya: se enciende a mano y se cuentan los cuadros de verdad
        //  hasta que se apaga. Es el unico de los tres que se mide llamando a
        //  la funcion que dibuja.
        padFlash[0] = 1.0f;
        double destello = 0.0;
        for (int i = 0; i < 20000 && padFlash[0] > 0.0f; ++i)
        {
            pintaCuadro (dtMs);
            destello += dtMs;
        }

        return std::array<double, 3> { aguja, clip, destello };
    };

    //  Las dos cadencias que separan un panel de 60 de uno de 120, con el
    //  MISMO binario y el mismo estado: lo unico que cambia es el `dt`.
    const auto a60  = una (1000.0 / 60.0);
    const auto a120 = una (1000.0 / 120.0);

    std::cout << "{\"balistica\":1"
              << ",\"aguja60\":"    << a60[0]  << ",\"aguja120\":"    << a120[0]
              << ",\"clip60\":"     << a60[1]  << ",\"clip120\":"     << a120[1]
              << ",\"destello60\":" << a60[2]  << ",\"destello120\":" << a120[2]
              << "}" << std::endl;

    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

// ============================================================================
//  LOS MODOS ARMADOS. Ver Tests/modos.py.
//
//  NINGUNA DE LAS ONCE REGLAS DE `expo.py` PUEDE VER NADA DE ESTO. Un modo es
//  un ESTADO -cambia lo que hace el MISMO gesto- y una rejilla que dispara
//  donde deberia aislar se maqueta perfecta: no solapa, no se sale, no corta un
//  rotulo, no mide cero y esta traducida. Es la familia de los cinco fallos del
//  compas del piano, otra vez.
//
//  Y SE MIDE POR EL GESTO: se construye un `MouseEvent` y se llama a
//  `PadButton::mouseDown`, que es donde vive todo lo nuevo. Llamar a
//  `setPadSolo` por dentro se salta justo el codigo que decide si tocar un pad
//  suena o aisla.
// ============================================================================
void MainComponent::auditModos()
{
    juce::AudioBuffer<float> b (2, 128);
    auto vivas = [this, &b]
    {
        b.clear();
        engine.renderNextBlock (b, 0, 128);
        return engine.getActiveVoiceCount();
    };

    auto tocaPad = [this] (int i)
    {
        auto* pad = pads[i];
        const auto punto = juce::Point<float> ((float) (pad->getWidth() / 2),
                                               (float) (pad->getHeight() / 2));
        const auto ahora = juce::Time::getCurrentTime();
        juce::MouseEvent ev (juce::Desktop::getInstance().getMainMouseSource(),
                             punto, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                             pad, pad, ahora, punto, ahora, 1, false);
        pad->mouseDown (ev);
        pad->mouseUp (ev);
    };

    //  1. CON EL MODO ARMADO, TOCAR AISLA Y NO SUENA — las dos mitades.
    //
    //  Solo la primera la cumple un modo que ademas dispara: «aisla» seria
    //  verdad y «es un modo» no, y el golpe que quieres oir aislado te lo
    //  comerias tu en el mismo toque. Y solo la segunda la cumple una tapa
    //  muerta, que no aisla nada.
    engine.postPanic();
    vivas();
    soloButton.setToggleState (true, juce::sendNotificationSync);
    tocaPad (5);
    const int vocesConSolo = vivas();
    const int aislado = engine.isPadSoloed (5) ? 5 : -1;

    std::cout << "{\"modos\":1,\"armado\":" << (soloArmado ? 1 : 0)
              << ",\"aislado\":" << aislado
              << ",\"anySolo\":" << (engine.anySolo() ? 1 : 0)
              << ",\"voces\":" << vocesConSolo << "}" << std::endl;

    //  2. Y MANTENER LOS QUITA TODOS, que es la otra mitad de la pareja: vaciar
    //     los solos es lo unico de esta funcion que no se deshace tocando otra
    //     vez, asi que no puede compartir gesto con armarla.
    if (soloButton.onHold) soloButton.onHold();
    std::cout << "{\"modos\":2,\"trasMantener\":" << (engine.anySolo() ? 1 : 0) << "}" << std::endl;

    //  3. EL LIENZO DICE QUE HAY UN MODO PUESTO, y se mide en PIXELES.
    //
    //  Un booleano que el codigo se pone a si mismo no mide nada: es
    //  exactamente el fallo de `caraLista`, que decia «tapada» con la cara
    //  entera a la vista. Se pinta el MISMO pad con el modo quitado y con el
    //  modo puesto y se cuentan los pixeles que cambian.
    auto pinta = [] (juce::Component& c)
    {
        juce::Image img (juce::Image::ARGB, juce::jmax (1, c.getWidth()),
                         juce::jmax (1, c.getHeight()), true);
        juce::Graphics gg (img);
        c.paint (gg);
        return img;
    };
    auto difieren = [] (const juce::Image& a, const juce::Image& c)
    {
        if (a.getWidth() != c.getWidth() || a.getHeight() != c.getHeight()) return -1;
        int n = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt (x, y) != c.getPixelAt (x, y)) ++n;
        return n;
    };

    ponSoloArmado (false);
    const auto padSin = pinta (*pads[0]);
    ponSoloArmado (true);
    const auto padCon = pinta (*pads[0]);
    ponSoloArmado (false);

    //  Y EL PIANO, con su herramienta. Sin herramienta la rejilla se comporta
    //  como siempre y por eso no lleva marco: un aviso permanente no avisa.
    showSeqPage (seqPagePiano);
    openSheet (seqSheet, secButton);
    resized();
    pianoGrid.setHerramienta (0);
    const auto pianoSin = pinta (pianoGrid);
    pianoGrid.setHerramienta (2);            // TIJERAS
    const auto pianoCon = pinta (pianoGrid);
    pianoGrid.setHerramienta (0);

    std::cout << "{\"modos\":3,\"pad\":" << difieren (padSin, padCon)
              << ",\"piano\":" << difieren (pianoSin, pianoCon) << "}" << std::endl;

    //  4. Y UN PAD CALLADO POR EL SOLO DE OTRO SE VE. El solo se pone en la
    //     mesa y la mesa TAPA la rejilla: hasta hoy la cara no decia nada de
    //     que doce pads estuvieran mudos, asi que tocabas uno y no sonaba.
    engine.clearSolo();
    refrescaRejillaModo();
    const auto mudoSin = pinta (*pads[3]);
    engine.setPadSolo (0, true);
    refrescaRejillaModo();
    const auto mudoCon = pinta (*pads[3]);
    engine.clearSolo();
    refrescaRejillaModo();

    std::cout << "{\"modos\":4,\"mudo\":" << difieren (mudoSin, mudoCon) << "}" << std::endl;

}

// ---------------------------------------------------------------------------
//  EL BANCO DE TOMAS: DONDE CAE LO QUE SE GRABA.
//
//  NINGUNA DE LAS TRECE REGLAS DE `expo.py` PUEDE VER NADA DE ESTO: es un fallo
//  de INDICE y de estado, y una toma que cae en el pad equivocado se maqueta
//  perfecta -no solapa, no se sale, no corta un rotulo, no mide cero y esta
//  traducida-. Es la familia de los cinco fallos del compas del piano.
//
//  Y SE MIDE POR LA TAPA -`songRecBtn.onClick`- y no llamando a
//  `grabaAlArreglo` por dentro, que es justo donde no vive ninguno de estos
//  fallos. Lo unico que se planta es el MICROFONO, que en un escritorio no
//  existe: la toma se cierra grabando el MASTER, que es la otra puerta del
//  mismo grabador, y asi `finishRecording` devuelve un buffer de verdad y el
//  camino de aterrizaje -`assignSampleToPad (recordingSlot, ...)`- corre
//  entero. Es lo mismo que hacen `ZATI_DLC` con los packs y `ZATI_INSETS` con
//  los margenes: convertir en ENTRADA lo que si no seria «lo que hubiera».
// ---------------------------------------------------------------------------
void MainComponent::auditTomas()
{

    //  Sin cuenta atras, o la toma se queda esperando un compas que en el banco
    //  no llega: lo que se mide aqui es el destino y no el arranque, que ya lo
    //  mide `Tests/cuenta.py` con sus dos cifras.
    pulsaTapa (cuentaButtons[0]);

    //  Y CON EL PAD 01 ELEGIDO, que es donde la queja llego: la caida de antes
    //  cogia `selectedPad`, asi que lo que se comia era el pad que tuvieras
    //  tocado - y recien abierta la app ese es el primero.
    selectPad (0);

    auto toma = [this] () -> int
    {
        pulsaTapa (&songRecBtn);
        if (! grabandoAlArreglo) return -1;         // no habia sitio: no arranco
        const int slot = recordingSlot;
        engine.startRecording (slot, true);         // el master en vez del micro
        engine.setPlaying (true);
        for (int i = 0; i < 4; ++i) bombeaAudioDePrueba();
        pulsaTapa (&songRecBtn);
        return slot;
    };

    auto cuantosDeFabrica = [this]
    {
        int n = 0;
        for (int i = 0; i < kNumPads; ++i) if (padDeFabrica[(size_t) i]) ++n;
        return n;
    };

    //  1. LA TOMA CAE EN EL BANCO DE TOMAS *Y EL PAD 01 SIGUE INTACTO*.
    //
    //  Con DOS cifras porque una se engaña: «cae en C01» lo cumple tambien un
    //  codigo que escribe siempre el mismo pad, y «el 01 sigue intacto» lo
    //  cumple una toma que no llego a grabarse. La segunda es la queja tal cual
    //  llego -«cuando grabo se sobrescribe el pad uno»- y con la caida de antes
    //  sale `A01` y el nombre de fabrica sustituido.
    const juce::String nombre01 = padName[0];
    const int primera = toma();

    //  2. Y LA SEGUNDA CAE EN OTRO PAD. Es la mitad que no se ve: un clip
    //     apunta al PAD, asi que dos tomas en el mismo hueco reescriben el
    //     audio de la primera y el clip ya puesto pasa a sonar otra cosa.
    const int segunda = toma();

    std::cout << "{\"tomas\":1,\"primera\":\"" << etiquetaPad (juce::jmax (0, primera))
              << "\",\"segunda\":\"" << etiquetaPad (juce::jmax (0, segunda))
              << "\",\"pad01\":\"" << padName[0]
              << "\",\"pad01antes\":\"" << nombre01
              << "\",\"nombre\":\"" << padName[(size_t) juce::jmax (0, primera)]
              << "\",\"banco\":" << bancoTomas << "}" << std::endl;

    //  3. CON EL BANCO LLENO DE LO QUE PUSO LA PERSONA, NO GRABA Y LO DICE.
    //
    //  El estado se pone a mano -los dieciseis del banco dejan de ser de
    //  fabrica, que es lo que pasa cuando los cargas tu- y lo que se mide es la
    //  puerta: que `padParaToma` diga -1 y que la tapa NO arranque. Con dos
    //  cifras: sin la segunda, «dice que esta lleno» lo cumple tambien una app
    //  que lo dice y graba igual.
    const int base = bancoTomas * kPadsPerBank;
    for (int i = 0; i < kPadsPerBank; ++i)
    {
        padHasSample[(size_t) (base + i)] = true;
        padDeFabrica[(size_t) (base + i)] = false;
    }
    const int hueco = padParaToma();
    pulsaTapa (&songRecBtn);
    const int arranco = grabandoAlArreglo ? 1 : 0;
    if (grabandoAlArreglo) { pulsaTapa (&songRecBtn); }

    std::cout << "{\"tomas\":2,\"lleno\":" << (hueco < 0 ? 1 : 0)
              << ",\"grabando\":" << arranco << "}" << std::endl;

    //  4. Y LA MARCA VUELVE DEL FICHERO.
    //
    //  Con el MISMO arbol que escribe el proyecto y la sesion, borrandola a
    //  mano entre medias: si al volver sigue puesta no es que se haya guardado,
    //  es que nadie la quito. Sin esto la regla cambiaria entre el primer
    //  arranque y el segundo -la sesion devuelve los sesenta y cuatro desde sus
    //  WAV- y la toma siguiente se comeria la anterior.
    for (int i = 0; i < kNumPads; ++i) padDeFabrica[(size_t) i] = true;
    padDeFabrica[(size_t) base] = false;
    const int antesDeGuardar = cuantosDeFabrica();
    const auto estado = captureState();
    for (int i = 0; i < kNumPads; ++i) padDeFabrica[(size_t) i] = false;
    applyState (estado);
    const int vuelven = cuantosDeFabrica();

    std::cout << "{\"tomas\":3,\"antes\":" << antesDeGuardar
              << ",\"vuelven\":" << vuelven << "}" << std::endl;
}
