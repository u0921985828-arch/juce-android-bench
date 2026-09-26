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
                std::cout << (b ? "," : "") << celdaCancion (ln, b);
            std::cout << "]";
        }
        //  Y LA LISTA DE VERDAD, que es lo que el modelo guarda desde que un
        //  bloque puede empezar a mitad de compas y durar medio: la rejilla de
        //  celdas de arriba dice DONDE hay algo y ya no puede decir en que paso
        //  arranca ni con que paso del patron -el `offset`- y las dos cosas son
        //  exactamente lo que esta tanda anade. En PASOS absolutos.
        std::cout << "],\"bloques\":[";
        {
            const int pc = juce::jmax (1, engine.pasosPorCompas());
            bool primero = true;
            for (const auto& b : bloques)
            {
                std::cout << (primero ? "" : ",") << "[" << b.lane << "," << b.bank << ","
                          << (b.compas * pc + b.paso) << "," << b.largo << ","
                          << b.offset << "," << (b.mudo ? 1 : 0) << "]";
                primero = false;
            }
        }
        std::cout << "],\"pc\":" << engine.pasosPorCompas() << "}" << std::endl;
    };

    //  UNA CANCION QUE SE LEE DE UN VISTAZO: el carril n lleva el valor n+1 en
    //  el compas n, y nada mas. Asi cualquier desplazamiento se ve en la
    //  posicion Y en el valor, y una operacion que mueve la fila equivocada no
    //  se puede confundir con una que mueve la columna equivocada.
    vaciaCancion();
    engine.setSongLength (8);
    for (int ln = 0; ln < AudioEngine::kSongLanes; ++ln)
        ponBloqueCompas (ln, ln * 2, ln + 1);
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
    //  COPIAR Y PEGAR YA NO SON DE COMPAS SINO DE BANDA. Las dos tapas se
    //  retiraron porque copiaban una columna de cuatro celdas sin sus clips y
    //  sin poder decir «de aqui a aqui»; lo que se pidio es copiar MEDIO
    //  patron. Se mide la banda: el compas 2 entero de los cuatro carriles,
    //  pegado en el 5.
    {
        const int pc = juce::jmax (1, engine.pasosPorCompas());
        songBanda (0, 2 * pc, AudioEngine::kSongLanes - 1, 3 * pc);
        songCopiaSel();
        songMarcaLane = 0;
        songMarcaPaso = 5 * pc;
        songPegaSel();
        songVaciaSel();
    }
    songCursor = 5;
    fila ("pegar el 2 en el 5");

    //  EL PATRON. Un golpe en el paso 0 del pad 0 y otro en el 3 del pad 1,
    //  con nota y fuerza distintas de las de por defecto, para que se vea si
    //  el desplazamiento se lleva TODO lo que un paso lleva o solo el "suena".
    selectedPattern = 0;
    engine.clearPattern (0);
    for (int st = 0; st < AudioEngine::kNumSteps; ++st)
        for (int p = 0; p < kNumPads; ++p)
            pattern[0][(size_t) st][(size_t) p] = false;

    //  Y los NUEVE campos, todos DISTINTOS de su defecto y con un acorde de
    //  verdad. Con solo nota y fuerza no se podia ver lo que la queja decia:
    //  que copiar un patron se dejaba la velocidad y que de un acorde de tres
    //  notas solo se pegaba la tonica.
    engine.setPatternLength (0, 16);
    pattern[0][0][0] = true;  engine.setStep (0, 0, 0, true);
    engine.setStepNote  (0, 0, 0, 5);
    engine.setStepVel   (0, 0, 0, 90);
    engine.setStepRoll  (0, 0, 0, 3);
    engine.setStepLen   (0, 0, 0, 9);
    engine.setStepNudge (0, 0, 0, -25);
    engine.setStepLock  (0, 0, 0, 33);
    engine.setStepExtra (0, 0, 0, 0, 4, true);
    engine.setStepExtra (0, 0, 0, 1, 7, true);
    engine.setStepPLock (0, 0, 0, AudioEngine::plockAtaque, 11);
    engine.setStepPLock (0, 0, 0, AudioEngine::plockCaida,  22);
    engine.setStepPLock (0, 0, 0, AudioEngine::plockInicio, 44);
    engine.setStepPLock (0, 0, 0, AudioEngine::plockPan,    88);
    pattern[0][3][1] = true;  engine.setStep (0, 3, 1, true);
    engine.setStepRoll (0, 3, 1, 4);

    //  Las notas de mas del acorde, en una lista: es lo que la queja nombra
    //  -«de tres notas solo se pega una»- y en crudo son un entero opaco.
    auto acordeDe = [this] (int b, int st, int p)
    {
        juce::String t = "[";
        bool primera = true;
        for (int i = 0; i < AudioEngine::kExtraNotes; ++i)
        {
            const int semi = engine.getStepExtra (b, st, p, i);
            if (semi == -128) continue;
            t << (primera ? "" : ",") << semi;
            primera = false;
        }
        return (t + "]").toStdString();
    };

    auto plocksDe = [this] (int b, int st, int p)
    {
        juce::String t = "[";
        for (int i = 0; i < 4; ++i)
            t << (i ? "," : "") << engine.getStepPLock (b, st, p, i);
        return (t + "]").toStdString();
    };

    auto patron = [this, acordeDe, plocksDe] (const char* que, int b = 0)
    {
        const int len = engine.getPatternLength (b);
        std::cout << "{\"pat\":\"" << que << "\",\"largo\":" << len << ",\"pasos\":[";
        bool first = true;
        for (int st = 0; st < len; ++st)
            for (int p = 0; p < kNumPads; ++p)
                if (pattern[(size_t) b][(size_t) st][(size_t) p])
                {
                    std::cout << (first ? "" : ",") << "[" << st << "," << p << ","
                              << engine.getStepNote  (b, st, p) << ","
                              << engine.getStepVel   (b, st, p) << ","
                              << engine.getStepRoll  (b, st, p) << ","
                              << engine.getStepLen   (b, st, p) << ","
                              << engine.getStepNudge (b, st, p) << ","
                              << engine.getStepLock  (b, st, p) << ","
                              << acordeDe (b, st, p) << "," << plocksDe (b, st, p) << "]";
                    first = false;
                }
        std::cout << "]}" << std::endl;
    };

    patron ("inicial");

    //  COPIAR Y PEGAR EL PATRON, con DOS cifras y no una.
    //
    //  La primera dice lo que VIAJA: los nueve campos del patron 0 tienen que
    //  aparecer en el 1. Sin ella, "pega" lo cumple un codigo que solo mueve el
    //  encendido, que es lo que habia.
    //
    //  La segunda dice lo que se LIMPIA, y es la mitad invisible del fallo: el
    //  destino se ensucia A MANO antes de pegar, en un paso donde el origen
    //  esta APAGADO. Quien pegaba escribia encima sin vaciar, asi que ese paso
    //  se quedaba con la fuerza y el acorde del patron anterior - la figura
    //  nueva sonando con los parametros de la vieja. Tiene que volver a su
    //  defecto.
    auto crudo = [this, acordeDe, plocksDe] (const char* que, int b, int st, int p)
    {
        std::cout << "{\"arr\":\"" << que << "\",\"paso\":[" << st << "," << p << ","
                  << engine.getStepNote  (b, st, p) << ","
                  << engine.getStepVel   (b, st, p) << ","
                  << engine.getStepRoll  (b, st, p) << ","
                  << engine.getStepLen   (b, st, p) << ","
                  << engine.getStepNudge (b, st, p) << ","
                  << engine.getStepLock  (b, st, p) << ","
                  << acordeDe (b, st, p) << "," << plocksDe (b, st, p) << "]}" << std::endl;
    };

    selectedPattern = 0;
    copyPattern();

    engine.clearPattern (1);
    for (int st = 0; st < AudioEngine::kNumSteps; ++st)
        for (int p = 0; p < kNumPads; ++p)
            pattern[1][(size_t) st][(size_t) p] = false;
    engine.setStepVel   (1, 5, 0, 40);
    engine.setStepExtra (1, 5, 0, 0, 2, true);
    engine.setStepExtra (1, 5, 0, 1, 9, true);
    engine.setStepLen   (1, 5, 0, 7);
    crudo ("basura en el destino", 1, 5, 0);

    selectedPattern = 1;
    pastePattern();
    patron ("pegado", 1);
    crudo ("tras pegar", 1, 5, 0);

    selectedPattern = 0;
    rotatePattern (+1);   patron ("adelante");
    rotatePattern (-1);   patron ("atras");
    doublePattern();      patron ("doblado");

    //  Y COPIAR / PEGAR LA FILA DE UN PAD, que es el camino que esta casa da
    //  por bueno desde hace tandas y que no ejecutaba NINGUNA prueba. Va aqui
    //  y no antes: hecho antes, el pad 2 sale en los tres volcados de arriba y
    //  lo que se mide deja de ser la rotacion.
    selectedPad = 0;
    copiarFila();
    selectedPad = 2;
    pegarFila();
    crudo ("fila pegada", 0, 0, 2);

    //  RECORTAR Y ALARGAR UN BLOQUE. Un patron de dos compases puesto en el
    //  compas 1: acortarlo a uno tiene que tirar la cola, alargarlo a tres
    //  tiene que ponerla, y alargarlo contra el bloque de al lado no tiene que
    //  hacer nada - comerse lo del vecino seria borrar algo que nadie ha
    //  pedido borrar.
    vaciaCancion();
    engine.setSongLength (8);
    ponBloqueCompas (0, 1, 1, 2);          // dos compases, del 1 al 2
    ponBloqueCompas (0, 4, 2);             // el vecino, para que ALARGAR choque
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
    //  EL SILENCIO VIAJA DENTRO DEL BLOQUE desde que la cancion es una lista:
    //  era un bit por compas en cuatro mascaras y por eso silenciar una COLA no
    //  callaba nada -el motor solo miraba la cabeza-. Se marcan DOS y en
    //  carriles distintos, porque una vuelta con un solo bit puesto la cumple
    //  igual un lector que se quedo con el primero de la lista.
    ponBloqueCompas (2, 6, 3);
    for (auto& b : bloques)
        if ((b.lane == 0 && b.compas == 1) || (b.lane == 2 && b.compas == 6)) b.mudo = true;

    const auto arbol = captureState();
    engine.setSongLaneMute (0, false);
    engine.setSongLaneMute (3, false);
    engine.clearSongLoop();
    //  BORRADA A MANO ENTRE MEDIAS, que es lo unico que separa «se ha
    //  guardado» de «nadie lo quito».
    for (auto& b : bloques) b.mudo = false;
    applyState (arbol);

    std::cout << "{\"vuelta\":1,\"mudos\":[";
    for (int ln = 0; ln < AudioEngine::kSongLanes; ++ln)
        std::cout << (ln ? "," : "") << (engine.isSongLaneMuted (ln) ? 1 : 0);
    std::cout << "],\"bucle\":[" << engine.getSongLoopFrom() << ","
              << engine.getSongLoopTo() << "],\"bloques mudos\":[";
    {
        bool primero = true;
        for (const auto& b : bloques)
            if (b.mudo)
            {
                std::cout << (primero ? "" : ",") << "[" << b.lane << "," << b.compas << "]";
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
    vaciaCancion();
    engine.setSongLength (8);
    ponBloqueCompas (0, 0, -(33 + 1));      // el pad 33: banco C, fuera de la rejilla
    ponBloqueCompas (1, 1, -(63 + 1));      // y el ultimo de los sesenta y cuatro
    Playlist::zatisFueraDeRango = 0;
    refreshSong();
    {
        juce::Image img (juce::Image::ARGB, juce::jmax (1, songGrid.getWidth()),
                         juce::jmax (1, songGrid.getHeight()), true);
        juce::Graphics g (img);
        songGrid.paintEntireComponent (g, false);
    }
    std::cout << "{\"golpe\":\"suelto\",\"celdas\":["
              << celdaCancion (0, 0) << "," << celdaCancion (1, 1)
              << "],\"zatis\":" << songGrid.numZatis()
              << ",\"fuera\":" << Playlist::zatisFueraDeRango << "}" << std::endl;

    //  Y LO QUE EL FICHERO DE PROYECTO PUEDE METER EN UN BLOQUE. El valor sale
    //  de `getIntValue()` sobre un texto, o sea cualquier entero; la puerta es
    //  `publicaBloques`, que es quien traduce la lista de la cara a la tabla
    //  que suena. Se meten tres a mano -dos imposibles y uno valido- y se mide
    //  cuantos LLEGAN al motor: el de un pad que no existe y el de un banco que
    //  no existe se quedan fuera, el pad 63 pasa.
    {
        const int antes = engine.numBloques();
        BloqueUI malo1; malo1.lane = 2; malo1.bank = -9999;  malo1.largo = 1;
        BloqueUI malo2; malo2.lane = 2; malo2.bank = 999999; malo2.compas = 1; malo2.largo = 1;
        BloqueUI bueno; bueno.lane = 2; bueno.bank = -(AudioEngine::kNumPads);
        bueno.compas = 2; bueno.largo = 1;
        bloques.push_back (malo1); bloques.push_back (malo2); bloques.push_back (bueno);
        //  DOS CIFRAS Y NO UNA, y la nueva es la que mide de verdad. `llegan`
        //  sale de `bloquesVivos`, que lo escribe el hilo de audio al adoptar
        //  la tabla, y aqui no se ha procesado ni un bloque de audio: daba 0
        //  con el guardia y 0 sin el, o sea que la mitad de «y lo bueno pasa»
        //  no se podia escribir. `validos` es lo que `publicaBloques` acepto,
        //  contado en la puerta misma y en el hilo de mensajes: de los tres
        //  metidos tiene que pasar UNO, y con los dos que ya habia del golpe
        //  suelto son TRES de cinco. Se dejan las dos porque dicen cosas
        //  distintas -una la puerta, otra el motor- y borrar la floja seria
        //  perder el dia que el motor deje de adoptar.
        const int validos = publicaBloques();
        std::cout << "{\"celda\":\"acotada\",\"puestos\":" << (int) bloques.size()
                  << ",\"validos\":" << validos
                  << ",\"llegan\":" << (engine.numBloques() - antes) << "}" << std::endl;
        bloques.pop_back(); bloques.pop_back(); bloques.pop_back();
        publicaBloques();
    }

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
        vaciaCancion();
        ponBloqueCompas (0, 0, pat + 1, 4);
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
        vaciaCancion();
        //  Un bloque de TRES compases en el carril 1, del 2 al 4: tres es el
        //  minimo que lleva asas, asi que con dos la prueba pasaria sin haber
        //  medido nada.
        ponBloqueCompas (1, 2, 1, 3);
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
        //  EN COMPASES, que es como esta escrita la regla: el primer bloque del
        //  carril, su compas de arranque y cuantos compases ocupa.
        auto bloque = [&] (int carril)
        {
            const int pc = juce::jmax (1, engine.pasosPorCompas());
            for (const auto& b : bloques)
                if (b.lane == carril)
                    return juce::String ("[") + juce::String (b.compas) + ","
                                              + juce::String (juce::jmax (1, b.largo / pc)) + "]";
            return juce::String ("[-1,0]");
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
        vaciaCancion();
        ponBloqueCompas (1, 2, 1, 3);
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
        for (const auto& b : bloques) if (b.lane == 1) { cab = b.compas; break; }
        auto mudoEn = [this] (int carril)
        {
            for (const auto& b : bloques) if (b.lane == carril) return b.mudo ? 1 : 0;
            return 0;
        };
        rej.mouseDown (evento (centro (1, cab >= 0 ? cab : 0)));
        rej.mouseUp   (evento (centro (1, cab >= 0 ? cab : 0)));
        const int mudo1 = mudoEn (1);
        rej.mouseDown (evento (centro (1, cab >= 0 ? cab : 0)));
        rej.mouseUp   (evento (centro (1, cab >= 0 ? cab : 0)));
        const int mudo2 = mudoEn (1);

        //  Y con el LAPIZ, el mismo toque escribe: la herramienta decide.
        if (songToolBtns.size() > 1) songToolBtns[1]->onClick();
        songBrush = 5;
        rej.mouseDown (evento (centro (1, 12)));
        rej.mouseUp   (evento (centro (1, 12)));
        const int pintado = celdaCancion (1, 12);

        std::cout << "{\"arr\":\"herramientas\",\"movido\":" << movido
                  << ",\"entradas\":" << entradasMov
                  << ",\"mudo\":[" << mudo1 << "," << mudo2 << "]"
                  << ",\"con lapiz pinta\":" << pintado << "}" << std::endl;
    }

    //  ------------------------------------------------------------------
    //  LA RED DEBAJO DE LO QUE SE ESCRIBE EN EL SECUENCIADOR
    //  ------------------------------------------------------------------
    //
    //  `pushUndo` cubria las nueve acciones que se piden por una TAPA -PEGAR,
    //  MOVER, DOBLAR, HUMANIZAR, EUCLIDES...- y se dejaba fuera las dos que se
    //  hacen con el dedo mil veces por sesion: encender un paso en la rejilla y
    //  escribir una nota en el piano. Deshacer saltaba por encima de todo lo
    //  tocado hasta la ultima tapa pulsada.
    //
    //  Y `Tests/deshacer.py` no puede ver esto: aquella regla deriva del EMBUDO
    //  de los pads -`assignSampleToPad`- y un paso no ocupa ningun pad. Es la
    //  misma figura que ya se pago con el visor del rack: una regla perfecta de
    //  otra cosa.
    //
    //  CUATRO CIFRAS Y NO UNA, porque cada una sola se engaña sola:
    //    - `pintados` dice que el gesto escribio -sin esto, una foto que
    //      congela la rejilla tambien saldria verde-;
    //    - `entradas` dice que el arrastre dejo UNA foto y no una por celda;
    //    - `tras deshacer` dice que la foto valia; y
    //    - `tras rehacer` que la rama de rehacer sigue viva, que es lo que se
    //      pierde si alguien apila una foto mientras se repone.
    {
        openSheet (seqSheet, secButton);
        showSeqPage (seqPageGrid);
        resized();

        selectedPattern = 0;
        currentBank = 0;
        seqPrimerCelda = 0;
        for (int st = 0; st < AudioEngine::kNumSteps; ++st)
            for (int p = 0; p < kNumPads; ++p)
            { pattern[0][(size_t) st][(size_t) p] = false; engine.setStep (0, st, p, false); }
        selectedPad = 0;
        refreshStepGrid();
        resized();

        auto encendidos = [this]
        {
            int n = 0;
            for (int st = 0; st < AudioEngine::kNumSteps; ++st)
                for (int p = 0; p < kNumPads; ++p)
                    if (pattern[0][(size_t) st][(size_t) p]) ++n;
            return n;
        };

        const auto cuando = juce::Time::getCurrentTime();
        auto evento = [&] (juce::Component& c, juce::Point<float> pt)
        {
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                     pt, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     &c, &c, cuando, pt, cuando, 1, false);
        };

        //  POR EL GESTO Y NO POR DENTRO: lo que se mide es que un ARRASTRE deje
        //  una sola foto, y eso solo lo puede decir el camino del dedo entero
        //  -`mouseDrag` marca `arrastrando` y la rejilla lo reenvia-. Llamando a
        //  `stepCellToggled` por dentro saldria verde con el aviso sin cablear.
        //  Y LA CELDA SE MIDE ANTES DE CADA EVENTO, no una vez. El primer paso
        //  tocado hace aparecer la tira de debajo de la rejilla -es un cambio de
        //  MAQUETA, y `stepCellToggled` pide el `resized` el mismo-, asi que la
        //  celda encoge a mitad del gesto. Con las medidas tomadas una sola vez
        //  el arrastre caia dos veces en la misma columna y esta medida saco
        //  «pintados 3» de cuatro eventos con el codigo correcto: primero se
        //  duda de la prueba.
        auto celda = [this] (int col)
        {
            return juce::Point<float> ((float) StepGrid::kGutter
                                         + stepGrid.celdaAnchoPx() * ((float) col + 0.5f),
                                       stepGrid.celdaAltoPx() * 0.5f);
        };

        //  Y LA PILA SE VACIA ANTES DE CADA MEDIDA. Tiene fondo -`kUndoDepth`,
        //  dieciseis- y al llegar al tope `pushUndo` tira la mas vieja, asi que
        //  el tamaño deja de crecer: contando por tamaño, «tomo la foto» y «tomo
        //  la foto y se cayo la de abajo» dan el mismo numero. Esta medida saco
        //  «entradas 0» con la foto puesta y el codigo correcto — la pila estaba
        //  llena de las quince acciones de las medidas de arriba.
        undoStack.clear(); redoStack.clear();
        const int undo0 = (int) undoStack.size();
        stepGrid.mouseDown (evento (stepGrid, celda (0)));
        for (int c = 1; c < 4; ++c) stepGrid.mouseDrag (evento (stepGrid, celda (c)));
        const int pintados      = encendidos();
        const int entradasPaso  = (int) undoStack.size() - undo0;
        performUndo();
        const int trasDeshacer  = encendidos();
        performRedo();
        const int trasRehacer   = encendidos();

        std::cout << "{\"arr\":\"deshacer pasos\",\"pintados\":" << pintados
                  << ",\"entradas\":" << entradasPaso
                  << ",\"tras deshacer\":" << trasDeshacer
                  << ",\"tras rehacer\":" << trasRehacer << "}" << std::endl;

        //  EL PIANO, QUE ES LA OTRA VISTA DEL MISMO PATRON. En diagonal: por la
        //  MISMA fila un arrastre ESTIRA la nota -es el gesto de cualquier piano
        //  roll- y lo que aqui se mide es el de pintar.
        showSeqPage (seqPagePiano);
        refreshPiano();
        resized();

        for (int st = 0; st < AudioEngine::kNumSteps; ++st)
            for (int p = 0; p < kNumPads; ++p)
            { pattern[0][(size_t) st][(size_t) p] = false; engine.setStep (0, st, p, false); }
        refreshPiano();

        auto tecla = [this] (int col, int fila)
        {
            return juce::Point<float> ((float) pianoGrid.canalIzq()
                                         + pianoGrid.celdaAnchoPx() * ((float) col + 0.5f),
                                       pianoGrid.celdaAltoPx() * ((float) fila + 0.5f));
        };

        undoStack.clear(); redoStack.clear();
        const int undoP = (int) undoStack.size();
        pianoGrid.mouseDown (evento (pianoGrid, tecla (0, 6)));
        pianoGrid.mouseDrag (evento (pianoGrid, tecla (1, 5)));
        pianoGrid.mouseDrag (evento (pianoGrid, tecla (2, 4)));
        const int escritas     = encendidos();
        const int entradasNota = (int) undoStack.size() - undoP;
        performUndo();
        const int notasTrasDeshacer = encendidos();

        std::cout << "{\"arr\":\"deshacer notas\",\"escritas\":" << escritas
                  << ",\"entradas\":" << entradasNota
                  << ",\"tras deshacer\":" << notasTrasDeshacer << "}" << std::endl;

        //  Y LA REJILLA -cuantos cuadrados dura un pulso-, que reparte el patron
        //  entero por otro reloj y hasta hoy no tenia vuelta. La segunda cifra
        //  es la que caza la trampa de tomar la foto en un `onValueChange`:
        //  deshacer repone el mando CON aviso, asi que sin la bandera de
        //  `applyState` la pila crece mientras se desapila y «deshacer» se
        //  queda dando vueltas sobre si mismo.
        //  Y SE MIDE LA VISTA Y NO LAS NEGRAS POR PASO. Aqui decia
        //  `getStepBeats`, que era lo mismo mientras la rejilla ERA el paso
        //  guardado; desde que es una vista aparte, ensanchar la rejilla no
        //  mueve el paso ni un bit -ese es el arreglo- y una prueba que
        //  siguiera pidiendo que se moviera defenderia el fallo. El paso va al
        //  lado, porque deshacer tiene que devolver los dos.
        undoStack.clear(); redoStack.clear();
        const int   vistaAntes = vistaRejilla;
        const float pasoAntes  = engine.getStepBeats();
        const int   undoR      = (int) undoStack.size();
        gridSlider.setValue (gridSlider.getValue() == 0.0 ? 1.0 : 0.0, juce::sendNotificationSync);
        const int   vistaTras  = vistaRejilla;
        const int   entradasRej = (int) undoStack.size() - undoR;
        performUndo();
        const int   vistaVuelta = vistaRejilla;
        const float pasoVuelta  = engine.getStepBeats();
        const int   pilaTrasDeshacer = (int) undoStack.size();

        std::cout << "{\"arr\":\"deshacer rejilla\",\"antes\":" << vistaAntes
                  << ",\"tras el mando\":" << vistaTras
                  << ",\"entradas\":" << entradasRej
                  << ",\"tras deshacer\":" << vistaVuelta
                  << ",\"paso\":[" << juce::String (pasoAntes, 4) << ","
                                     << juce::String (pasoVuelta, 4) << "]"
                  << ",\"pila\":[" << undoR << "," << pilaTrasDeshacer << "]}" << std::endl;

        //  LA REJILLA MIDE EL CUADRADITO Y NO TOCA EL PATRON.
        //
        //  El fallo que esto caza no se ve contando golpes: al cambiar de
        //  rejilla los tres seguian encendidos y lo que cambiaba era lo que
        //  VALE un paso, o sea que el patron entero se oia al doble. La queja
        //  fue «no deberia cambiarse ni el tiempo, ni los BPM, ni nada del
        //  proyecto, solo lo visual». La cifra es el PULSO de cada golpe
        //  -paso por lo que dura un paso-, antes y despues, y las dos listas
        //  tienen que ser iguales.
        //
        //  Y AL LADO, LO QUE SI TIENE QUE CAMBIAR: cuantas CASILLAS ocupa el
        //  patron. Una medida que solo dice «no ha cambiado nada» la aprueba
        //  igual un mando desconectado. De 1/16 a 1/8 el mismo compas pasa de
        //  dieciseis casillas a ocho y el paso guardado no se mueve: eso es
        //  literalmente «el cuadradito es mas gordo».
        //
        //  Y el tempo tambien, que es la otra mitad de la queja: si los pulsos
        //  cuadraran y ademas alguien tocara los BPM, la musica no.
        {
            for (int st = 0; st < AudioEngine::kNumSteps; ++st)
                for (int pd = 0; pd < kNumPads; ++pd)
                { pattern[0][(size_t) st][(size_t) pd] = false; engine.setStep (0, st, pd, false); }

            //  Se parte de 1/16 -indice 2- y se va a 1/8, que es el caso de la
            //  queja: la rejilla se ENSANCHA y los golpes tienen que quedarse
            //  donde estan.
            //  Y DE UN COMPAS LIMPIO A 1/16, puesto a mano: este bloque hereda
            //  el paso guardado y el largo del que corrio antes, y medir «el
            //  cuadradito se hace mas gordo» sobre tres casillas heredadas
            //  mide el bloque anterior.
            engine.setPasoUnidades (rejillaU (2));
            for (int b = 0; b < AudioEngine::kNumPatterns; ++b)
                engine.setPatternLength (b, engine.pasosPorCompas());
            vistaRejilla = 2;
            gridSlider.setValue (2.0, juce::dontSendNotification);
            reajustaMandoLargo();
            //  EN CASILLAS DE LA VISTA, que es donde cae el dedo: el paso
            //  guardado puede ser mas fino que la casilla y sembrar por indice
            //  de paso mediria otra cosa.
            const int celdasSembradas[3] = { 0, 4, 8 };
            for (int k = 0; k < 3; ++k)
            {
                const int st = pasoDeCelda (celdasSembradas[k]);
                pattern[0][(size_t) st][0] = true; engine.setStep (0, st, 0, true);
            }

            const float beats0  = engine.getStepBeats();
            const double bpm0   = (double) engine.getBpm();
            const int    largo0 = engine.getPatternLength (0);
            const int    celdas0 = celdasDePatron (0);

            auto pulsos = [this] (juce::Array<double>& out)
            {
                out.clearQuick();
                const double b = (double) engine.getStepBeats();
                for (int st = 0; st < AudioEngine::kNumSteps; ++st)
                    if (engine.leePaso (0, st, 0).on) out.add ((double) st * b);
            };

            juce::Array<double> antes, despues;
            pulsos (antes);
            gridSlider.setValue (0.0, juce::sendNotificationSync);       // 1/8
            pulsos (despues);

            auto lista = [] (const juce::Array<double>& a)
            {
                juce::String t;
                for (int k = 0; k < a.size(); ++k) t << (k ? "," : "") << juce::String (a[k], 4);
                return t;
            };

            std::cout << "{\"arr\":\"la rejilla es un zoom\""
                      << ",\"largo\":[" << largo0 << "," << engine.getPatternLength (0) << "]"
                      << ",\"celdas\":[" << celdas0 << "," << celdasDePatron (0) << "]"
                      << ",\"paso antes\":" << juce::String (beats0, 4)
                      << ",\"paso despues\":" << juce::String (engine.getStepBeats(), 4)
                      << ",\"pulsos antes\":[" << lista (antes) << "]"
                      << ",\"pulsos despues\":[" << lista (despues) << "]"
                      << ",\"bpm\":[" << juce::String (bpm0, 2) << ","
                                      << juce::String ((double) engine.getBpm(), 2) << "]}"
                      << std::endl;
        }

        //  Y EL TRESILLO, QUE ES EL CASO QUE NO SALE REDONDO Y POR ESO ES EL
        //  QUE HAY QUE MEDIR. Van los dos sentidos, y no son el mismo:
        //
        //   - DE RECTO A TRESILLO Y VUELTA los pulsos se conservan CLAVADOS
        //     aunque entre una rejilla y su tresillo no haya factor entero.
        //     El paso guardado se afina a 1/48 de pulso -que si divide a las
        //     dos- y la vuelta lo engorda otra vez. La regla de 1/16 a 1/8
        //     -que no mueve el paso guardado siquiera- no puede ver eso.
        //   - Y CON GOLPES QUE SOLO EXISTEN EN EL TRESILLO tampoco se pierde
        //     ninguno, que es lo que esta tanda cambia: el pulso 0.0833 no se
        //     puede decir con casillas de 0.25, pero SI con pasos guardados de
        //     1/48, y la casilla de 0.25 solo es lo que se dibuja. Antes uno
        //     caia encima de otro y se perdia.
        //
        //  Se publica el renglon de estado literal, que es lo que lee la
        //  persona, y no el numero por dentro: *medir el numero y no el
        //  rotulo* es como esta casa se ha comido ya dos fallos.
        {
            auto limpia = [this]
            {
                for (int st = 0; st < AudioEngine::kNumSteps; ++st)
                    for (int pd = 0; pd < kNumPads; ++pd)
                    { pattern[0][(size_t) st][(size_t) pd] = false; engine.setStep (0, st, pd, false); }
            };
            auto siembra = [this] (const int* celdas, int cuantos)
            {
                //  EN CASILLAS DE LA VISTA, que es donde cae el dedo. Sembrar
                //  por indice de paso guardado mediria otra cosa en cuanto el
                //  paso y la casilla dejaran de ser lo mismo.
                for (int k = 0; k < cuantos; ++k)
                {
                    const int st = pasoDeCelda (celdas[k]);
                    pattern[0][(size_t) st][0] = true; engine.setStep (0, st, 0, true);
                }
            };
            auto pulsos = [this] (juce::Array<double>& out)
            {
                out.clearQuick();
                const double b = (double) engine.getStepBeats();
                for (int st = 0; st < AudioEngine::kNumSteps; ++st)
                    if (engine.leePaso (0, st, 0).on) out.add ((double) st * b);
            };
            auto lista = [] (const juce::Array<double>& a)
            {
                juce::String t;
                for (int k = 0; k < a.size(); ++k) t << (k ? "," : "") << juce::String (a[k], 4);
                return t;
            };

            //  IDA Y VUELTA POR EL TRESILLO, con golpes que los dos saben
            //  decir: las casillas 0, 4 y 8 de 1/16 son los pulsos 0, 1 y 2.
            //  Y UN COMPAS JUSTO, puesto a mano: este bloque hereda el largo
            //  del que corrio antes, y medir "el bucle son compases enteros"
            //  sobre medio compas heredado mide el bloque anterior.
            limpia();
            gridSlider.setValue (2.0, juce::sendNotificationSync);       // 1/16
            engine.setPatternLength (0, engine.pasosPorCompas());
            const int rectos[3] = { 0, 4, 8 };
            siembra (rectos, 3);

            juce::Array<double> recto, tresillo, vuelta;
            pulsos (recto);
            gridSlider.setValue (3.0, juce::sendNotificationSync);       // 1/16T
            pulsos (tresillo);
            const int largoTresillo = engine.getPatternLength (0);
            gridSlider.setValue (2.0, juce::sendNotificationSync);       // 1/16 otra vez
            pulsos (vuelta);

            //  Y LOS QUE SOLO EXISTEN EN EL TRESILLO: las casillas 0, 1 y 2 de
            //  1/16T son los pulsos 0, 0.1667 y 0.3333. Mirados con casillas de
            //  0.25 no hay donde dibujarlos por separado; guardados con pasos
            //  de 1/48 de pulso siguen exactamente donde estaban.
            limpia();
            gridSlider.setValue (3.0, juce::sendNotificationSync);       // 1/16T
            engine.setPatternLength (0, engine.pasosPorCompas());
            const int solosDelTresillo[3] = { 0, 1, 2 };
            siembra (solosDelTresillo, 3);

            juce::Array<double> soloTres, apretados;
            pulsos (soloTres);
            gridSlider.setValue (2.0, juce::sendNotificationSync);       // 1/16
            pulsos (apretados);
            const juce::String dicho = status.getText();

            std::cout << "{\"arr\":\"la rejilla y el tresillo\""
                      << ",\"pulsos recto\":[" << lista (recto) << "]"
                      << ",\"pulsos tresillo\":[" << lista (tresillo) << "]"
                      << ",\"pulsos vuelta\":[" << lista (vuelta) << "]"
                      << ",\"largo tresillo\":" << largoTresillo
                      << ",\"pulsos solo tresillo\":[" << lista (soloTres) << "]"
                      << ",\"pulsos apretados\":[" << lista (apretados) << "]"
                      << ",\"largo apretado\":" << engine.getPatternLength (0)
                      << ",\"bucle\":" << juce::String ((double) engine.getPatternLength (0)
                                                        * (double) engine.getStepBeats(), 4)
                      << ",\"compas\":" << engine.pasosPorCompas()
                      << ",\"dicho\":\"" << dicho.replace ("\"", "'") << "\"}"
                      << std::endl;
        }

        //  Y QUE EL PASO GUARDADO NO SE ATASQUE FINO.
        //
        //  Las 42 parejas salen de un compas limpio cada una, asi que no ven
        //  lo que pasa cuando alguien toca el mando siete veces seguidas: el
        //  paso guardado se AFINA para poder decir cada rejilla, y si no se
        //  volviera a ENGORDAR cuando los datos lo permiten, acabaria en 1/48
        //  de pulso para siempre. Ahi un compas son los 192 pasos enteros que
        //  la maquina guarda y el siguiente cambio de rejilla ya no cabe.
        //
        //  No se pierde ni un golpe por el camino -por eso las reglas de
        //  pulsos no lo ven- y sin embargo la app se queda sin poder cambiar
        //  de rejilla. La cifra es el PASO GUARDADO en 1/48 de pulso despues
        //  de cada parada, y al volver a 1/16 con golpes que 1/16 sabe decir
        //  tiene que valer 12 otra vez.
        {
            for (int b = 0; b < AudioEngine::kNumPatterns; ++b)
                for (int st = 0; st < AudioEngine::kNumSteps; ++st)
                    for (int pd = 0; pd < kNumPads; ++pd)
                    { pattern[b][(size_t) st][(size_t) pd] = false; engine.setStep (b, st, pd, false); }

            engine.setPasoUnidades (rejillaU (2));           // 1/16
            for (int b = 0; b < AudioEngine::kNumPatterns; ++b)
                engine.setPatternLength (b, engine.pasosPorCompas());
            vistaRejilla = 2;
            gridSlider.setValue (2.0, juce::dontSendNotification);
            reajustaMandoLargo();

            for (int k = 0; k < 3; ++k)
            {
                const int st = pasoDeCelda (k);
                pattern[0][(size_t) st][0] = true; engine.setStep (0, st, 0, true);
            }

            //  Las siete, y acabando donde empezo.
            const int ruta[8] = { 3, 4, 5, 6, 1, 0, 2, 2 };
            juce::String pasos, largos;
            int negadas = 0;

            for (int k = 0; k < 7; ++k)
            {
                gridSlider.setValue ((double) ruta[k], juce::sendNotificationSync);
                if ((int) gridSlider.getValue() != ruta[k]) ++negadas;
                pasos  << (k ? "," : "") << engine.pasoUnidades();
                largos << (k ? "," : "") << engine.getPatternLength (0);
            }

            juce::String pulsos;
            {
                const double bt = (double) engine.getStepBeats();
                bool primero = true;
                for (int st = 0; st < AudioEngine::kNumSteps; ++st)
                    if (engine.leePaso (0, st, 0).on)
                    { pulsos << (primero ? "" : ",") << juce::String ((double) st * bt, 4); primero = false; }
            }

            std::cout << "{\"arr\":\"la rejilla no se atasca\""
                      << ",\"pasos\":[" << pasos << "]"
                      << ",\"largos\":[" << largos << "]"
                      << ",\"negadas\":" << negadas
                      << ",\"pulsos\":[" << pulsos << "]"
                      << ",\"paso final\":" << engine.pasoUnidades() << "}" << std::endl;
        }

        //  Y EL COMPAS DE LA CANCION SIGUE SIENDO UN COMPAS.
        //
        //  Las reglas de arriba miden el PATRON y ninguna toca la CANCION, que
        //  es donde la correccion del compas no se hizo: `AudioEngine` llevaba
        //  un `kBarSteps = 16` escrito a mano al lado de `pasosPorCompas()`,
        //  que lo deriva. Con el paso guardado en 1/16 los dos coinciden y por
        //  eso nadie lo vio; en cuanto el mando de rejilla afina el paso -y
        //  desde la tanda anterior puede bajar a 1/48 de pulso- el «compas» de
        //  la linea de tiempo pasaba a valer un CUARTO de compas y
        //  `muestrasPorCompas()` devolvia un cuarto de lo que debe: el clip
        //  suena donde no se dibuja, que es literalmente el fallo que el
        //  comentario de esa funcion dice que existe para evitar.
        //
        //  Se mide lo que no puede cambiar: **un compas dura lo que dura**.
        //  Las muestras por compas tienen que salir IGUALES con las siete
        //  rejillas, porque el tempo no lo toca ninguna. No hace falta ni la
        //  frecuencia de muestreo ni el tempo para preguntarlo - solo que las
        //  siete cifras sean la misma-, que es lo que separa esta pregunta de
        //  repetir la cuenta del C++.
        {
            for (int b = 0; b < AudioEngine::kNumPatterns; ++b)
                for (int st = 0; st < AudioEngine::kNumSteps; ++st)
                    for (int pd = 0; pd < kNumPads; ++pd)
                    { pattern[b][(size_t) st][(size_t) pd] = false; engine.setStep (b, st, pd, false); }

            engine.setPasoUnidades (rejillaU (2));           // 1/16
            for (int b = 0; b < AudioEngine::kNumPatterns; ++b)
                engine.setPatternLength (b, engine.pasosPorCompas());
            vistaRejilla = 2;
            gridSlider.setValue (2.0, juce::dontSendNotification);
            reajustaMandoLargo();

            engine.setSongLength (8);
            //  Un clip de UN compas en el compas 3 de la pista 1, puesto con
            //  la misma cuenta que usa la cara: su largo en MUESTRAS sale de
            //  `muestrasPorCompas()`, que es la traduccion que esta regla mide.
            clips.clear();
            {
                ClipUI c;
                c.pad = 0; c.pista = 1; c.compas = 3; c.desde = 0;
                c.largo = (int) engine.muestrasPorCompas();
                c.gain = 1.0f;
                clips.push_back (c);
            }
            publicaClips();

            juce::String pasos, compasPasos, compasMuestras, dibujado;
            for (int i = 0; i < 7; ++i)
            {
                gridSlider.setValue ((double) i, juce::sendNotificationSync);
                pasos          << (i ? "," : "") << engine.pasoUnidades();
                compasPasos    << (i ? "," : "") << engine.pasosPorCompas();
                compasMuestras << (i ? "," : "") << juce::String (engine.muestrasPorCompas(), 1);
                dibujado       << (i ? "," : "") << (clips.empty() ? -1 : clips[0].compas);
            }

            std::cout << "{\"arr\":\"el compas de la cancion\""
                      << ",\"pasos\":[" << pasos << "]"
                      << ",\"compas pasos\":[" << compasPasos << "]"
                      << ",\"compas muestras\":[" << compasMuestras << "]"
                      << ",\"clip compas\":[" << dibujado << "]"
                      << ",\"largo cancion\":" << engine.getSongLength() << "}" << std::endl;

            clips.clear();
            publicaClips();
        }

        //  Y EL CLIP SUENA DONDE SE DIBUJA, CON EL COMPAS EMPEZADO.
        //
        //  La regla de arriba mide el COMPAS, que es lo que habia; desde que
        //  un clip lleva desfase dentro del compas eso deja de bastar: un
        //  motor que ignore el paso pinta el clip a la mitad del compas 3 y lo
        //  toca en el filo, o sea medio compas de error -1000 ms a 120- sin
        //  que nada falle y sin que se vea hasta que suena. Es exactamente la
        //  figura que `muestrasPorCompas()` documenta y la que esta tanda
        //  acaba de poder cometer en un sitio nuevo.
        //
        //  Se mide LO QUE SALE POR EL BUS y no la cuenta: se arranca el
        //  transporte en modo cancion sobre una cancion vacia -asi lo unico
        //  que puede sonar es el clip- y se busca la primera muestra que no es
        //  silencio. El sitio donde se DIBUJA sale de `songClipsVista`, que es
        //  lo que la rejilla recibe, y no de los campos del clip: comparar el
        //  clip consigo mismo no compara nada.
        {
            vaciaCancion();
            publicaBloques();
            engine.setSongLength (8);
            engine.setClick (false);
            //  Y SIN TRAMO EN BUCLE, que lo dejo puesto una medida de antes.
            //  Con el bucle en [2,5) el transporte ARRANCA DENTRO del tramo,
            //  asi que el compas 3 llega al oido en el paso 20 de la corrida y
            //  no en el 52: la primera version de esta regla salio en rojo con
            //  el codigo perfecto. Primero se duda de la prueba.
            engine.setSongLoop (0, 0);
            engine.setPasoUnidades (rejillaU (2));           // 1/16
            vistaRejilla = 2;
            gridSlider.setValue (2.0, juce::dontSendNotification);

            const int    pc = juce::jmax (1, engine.pasosPorCompas());
            constexpr int kPasoDelClip = 4;                  // 1/4 de compas en 1/16

            clips.clear();
            {
                ClipUI c;
                c.pad = 0; c.pista = 1; c.compas = 3; c.paso = kPasoDelClip;
                c.desde = 0; c.largo = (int) engine.muestrasPorCompas();
                c.gain = 1.0f;
                clips.push_back (c);
            }
            publicaClips();
            refreshSong (false);
            const int dibuja = songClipsVista.empty() ? -1 : songClipsVista[0].desdePaso;

            //  El aparato del banco, que aqui no hay tarjeta. Mismas cifras que
            //  `bombeaAudioDePrueba` para no inventarse un segundo contrato.
            constexpr int    kRafaga = 128;
            constexpr double kRate   = 48000.0;
            juce::AudioBuffer<float> bloque (2, kRafaga);
            engine.prepareToPlay (kRate, kRafaga);
            enginePreparedRate  = kRate;
            enginePreparedBlock = kRafaga;

            //  Y LA CUENTA DE MUESTRAS SE PIDE DESPUES DE `prepareToPlay`, que
            //  es quien fija la frecuencia: preguntarla antes devolvia la de la
            //  sesion -44100- mientras el bucle renderiza a 48000, o sea una
            //  regla de tres con dos relojes distintos.
            const double porPaso = engine.muestrasPorCompas() / (double) pc;

            engine.setSongMode (true);
            engine.setPlaying (false);
            bloque.clear(); engine.renderNextBlock (bloque, 0, kRafaga);
            engine.setPlaying (true);

            //  Cinco compases de margen: el clip entra en el 3, asi que si no
            //  ha sonado en cinco es que no va a sonar.
            const int tope = (int) (engine.muestrasPorCompas() * 5.0) / kRafaga + 2;
            std::int64_t sonoEn = -1;
            for (int b = 0; b < tope && sonoEn < 0; ++b)
            {
                bloque.clear();
                engine.renderNextBlock (bloque, 0, kRafaga);
                for (int i = 0; i < kRafaga; ++i)
                    if (std::abs (bloque.getSample (0, i)) > 1.0e-4f
                        || std::abs (bloque.getSample (1, i)) > 1.0e-4f)
                    { sonoEn = (std::int64_t) b * kRafaga + i; break; }
            }
            engine.setPlaying (false);

            //  EL CONTROL: la misma corrida SIN clip tiene que ser silencio.
            //  Sin el, cualquier cosa que sonara -un patron que quedo puesto,
            //  la cola de un pad, el metronomo- se leeria como «el clip» y la
            //  regla mediria el ruido de fondo.
            clips.clear();
            publicaClips();
            engine.setPlaying (true);
            std::int64_t ruidoEn = -1;
            for (int b = 0; b < tope && ruidoEn < 0; ++b)
            {
                bloque.clear();
                engine.renderNextBlock (bloque, 0, kRafaga);
                for (int i = 0; i < kRafaga; ++i)
                    if (std::abs (bloque.getSample (0, i)) > 1.0e-4f
                        || std::abs (bloque.getSample (1, i)) > 1.0e-4f)
                    { ruidoEn = (std::int64_t) b * kRafaga + i; break; }
            }
            engine.setPlaying (false);
            engine.setSongMode (false);

            const double suena = (sonoEn < 0) ? -1.0 : (double) sonoEn / porPaso;
            std::cout << "{\"arr\":\"el clip suena donde se dibuja\""
                      << ",\"dibuja paso\":" << dibuja
                      << ",\"suena paso\":" << juce::String (suena, 2)
                      << ",\"pasos compas\":" << pc
                      << ",\"muestras paso\":" << juce::String (porPaso, 1)
                      << ",\"sin clip\":" << (int) ruidoEn << "}" << std::endl;

            clips.clear();
            publicaClips();
        }

        //  LAS SIETE CONTRA LAS SIETE, IDA Y VUELTA.
        //
        //  Las dos reglas de arriba miden tres pares escogidos a mano, y con
        //  los tres en verde la queja siguio siendo «se deforman los
        //  patrones». Lo que no median es la IDA Y VUELTA COMPLETA de las 42
        //  parejas. Se mide lo que se oye y no los pasos: el PULSO de cada
        //  golpe y el LARGO DEL BUCLE EN PULSOS -pasos por lo que dura un
        //  paso-, que es lo unico que no depende de la rejilla con la que se
        //  mire.
        //
        //  Y TIENEN QUE SALIR LAS 42, incluidas las que la app se NIEGA a
        //  cambiar: negarse es no tocar nada, asi que la vuelta tambien es
        //  exacta. `negada` se publica para que se vea CUALES y cuantas, que
        //  es lo unico que separa «no cabe» de «no funciona».
        {
            const int kRejillas = 7;
            std::cout << "{\"arr\":\"las siete rejillas\",\"vueltas\":[";
            bool primera = true;

            for (int i = 0; i < kRejillas; ++i)
                for (int j = 0; j < kRejillas; ++j)
                {
                    if (i == j) continue;

                    //  CADA PAREJA SALE DE UN COMPAS LIMPIO, y eso hay que
                    //  ponerlo a mano. Encadenadas, el paso guardado de una
                    //  pareja lo hereda la siguiente -se afina y solo se
                    //  engorda cuando los datos dejan- y a la decima pareja se
                    //  estaba midiendo un estado que ningun dedo produce. La
                    //  pregunta que esto contesta es "abro la app, escribo un
                    //  compas y toco el mando", 42 veces.
                    for (int b = 0; b < AudioEngine::kNumPatterns; ++b)
                        for (int st = 0; st < AudioEngine::kNumSteps; ++st)
                            for (int pd = 0; pd < kNumPads; ++pd)
                            { pattern[b][(size_t) st][(size_t) pd] = false; engine.setStep (b, st, pd, false); }

                    engine.setPasoUnidades (rejillaU (i));
                    for (int b = 0; b < AudioEngine::kNumPatterns; ++b)
                        engine.setPatternLength (b, engine.pasosPorCompas());
                    vistaRejilla = i;
                    gridSlider.setValue ((double) i, juce::dontSendNotification);
                    reajustaMandoLargo();

                    //  TRES GOLPES EN LAS TRES PRIMERAS CASILLAS DE LA VISTA,
                    //  que es lo que un dedo haria.
                    for (int k = 0; k < 3; ++k)
                    {
                        const int st = pasoDeCelda (k);
                        pattern[0][(size_t) st][0] = true; engine.setStep (0, st, 0, true);
                    }

                    auto pulsos = [this] (juce::Array<double>& out)
                    {
                        out.clearQuick();
                        const double b = (double) engine.getStepBeats();
                        for (int st = 0; st < AudioEngine::kNumSteps; ++st)
                            if (engine.leePaso (0, st, 0).on) out.add ((double) st * b);
                    };
                    auto bucle = [this]
                    { return (double) engine.getPatternLength (0) * (double) engine.getStepBeats(); };
                    auto lista = [] (const juce::Array<double>& a)
                    {
                        juce::String t;
                        for (int k = 0; k < a.size(); ++k) t << (k ? "," : "") << juce::String (a[k], 4);
                        return t;
                    };

                    juce::Array<double> antes, vuelta;
                    pulsos (antes);
                    const double bucleAntes = bucle();

                    gridSlider.setValue ((double) j, juce::sendNotificationSync);
                    const double bucleMedio = bucle();
                    const bool   negada     = ((int) gridSlider.getValue() != j);

                    gridSlider.setValue ((double) i, juce::sendNotificationSync);
                    pulsos (vuelta);

                    std::cout << (primera ? "" : ",")
                              << "{\"de\":" << i << ",\"a\":" << j
                              << ",\"antes\":[" << lista (antes) << "]"
                              << ",\"vuelta\":[" << lista (vuelta) << "]"
                              << ",\"bucle\":[" << juce::String (bucleAntes, 4) << ","
                                                 << juce::String (bucleMedio, 4) << ","
                                                 << juce::String (bucle(), 4) << "]"
                              << ",\"negada\":" << (negada ? "true" : "false")
                              << ",\"compas\":" << engine.pasosPorCompas()
                              << ",\"pasos\":" << engine.getPatternLength (0) << "}";
                    primera = false;
                }

            std::cout << "]}" << std::endl;
        }
    }

    //  ========================================================================
    //  MEDIO PATRON SUENA, que es la frase entera de esta tanda y la unica que
    //  no se puede ver desde fuera.
    //
    //  El modelo de celdas no podia guardar «este bloque empieza por el paso 8
    //  del patron»: una celda por compas no tiene donde escribirlo. La lista de
    //  bloques si, en `offset`, y hasta aqui lo unico medido era el `offset`
    //  DECLARADO en la lista -o sea lo que la cara dice que guardo-. Que el
    //  motor lo USE al disparar no lo miraba nadie, y es justo la mitad que se
    //  rompe sola: quitar el `+ b.offset` de AudioEngine.cpp deja las reglas de
    //  `arr.py` en verde y la app sonando por el paso 0.
    //
    //  `getUltimoDisparo` existe para esto -su comentario ya decia «lo lee la
    //  auditoria» y no lo leia nadie, que es la misma figura de la regla que el
    //  codigo no cumple-. Devuelve `(bank << 16) | pasoDelPatron` por carril, y
    //  se muestrea UNA VEZ POR BLOQUE DE AUDIO: a 48 kHz y 128 muestras un paso
    //  dura decenas de bloques, asi que se apuntan los CAMBIOS y no las
    //  lecturas, o la lista saldria con cada paso repetido cuarenta veces.
    {
        vaciaCancion();
        clips.clear();
        publicaClips();
        engine.setClick (false);
        engine.setSongLoop (0, 0);
        engine.setPasoUnidades (rejillaU (2));           // 1/16
        vistaRejilla = 2;
        gridSlider.setValue (2.0, juce::dontSendNotification);

        const int pc = juce::jmax (1, engine.pasosPorCompas());
        selectedPattern = 0;
        engine.clearPattern (0);
        engine.setPatternLength (0, 16);
        engine.setSongLength (2);
        //  Y LOS CUATRO CARRILES SIN SILENCIAR, que no lo hace `vaciaCancion`:
        //  las sondas de arriba silencian carriles para medir «silenciar
        //  silencia», y un carril que se quedo mudo hace que esta medida diga
        //  «no suena nada» con el motor perfecto.
        for (int ln = 0; ln < AudioEngine::kSongLanes; ++ln)
            engine.setSongLaneMute (ln, false);
        engine.setSongLoop (0, 0);

        //  MEDIO COMPAS, arrancando por la mitad del patron: paso 8, ocho
        //  pasos de largo, `offset 8`. Con el offset bien suena el 8..15; sin
        //  el, el 0..7 -y las dos listas tienen ocho numeros, o sea que contar
        //  disparos no distingue una de otra. Por eso la regla mira CUALES.
        {
            BloqueUI b;
            b.lane = 0; b.bank = 0; b.compas = 0; b.paso = 8;
            b.largo = 8; b.offset = 8; b.mudo = false;
            bloques.push_back (b);
        }
        publicaBloques();

        constexpr int    kRafaga = 128;
        constexpr double kRate   = 48000.0;
        juce::AudioBuffer<float> bloque (2, kRafaga);
        engine.prepareToPlay (kRate, kRafaga);
        enginePreparedRate  = kRate;
        enginePreparedBlock = kRafaga;

        engine.setSongMode (true);
        engine.setPlaying (false);
        bloque.clear(); engine.renderNextBlock (bloque, 0, kRafaga);
        engine.setPlaying (true);

        juce::Array<int> vistos;
        int ultimo = -2;
        const int tope = (int) (engine.muestrasPorCompas() * 1.5) / kRafaga + 2;
        for (int b = 0; b < tope; ++b)
        {
            bloque.clear();
            engine.renderNextBlock (bloque, 0, kRafaga);
            const int d = engine.getUltimoDisparo (0);
            if (d != ultimo)
            {
                ultimo = d;
                if (d >= 0) vistos.add (d);
            }
        }
        engine.setPlaying (false);
        engine.setSongMode (false);

        std::cout << "{\"arr\":\"medio patron suena\",\"pc\":" << pc
                  //  Los tres del aparato, que son lo primero que se mira
                  //  cuando la lista sale vacia: si el motor no adopto la
                  //  tabla, o la cancion mide cero, o el carril esta mudo, la
                  //  medida no dice nada del `offset` - dice que no se midio.
                  << ",\"bloques en el motor\":" << engine.numBloques()
                  << ",\"compases\":" << engine.getSongLength()
                  << ",\"largo del patron\":" << engine.getPatternLength (0)
                  << ",\"bancos\":[";
        for (int i = 0; i < vistos.size(); ++i)
            std::cout << (i ? "," : "") << (vistos[i] >> 16);
        std::cout << "],\"pasos\":[";
        for (int i = 0; i < vistos.size(); ++i)
            std::cout << (i ? "," : "") << (vistos[i] & 0xFFFF);
        std::cout << "]}" << std::endl;

        bloques.clear();
        publicaBloques();
        vaciaCancion();
    }

    //  ========================================================================
    //  EL LARGO DE UNA NOTA SOBREVIVE AL CAMBIO DE REJILLA.
    //
    //  Lo pidio quien manda con estas palabras: «los golpes que ocupan una
    //  rejilla en 1/8 ocupen lo que tengan que ocupar en las demas; en un midi
    //  melodico mas que en un ritmo». `remapeaPaso` ya escalaba el largo, pero
    //  `stepLen` era un uint8 con techo de 63 cuartos: una redonda escrita a
    //  1/8 son 32 cuartos, a 1/32 pide 128 y se quedaba en 63 -la nota duraba
    //  la mitad-. El techo subio a `kLenMax = 4 * kNumSteps` (768).
    //
    //  Y HASTA AQUI NADIE LO MEDIA, con el agravante de que el comentario de
    //  `AudioEngine.cpp` afirmaba «y `Tests/arr.py` lo mide: 32 -> 1/32 -> 1/8
    //  vuelve 32». No lo medía: esa regla no existia. Un comentario que promete
    //  una medida que no existe es peor que no tener ninguna, porque el
    //  siguiente que pase no la escribe.
    //
    //  Se mide la ida Y la vuelta, y se elige una pareja de division EXACTA
    //  -1/8 a 1/32 es por cuatro- a proposito: la `escala` de `remapeaPaso`
    //  trunca, asi que un largo que no sea multiplo del factor pierde un cuarto
    //  por el camino. Eso es otro asunto y no se finge medido aqui.
    //
    //  `recortados` NO es la cifra de esta regla y por eso no se pide: desde
    //  que el largo dejo de tener techo util, ese contador solo cuenta el
    //  empujon, y este golpe no lleva empujon. Pedirle cero seria una linea que
    //  imprime OK.
    {
        vaciaCancion();
        clips.clear();
        publicaClips();
        selectedPattern = 0;
        engine.clearPattern (0);

        //  UN COMPAS LIMPIO EN LOS OCHO PATRONES, y hay que ponerlo a mano.
        //
        //  La sonda de las siete rejillas deja los ocho patrones con el largo
        //  de UN compas de 1/64, o sea muchos pasos; poner aqui la rejilla a
        //  1/8 sin remapear los convierte en ocho compases, y entonces afinar
        //  a 1/32 pediria 256 pasos de los 192 que la maquina guarda: la app se
        //  NIEGA -que es lo correcto- y esta medida salia en verde con los tres
        //  numeros iguales, o sea sin medir nada. Es la misma cautela que ya
        //  lleva escrita la sonda de las 42 parejas: cada medida sale de un
        //  compas limpio.
        for (int b = 0; b < AudioEngine::kNumPatterns; ++b)
        {
            engine.clearPattern (b);
            for (int st = 0; st < AudioEngine::kNumSteps; ++st)
                for (int pd = 0; pd < kNumPads; ++pd)
                    pattern[b][(size_t) st][(size_t) pd] = false;
        }
        engine.setPasoUnidades (rejillaU (0));           // 1/8
        vistaRejilla = 0;
        gridSlider.setValue (0.0, juce::dontSendNotification);
        for (int b = 0; b < AudioEngine::kNumPatterns; ++b)
            engine.setPatternLength (b, engine.pasosPorCompas());
        reajustaMandoLargo();
        const int pasos18 = engine.getPatternLength (0);

        //  UNA REDONDA a 1/8: 32 cuartos de paso, o sea ocho pasos de 1/8, o
        //  sea cuatro negras. Es la nota que el techo de 63 partia por la
        //  mitad al afinar la rejilla.
        pattern[0][0][0] = true;
        engine.setStep    (0, 0, 0, true);
        engine.setStepLen (0, 0, 0, 32);
        const int en18 = engine.getStepLen (0, 0, 0);

        gridSlider.setValue (4.0, juce::sendNotificationSync);   // 1/32
        const int en132   = engine.getStepLen (0, 0, 0);
        const int paso132 = engine.getPatternLength (0);

        gridSlider.setValue (0.0, juce::sendNotificationSync);   // y vuelta
        const int vuelta = engine.getStepLen (0, 0, 0);

        std::cout << "{\"arr\":\"rejilla largo\",\"en 1/8\":" << en18
                  << ",\"en 1/32\":" << en132
                  << ",\"vuelta\":" << vuelta
                  //  Los pasos del patron en cada parada son el CONTROL sin el
                  //  cual los tres largos iguales tambien los cumple una app
                  //  que se nego a cambiar de rejilla: 1/32 tiene que multiplicar
                  //  por cuatro los pasos igual que el largo.
                  << ",\"pasos en 1/8\":" << pasos18
                  << ",\"pasos en 1/32\":" << paso132
                  << ",\"tope\":" << AudioEngine::kLenMax << "}" << std::endl;

        engine.clearPattern (0);
        engine.setPasoUnidades (rejillaU (2));
        vistaRejilla = 2;
        gridSlider.setValue (2.0, juce::dontSendNotification);
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
        cargaInstrumento (0); esperaInstrumentos();
        cargaInstrumento (0); esperaInstrumentos();
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
    cargaInstrumento (2); esperaInstrumentos();
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
        //  El centinela de «sin canal» no es un canal: metido en un maximo
        //  diria que el reparto llega al 255. Ver AudioEngine::kSinCanal.
        if (AudioEngine::tieneCanal (engine.getPadCanal (i)))
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
        vaciaCancion();
        for (int b = 0; b < AudioEngine::kSongBars; ++b)
            ponBloqueCompas (0, b, 1);             // el patron 1 en los 64 compases
        publicaBloques();
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
//  Y no se escriben los 384 enteros: son 2.3 MB cada uno, o sea 880 MB de
//  temporales. Se escribe
//
//    - LA ZONA DE REFERENCIA de los 384 (raiz 0, capa fuerte): con eso se mide
//      que ninguno este mudo, que los 384 esten igualados y que no haya dos que
//      sean el mismo sonido, que son las cuatro que necesitan a TODOS.
//    - EL PRESET ENTERO del primero de cada familia: con eso se miden las
//      cosas que son ESTRUCTURA -la costura entre octavas, la del bucle y las
//      dos capas- y para eso no hacen falta los 384, hace falta uno por
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

    //  EL COSTE, EN PROPORCION Y NO EN MILISEGUNDOS ABSOLUTOS.
    //
    //  La doctrina de `Cpu.cpp`, citada: *el valor absoluto no dice si va a ir;
    //  la proporcion si viaja*. La referencia es un golpe de fabrica rendido en
    //  ESTA misma corrida y en ESTA misma maquina, que es lo unico que hace
    //  comparable un numero medido en un portatil con uno medido en el CI.
    {
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        auto ref0 = Kits::render (0);
        const double msRef = juce::Time::getMillisecondCounterHiRes() - t0;
        juce::ignoreUnused (ref0);
        std::cout << "{\"instr\":\"ref\",\"msKits\":" << juce::String (msRef, 2) << "}" << std::endl;
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

            //  LA ZONA DE REFERENCIA SALE CON SUS DOS CANALES, que es lo unico
            //  que permite medir el ancho: con la mezcla ya hecha, `r` valdria
            //  1.000 siempre y la regla del ancho mediria su propia suma.
            const auto& Z = sb->zonas[(size_t) Sintes::kZonaRef];
            const int nCh = sb->buffer.getNumChannels();
            juce::AudioBuffer<float> ref (nCh, Z.fin - Z.ini);
            for (int ch = 0; ch < nCh; ++ch)
                ref.copyFrom (ch, 0, sb->buffer, ch, Z.ini, Z.fin - Z.ini);
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
                      << ",\"canales\":" << nCh
                      << ",\"forma\":" << (int) F.forma
                      << ",\"ms\":" << juce::String (ms, 1)
                      << ",\"mapa\":[";
            for (int z = 0; z < sb->nZonas; ++z)
            {
                const auto& q = sb->zonas[(size_t) z];
                //  Y LA VELOCIDAD DEL LFO YA CUADRADA, que no se puede deducir
                //  del audio sin volver a estimarla -y estimarla es otra regla,
                //  con su propio error-. Ver `Sintes::lfoDeZona`.
                double lfoHz = -1.0, bucleSeg = 0.0;
                Sintes::lfoDeZona (f, pr, F.p[pr], (q.raiz + 24) / 12, 1.00, &lfoHz, &bucleSeg);
                std::cout << (z ? "," : "") << "[" << q.raiz << "," << q.capa << ","
                          << q.ini << "," << q.fin << "," << q.bucleIni << "," << q.bucleFin
                          << "," << juce::String (lfoHz, 6)
                          << "," << juce::String (bucleSeg, 6) << "]";
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
        ponInstrumentoYEspera (pad, 3, 5);
        const auto arbol = captureState();

        uiSample[(size_t) pad] = nullptr;
        padHasSample[(size_t) pad] = false;
        engine.clearPad (pad);

        std::array<int, kNumPads> mapa {};
        readInstMap (arbol, mapa);
        const int receta = mapa[(size_t) pad];
        if (receta >= 0)
            ponInstrumentoYEspera (pad, receta / Sintes::kPresets, receta % Sintes::kPresets);

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

    //  Y MOVER UN MANDO NO PARA LA INTERFAZ. Soltar un mando de la ficha
    //  re-sintetizaba en el hilo de mensajes: 473 ms de mediana, 1610 el peor
    //  (Tribunal 2026-09, 4.2). Se mide lo que tarda la llamada y que el pad
    //  acabe con un buffer NUEVO de la misma familia y con su recorte.
    {
        const int pad = kBancoInstr * kPadsPerBank + 3;
        padReceta[(size_t) pad].brillo *= 0.5f;
        padStart01[(size_t) pad] = 0.25f;
        auto* antes = uiSample[(size_t) pad].get();
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        resintetizaInstrumento (pad);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        esperaInstrumentos();
        auto* sb = uiSample[(size_t) pad].get();
        std::cout << "{\"instr\":\"resintesis\",\"ms\":" << juce::roundToInt (ms)
                  << ",\"nuevo\":" << ((sb != nullptr && sb != antes) ? 1 : 0)
                  << ",\"fam\":" << (sb ? sb->familia : -1)
                  << ",\"inicio\":" << padStart01[(size_t) pad] << "}" << std::endl;
    }

    // ------------------------------------------------------------------
    //  Y CARGAR UN INSTRUMENTO TAMPOCO PARA LA INTERFAZ.
    //
    //  Llego del telefono: con un ritmo ya montado, cargar el GRAND tardo
    //  SIETE SEGUNDOS y saco el cuadro de «no responde» de Android. No era el
    //  render -eso corre en `sintesPool` desde antes de la Tanda 27- sino lo
    //  que el gesto hace DESPUES de pedirlo, todo en el hilo de mensajes.
    //
    //  Y ninguna regla lo medía: `atasco.py` tiene su tabla de gesto -> ms para
    //  los chasquidos, GRABAR, PARAR, MEDIR y el chip de AUDIO, y la de aqui
    //  arriba mide la re-sintesis, pero CARGAR no lo miraba nadie. Un gesto sin
    //  regla es un gesto que puede tardar siete segundos sin que el banco se
    //  entere, que es exactamente lo que paso.
    //
    //  CON TRES CIFRAS Y NO UNA. «ms» solo dice que tarda; `refrescos` y
    //  `tintes` dicen POR QUE, y sin ellas cualquier cosa que baje los
    //  milisegundos por casualidad pasaria por arreglo. Y con la app LLENA, que
    //  es la condicion en la que la persona lo sufrio: medir la rejilla vacia
    //  seria medir otra app.
    //
    //  Se mide solo la parte SINCRONA -no se llama a `esperaInstrumentos`
    //  dentro del cronometro-, porque lo que congela la interfaz es esa y no el
    //  render.
    {
        llenaDePrueba();
        instCatalogo = Instrumentos::lee();
        instPack = 0;                                   // SINTES va el primero
        //  GRAND es la familia 0, y la celda es la del MENU y no la familia:
        //  ver la medida del destino, que ya se equivoco una vez por esto.
        int celda = 0;
        for (int i = 0; i < (int) instCatalogo[0].instr.size(); ++i)
            if (instCatalogo[0].instr[(size_t) i].familiaSintes == 0) { celda = i; break; }

        int llenos = 0;
        for (int i = 0; i < kNumPads; ++i) if (padHasSample[(size_t) i]) ++llenos;

        //  Lo que cuesta la foto del deshacer, aparte: si es ella la que pesa,
        //  el arreglo es otro (Tribunal 2026-09, 6.2) y conviene saberlo antes
        //  de tocar la rejilla.
        undoStack.clear();
        const double u0 = juce::Time::getMillisecondCounterHiRes();
        pushUndo ("MEDIDA");
        const int undoMs = juce::roundToInt (juce::Time::getMillisecondCounterHiRes() - u0);
        undoStack.clear();

        //  1. EL NAVEGADOR. El destino en otro banco a proposito: es el caso
        //     caro, el que ademas cambia de banco.
        instDestPad = 2 * kPadsPerBank + 5;
        currentBank = 0;
        bancoRefrescos = 0; bancoTintes = 0;
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        cargaInstrumento (celda);
        const int msNav = juce::roundToInt (juce::Time::getMillisecondCounterHiRes() - t0);
        const int refNav = bancoRefrescos, tinNav = bancoTintes;
        esperaInstrumentos();

        std::cout << "{\"instr\":\"cargar\",\"gesto\":\"navegador\",\"ms\":" << msNav
                  << ",\"refrescos\":" << refNav << ",\"tintes\":" << tinNav
                  << ",\"undo_ms\":" << undoMs << ",\"llenos\":" << llenos << "}" << std::endl;

        //  2. LA FICHA. El otro camino que la persona nombro: mantener un pad
        //     abre su ficha y desde alli se elige el sonido. Comparte
        //     `pushUndo` y el render, pero no el bloque de refrescos, asi que
        //     medirlos por separado es lo que dice cual de las dos mitades pesa.
        vstPad = instDestPad;
        bancoRefrescos = 0; bancoTintes = 0;
        const double t1 = juce::Time::getMillisecondCounterHiRes();
        eligePreset (4);
        const int msFicha = juce::roundToInt (juce::Time::getMillisecondCounterHiRes() - t1);
        const int refFicha = bancoRefrescos, tinFicha = bancoTintes;
        esperaInstrumentos();

        std::cout << "{\"instr\":\"cargar\",\"gesto\":\"ficha\",\"ms\":" << msFicha
                  << ",\"refrescos\":" << refFicha << ",\"tintes\":" << tinFicha
                  << ",\"undo_ms\":" << undoMs << ",\"llenos\":" << llenos << "}" << std::endl;
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

        ponInstrumentoYEspera (pad, fam, pre);
        auto tabla = uiSample[(size_t) pad];

        //  Al extremo MAS LEJANO del valor de hoy y no a un tope escrito: si
        //  el preset ya estuviera en ese tope, «moverlo» no moveria nada y la
        //  prueba saldria verde sin haber medido.
        Sintes::Preset r = Sintes::tabla()[fam].p[pre];
        const auto rg = Sintes::rango (fam, 0);
        const float v0 = Sintes::valor (r, 0);
        Sintes::ponValor (r, 0, std::abs (v0 - rg.lo) > std::abs (v0 - rg.hi) ? rg.lo : rg.hi);
        ponInstrumentoYEspera (pad, fam, pre, &r, true);
        auto movido = uiSample[(size_t) pad];
        const int suena = difieren (tabla.get(), movido.get());

        //  Y VOLVER devuelve la fila, bit a bit.
        ponInstrumentoYEspera (pad, fam, pre);
        const int vuelve = difieren (tabla.get(), uiSample[(size_t) pad].get());

        //  Y AHORA LA IDA Y VUELTA POR EL FICHERO, con el mismo arbol que lo
        //  escribe y POR EL CAMINO DE VERDAD -el trabajo troceado, que es
        //  quien rinde los pads al abrir un proyecto-: `applyState` corre en
        //  su `onDone`, o sea DESPUES, asi que leer la receta alli habria
        //  devuelto el instrumento sonando con la fila de la tabla.
        //
        //  Y BORRANDO EL PAD Y SU RECETA A MANO entre medias: si al volver
        //  siguen puestos no es que se hayan guardado, es que nadie los quito.
        ponInstrumentoYEspera (pad, fam, pre, &r, true);
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
        instDestPad = pedido;
        //  LA CELDA Y NO LA FAMILIA. `cargaInstrumento` recibe el sitio en el
        //  MENU, y desde que el menu se ordena por tipos la celda 11 ya no es
        //  la familia 11: CUERDA PULS esta en ARCO Y PUA, tercera de su grupo.
        //  Escrito como `cargaInstrumento (familia)` la medida cargaba ARPAS y
        //  despues buscaba CUERDA PULS, que no estaba en ningun pad: «se pidio
        //  el pad 37 y el instrumento fue al -1». Ver `Sintes::ordenDeMenu`.
        int celda = 0;
        for (int i = 0; i < (int) instCatalogo[0].instr.size(); ++i)
            if (instCatalogo[0].instr[(size_t) i].familiaSintes == familia) { celda = i; break; }
        cargaInstrumento (celda); esperaInstrumentos();

        int fue = -1;
        for (int i = 0; i < kNumPads; ++i)
            if (padEsInstrumento (i) && uiSample[(size_t) i]->familia == familia) { fue = i; break; }

        std::cout << "{\"instr\":\"destino\",\"pedido\":" << pedido
                  << ",\"fue\":" << fue << ",\"clavado\":" << clavado << "}" << std::endl;
    }


    // ------------------------------------------------------------------
    //  Y QUE LA FICHA ABRA EN EL PAD DEL QUE VIENES, ESTE LIBRE O NO.
    //
    //  Llego del telefono: «seleccionas el pad seis y le das a CARGAR; se abre
    //  la pestaña, le das INSTRUMENTO y se te abre automaticamente en el
    //  cuarenta y nueve». Y ninguna de las quince reglas de `expo.py` puede
    //  verlo: una rejilla que marca la celda equivocada se maqueta perfecta -no
    //  solapa, no se sale, no corta un rotulo, no mide cero y esta traducida-.
    //  Es la familia de los cinco fallos del compas del piano.
    //
    //  POR EL GESTO ENTERO -se arma LOAD por su tapa, se toca el pad con un
    //  `MouseEvent` construido y se pulsa INSTRUMENTOS-, porque el pad moria
    //  justo en el eslabon del medio: `browseFactoryButton.onClick` es una
    //  lambda SIN argumentos. Llamar a `openInstSheet()` por dentro es
    //  exactamente el sitio donde el fallo no existe.
    //
    //  Y el destino se deja ANTES en el 48, que es el valor de ayer: sin eso,
    //  un resto de la comprobacion anterior podria dar la respuesta correcta
    //  sin que nadie la hubiera sembrado.
    //
    //  Con CUATRO cifras, que cada una sola se engaña: que `vengoDe` NO sea el
    //  48 -pedir «que abra en el 49» saldria verde con el fallo puesto-, que
    //  ese pad este LLENO -que es la mitad que la queja dice con sus palabras,
    //  este libre o no, y la que una siembra con `firstEmptyPad` incumple-, la
    //  invariante del BANCO derivado -lo que `instg` rompia- y que la ficha
    //  quede ABIERTA, o las otras tres las cumple igual un camino que no llego
    //  a abrir nada.
    {
        closeAllSheets();
        const int vengoDe = 2 * kPadsPerBank + 5;      // C06, y con sonido de fabrica
        selectPad (0);
        instDestPad = kBancoInstr * kPadsPerBank;      // el 48 de ayer

        pulsaTapa (&loadButton);

        auto* p = pads[vengoDe];
        const auto punto = juce::Point<float> ((float) (p->getWidth()  / 2),
                                               (float) (p->getHeight() / 2));
        const auto ahora = juce::Time::getCurrentTime();
        juce::MouseEvent ev (juce::Desktop::getInstance().getMainMouseSource(),
                             punto, juce::ModifierKeys(), 1.0f,
                             0.0f, 0.0f, 0.0f, 0.0f,
                             p, p, ahora, punto, ahora, 1, false);
        p->mouseDown (ev);
        p->mouseUp (ev);

        pulsaTapa (&browseFactoryButton);

        const int abierta = instSheet.isVisible() ? 1 : 0;
        const int lleno   = padHasSample[(size_t) vengoDe] ? 1 : 0;
        std::cout << "{\"instr\":\"abre\",\"vengoDe\":" << vengoDe
                  << ",\"destino\":" << instDestPad
                  << ",\"banco\":" << bancoDestino()
                  << ",\"lleno\":" << lleno
                  << ",\"abierta\":" << abierta << "}" << std::endl;
        closeAllSheets();
    }

    // ------------------------------------------------------------------
    //  Y QUE OIR UNA TECLA SUENE EL PAD COMO ESTA AFINADO, y no una octava de
    //  mas. `abreVst` ponia la base del teclado en la octava de `padPitch` con
    //  este argumento al lado: «abrir siempre en el cero dejaria un bajo
    //  afinado dos octavas abajo sonando en un sitio que no es el suyo». Leido
    //  el camino entero, la frase esta del reves:
    //
    //      Teclado::notaEn   ->  base + blancas[i]
    //      vstTeclado.onNota ->  postNoteOnAt (vstPad, semis, ...)
    //      AudioEngine       ->  semis = padPitch[slot] + extraSemis
    //
    //  `postNoteOnAt` es RELATIVO al pad POR DISEÑO MEDIDO -es la puerta que
    //  existe para que oir una tecla no afine el pad- asi que la base se SUMA
    //  al pitch que el motor ya aplica: con el pad a +12 la tecla C sonaba +24.
    //
    //  Y SE MIDE POR IDENTIDAD Y BIT A BIT, que es lo unico que no obliga a
    //  inventarse un accesor al semitono que una voz acabo usando: la tecla
    //  cero tiene que dar EXACTAMENTE el mismo audio que `postNoteOnAt (pad, 0)`
    //  -«el pad como esta afinado»- y, subida una octava, el de
    //  `postNoteOnAt (pad, 12)`. «Casi lo mismo» es justo lo que dejaria pasar
    //  un doble conteo suave.
    //
    //  Con el pad a +12 y no en cero, que ahi las dos formas coinciden y la
    //  prueba diria que si sin haber medido. Y con una cifra de CONTROL -que
    //  las dos referencias se separen entre si-, o las dos comparaciones
    //  saldrian verdes comparando silencio con silencio.
    {
        const int pad = kBancoInstr * kPadsPerBank + 11;
        ponInstrumentoYEspera (pad, 4, 0);
        engine.setPadPitch (pad, 12);

        closeAllSheets();
        selectPad (pad);
        abreVst();
        const int base0 = vstTeclado.getBase();

        engine.prepareToPlay (48000.0, 128);
        juce::AudioBuffer<float> b (2, 128);
        constexpr int kBloques = 48;

        auto rinde = [&] (std::function<void()> disparo)
        {
            engine.postPanic();
            for (int i = 0; i < 8; ++i) { b.clear(); engine.renderNextBlock (b, 0, 128); }
            disparo();
            juce::AudioBuffer<float> out (2, kBloques * 128);
            for (int i = 0; i < kBloques; ++i)
            {
                b.clear();
                engine.renderNextBlock (b, 0, 128);
                out.copyFrom (0, i * 128, b, 0, 0, 128);
                out.copyFrom (1, i * 128, b, 1, 0, 128);
            }
            return out;
        };
        auto difieren = [] (const juce::AudioBuffer<float>& x, const juce::AudioBuffer<float>& y)
        {
            int n = 0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < x.getNumSamples(); ++i)
                    if (x.getReadPointer (ch)[i] != y.getReadPointer (ch)[i]) ++n;
            return n;
        };

        //  La tecla cero es la primera blanca: por debajo de la banda de las
        //  negras, que se preguntan antes porque estan encima.
        auto tocaDo = [this]
        {
            const float ancho = (float) juce::jmax (8, vstTeclado.getWidth() - 2) / 8.0f;
            const auto q = juce::Point<float> (1.0f + ancho * 0.5f,
                                               (float) vstTeclado.getHeight() * 0.85f);
            const auto t = juce::Time::getCurrentTime();
            juce::MouseEvent me (juce::Desktop::getInstance().getMainMouseSource(),
                                 q, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &vstTeclado, &vstTeclado, t, q, t, 1, false);
            vstTeclado.mouseDown (me);
        };
        auto suelta = [this]
        {
            const auto q = juce::Point<float> (0.0f, 0.0f);
            const auto t = juce::Time::getCurrentTime();
            juce::MouseEvent me (juce::Desktop::getInstance().getMainMouseSource(),
                                 q, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &vstTeclado, &vstTeclado, t, q, t, 1, false);
            vstTeclado.mouseUp (me);
        };

        const auto refA = rinde ([&] { engine.postNoteOnAt (pad, 0, 0.9f, AudioEngine::kSostenida); });
        engine.postNoteOff (pad);
        const auto tecA = rinde ([&] { tocaDo(); });
        suelta();

        if (vstOctUp.onClick) vstOctUp.onClick();
        const int base1 = vstTeclado.getBase();

        const auto refB = rinde ([&] { engine.postNoteOnAt (pad, 12, 0.9f, AudioEngine::kSostenida); });
        engine.postNoteOff (pad);
        const auto tecB = rinde ([&] { tocaDo(); });
        suelta();
        engine.postPanic();
        closeAllSheets();

        std::cout << "{\"instr\":\"teclado\",\"base\":" << base0
                  << ",\"baseArriba\":" << base1
                  << ",\"difA\":" << difieren (refA, tecA)
                  << ",\"difB\":" << difieren (refB, tecB)
                  << ",\"control\":" << difieren (refA, refB) << "}" << std::endl;
    }

    // ------------------------------------------------------------------
    //  Y QUE LA PUERTA AL PIANO SEA EL PIANO Y NO UN CLON.
    //
    //  Se pidio con esa duda: «igual mejor clonar esa pestaña... no, no,
    //  porque sera el piano rol, que este conectado, sincronizado con el otro,
    //  en los pads que selecciones». Medido, no hay nada que deshacer: hay UNA
    //  sola `pianoGrid` y `abrePianoDelPad` es una PUERTA. Lo que no habia era
    //  una cifra que lo dijera - hoy se puede romper para que abra el piano de
    //  otro pad y las 1428 corridas y las treinta y tantas pruebas siguen en
    //  verde.
    //
    //  Se mide POR EL GESTO -`pianoGrid.gesto` en pixeles- y no llamando a
    //  `pianoCellToggled`, que es justo donde vivian los cinco fallos del
    //  compas, y con un TESTIGO en el pad 0 en la MISMA columna: sin el,
    //  «escribio» no separa de «escribio en el pad de por defecto», que es
    //  exactamente el fallo que se busca. Las dos cifras se leen juntas - la
    //  nota en el pad del instrumento Y el testigo intacto.
    {
        const int pad = kBancoInstr * kPadsPerBank + 9;
        ponInstrumentoYEspera (pad, 2, 0);
        closeAllSheets();
        selectPad (pad);
        abreVst();
        const int quien = vstPad;

        const int b = selectedPattern, col = 4;
        engine.setPatternLength (b, 16);
        engine.clearPattern (b);
        for (int st = 0; st < kNumSteps; ++st)
            for (int q = 0; q < kNumPads; ++q)
                pattern[(size_t) b][(size_t) st][(size_t) q] = false;
        seqPrimerCelda = 0;
        pattern[(size_t) b][(size_t) col][0] = true;
        engine.setStep (b, col, 0, true);
        engine.setStepNote (b, col, 0, 9);

        if (vstPianoBtn.onClick) vstPianoBtn.onClick();
        resized();
        refreshPiano();
        const int pianoPad = selectedPad;

        const int filas = juce::jmax (1, pianoGrid.getFilas());
        const float altoFila = (float) pianoGrid.getHeight() / (float) filas;
        const float anchoCol = (float) (pianoGrid.getWidth() - PianoRoll::kGutter)
                                 / (float) StepGrid::kBarSteps;
        pianoGrid.gesto ((float) PianoRoll::kGutter + ((float) col + 0.5f) * anchoCol,
                         ((float) (filas / 2) + 0.5f) * altoFila, false);
        pianoGrid.suelta();

        const int puesto  = pattern[(size_t) b][(size_t) col][(size_t) quien] ? 1 : 0;
        const int nota    = engine.getStepNote (b, col, quien);
        const int testigo = engine.getStepNote (b, col, 0);
        closeAllSheets();

        std::cout << "{\"instr\":\"piano\",\"vstPad\":" << quien
                  << ",\"pianoPad\":" << pianoPad
                  << ",\"puesto\":" << puesto
                  << ",\"nota\":" << nota
                  << ",\"testigo\":" << testigo << "}" << std::endl;
    }

    // ------------------------------------------------------------------
    //  Y LO QUE CUESTA LLENAR EL BANCO D, que es la cifra que decide si los
    //  dieciseis pueden venir puestos de fabrica o hay que ir a buscarlos.
    //
    //  DIECISEIS Y NO VEINTICUATRO, y ese numero sale del BANCO y no de la
    //  tabla: un banco tiene `kPadsPerBank` casillas, asi que desde que hay
    //  veinticuatro familias el banco D ya no las puede traer todas. Recorrer
    //  `kFamilias` media el coste de llenar un banco y medio, que no es una
    //  cifra de nada. Se llenan las dieciseis PRIMERAS DEL MENU, que son las
    //  que vendrian puestas.
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < kPadsPerBank; ++i) Sintes::sintetiza (Sintes::ordenDeMenu()[i], 0);
    //  Y QUIENES SON, que es la otra mitad: la prueba suma los megas de los
    //  que caben en el banco y sin esta lista tendria que adivinar cuales son
    //  -sumaba los de las veinticuatro familias y lo llamaba «los 16»-.
    juce::String cuales;
    for (int i = 0; i < kPadsPerBank; ++i)
        cuales << (i > 0 ? "," : "") << Sintes::ordenDeMenu()[i];
    std::cout << "{\"instr\":\"bancoD\",\"ms\":"
              << juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 1)
              << ",\"cuales\":\"" << cuales << "\"}" << std::endl;
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
        //  Y CUANTOS PADS NO ESTAN EN NINGUNA TIRA, que en un proyecto nuevo
        //  tienen que ser los SESENTA Y CUATRO. Es la cifra de la tanda: antes
        //  nacian todos en el canal 0 -o sea la mesa entera en una tira- y
        //  `canalMax 0` no distinguia eso de lo que hay ahora. Ver
        //  AudioEngine::kSinCanal.
        int canalMax = 0, muteN = 0, sinCanalN = 0;
        double ganSuma = 0.0;
        for (int i = 0; i < kNumPads; ++i)
        {
            const int c = engine.getPadCanal (i);
            if (AudioEngine::tieneCanal (c)) canalMax = juce::jmax (canalMax, c);
            else                             ++sinCanalN;
        }
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
                  << ",\"sincanal\":" << sinCanalN
                  << ",\"ranuras\":[" << ranuras << "]"
                  << ",\"largo\":" << engine.getSongLength() << ",\"carriles\":[";
        for (int ln = 0; ln < AudioEngine::kSongLanes; ++ln)
        {
            std::cout << (ln ? "," : "") << "[";
            for (int b = 0; b < engine.getSongLength(); ++b)
                std::cout << (b ? "," : "") << celdaCancion (ln, b);
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
    //  Y EL ARBOL ENTERO, QUE ES DONDE ESTAN. Este bucle recorria los hijos
    //  DIRECTOS de la cara, y con eso cubria nueve de las diez tapas de
    //  mantener que tiene la app: `autoBtn` vive dentro de `songSheet` -es
    //  hija de una ficha, no de la cara- asi que era invisible para la regla
    //  que existe justo para que ningun gesto se quede sin fila. La regla
    //  decia «se buscan, no se enumeran» y buscaba en un solo piso.
    UiAudit::mantener.clear();
    std::function<void (juce::Component&)> recorre = [&] (juce::Component& c)
    {
        for (auto* hijo : c.getChildren())
        {
            //  Y LOS CANALONES DEL RACK SON FAMILIA, igual que las seis
            //  ranuras de la cara: su fila es «MANTEN UNA RANURA DEL RACK» y
            //  no las nombra una por una. Sin esto la regla pediria una fila
            //  para «DLY», otra para «REV» y otra para «+», que es el nombre
            //  de lo que hay DENTRO de la ranura y no de la tapa — seis filas
            //  que ademas cambiarian al cambiar de efecto.
            //  Y LA TAPA DEL PRESET DE CADA FILA, por lo mismo y con MAS
            //  razon: su rotulo es el nombre del preset puesto -«CAMPANA»,
            //  «MI ECO», «MOVIDO»- o sea que la fila que la regla pediria
            //  cambiaria al girar un mando. Su mantener hace EXACTAMENTE lo
            //  que la fila que ya existe promete -abrir los presets de ese
            //  efecto-, asi que es la misma familia y no una fila nueva.
            if (auto* h = dynamic_cast<HoldButton*> (hijo))
                UiAudit::gestoDe (h->getButtonText(),
                                  (fxButtons.contains (h) || rackSlotBtns.contains (h)
                                     || rackPresetBtns.contains (h)) ? 1 : 0);
            recorre (*hijo);
        }
    };
    recorre (*this);

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
    //  LA REJILLA CON LA BANDA PUESTA, que es el estado en el que existe la
    //  TIRA DE ACCIONES -COPIAR, CORTE, PEGAR, BORRAR- encima de ella.
    //
    //  Es la leccion de `secp`, `eqb`, `instp`, `rackf`, `ranural` y
    //  `pianosel`, la septima vez: una tira que solo aparece con algo puesto se
    //  mide SIEMPRE vacia si el banco solo sabe abrir la pagina recien abierta.
    //  Y aqui son cuatro tapas mas y una fila mas de alto que sale ENTERA de la
    //  rejilla -44 px, `hit` mas `sm`-, o sea la clase de cosa que baja la
    //  celda de paso por debajo de su suelo sin que nadie se entere.
    //
    //  CON PORTAPAPELES TAMBIEN, que es la quinta tapa: PEGAR solo existe si se
    //  ha copiado algo, asi que sin copiar antes esta entrada mediria una tira
    //  de cuatro y la de cinco no la veria nunca. Se copia y se vuelve a
    //  marcar, que es ademas el camino que hace una persona.
    else if (which == "secsel")
    {
        showSeqPage (seqPageGrid);
        openSheet (seqSheet, secButton);
        //  Con pasos ESCRITOS dentro de la banda: una banda sobre dieciseis
        //  celdas vacias copia cero y PEGAR no llegaria a existir.
        for (int st = 0; st < 16; st += 3)
        {
            pattern[0][(size_t) st][(size_t) (st % 4)] = true;
            engine.setStep (0, st, st % 4, true);
        }
        seqSelBtn.setToggleState (true, juce::dontSendNotification);
        stepGrid.selArmada = true;
        seqBanda (0, 0, 3, 8);
        seqCopiaSel();            // ...y asi PEGAR tambien existe
        seqBanda (0, 0, 3, 8);
        refreshStepGrid();
    }
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
    //  LA PLAYLIST CON LA BANDA PUESTA, que es la OCTAVA vez de la misma
    //  leccion: `secp`, `eqb`, `instp`, `rackf`, `ranural`, `pianosel` y
    //  `secsel` estan aqui por lo mismo. Una tira que solo existe con algo
    //  seleccionado se mide SIEMPRE vacia si el banco solo sabe abrir la
    //  pagina recien abierta, y la de la cancion son CUATRO TAPAS y una fila
    //  entera de alto que sale de la rejilla de carriles - la clase de cosa
    //  que baja una celda por debajo de su suelo sin que nadie se entere.
    //
    //  Con bloques, con un CLIP y con portapapeles: el clip porque la banda lo
    //  recorta por sus filos y sin uno dentro esa mitad no se ve; y se copia
    //  antes de volver a marcar para que PEGAR -la quinta tapa, que solo
    //  existe con portapapeles- llegue a existir. Es ademas el camino que hace
    //  una persona: marcar, copiar, volver a marcar.
    else if (which == "songsel")
    {
        openSheet (songSheet, songButton);
        selectedPad = 0;
        const int pc = juce::jmax (1, engine.pasosPorCompas());
        ponBloqueCompas (0, 0, 1, 2);
        ponBloqueCompas (1, 1, 2);
        ponClip (2, 0);
        ponHerramienta (Playlist::hSel);
        songBanda (0, 0, 2, pc + pc / 2);
        songCopiaSel();                   // ...y asi PEGAR tambien existe
        songBanda (0, 0, 2, pc + pc / 2);
        refreshSong();
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
    //  EL PIANO CON LA SELECCION PUESTA, que es el estado en el que existe la
    //  TIRA DE ACCIONES —COPIAR, CORTE, PEGAR, BORRAR— y sin esta entrada no lo
    //  mide nadie.
    //
    //  Es la leccion de `secp`, `eqb`, `instp`, `rackf` y `ranural`, la sexta
    //  vez: una tira que solo aparece con algo puesto se mide SIEMPRE vacia si
    //  el banco solo sabe abrir la pagina recien abierta. Y aqui son cuatro
    //  tapas mas y una fila mas de alto, o sea la clase de cosa que rompe un
    //  reparto sin que nadie se entere.
    //
    //  CON PORTAPAPELES TAMBIEN, que es la quinta tapa: PEGAR solo existe si se
    //  ha copiado algo, asi que sin copiar antes esta entrada mediria una tira
    //  de cuatro y la de cinco no la veria nunca — el mismo agujero una capa
    //  mas abajo. Se copia y se vuelve a seleccionar, que es ademas el camino
    //  que hace una persona.
    else if (which == "pianosel")
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
        openSheet (seqSheet, secButton); showSeqPage (seqPagePiano);
        pianoGrid.setHerramienta (PianoRoll::sel);
        pianoSelBtn.setToggleState (true, juce::dontSendNotification);
        //  La banda cubre las cinco: se mide la tira, no cuantas caen dentro.
        pianoBanda (0, -12, 15, 12);
        pianoCopiaSel();          // ...y asi PEGAR tambien existe
        pianoBanda (0, -12, 15, 12);
        refreshPiano();
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
    //  Y LA FICHA MIDI DEL PIANO ROLL, que es otra: «midi» de arriba es la
    //  pagina de los PUERTOS. Sin nombre propio seria *una ficha que nadie
    //  mide*, que es como esta casa ha pagado el residuo, las tapas de 0x0
    //  y los rotulos cortados.
    else if (which == "midf") { abreMidiSheet(); }
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
        //  Y CON UN PRESET PUESTO EN CADA UNA, EL DE NOMBRE MAS LARGO.
        //
        //  Sin esto las seis tapas de preset decian «MOVIDO» —`needW` 35.2
        //  contra `haveW` 36.0 en 280x653, o sea cabe por seis decimas— y la
        //  regla de rotulo cortado de `expo.py` estaba mirando el unico estado
        //  del rack que no se rompe: con «DEFECTO» puesto son 41.1 contra 36.0
        //  y salen puntos suspensivos. *El banco no adivina lo que la app
        //  puede decir*, asi que se le da el caso peor.
        //
        //  Y el peor se BUSCA en la tabla, no se escribe aqui: hoy el nombre
        //  mas largo es «SUB CENTRO», diez letras, y el dia que entre uno de
        //  doce esta linea lo mide sola. Es lo mismo que hace la regla de
        //  curvas de `Tests/presets.py` con el numero de combinaciones.
        for (int s = 0; s < kNumRanuras; ++s)
            if (const int fx = enRanura (s); fx >= 0)
            {
                //  Y DE LOS ESCRITOS, que el cero es DEFECTO y lo llevan los
                //  treinta efectos igual: sembrando el mas largo a secas, DRV
                //  y DLY -cuyos nombres propios son todos mas cortos que esa
                //  palabra- acababan los dos diciendo «DEFECTO» con el mismo
                //  dibujo al lado, y `planos.py` lo canto como par de tapas
                //  gemelas. Tenia razon: en una pantalla de banco dos filas
                //  identicas no dicen cual es cual.
                int peor = 1;
                for (int k = 1; k < FxPresets::cuantos(); ++k)
                    if (juce::String (FxPresets::nombre (fx, k)).length()
                          > juce::String (FxPresets::nombre (fx, peor)).length())
                        peor = k;
                aplicaFxPreset (fx, peor);
            }

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
    //  LA FICHA DE PRESETS, con un tipo que TRAE los suyos y con el EQ, que es
    //  el unico cuyo preset lleva ademas cinco bandas. Dos entradas y no una:
    //  la excepcion declarada tiene que verse en la matriz de `expo.py` como se
    //  ve la ficha del instrumento con la receta movida.
    else if (which == "preset")  { ponEnRanura (0, 3);  abreMenuPresets (3); }
    else if (which == "preseteq"){ ponEnRanura (0, kFxEq); abreMenuPresets (kFxEq); }
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
        ponInstrumentoYEspera (kBancoInstr * kPadsPerBank + 6, 11, 0);   // CUERDA PULS
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
        //  los 384 son DIECISIETE. O sea que la regla del rotulo cortado se le
        //  hacia a la cadena mas corta posible - una linea que imprime OK.
        //
        //  La cuenta es la del pintor MENOS lo que es constante: "/16" y los
        //  tres espacios valen lo mismo en las 384, asi que lo que separa a una
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

        //  Y EL PAD SE DA LA VUELTA CON EL BANCO, que hasta las veinticuatro
        //  familias no hacia falta: `kBancoInstr * 16 + fam` con `fam` en
        //  0..15 caia dentro del banco D, y con `fam` llegando a 23 se sale de
        //  los 64 pads que hay. No habria fallado ruidosamente: `selectPad`
        //  acota, o sea que la medida se habria hecho sobre OTRO pad sin que
        //  nadie lo viera. Es la misma figura que el `t[16]` de `deFamilia`.
        const int pad = kBancoInstr * kPadsPerBank + (fam % kPadsPerBank);
        ponInstrumentoYEspera (pad, fam, pre);
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
    //  LA CARA CON LA SESION YA ESCRITA, que es el unico estado donde la banda
    //  de continuidad puede decir «A SALVO» — y sin esta entrada NO SE MEDIA.
    //
    //  El campo lo gobierna `SessionKeeper::ultimaEscrituraMs()`, que vale cero
    //  hasta que algo acaba en disco. Una corrida del banco dura menos que los
    //  dos segundos de `kSyncSesionMs`, asi que las 53 pantallas medidas lo
    //  daban por ausente y una regla que lo pidiera no habria podido verlo
    //  jamas: exactamente el liston que no puede fallar que `asomaUnPad` ya
    //  costo en `desglose.py`.
    //
    //  Se escribe DE VERDAD -autosave y flush- y no se falsea el reloj: lo que
    //  se quiere medir es que el campo sale cuando el fichero esta en disco, y
    //  poner el instante a mano mediria el `if` y no la escritura.
    else if (which == "salvo")
    {
        closeAllSheets();
        autosave();
        session.flush (2000);
        repaint();
    }
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

    //  EL DEDO CAE EN UNA DIVISION Y NO EN EL CENTRO DEL COMPAS.
    //
    //  Con la banda pegada al compas, «el centro del compas 3» y «el compas 3»
    //  eran lo mismo. En cuanto la rejilla se subdivide dejan de serlo: el
    //  centro del compas 3 es la division de la mitad, o sea que las cinco
    //  medidas de siempre habrian cambiado de cifra sin que nada estuviera
    //  roto. Se apunta al CENTRO DE LA PRIMERA DIVISION del compas, que es el
    //  mismo sitio de antes para una rejilla sin subdividir y el sitio que la
    //  persona quiere decir cuando dice «el compas 3».
    const int pasosCompas = juce::jmax (1, engine.pasosPorCompas());
    const int division    = juce::jmax (1, songGrid.divisionPaso());
    const float pasoW     = barW / (float) pasosCompas;

    auto puntoPaso = [&] (int pista, int pasoAbs)
    {
        return juce::Point<float> ((float) gutter
                                       + pasoW * ((float) pasoAbs + (float) division * 0.5f),
                                   pistaH * ((float) pista + 0.5f));
    };
    auto punto = [&] (int pista, int compas) { return puntoPaso (pista, compas * pasosCompas); };
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
    //  El largo SIGUE DICIENDOSE EN COMPASES aunque la vista lo guarde en
    //  pasos: es lo que la regla del banco juzga y lo que una persona cuenta.
    auto compasesDe = [&] (int i)
    {
        if (! juce::isPositiveAndBelow (i, (int) songClipsVista.size())) return 0;
        return (songClipsVista[(size_t) i].hastaPaso
                    - songClipsVista[(size_t) i].desdePaso) / pasosCompas;
    };
    //  Y LA CIFRA NUEVA: pista, compas Y PASO dentro del compas, que es lo que
    //  `fila` no podia decir porque no existia.
    auto filaPaso = [&] (int i)
    {
        if (! juce::isPositiveAndBelow (i, (int) clips.size())) return juce::String ("[]");
        return "[" + juce::String (clips[(size_t) i].pista) + ","
                   + juce::String (clips[(size_t) i].compas) + ","
                   + juce::String (clips[(size_t) i].paso) + "]";
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
    auto arrastraPaso = [&] (int p0, int s0, int p1, int s1)
    {
        auto d = evento (puntoPaso (p0, s0));  rej.mouseDown (d);
        auto m = evento (puntoPaso (p1, s1));  rej.mouseDrag (m);
        auto u = evento (puntoPaso (p1, s1));  rej.mouseUp (u);
    };
    auto arrastra = [&] (int p0, int c0, int p1, int c1)
    {
        arrastraPaso (p0, c0 * pasosCompas, p1, c1 * pasosCompas);
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

    //  Y EL SUB-COMPAS, que es lo que esta tanda añade: un toque en la SEGUNDA
    //  division del compas 6 tiene que dejar el clip ahi y no en el filo del
    //  compas. Con la banda pegada al compas esto devolvia paso 0 -medio
    //  compas de error a 120, o sea 1000 ms- y no habia forma de verlo: las
    //  ocho reglas de esta prueba solo miraban el compas.
    clips.clear(); publicaClips(); refreshSong (false);
    { auto e = evento (puntoPaso (3, 6 * pasosCompas + division)); rej.mouseDown (e); }
    const auto subPaso = filaPaso (0);

    //  Y LA ONDA DEL BLOQUE, que es lo otro que se pidio: «que las tomas que se
    //  graben se vea el audio facil». Se mide la firma de la envolvente que la
    //  rejilla RECIBE -columnas y area- y no que exista un array: un bloque
    //  liso tiene tambien su array, lleno de ceros.
    //
    //  DOS VENTANAS DE RECORTE DISTINTAS del mismo pad, y las dos firmas tienen
    //  que salir distintas. Con una sola, «dibuja la onda» lo cumple igual un
    //  codigo que dibuje siempre la del fichero entero - que es exactamente el
    //  fallo que «el clip toma el recorte del pad» arreglo en el motor y que
    //  aqui volveria a aparecer en el dibujo.
    auto ondaDe = [&] (int i)
    {
        if (! juce::isPositiveAndBelow (i, (int) songClipsVista.size()))
            return juce::String ("[0,0.000]");
        const auto& v = songClipsVista[(size_t) i];
        double suma = 0.0;
        for (int k = 0; k < v.columnas && v.onda != nullptr; ++k)
            suma += (double) (v.onda[k * 2 + 1] - v.onda[k * 2]);
        return "[" + juce::String (v.columnas) + "," + juce::String (suma, 3) + "]";
    };
    auto ponConRecorte = [&] (float a, float b)
    {
        padStart01[0] = a; padEnd01[0] = b;
        clips.clear(); publicaClips();
        selectedPad = 0;
        ponClip (1, 0);
        refreshSong (false);
        return ondaDe (0);
    };
    const auto ondaMitad = ponConRecorte (0.0f, 0.5f);
    const auto ondaEntera = ponConRecorte (0.0f, 1.0f);

    //  Y LAS TIJERAS. Un clip de cuatro compases en (1,0) partido por el
    //  compas 2: dos clips que SUMAN el original. Las tres cifras -cuantos
    //  quedan, donde empieza cada uno y que los largos sumen- porque cada una
    //  sola la cumple media maquina: partir y perder la cola pasa «quedan 2»,
    //  y duplicar el clip entero pasa «quedan 2» y «suman» no.
    padStart01[0] = 0.0f; padEnd01[0] = 1.0f;
    pon (1, 0, 4);
    const int largoAntes = clips.empty() ? 0 : clips[0].largo;
    ponHerramienta (Playlist::hTijeras);
    { auto e = evento (punto (1, 2)); rej.mouseDown (e); }
    const int trasTijeras = (int) clips.size();
    const auto tijeraA = filaPaso (0);
    const auto tijeraB = filaPaso (1);
    int sumaLargos = 0;
    for (const auto& c : clips) sumaLargos += c.largo;

    //  Y EL ATAJO A CORTAR: un doble toque en un clip abre la ficha del
    //  troceado CON EL PAD DEL CLIP puesto. Dos cifras, porque «se abrio algo»
    //  lo cumple igual una ficha abierta sobre el pad que ya estaba elegido.
    clips.clear(); publicaClips();
    selectedPad = 0;
    ponHerramienta (Playlist::hLapiz);
    {
        ClipUI c;
        c.pad = 5; c.pista = 1; c.compas = 1; c.paso = 0; c.desde = 0;
        c.largo = (int) engine.muestrasPorCompas(); c.gain = 1.0f;
        clips.push_back (c);
        publicaClips(); refreshSong (false);
    }
    { auto e = evento (punto (1, 1)); rej.mouseDoubleClick (e); }
    const int padTrasDoble  = selectedPad;
    const int chopTrasDoble = chopSheet.isVisible() ? 1 : 0;
    clips.clear(); publicaClips(); refreshSong (false);

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
    //
    //  Y SE SUELTA EN LA ULTIMA DIVISION DEL COMPAS 4 y no en la primera: el
    //  filo derecho se pega a la division que hay bajo el dedo MAS una, asi
    //  que apuntando al principio del 4 el clip acabaria en 4 compases y una
    //  division. Es la mejora entera de esta tanda -antes el filo solo podia
    //  caer en un multiplo de compas- y la prueba sigue diciendo CINCO porque
    //  se apunta al sitio que en compases enteros significa «hasta el final
    //  del compas 4».
    pon (1, 0, 3);
    arrastraPaso (1, 2 * pasosCompas, 1, 5 * pasosCompas - division);
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
              << ",\"sub_paso\":" << subPaso
              << ",\"onda_mitad\":" << ondaMitad
              << ",\"onda_entera\":" << ondaEntera
              << ",\"tras_tijeras\":" << trasTijeras
              << ",\"tijera_a\":" << tijeraA
              << ",\"tijera_b\":" << tijeraB
              << ",\"largo_antes\":" << largoAntes
              << ",\"suma_largos\":" << sumaLargos
              << ",\"pad_tras_doble\":" << padTrasDoble
              << ",\"chop_tras_doble\":" << chopTrasDoble
              << ",\"pasos_compas\":" << pasosCompas
              << ",\"division\":" << division
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

    //  OCHO NOTAS EN UN PASO, y la novena rebotada.
    //
    //  La triada de arriba pasaba igual con el tope viejo -tres notas caben en
    //  cuatro- asi que no media el tope, solo que el acorde existe. Esta si: se
    //  escriben OCHO por la rejilla, que es el maximo, y detras una NOVENA que
    //  tiene que rebotar. Las dos cifras hacen falta: "caben ocho" lo cumpliria
    //  una rejilla sin tope ninguno, y esa se comeria las voces del pad.
    //
    //  Por la rejilla y no por setStepExtra: el tope de la rejilla y el de la
    //  celda del motor son numeros distintos atados por un static_assert, y
    //  llamar a la API salta justo el camino que la persona usa.
    for (int semi : { 0, 2, 4, 5, 7, 9, 11, 12 }) pianoCellToggled (6, semi);
    std::cout << "{\"piano\":\"acorde ocho\",\"notas\":" << notasDe (6, selectedPad)
              << ",\"tope\":" << PianoRoll::kMaxNotas << "}" << std::endl;
    status.setText ({}, juce::dontSendNotification);
    pianoCellToggled (6, 14);
    //  Y CON EL AVISO, que es la mitad que se ve: un toque tragado en silencio
    //  se lee como que la rejilla no responde. La cuenta de notas sola no puede
    //  cazarlo -con el tope quitado, la novena la tira igual la celda del motor
    //  y la lista sale identica-, asi que lo que se mide es lo que la app DICE.
    std::cout << "{\"piano\":\"acorde nueve\",\"notas\":" << notasDe (6, selectedPad)
              << ",\"aviso\":\"" << status.getText().toStdString() << "\"}" << std::endl;

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
    if (pianoGrid.onCelda) pianoGrid.onCelda (3, 9, false);

    //  Y AHORA AL COMPAS 1, POR LA BARRA y no moviendo `seqPrimerCelda` a
    //  mano: mover la variable por dentro se salta el codigo que decide cuanto
    //  avanza un toque y quien repinta despues, que es donde vivieron dos de
    //  los cinco fallos. Un toque en la PISTA, mas alla del pulgar, salta una
    //  pagina — o sea las columnas que se ven, que aqui son dieciseis.
    tocaBarra (seqBarra, 1.0f);
    if (pianoGrid.onCelda) pianoGrid.onCelda (3, 5, false);

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
    const int baseTrasTocar = seqPrimerCelda;
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
    //  recibe una COLUMNA y el codigo la convierte con `seqPrimerCelda`, asi que
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
    const int baseGoma = juce::jlimit (0, kNumSteps - 4, seqPrimerCelda);
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
    for (int c = 0; c < StepGrid::kBarSteps; ++c)
        for (int k = 0; k < PianoRoll::kMaxNotas; ++k)
            if (pianoCells[c * PianoRoll::kMaxNotas + k] != -128) ++viejas;
    std::cout << "{\"piano\":\"encoge\",\"sel\":" << (seqPrimerCelda / StepGrid::kBarSteps)
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
    seqPrimerCelda = 0;
    showSeqPage (seqPagePiano);
    resized();
    refreshPiano();

    //  El punto de una celda, en coordenadas de la rejilla. La canaleta del
    //  teclado se salta a proposito: ahi el gesto suena y no escribe.
    const int filas = pianoGrid.getFilas();
    const float altoFila = (float) pianoGrid.getHeight() / (float) juce::jmax (1, filas);
    const float anchoCol = (float) (pianoGrid.getWidth() - PianoRoll::kGutter)
                             / (float) StepGrid::kBarSteps;
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
        //
        //  LA PILA SE VACIA ANTES, como en las otras tres medidas de deshacer
        //  de este mismo fichero. La pila tiene tope -kUndoDepth = 16- y este
        //  bloque corre detras de todo lo que el volcado del piano ya escribio:
        //  llena, `size()` deja de crecer y la resta da CERO aunque se haya
        //  apuntado la entrada. Paso: al subir el tope del acorde a ocho, el
        //  volcado gano nueve toques mas -ocho notas y la novena rebotada- y
        //  esta regla se puso roja sin que el codigo que mide hubiera cambiado.
        //  La prueba estaba midiendo el crecimiento de una pila con tope, que
        //  es una cifra distinta de "cuantas entradas apunto el movimiento".
        undoStack.clear(); redoStack.clear();
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
        seqPrimerCelda = StepGrid::kBarSteps;
        engine.setPatternLength (0, 32);
        refreshPiano();
        pianoPegaSel();
        int pegadas = 0;
        for (int st = 16; st < 32; ++st)
            if (pattern[0][(size_t) st][(size_t) selectedPad]) ++pegadas;

        pianoGrid.setHerramienta (PianoRoll::dibujar);
        seqPrimerCelda = 0;

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
        seqPrimerCelda = 0;
        //  POR LA TAPA y no poniendo el numero a mano, que es lo unico que
        //  mide la ESCALERA: el ciclo salta el paso que no cabe, y llamando a
        //  `pianoCols = 32` por dentro eso no se ve nunca. Tres pulsaciones,
        //  que es la vuelta completa del ciclo.
        pianoCols = StepGrid::kBarSteps;
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
        pianoCols = StepGrid::kBarSteps;
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
            //  el PASO sale de `seqPrimerCelda + columna`, que es la unica
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
                    s << (first ? "" : ",") << (seqPrimerCelda + c);
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
    //  Y LA SEPTIMA EXTRA, que es la que vive en la mitad alta de la celda.
    //  El acorde se guarda en un uint64 desde que el tope subio a ocho notas,
    //  y el lector del fichero recortaba a uint32: con solo dos extras -bits
    //  bajos- ese recorte salia verde. Esta cae en los bits 48..55 y el de
    //  presencia en el 62, asi que un proyecto que vuelva recortado la pierde.
    engine.setStepExtra (0, 0, 0, AudioEngine::kExtraNotes - 1, -5, true);
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
    //  Y CON EL PASO DENTRO DEL COMPAS DISTINTO DE CERO en los dos, que es el
    //  campo nuevo: escrito al FINAL de la fila del fichero para no correr los
    //  seis de antes, asi que la unica forma de saber que se lee es ponerlo a
    //  un valor que no sea el defecto. Con cero, un lector que lo ignorara
    //  entero pasaria la prueba.
    clips.push_back ({ /*pad*/ 0,  /*pista*/ 1, /*compas*/ 3, /*paso*/ 5,
                       /*desde*/ 100, /*largo*/ 4800, 0.75f });
    clips.push_back ({ /*pad*/ 16, /*pista*/ 2, /*compas*/ 7, /*paso*/ 11,
                       /*desde*/ 250, /*largo*/ 9600, 0.50f });
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
                                  << engine.getStepExtra (0, 0, 0, 2) << ","
                                  << engine.getStepExtra (0, 0, 0, AudioEngine::kExtraNotes - 1) << "]"
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

    //  ============ UN PROYECTO ESCRITO CON EL TOPE DE CUATRO ============
    //
    //  Y ESTA ES LA QUE FALTABA, que es la parte que importa de todo el bloque.
    //  Lo de arriba es una ida y vuelta CON LA MISMA COMPILACION: escribe con
    //  el formato de hoy y lee con el formato de hoy, asi que le cuadra
    //  cualquier formato mientras sea consistente consigo mismo. Un cambio de
    //  empaquetado le sale verde por construccion, y le salio: subir el acorde
    //  a ocho notas mudo los bits de presencia del 24..26 al 56..62 y los
    //  proyectos guardados con la version anterior empezaron a cargar SOLO LA
    //  TONICA, con el banco entero en verde y 46 de 46.
    //
    //  La entrada NO LA ESCRIBE ESTA COMPILACION: es la cadena literal que
    //  escribia la version vieja. Paso 0, pad 0, y la celda 0x070c0704:
    //      byte 0 = 0x04 -> +4 semitonos      bit 24 -> la primera existe
    //      byte 1 = 0x07 -> +7 semitonos      bit 25 -> la segunda existe
    //      byte 2 = 0x0c -> +12 semitonos     bit 26 -> la tercera existe
    //  o sea un acorde mayor con la octava. Por el camino de verdad -el arbol
    //  que lee el fichero de proyecto- y no llamando a la migracion a mano:
    //  *lo que importa no es lo que devuelve la orden sino que acabo en el
    //  motor.*
    {
        auto arbol = captureState();
        if (auto banks = arbol.getChildWithName ("BANKS"); banks.isValid() && banks.getNumChildren() > 0)
            banks.getChild (0).setProperty ("chords", "0 0 70c0704", nullptr);

        applyState (arbol);

        std::cout << "{\"viejo\":1,\"acorde\":[" << engine.getStepExtra (0, 0, 0, 0) << ","
                                                    << engine.getStepExtra (0, 0, 0, 1) << ","
                                                    << engine.getStepExtra (0, 0, 0, 2) << "]}"
                  << std::endl;
    }

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
                  << "," << c.desde << "," << c.largo << "," << c.gain
                  << "," << c.paso << "]";
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
        vaciaCancion();
        ponBloqueCompas (0, 0, 3);
        ponBloqueCompas (1, 4, 2);
        publicaBloques();

        const auto destino = ProjectStore::folderFor (f.getFileNameWithoutExtension());
        ProjectStore::ensureDirectory (destino);
        f.copyFileTo (destino.getChildFile ("project.xml"));
        loadProject (f.getFileNameWithoutExtension());
        //  Y SE ESPERA A QUE TERMINE, que es la linea que faltaba. Desde que
        //  la Tanda 26 puso los pads en su hebra, `loadProject` solo ARRANCA
        //  el trabajo -`PadLoadJob` lee y rinde fuera, y `applyState` corre en
        //  `finishProjectOpen`, o sea despues-, asi que leer el estado en la
        //  linea siguiente lee el del proyecto ANTERIOR. Y peor: con el
        //  trabajo en vuelo, `padsBusy()` sigue puesto y el `loadProject` de
        //  la vuelta siguiente se va por su primera linea sin abrir nada.
        //  Medido: los SIETE ficheros congelados salian con las mismas cifras
        //  -las de «ayer»: g0.20 p0.90 c300 canal5- y `Tests/session.py`
        //  imprimia «HEREDA DEL ANTERIOR» siete veces y salia con 1. La misma
        //  espera que `auditDisperso` ya hacia veinte lineas mas arriba; una
        //  prueba que no espera al trabajo que mide no mide ese trabajo.
        while (padJob != nullptr) stepPadJob();

        //  Cuantos compases de la cancion llevan algo: con <song> ausente tiene
        //  que ser CERO, y antes salia el arreglo del proyecto anterior.
        int celdasCancion = 0;
        for (int ln = 0; ln < AudioEngine::kSongLanes; ++ln)
            for (int bar = 0; bar < AudioEngine::kSongBars; ++bar)
                if (celdaCancion (ln, bar) != 0) ++celdasCancion;

        //  Y LA LISTA CONVERTIDA, en el MISMO formato que `auditArrange`
        //  -[carril, banco, pasoAbsoluto, largo, offset, mudo]- para que las
        //  dos reglas hablen el mismo idioma y no haya dos volcados que digan
        //  lo mismo de dos maneras.
        juce::String listaBloques;
        {
            const int pc = juce::jmax (1, engine.pasosPorCompas());
            for (const auto& b : bloques)
            {
                if (listaBloques.isNotEmpty()) listaBloques << ",";
                listaBloques << "[" << b.lane << "," << b.bank << ","
                             << (b.compas * pc + b.paso) << "," << b.largo << ","
                             << b.offset << "," << (b.mudo ? 1 : 0) << "]";
            }
        }

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
                  //  Y LA CANCION CONVERTIDA, bloque a bloque, que es la cifra
                  //  que faltaba. Arriba va `cancion`, un CONTEO de compases
                  //  con algo, y era lo unico que fijaba la rama de conversion
                  //  de `lane0..lane3` + `bmudos` a la lista de bloques: un
                  //  lector que convirtiera al carril equivocado, con el largo
                  //  equivocado o perdiendo el mudo daria exactamente el mismo
                  //  numero. Es la leccion de siempre -«volvio algo» no es
                  //  «volvio lo mismo»- y aqui costaba una linea.
                  << ",\"bloques\":[" << listaBloques << "]"
                  //  Y LOS PASOS POR COMPAS, que es de donde la regla deriva
                  //  los largos en vez de escribir un 16 a mano: la rejilla de
                  //  la pagina abierta decide `pc`, y una prueba con el numero
                  //  clavado sale en rojo con la app perfecta -ya paso en
                  //  `sel.py`, que esperaba 16 y veia 64-.
                  << ",\"pc\":" << engine.pasosPorCompas()
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

    //  5b. Y EL SOLO DEL CANAL, por la misma puerta. Con DOS cifras, que una
    //      sola se engaña: que el motor lo SEPA -`getCanalSolo`- y que sepa que
    //      HAY alguno -`anyCanalSolo`, el bit cacheado que el hilo de audio lee
    //      de verdad-. Sin la segunda, la primera la cumple un `setCanalSolo`
    //      que guarda el bit y se olvida de `refreshCanalSolo`, y entonces el
    //      solo se ve encendido en la tapa y no calla a nadie: la app muda por
    //      dentro con la cara diciendo que si.
    if (canSolos[7] != nullptr) { canSolos[7]->setToggleState (true, juce::dontSendNotification);
                                  if (canSolos[7]->onClick) canSolos[7]->onClick(); }
    const int soloCanal7 = engine.getCanalSolo (7) ? 1 : 0;
    const int haySolo    = engine.anyCanalSolo()   ? 1 : 0;
    if (canSolos[7] != nullptr) { canSolos[7]->setToggleState (false, juce::dontSendNotification);
                                  if (canSolos[7]->onClick) canSolos[7]->onClick(); }
    const int soloTrasApagar = engine.anyCanalSolo() ? 1 : 0;

    //  5c. UN EFECTO ENTRA SONANDO: encendido y con el envio de su canal al
    //      maximo.
    //
    //      Del telefono: «el envio predeterminado al mixer del efecto debe ser
    //      al 100 como Default, pero que este activado tambien el efecto cuando
    //      se mete en el Slot». Poner un efecto dejaba las dos cosas donde
    //      estaban -apagado y el envio en su cero de fabrica- asi que el gesto
    //      entero no movia un decibelio y la tapa se pintaba llena.
    //
    //      TRES cifras: el envio, el interruptor, y que **el canal de al lado NO
    //      se entere**. Sin la tercera, «el envio entra al maximo» lo cumple un
    //      codigo que lo sube en los treinta y dos, que meteria en la reverb
    //      treinta y un canales que nadie mando.
    //      Y UNA CUARTA, que es la que faltaba y la que se pago: que el PAD
    //      acabe mandando de verdad.
    //
    //      Las tres de arriba se cumplian y el efecto no sonaba, porque el
    //      envio se escribe en el CANAL y quien tiene que mandar es el pad —que
    //      nace SIN canal, los sesenta y cuatro—. `refrescaSendMask` lo dice
    //      entero: «un pad SIN canal no manda a ningun bus». O sea que el
    //      interruptor encendido y el envio al maximo alimentaban un bus al que
    //      no llegaba una muestra.
    //
    //      Se mide sobre `padSendMask`, que es lo que el hilo de audio lee de
    //      verdad, y no sobre `getPadCanal`: que el pad tenga canal es el medio,
    //      que el bit este puesto es el fin. Preguntando por el canal, esta
    //      comprobacion la cumpliria un `setPadCanal` a un canal que no manda a
    //      ningun efecto.
    for (auto& f : slotFx) f.fill (kSlotVacia);
    setFxEnabled (AudioEngine::kFxDrv, false);
    ponCanalActual (2);
    engine.setPadCanal (selectedPad >= 0 ? selectedPad : 0, AudioEngine::kSinCanal);
    ponEnRanura (0, AudioEngine::kFxDrv);
    const double envioAlEntrar = (double) engine.getCanalSend (2, AudioEngine::kFxDrv);
    const int    encendidoAlEntrar = fxEncendido (AudioEngine::kFxDrv) ? 1 : 0;
    const double envioDelVecino = (double) engine.getCanalSend (3, AudioEngine::kFxDrv);
    //  6b. Y EL PAN DEL CANAL VUELVE DEL FICHERO.
    //
    //  Es una propiedad NUEVA -`cpan`, `canc`- y una propiedad que nadie mide es
    //  una que se pierde en la primera tanda que toque el guardado. Se hace por
    //  el camino de verdad: `captureState` y `applyState`, que es el mismo que
    //  la cuenta de los pads de los bancos altos uso para salir.
    engine.setCanalPan   (7, -0.75f);
    engine.setCanalAncho (7,  1.60f);
    const auto arbolPan = captureState();
    engine.setCanalPan   (7, 0.0f);
    engine.setCanalAncho (7, 1.0f);
    applyState (arbolPan);
    const double panVuelve = (double) engine.getCanalPan (7);
    const double ancVuelve = (double) engine.getCanalAncho (7);
    engine.setCanalPan   (7, 0.0f);
    engine.setCanalAncho (7, 1.0f);

    const int    mandaAlEntrar = (selectedPad >= 0
                                    && ((engine.getPadSendMask() >> (unsigned) selectedPad) & 1ull)) ? 1 : 0;

    //  5b. LA SECUENCIA DEL TELEFONO, PASO POR PASO.
    //
    //      «Meto un sonido, pongo una caja en el pad 2, que esta sin canal. Lo
    //      linkeo al 3, pongo el EQ en el 3, en el slot 1, y ese EQ ni analiza
    //      nada ni modifica nada.»
    //
    //      Las cinco cifras que hacen falta para saber DONDE se rompe, porque
    //      con una sola no se puede: en que canal quedo el pad, cual esta
    //      editando la cara, en que canal acabo el EQ, si el pad manda, y si la
    //      mezcla del efecto llego al canal donde esta puesto.
    for (auto& f : slotFx) f.fill (kSlotVacia);
    setFxEnabled (AudioEngine::kFxEq, false);
    for (int p = 0; p < kNumPads; ++p) engine.setPadCanal (p, AudioEngine::kSinCanal);
    selectPad (2);
    ponCanalActual (3);
    engine.setPadCanal (2, 3);      // «lo linkeo al 3»
    selectPad (2);                  // y se vuelve a el, como al cerrar la ficha
    ponEnRanura (1, AudioEngine::kFxEq);
    const int    tfCanalPad   = engine.getPadCanal (2);
    const int    tfCanalCara  = canalActual;
    const int    tfCanalDelEq = canalDeFx (AudioEngine::kFxEq);
    const int    tfManda      = (int) ((engine.getPadSendMask() >> 2u) & 1ull);
    const double tfMezcla     = (double) engine.getFxParam (tfCanalDelEq >= 0 ? tfCanalDelEq : 0,
                                                            AudioEngine::kFxEq, 2);
    const double tfEnvio      = (double) engine.getCanalSend (tfCanalDelEq >= 0 ? tfCanalDelEq : 0,
                                                              AudioEngine::kFxEq);

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
              << ",\"solo_canal7\":" << soloCanal7
              << ",\"hay_solo\":" << haySolo
              << ",\"solo_tras_apagar\":" << soloTrasApagar
              << ",\"envio_al_entrar\":" << envioAlEntrar
              << ",\"encendido_al_entrar\":" << encendidoAlEntrar
              << ",\"envio_del_vecino\":" << envioDelVecino
              << ",\"manda_al_entrar\":" << mandaAlEntrar
              << ",\"pan_vuelve\":" << juce::String (panVuelve, 2)
              << ",\"anc_vuelve\":" << juce::String (ancVuelve, 2)
              << ",\"tf_canal_pad\":" << tfCanalPad
              << ",\"tf_canal_cara\":" << tfCanalCara
              << ",\"tf_canal_eq\":" << tfCanalDelEq
              << ",\"tf_manda\":" << tfManda
              << ",\"tf_mezcla\":" << juce::String (tfMezcla, 2)
              << ",\"tf_envio\":" << juce::String (tfEnvio, 2)
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
    //  Y LO QUE VALE ANTES DE TOCARLA, que es la cifra que faltaba.
    //
    //  Desde que se pidio que «el efecto este activado tambien cuando se mete
    //  en el Slot», `ponEnRanura` lo deja ENCENDIDO, asi que el primer toque
    //  en esa tapa lo APAGA — que es lo que un interruptor hace. La prueba
    //  seguia pidiendo «tocar una ranura llena la enciende», que era cierto
    //  cuando entraban apagadas: se quedo vieja al cambiar lo que mide, y daba
    //  FALLA sobre un comportamiento correcto. Con las DOS cifras la pregunta
    //  se contesta entera -entra encendida, y la tapa conmuta- y ninguna de
    //  las dos la cumple sola.
    const int enciendeAlEntrar = fxEncendido (0) ? 1 : 0;
    pulsaTapa (fxButtons[0]);
    const int menuTrasLlena = ranuraEditada;
    const int encendioAlTocar = fxEncendido (0) ? 1 : 0;
    abreMenuRanura (-1);
    setFxEnabled (0, false);

    //  2. ELEGIR EN EL MENU LLENA LA RANURA, y desde la cara ya no se cambia:
    //     esa tapa pasa a encender y apagar. Es la ACCION UNICA que se pidio.
    for (int s = 0; s < kNumRanuras; ++s) ponEnRanura (s, kSlotVacia);
    pulsaTapa (fxButtons[2]);                       // el «+» de la ranura 2
    pulsaTapa (ranuraBtns[celdaDeFx (AudioEngine::kFxBit)]);   // se elige BIT
    const juce::String trasElegir = mapa();
    const int menuTrasElegir = ranuraEditada;   // se cierra sola
    //  Igual que arriba: elegir en el menu la deja ENCENDIDA, asi que el
    //  estado que hay que mirar antes de conmutar es este.
    const int enciendeAlElegir = fxEncendido (4) ? 1 : 0;
    pulsaTapa (fxButtons[2]);                       // y ahora ese boton CONMUTA
    const int enciendeDespues = fxEncendido (4) ? 1 : 0;
    const juce::String mapaDespues = mapa();    // que no ha cambiado
    setFxEnabled (4, false);

    //  3. UN TIPO, UNA RANURA. Poner en la 0 un tipo que ya estaba en la 3
    //     tiene que DEJAR LA 3 VACIA: dos ranuras del mismo tipo serian dos
    //     ventanas al mismo aparato del motor.
    for (int s = 0; s < kNumRanuras; ++s) ponEnRanura (s, s);
    abreMenuRanura (0);
    pulsaTapa (ranuraBtns[celdaDeFx (3)]);
    const juce::String trasMover = mapa();

    //  4. VACIAR UNA RANURA APAGA SU EFECTO. Un efecto encendido cuya tapa
    //     desaparece sigue sonando y no hay donde tocarlo.
    for (int s = 0; s < kNumRanuras; ++s) ponEnRanura (s, s);
    setFxEnabled (3, true);
    const int antesDeVaciar = fxEncendido (3) ? 1 : 0;
    ponEnRanura (3, kSlotVacia);
    const int trasVaciar = fxEncendido (3) ? 1 : 0;

    //  4b. EL GESTO ENTERO CONTRA EL AUDIO — la medida que no tenia nadie.
    //
    //  Llego del telefono: «cuando inserto un efecto en uno de los slots, hasta
    //  que no voy al mixer, a rack, y toco el fader de 0 a 100, no puedo tocar
    //  los parametros; es como que estan bloqueados». Lo estaban, por dos
    //  sitios a la vez -`ponEnRanura` no pedia el foco y el `setEnabled` de los
    //  tres mandos vivia en una funcion que `focusFx` no llama- y el banco
    //  entero dijo que si con el fallo dentro. La razon, contada en columnas:
    //
    //    medida                       gesto     mide audio   abre el envio
    //    auditCanales 5c + telefono   casi        NO            no
    //    auditRanuras 1-4             SI          NO            no
    //    auditRack capa viva          casi        si            SI
    //    StressTest un inserto        no          si            SI
    //    auditFxPresets               casi        si            SI
    //
    //  «Mide audio» y «no abre el envio» no coinciden en ninguna fila. Las tres
    //  que rinden bloques se ponen el envio y el canal del pad a mano -es el
    //  andamio de `enCanalCero`- y por eso no pueden ver un camino que nace
    //  cerrado: *una prueba que se adapta al defecto deja de medirlo.* Esta es
    //  la que junta las dos mitades, y para eso NO toca `setCanalSend` ni
    //  `setPadCanal` despues del gesto: el andamio solo pone el estado de
    //  FABRICA -pad SIN canal, ranuras vacias, los envios a cero- y a partir de
    //  ahi todo lo hace la tapa.
    //
    //  En el canal 4 y no en el 0, que es la leccion de `dff3c09`: el cero era
    //  justo el unico que funcionaba.
    int    gestoFoco = -1, gestoMandos = -1, gestoCanalPad = -1;
    int    mantenFoco = -1, mantenMandos = -1, mantenApagados = -1;
    double gestoEnvio = -1.0, gestoCambia = -1.0, mandoCambia = -1.0;
    double rmsAntes = 0.0, rmsDespues = 0.0;
    {
        constexpr int kCanalG  = 4;
        constexpr int kBloque  = 128;
        //  La misma ventana que `auditFxPresets` derivo alli -683 ms- y por la
        //  misma razon: *el liston de una prueba no se reinventa en la de al
        //  lado*. Con 64 ms el eco de un DLY no entra y sale «mudo».
        constexpr int kBloques = 256;
        const int fG = AudioEngine::kFxDrv;

        engine.prepareToPlay (48000.0, kBloque);

        //  UN RUIDO Y NO UN TONO: un seno deja fuera casi todo el espectro y
        //  entonces «no cambia nada» diria mas de donde cayo el tono que del
        //  efecto. Es el mismo ruido que usan la capa viva del rack y los
        //  presets, con su semilla escrita.
        auto ruido = []
        {
            auto* sb = new SampleBuffer();
            const int n = 24000;
            sb->buffer.setSize (2, n);
            juce::Random r (20260918);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                    sb->buffer.setSample (c, i, 0.5f * (r.nextFloat() * 2.0f - 1.0f));
            sb->sourceSampleRate = 48000.0;
            return SampleBuffer::Ptr (sb);
        };

        //  DOS TIRADAS Y SE GUARDA LA SEGUNDA, y esto es una medida y no una
        //  precaucion: con una sola, dos tiradas IDENTICAS salian distintas en
        //  el **25.00 %** de las muestras. Los parametros del motor van
        //  suavizados, asi que la primera tirada despues de poner un efecto
        //  todavia esta llegando a su sitio y la siguiente ya no. Esa deriva se
        //  comia la rotura a proposito de `macroMoved`: con el mando
        //  desconectado del motor la cifra salia 25 % —muy por encima del 2 %—
        //  y la prueba decia OK. *Una prueba que nunca se ha visto fallar no es
        //  una prueba.*
        auto rinde = [&] (juce::AudioBuffer<float>& out)
        {
            out.setSize (2, kBloque * kBloques, false, false, true);
            juce::AudioBuffer<float> b (2, kBloque);
            for (int pasada = 0; pasada < 2; ++pasada)
            {
                engine.postPanic();
                out.clear();
                engine.postNoteOn (0, 1.0f);
                for (int i = 0; i < kBloques; ++i)
                {
                    b.clear();
                    engine.renderNextBlock (b, 0, kBloque);
                    for (int ch = 0; ch < 2; ++ch)
                        out.copyFrom (ch, i * kBloque, b, ch, 0, kBloque);
                }
            }
        };

        auto compara = [] (const juce::AudioBuffer<float>& a,
                           const juce::AudioBuffer<float>& b)
        {
            const int n = juce::jmin (a.getNumSamples(), b.getNumSamples());
            if (n <= 0) return 0.0;
            int distintas = 0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                    if (std::abs (a.getSample (ch, i) - b.getSample (ch, i)) > 1.0e-5f)
                        ++distintas;
            return 100.0 * (double) distintas / (double) (2 * n);
        };

        auto rms = [] (const juce::AudioBuffer<float>& a)
        {
            const int n = a.getNumSamples();
            if (n <= 0) return 0.0;
            double e = 0.0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i) e += (double) a.getSample (ch, i) * a.getSample (ch, i);
            return std::sqrt (e / (double) (2 * n));
        };

        //  EL ESTADO DE FABRICA, y ni una linea mas. `kSinCanal` es donde nacen
        //  los sesenta y cuatro y el cero es donde nacen los envios: ponerlos
        //  ahi no es abrir nada, es deshacer lo que las comprobaciones de
        //  arriba dejaron puesto. *Una prueba que arrastra estado mide otra
        //  cosa.*
        closeAllSheets();
        abreMenuRanura (-1);
        for (int c = 0; c < kNumCanales; ++c)
            for (int k = 0; k < kNumRanuras; ++k) slotFx[(size_t) c][(size_t) k] = kSlotVacia;
        for (int c = 0; c < kNumCanales; ++c)
            for (int k = 0; k < kNumFx; ++k) engine.setCanalSend (c, k, 0.0f);
        for (int k = 0; k < kNumFx; ++k) ponFxEncendido (k, false);
        engine.setPadCanal (0, AudioEngine::kSinCanal);
        engine.setPadGain  (0, 1.0f);
        engine.publishSample (0, ruido());
        selectPad (0);
        ponCanalActual (kCanalG);
        refrescaRanuras();

        juce::AudioBuffer<float> antes, despues, movido;
        rinde (antes);

        //  EL GESTO, y nada mas que el gesto: el «+» de la ranura 0 y la celda
        //  de DRV en el menu que se abre. Dos toques, los mismos dos que hace
        //  un dedo.
        pulsaTapa (fxButtons[0]);
        pulsaTapa (ranuraBtns[celdaDeFx (fG)]);

        gestoFoco     = focusedFx;
        gestoMandos   = (macroCtrl1.isEnabled() ? 1 : 0)
                      + (macroCtrl2.isEnabled() ? 1 : 0)
                      + (macroCtrl3.isEnabled() ? 1 : 0);
        gestoCanalPad = engine.getPadCanal (0);
        gestoEnvio    = (double) engine.getCanalSend (kCanalG, fG);

        rinde (despues);
        gestoCambia = compara (antes, despues);
        rmsAntes    = rms (antes);
        rmsDespues  = rms (despues);

        //  Y LA OTRA MITAD DEL SINTOMA, que tampoco medía nadie: que MOVER uno
        //  de los tres mandos cambie el audio. `macroCtrl1/2` se usan en cuatro
        //  auditorias y las cuatro comparan contra estado o contra el visor;
        //  ninguna llega a `renderNextBlock`. Se mueve por su propio
        //  `onValueChange` -o sea pasando por `macroMoved`- y no escribiendo el
        //  parametro por dentro, que es lo unico que recorre el camino entero.
        {
            const auto& sp = fxDefs[fG].spec[0];
            const double hoy = macroCtrl1.getValue();
            const double a   = std::abs (sp.hi - hoy) >= std::abs (hoy - sp.lo) ? sp.hi : sp.lo;
            macroCtrl1.setValue (a, juce::sendNotificationSync);
        }
        rinde (movido);
        mandoCambia = compara (despues, movido);

        //  Y LA SEGUNDA GRIETA, que este mismo gesto NO puede ver.
        //
        //  El `setEnabled` de los tres mandos vivia en `refrescaRanuras`, y
        //  `focusFx` no la llama -ni `fxTapped` ni `fxFocusOnly` tampoco-. Con
        //  el foco ya arreglado eso no se nota poniendo un efecto, porque
        //  `ponEnRanura` termina en `refrescaRanuras` de todos modos: hace
        //  falta un camino que mueva el foco SIN pasar por ella. Es MANTENER
        //  pulsada una ranura llena, que es el gesto de «dame los mandos sin
        //  apagarlo».
        //
        //  Y hace falta llegar con los mandos APAGADOS, o la cifra no puede
        //  fallar: se vacia la ranura del efecto enfocado -que los apaga, y con
        //  razon- dejando la otra llena. De ahi el rodeo de tres pasos.
        ponEnRanura (1, AudioEngine::kFxCmp);       // el foco se va al CMP
        ponEnRanura (1, kSlotVacia);                // y se queda sin sitio: mandos OFF
        mantenApagados = (macroCtrl1.isEnabled() ? 1 : 0)
                       + (macroCtrl2.isEnabled() ? 1 : 0)
                       + (macroCtrl3.isEnabled() ? 1 : 0);
        //  `fxButtons` es un `OwnedArray<TextButton>` y el gesto vive en
        //  `HoldButton`, que es quien lo declara: se baja el tipo para
        //  llamar al MISMO `onHold` que dispara un dedo, y no a
        //  `ranuraMantenida` por dentro, que se saltaria justo la puerta.
        if (auto* b = dynamic_cast<HoldButton*> (fxButtons[0]))
            if (b->onHold) b->onHold();
        mantenFoco   = focusedFx;
        mantenMandos = (macroCtrl1.isEnabled() ? 1 : 0)
                     + (macroCtrl2.isEnabled() ? 1 : 0)
                     + (macroCtrl3.isEnabled() ? 1 : 0);
    }

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
    //  Y CON EL NUMERO DE COLUMNAS PEDIDO, que es la mitad que faltaba y que
    //  hacia que esta medida contestara por OTRA rejilla. `resized()` llama
    //  con `kFxPorTipo` -cada fila es una familia de efectos- y aqui se
    //  llamaba sin el, asi que la tabla publicaba 3x10 a 412x915 mientras la
    //  app pintaba 5x6. Una medida que no llama igual que el codigo que mide
    //  no mide ese codigo: mide otro. Salio a la luz al poner los rotulos de
    //  familia, que solo existen cuando cada fila ES una familia.
    auto forma = [&] (int n, int pedido)
    {
        const int c = menuRanuraColumnas (n, topeR, anchoR, true, pedido);
        const int f = (n + c - 1) / c;
        //  Y las bandas de familia se cuentan cuando las hay, por lo mismo:
        //  el alto que publica esta linea tiene que ser el que la tarjeta
        //  pide de verdad.
        const int bandas = (c == pedido && f == kFxCategorias) ? f : 0;
        return juce::String (c) + "x" + juce::String (f) + ":"
                 + juce::String (menuRanuraPide (f, true, Metrics::btn, bandas)) + ":"
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

    //  6. LOS SEIS ROTULOS DE FAMILIA, Y QUE CADA UNO ESTE SOBRE LA SUYA.
    //
    //  Que existan no basta: un rotulo colocado sobre la fila equivocada pasa
    //  cualquier regla que solo cuente cuantos hay, y seria peor que no
    //  tenerlo -diria que FLT es una saturacion-. Asi que se publica, por
    //  familia, el nombre, si la banda tiene limites, y si esa banda queda POR
    //  ENCIMA de la primera tapa de su fila. Se abre el menu de verdad y se
    //  deja que `resized()` reparta: los limites salen del maquetado, que es
    //  el unico que sabe donde acabaron.
    abreMenuRanura (0);
    resized();
    juce::String familias;
    int bandasPuestas = 0, bandasSobreSuFila = 0;
    for (int cat = 0; cat < kFxCategorias; ++cat)
    {
        const auto banda = ranuraCatArea[(size_t) cat];
        const int celda = cat * kFxPorTipo;
        const bool hay = ! banda.isEmpty();
        const bool sobre = hay && celda < ranuraBtns.size() && ranuraBtns[celda] != nullptr
                             && ! ranuraBtns[celda]->getBounds().isEmpty()
                             && banda.getBottom() <= ranuraBtns[celda]->getY();
        if (hay) ++bandasPuestas;
        if (sobre) ++bandasSobreSuFila;
        familias << (cat ? "," : "") << categoriasFx()[cat] << ":" << (sobre ? 1 : 0);
    }
    abreMenuRanura (-1);
    //  Y CERRADO EL MENU, LAS BANDAS TIENEN QUE QUEDAR VACIAS. Son bandas
    //  PINTADAS y no componentes, asi que no hay `setBounds({})` que las
    //  apague: si sobreviven al cierre, el pintor las sigue dibujando encima
    //  de lo que haya debajo. Es el mismo fallo que ya costo el titulo de la
    //  ficha del instrumento, y la septima regla del banco existe por el.
    int bandasQueSobreviven = 0;
    for (const auto& r : ranuraCatArea) if (! r.isEmpty()) ++bandasQueSobreviven;

    std::cout << "{\"ranuras\":1"
              << ",\"familias\":\"" << familias << "\""
              << ",\"bandas\":" << bandasPuestas
              << ",\"bandas_sobre\":" << bandasSobreSuFila
              << ",\"bandas_zombis\":" << bandasQueSobreviven
              << ",\"tipos\":"     << kNumFx
              << ",\"params\":\""   << claves  << "\""
              << ",\"nombres\":\""  << nombres << "\""
              << ",\"forma\":\""          << forma (kNumFx, kFxPorTipo) << "\""
              //  Y LA FORMA CON UN TIPO MAS, que es la pregunta que de verdad
              //  importa: el dia que entre el siguiente, sigue cabiendo el
              //  menu? Estaba escrita como `forma21` -un numero clavado que
              //  valia lo mismo que `kNumFx` el dia que se escribio- y en
              //  cuanto entraron WAH y OCT paso a probar una cuenta que la
              //  app ya no tiene, con el veredicto del script diciendo «cabe
              //  a 21» para siempre. Una afirmacion sin medida, en la prueba.
              //  Con un tipo mas las familias dejan de salir de cinco -31 no
              //  es multiplo de 5-, asi que esa pregunta se hace SIN pedido:
              //  es la rejilla que quedaria, no la de familias.
              << ",\"formaMas\":\""       << forma (kNumFx + 1, 0) << "\""
              << ",\"tope_tarjeta\":"    << topeR
              << ",\"defectos_cruzados\":" << defectosQueNoCuadran
              << ",\"canales\":"           << kNumCanales
              << ",\"canales_raros\":"     << canalesRaros
              << ",\"menu_tras_vacia\":"   << menuTrasVacia
              << ",\"menu_tras_llena\":"   << menuTrasLlena
              << ",\"enciende_al_entrar\":" << enciendeAlEntrar
              << ",\"enciende_al_elegir\":" << enciendeAlElegir
              << ",\"enciende_al_tocar\":" << encendioAlTocar
              << ",\"tras_elegir\":\""     << trasElegir << "\""
              << ",\"menu_tras_elegir\":"  << menuTrasElegir
              << ",\"enciende_despues\":"  << enciendeDespues
              << ",\"mapa_despues\":\""    << mapaDespues << "\""
              << ",\"tras_mover\":\""      << trasMover << "\""
              << ",\"antes_de_vaciar\":"   << antesDeVaciar
              << ",\"tras_vaciar\":"       << trasVaciar
              //  EL GESTO ENTERO, siete cifras. Ver el bloque 4b.
              << ",\"gesto_foco\":"        << gestoFoco
              << ",\"gesto_drv\":"         << AudioEngine::kFxDrv
              << ",\"gesto_mandos\":"      << gestoMandos
              << ",\"gesto_canal_pad\":"   << gestoCanalPad
              << ",\"gesto_envio\":"       << juce::String (gestoEnvio, 3)
              << ",\"gesto_cambia\":"      << juce::String (gestoCambia, 3)
              << ",\"gesto_rms_antes\":"   << juce::String (rmsAntes, 6)
              << ",\"gesto_rms_despues\":" << juce::String (rmsDespues, 6)
              << ",\"mando_cambia\":"      << juce::String (mandoCambia, 3)
              << ",\"manten_apagados\":"  << mantenApagados
              << ",\"manten_foco\":"      << mantenFoco
              << ",\"manten_mandos\":"    << mantenMandos
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

            double diagPre = 0.0, diagPost = 0.0; int diagVivo = 0;
            double diagPre1 = 0.0, diagPost1 = 0.0; int diagVivo1 = 0;
            auto lee = [&] (bool conSenal, int tics)
            {
                //  EL PAD 0 SE PONE EN EL CANAL 0, Y ESTE PARRAFO DECIA LO
                //  CONTRARIO: «los 64 pads nacen en el canal 0». Lo hacian, y
                //  dejaron de hacerlo cuando se pidio que el proyecto empiece
                //  con los sesenta y cuatro SIN CANAL. Sin esta linea el envio
                //  se abre en un bus por el que no pasa ningun pad, no llega
                //  señal a ninguna capa y las 22 salen «un adorno»: 22 de 22
                //  FALLA con el codigo intacto. Es el mismo andamio que hizo
                //  falta en `StressTest` -`enCanalCero`- y por la misma causa.
                //
                //  Y aqui y no fuera: `lee` se llama cuatro veces por efecto y
                //  el reparto no se toca entre medias, asi que ponerlo donde
                //  se abre el envio deja las dos mitades juntas.
                engine.setPadCanal (0, 0);
                //  Y EL CANAL 0 SE VACIA ENTERO, no solo este tipo.
                //
                //  Esto ponia `canalSend[0][f] = 1` en cada vuelta y NUNCA
                //  limpiaba el de la vuelta anterior, asi que el canal 0 iba
                //  acumulando efectos. Daba igual mientras cada uno recibia su
                //  copia del pad; con la cadena en serie deja de darlo — al
                //  llegar a DLY el canal ya tenia un inserto delante y DLY no era
                //  el primer eslabon. El banco lo canto con `mudos [3, 5]`, y no
                //  era el visor: era el andamio midiendo con un canal sucio.
                for (int k = 0; k < AudioEngine::kNumFx; ++k)
                    engine.setCanalSend (0, k, 0.0f);
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
                {
                    double sp = 0.0, sq = 0.0;
                    for (int i = 0; i < kFxScopeBanco; ++i)
                    { sp += (double) pre[(size_t) i] * pre[(size_t) i];
                      sq += (double) post[(size_t) i] * post[(size_t) i]; }
                    diagPre  = std::sqrt (sp / kFxScopeBanco);
                    diagPost = std::sqrt (sq / kFxScopeBanco);
                    diagVivo = engine.fxScopeVivo() ? 1 : 0;
                }
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
            //  DOSCIENTOS TICS SON 6.6 s, y ochenta -2.6 s- se quedaban
            //  cortos en cuanto entro PNG: el banco saco «1 de 29 capas vivas
            //  se mueven sin señal» con `27=... curva 0.5649@47`, y la columna
            //  47 es la MAS VIEJA de la cola. No dibujaba ruido — dibujaba la
            //  cola de verdad del ping-pong, y el experimento lo confirmo:
            //  con `if (false && live (kFxPng))` salieron «29 de 29 quietos
            //  sin ella» y, a cambio, «1 de 29 no se mueven con señal».
            //
            //  El numero se deriva y no se elige. Son DOS tiempos en serie:
            //    - la cola del efecto, con la realimentacion de fabrica de PNG
            //      -0.45 cada 300 ms-: bajar del pico -que entra recortado a
            //      1.0 desde un RMS de 4.8- a los 0.01 del liston de `distinto`
            //      pide log(0.01/8)/log(0.45) = 8.4 repeticiones = 2.5 s;
            //    - y la CINTA del dibujo, que son `kPuntos` columnas a
            //      `kVentanaMs/(kPuntos-1)` = 42.6 ms, o sea 2.04 s mas, porque
            //      hasta que no sale por la derecha la ultima columna cargada
            //      el dibujo sigue moviendose aunque el audio ya este mudo.
            //  Son 4.6 s = 139 tics de 33 ms; 200 deja el margen.
            //
            //  Lo que se mide asi sigue siendo lo que importa — que se asiente
            //  y se PARE — que es ademas lo unico que impide que esto repinte
            //  para siempre, que es el fallo que `Tests/cpu.py` ya cazo en la
            //  curva del EQ.
            lee (false, 200);
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
            diagPre1 = diagPre; diagPost1 = diagPost; diagVivo1 = diagVivo;
            lee (false, 200);
            const auto otra = lee (false, 5);
            if (! distinto (callado, otra)) ++quietosSinSenal;
            else
            {
                //  Y CON LAS DOS LECTURAS AL LADO, que es lo que faltaba para
                //  no tener que adivinar: un indice solo dice QUE capa, y lo que
                //  hace falta saber es CUANTO se movio y en que eje.
                //  Y CON LAS DOS LECTURAS AL LADO, que es lo que faltaba para
                //  no tener que adivinar: el indice dice QUE capa y no CUANTO se
                //  movio ni en que eje. Va entrecomillado porque este array se
                //  emite crudo —era una lista de numeros— y sin comillas rompe
                //  el JSON entero: «la app no publico la linea del rack».
                float peor = 0.0f; int donde = -1;
                for (int i = 0; i < FxVisor::kPuntos; ++i)
                {
                    const float d = std::abs (callado.first[(size_t) i] - otra.first[(size_t) i]);
                    if (d > peor) { peor = d; donde = i; }
                }
                vivoRuido.add ("\"" + juce::String (f) + "="
                                 + juce::String (callado.second.x, 4) + ","
                                 + juce::String (callado.second.y, 4) + ","
                                 + juce::String (otra.second.x, 4) + ","
                                 + juce::String (otra.second.y, 4)
                                 + " curva " + juce::String (peor, 4) + "@" + juce::String (donde)
                                 + " son1 " + juce::String (diagPre1, 6) + "/" + juce::String (diagPost1, 6)
                                 + "/" + juce::String (diagVivo1)
                                 + " q " + juce::String (diagPre, 6) + "/" + juce::String (diagPost, 6)
                                 + "/" + juce::String (diagVivo) + "\"");
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
              //  Y LA TERCERA, QUE AHORA NO ES LA MISMA. `insertos` cuenta los
              //  que RESTAN SECO —18— y `porcanal` los que tienen UNO POR CANAL
              //  —21—. Eran el mismo numero mientras `fxSustituye` contestaba
              //  las dos preguntas; desde que CHO, FLA y PHA suman pero son de
              //  su canal, la formula de los buses usa esta y la fila del rack
              //  usa aquella. Con una sola, `rack.py` cuadraba una cuenta falsa.
              << ",\"porcanal\":"  << AudioEngine::numPorCanal()
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
    pulsaTapa (ranuraBtns[celdaDeFx (AudioEngine::kFxCmp)]);
    abreMenuRanura (1);
    pulsaTapa (ranuraBtns[celdaDeFx (AudioEngine::kFxLim)]);
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

    //  Y EL PAD 0 EN EL CANAL 0, que es la premisa que este andamio da por
    //  hecha mas abajo -«el pad 0 vive en el canal cero»- y que dejo de ser
    //  cierta cuando se pidio que el proyecto empiece con los sesenta y cuatro
    //  SIN tira. Sin esta linea el tono no llega a ningun bus, el limitador
    //  del canal 0 no ve una muestra y la prueba decia «quita 0.00 dB: no esta
    //  limitando» con el motor perfecto. Tercera vez en esta tanda que la
    //  misma premisa vieja suspende un andamio: ver `rack` y `StressTest`.
    engine.setPadCanal (0, 0);

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
    vaciaCancion();
    for (int b = 0; b < 4; ++b) ponBloqueCompas (0, b, 1);
    publicaBloques();

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
// ============================================================================
//  EL MIDI DEL PIANO ROLL. Ver Tests/midi.py.
// ============================================================================
//
//  NINGUNA DE LAS QUINCE REGLAS DE `expo.py` PUEDE VER NADA DE ESTO: un
//  importador que deja las notas una octava mas abajo, o media casilla tarde,
//  se maqueta perfecto -no solapa, no se sale, no corta un rotulo, no mide cero
//  y esta traducido-. Es la familia de los cinco fallos del compas del piano.
//
//  Y LA CIFRA QUE MANDA ES UNA IDENTIDAD: lo que sale y vuelve a entrar tiene
//  que ser EL MISMO patron, nota a nota. «Se exporta» lo cumple un fichero con
//  la raiz sola y «se importa» lo cumple uno que cae una octava abajo; solo la
//  vuelta entera dice que las dos mitades hablan el mismo idioma. Es la misma
//  figura que las dos filas que mas valen de `Tests/arr.py` -quitar el compas
//  que se acaba de insertar devuelve la cancion exacta- y la del troceado que
//  volvia siendo N copias.
void MainComponent::auditMidi()
{
    const int pat = 0;
    const int pad = 0;
    selectedPattern = pat;
    selectedPad = pad;
    engine.setPatternLength (pat, 16);

    //  UN PATRON QUE SE LEE DE UN VISTAZO, y con las cuatro cosas que un .mid
    //  puede perder: el semitono, la fuerza, el LARGO y el ACORDE. Sin el
    //  acorde, «vuelve igual» lo cumple un exportador que escribe la raiz sola
    //  -que es exactamente el fallo que ya costo una medida cuando un acorde
    //  volvia del proyecto siendo una nota-.
    //
    //  Y NINGUNO DE LOS CUATRO LARGOS ES EL CUATRO, que no es esquivar el caso
    //  sino que ese es la OTRA ortografia del cero -un paso- y las dos no
    //  pueden volver las dos: el fichero lleva una duracion y no dos formas de
    //  escribirla. Que un paso vuelve como cero lo mide la fila de abajo, que
    //  es donde esa eleccion vive.
    struct Puesta { int paso, semis, vel, largo; };
    const Puesta puestas[] = { { 0,   0, 100,  8 },
                               { 3,   7,  80,  2 },
                               { 8, -12, 120, 16 },
                               { 12, 24,  60,  0 } };
    for (const auto& p : puestas)
    {
        engine.setStep     (pat, p.paso, pad, true);
        engine.setStepNote (pat, p.paso, pad, p.semis);
        engine.setStepVel  (pat, p.paso, pad, p.vel);
        engine.setStepLen  (pat, p.paso, pad, p.largo);
        pattern[(size_t) pat][(size_t) p.paso][(size_t) pad] = true;
    }
    //  Un acorde en el paso 0: raiz 0 mas tercera, quinta y octava.
    const int extras[3] = { 4, 7, 12 };
    for (int i = 0; i < 3; ++i)
        engine.setStepExtra (pat, 0, pad, i, extras[i], true);

    const auto antes = notasDelPatron (pat, pad);

    //  EL FICHERO DE VERDAD Y POR EL CAMINO DE VERDAD: `exportaMidiPatron`
    //  escribe donde cae el rebote, que en el banco es la carpeta del HOME de
    //  esta corrida. Llamar a `MidiArchivo::escribe` por dentro se saltaria la
    //  mitad que puede fallar - de donde salen las notas y donde acaba.
    exportaMidiPatron();

    juce::File escrito;
    for (const auto& f : ProjectStore::exports().findChildFiles (juce::File::findFiles, false, "*.mid"))
        escrito = f;

    //  Y SE BORRA EL PAD A MANO ENTRE MEDIAS, que es lo unico que separa «se ha
    //  guardado» de «nadie lo quito». Es la misma linea que la prueba de la
    //  sesion y la del troceado.
    for (int s2 = 0; s2 < AudioEngine::kNumSteps; ++s2)
    {
        engine.vaciaPaso (pat, s2, pad);
        pattern[(size_t) pat][(size_t) s2][(size_t) pad] = false;
    }
    const int trasBorrar = (int) notasDelPatron (pat, pad).size();

    if (escrito.existsAsFile()) importaMidiPatron (escrito);
    const auto despues = notasDelPatron (pat, pad);

    //  Cuantas de las de antes vuelven EXACTAS: paso, semitono, fuerza y largo.
    //  Compararlas por cuenta seria decir que si a un importador que las pone
    //  todas en el paso cero.
    int iguales = 0;
    for (const auto& a : antes)
        for (const auto& b : despues)
            if (a.paso == b.paso && a.semis == b.semis && a.vel == b.vel && a.largo == b.largo)
                { ++iguales; break; }

    std::cout << "{\"midi\":\"vuelta\",\"antes\":" << (int) antes.size()
              << ",\"bytes\":" << (int) (escrito.existsAsFile() ? escrito.getSize() : 0)
              << ",\"tras_borrar\":" << trasBorrar
              << ",\"despues\":" << (int) despues.size()
              << ",\"iguales\":" << iguales << "}" << std::endl;

    // ------------------------------------------------------------------
    //  Y LO QUE NO CABE SE CUENTA Y SE DICE. Un fichero de fuera no tiene por
    //  que caber: notas a tres octavas del pad, compases mas alla del patron y
    //  una quinta voz en la misma columna. Una importacion que se come la mitad
    //  en silencio es la peor forma de funcionar: la que parece que funciona.
    //
    //  Se escribe un .mid A MANO -no por el exportador- porque lo que hay que
    //  medir es justo lo que esta maquina NO sabe escribir.
    {
        juce::MidiMessageSequence pista;
        auto pon = [&pista] (int nota, double tick, double dur)
        {
            pista.addEvent (juce::MidiMessage::noteOn  (1, nota, (juce::uint8) 100), tick);
            pista.addEvent (juce::MidiMessage::noteOff (1, nota), tick + dur);
        };
        const double tp = (double) MidiArchivo::kTicksPaso;
        pon (60, 0.0, tp);                 // cabe
        pon (100, tp, tp);                 // +40 semitonos: fuera del pad
        pon (20,  tp * 2, tp);             // -40: fuera por abajo
        pon (60,  tp * 200, tp);           // paso 200: mas alla del patron
        for (int i = 0; i < 6; ++i) pon (60 + i, tp * 4, tp);   // seis en la misma columna
        pista.updateMatchedPairs();
        pista.addEvent (juce::MidiMessage::endOfTrack(), pista.getEndTime() + 1.0);

        juce::MidiFile mf;
        mf.setTicksPerQuarterNote (MidiArchivo::kPpq);
        mf.addTrack (pista);

        const auto hostil = ProjectStore::exports().getChildFile ("hostil.mid");
        hostil.deleteFile();
        if (auto out = std::unique_ptr<juce::FileOutputStream> (hostil.createOutputStream()))
        {
            mf.writeTo (*out);
            out->flush();
        }

        MidiArchivo::Parte parte;
        const auto leidas = MidiArchivo::lee (hostil, AudioEngine::kNumSteps, parte);

        std::cout << "{\"midi\":\"hostil\",\"puestas\":" << parte.puestas
                  << ",\"fuera\":" << parte.fuera
                  << ",\"tarde\":" << parte.tarde
                  << ",\"apiladas\":" << parte.apiladas
                  << ",\"leidas\":" << (int) leidas.size() << "}" << std::endl;
    }

    // ------------------------------------------------------------------
    //  Y LAS DOS CONVENCIONES, medidas y no supuestas: que el do central caiga
    //  en el semitono cero y que una NEGRA ocupe cuatro pasos. Las dos son
    //  elecciones, asi que las dos tienen que poder fallar.
    {
        juce::MidiMessageSequence pista;
        //  Do central en el tick cero, y la siguiente una NEGRA despues.
        pista.addEvent (juce::MidiMessage::noteOn  (1, 60, (juce::uint8) 100), 0.0);
        pista.addEvent (juce::MidiMessage::noteOff (1, 60), (double) MidiArchivo::kPpq);
        pista.addEvent (juce::MidiMessage::noteOn  (1, 67, (juce::uint8) 100), (double) MidiArchivo::kPpq);
        pista.addEvent (juce::MidiMessage::noteOff (1, 67), (double) MidiArchivo::kPpq * 2.0);
        //  Y una de UN PASO, que es la que dice con que ortografia vuelve: cero
        //  y cuatro son el mismo sonido y el fichero solo puede llevar uno.
        pista.addEvent (juce::MidiMessage::noteOn  (1, 62, (juce::uint8) 100), (double) MidiArchivo::kPpq * 3.0);
        pista.addEvent (juce::MidiMessage::noteOff (1, 62), (double) MidiArchivo::kPpq * 3.0 + (double) MidiArchivo::kTicksPaso);
        pista.updateMatchedPairs();
        pista.addEvent (juce::MidiMessage::endOfTrack(), pista.getEndTime() + 1.0);

        juce::MidiFile mf;
        mf.setTicksPerQuarterNote (MidiArchivo::kPpq);
        mf.addTrack (pista);

        const auto conv = ProjectStore::exports().getChildFile ("convencion.mid");
        conv.deleteFile();
        if (auto out = std::unique_ptr<juce::FileOutputStream> (conv.createOutputStream()))
        {
            mf.writeTo (*out);
            out->flush();
        }

        MidiArchivo::Parte parte;
        const auto leidas = MidiArchivo::lee (conv, AudioEngine::kNumSteps, parte);
        const int semis0 = leidas.size() > 0 ? leidas[0].semis : -99;
        const int paso1  = leidas.size() > 1 ? leidas[1].paso  : -1;
        const int largo0 = leidas.size() > 0 ? leidas[0].largo : -1;
        const int largo2 = leidas.size() > 2 ? leidas[2].largo : -1;

        std::cout << "{\"midi\":\"convencion\",\"do_central\":" << semis0
                  << ",\"paso_de_la_negra\":" << paso1
                  << ",\"largo_de_una_negra\":" << largo0
                  << ",\"largo_de_un_paso\":" << largo2 << "}" << std::endl;
    }

    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

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
        //  Y SE ESPERA A QUE ABRA. Desde que grabar abre en el hilo abridor
        //  (Tribunal 2026-09, 4.3), la toma se arma al recoger la apertura y no
        //  al apretar; medir sin esperar medía el estado a medio arranque y
        //  sacaba las dos tomas en el mismo pad.
        esperaAbridor (15000);
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

//  ==========================================================================
//  LOS PRESETS DE CADA EFECTO. Ver Tests/presets.py y Source/FxPresets.h.
//  ==========================================================================
//
//  Esta funcion NO JUZGA NADA: rinde. Es la misma doctrina que `auditInstr` —
//  la ponderacion y los listones viven en Python, y escribirlos en C++ seria la
//  misma regla en dos sitios. Aqui se pone cada preset, se mide lo que sale y
//  se publica una linea por caso.
//
//  Y SE MIDE EN UN CANAL QUE NO ES EL CERO, que es la leccion que costo
//  dieciocho efectos mudos: el banco medía el camino de cada efecto en el canal
//  0 -que era justo el unico que funcionaba- y las dos medidas salian verdes
//  con el fallo dentro. Aqui el pad va al canal 4.
void MainComponent::auditFxPresets()
{
    constexpr int kCanal = 4;
    constexpr int kBloque = 128;
    //  SEISCIENTOS OCHENTA MILISEGUNDOS, y la cifra viene de una medida y no
    //  de un numero redondo: con 24 bloques -64 ms- salian SIETE presets
    //  «mudos» y cuatro de ellos eran los del delay, con sus tiempos en 90,
    //  250, 375 y 500 ms. El eco no llegaba dentro de la ventana. La ventana
    //  tiene que cubrir el parametro mas largo que un preset pone, que es el
    //  medio segundo de DLY LARGO, y con margen para su cola.
    constexpr int kBloques = 256;

    engine.prepareToPlay (48000.0, kBloque);

    //  UN RUIDO Y NO UN TONO: un seno deja fuera casi todo el espectro, y
    //  entonces «este preset no cambia nada» diria mas de donde cayo el tono
    //  que del preset. Es la misma razon por la que la capa viva del rack se
    //  mide con ruido.
    auto ruido = []
    {
        auto* sb = new SampleBuffer();
        const int n = 24000;
        sb->buffer.setSize (2, n);
        juce::Random r (20260917);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
                sb->buffer.setSample (c, i, 0.5f * (r.nextFloat() * 2.0f - 1.0f));
        sb->sourceSampleRate = 48000.0;
        return SampleBuffer::Ptr (sb);
    };

    closeAllSheets();
    engine.setPadCanal (0, kCanal);
    engine.setPadGain  (0, 1.0f);
    engine.publishSample (0, ruido());

    //  Rendir una tirada entera con el efecto puesto y devolver lo que sale.
    auto rinde = [&] (juce::AudioBuffer<float>& out)
    {
        engine.postPanic();
        out.setSize (2, kBloque * kBloques, false, false, true);
        out.clear();
        //  Un golpe y luego se deja correr: lo que se compara es la MISMA
        //  entrada pasada por dos ajustes, asi que el disparo va una vez y en
        //  el mismo sitio de las dos tiradas.
        engine.postNoteOn (0, 1.0f);
        juce::AudioBuffer<float> b (2, kBloque);
        for (int i = 0; i < kBloques; ++i)
        {
            b.clear();
            engine.renderNextBlock (b, 0, kBloque);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, i * kBloque, b, ch, 0, kBloque);
        }
    };

    juce::AudioBuffer<float> seco, mojado;

    for (int f = 0; f < kNumFx; ++f)
    {
        //  EL CANAL SE VACIA ENTERO EN CADA VUELTA. Con la cadena en serie, no
        //  limpiar el tipo anterior deja al de ahora con un inserto delante y
        //  entonces no mide lo que dice medir. Es la figura de `enCanalCero`:
        //  *una prueba que arrastra estado mide otra cosa.*
        for (int k = 0; k < kNumRanuras; ++k) slotFx[(size_t) kCanal][(size_t) k] = kSlotVacia;
        for (int k = 0; k < kNumFx; ++k) engine.setCanalSend (kCanal, k, 0.0f);

        ponCanalActual (kCanal);
        ponEnRanura (0, f);
        engine.setCanalSend (kCanal, f, 1.0f);

        //  LA REFERENCIA ES EL EFECTO EN NEUTRO, no el pad sin efecto: asi la
        //  cifra dice «este preset cambia el audio» y no «el efecto esta
        //  puesto», que lo cumpliria tambien un preset identico al de al lado.
        escribeFxParam (f, 2, 0.0f);
        rinde (seco);

        for (int k = 0; k < FxPresets::cuantos(); ++k)
        {
            aplicaFxPreset (f, k);
            rinde (mojado);

            //  DOS CIFRAS Y NO UNA: cuantas muestras cambian y cuanto. Solo
            //  «cambian» lo cumple un preset que mete un ruido de un bit, y
            //  solo «cuanto» lo cumple uno que desplaza el nivel entero sin
            //  tocar la forma.
            int distintas = 0;
            double e1 = 0.0, e2 = 0.0;
            const int n = juce::jmin (seco.getNumSamples(), mojado.getNumSamples());
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                {
                    const float a = seco.getSample (ch, i), b = mojado.getSample (ch, i);
                    if (std::abs (a - b) > 1.0e-5f) ++distintas;
                    e1 += (double) a * a;
                    e2 += (double) b * b;
                }

            juce::StringArray p;
            for (int pi = 0; pi < kParamsPorFx; ++pi)
                p.add (juce::String (engine.getFxParam (kCanal, f, pi), 6));

            juce::String bandas;
            if (f == kFxEq)
            {
                juce::StringArray e;
                for (int b = 0; b < Eq5::kBands; ++b)
                    e.add (juce::String (engine.getEqFreq (kCanal, b), 1) + ":"
                             + juce::String (engine.getEqGain (kCanal, b), 2) + ":"
                             + juce::String (engine.getEqTipo (kCanal, b)) + ":"
                             + juce::String (engine.getEqQ (kCanal, b), 2));
                bandas = e.joinIntoString (";");
            }

            std::cout << "{\"preset\":1,\"fx\":" << f
                      << ",\"tipo\":\"" << fxDefs[f].name << "\""
                      << ",\"k\":" << k
                      << ",\"nombre\":\"" << FxPresets::nombre (f, k) << "\""
                      << ",\"p\":[" << p.joinIntoString (",") << "]"
                      << ",\"bandas\":\"" << bandas << "\""
                      << ",\"distintas\":" << distintas
                      << ",\"muestras\":" << (n * 2)
                      << ",\"rms_seco\":" << juce::String (std::sqrt (e1 / juce::jmax (1, n * 2)), 6)
                      << ",\"rms_fx\":" << juce::String (std::sqrt (e2 / juce::jmax (1, n * 2)), 6)
                      << "}" << std::endl;
        }
    }

    //  LA FILA DE FABRICA DEL MOTOR, para que Python pueda comprobar que el
    //  preset cero ES `kFxDef` y no una tercera tabla de defectos.
    for (int f = 0; f < kNumFx; ++f)
    {
        juce::StringArray d;
        for (int pi = 0; pi < 3; ++pi) d.add (juce::String (AudioEngine::kFxDef[f][pi], 6));
        std::cout << "{\"kfxdef\":1,\"fx\":" << f
                  << ",\"d\":[" << d.joinIntoString (",") << "]}" << std::endl;
    }

    //  Y LOS RANGOS QUE LA CARA DECLARA, que es contra lo que un preset tiene
    //  que caber. No se copian a Python: se publican.
    for (int f = 0; f < kNumFx; ++f)
        for (int pi = 0; pi < 3; ++pi)
            std::cout << "{\"spec\":1,\"fx\":" << f << ",\"p\":" << pi
                      << ",\"lo\":" << juce::String (fxDefs[f].spec[pi].lo, 6)
                      << ",\"hi\":" << juce::String (fxDefs[f].spec[pi].hi, 6)
                      << "}" << std::endl;

    //  GUARDAR UNO TUYO, Y CON UN NOMBRE QUE INTENTA SALIRSE.
    //
    //  `../fuera` es el caso que `ProjectStore::componente` existe para parar:
    //  `getChildFile` resuelve `..` subiendo un nivel, y un preset llamado asi
    //  escribiria en la biblioteca y no en su carpeta. Lo que se publica no es
    //  lo que devolvio la orden sino DONDE ACABO EL FICHERO.
    {
        ponCanalActual (kCanal);
        ponEnRanura (0, AudioEngine::kFxDly);
        aplicaFxPreset (AudioEngine::kFxDly, 2);

        //  PRIMERO LA MARCA, Y SOBRE UN PRESET DE FABRICA.
        //
        //  Esto se medía despues de guardar uno tuyo, y guardar pone la casilla
        //  en MOVIDO por su cuenta: la cifra salia -1 con el codigo roto Y con
        //  el codigo bueno. *Una prueba que nunca se ha visto fallar no es una
        //  prueba.* Ahora se mide con el preset 2 puesto, que es un indice de
        //  verdad, y mover el mando tiene que convertirlo en -1.
        const int puestoAntes = fxPresetPuesto[(size_t) kCanal][(size_t) AudioEngine::kFxDly];
        {
            const auto& sp0 = fxDefs[AudioEngine::kFxDly].spec[0];
            const double hoy = engine.getFxParam (kCanal, AudioEngine::kFxDly, 0);
            const double lejos0 = (hoy - sp0.lo) > (sp0.hi - hoy) ? sp0.lo : sp0.hi;
            fxParam (AudioEngine::kFxDly, 0).setValue (lejos0, juce::dontSendNotification);
            escribeFxParam (AudioEngine::kFxDly, 0, (float) lejos0);
        }
        const int marcaTrasMando = fxPresetPuesto[(size_t) kCanal][(size_t) AudioEngine::kFxDly];
        aplicaFxPreset (AudioEngine::kFxDly, 2);

        const bool ok = guardaFxPresetTuyo (AudioEngine::kFxDly, "MI ECO");
        const auto dir = carpetaFxPresets (AudioEngine::kFxDly);
        const auto mio = dir.getChildFile ("MI ECO.txt");

        const bool okFuera = guardaFxPresetTuyo (AudioEngine::kFxDly, "../fuera");
        int fueraDeSitio = 0;
        for (const auto& e : juce::RangedDirectoryIterator (ProjectStore::presets(), true, "*.txt",
                                                            juce::File::findFiles))
            if (! e.getFile().isAChildOf (dir) && e.getFile().getFileName().contains ("fuera"))
                ++fueraDeSitio;

        //  Y LA VUELTA: se mueve un mando, se relee el fichero y tienen que
        //  volver los cuatro numeros. Se mueve al extremo MAS LEJANO del valor
        //  de hoy, que es el cuarto truco de `auditInstr`: mover a un tope
        //  escrito sale verde si ya estaba en el tope.
        const double antes = engine.getFxParam (kCanal, AudioEngine::kFxDly, 0);
        const auto& sp = fxDefs[AudioEngine::kFxDly].spec[0];
        const double lejos = (antes - sp.lo) > (sp.hi - antes) ? sp.lo : sp.hi;
        fxParam (AudioEngine::kFxDly, 0).setValue (lejos, juce::dontSendNotification);
        escribeFxParam (AudioEngine::kFxDly, 0, (float) lejos);
        const int movido = fxPresetPuesto[(size_t) kCanal][(size_t) AudioEngine::kFxDly];

        aplicaFxPresetTuyo (AudioEngine::kFxDly, "MI ECO");
        const double vuelve = engine.getFxParam (kCanal, AudioEngine::kFxDly, 0);

        std::cout << "{\"tuyo\":1,\"guardado\":" << (ok ? 1 : 0)
                  << ",\"existe\":" << (mio.existsAsFile() ? 1 : 0)
                  << ",\"dentro\":" << (mio.isAChildOf (ProjectStore::presets()) ? 1 : 0)
                  << ",\"fuera_ok\":" << (okFuera ? 1 : 0)
                  << ",\"fuera_de_sitio\":" << fueraDeSitio
                  << ",\"puesto_antes\":" << puestoAntes
                  << ",\"marca_tras_mando\":" << marcaTrasMando
                  << ",\"movido\":" << movido
                  << ",\"antes\":" << juce::String (antes, 6)
                  << ",\"lejos\":" << juce::String (lejos, 6)
                  << ",\"vuelve\":" << juce::String (vuelve, 6)
                  << ",\"nombre\":\"" << fxPresetNombre (AudioEngine::kFxDly) << "\"}" << std::endl;
    }

    //  ========================================================================
    //  LA FICHA DE PRESETS: LA PUERTA Y LO QUE SE VE EN ELLA.
    //  ========================================================================
    //
    //  Del telefono, con la ficha del RACK delante: *«hay que mejorar el tema
    //  de los presets para los efectos, porque no esta muy accesible o legible
    //  que digamos»*. Son DOS cosas y se miden las dos, porque arreglar una no
    //  ensena la otra:
    //
    //    · ACCESIBLE es que haya puerta donde estas -mantener el canalon del
    //      rack- y que NO la haya donde no significa nada, que es una ranura
    //      vacia;
    //    · LEGIBLE es que la celda diga algo mas que un nombre. «TELEFONO» no
    //      dice cuanto cierra el filtro, y para saberlo habia que ponerlo y
    //      oirlo, perdiendo por el camino lo que tenias puesto.
    //
    //  Y LA SEGUNDA SE MIDE POR LA CURVA Y NO POR «HAY UN DIBUJO»: seis celdas
    //  con el MISMO dibujo son seis celdas que no informan, que es justo lo que
    //  pasaria si alguien las alimentara del motor en vez del preset. Se cuenta
    //  cuantas de las seis son distintas entre si.
    {
        closeAllSheets();
        ponCanalActual (kCanal);

        //  LA PUERTA, POR EL GESTO Y NO POR LA FUNCION.
        //
        //  Se llama al `onHold` de la tapa -que es lo que el temporizador de
        //  `HoldButton` dispara con el dedo puesto- y no a `abreMenuPresets`:
        //  lo que se mide es el CABLE, y llamar a la funcion de destino saldria
        //  verde con la tapa sin cable. El temporizador en si es de JUCE y lo
        //  mide JUCE; aqui no hay bucle de mensajes que lo deje correr.
        for (int k = 0; k < kNumRanuras; ++k) slotFx[(size_t) kCanal][(size_t) k] = kSlotVacia;
        refrescaRanuras();

        auto* canalon = rackSlotBtns.isEmpty() ? nullptr : rackSlotBtns[0];
        const int cable = (canalon != nullptr && canalon->onHold != nullptr) ? 1 : 0;

        //  PRIMERO SOBRE LA RANURA VACIA, que es la mitad que un cable pelado
        //  cumpliria igual: abrir la ficha de presets del efecto -1.
        if (cable != 0) canalon->onHold();
        const int abreVacia = presetSheet.isVisible() ? 1 : 0;
        abreMenuPresets (-1);

        //  Y AHORA CON UN EFECTO PUESTO, y uno que NO es el que la cara tiene
        //  enfocado por defecto: si coincidieran, «abre los presets de ESTA
        //  ranura» y «abre los del efecto de siempre» darian el mismo numero.
        ponEnRanura (0, AudioEngine::kFxDly);
        refrescaRanuras();
        if (cable != 0) canalon->onHold();
        const int abrePuesta = presetSheet.isVisible() ? 1 : 0;
        const int editado    = presetEditado;
        resized();

        //  Y LAS CELDAS, con la ficha ya colocada: cuantas tienen curva y de
        //  que tamano. Una curva de 0x0 pasa las ocho reglas de geometria.
        int conCurva = 0, curvaCero = 0;
        for (int i = 0; i < FxPresets::kPresets && i < presetCurvas.size(); ++i)
            if (auto* v = presetCurvas[i])
            {
                if (v->tipo() >= 0 && ! v->getBounds().isEmpty()) ++conCurva;
                if (v->isVisible() && v->getBounds().isEmpty() && v->tipo() >= 0) ++curvaCero;
            }

        std::cout << "{\"pficha\":1,\"cable\":" << cable
                  << ",\"abre_vacia\":" << abreVacia
                  << ",\"abre_puesta\":" << abrePuesta
                  << ",\"editado\":" << editado
                  << ",\"dly\":" << (int) AudioEngine::kFxDly
                  << ",\"con_curva\":" << conCurva
                  << ",\"curva_cero\":" << curvaCero
                  << ",\"celdas\":" << FxPresets::kPresets << "}" << std::endl;

        abreMenuPresets (-1);

        //  Y LA CUENTA DE CURVAS DISTINTAS, tipo por tipo.
        for (int f = 0; f < kNumFx; ++f)
        {
            ponCanalActual (kCanal);
            abreMenuPresets (f);
            refrescaMenuPresets();

            //  La curva de cada preset, muestreada por la misma funcion que
            //  dibuja la de la cara. Dos presets con la misma curva son dos
            //  celdas que dicen lo mismo.
            std::vector<FxVisor::Curva> vistas;
            int dibujadas = 0;
            for (int i = 0; i < FxPresets::kPresets && i < presetCurvas.size(); ++i)
            {
                auto* v = presetCurvas[i];
                if (v == nullptr || v->tipo() < 0) continue;
                ++dibujadas;
                vistas.push_back (v->puntos());
            }

            int distintas = 0;
            for (size_t a = 0; a < vistas.size(); ++a)
            {
                bool repetida = false;
                for (size_t b = 0; b < a && ! repetida; ++b)
                    repetida = (vistas[a] == vistas[b]);
                if (! repetida) ++distintas;
            }

            //  Y QUE MANDOS MUEVEN SU DIBUJO, que es lo que convierte esta
            //  cifra en un veredicto.
            //
            //  Seis celdas con la misma curva son seis celdas que no informan
            //  —y es lo que saldria si alguien las alimentara del motor en vez
            //  del preset— pero «distintas == 6» NO es la regla: hay visores
            //  que declaran, con su razon escrita, que uno de los dos mandos
            //  no cabe en su eje. RNG es el caso: su FREQ es un tiempo y la
            //  ventana se mide en periodos, asi que GRAVE, METAL y CAMPANA
            //  —que solo se diferencian en FREQ— dibujan lo mismo y TIENEN que
            //  dibujar lo mismo. Lo que se publica es la materia prima; la
            //  cuenta la hace Python con los valores de los presets, que ya
            //  tiene. *El banco no adivina lo que la app puede decir.*
            const auto mm = FxVisor::mandosDe (f);
            std::cout << "{\"pcurva\":1,\"fx\":" << f
                      << ",\"tipo\":\"" << fxDefs[f].name << "\""
                      << ",\"m0\":" << (mm.p0 ? 1 : 0)
                      << ",\"m1\":" << (mm.p1 ? 1 : 0)
                      << ",\"cara\":" << (fxTraeCara (f) ? 1 : 0)
                      << ",\"dibujadas\":" << dibujadas
                      << ",\"distintas\":" << distintas
                      << ",\"celdas\":" << FxPresets::kPresets << "}" << std::endl;

            abreMenuPresets (-1);
        }
    }

    //  ========================================================================
    //  LA FILA DEL RACK: EL PRESET SE VE, SE CAMBIA, Y LA LUZ NO MIENTE
    //  ========================================================================
    //
    //  Del telefono, con la foto del rack delante y por partes:
    //
    //    · *«ahi falta un cuadrado o un visor en el que tu puedas cambiar el
    //      preset sin tener que entrar al propio efecto»* — o sea que la fila
    //      DIGA que preset lleva y se pueda tocar. Mantener el canalon ya
    //      abria la rejilla desde la tanda anterior y un gesto que no se ve no
    //      lo encuentra nadie.
    //    · *«el boton de encender y apagar, que a veces se peta y no se
    //      mantiene en negro»* — y eso no era un pintado raro: `setFxEnabled`
    //      encendia UNA de las tres ventanas de una ranura.
    //
    //  Las dos se miden aqui porque las dos viven en la misma fila, y la
    //  segunda solo se ve DESDE el rack: la tapa de la cara si se encendia.
    {
        closeAllSheets();
        ponCanalActual (kCanal);
        for (int k = 0; k < kNumRanuras; ++k) slotFx[(size_t) kCanal][(size_t) k] = kSlotVacia;
        ponEnRanura (0, AudioEngine::kFxDly);
        refrescaRanuras();
        openSheet (rackSheet, mixButton);
        refreshRack();
        resized();

        const int dly = AudioEngine::kFxDly;
        auto* pb    = rackPresetBtns.isEmpty()      ? nullptr : rackPresetBtns[0];
        auto* pbVac = rackPresetBtns.size() > 1     ? rackPresetBtns[1] : nullptr;

        //  EL CABLE, y los dos gestos: el toque pasa al siguiente y el
        //  mantener abre la rejilla. Medir la funcion de destino saldria verde
        //  con la tapa sin cable, que es el fallo que esta regla existe para
        //  cazar.
        const int cable = (pb != nullptr && pb->onClick != nullptr && pb->onHold != nullptr) ? 1 : 0;
        const int seVe  = (pb != nullptr && pb->isVisible() && ! pb->getBounds().isEmpty()) ? 1 : 0;
        const int anchoP = pb != nullptr ? pb->getWidth()  : 0;
        const int altoP  = pb != nullptr ? pb->getHeight() : 0;
        //  Y LA DE UNA RANURA VACIA, APAGADA Y VACIADA: las dos cosas, que
        //  encendida y de 0x0 pasa las ocho reglas de geometria sin rozarlas.
        const int vacApagada = (pbVac != nullptr && ! pbVac->isVisible()) ? 1 : 0;
        const int vacVaciada = (pbVac != nullptr && pbVac->getBounds().isEmpty()) ? 1 : 0;
        const int diceNombre = (pb != nullptr
                                  && pb->getButtonText() == fxPresetNombre (dly)) ? 1 : 0;

        //  EL PASEO: tantos toques como presets hay tienen que recorrerlos
        //  todos y volver al primero. Se cuenta paso a paso y no solo la
        //  vuelta, que quedarse quieto en el cero tambien vuelve al cero.
        aplicaFxPreset (dly, 0);
        int pasos = 0;
        for (int i = 0; i < FxPresets::cuantos(); ++i)
        {
            if (cable != 0) pb->onClick();
            if (fxPresetPuesto[(size_t) kCanal][(size_t) dly] == (i + 1) % FxPresets::cuantos())
                ++pasos;
        }
        const int vuelve = (fxPresetPuesto[(size_t) kCanal][(size_t) dly] == 0) ? 1 : 0;

        //  Y EL TOQUE SOBRE UNA RANURA VACIA NO MUEVE NADA, que es la mitad
        //  que un cable pelado cumpliria igual.
        const int antes = fxPresetPuesto[(size_t) kCanal][(size_t) dly];
        if (pbVac != nullptr && pbVac->onClick != nullptr) pbVac->onClick();
        const int quieto = (fxPresetPuesto[(size_t) kCanal][(size_t) dly] == antes) ? 1 : 0;

        //  LA LUZ DE LA FILA, CUATRO VECES: encender, apagar, encender,
        //  apagar. Una sola pasada no distingue «no se entera» de «se entero
        //  al reves», y el fallo del telefono era intermitente porque lo
        //  curaba cualquier cosa que volviera a abrir el rack.
        auto* mute = rackMuteBtns.isEmpty()  ? nullptr : rackMuteBtns[0];
        auto* can  = rackSlotBtns.isEmpty()  ? nullptr : rackSlotBtns[0];
        int luzOk = 0, luzMal = 0;
        for (int t = 0; t < 4; ++t)
        {
            if (mute != nullptr && mute->onClick != nullptr) mute->onClick();
            const bool suena = fxEncendido (dly);
            const bool dm = (mute != nullptr && mute->getToggleState());
            const bool dc = (can  != nullptr && can->getToggleState());
            if (dm == suena && dc == suena) ++luzOk; else ++luzMal;
        }

        std::cout << "{\"prack\":1,\"cable\":" << cable
                  << ",\"se_ve\":" << seVe
                  << ",\"ancho\":" << anchoP
                  << ",\"alto\":" << altoP
                  << ",\"dedo\":" << Metrics::hit
                  << ",\"dice_nombre\":" << diceNombre
                  << ",\"vac_apagada\":" << vacApagada
                  << ",\"vac_vaciada\":" << vacVaciada
                  << ",\"pasos\":" << pasos
                  << ",\"presets\":" << FxPresets::cuantos()
                  << ",\"vuelve\":" << vuelve
                  << ",\"quieto\":" << quieto
                  << ",\"luz_ok\":" << luzOk
                  << ",\"luz_mal\":" << luzMal
                  << ",\"luces\":4}" << std::endl;

        closeAllSheets();
    }
}

//  LAS DOS SELECCIONES DE RANGO: la del secuenciador y la de la cancion.
//
//  Se miden JUNTAS y en una sola corrida porque son la misma funcion en dos
//  lienzos, y separarlas en dos pruebas cuesta dos arranques del binario para
//  medir el mismo contrato: banda, cuatro acciones, portapapeles relativo.
//
//  Lo que cada mitad aporta y la otra no:
//
//   - En SEC, los NUEVE campos del paso. Es la leccion que esta casa pago dos
//     veces -COPIAR PATRON se llevaba TRES de los nueve, DESPLAZAR y DOBLAR
//     cuatro- y la unica forma de cazarla es escribir un paso con los nueve
//     fuera de su valor por defecto, pegarlo EN OTRO PATRON y volver a leerlo.
//     Pegar en el mismo patron no vale: con el original debajo, una copia que
//     no se lleve un campo lo encuentra ya puesto y la prueba sale verde.
//
//   - En la CANCION, el `offset`. Una banda que corta un bloque por la mitad
//     tiene que dejar el trozo sonando por donde iba: un bloque de 64 pasos
//     cortado en el 24 sale como bloque de 24 con offset 0 y deja detras uno
//     de 40 con offset 24. Sin `offset` los dos empezarian por el paso 0 del
//     patron, que es la mitad del fallo que el modelo de bloques vino a
//     arreglar y que la rejilla de celdas no puede ni representar.
void MainComponent::auditSelecciones()
{
    auto nueve = [this] (int pat, int st, int pad)
    {
        const auto p = engine.leePaso (pat, st, pad);
        std::cout << "[" << (p.on ? 1 : 0) << "," << (int) p.nota << "," << (int) p.empujon
                  << "," << (int) p.corte << "," << (int) p.vel << "," << (int) p.roll
                  << "," << (int) p.largo << "," << (juce::int64) p.acorde
                  << "," << (juce::int64) p.bloqueos << "]";
    };

    //  ---- SEC ----------------------------------------------------------
    //
    //  Un paso con los NUEVE campos fuera de su defecto, y en el pad 2 / paso
    //  3 para que el pegado caiga dentro de una banda que empieza en 0: si el
    //  golpe estuviera en el filo, un recorte de un paso pasaria desapercibido.
    selectedPattern = 0;
    currentBank     = 0;
    engine.setPatternLength (0, 16);
    engine.setPatternLength (1, 16);
    for (int st = 0; st < 16; ++st)
        for (int pad = 0; pad < 8; ++pad)
        {
            pattern[0][(size_t) st][(size_t) pad] = false;
            pattern[1][(size_t) st][(size_t) pad] = false;
            engine.setStep (0, st, pad, false);
            engine.setStep (1, st, pad, false);
            engine.vaciaPaso (0, st, pad);
            engine.vaciaPaso (1, st, pad);
        }

    {
        AudioEngine::Paso p;
        p.on       = true;
        p.nota     = 5;
        p.empujon  = 3;
        p.corte    = 40;
        p.vel      = 51;      // 0.4 de 127, que es la cifra con la que se pidio
        p.roll     = 4;
        p.largo    = 40;      // diez pasos: no cabia en el uint8 de antes
        p.acorde   = 0;
        p.bloqueos = 0;
        engine.escribePaso (0, 3, 2, p);
        engine.setStepExtra (0, 3, 2, 0, 4, true);
        engine.setStepExtra (0, 3, 2, 1, 7, true);
        pattern[0][3][2] = true;
        //  Y un segundo golpe FUERA de la banda, para que BORRAR tenga algo
        //  que NO tocar: una regla que solo mira lo que se borra no distingue
        //  «borra el rango» de «borra el patron».
        engine.setStep (0, 12, 2, true);
        pattern[0][12][2] = true;
        //  Y un tercero DENTRO y en otro pad: con un solo golpe copiado, un
        //  portapapeles que se quedara con el ultimo en vez de con todos daria
        //  la misma cifra que uno correcto.
        engine.setStep (0, 6, 0, true);
        pattern[0][6][0] = true;
    }
    seqPrimerCelda = 0;
    refreshStepGrid();

    std::cout << "{\"sel\":\"sec original\",\"paso\":";
    nueve (0, 3, 2);
    std::cout << "}" << std::endl;

    //  La banda: tres pads x ocho casillas. Con la rejilla en 1/16 sobre un
    //  patron de 16, una casilla es un paso.
    seqBanda (0, 0, 2, 8);
    seqCopiaSel();
    std::cout << "{\"sel\":\"sec copiado\",\"entradas\":" << (int) seqPortapapeles.size()
              << ",\"pads\":" << seqPegPads << ",\"pasos\":" << seqPegPasos << "}" << std::endl;

    //  PEGAR EN OTRO PATRON, que es lo que la peticion pedia y lo unico que
    //  demuestra que los nueve campos viajan de verdad.
    selectedPattern = 1;
    seqPrimerCelda  = 0;
    refreshStepGrid();
    seqPegaSel();
    std::cout << "{\"sel\":\"sec pegado\",\"paso\":";
    nueve (1, 3, 2);
    std::cout << "}" << std::endl;

    //  BORRAR la banda del patron original: dentro se va todo -incluida la
    //  cola de los nueve campos, no solo el «suena»- y fuera se queda el
    //  golpe del paso 12.
    selectedPattern = 0;
    refreshStepGrid();
    seqBanda (0, 0, 2, 8);
    seqBorraSel();
    {
        int dentro = 0, fuera = 0;
        for (int st = 0; st < 16; ++st)
            for (int pad = 0; pad < 3; ++pad)
                if (engine.leePaso (0, st, pad).on) (st < 8 ? dentro : fuera)++;
        std::cout << "{\"sel\":\"sec borrado\",\"dentro\":" << dentro << ",\"fuera\":" << fuera
                  << ",\"resto\":";
        nueve (0, 3, 2);
        std::cout << "}" << std::endl;
    }

    //  ---- CANCION ------------------------------------------------------
    const int pc = juce::jmax (1, engine.pasosPorCompas());

    vaciaCancion();
    engine.setSongLength (8);
    //  Un bloque de CUATRO compases en el carril 0 -mas largo que la banda,
    //  que es la unica forma de que la banda lo CORTE- y otro en el carril 1
    //  que la banda no toca: si los dos cayeran dentro, «recorta por el filo»
    //  y «se lleva el bloque entero» darian la misma lista.
    ponBloqueCompas (0, 0, 1, 4);
    ponBloqueCompas (1, 2, 2);
    std::cout << "{\"sel\":\"cancion inicial\",\"pc\":" << pc << ",\"bloques\":[";
    {
        bool primero = true;
        for (const auto& b : bloques)
        {
            std::cout << (primero ? "" : ",") << "[" << b.lane << "," << b.bank << ","
                      << (b.compas * pc + b.paso) << "," << b.largo << "," << b.offset << "]";
            primero = false;
        }
    }
    std::cout << "]}" << std::endl;

    //  Y UN CLIP DE AUDIO QUE ASOMA POR EL FILO, que es la otra mitad de la
    //  banda y no la miraba nadie.
    //
    //  `songCopiaSel` recorta los clips «con la misma cuenta de muestras que
    //  `parteClip`» y esa frase estaba escrita en el codigo sin una sola regla
    //  detras: un recorte que se llevara el clip entero, o que empezara por el
    //  principio del fichero en vez de por donde lo corta la banda, pasaba las
    //  trece reglas de `sel.py` y las trece de `clips.py`.
    //
    //  Y SE MIDEN LOS DOS FILOS CON UN SOLO CLIP, que es lo unico que separa
    //  «recorta» de «recorta bien». El clip empieza en el paso 0 y dura 30
    //  pasos; la banda va del 8 al 24, o sea SOBRA por los dos lados: 8 pasos
    //  por delante y 6 por detras.
    //
    //  Dos cifras y ninguna sobra. El LARGO tiene que volver 16 -y no 30, ni
    //  22, que es lo que sale recortando un solo filo- y el `desde` tiene que
    //  AVANZAR ocho pasos de muestras: un recorte que mueva la cabeza del clip
    //  sin mover por donde entra al fichero deja la toma sonando ocho pasos
    //  antes de lo que se ve, que es el mismo fallo de «el clip suena donde no
    //  se dibuja» en un sitio nuevo. Y se pregunta en MUESTRAS, que es como el
    //  clip guarda su desfase: en pasos la cuenta se redondea y el error de
    //  media muestra por paso se esconde.
    //
    //  Va aqui y no en `clips.py` porque la banda vive aqui: medirlo alli
    //  seria arrancar el binario otra vez para juzgar el mismo contrato.
    const double porPaso = engine.muestrasPorCompas() / (double) pc;
    clips.clear();
    {
        ClipUI c;
        c.pad = 0; c.pista = 1; c.compas = 0; c.paso = 0;
        c.desde = 0; c.largo = (int) (30.0 * porPaso);
        c.gain = 1.0f;
        clips.push_back (c);
    }
    publicaClips();

    songBanda (0, 8, 1, 24);
    songCopiaSel();
    std::cout << "{\"sel\":\"cancion clip\",\"por paso\":" << juce::String (porPaso, 2)
              << ",\"antes pasos\":" << juce::String (30.0, 1)
              << ",\"banda pasos\":" << songPortapapeles.pasos
              << ",\"clips\":[";
    {
        bool primero = true;
        for (const auto& k : songPortapapeles.clips)
        {
            std::cout << (primero ? "" : ",") << "[" << k.pista << ","
                      << (k.compas * pc + k.paso) << ","
                      << juce::String ((double) k.largo / porPaso, 2) << ","
                      << k.desde << "]";
            primero = false;
        }
    }
    std::cout << "]}" << std::endl;

    //  Y la banda de verdad para las cuatro reglas de abajo: dos carriles x los
    //  24 primeros pasos, o sea MEDIO bloque y un poco. Es exactamente «copiar
    //  medio patron», y se vuelve a marcar porque la de arriba empezaba en el 8.
    songBanda (0, 0, 1, 24);
    songCopiaSel();
    std::cout << "{\"sel\":\"cancion copiado\",\"carriles\":" << songPortapapeles.carriles
              << ",\"pasos\":" << songPortapapeles.pasos << ",\"bloques\":[";
    {
        bool primero = true;
        for (const auto& b : songPortapapeles.bloques)
        {
            std::cout << (primero ? "" : ",") << "[" << b.lane << "," << b.bank << ","
                      << (b.compas * pc + b.paso) << "," << b.largo << "," << b.offset << "]";
            primero = false;
        }
    }
    std::cout << "]}" << std::endl;

    //  PEGAR en el compas 5, paso 8: un sitio que no es multiplo de compas,
    //  que es la mitad que el modelo viejo no podia ni escribir.
    songMarcaLane = 0;
    songMarcaPaso = 5 * pc + 8;
    songPegaSel();
    std::cout << "{\"sel\":\"cancion pegado\",\"bloques\":[";
    {
        bool primero = true;
        for (const auto& b : bloques)
        {
            std::cout << (primero ? "" : ",") << "[" << b.lane << "," << b.bank << ","
                      << (b.compas * pc + b.paso) << "," << b.largo << "," << b.offset << "]";
            primero = false;
        }
    }
    std::cout << "]}" << std::endl;

    //  Y BORRAR la banda: el bloque del carril 0 se PARTE por el filo y deja
    //  vivo lo que asomaba, con su offset corrido. Lo que se borra entero se
    //  confunde con lo que se parte si solo se cuenta cuantos quedan, asi que
    //  se imprime la lista.
    songBanda (0, 0, 1, 24);
    songBorraSel();
    std::cout << "{\"sel\":\"cancion borrado\",\"bloques\":[";
    {
        bool primero = true;
        for (const auto& b : bloques)
        {
            std::cout << (primero ? "" : ",") << "[" << b.lane << "," << b.bank << ","
                      << (b.compas * pc + b.paso) << "," << b.largo << "," << b.offset << "]";
            primero = false;
        }
    }
    std::cout << "]}" << std::endl;

    //  Y CORTE, QUE TIENE QUE SER UNA SOLA ENTRADA DE DESHACER.
    //
    //  CORTE es COPIAR mas BORRAR, y esta escrito llamando a los dos. Si
    //  BORRAR metiera su propio pushUndo, deshacer una vez dejaria el trozo
    //  cortado a medias -copiado pero no borrado- y harian falta dos. Es la
    //  misma cuenta que el piano ya mide: una accion, una entrada.
    const int undoAntes = (int) undoStack.size();
    songBanda (0, 88, 1, 104);
    songCortaSel();
    std::cout << "{\"sel\":\"cancion corte\",\"undo\":" << ((int) undoStack.size() - undoAntes)
              << ",\"bloques\":" << (int) bloques.size() << "}" << std::endl;
}

// ==========================================================================
//  LA DECISION DEL CARRIL RAPIDO, MEDIDA SOBRE TABLAS SINTETICAS.
//
//  `AudioPath::probeFastPath` vive tras `#if JUCE_ANDROID` y habla con
//  libaaudio, asi que en un escritorio no corre y NUNCA ha podido fallar - y
//  una regla que no puede fallar no es una regla, es una linea que imprime OK.
//  Se partio en dos: el contacto con AAudio, que solo lo comprueba un telefono
//  y se declara como limite, y `AudioPath::concluye`, que es pura y es la que
//  decide que `usage` se le pasa a JUCE y si se le fuerza el formato de 16
//  bits. Eso es lo que se mide aqui, con las cinco tablas que importan.
//
//  La quinta no estaba en el plan y es la regla que esta tanda ANADE: un flujo
//  que abre y no arranca no es un carril. Dejarla sin medir seria repetir el
//  fallo que la tanda viene a arreglar.
//
//  Ver Tests/audio.py.
// ==========================================================================
void MainComponent::auditAudio()
{
    using AudioPath::Intento;

    //  Un intento que abrio y arranco, con los terminos de un movil normal.
    auto ok = [] (int usage, int pidio, bool excl, bool mmap, bool i16) -> Intento
    {
        Intento t;
        t.usage = usage;  t.pidio = pidio;
        t.abrio = true;   t.arranco = true;
        t.exclusiva = excl; t.mmap = mmap; t.baja = true; t.i16 = i16;
        t.burst = excl ? 96 : 192;
        t.capacity = t.burst * 2;
        t.canales = 2;    t.rate = 48000;
        return t;
    };

    auto nada = [] (int usage, int pidio, int error) -> Intento
    {
        Intento t;
        t.usage = usage;  t.pidio = pidio;  t.error = error;
        return t;
    };

    //  Abre y NO arranca: AAudio concedio el constructor y nego el START, que
    //  es donde de verdad se compromete el MMAP.
    auto muerto = [] (int usage, int pidio, bool excl) -> Intento
    {
        Intento t;
        t.usage = usage;  t.pidio = pidio;
        t.abrio = true;   t.arranco = false;
        t.exclusiva = excl; t.mmap = excl; t.baja = true;
        t.burst = 96;  t.canales = 2;  t.rate = 48000;  t.error = -895;
        return t;
    };

    auto fila = [] (const char* que, const std::array<Intento, AudioPath::kIntentos>& tabla,
                    int n, bool mmapKnown)
    {
        const auto f = AudioPath::concluye (tabla, n, mmapKnown);

        //  Y LOS DOS NUMEROS QUE DE VERDAD SALEN DE AQUI, calculados igual que
        //  en el constructor: son los que el parche de JUCE lee antes de abrir
        //  el flujo de verdad, y por tanto lo unico de esta funcion que cambia
        //  el sonido de la maquina.
        const int oboeUsage = f.exclusive ? f.usage : 0;
        const int oboeI16   = (f.exclusive && f.useI16) ? 1 : 0;

        std::cout << "{\"audio\":\"" << que
                  << "\",\"ran\":"   << (f.ran ? 1 : 0)
                  << ",\"excl\":"    << (f.exclusive ? 1 : 0)
                  << ",\"mmap\":"    << (f.mmapUsed ? 1 : 0)
                  << ",\"gano\":"    << f.gano
                  << ",\"burst\":"   << f.burst
                  << ",\"rate\":"    << f.rate
                  << ",\"usage\":"   << oboeUsage
                  << ",\"i16\":"     << oboeI16
                  << ",\"texto\":\"" << UiAudit::esc (AudioPath::describe (f))
                  << "\"}" << std::endl;
    };

    const int kGame  = AudioPath::kUsageGame;
    const int kMedia = AudioPath::kUsageMedia;

    //  1. NINGUNO ABRE. libaaudio esta, los seis dan error: no hay respuesta y
    //     no se le toca nada a JUCE.
    {
        std::array<Intento, AudioPath::kIntentos> t {};
        t[0] = nada (0,      0, -895);
        t[1] = nada (kGame,  0, -895);
        t[2] = nada (kGame,  2, -895);
        t[3] = nada (kGame,  1, -895);
        t[4] = nada (kMedia, 2, -895);
        t[5] = nada (kMedia, 1, -895);
        fila ("ninguno abre", t, 6, true);
    }

    //  2. EL PRIMERO ABRE EN COMPARTIDO, y ninguno mejora. Es el caso del
    //     telefono de la bitacora: `via compartida MEZCLADOR`.
    {
        std::array<Intento, AudioPath::kIntentos> t {};
        t[0] = ok (0,      0, false, false, false);
        t[1] = ok (kGame,  0, false, false, false);
        t[2] = ok (kGame,  2, false, false, false);
        t[3] = ok (kGame,  1, false, false, true);
        t[4] = ok (kMedia, 2, false, false, false);
        t[5] = ok (kMedia, 1, false, false, true);
        fila ("compartido siempre", t, 6, true);
    }

    //  3. EL TERCERO DA EXCLUSIVA en float con GAME. Es el reparto que el
    //     comentario de la sonda describe: MEDIA cae en el posproceso del
    //     fabricante y GAME lo esquiva.
    {
        std::array<Intento, AudioPath::kIntentos> t {};
        t[0] = ok (0,     0, false, false, false);
        t[1] = ok (kGame, 0, false, false, false);
        t[2] = ok (kGame, 2, true,  true,  false);
        fila ("game float exclusiva", t, 3, true);
    }

    //  4. EXCLUSIVA SOLO EN 16 BITS. El cuarto intento es el unico que la
    //     consigue, y con el va el `forceI16` que salta la tentativa float de
    //     JUCE. Sin esta tabla, ese 1 no lo comprueba nadie.
    {
        std::array<Intento, AudioPath::kIntentos> t {};
        t[0] = ok (0,     0, false, false, false);
        t[1] = ok (kGame, 0, false, false, false);
        t[2] = ok (kGame, 2, false, false, false);
        t[3] = ok (kGame, 1, true,  true,  true);
        fila ("game 16b exclusiva", t, 4, true);
    }

    //  5. ABRE EXCLUSIVA Y NO ARRANCA, y otro arranca compartido. Gana el que
    //     arranco: leer el modo de reparto sobre un flujo que nunca llego a
    //     START es leer una intencion, y publicarla como exclusiva le manda a
    //     JUCE una `usage` que no ha ganado nada.
    {
        std::array<Intento, AudioPath::kIntentos> t {};
        t[0] = nada   (0,      0, -895);
        t[1] = muerto (kGame,  0, true);
        t[2] = ok     (kGame,  2, false, true, false);
        fila ("abre exclusiva y no arranca", t, 3, true);
    }

    //  6. Y SIN EL SIMBOLO OCULTO: compartido, y no decimos saber si hay MMAP.
    {
        std::array<Intento, AudioPath::kIntentos> t {};
        t[0] = ok (0, 0, false, true, false);
        fila ("sin isMMapUsed", t, 1, false);
    }

    //  Y EL TAMANO DE UN CUADRO, que es lo que provoco el ANR.
    //
    //  El callback de la sonda se pasaba de largo el bufer de AAudio porque
    //  calculaba los bytes con un booleano -«no es de 16 bits» = «son 4 bytes»-
    //  y hay dos formatos concedibles que no miden cuatro. Ese callback no lo
    //  puede correr esta maquina; esta cuenta SI, asi que se publica formato a
    //  formato y `Tests/audio.py` la mide. Los cinco valores, y el desconocido,
    //  que tiene que dar CERO para que el callback no escriba nada.
    for (int f = 0; f <= 5; ++f)
        std::cout << "{\"bytes\":" << f
                  << ",\"por\":"  << AudioPath::bytesPorMuestra (f)
                  << "}" << std::endl;

    //  Y LO QUE DE VERDAD LE LLEGA A OBOE AL ARRANCAR. Las tablas de arriba
    //  prueban la decision; esto prueba que el constructor no la toma: ni
    //  sonda al arrancar ni ajuste de usage o formato sobre el flujo de verdad.
    std::cout << "{\"arranque_audio\":1,\"sonda\":" << (kSondaAlArrancar ? 1 : 0)
              << ",\"usage\":" << zatiOboeUsage
              << ",\"i16\":" << zatiOboeForceI16 << "}" << std::endl;
}

// ============================================================================
//  LO QUE CUESTA GUARDAR EL ESTADO, Y EN QUE HILO SE PAGA. Ver Tests/atasco.py.
//
//  La app se quedo «no responde» con el audio SONANDO -medidor a -11 dB, forma
//  de onda viva, 94 BPM- o sea con el hilo de audio intacto y el de mensajes
//  parado. Eso descarta el dispositivo y senala a lo unico que este hilo hace
//  a solas y sin tope: guardar.
//
//  Y guardar el estado no es escribir un fichero. Es, en este orden y todo
//  seguido: construir el arbol entero, COPIARLO, serializarlo a XML,
//  escribirlo, VOLVERLO A LEER DE DISCO Y PARSEARLO para comprobar que no se
//  trunco, y renombrar. Mas, en `autosave`, una SEGUNDA serializacion del mismo
//  arbol para `project.xml`. Cada veinte segundos, en el hilo que atiende el
//  dedo.
//
//  Esta sonda pone la app con trabajo dentro y le pone numero a cada tramo,
//  porque la cifra que decide es el reparto y no el total: si la parte que
//  obliga a estar en el hilo de mensajes -leer la interfaz- es el 5 % y el
//  resto es serializar y disco, entonces el resto se puede ir a otro hilo y
//  esto se arregla. Si fuera al reves, no.
// ============================================================================
void MainComponent::auditEstado()
{
    const auto cronometro = [] { return juce::Time::getMillisecondCounterHiRes(); };

    //  LA FABRICA, que es el primer arranque de cualquiera: 64 pads
    //  sintetizados. Se mide ANTES de llenar nada, que es el orden en el que la
    //  app la corre.
    //
    //  Y SE MIDE EL TROZO MAS LARGO, no el total. Lo que cuelga una app de
    //  Android no es que una tarea dure tres segundos: es que el hilo de
    //  mensajes se pase tres segundos sin volver al bucle. Con la fabrica
    //  rindiendo en su hebra, el reloj de pared es el mismo de antes y lo que
    //  cambia -que es lo unico que Android mira- es que ningun tramo de este
    //  hilo pase de unos milisegundos. Se publican los dos: `ms` es el peor
    //  tramo y `reloj` lo que tardo entera.
    {
        const double t0 = cronometro();
        double peor = 0.0;

        const double tA = cronometro();
        loadFactoryKits();
        peor = juce::jmax (peor, cronometro() - tA);

        const auto tope = juce::Time::getMillisecondCounter() + 60000u;
        while ((fabricaJob != nullptr || ! fabricaCola.empty())
               && juce::Time::getMillisecondCounter() < tope)
        {
            const double tB = cronometro();
            stepFabricaJob();
            peor = juce::jmax (peor, cronometro() - tB);
            if (fabricaJob != nullptr) juce::Thread::sleep (2);
        }

        std::cout << "{\"atasco\":\"op\",\"que\":\"fabrica\",\"ms\":"
                  << juce::roundToInt (peor)
                  << ",\"reloj\":" << juce::roundToInt (cronometro() - t0)
                  << "}" << std::endl;
    }

    llenaDePrueba();

    //  Y EL MAQUETADO ENTERO, que es lo que corre en cada giro de pantalla.
    {
        const double t = cronometro();
        resized();
        std::cout << "{\"atasco\":\"op\",\"que\":\"maqueta\",\"ms\":"
                  << juce::roundToInt (cronometro() - t) << "}" << std::endl;
    }

    const auto reloj = cronometro;

    const double t0 = reloj();
    const auto estado = captureState();
    const double msCaptura = reloj() - t0;

    const double t1 = reloj();
    const auto texto = estado.toXmlString();
    const double msXml = reloj() - t1;

    //  La copia que `writeState` hace por dentro, medida aparte: es el unico
    //  tramo que tendria que quedarse en el hilo de mensajes si el resto se va,
    //  porque es lo que convierte el arbol compartido en uno que otro hilo
    //  puede leer sin carreras.
    const double t2 = reloj();
    auto copia = estado.createCopy();
    const double msCopia = reloj() - t2;
    juce::ignoreUnused (copia);

    const double t3 = reloj();
    session.writeState (estado, currentProject);
    const double msEscribe = reloj() - t3;

    //  Y EL PARSEO DE VUELTA A SOLAS, que es el tramo que nadie sospecha: la
    //  comprobacion de que el fichero no salio truncado cuesta leer y parsear
    //  otra vez el XML entero.
    const double t4 = reloj();
    const auto vuelta = juce::parseXML (SessionKeeper::stateFile());
    const double msParseo = reloj() - t4;

    //  Y EL GUARDADO COMPLETO tal y como la app lo hace: `autosave` es lo que
    //  corre en `onPause` y lo que el temporizador acaba llamando.
    const double t5 = reloj();
    autosave();
    const double msAuto = reloj() - t5;

    std::cout << "{\"estado\":\"coste\""
              << ",\"bytes\":"   << (int) texto.getNumBytesAsUTF8()
              << ",\"captura\":" << juce::roundToInt (msCaptura)
              << ",\"xml\":"     << juce::roundToInt (msXml)
              << ",\"copia\":"   << juce::roundToInt (msCopia)
              << ",\"escribe\":" << juce::roundToInt (msEscribe)
              << ",\"parseo\":"  << juce::roundToInt (msParseo)
              << ",\"auto\":"    << juce::roundToInt (msAuto)
              << ",\"vuelve\":"  << (vuelta != nullptr ? 1 : 0)
              << "}" << std::endl;

    //  Y las mismas cifras en la forma que `Tests/atasco.py` juzga: una linea
    //  por operacion del hilo de mensajes, con su presupuesto comun.
    std::cout << "{\"atasco\":\"op\",\"que\":\"captura\",\"ms\":"
              << juce::roundToInt (msCaptura) << "}" << std::endl;
    std::cout << "{\"atasco\":\"op\",\"que\":\"guarda\",\"ms\":"
              << juce::roundToInt (msAuto) << "}" << std::endl;

    const auto mide = [&cronometro] (const char* que, auto&& fn)
    {
        const double t = cronometro();
        fn();
        std::cout << "{\"atasco\":\"op\",\"que\":\"" << que << "\",\"ms\":"
                  << juce::roundToInt (cronometro() - t) << "}" << std::endl;
    };

    //  Y EL RESTO DE LO QUE UNA PERSONA APRIETA Y ESTE HILO HACE ENTERO.
    //  Un banco de fabrica solo -la tapa FABRICA de la ficha de pads-, la
    //  carcasa, el idioma, y guardar y abrir el proyecto que se acaba de
    //  llenar. Son las operaciones sin trocear que quedan.
    mide ("carcasa", [this] { applySkin(); });
    mide ("idioma",  [this] { retranslateUi(); });
    mide ("guardap", [this] { saveProject ("atasco"); });
    mide ("abrep",   [this] { loadProject ("atasco"); });

    //  Y DESHACER, que es lo que `auditTapas` encontro y esta lista escrita a
    //  mano no tenia. Las tres van juntas y en este orden porque deshacer sin
    //  pila no hace nada -`if (undoStack.empty()) return;`- y una medida de
    //  cero seria una medida en verde de una operacion que no corrio.
    mide ("guardaund", [this] { pushUndo ("atasco"); });
    mide ("deshacer",  [this] { performUndo(); });
    mide ("rehacer",   [this] { performRedo(); });
}

// ============================================================================
//  TODO LO QUE SE PUEDE APRETAR, CRONOMETRADO UNA TAPA CADA VEZ.
//
//  `auditEstado` mide una lista ESCRITA A MANO: la fabrica, maquetar, capturar,
//  guardar, la carcasa, el idioma, guardar y abrir el proyecto. Con esa lista
//  se encontro el ANR de la tanda anterior -la fabrica bloqueaba el hilo de
//  mensajes 1691 ms- y con esa lista no se puede encontrar el siguiente, porque
//  una lista a mano solo cubre lo que alguien se acordo de apuntar. La app
//  tiene del orden de seiscientas tapas y mandos, y CUALQUIERA de ellos puede
//  colgar este hilo: eso es exactamente lo que le paso a la fabrica durante
//  siete tandas sin que ninguna prueba lo viera.
//
//  Asi que esto no enumera: abre las fichas una por una, recorre el arbol
//  entero, y aprieta lo que este VISIBLE y ENCENDIDO con el reloj al lado. Una
//  linea por control, con el mismo `{"atasco":"op"}` que ya juzga
//  `Tests/atasco.py`, para que el presupuesto de 250 ms este escrito UNA vez.
//
//  Tres cuidados que la hacen fiable y sin los cuales mediria mentiras:
//
//    1. Se aprieta con `pulsaTapa` y NUNCA con `triggerClick()`, que es
//       `postCommandMessage`: el mensaje no se entrega hasta que esta funcion
//       entera termina, o sea que el reloj mediria cero y el banco diria que
//       ninguna tapa cuesta nada. Es el fallo que el fuzz llevaba encima.
//    2. Cada control se toma por `SafePointer`. Apretar una tapa puede DESTRUIR
//       a sus hermanas -cambiar de pagina rehace la fila entera- y un
//       `juce::Array<juce::Button*>` recogido antes se queda con punteros
//       colgando. Lo que se salta no es una tapa: es el proceso.
//    3. Y se vuelve a la ficha DESPUES de cada apriete, porque la mitad de las
//       tapas navegan. Sin esto, la primera que cierre la ficha deja a las
//       siguientes apretando sobre una pantalla que ya no es la que se dijo, y
//       el nombre que sale en la linea no es el del control que se midio.
// ============================================================================
void MainComponent::auditTapas()
{
    const auto cronometro = [] { return juce::Time::getMillisecondCounterHiRes(); };

    //  Y SE PUEDE PEDIR UN PUNADO DE FICHAS: `ZATI_TAPAS=inst,rack`. La corrida
    //  entera son diez minutos -2442 apretadas, cada una reabriendo su ficha- y
    //  eso esta bien para el banco y esta mal para perseguir una cifra, que es
    //  medir, cambiar una linea y volver a medir. Con `1` van todas, que es lo
    //  que `Tests/atasco.py` pide.
    juce::StringArray soloEstas;
    if (const auto q = UiAudit::env ("ZATI_TAPAS"); q.isNotEmpty() && q != "1")
        soloEstas.addTokens (q, ",", "");

    //  LA APP CON TRABAJO DENTRO, que es la unica forma de que las cifras
    //  signifiquen algo: una tapa de la mesa sobre 64 pads vacios no toca nada.
    loadFactoryKits();
    esperaFabrica();
    llenaDePrueba();

    //  Las fichas, por el mismo nombre con el que el resto del banco las pide.
    //  La cadena vacia es LA CARA, que es donde estan el transporte, los cuatro
    //  bancos y las dieciseis tapas de pad.
    static const char* kFichas[] = {
        "", "pads", "pad2", "pad3", "sec", "paso", "secp", "song", "songm",
        "piano", "pianod", "mix", "mixc", "canal", "xy", "eq", "eqb", "plato",
        "set", "asp", "lang", "proj", "gest", "midi", "midf", "rack", "rackf",
        "ranura", "ranural", "preset", "preseteq", "inst", "instg", "instd",
        "chop", "expo", "manual", "tour", "pick"
    };

    int apretadas = 0, saltadas = 0;
    double peorMs = 0.0;
    juce::String peorQuien;

    for (const char* ficha : kFichas)
    {
        const juce::String nombreFicha = (*ficha == 0 ? juce::String ("cara")
                                                      : juce::String (ficha));
        if (! soloEstas.isEmpty() && ! soloEstas.contains (nombreFicha)) continue;

        //  Se recogen los controles UNA vez para saber CUANTOS hay y en que
        //  orden, y despues se vuelve a recorrer en cada vuelta para coger el
        //  que toca. Recoger punteros crudos y guardarlos entre apretadas es
        //  justo lo que el cuidado 2 prohibe.
        const auto cuenta = [this, ficha]
        {
            auditOpen (ficha);
            int n = 0;
            std::function<void (juce::Component&)> mira = [&] (juce::Component& c)
            {
                if (! c.isVisible()) return;
                if (auto* b = dynamic_cast<juce::Button*> (&c)) { if (tapaDeBanco (*b) && b->isEnabled()) ++n; }
                else if (dynamic_cast<juce::Slider*> (&c) != nullptr)                                     ++n;
                for (auto* k : c.getChildren()) mira (*k);
            };
            mira (*this);
            return n;
        }();

        for (int i = 0; i < cuenta; ++i)
        {
            //  De vuelta a la ficha, y a por el i-esimo control de este
            //  recorrido. El arbol puede haber cambiado entre apretadas, asi
            //  que el indice puede caer en otro control o en ninguno; las dos
            //  cosas son correctas y la segunda se cuenta como saltada.
            auditOpen (ficha);

            juce::Component::SafePointer<juce::Button> tapa;
            juce::Component::SafePointer<juce::Slider> mando;
            juce::String quien;
            int visto = 0;

            std::function<void (juce::Component&)> busca = [&] (juce::Component& c)
            {
                if (! c.isVisible() || quien.isNotEmpty()) return;

                if (auto* b = dynamic_cast<juce::Button*> (&c))
                {
                    if (tapaDeBanco (*b) && b->isEnabled())
                    {
                        if (visto == i)
                        {
                            tapa  = b;
                            quien = b->getButtonText().isNotEmpty() ? b->getButtonText()
                                                                   : b->getName();
                        }
                        ++visto;
                    }
                }
                else if (auto* s = dynamic_cast<juce::Slider*> (&c))
                {
                    if (visto == i)
                    {
                        mando = s;
                        quien = s->getName().isNotEmpty() ? s->getName() + "=" : juce::String ("mando=");
                    }
                    ++visto;
                }

                for (auto* k : c.getChildren()) busca (*k);
            };
            busca (*this);

            if (quien.isEmpty()) { ++saltadas; continue; }

            //  Y CON ETIQUETA EN LA CAJA NEGRA, que es la otra mitad de la
            //  medida: si una tapa cuelga esto de verdad, el vigilante escribe
            //  QUIEN mientras el atasco dura y no despues. Ver Bitacora::Tarea.
            const Bitacora::Tarea marca ("banco/tapa");

            const double t = cronometro();
            if (tapa != nullptr)
            {
                pulsaTapa (tapa.getComponent());
            }
            else if (mando != nullptr)
            {
                //  Al medio de su recorrido y no a un extremo: los extremos son
                //  donde un mando suele tener el camino corto -silencio, cero,
                //  apagado- y lo que cuesta esta en el resto.
                const auto lo = mando->getMinimum(), hi = mando->getMaximum();
                mando->setValue (lo + (hi - lo) * 0.5, juce::sendNotificationSync);
            }
            const double ms = cronometro() - t;

            ++apretadas;
            if (ms > peorMs) { peorMs = ms; peorQuien = nombreFicha + "/" + quien; }

            std::cout << "{\"atasco\":\"op\",\"que\":\"" << UiAudit::esc (nombreFicha + "/" + quien)
                      << "\",\"ms\":" << juce::roundToInt (ms)
                      << ",\"tapa\":1}" << std::endl;
        }
    }

    //  Y EL RECUENTO, que es lo que impide que esta prueba se apruebe midiendo
    //  nada. Una regla que recorre un arbol vacio -porque las fichas dejaron de
    //  abrirse, porque `tapaDeBanco` se volvio falso para todas- sale en VERDE
    //  con cero lineas, que es la forma mas barata que hay de vaciar una medida
    //  sin que se note. `Tests/atasco.py` exige un minimo.
    std::cout << "{\"tapas\":\"total\",\"apretadas\":" << apretadas
              << ",\"saltadas\":" << saltadas
              << ",\"fichas\":" << (int) juce::numElementsInArray (kFichas)
              << ",\"peor\":" << juce::roundToInt (peorMs)
              << ",\"peor_en\":\"" << UiAudit::esc (peorQuien) << "\"}" << std::endl;
}

//  UN DISPOSITIVO DE BANCO QUE CUENTA COMO CUENTA OBOE.
//
//  En este banco no hay tarjeta de sonido -`dispositivo 0`, medido- asi que
//  sin esto la prueba de abajo contestaba cero aperturas y cero consultas, que
//  es exactamente lo que contestaria el codigo roto. Este falso copia de Oboe
//  las tres cosas que importan y nada mas:
//
//    - `getAvailableBufferSizes` y `getDefaultBufferSize` son PREGUNTAS AL
//      DRIVER: en Oboe cada una abre un flujo exclusivo temporal. Se cuentan.
//    - el bufer por defecto es el de 40 ms de JUCE y la rafaga es 192, asi que
//      `useLowestLatency` tiene algo que cambiar, como en un telefono.
//    - `open` se cuenta, y se le puede decir que falle.
namespace
{
    int bancoAperturas = 0, bancoPreguntas = 0;
    bool bancoFalla = false;
    //  Lo que tarda CADA apertura, dentro del driver: asi paga igual quien
    //  abre desde el hilo de mensajes que quien abre desde el suyo, y la
    //  medida distingue los dos (Tribunal 2026-09, 4.1 y 4.3).
    int bancoOpenLentoMs = 0;

    struct BancoDevice final : juce::AudioIODevice
    {
        BancoDevice() : juce::AudioIODevice ("BANCO SALIDA", "BANCO") {}
        juce::StringArray getOutputChannelNames() override { return { "L", "R" }; }
        juce::StringArray getInputChannelNames() override  { return {}; }
        juce::Array<double> getAvailableSampleRates() override { return { 44100.0, 48000.0 }; }
        juce::Array<int> getAvailableBufferSizes() override
        {
            ++bancoPreguntas;
            return { 192, 384, 576, 768, 960, 1152, 1920 };
        }
        int getDefaultBufferSize() override { ++bancoPreguntas; return 1920; }
        juce::String open (const juce::BigInteger&, const juce::BigInteger& outs,
                           double sr, int buf) override
        {
            ++bancoAperturas;
            if (bancoOpenLentoMs > 0) juce::Thread::sleep (bancoOpenLentoMs);
            if (bancoFalla) return "banco: el HAL no abre";
            rate = sr > 0.0 ? sr : 48000.0;
            block = buf > 0 ? buf : 1920;
            salidas = outs;
            abierto = true;
            return {};
        }
        void close() override { stop(); abierto = false; }
        bool isOpen() override { return abierto; }
        void start (juce::AudioIODeviceCallback* cb) override
        {
            if (cb != nullptr && abierto) { cb->audioDeviceAboutToStart (this); quien = cb; }
        }
        void stop() override
        {
            if (auto* cb = std::exchange (quien, nullptr)) cb->audioDeviceStopped();
        }
        bool isPlaying() override { return quien != nullptr; }
        juce::String getLastError() override { return {}; }
        int getCurrentBufferSizeSamples() override { return block; }
        double getCurrentSampleRate() override { return rate; }
        int getCurrentBitDepth() override { return 32; }
        juce::BigInteger getActiveOutputChannels() const override { return salidas; }
        juce::BigInteger getActiveInputChannels() const override { return {}; }
        int getOutputLatencyInSamples() override { return block; }
        int getInputLatencyInSamples() override { return 0; }

        double rate = 48000.0;
        int block = 1920;
        bool abierto = false;
        juce::BigInteger salidas;
        juce::AudioIODeviceCallback* quien = nullptr;
    };

    struct BancoType final : juce::AudioIODeviceType
    {
        BancoType() : juce::AudioIODeviceType ("BANCO") {}
        void scanForDevices() override {}
        juce::StringArray getDeviceNames (bool entrada) const override
        {
            return entrada ? juce::StringArray() : juce::StringArray ("BANCO SALIDA");
        }
        int getDefaultDeviceIndex (bool) const override { return 0; }
        int getIndexOfDevice (juce::AudioIODevice* d, bool entrada) const override
        {
            return (d != nullptr && ! entrada) ? 0 : -1;
        }
        bool hasSeparateInputsAndOutputs() const override { return true; }
        juce::AudioIODevice* createDevice (const juce::String& salida, const juce::String&) override
        {
            return salida.isEmpty() || salida == "BANCO SALIDA" ? new BancoDevice() : nullptr;
        }
    };
}

//  ABRIR EL DISPOSITIVO SIN COLGAR EL HILO DE MENSAJES.
//
//  El «no responde» volvio con la caja negra diciendo «reanudada» y nada mas:
//  la app murio dentro de `appResumed`, que abria el dispositivo dos veces
//  seguidas, el vigilante de silencio lo repetia cada segundo sin mirar lo que
//  costaba, y pintar AJUSTES · AUDIO preguntaba al driver en cada repintado.
//  Oboe no existe en este banco, asi que lo que se mide no es el tiempo del
//  telefono sino lo que lo multiplicaba, que si es del codigo:
//
//    preguntas_pintar  preguntas al driver -flujos temporales en Oboe- al
//                      pintar sesenta veces la pagina y rehacer sus tapas dos
//    aperturas_volver  aperturas del dispositivo al volver del fondo
//    intentos_60s      aperturas en sesenta segundos contra un HAL muerto
//    vigilante_suelto  que la caja negra vuelva a mirar al reanudar
void MainComponent::auditRevive()
{
    //  El falso entra como un tipo mas y se elige, igual que el telefono elige
    //  Oboe. Y como en el constructor: se abre y se ajusta a la rafaga.
    //  La apertura del primer tick, si aun va por el hilo que abre, se deja
    //  acabar: esta prueba cambia el tipo de dispositivo por debajo.
    esperaAbridor (15000);
    recogeApertura();
    deviceManager.addAudioDeviceType (std::make_unique<BancoType>());
    deviceManager.setCurrentAudioDeviceType ("BANCO", true);
    setAudioChannels (0, 2);
    useLowestLatency();
    auto* dev = deviceManager.getCurrentAudioDevice();
    const bool hay = dev != nullptr && dev->getTypeName() == "BANCO";
    const int bloque = dev != nullptr ? dev->getCurrentBufferSizeSamples() : 0;

    //  1. Pintar la pagina de audio sesenta veces y rehacer sus tapas dos,
    //     que es lo que hace el telefono mientras la miras. Desde una cache
    //     vacia, para que la primera pregunta -la unica legitima- se vea.
    buferesClave.clear();
    buferesCache.clear();
    bancoPreguntas = 0;
    {
        juce::Image lienzo (juce::Image::ARGB, 480, 360, true);
        for (int i = 0; i < 60; ++i)
        {
            juce::Graphics g (lienzo);
            paintAudioInfo (g, lienzo.getBounds());
        }
    }
    refreshAudioOptions();
    refreshAudioOptions();
    const int preguntasPintar = bancoPreguntas;

    //  2. Irse al fondo y volver, por el camino de verdad. Y con el
    //     vigilante como lo deja el congelador de Android: el ultimo latido de
    //     hace diez minutos y el aviso ya gastado en ese hueco falso. Si al
    //     volver sigue asi, un atasco dentro de `appResumed` no lo apunta
    //     nadie, que es la captura: «reanudada» y detras nada.
    appSuspended();
    Bitacora::latido.store (juce::Time::getMillisecondCounter() - 600000u);
    Bitacora::avisado.store (true);
    bancoAperturas = 0;
    bancoPreguntas = 0;
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    appResumed();
    esperaAbridor (15000);
    recogeApertura();
    const bool vigilanteSuelto = ! Bitacora::avisado.load()
        && (juce::Time::getMillisecondCounter() - Bitacora::latido.load()) < 1000u;
    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    const int aperturasVolver = bancoAperturas;
    const int preguntasVolver = bancoPreguntas;
    auto* d2 = deviceManager.getCurrentAudioDevice();
    const int bloqueVuelta = d2 != nullptr ? d2->getCurrentBufferSizeSamples() : 0;

    //  3. Un HAL que no abre, sesenta segundos de reloj a pasos de 50 ms por
    //     el vigilante de verdad. Se cuentan las aperturas intentadas.
    shutdownAudio();
    deviceManager.closeAudioDevice();
    bancoFalla = true;
    bancoAperturas = 0;
    deviceRevivalTicks = 0.0;
    esperaRevivirMs = kReviveMinMs;
    for (int t = 0; t < 60000; t += 50)
    {
        reviveSalida (50.0);
        esperaAbridor (15000);
    }
    recogeApertura();
    const int intentosMuerto = bancoAperturas;

    //  ...y cuando el HAL vuelve, el vigilante lo recoge en su siguiente
    //  espera y la espera vuelve al minimo.
    bancoFalla = false;
    bancoAperturas = 0;
    for (int t = 0; t < 20000 && deviceManager.getCurrentAudioDevice() == nullptr; t += 50)
    {
        reviveSalida (50.0);
        esperaAbridor (15000);
        recogeApertura();
    }
    const bool recupera = deviceManager.getCurrentAudioDevice() != nullptr;
    const double esperaTras = esperaRevivirMs;

    //  4. UN SERVIDOR DE AUDIO QUE TARDA SEIS SEGUNDOS EN CONTESTAR, que es el
    //     cartel: el dispositivo cerrado, la app delante y el vigilante
    //     pidiendo. Lo que se mide es lo que el hilo de mensajes pasa DENTRO
    //     de la peticion -volver del fondo, que es donde se pide- y que la
    //     apertura acabe llegando igual.
    shutdownAudio();
    deviceManager.closeAudioDevice();
    bancoLentoMs = 6000;
    const double tl = juce::Time::getMillisecondCounterHiRes();
    appResumed();
    const double msLento = juce::Time::getMillisecondCounterHiRes() - tl;
    const bool abreTras = esperaAbridor (15000) && deviceManager.getCurrentAudioDevice() != nullptr;
    recogeApertura();
    bancoLentoMs = 0;

    //  4b. OTRA APP SE QUEDA EL ALTAVOZ PARA SIEMPRE, con la app delante: la
    //      cortina con el PLAY de otra app, o la pantalla partida. Tres
    //      segundos de vigilante no pueden reabrir -eso seria sonar encima de
    //      quien lo cogio- y PLAY si, porque es la persona pidiendo sonido
    //      (Tribunal 2026-09, 5.2). Antes: nada la sacaba del silencio.
    int focoRespeta = -1, focoVuelve = -1;
    {
        appInForeground = true;
        audioFocusLost (true);
        for (int t = 0; t < 3000; t += 50)
        {
            reviveSalida (50.0);
            esperaAbridor (15000);
        }
        recogeApertura();
        focoRespeta = deviceManager.getCurrentAudioDevice() == nullptr ? 1 : 0;

        ponTransporte (true);
        esperaAbridor (15000);
        recogeApertura();
        focoVuelve = deviceManager.getCurrentAudioDevice() != nullptr ? 1 : 0;
        ponTransporte (false);
    }

    //  4c. LOS CHASQUIDOS Y LOS GESTOS QUE REABREN, con un driver que tarda
    //      1500 ms en CADA apertura. Subir el bufer por chasquidos -`checkXRuns`-
    //      y GRABAR, MEDIR y un chip de AUDIO llamaban a `setAudioDeviceSetup`
    //      en este hilo: ese tiempo, y el doble o el triple con dos o tres
    //      aperturas por gesto (Tribunal 2026-09, 4.1 y 4.3). Se mide lo que
    //      pasa este hilo DENTRO de cada llamada y las aperturas de cada gesto.
    int msXrun = -1, buferXrun = 0, msGrabar = -1, abreGrabar = -1, buferGrabar = 0,
        msParar = -1, abreParar = -1, msMedir = -1, abreMedir = -1, msChip = -1, buferChip = 0;
    {
        auto reloj = [] { return juce::Time::getMillisecondCounterHiRes(); };
        auto bloqueAhora = [this]
        {
            auto* d = deviceManager.getCurrentAudioDevice();
            return d != nullptr ? d->getCurrentBufferSizeSamples() : 0;
        };
        esperaAbridor (15000);
        recogeApertura();
        if (deviceManager.getCurrentAudioDevice() == nullptr)
        {
            pideAbrirSalida();
            esperaAbridor (15000);
            recogeApertura();
        }
        const int prefGuardada = loadBurstPreference();
        burstMult = 1;
        burstSuelo = 1;
        useLowestLatency();

        //  Cuatro chasquidos de golpe: la ley sube un nivel, 192 -> 384.
        xrunGuion = "0:10";
        xrunEscala = 1.0;
        xrunRelojMs = 0.0;
        xrunGraceMs = 0.0;
        lastXRuns = 0;
        xrunsSeen = 0;
        bancoOpenLentoMs = 1500;
        double a = reloj();
        checkXRuns (50.0);
        msXrun = juce::roundToInt (reloj() - a);
        esperaAbridor (15000);
        recogeApertura();
        buferXrun = bloqueAhora();
        xrunGuion = {};
        burstMult = 1;
        ProjectStore::escribeTexto (burstPreferenceFile(), juce::String (prefGuardada));
        bancoOpenLentoMs = 0;
        useLowestLatency();

        //  GRABAR y PARAR del micro.
        bancoOpenLentoMs = 1500;
        bancoAperturas = 0;
        a = reloj();
        toggleMicSampling();
        msGrabar = juce::roundToInt (reloj() - a);
        esperaAbridor (15000);
        abreGrabar = bancoAperturas;
        buferGrabar = recordingActive ? bloqueAhora() : -1;
        bancoAperturas = 0;
        a = reloj();
        toggleMicSampling();
        msParar = juce::roundToInt (reloj() - a);
        esperaAbridor (15000);
        recogeApertura();
        abreParar = bancoAperturas;

        //  MEDIR: solo la mitad que abre -la otra espera a que el audio oiga
        //  el clic, y aqui no hay audio que lo oiga-, y se deja como estaba.
        bancoAperturas = 0;
        a = reloj();
        startMeasure();
        msMedir = juce::roundToInt (reloj() - a);
        esperaAbridor (15000);
        abreMedir = bancoAperturas;
        engine.finishLatencyProbe();
        measuring = false;
        measureButton.setEnabled (true);
        pideEncargoAudio ([this] { abreCon (0); });
        esperaAbridor (15000);

        //  Un chip de AUDIO: bufer a 384.
        a = reloj();
        applyAudioSetup (384, 0.0);
        msChip = juce::roundToInt (reloj() - a);
        esperaAbridor (15000);
        recogeApertura();
        buferChip = bloqueAhora();
        bancoOpenLentoMs = 0;
        applyAudioSetup (192, 0.0);
        esperaAbridor (15000);
        recogeApertura();
    }

    //  5. UN PAD QUE TARDA TRES SEGUNDOS EN LEERSE, que es el «ATASCO 1081 ms
    //     en pads/cargar» de la captura del telefono: FUSE, MediaProvider o un
    //     instrumento de cinco octavas. Solo el pad 0 se lee -el resto sale de
    //     el, como un troceado- para que la medida dure tres segundos y no
    //     sesenta y cuatro veces tres. Lo que se mide es la vuelta MAS LARGA
    //     de stepPadJob, que es lo que el hilo de mensajes pasa sin latir.
    double peorPaso = 0.0;
    bool padsAcaban = false;
    {
        bancoLeerLentoMs = 3000;
        padJob = std::make_unique<PadLoadJob>();
        padJob->folder = juce::File();
        padJob->clearMissing = false;
        for (int i = 1; i < kNumPads; ++i) padJob->source[(size_t) i] = 0;
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        while (padJob != nullptr
               && juce::Time::getMillisecondCounterHiRes() - t0 < 20000.0)
        {
            const double a = juce::Time::getMillisecondCounterHiRes();
            stepPadJob();
            peorPaso = juce::jmax (peorPaso, juce::Time::getMillisecondCounterHiRes() - a);
            juce::Thread::sleep (5);
        }
        padsAcaban = padJob == nullptr;
        padJob.reset();
        bancoLeerLentoMs = 0;
    }

    //  6. EL PARTE DE ANDROID, con la traza de muestra que el banco inyecta por
    //     ZATI_SALIDA_PREVIA: el renglon que veria la persona y la cabeza del
    //     hilo principal.
    const auto parte = SalidaPrevia::lee();
    auto limpio = [] (juce::String t)
    {
        return t.replace ("\\", "/").replace ("\"", "'").replace ("\n", " | ");
    };

    juce::String serie;
    double f = kReviveMinMs;
    for (int i = 0; i < 6; ++i)
    {
        f = siguienteEsperaRevivir (f, 3000.0);
        serie << (i ? "," : "") << juce::roundToInt (f);
    }

    std::cout << "{\"revive\":1,\"dispositivo\":" << (hay ? 1 : 0)
              << ",\"bloque\":" << bloque
              << ",\"preguntas_pintar\":" << preguntasPintar
              << ",\"aperturas_volver\":" << aperturasVolver
              << ",\"preguntas_volver\":" << preguntasVolver
              << ",\"bloque_vuelta\":" << bloqueVuelta
              << ",\"ms_volver\":" << juce::roundToInt (ms)
              << ",\"vigilante_suelto\":" << (vigilanteSuelto ? 1 : 0)
              << ",\"intentos_60s\":" << intentosMuerto
              << ",\"recupera\":" << (recupera ? 1 : 0)
              << ",\"espera_tras\":" << juce::roundToInt (esperaTras)
              << ",\"esperas_caras\":[" << serie << "]"
              << ",\"tope\":" << juce::roundToInt (kReviveMaxMs)
              << ",\"ms_pedir_lento\":" << juce::roundToInt (msLento)
              << ",\"abre_lento\":" << (abreTras ? 1 : 0)
              << ",\"foco_respeta\":" << focoRespeta
              << ",\"foco_vuelve\":" << focoVuelve
              << ",\"tarea\":\"" << Bitacora::tarea.load() << "\""
              << ",\"ms_xrun\":" << msXrun
              << ",\"bufer_xrun\":" << buferXrun
              << ",\"ms_grabar\":" << msGrabar
              << ",\"abre_grabar\":" << abreGrabar
              << ",\"bufer_grabar\":" << buferGrabar
              << ",\"ms_parar\":" << msParar
              << ",\"abre_parar\":" << abreParar
              << ",\"ms_medir\":" << msMedir
              << ",\"abre_medir\":" << abreMedir
              << ",\"ms_chip\":" << msChip
              << ",\"bufer_chip\":" << buferChip
              << ",\"peor_paso_pads\":" << juce::roundToInt (peorPaso)
              << ",\"pads_acaban\":" << (padsAcaban ? 1 : 0)
              << ",\"salida_hay\":" << (parte.hay ? 1 : 0)
              << ",\"salida_fallo\":" << (salidaEsFallo (parte.motivo) ? 1 : 0)
              << ",\"salida_renglon\":\"" << limpio (renglonSalida (parte)) << "\""
              << ",\"salida_cabeza\":\"" << limpio (SalidaPrevia::cabezaDelMain (parte.traza)) << "\""
              << "}" << std::endl;
}


//  GUARDAR SIN HUECOS. Tribunal 2026-09, 7.1 a 7.5: lo que la persona no puede
//  recuperar si se pierde. Ver Tests/guardado.py, que ademas corre esto bajo
//  strace para contar los borrados del destino antes de cada rename.
void MainComponent::auditGuardado()
{
    //  La fabrica entera, como en el primer arranque: sin pads con audio no
    //  hay troceado que medir.
    loadFactoryKits();
    {
        const auto tope = juce::Time::getMillisecondCounter() + 60000u;
        while ((fabricaJob != nullptr || ! fabricaCola.empty())
               && juce::Time::getMillisecondCounter() < tope)
        {
            stepFabricaJob();
            if (fabricaJob != nullptr) juce::Thread::sleep (2);
        }
    }

    //  Punto de partida en disco: los 64 WAV y un estado que los describe.
    autosave();
    session.flush (30000);

    //  1. EL RELEVO, tres veces sobre fichero que ya existe: es el caso en
    //     el que `moveFileTo` borraba antes de renombrar. strace lo cuenta.
    for (int i = 0; i < 3; ++i)
    {
        session.writeState (captureState(), "relevo " + juce::String (i));
        ProjectStore::escribeTexto (ProjectStore::home().getChildFile ("relevo.txt"),
                                    "relevo " + juce::String (i));
        if (uiSample[0] != nullptr)
            ProjectStore::writeSample (ProjectStore::home().getChildFile ("relevo.wav"),
                                       uiSample[0]->buffer, uiSample[0]->sourceSampleRate);
    }

    //  2. EL TEMPORAL QUE SE RESCATA: una muerte entre escribirlo y ponerlo
    //     encima deja state.xml.tmp validado y ningun state.xml.
    bool rescata = false;
    {
        const auto st  = SessionKeeper::stateFile();
        const auto tmp = st.getSiblingFile ("state.xml.tmp");
        //  Por rename y no copiando y borrando: la sonda no puede abrir el
        //  hueco que strace esta contando.
        std::rename (st.getFullPathName().toRawUTF8(), tmp.getFullPathName().toRawUTF8());
        rescata = SessionKeeper::exists() && st.existsAsFile() && ! tmp.existsAsFile();
    }

    //  3. UN TROCEADO: el estado en disco tiene que decir que los trozos salen
    //     del pad 0 cuando el escritor ya ha borrado sus WAV. Por el camino
    //     del temporizador -sync con el estado delante- y sin el guardado de
    //     cada veinte segundos, que es el que llegaba tarde.
    constexpr int trozos = 8;
    session.writeState (captureState(), currentProject);
    session.flush (30000);
    chopSlices    = trozos;
    chopOnlyEmpty = false;
    chopByHits    = false;
    selectPad (0);
    applyAutoChop();
    session.sync (uiSample.data(), kNumPads, [this] { return textoSesion(); });
    session.flush (30000);

    int alDia = 0, borrados = 0;
    if (auto xml = juce::parseXML (SessionKeeper::stateFile()))
    {
        const auto padsV = juce::ValueTree::fromXml (*xml).getChildWithName ("PADS");
        for (int i = 1; i < trozos; ++i)
        {
            for (int m = 0; m < padsV.getNumChildren(); ++m)
            {
                const auto p = padsV.getChild (m);
                if ((int) p.getProperty ("i", -1) == i && (int) p.getProperty ("fuente", -1) == 0)
                    ++alDia;
            }
            if (! SessionKeeper::padFile (i).existsAsFile()) ++borrados;
        }
    }

    //  4. EL MISMO ESTADO DOS VECES NO SE ESCRIBE DOS VECES.
    const int escritosAntes = session.estadosEscritos();
    const int igualesAntes  = session.estadosIguales();
    session.pideEstado (textoSesion());
    session.flush (10000);
    session.pideEstado (textoSesion());
    session.flush (10000);
    const int escritos = session.estadosEscritos() - escritosAntes;
    const int iguales  = session.estadosIguales()  - igualesAntes;

    //  5. UNA ESCRITURA QUE FALLA NO DICE «GUARDADO». Un directorio donde va
    //     state.xml hace fallar el rename sin tocar nada mas.
    int fallaDevuelve = -1, fallaApunta = -1;
    {
        const auto st = SessionKeeper::stateFile();
        const auto guardado = st.getSiblingFile ("state.xml.banco");
        std::rename (st.getFullPathName().toRawUTF8(), guardado.getFullPathName().toRawUTF8());
        st.createDirectory();
        const auto antes = session.ultimaEscrituraMs();
        juce::Thread::sleep (20);
        fallaDevuelve = session.writeState (captureState(), "falla") ? 1 : 0;
        fallaApunta   = session.ultimaEscrituraMs() != antes ? 1 : 0;
        st.deleteRecursively();
        std::rename (guardado.getFullPathName().toRawUTF8(), st.getFullPathName().toRawUTF8());
    }

    //  6. EL PROYECTO ABIERTO NO SE REESCRIBE AL IRSE AL FONDO. Su project.xml
    //     describe las muestras de su ultimo guardado y nada mas.
    int intacto = -1;
    {
        const auto carpeta = ProjectStore::folderFor ("BANCO_GUARDADO");
        carpeta.createDirectory();
        const auto px = carpeta.getChildFile ("project.xml");
        const juce::String marca ("<ZATI marca=\"guardado por la persona\"/>");
        px.replaceWithText (marca);
        const auto antes = currentProject;
        currentProject = "BANCO_GUARDADO";
        autosave();
        session.flush (10000);
        intacto = px.loadFileAsString() == marca ? 1 : 0;
        currentProject = antes;
    }

    std::cout << "{\"guardado\":1"
              << ",\"rescata\":" << (rescata ? 1 : 0)
              << ",\"trozos\":" << trozos
              << ",\"estado_al_dia\":" << alDia
              << ",\"wav_borrados\":" << borrados
              << ",\"escritos\":" << escritos
              << ",\"iguales\":" << iguales
              << ",\"falla_devuelve\":" << fallaDevuelve
              << ",\"falla_apunta\":" << fallaApunta
              << ",\"proyecto_intacto\":" << intacto
              << "}" << std::endl;
}
