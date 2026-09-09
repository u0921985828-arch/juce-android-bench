#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "Zati.h"
#include "AudioEngine.h"
#include "StepGrid.h"
#include <array>
#include <cstring>
#include <vector>
#include <vector>
#include <utility>

// ============================================================================
//  Playlist — the arrangement, as a timeline you can see.
//
//  The chain was a queue: pattern after pattern, in a straight line, and that
//  is all a track could ever be. Here time runs across and four lanes run down,
//  so a drum pattern, a chopped break and a single vocal hit can all sit on the
//  same bar. That is what makes it an arrangement rather than a loop.
//
//  Two kinds of block, deliberately:
//    · a PATTERN, which occupies as many bars as its own length needs
//    · a ONE-SHOT, a single pad fired at the top of that bar — the thing the
//      chain could never hold, and the reason a crash or a vocal stab had to
//      be baked into a pattern of its own before.
//
//  A block wears the colour of what it holds: the pattern's bank colour, or
//  the pad's zati. Nothing here invents a new colour language.
// ============================================================================
class Playlist : public juce::Component, public Rejilla
{
public:
    //  Las tres cifras del banco. Los compases que se ven los mueve el zoom
    //  -cuatro, ocho o dieciseis- y los carriles son cuatro por contrato.
    int celdasAncho() const override { return getCompasesVista(); }
    int celdasAlto()  const override { return kLanes; }
    int canalIzq()    const override { return kGutter; }
    float celdaAnchoPx() const override { return (float) juce::jmax (0, getWidth() - kGutter) / (float) juce::jmax (1, barsView); }
    float celdaAltoPx()  const override { return (float) getHeight() / (float) kLanes; }

    //  La columna de los nombres de pista. Misma razon que en StepGrid: se
    //  escribia dos veces, y el pintado y el toque tienen que coincidir.
    static constexpr int kGutter = Metrics::canalCancion;

    static constexpr int kLanes    = 4;
    //  CUANTOS COMPASES SE VEN A LA VEZ. Era `constexpr 8` y por eso un
    //  estribillo de dieciseis no cabia en una pantalla: no se podia mirar
    //  entero. Ahora es 4, 8 o 16 y lo decide la persona, con el suelo de
    //  celda diciendo cual de los tres se puede ofrecer en cada pantalla.
    //
    //  El maximo se queda `constexpr` porque de el salen las tapas de pagina,
    //  que se crean UNA vez en el constructor: crear tapas al vuelo es lo que
    //  cerro la app la unica vez que la caja negra sirvio para algo.
    static constexpr int kBarsViewMax = 16;
    static constexpr int kBarsViewDef = 8;    // bars visible at once; pages beyond

    //  LOS CLIPS DE AUDIO SON LOS MISMOS CUATRO CARRILES.
    //
    //  Fueron dos VISTAS —PATRONES y AUDIO— con este argumento medido: la
    //  linea de tiempo se lleva 42 px por carril en un movil grande y 20.2 en
    //  280x653, asi que cuatro pistas MAS al mismo alto piden 160 px que no
    //  existen. Ese argumento sigue en pie y por eso esto no son ocho carriles:
    //  son los mismos cuatro con las dos cosas dentro, que es lo que se pidio
    //  —«que este todo junto y asi se puedan cuadrar mejor»— y lo que hace FL.
    //
    //  Y el numero ya cuadraba: `kAudioLanes` y `kLanes` valen los dos cuatro
    //  desde que existen, asi que fundirlos no cuesta un pixel de alto —`laneH`
    //  sigue siendo `h / 4`— y la comprobacion de la celda contra el dedo sigue
    //  saliendo. Con ocho, `h / 8` la tumbaba por el eje Y.
    //
    //  Lo que hacia falta resolver no era el alto sino el GESTO: en patrones
    //  arrastrar PINTA y en audio arrastrar MUEVE, y dos significados en un
    //  dedo es lo que esta casa lleva escrito que no se puede aprender. Lo
    //  resuelve la HERRAMIENTA armada, que ya existia: la MANO mueve —un
    //  bloque o un clip, lo que caiga debajo—, la GOMA quita, y el LAPIZ pinta
    //  patron. Una celda, una familia, decidida por lo que hay bajo el dedo.
    static_assert (kLanes == AudioEngine::kAudioTracks,
                   "un carril es una pista: si dejan de ser el mismo numero, "
                   "la rejilla dibujaria clips en carriles que no existen");

    //  Un clip, tal y como esta rejilla necesita verlo: EN COMPASES. El motor
    //  lo guarda en muestras -el audio mide lo que mide- y traducirlo pide el
    //  tempo, que es cosa del motor y no de un componente que dibuja. Quien
    //  sabe cuantas muestras son un compas lo traduce y pasa esto, igual que la
    //  tabla de zatis se pasa en vez de preguntarle al motor por cada bloque.
    struct ClipVista
    {
        int pista  = 0;      // 0..kAudioLanes-1
        int desde  = 0;      // primer compas que ocupa
        int hasta  = 1;      // el primero que YA NO ocupa, o sea [desde, hasta)
        int pad    = 0;      // de que pad salio, que es de donde sale su color
    };

    // (lane, bar) — the host decides what to place or whether to clear.
    std::function<void (int lane, int bar)> onCell;

    //  ESTIRAR UN BLOQUE ARRASTRANDO SU FILO. El largo de un bloque de patron
    //  solo se cambiaba con ACORTAR / ALARGAR, de compas en compas y sobre el
    //  bloque donde estuviera el cursor - mientras que un clip de audio, en la
    //  MISMA rejilla y en la vista de al lado, se estira arrastrando su borde.
    //  La misma accion con dos gestos segun lo que hubiera en la celda.
    //  El cuarto argumento es si este cambio es el PRIMERO del gesto, que es
    //  lo unico que hace falta para que deshacer se lleve el estiron entero:
    //  un arrastre emite un evento por movimiento, asi que apilar una entrada
    //  por evento no es deshacer, es contar. Medido: 3 entradas por un solo
    //  gesto antes de esta linea.
    std::function<void (int lane, int cabeza, int largo, bool primero)> onLargoBloque;

    //  MOVER un bloque entero y SILENCIARLO, que son las dos herramientas que
    //  esta rejilla no tenia y que un playlist de verdad si: probar una cancion
    //  sin el estribillo, o correrlo dos compases, se hacia borrando y
    //  volviendo a escribir.
    std::function<void (int lane, int cabeza, int carrilNuevo, int compasNuevo, bool primero)> onMueveBloque;
    std::function<void (int lane, int cabeza)> onMuteBloque;
    //  UN HUECO DE LA BANDA DE AUDIO: aqui no habia nada, pon lo que tengas.
    std::function<void (int pista, int compas)> onClipNuevo;
    //  Y UN CLIP QUE SE ARRASTRA. El indice es el de la tabla que se paso, no
    //  una identidad: quien la publica es quien la ordena.
    std::function<void (int indice, int pista, int compas)> onClipMueve;
    std::function<void (int indice)> onClipQuita;
    //  Y EL LARGO, arrastrando un filo. Llega en COMPASES porque es lo que
    //  esta rejilla sabe: quien traduce a muestras es quien tiene el tempo.
    std::function<void (int indice, int desdeCompas, int hastaCompas)> onClipLargo;
    //  Un toque en la canaleta del carril: lo silencia. Ver mouseDown.
    std::function<void (int lane)> onLane;

    //  Las veces que blockColour pidio un pad fuera de la tabla. Ver el
    //  desbordamiento que esto arreglo: es un fallo de INDICE, no de geometria,
    //  asi que ninguna de las ocho reglas del banco puede verlo y hace falta
    //  que la app lo cuente.
    static inline int zatisFueraDeRango = 0;

    //  Lo que de verdad le pasaron, para que el banco no tenga que repetir la
    //  constante: una prueba que lee el numero que juzga cambia de opinion a la
    //  vez que el fallo.
    int numZatis() const noexcept { return zatis; }

    //  LA MISMA CUENTA DE COMPAS A PIXEL, con otra cosa dibujada encima.
    //
    //  Cuantos carriles tiene la banda lo dice el MOTOR y no este fichero: son
    //  las pistas que sabe reproducir, y escribir aqui un cuatro seria la misma
    //  regla en dos sitios - la que ya costo que `kContinued` estuviera
    //  definido dos veces.
    static constexpr int kAudioLanes = AudioEngine::kAudioTracks;
    //  Lo mas corto que puede llevar asas. Con dos compases, las dos asas son
    //  el clip entero y no quedaria medio que agarrar para moverlo.
    static constexpr int kCompasesConAsa = 3;

    //  La tabla se PRESTA, no se copia: la publica quien la tiene y vive lo que
    //  dure la llamada del temporizador, igual que `data` y `zati`.
    //
    //  Y CON SOMBRA, que antes no hacia falta y ahora si: esto se llamaba solo
    //  con la vista de audio delante y ahora se llama SIEMPRE, treinta veces
    //  por segundo, con la ficha CANCION abierta. Un `repaint()` incondicional
    //  aqui arrastra el chasis, los dieciseis pads y los cuarenta controles que
    //  hay debajo del velo — que es exactamente el derroche que `setSource` ya
    //  evita con la suya y que `Tests/cpu.py` existe para cazar.
    void setAudio (const ClipVista* filas, int cuantas, int elegido, unsigned mudos) noexcept
    {
        const int n = juce::jmax (0, cuantas);
        const bool igual = clipsVistos
                        && n == (int) sombraClips.size() && elegido == clipSel
                        && mudos == mudoAudio
                        && (n == 0 || std::memcmp (sombraClips.data(), filas,
                                                   (size_t) n * sizeof (ClipVista)) == 0);
        clips = filas; numClips = n; clipSel = elegido;
        //  SU PROPIA MASCARA DE SILENCIO y no la de los carriles de patron.
        //  Son cuatro cosas distintas -silenciar el carril 1 no puede callar la
        //  pista de audio 1- y compartir el numero habria hecho justo eso sin
        //  un solo error de compilacion.
        mudoAudio = mudos;
        if (igual) return;
        sombraClips.assign (filas, filas + n);
        clipsVistos = true;
        repaint();
    }

    void setSource (const int* cells,       // [lane][bar] flattened, stride = bars
                    const int* zatiOf,      // un zati por pad...
                    int numZatis,           // ...y CUANTOS, que es la mitad que faltaba
                    int bars, int desdeCompas, int playBar,
                    int cursorBar = -1,     // el compas sobre el que actuan las herramientas
                    unsigned mudos = 0,     // un bit por carril silenciado
                    int loopA = 0, int loopB = 0,   // el tramo en bucle, [A,B) en compases
                    const juce::uint64* bloquesMudos = nullptr)  // un bit por compas y carril
    {
        data = cells; zati = zatiOf; zatis = numZatis;
        totalBars = bars;
        //  LA VENTANA ARRANCA EN UN COMPAS CUALQUIERA, no en un multiplo del
        //  zoom. Antes llegaba el numero de PAGINA y la vista empezaba en
        //  `pagina * barsView`, asi que con la vista en ocho un estribillo que
        //  cruzara el compas 8 no se podia mirar entero: habia que elegir una
        //  mitad. Se acota aqui -en la puerta- porque el valor sale de la
        //  barra, del cabezal y del fichero de proyecto.
        primerCompas = juce::jlimit (0, juce::jmax (0, bars - barsView), desdeCompas);
        playing = playBar;
        cursor = cursorBar; mute = mudos; lA = loopA; lB = loopB;
        juce::uint64 bm[kLanes] {};
        if (bloquesMudos != nullptr)
            for (int i = 0; i < kLanes; ++i) bm[i] = bloquesMudos[i];

        //  REPINTAR SOLO SI HA CAMBIADO ALGO. Por lo mismo que la rejilla de
        //  pasos: el temporizador llama aqui treinta veces por segundo
        //  mientras la ficha CANCION esta abierta, y lo unico que se mueve
        //  solo es el compas que suena. La tabla la cambias tu, con el dedo.
        //
        //  Y la ficha es translucida y ocupa la ventana entera, asi que un
        //  repintado de esta rejilla arrastra el chasis, los dieciseis pads y
        //  los cuarenta controles que hay debajo del velo.
        const size_t nCel = (size_t) kLanes * (size_t) juce::jmax (1, totalBars);
        bool igual = data != nullptr && visto
                  && totalBars == prevBars && primerCompas == prevPage && playing == prevPlaying
                  && cursor == prevCursor && mute == prevMute
                  //  Y el silencio por bloque, o silenciar uno no repintaria:
                  //  el atajo de arriba existe para no arrastrar el chasis
                  //  treinta veces por segundo, no para tragarse un cambio.
                  && std::memcmp (bmute, bm, sizeof (bmute)) == 0
                  && lA == prevLA && lB == prevLB
                  && sombra.size() == nCel
                  && std::memcmp (sombra.data(), data, nCel * sizeof (int)) == 0
                  && (zati == nullptr
                        || std::memcmp (sombraZati.data(), zati, sizeof (sombraZati)) == 0);

        if (igual) return;

        if (data != nullptr) { sombra.resize (nCel); std::memcpy (sombra.data(), data, nCel * sizeof (int)); }
        if (zati != nullptr) std::memcpy (sombraZati.data(), zati, sizeof (sombraZati));
        prevBars = totalBars; prevPage = primerCompas; prevPlaying = playing;
        prevCursor = cursor; prevMute = mute; prevLA = lA; prevLB = lB;
        std::memcpy (bmute, bm, sizeof (bmute));
        visto = true;

        repaint();
    }

    //  LO QUE UN BLOQUE LLEVA DENTRO, que es lo que separa una lista de
    //  bloques de una LINEA DE TIEMPO.
    //
    //  Un bloque era un rectangulo de color con «P3» escrito en medio, asi que
    //  la pagina decia DONDE va cada patron y no decia NINGUNO de ellos: para
    //  saber si el 3 es el que lleva el bombo hay que abrir la ficha del
    //  secuenciador, mirar, y volver. Con ocho patrones repartidos por sesenta
    //  y cuatro compases eso es la mitad del trabajo de arreglar una cancion.
    //
    //  Ahora el bloque dibuja SUS PASOS: una fila por pad que suene en ese
    //  patron y una marca por paso puesto, con el color del pad. No es un
    //  adorno - es la misma informacion que la rejilla de pasos, a la escala a
    //  la que cabe -, y a un vistazo se distingue un patron de bombo de uno de
    //  charles sin leer un solo numero.
    //
    //  La tabla es la MISMA que alimenta la rejilla de pasos y llega por
    //  puntero: 8 x 64 x 64 booleanos son 32 KB que ya existen, y copiarlos
    //  seria una segunda copia que un dia se queda vieja.
    void setPatrones (const bool* pasos,     // [patron][paso][pad]
                      const int*  largos,    // pasos que dura cada patron
                      int nPatrones, int nPasos, int nPads)
    {
        //  Las filas de cada patron -que pads suenan- se calculan AQUI y no en
        //  `paint`: alli habria que recorrer 64x64 booleanos por celda y por
        //  repintado, y el cabezal repinta esta rejilla en cada compas.
        const bool cambio = pasos != patPasos || largos != patLargos
                         || nPatrones != patN || nPasos != patPasos_ || nPads != patPads
                         || (pasos != nullptr
                             && std::memcmp (sombraPat.data(), pasos,
                                             juce::jmin (sombraPat.size(),
                                                         (size_t) nPatrones * (size_t) nPasos * (size_t) nPads)) != 0);
        patPasos = pasos; patLargos = largos;
        patN = nPatrones; patPasos_ = nPasos; patPads = nPads;
        if (! cambio || pasos == nullptr) return;

        const size_t n = (size_t) nPatrones * (size_t) nPasos * (size_t) nPads;
        sombraPat.resize (n);
        std::memcpy (sombraPat.data(), pasos, n);

        for (int q = 0; q < kMaxPat; ++q) filasDe[(size_t) q].clear();
        for (int q = 0; q < juce::jmin (kMaxPat, nPatrones); ++q)
            for (int pad = 0; pad < nPads; ++pad)
                for (int st = 0; st < nPasos; ++st)
                    if (pasos[((size_t) q * (size_t) nPasos + (size_t) st) * (size_t) nPads + (size_t) pad])
                    { filasDe[(size_t) q].push_back (pad); break; }

        repaint();
    }

    //  CUANTOS COMPASES SE VEN, y cuantos caben.
    //
    //  El suelo lo decide la CELDA y no un numero escrito aqui: a dieciseis
    //  compases la celda cae a la mitad de ancho, y una celda que se pinta con
    //  el dedo arrastrado por debajo de su suelo es una celda que se falla. La
    //  pregunta se hace con el ancho que la rejilla TIENE, asi que en una
    //  tableta se ofrecen los tres pasos y en un movil estrecho no.
    int  getCompasesVista() const noexcept { return barsView; }
    int  getPrimerCompas()  const noexcept { return primerCompas; }
    int  getPaginas()       const noexcept
    {
        return juce::jmax (1, (AudioEngine::kSongBars + barsView - 1) / barsView);
    }
    bool cabeVista (int n) const noexcept
    {
        const int w = getWidth() - kGutter;
        return n > 0 && w > 0 && (float) w / (float) n >= (float) Metrics::celdaCancion;
    }
    void setCompasesVista (int n)
    {
        n = juce::jlimit (4, kBarsViewMax, n);
        if (n == barsView) return;
        barsView = n;
        //  Y EL PRIMER COMPAS SE RE-ACOTA: al abrir la vista, lo que era el
        //  ultimo compas visible puede caer detras del final, y entonces la
        //  rejilla dibuja columnas que no existen.
        primerCompas = juce::jlimit (0, juce::jmax (0, totalBars - barsView), primerCompas);
        repaint();
    }

    //  EL MODO ARMADO, VISTO DONDE SE ACTUA. AUTO cambia si mover un mando
    //  ESCRIBE en la linea de tiempo, y su tapa esta en la fila de arriba
    //  mientras el dedo esta aqui. Transparente es «ningun modo».
    void setModo (juce::Colour c) { if (modoTinte != c) { modoTinte = c; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        if (data == nullptr) { marcoDelModo (g); return; }
        auto r = getLocalBounds();
        const int gutter = kGutter;
        const float laneH = (float) r.getHeight() / (float) kLanes;
        const float barW  = (float) (r.getWidth() - gutter) / (float) barsView;
        const int   base  = primerCompas;

        for (int lane = 0; lane < kLanes; ++lane)
        {
            const float y = (float) r.getY() + laneH * (float) lane;

            const bool mudo = (mute & (1u << (unsigned) lane)) != 0;

            auto gut = juce::Rectangle<float> ((float) r.getX(), y, (float) gutter, laneH).reduced (1.0f, 1.0f);
            //  Mismo criterio que la rejilla de pasos: la canaleta del carril
            //  es una marca medida contra la tarjeta, no un token de la placa
            //  de pads - que en las dos carcasas oscuras sale mas claro que el
            //  fondo y convierte una fila vacia en una fila que parece llena.
            //
            //  Y LA CANALETA ES EL INTERRUPTOR DE SILENCIO del carril. No hay
            //  sitio para cuatro tapas mas en una tarjeta que ya llega al tope
            //  de altura, y el numero del carril es justo lo que en una mesa
            //  lleva el boton de MUTE al lado. Silenciado se pinta en rojo con
            //  el numero tachado, que es lo unico que se lee de un vistazo en
            //  una casilla de treinta pixeles.
            g.setColour (mudo ? ZatiColours::red.withAlpha (0.85f)
                              : ZatiColours::markOn (ZatiColours::chassisTop, 0.18f));
            g.fillRect (gut);
            g.setColour (mudo ? ZatiColours::bestOn (ZatiColours::red, ZatiColours::ink, juce::Colours::white)
                              : ZatiColours::inkDim);
            g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
            g.drawText (juce::String (lane + 1), gut, juce::Justification::centred);
            if (mudo)
                g.fillRect (gut.getX() + 3.0f, gut.getCentreY() - 0.5f, gut.getWidth() - 6.0f, 1.4f);

            for (int c = 0; c < barsView; ++c)
            {
                const int bar = base + c;
                const float x = (float) r.getX() + (float) gutter + barW * (float) c;
                auto cell = juce::Rectangle<float> (x, y, barW, laneH).reduced (1.5f);

                if (bar >= totalBars)                        // past the end of the song
                {
                    g.setColour (ZatiColours::groove (0.30f));   // pasado el final
                    g.fillRect (cell);
                    continue;
                }

                const int v = data[lane * totalBars + bar];

                if (v == 0)
                {
                    //  Un compas vacio es un hueco, y un hueco oscurece.
                    g.setColour (ZatiColours::groove ((bar % 4 == 0) ? 0.48f : 0.28f));
                    g.fillRect (cell);
                }
                else if (v == kContinued)
                {
                    // The tail of a longer pattern: same colour, no label, so a
                    // four-bar block reads as ONE block instead of four copies.
                    const int startBar = findStart (lane, bar);
                    const int sv = startBar >= 0 ? data[lane * totalBars + startBar] : 0;
                    const bool mini = cabeMini (cell) && sv > 0;
                    const bool mudoB = mudo || bloqueMudo (lane, bar);
                    g.setColour (blockColour (sv).withAlpha (mudoB ? 0.18f : (mini ? 0.22f : 0.55f)));
                    g.fillRect (cell.withTrimmedLeft (-1.5f));
                    //  Y LA COLA ENSEÑA SU TROZO, que es lo que la hace cola y
                    //  no una copia: un bloque de cuatro compases con un patron
                    //  de cuatro enseña compases distintos en cada celda, y con
                    //  uno de uno enseña el mismo cuatro veces - que es
                    //  exactamente lo que suena.
                    if (mini && startBar >= 0)
                        pintaPasos (g, cell.reduced (2.0f).withTrimmedTop (0.0f), sv,
                                    (bar - startBar) * StepGrid::kBarSteps, mudoB);
                }
                else
                {
                    const auto col = blockColour (v);
                    //  Un carril silenciado ensena sus bloques HUECOS: siguen
                    //  ahi, con su color y su nombre, y no suenan. Borrarlos
                    //  seria otra cosa, y esa ya existe.
                    //
                    //  Y un bloque silenciado SOLO se ve igual, que es lo que
                    //  hace que la herramienta se vea: un mute que no se
                    //  distingue de sonar no es un mute, es un boton.
                    const bool mudoB = mudo || bloqueMudo (lane, bar);
                    if (mudoB)
                    {
                        g.setColour (ZatiColours::groove (0.30f));
                        g.fillRect (cell);
                        g.setColour (col.withAlpha (0.70f));
                        g.drawRect (cell, 1.4f);
                    }
                    else
                    {
                        //  EL FONDO DEL BLOQUE SE HUNDE cuando lleva
                        //  miniatura. Con el color a pleno las marcas de los
                        //  pads caen sobre su propio tono y desaparecen: el
                        //  bloque es del color del PATRON y las marcas del
                        //  color de cada PAD, y dos colores plenos uno sobre
                        //  otro no se separan. Con la tapa al 30% el bloque
                        //  sigue diciendo cual es y las marcas se leen encima.
                        g.setColour (cabeMini (cell) ? col.withAlpha (0.30f) : col);
                        g.fillRect (cell);
                        if (cabeMini (cell))
                        {
                            g.setColour (col.withAlpha (0.85f));
                            g.drawRect (cell, 1.2f);
                        }
                    }

                    //  LO QUE EL BLOQUE LLEVA DENTRO. Solo un patron - un
                    //  golpe de pad suelto no tiene pasos que enseñar - y solo
                    //  donde cabe: por debajo de kAltoMini el bloque se queda
                    //  como estaba, que es la escalera de siempre.
                    juce::Rectangle<float> texto = cell;
                    if (v > 0 && cabeMini (cell))
                    {
                        auto dentro = cell.reduced (2.0f);
                        texto = dentro.removeFromTop (kFilaRotulo);
                        //  El primer compas del bloque empieza en el paso cero
                        //  del patron; los de detras siguen contando, y el
                        //  resto da la vuelta si el bloque es mas largo que el
                        //  patron.
                        const int desde = (bar - findStart (lane, bar)) * StepGrid::kBarSteps;
                        pintaPasos (g, dentro, v, juce::jmax (0, desde), mudo);
                    }

                    g.setColour (mudo ? col.withAlpha (0.85f)
                                      : ZatiColours::bestOn (col, ZatiColours::ink, juce::Colours::white));
                    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
                    g.drawText (v > 0 ? "P" + juce::String (v)
                                      : juce::String (-v).paddedLeft ('0', 2),
                                texto,
                                texto == cell ? juce::Justification::centred
                                              : juce::Justification::centredLeft);
                }

                if (bar == playing)
                {
                    //  The bar being played, in the playhead's colour rather
                    //  than in the recording one. Ink outside, white inside,
                    //  so it reads on a pale card and on a filled block alike.
                    g.setColour (ZatiColours::playheadEdge);
                    g.drawRect (cell.expanded (1.0f), 1.4f);
                    g.setColour (ZatiColours::playhead);
                    g.drawRect (cell, 1.6f);
                }
            }
        }

        //  EL TRAMO EN BUCLE, como una barra sobre los compases que repiten.
        //  Dibujado ENCIMA de las celdas y no detras: detras lo tapaba el
        //  primer bloque que cayera dentro, que es siempre - un tramo en bucle
        //  sin bloques dentro no es un tramo, es un descanso.
        if (lB > lA)
        {
            const float x0 = (float) r.getX() + gutter + barW * (float) juce::jmax (0, lA - base);
            const float x1 = (float) r.getX() + gutter + barW * (float) juce::jmin (barsView, lB - base);
            if (x1 > x0 && lB > base && lA < base + barsView)
            {
                g.setColour (ZatiColours::green.withAlpha (0.85f));
                g.fillRect (x0, (float) r.getY(), x1 - x0, 3.0f);
                g.fillRect (x0, (float) r.getY(), 2.0f, 9.0f);
                g.fillRect (x1 - 2.0f, (float) r.getY(), 2.0f, 9.0f);
            }
        }

        //  EL CURSOR: el compas sobre el que actuan INSERTAR, QUITAR, COPIAR y
        //  PEGAR. Sin el, esas cuatro no tendrian donde actuar y habria que
        //  inventar un gesto para decirselo. Una columna entera marcada, no
        //  una celda: las cuatro herramientas trabajan sobre el COMPAS, con
        //  sus cuatro carriles, y marcar una sola celda diria lo contrario.
        if (cursor >= base && cursor < base + barsView && cursor < totalBars)
        {
            const float x = (float) r.getX() + gutter + barW * (float) (cursor - base);
            g.setColour (ZatiColours::ink.withAlpha (0.85f));
            g.drawRect (juce::Rectangle<float> (x, (float) r.getY(), barW, (float) r.getHeight()), 2.0f);
        }

        //  Y LOS CLIPS ENCIMA, en los MISMOS carriles.
        //
        //  Encima y no al lado: un clip y un bloque de patron pueden caer en la
        //  misma celda —nada lo impide y en FL tampoco— y entonces el clip gana
        //  el dibujo y gana el dedo. Es una eleccion y no un descuido: el
        //  patron de debajo sigue sonando, que es lo que el motor hace desde
        //  que los dos se renderizan en el mismo bucle de segmento, y lo que no
        //  puede pasar es que el dibujo diga una cosa y el toque haga otra.
        pintaClips (g);

        // Bar numbers along the top edge of the first lane.
        g.setColour (ZatiColours::inkDim.withAlpha (0.7f));
        g.setFont (ZatiColours::monoFont (Metrics::fTiny, true));
        for (int c = 0; c < barsView; c += 2)
            g.drawText (juce::String (base + c + 1),
                        (int) ((float) r.getX() + gutter + barW * (float) c) + 2, r.getY(),
                        (int) barW, 9, juce::Justification::topLeft);
    }

    //  Un marco alrededor del LIENZO y no de la ficha, por lo mismo que en el
    //  piano: lo que tiene que decir «esto esta en un modo» es la superficie
    //  sobre la que cae el dedo.
    void marcoDelModo (juce::Graphics& g)
    {
        if (modoTinte.isTransparent()) return;
        auto marco = getLocalBounds().toFloat().reduced (0.75f);
        marco.setLeft (marco.getX() + (float) kGutter);
        g.setColour (modoTinte.withAlpha (0.9f));
        g.drawRect (marco, 1.5f);
    }

    //  SE PINTA CON EL DEDO ARRASTRADO, como la rejilla de pasos y como el
    //  piano.
    //
    //  Solo habia mouseDown: escribir ocho compases eran ocho toques, y la
    //  regla de la casa dice que esta ficha no se desplaza
    //  precisamente porque "se pinta con el dedo arrastrado". O faltaba el
    //  gesto o sobraba el argumento; faltaba el gesto.
    //
    //  Con dos candados que la rejilla de pasos ya tiene por el mismo motivo:
    //  la canaleta del MUTE solo responde al TOQUE -arrastrar por ella
    //  silenciaria los cuatro carriles de una pasada- y una celda no se
    //  escribe dos veces seguidas, o pasar el dedo por encima de la misma
    //  casilla la enciende y la apaga a la velocidad de los eventos del raton.
    //  Y EN LA BANDA DE AUDIO, ARRASTRAR MUEVE Y NO PINTA. Son dos gestos
    //  incompatibles en el mismo dedo -uno escribe celdas y el otro desplaza un
    //  bloque- y por eso no comparten vista: aqui no hay ninguna celda que se
    //  pueda encender arrastrando, asi que no hay nada que aprender dos veces.
    //  UNA CELDA, UNA FAMILIA — y la decide lo que hay bajo el dedo MAS la
    //  herramienta armada, no la pestaña en la que estabas.
    //
    //  La MANO mueve y estira lo que caiga: un bloque de patron o un clip, que
    //  son la misma accion sobre dos cosas distintas. La GOMA quita lo mismo.
    //  El LAPIZ pinta PATRON —o suelta un clip, si la brocha es CLIP— y
    //  SILENCIAR es de un bloque, que es lo unico que hoy se puede silenciar
    //  por separado: un clip se calla por su carril y eso ya lo hace la
    //  canaleta.
    //
    //  Y TODO EL ESTADO DE GESTO SE LIMPIA AL APOYAR. Antes lo limpiaba solo
    //  `mouseUp` y `mouseDown` reseteaba `ultima` en una sola de las dos ramas:
    //  con las dos familias en la misma rejilla, un arrastre que empieza en un
    //  bloque y acaba sobre un clip cruzaba `agarre`, `asa` y `arrastrado` sin
    //  que nada lo impidiera.
    bool clipBajoElDedo (const juce::MouseEvent& e) const
    {
        auto r = getLocalBounds();
        if (e.x < r.getX() + kGutter) return false;
        const float pistaH = (float) r.getHeight() / (float) kAudioLanes;
        const float barW   = (float) (r.getWidth() - kGutter) / (float) barsView;
        if (barW <= 0.0f || pistaH <= 0.0f) return false;
        const int pista  = juce::jlimit (0, kAudioLanes - 1, (int) ((float) (e.y - r.getY()) / pistaH));
        const int compas = primerCompas
                         + juce::jlimit (0, barsView - 1, (int) ((float) (e.x - r.getX() - kGutter) / barW));
        return clipEn (pista, compas) >= 0;
    }

    bool tocaAlClip (const juce::MouseEvent& e) const
    {
        if (herramienta == hMano || herramienta == hGoma) return clipBajoElDedo (e);
        //  Con el LAPIZ y la brocha en CLIP, un hueco suelta un clip; encima de
        //  uno que ya esta, el lapiz no tiene nada que decir.
        if (herramienta == hLapiz && pincelClip) return ! clipBajoElDedo (e);
        return false;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        asaBloque = 0; bloqueCarril = -1; arrastrado = -1; asa = 0;
        ultima = { -1, -1 };
        enClip = tocaAlClip (e);
        if (enClip) { tocaAudio (e, false); return; }
        toca (e, false);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (enClip) { tocaAudio (e, true); return; }
        toca (e, true);
    }
    void mouseUp   (const juce::MouseEvent&)   override
    {
        asaBloque = 0;
        bloqueCarril = -1;
        ultima = { -1, -1 };
        arrastrado = -1;
        enClip = false;
    }

    void toca (const juce::MouseEvent& e, bool arrastrando)
    {
        if (data == nullptr) return;
        auto r = getLocalBounds();
        const int gutter = kGutter;
        const float laneH0 = (float) r.getHeight() / (float) kLanes;

        //  La canaleta silencia el carril. Ver el pintado: es el sitio donde
        //  una mesa lleva el MUTE, y no cuesta una fila de tapas en una
        //  tarjeta que ya llega al tope de altura.
        if (e.x < r.getX() + gutter)
        {
            if (onLane && ! arrastrando)
                onLane (juce::jlimit (0, kLanes - 1, (int) ((float) (e.y - r.getY()) / laneH0)));
            return;
        }

        const float laneH = (float) r.getHeight() / (float) kLanes;
        const float barW  = (float) (r.getWidth() - gutter) / (float) barsView;
        const int lane = juce::jlimit (0, kLanes - 1, (int) ((float) (e.y - r.getY()) / laneH));
        const int bar  = primerCompas
                       + juce::jlimit (0, barsView - 1, (int) ((float) (e.x - r.getX() - gutter) / barW));
        if (bar >= totalBars) return;

        //  SILENCIAR UN BLOQUE: un toque, y solo un toque. Arrastrar por una
        //  fila silenciandolos todos es lo mismo que la canaleta ya tiene
        //  prohibido y por la misma razon: un roce se llevaria la cancion.
        if (herramienta == hMute)
        {
            if (! arrastrando && onMuteBloque)
            {
                const auto b = bloqueEn (lane, bar);
                if (b.first >= 0) onMuteBloque (lane, b.first);
            }
            return;
        }

        //  LA MANO: EL FILO ESTIRA Y EL MEDIO MUEVE.
        //
        //  Las dos viven aqui y no en el lapiz, que es lo que hace que no
        //  cuesten el pincel: con el lapiz armado esta rejilla se comporta
        //  exactamente como siempre. La primera version puso el asa en el
        //  lapiz con un candado -el toque pinta, el arrastre estira- y
        //  funcionaba, pero seguia siendo un segundo significado en el mismo
        //  dedo, que es lo que esta casa lleva escrito que no se aprende. Con
        //  la mano cada gesto tiene uno. Es la leccion de la SELECCION del
        //  piano: la herramienta es la quinta, no un gesto nuevo.
        //
        //  Y el asa con las dos condiciones que la vista de audio ya midio:
        //  solo el primer y el ultimo compas, y POR DEBAJO DE TRES COMPASES no
        //  hay asas - dos asas de un compas se comen un bloque de dos y no
        //  queda nada que arrastrar.
        if (herramienta == hMano)
        {
            if (! arrastrando)
            {
                asaBloque = 0;
                bloqueCarril = -1;
                bloquePrimero = true;
                const auto b = bloqueEn (lane, bar);
                if (b.first < 0) return;
                bloqueCarril = lane;
                bloqueCabeza = b.first;
                bloqueFin    = b.second;
                if ((b.second - b.first) >= kCompasesConAsa)
                {
                    if (bar == b.first)           asaBloque = -1;
                    else if (bar == b.second - 1) asaBloque = +1;
                }
                //  DONDE SE AGARRO, en compases desde la cabeza. Sin esto,
                //  arrastrar un bloque de cuatro por su tercer compas lo pega
                //  de un salto por su primero: se mueve un trozo que la persona
                //  no pidio, y es lo primero que se nota.
                agarre = bar - b.first;
                return;
            }

            if (bloqueCarril < 0) return;

            if (asaBloque != 0)
            {
                //  UN ASA CAMBIA EL LARGO Y NO LA POSICION. Son dos cosas y no
                //  una: un asa que ademas mueve pasa cualquier prueba que solo
                //  mire el largo, y desde el dedo es un bloque que se escapa
                //  mientras lo recortas. Por el filo izquierdo la cabeza no se
                //  toca: lo que se mueve es el FINAL.
                const int nuevoL = (asaBloque < 0) ? juce::jmax (1, bloqueFin - bar)
                                                   : juce::jmax (1, bar - bloqueCabeza + 1);
                if (onLargoBloque && nuevoL != bloqueFin - bloqueCabeza)
                {
                    onLargoBloque (bloqueCarril, bloqueCabeza, nuevoL, bloquePrimero);
                    bloquePrimero = false;
                    bloqueFin = bloqueCabeza + nuevoL;
                }
                return;
            }

            const int destino = juce::jmax (0, bar - agarre);
            if (onMueveBloque && (destino != bloqueCabeza || lane != bloqueCarril))
            {
                onMueveBloque (bloqueCarril, bloqueCabeza, lane, destino, bloquePrimero);
                bloquePrimero = false;
                bloqueFin    = destino + (bloqueFin - bloqueCabeza);
                bloqueCabeza = destino;
                bloqueCarril = lane;
            }
            return;
        }

        if (! onCell) return;
        if (arrastrando && lane == ultima.first && bar == ultima.second) return;
        ultima = { lane, bar };
        onCell (lane, bar);
    }

    //  ------------------------------------------------------------------
    //  LA BANDA DE AUDIO
    //  ------------------------------------------------------------------

    //  El rectangulo de un compas en una pista, con la MISMA cuenta que la
    //  vista de patrones. Escrita una vez y usada por el pintado y por el
    //  gesto, que es la leccion de la canaleta: cuando el dibujo y el toque
    //  hacen la cuenta cada uno por su lado, un dia dejan de coincidir.
    juce::Rectangle<float> celdaAudio (int pista, int compas) const
    {
        auto r = getLocalBounds();
        const float pistaH = (float) r.getHeight() / (float) kAudioLanes;
        const float barW   = (float) (r.getWidth() - kGutter) / (float) barsView;
        return { (float) r.getX() + (float) kGutter + barW * (float) (compas - primerCompas),
                 (float) r.getY() + pistaH * (float) pista, barW, pistaH };
    }

    //  LOS CLIPS, DESPUES DE LOS BLOQUES y no celda a celda: un clip de cuatro
    //  compases es UN bloque y no cuatro copias del mismo, que es exactamente
    //  lo que `kContinued` existe para conseguir con los patrones. Aqui sale
    //  gratis porque el clip ya sabe donde acaba.
    void pintaClips (juce::Graphics& g)
    {
        auto r = getLocalBounds();
        const float barW = (float) (r.getWidth() - kGutter) / (float) barsView;
        const int   base = primerCompas;

        for (int i = 0; i < numClips; ++i)
        {
            const ClipVista& c = clips[i];
            if (! juce::isPositiveAndBelow (c.pista, kAudioLanes)) continue;
            const int d = juce::jmax (c.desde, base);
            const int h = juce::jmin (c.hasta, base + barsView);
            if (h <= d) continue;                       // no cae en esta pagina

            auto caja = celdaAudio (c.pista, d)
                            .withWidth (barW * (float) (h - d)).reduced (1.5f);
            const auto col = (zati != nullptr && juce::isPositiveAndBelow (c.pad, zatis))
                                 ? Zati::colour (zati[c.pad]) : Zati::colour (c.pad);
            const bool mudo = (mudoAudio & (1u << (unsigned) c.pista)) != 0;

            if (mudo)
            {
                g.setColour (ZatiColours::groove (0.30f)); g.fillRect (caja);
                g.setColour (col.withAlpha (0.70f));       g.drawRect (caja, 1.4f);
            }
            else
            {
                g.setColour (col); g.fillRect (caja);
            }

            //  EL QUE SE ESTA MOVIENDO SE VE. Sin marca, arrastrar en una banda
            //  de cuatro pistas es soltar y buscar cual se movio.
            if (i == clipSel)
            {
                g.setColour (ZatiColours::playheadEdge); g.drawRect (caja.expanded (1.0f), 1.4f);
                g.setColour (ZatiColours::playhead);     g.drawRect (caja, 1.6f);
            }

            //  Y LAS ASAS SE DIBUJAN donde se pueden coger: dos filos verticales
            //  en el primer y el ultimo compas. Un asa que existe y no se ve es
            //  un gesto que nadie encuentra, y una que se ve donde no existe
            //  -en un clip corto- es peor.
            if ((c.hasta - c.desde) >= kCompasesConAsa)
            {
                g.setColour (ZatiColours::bestOn (col, ZatiColours::ink, juce::Colours::white)
                                 .withAlpha (0.55f));
                g.fillRect (caja.getX() + 2.0f, caja.getY() + 3.0f, 2.0f, caja.getHeight() - 6.0f);
                g.fillRect (caja.getRight() - 4.0f, caja.getY() + 3.0f, 2.0f, caja.getHeight() - 6.0f);
            }

            g.setColour (mudo ? col.withAlpha (0.85f)
                              : ZatiColours::bestOn (col, ZatiColours::ink, juce::Colours::white));
            g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
            g.drawText (juce::String (c.pad + 1).paddedLeft ('0', 2), caja, juce::Justification::centred);
        }
    }

    //  Que clip cae bajo (pista, compas). El PRIMERO que lo contenga: dos clips
    //  encima no es un estado que esta app produzca, y elegir "el de arriba"
    //  sin que haya arriba seria inventarse una regla.
    int clipEn (int pista, int compas) const
    {
        for (int i = 0; i < numClips; ++i)
            if (clips[i].pista == pista && compas >= clips[i].desde && compas < clips[i].hasta)
                return i;
        return -1;
    }

    void tocaAudio (const juce::MouseEvent& e, bool arrastrando)
    {
        auto r = getLocalBounds();
        const float pistaH = (float) r.getHeight() / (float) kAudioLanes;
        const float barW   = (float) (r.getWidth() - kGutter) / (float) barsView;
        if (barW <= 0.0f || pistaH <= 0.0f) return;

        const int pista  = juce::jlimit (0, kAudioLanes - 1, (int) ((float) (e.y - r.getY()) / pistaH));
        const int compas = primerCompas
                         + juce::jlimit (0, barsView - 1, (int) ((float) (e.x - r.getX() - kGutter) / barW));
        if (compas >= totalBars) return;

        if (! arrastrando)
        {
            arrastrado = clipEn (pista, compas);
            asa = 0;
            if (arrastrado >= 0)
            {
                //  LAS ASAS, con la cuenta hecha. El precedente es
                //  WaveformDisplay -«coge el asa mas cercana, pero solo dentro
                //  de un dedo»- y aqui un dedo entero no vale: la celda mide
                //  40 px por compas (medido), asi que dos asas de 40 se comen
                //  un clip de un compas y no queda nada que arrastrar.
                //
                //  Un TERCIO del clip por lado y como mucho 16 px, y POR DEBAJO
                //  DE TRES COMPASES no hay asas: ahi el gesto solo mueve, que
                //  es lo que se quiere de un clip corto. La escalera de
                //  siempre, y el banco la mide.
                const auto& c = clips[arrastrado];
                const int  ancho = c.hasta - c.desde;
                if (ancho >= kCompasesConAsa)
                {
                    if (compas == c.desde)          asa = -1;
                    else if (compas == c.hasta - 1) asa = +1;
                }
            }
            if (arrastrado < 0)
            {
                //  Un hueco: aqui no hay nada que mover, asi que el gesto solo
                //  puede significar poner algo.
                if (onClipNuevo) onClipNuevo (pista, compas);
                return;
            }
            //  DONDE SE AGARRO, en compases desde el principio del clip. Sin
            //  esto, arrastrar un clip de cuatro compases por su tercer compas
            //  lo pega de un salto por su primero: el bloque se mueve un trozo
            //  que la persona no pidio, y es lo primero que se nota.
            agarre = compas - clips[arrastrado].desde;
            //  Y LA GOMA LO QUITA, que es la misma herramienta que borra un
            //  bloque: una funcion, un dueño. `borrando` sigue valiendo porque
            //  la brocha VACIAR y la GOMA son la misma cosa vista desde dos
            //  sitios — lo dice `ponHerramienta`.
            if (onClipQuita && (borrando || herramienta == hGoma))
            { onClipQuita (arrastrado); arrastrado = -1; }
            return;
        }

        if (arrastrado < 0 || arrastrado >= numClips) return;

        //  UN ASA CAMBIA EL LARGO Y NO LA POSICION. Son dos cosas y no una: un
        //  asa que ademas mueve pasa cualquier prueba que solo mire el largo, y
        //  desde el dedo es un clip que se escapa mientras lo recortas.
        if (asa != 0)
        {
            const auto& c = clips[arrastrado];
            int d = c.desde, h = c.hasta;
            if (asa < 0) d = juce::jmin (compas, h - 1);
            else         h = juce::jmax (compas + 1, d + 1);
            if (d == c.desde && h == c.hasta) return;
            if (onClipLargo) onClipLargo (arrastrado, d, h);
            return;
        }

        const int nuevoCompas = juce::jmax (0, compas - agarre);
        if (nuevoCompas == clips[arrastrado].desde && pista == clips[arrastrado].pista) return;
        if (onClipMueve) onClipMueve (arrastrado, pista, nuevoCompas);
    }

    //  LA HERRAMIENTA ARMADA, que es lo que hace que el gesto sea inequivoco.
    //
    //  Esta rejilla se pinta con el dedo arrastrado, asi que arrastrar YA
    //  significa pintar: meter «mover» y «estirar» encima serian tres
    //  significados en un dedo, que es lo que esta casa lleva escrito que no se
    //  puede aprender. Con una herramienta armada cada gesto tiene UN
    //  significado, y sin ninguna la rejilla se comporta exactamente como
    //  siempre - que es la leccion de la SELECCION del piano: la herramienta es
    //  la quinta, no un gesto nuevo.
    //  Con prefijo: `mute` a secas choca con la mascara de carriles
    //  silenciados, que se llama asi desde que existe.
    enum Herramienta { hLapiz = 0, hGoma, hMano, hMute };
    int herramienta = hLapiz;

    //  Lo que la brocha VACIAR pone: quien la lleva es la ficha, y aqui solo se
    //  lee. Un componente que decidiera solo cual es la brocha seria un segundo
    //  dueno de la misma pregunta.
    bool borrando = false;

    //  Y SI LA BROCHA PONE UN CLIP. `songPadModeBtn` cicla SONIDO · CLIP ·
    //  VACIAR: con esto puesto, un toque en un hueco suelta un clip del pad
    //  elegido, que es lo que hacia un toque en la vista de audio. Cero pixeles
    //  nuevos — la tapa ya existia y sigue diciendo el ESTADO.
    bool pincelClip = false;

    //  El mismo centinela que AudioEngine::kContinued, y por eso se toma de
    //  alli: estaba escrito dos veces con el mismo numero, o sea dos duenos
    //  para una regla - mover uno dejaria la rejilla pintando bloques que el
    //  motor no reconoce, sin un solo error de compilacion.
    static constexpr int kContinued = AudioEngine::kContinued;

private:
    //  La ultima celda escrita por el arrastre en curso: sin esto, pasar el
    //  dedo por la misma casilla la enciende y la apaga a la velocidad de los
    //  eventos del raton.
    std::pair<int, int> ultima { -1, -1 };
    // Pattern banks borrow the fragment palette so a block is recognisable at a
    // glance; a one-shot wears its own pad's zati.
    //  EL COLOR DE UN GOLPE SUELTO SE PEDIA CON UN INDICE DE 0..63 A UNA TABLA
    //  DE DIECISEIS.
    //
    //  El pincel de un solo golpe guarda -(pad+1) y `selectedPad` va de 0 a 63
    //  (cuatro bancos de dieciseis), pero aqui llegaba `gridZati`, que es la
    //  tabla del banco QUE SE VE: dieciseis huecos. Con cualquier pad de los
    //  bancos B, C o D pintado en la linea de tiempo, esto leia fuera del array
    //  en CADA repintado de la ficha CANCION.
    //
    //  Hoy no se cae -lo leido acaba en Zati::colour, que envuelve con un modulo
    //  y devuelve un color cualquiera- pero es lectura fuera de rango: una
    //  compilacion con ASan, un asignador endurecido o MTE en un movil reciente
    //  lo convierten en un cierre. Y el sintoma visible tampoco era ninguno: el
    //  bloque salia del color de OTRO pad.
    //
    //  Se arregla por los dos lados. La tabla que se pasa es la de los sesenta y
    //  cuatro (ver refreshSong), que ademas es lo correcto — un golpe del pad 33
    //  se pinta del color del pad 33 y no del 01 del banco de delante — y el
    //  subindice se acota igual, porque una tabla correcta hoy no impide que
    //  alguien vuelva a pasar una corta manana.
    //  LA MINIATURA DE UN PATRON dentro de su celda.
    //
    //  Una fila por pad que suene y una marca por paso puesto. `paso0` es el
    //  paso del patron con el que empieza ESTA celda, que no es siempre cero:
    //  un bloque de cuatro compases con un patron de dos DA LA VUELTA dentro
    //  -esa es la regla del largo de bloque, medida contando disparos- asi que
    //  la tercera celda vuelve a empezar. Dibujarla desde cero seria enseñar
    //  cuatro veces el mismo compas y sonar otra cosa.
    //
    //  Y NO CABEN LAS SESENTA Y CUATRO FILAS: a 42 px de carril, dieciseis
    //  pads dan 2.6 px por fila y sesenta y cuatro dan 0.65. Se enseñan las
    //  que quepan a `kMinFila` cada una, en orden de pad — que es el orden de
    //  la rejilla — y las de mas no se dibujan. Es la escalera de siempre: se
    //  pide lo que hay.
    void pintaPasos (juce::Graphics& g, juce::Rectangle<float> caja,
                     int patron, int paso0, bool mudo) const
    {
        if (patPasos == nullptr || patron < 1 || patron > juce::jmin (kMaxPat, patN)) return;
        const auto& filas = filasDe[(size_t) (patron - 1)];
        if (filas.empty() || caja.getHeight() < 6.0f) return;

        const int largo = (patLargos != nullptr)
                            ? juce::jlimit (1, patPasos_, patLargos[patron - 1]) : patPasos_;

        constexpr float kMinFila = 2.0f;
        const int cabenF = juce::jmax (1, (int) (caja.getHeight() / kMinFila));
        const int nF     = juce::jmin ((int) filas.size(), cabenF);
        const float fh   = caja.getHeight() / (float) nF;
        const float cw   = caja.getWidth()  / (float) StepGrid::kBarSteps;

        for (int r = 0; r < nF; ++r)
        {
            const int pad = filas[(size_t) r];
            const auto col = (zati != nullptr && pad >= 0 && pad < zatis)
                               ? Zati::colour (zati[pad]) : ZatiColours::inkDim;
            g.setColour (mudo ? col.withAlpha (0.35f) : col);

            const float y = caja.getY() + fh * (float) r;
            //  La marca no llena su fila: un pixel de aire entre filas es lo
            //  que hace que ocho filas se lean como ocho y no como una mancha.
            const float mh = juce::jmax (1.0f, fh - 1.0f);

            for (int c = 0; c < StepGrid::kBarSteps; ++c)
            {
                const int st = (paso0 + c) % largo;
                if (! patPasos[((size_t) (patron - 1) * (size_t) patPasos_ + (size_t) st)
                               * (size_t) patPads + (size_t) pad])
                    continue;
                g.fillRect (caja.getX() + cw * (float) c, y,
                            juce::jmax (1.0f, cw - 0.6f), mh);
            }
        }
    }

    juce::Colour blockColour (int v) const
    {
        if (v > 0 && v != kContinued) return Zati::colour (v - 1);
        if (v < 0)
        {
            const int pad = -v - 1;
            if (zati == nullptr || ! juce::isPositiveAndBelow (pad, zatis))
            {
                ++zatisFueraDeRango;
                //  Su propio color, que es lo unico honesto cuando la tabla no
                //  lo tiene: Zati::colour ya envuelve con el modulo de los ocho.
                return Zati::colour (pad);
            }
            return Zati::colour (zati[pad]);
        }
        return ZatiColours::padBorder;
    }
    //  El bloque que ocupa una celda: su cabeza y su final EXCLUSIVO, o
    //  {-1,-1} si ahi no hay ninguno. La cabeza se busca hacia atras -una
    //  continuacion no dice de quien es- y la cola hacia delante, que es la
    //  misma cuenta que ya hacen el pintado y `resizeSongBlock`.
    std::pair<int, int> bloqueEn (int lane, int bar) const
    {
        if (data == nullptr || lane < 0 || bar < 0 || bar >= totalBars) return { -1, -1 };
        int cabeza = bar;
        while (cabeza > 0 && data[lane * totalBars + cabeza] == kContinued) --cabeza;
        const int v = data[lane * totalBars + cabeza];
        if (v == 0 || v == kContinued) return { -1, -1 };
        int fin = cabeza + 1;
        while (fin < totalBars && data[lane * totalBars + fin] == kContinued) ++fin;
        return { cabeza, fin };
    }

    //  El silencio del BLOQUE al que pertenece una celda, que no es el del
    //  carril: aquel calla la pista entera. Se apunta en la CABEZA, que es
    //  donde el motor lo lee, asi que la cola pregunta por la suya.
    bool bloqueMudo (int lane, int bar) const
    {
        if (lane < 0 || lane >= kLanes || bar < 0 || bar >= 64) return false;
        int cabeza = bar;
        while (cabeza > 0 && data != nullptr
               && data[lane * totalBars + cabeza] == kContinued) --cabeza;
        return ((bmute[(size_t) lane] >> cabeza) & 1u) != 0;
    }

    int findStart (int lane, int bar) const
    {
        for (int b = bar - 1; b >= 0; --b)
            if (data[lane * totalBars + b] != kContinued) return b;
        return -1;
    }

    const int* data = nullptr;
    const int* zati = nullptr;
    //  CUANTOS trae esa tabla. Sin este numero, `zati` es un puntero sin
    //  largo — que es literalmente el fallo que costo la lectura fuera de
    //  rango: se le pasaba la tabla de dieciseis del banco de delante y aqui
    //  se indexaba con un pad de 0 a 63. Un puntero sin su largo no se puede
    //  acotar, asi que tampoco se puede medir.
    int         zatis = 0;
    int totalBars = 8, primerCompas = 0, playing = -1;
    int cursor = -1;          // el compas que las herramientas van a tocar
    unsigned mute = 0;        // un bit por carril silenciado
    //  Y un bit por COMPAS y por carril: el silencio de un bloque suelto. Son
    //  cuatro enteros copiados en cada refresco - la cancion mide sesenta y
    //  cuatro compases clavados, asi que la tabla entera cabe en 32 bytes.
    juce::uint64 bmute[kLanes] {};
    int lA = 0, lB = 0;       // el tramo en bucle, [A,B)

    //  Los pasos de los ocho patrones, y que pads usa cada uno.
    int barsView = kBarsViewDef;

    static constexpr int kMaxPat = 8;
    //  Por debajo de esto no cabe rotulo Y miniatura, asi que el bloque se
    //  queda como estaba: un color y su numero. Medido - en 280x653 el carril
    //  son 20 px y la celda 17, y ahi ocho filas darian dos pixeles cada una
    //  contando el rotulo, o sea una mancha.
    static constexpr float kAltoMini  = 26.0f;
    static constexpr float kFilaRotulo = 10.0f;
    static bool cabeMini (juce::Rectangle<float> cell) noexcept
    {
        return cell.getHeight() >= kAltoMini && cell.getWidth() >= 12.0f;
    }
    const bool* patPasos  = nullptr;
    const int*  patLargos = nullptr;
    int patN = 0, patPasos_ = 0, patPads = 0;
    std::vector<unsigned char> sombraPat;
    std::array<std::vector<int>, kMaxPat> filasDe;

    //  La copia de lo ultimo PINTADO, para no volver a pintarlo. Ver setSource.
    //  El numero de compases lo elige la persona, asi que la sombra de la tabla
    //  crece con ella; los sesenta y cuatro zatis son fijos.
    std::vector<int> sombra;
    std::array<int, 64> sombraZati {};
    int  prevBars = -1, prevPage = -1, prevPlaying = -2, prevCursor = -2, prevLA = -1, prevLB = -1;
    unsigned prevMute = 0xFFFFFFFFu;
    bool visto = false;

    //  LA BANDA DE AUDIO. La tabla se presta y no se copia, igual que `data`.
    //  Si el gesto en curso es de un clip. Se decide al APOYAR y no en cada
    //  evento: un arrastre que empieza en un clip tiene que seguir siendo de
    //  ese clip aunque el dedo pase por encima de un bloque.
    bool             enClip = false;
    std::vector<ClipVista> sombraClips;
    bool             clipsVistos = false;
    const ClipVista* clips = nullptr;
    int              numClips = 0;
    int              clipSel  = -1;   // el que se esta moviendo, para que se vea
    int              arrastrado = -1; // el que este dedo agarro
    //  El asa de un BLOQUE de patron, que no es la de un clip: aquella vive en
    //  `arrastrado`/`asa` y esta en el carril y la cabeza, porque un bloque no
    //  tiene indice - es lo que haya escrito en las celdas.
    int  asaBloque = 0;               // -1 filo izquierdo, +1 derecho, 0 ninguno
    int  bloqueCarril = -1, bloqueCabeza = 0, bloqueFin = 0;
    bool bloquePrimero = true;        // el primer cambio de largo del gesto

    int              agarre = 0;      // por que compas suyo lo agarro
    int              asa = 0;         // -1 filo izquierdo, +1 derecho, 0 el medio
    unsigned         mudoAudio = 0;   // un bit por pista de audio silenciada
    juce::Colour     modoTinte { juce::Colours::transparentBlack };
};
