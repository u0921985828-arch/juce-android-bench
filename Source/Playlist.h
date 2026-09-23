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

    //  Un clip, tal y como esta rejilla necesita verlo: EN PASOS GUARDADOS.
    //  El motor lo guarda en muestras -el audio mide lo que mide- y traducirlo
    //  pide el tempo, que es cosa del motor y no de un componente que dibuja.
    //  Quien sabe cuantas muestras son un paso lo traduce y pasa esto, igual
    //  que la tabla de zatis se pasa en vez de preguntarle al motor por bloque.
    //
    //  EN PASOS Y NO EN COMPASES, que es lo que cambio: mientras la unidad fue
    //  el compas, un clip solo podia empezar en un filo de compas y colocar
    //  una toma donde de verdad entra -a la mitad, en el contratiempo- no
    //  existia como gesto. Medio compas a 120 son 1000 ms. El paso absoluto es
    //  `compas * pasosPorCompas() + paso`, o sea una sola cifra, que es lo que
    //  evita que el dibujo y el dedo hagan la cuenta cada uno por su lado -la
    //  leccion de la canaleta- y lo que deja que el ZOOM de la rejilla cambie
    //  la division sin tocar ni un clip.
    struct ClipVista
    {
        int pista     = 0;   // 0..kAudioLanes-1
        int desdePaso = 0;   // primer paso absoluto que ocupa
        int hastaPaso = 1;   // el primero que YA NO ocupa, o sea [desde, hasta)
        int pad       = 0;   // de que pad salio, que es de donde sale su color
        //  Y LA ONDA DE SU VENTANA DE RECORTE, intercalada min,max por columna.
        //
        //  Se PRESTA igual que la tabla entera: la calcula quien tiene el
        //  buffer -`refreshSong`, en el hilo de mensajes- y la cachea, que es
        //  lo que hace que esto no cueste nada. Recalcularla en `paint` seria
        //  recorrer varios segundos de audio treinta veces por segundo.
        //
        //  Es lo que se pidio: «que las tomas que se graben se vea el audio
        //  facil para poder cortarlo y colocarlo donde debe». Un rectangulo
        //  liso con el numero del pad no dice donde entra el golpe.
        const float* onda = nullptr;
        int columnas      = 0;
    };

    //  UN BLOQUE DE PATRON, TAL Y COMO ESTA REJILLA LO VE: en pasos, como un
    //  clip. Era una celda por compas (`kContinued` para la cola) y con eso un
    //  bloque no podia empezar a mitad de compas ni partirse por un paso: dos
    //  cabezas no caben en la misma celda. Quien lo publica es la ficha, desde
    //  su lista, y aqui se presta igual que los clips.
    struct BloqueVista
    {
        int  lane      = 0;
        int  desdePaso = 0;   // primer paso absoluto que ocupa
        int  hastaPaso = 1;   // el primero que YA NO ocupa
        int  bank      = 0;   // 0..7 patron; < 0 golpe suelto -(pad+1)
        int  offset    = 0;   // paso del patron con el que arranca
        bool mudo      = false;
    };

    //  (lane, paso absoluto PEGADO a la division): el lapiz pone un bloque y
    //  la goma quita el que haya debajo. En pasos y no en compases desde que
    //  un bloque puede vivir a mitad de compas: con el compas la goma no
    //  encontraba un bloque de ocho pasos que empezara en el ocho.
    std::function<void (int lane, int paso)> onCell;

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
    //  Por INDICE de la tabla que se paso y en PASOS, como los clips.
    std::function<void (int indice, int desdePaso, int hastaPaso, bool primero)> onLargoBloque;

    //  MOVER un bloque entero y SILENCIARLO, que son las dos herramientas que
    //  esta rejilla no tenia y que un playlist de verdad si: probar una cancion
    //  sin el estribillo, o correrlo dos compases, se hacia borrando y
    //  volviendo a escribir.
    std::function<void (int indice, int carrilNuevo, int desdePaso, bool primero)> onMueveBloque;
    std::function<void (int indice)> onMuteBloque;
    //  LAS TIJERAS PARTEN UN BLOQUE por el paso pegado, como parten un clip.
    //  Antes caian en `onCell` y lo BORRABAN: unas tijeras que borran son una
    //  goma con otro dibujo.
    std::function<void (int indice, int paso)> onParteBloque;

    //  LA SELECCION, calcada del piano: una banda carriles x pasos que se
    //  marca arrastrando sobre vacio con SEL, se mueve arrastrando desde
    //  dentro, y se vacia tocando fuera. Quien tiene los datos es la ficha;
    //  aqui solo se sabe la geometria, igual que en PianoRoll.
    std::function<void (int lane0, int paso0, int lane1, int paso1)> onBanda;
    std::function<void (int dLane, int dPaso)> onMueveSel;
    std::function<void()> onVaciaSel;
    //  Y DONDE SE PEGA: un toque con SEL sin arrastrar marca (carril, paso).
    //  Sin esto, PEGAR no tendria donde ir salvo al principio de la vista.
    std::function<void (int lane, int paso)> onMarca;
    //  LA LUPA: arrastrar marca un tramo en compases y al soltar la vista se
    //  acerca a el; un toque sin arrastre vuelve a la vista anterior. Es una
    //  herramienta armada y no un pellizco por la regla de la casa: un
    //  pellizco no se puede medir por la tapa.
    std::function<void (int compas0, int compas1)> onLupa;
    std::function<void()> onLupaVuelve;
    //  EL DEDO SE LEVANTO. Cierra el arrastre para quien cuenta entradas de
    //  deshacer. Ver mouseUp.
    std::function<void()> onSuelta;
    //  UN HUECO DE LA BANDA DE AUDIO: aqui no habia nada, pon lo que tengas.
    //  El sitio llega en PASO ABSOLUTO y ya PEGADO a la division que se ve
    //  dibujada: pegar aqui y no en el anfitrion es lo que garantiza que el
    //  clip caiga en la raya que la persona tenia debajo del dedo.
    std::function<void (int pista, int paso)> onClipNuevo;
    //  Y UN CLIP QUE SE ARRASTRA. El indice es el de la tabla que se paso, no
    //  una identidad: quien la publica es quien la ordena.
    std::function<void (int indice, int pista, int paso)> onClipMueve;
    std::function<void (int indice)> onClipQuita;
    //  PARTIR UN CLIP POR UN PASO ABSOLUTO. Lo que sale son dos clips que
    //  suman el original: ni se reescribe audio ni se crea un pad, que es lo
    //  que el troceado de la ficha si hace y por lo que no vale aqui.
    std::function<void (int indice, int paso)> onClipParte;
    //  Y EL ATAJO A CORTAR con un doble toque. Hoy el camino desde un bloque
    //  de la cancion hasta el troceado de su sonido es cerrar la playlist,
    //  buscar el pad, abrir su ficha y bajar a la pagina RIG: cuatro pasos
    //  para una cosa que se esta mirando.
    std::function<void (int indice)> onClipChop;
    //  Y EL LARGO, arrastrando un filo. Llega en PASOS porque es lo que esta
    //  rejilla sabe: quien traduce a muestras es quien tiene el tempo.
    std::function<void (int indice, int desdePaso, int hastaPaso)> onClipLargo;
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

    //  CUANTOS PASOS GUARDADOS TIENE UN COMPAS, que lo dice el MOTOR.
    //
    //  Escribir aqui un dieciseis seria la tercera copia de esa regla, y ya
    //  costo una: `AudioEngine::kBarSteps` y `StepGrid::kBarSteps` valian 16 a
    //  mano mientras `pasosPorCompas()` devolvia entre 8 y 64 segun la rejilla,
    //  asi que fuera de 1/16 el clip sonaba donde no se dibujaba. El dueño es
    //  `pasosPorCompas()` y aqui solo se guarda lo que el diga.
    void setPasosCompas (int n) noexcept
    {
        n = juce::jlimit (1, 256, n);
        if (n == pasosCompas) return;
        pasosCompas = n;
        repaint();
    }
    int getPasosCompas() const noexcept { return pasosCompas; }

    //  LA DIVISION QUE SE DIBUJA Y A LA QUE SE PEGA, en pasos. La MISMA para
    //  las dos cosas a proposito: una raya que se ve y a la que no se puede
    //  pegar miente, y pegarse a una que no se ve es un clip que salta solo.
    //
    //  No es `pasosCompas` a secas porque con la rejilla en 1/64 y ocho
    //  compases a la vista salen 512 rayas en ~370 px -1.4 px cada una, o sea
    //  un tramado gris- y ademas ningun dedo acierta una. Se va dividiendo por
    //  dos -que respeta los tresillos: 12 da 6, 3 y 1, todos divisores- hasta
    //  que la celda llega al aire de la casa, que es el minimo por debajo del
    //  cual dos rayas dejan de leerse como dos.
    int divisionPaso() const noexcept
    {
        const int pc   = juce::jmax (1, pasosCompas);
        const float bW = (float) juce::jmax (0, getWidth() - kGutter)
                       / (float) juce::jmax (1, barsView);
        int d = pc;
        while (d > 1 && bW / (float) d < (float) Metrics::gap)
            d = (d % 2 == 0) ? d / 2 : 1;
        return juce::jmax (1, pc / juce::jmax (1, d));
    }

    //  EL PASO ABSOLUTO QUE CAE BAJO UNA X, sin pegar. Lo usan el dedo y las
    //  asas; quien quiera el sitio donde se suelta pide `pasoPegado`.
    int pasoDeX (int x) const noexcept
    {
        auto r = getLocalBounds();
        const int   pc   = juce::jmax (1, pasosCompas);
        const float pasoW = (float) (r.getWidth() - kGutter)
                          / (float) juce::jmax (1, barsView) / (float) pc;
        if (pasoW <= 0.0f) return primerCompas * pc;
        return primerCompas * pc
             + juce::jlimit (0, barsView * pc - 1,
                             (int) ((float) (x - r.getX() - kGutter) / pasoW));
    }

    int pasoPegado (int x) const noexcept
    {
        const int u = juce::jmax (1, divisionPaso());
        return (pasoDeX (x) / u) * u;
    }

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

    void setSource (const BloqueVista* filas, int cuantas,   // los bloques, prestados
                    const int* zatiOf,      // un zati por pad...
                    int numZatis,           // ...y CUANTOS, que es la mitad que faltaba
                    int bars, int desdeCompas, int playBar,
                    int cursorBar = -1,     // el compas sobre el que actuan las herramientas
                    unsigned mudos = 0,     // un bit por carril silenciado
                    int loopA = 0, int loopB = 0)   // el tramo en bucle, [A,B) en compases
    {
        bloques = filas; numBloques = juce::jmax (0, cuantas);
        zati = zatiOf; zatis = numZatis;
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

        //  REPINTAR SOLO SI HA CAMBIADO ALGO. Por lo mismo que la rejilla de
        //  pasos: el temporizador llama aqui treinta veces por segundo
        //  mientras la ficha CANCION esta abierta, y lo unico que se mueve
        //  solo es el compas que suena. La tabla la cambias tu, con el dedo.
        //
        //  Y la ficha es translucida y ocupa la ventana entera, asi que un
        //  repintado de esta rejilla arrastra el chasis, los dieciseis pads y
        //  los cuarenta controles que hay debajo del velo.
        bool igual = visto
                  && totalBars == prevBars && primerCompas == prevPage && playing == prevPlaying
                  && cursor == prevCursor && mute == prevMute
                  && lA == prevLA && lB == prevLB
                  && (int) sombra.size() == numBloques
                  && (numBloques == 0
                      || std::memcmp (sombra.data(), bloques, (size_t) numBloques * sizeof (BloqueVista)) == 0)
                  && (zati == nullptr
                        || std::memcmp (sombraZati.data(), zati, sizeof (sombraZati)) == 0);

        if (igual) return;

        sombra.assign (bloques, bloques + numBloques);
        if (zati != nullptr) std::memcpy (sombraZati.data(), zati, sizeof (sombraZati));
        prevBars = totalBars; prevPage = primerCompas; prevPlaying = playing;
        prevCursor = cursor; prevMute = mute; prevLA = lA; prevLB = lB;
        visto = true;

        repaint();
    }

    //  LA BANDA SELECCIONADA y el punto de pegado, que los tiene la ficha y
    //  aqui solo se pintan. `lane0 < 0` es «sin seleccion».
    struct Sel { int lane0 = -1, lane1 = -1, paso0 = 0, paso1 = 0;
                 bool activa() const noexcept { return lane0 >= 0 && paso1 > paso0; } };
    void setSel (Sel nueva)
    {
        if (nueva.lane0 == sel.lane0 && nueva.lane1 == sel.lane1
            && nueva.paso0 == sel.paso0 && nueva.paso1 == sel.paso1) return;
        sel = nueva; repaint();
    }
    Sel  getSel() const noexcept { return sel; }
    void setMarca (int lane, int paso)
    {
        if (lane == marcaLane && paso == marcaPaso) return;
        marcaLane = lane; marcaPaso = paso; repaint();
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
        //  DESDE UNO y no desde cuatro: la lupa acerca «ese trozo», y el
        //  trozo puede ser un compas. El techo de arriba lo sigue diciendo
        //  `cabeVista`; el de abajo era una cifra que sobraba.
        n = juce::jlimit (1, kBarsViewMax, n);
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
        if (! visto) { marcoDelModo (g); return; }
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

            //  LOS HUECOS PRIMERO, compas a compas: un compas vacio es un
            //  hueco, y un hueco oscurece. Pasado el final de la cancion, mas.
            for (int c = 0; c < barsView; ++c)
            {
                const int bar = base + c;
                const float x = (float) r.getX() + (float) gutter + barW * (float) c;
                auto cell = juce::Rectangle<float> (x, y, barW, laneH).reduced (1.5f);
                g.setColour (bar >= totalBars ? ZatiColours::groove (0.30f)
                                              : ZatiColours::groove ((bar % 4 == 0) ? 0.48f : 0.28f));
                g.fillRect (cell);
            }
        }

        //  Y LOS BLOQUES ENCIMA, EN PASOS: un bloque de ocho pasos ocupa
        //  ocho subceldas, y uno de cuatro compases es UNA caja y no cuatro
        //  copias -que es lo que `kContinued` conseguia a base de celdas-.
        {
            const int pc   = juce::jmax (1, pasosCompas);
            const int base0 = base * pc;
            const int tope  = base0 + barsView * pc;
            for (int i = 0; i < numBloques; ++i)
            {
                const BloqueVista& b = bloques[i];
                if (! juce::isPositiveAndBelow (b.lane, kLanes)) continue;
                const int d = juce::jmax (b.desdePaso, base0);
                const int h = juce::jmin (b.hastaPaso, tope);
                if (h <= d) continue;

                const bool mudo  = (mute & (1u << (unsigned) b.lane)) != 0;
                const bool mudoB = mudo || b.mudo;
                const auto col   = blockColour (b.bank);
                auto cell = cajaPaso (b.lane, d, h);
                cell = cell.reduced (juce::jmin (1.5f, cell.getWidth() * 0.25f), 1.5f);
                //  Un carril silenciado ensena sus bloques HUECOS: siguen
                //  ahi, con su color y su nombre, y no suenan. Borrarlos
                //  seria otra cosa, y esa ya existe. Y un bloque silenciado
                //  SOLO se ve igual: un mute que no se distingue de sonar no
                //  es un mute, es un boton.
                if (mudoB)
                {
                    g.setColour (ZatiColours::groove (0.30f));
                    g.fillRect (cell);
                    g.setColour (col.withAlpha (0.70f));
                    g.drawRect (cell, Metrics::filo);
                }
                else
                {
                    //  EL FONDO DEL BLOQUE SE HUNDE cuando lleva miniatura: el
                    //  bloque es del color del PATRON y las marcas del color de
                    //  cada PAD, y dos colores plenos uno sobre otro no se
                    //  separan. Con la tapa al 30% el bloque sigue diciendo
                    //  cual es y las marcas se leen encima.
                    g.setColour (cabeMini (cell) ? col.withAlpha (0.30f) : col);
                    g.fillRect (cell);
                    if (cabeMini (cell))
                    {
                        g.setColour (col.withAlpha (0.85f));
                        g.drawRect (cell, Metrics::filo);
                    }
                }

                //  LO QUE EL BLOQUE LLEVA DENTRO. Solo un patron -un golpe
                //  suelto no tiene pasos que enseñar- y solo donde cabe.
                juce::Rectangle<float> texto = cell;
                if (b.bank >= 0 && cabeMini (cell))
                {
                    auto dentro = cell.reduced (2.0f);
                    texto = dentro.removeFromTop (kFilaRotulo);
                    pintaPasos (g, dentro, b, d, h, mudoB);
                }

                //  Y LAS ASAS, donde se pueden coger: en los dos filos si el
                //  bloque mide tres compases o mas, como un clip.
                if ((b.hastaPaso - b.desdePaso) >= kCompasesConAsa * pc && ! mudoB)
                {
                    g.setColour (ZatiColours::bestOn (col, ZatiColours::ink, juce::Colours::white)
                                     .withAlpha (0.55f));
                    g.fillRect (cell.getX() + 2.0f, cell.getY() + 3.0f, 2.0f, cell.getHeight() - 6.0f);
                    g.fillRect (cell.getRight() - 4.0f, cell.getY() + 3.0f, 2.0f, cell.getHeight() - 6.0f);
                }

                g.setColour (mudoB ? col.withAlpha (0.85f)
                                   : ZatiColours::bestOn (col, ZatiColours::ink, juce::Colours::white));
                g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
                g.drawText (b.bank >= 0 ? "P" + juce::String (b.bank + 1)
                                        : juce::String (-b.bank).paddedLeft ('0', 2),
                            texto,
                            texto == cell ? juce::Justification::centred
                                          : juce::Justification::centredLeft);
            }
        }

        //  EL COMPAS QUE SUENA, en el color del cabezal y no en el de grabar,
        //  en los cuatro carriles: tinta fuera, blanco dentro, que se lee
        //  sobre una tarjeta clara y sobre un bloque lleno.
        if (playing >= base && playing < base + barsView)
        {
            const float x = (float) r.getX() + (float) gutter + barW * (float) (playing - base);
            for (int lane = 0; lane < kLanes; ++lane)
            {
                auto cell = juce::Rectangle<float> (x, (float) r.getY() + laneH * (float) lane, barW, laneH).reduced (1.5f);
                g.setColour (ZatiColours::playheadEdge);
                g.drawRect (cell.expanded (1.0f), Metrics::filo);
                g.setColour (ZatiColours::playhead);
                g.drawRect (cell, Metrics::filoFoco);
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
            g.drawRect (juce::Rectangle<float> (x, (float) r.getY(), barW, (float) r.getHeight()), Metrics::filoFoco);
        }

        //  Y LOS CLIPS ENCIMA, en los MISMOS carriles.
        //
        //  Encima y no al lado: un clip y un bloque de patron pueden caer en la
        //  misma celda —nada lo impide y en FL tampoco— y entonces el clip gana
        //  el dibujo y gana el dedo. Es una eleccion y no un descuido: el
        //  patron de debajo sigue sonando, que es lo que el motor hace desde
        //  que los dos se renderizan en el mismo bucle de segmento, y lo que no
        //  puede pasar es que el dibujo diga una cosa y el toque haga otra.
        //  LA REJILLA DENTRO DEL COMPAS, que es lo que se pidio: «en la
        //  playlist tambien poder editar las cuadriculas».
        //
        //  Debajo de los clips y encima de las celdas: son la referencia con
        //  la que se coloca una toma, asi que taparlas con el clip que se esta
        //  colocando seria dibujar justo lo contrario de lo que sirve. Y la
        //  division sale de `divisionPaso()`, la MISMA que usa el dedo: la
        //  leccion de la canaleta es que cuando el dibujo y el toque hacen la
        //  cuenta cada uno por su lado, un dia dejan de coincidir.
        {
            const int pc = juce::jmax (1, pasosCompas);
            const int u  = juce::jmax (1, divisionPaso());
            if (u < pc)
            {
                const float w = barW * (float) u / (float) pc;
                g.setColour (ZatiColours::markOn (ZatiColours::chassisTop, 0.10f));
                for (int c = 0; c < barsView; ++c)
                {
                    const float x0 = (float) r.getX() + gutter + barW * (float) c;
                    for (float k = w; k < barW - 0.5f; k += w)
                        g.fillRect (x0 + k, (float) r.getY(), 1.0f, (float) r.getHeight());
                }
            }
        }

        pintaClips (g);

        //  LA SELECCION SE VE POR RELLENO, como en el piano: el cabezal al
        //  55 % sobre lo que haya debajo, bloques y clips incluidos, que es lo
        //  que dice «esto es lo que se va a copiar».
        if (sel.activa())
        {
            const int pc = juce::jmax (1, pasosCompas);
            const int d = juce::jmax (sel.paso0, base * pc);
            const int h = juce::jmin (sel.paso1, (base + barsView) * pc);
            const int l0 = juce::jlimit (0, kLanes - 1, juce::jmin (sel.lane0, sel.lane1));
            const int l1 = juce::jlimit (0, kLanes - 1, juce::jmax (sel.lane0, sel.lane1));
            if (h > d)
            {
                auto caja = cajaPaso (l0, d, h);
                caja.setHeight (laneH * (float) (l1 - l0 + 1));
                g.setColour (ZatiColours::playhead.withAlpha (0.55f));
                g.fillRect (caja);
                g.setColour (ZatiColours::playheadEdge);
                g.drawRect (caja, Metrics::filo);
            }
        }
        //  Y EL PUNTO DE PEGADO, una raya vertical del cabezal en su carril.
        if (marcaLane >= 0 && marcaLane < kLanes)
        {
            const int pc = juce::jmax (1, pasosCompas);
            if (marcaPaso >= base * pc && marcaPaso < (base + barsView) * pc)
            {
                auto caja = cajaPaso (marcaLane, marcaPaso, marcaPaso + 1);
                g.setColour (ZatiColours::playhead);
                g.fillRect (caja.getX(), caja.getY(), 2.0f, caja.getHeight());
            }
        }
        //  Y EL TRAMO DE LA LUPA mientras se arrastra.
        if (lupaViva && lupaX1 != lupaX0)
        {
            const float x0 = (float) juce::jmin (lupaX0, lupaX1), x1 = (float) juce::jmax (lupaX0, lupaX1);
            g.setColour (ZatiColours::playhead.withAlpha (0.25f));
            g.fillRect (x0, (float) r.getY(), x1 - x0, (float) r.getHeight());
            g.setColour (ZatiColours::playheadEdge);
            g.drawRect (juce::Rectangle<float> (x0, (float) r.getY(), x1 - x0, (float) r.getHeight()), Metrics::filo);
        }

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
        g.drawRect (marco, Metrics::filo);
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
        const int pista = juce::jlimit (0, kAudioLanes - 1, (int) ((float) (e.y - r.getY()) / pistaH));
        return clipEn (pista, pasoDeX (e.x)) >= 0;
    }

    bool tocaAlClip (const juce::MouseEvent& e) const
    {
        if (herramienta == hSel || herramienta == hLupa) return false;
        if (herramienta == hMano || herramienta == hGoma || herramienta == hTijeras)
            return clipBajoElDedo (e);
        //  Con el LAPIZ y la brocha en CLIP, un hueco suelta un clip; encima de
        //  uno que ya esta, el lapiz no tiene nada que decir.
        if (herramienta == hLapiz && pincelClip) return ! clipBajoElDedo (e);
        return false;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        asaBloque = 0; bloqueIdx = -1; arrastrado = -1; asa = 0;
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
    void mouseUp   (const juce::MouseEvent& e)   override
    {
        asaBloque = 0;
        bloqueIdx = -1;
        ultima = { -1, -1 };
        arrastrado = -1;
        enClip = false;
        moviendoSel = false;
        //  Y SE AVISA DE QUE EL DEDO SE HA LEVANTADO. Quien escucha apila UNA
        //  entrada de deshacer por arrastre -el lapiz emite un evento por celda
        //  cruzada, asi que uno por evento no es deshacer, es contar- y sin
        //  esta señal el candado no se abre nunca: el segundo trazo se
        //  quedaria sin poder deshacerse.
        if (onSuelta) onSuelta();
        //  LA LUPA DECIDE AL SOLTAR: con tramo, acerca; sin el, vuelve.
        if (lupaViva)
        {
            lupaViva = false;
            const int pc = juce::jmax (1, pasosCompas);
            const int c0 = pasoDeX (juce::jmin (lupaX0, e.x)) / pc;
            const int c1 = pasoDeX (juce::jmax (lupaX0, e.x)) / pc;
            //  Un arrastre mas corto que el aire de la casa es un toque con
            //  el dedo temblando, no un tramo.
            if (std::abs (e.x - lupaX0) < Metrics::gap) { if (onLupaVuelve) onLupaVuelve(); }
            else if (onLupa) onLupa (c0, c1 + 1);
            repaint();
        }
    }

    //  DOBLE TOQUE EN UN CLIP: abre CORTAR con SU sonido.
    //
    //  Doble toque y no mantener, que es el gesto que esta rejilla no usa para
    //  nada: mantener ya lo ha pedido el bloque de patron, y arrastrar es
    //  pintar. Y no es una herramienta mas porque no es un MODO -no se repite,
    //  se hace una vez y se cambia de pantalla-, que es la misma razon por la
    //  que COPIAR y PEGAR son tapas y no herramientas.
    //
    //  Funciona con cualquier herramienta armada a proposito: el camino de hoy
    //  -cerrar la playlist, buscar el pad, ficha, pagina RIG- son cuatro pasos
    //  para una cosa que se esta mirando, y obligar ademas a armar una
    //  herramienta serian cinco.
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! clipBajoElDedo (e)) return;
        auto r = getLocalBounds();
        const float pistaH = (float) r.getHeight() / (float) kAudioLanes;
        if (pistaH <= 0.0f) return;
        const int pista = juce::jlimit (0, kAudioLanes - 1, (int) ((float) (e.y - r.getY()) / pistaH));
        const int i = clipEn (pista, pasoDeX (e.x));
        if (i >= 0 && onClipChop) onClipChop (i);
    }

    void toca (const juce::MouseEvent& e, bool arrastrando)
    {
        if (! visto) return;
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
        if (barW <= 0.0f || laneH <= 0.0f) return;
        const int pc   = juce::jmax (1, pasosCompas);
        const int u    = juce::jmax (1, divisionPaso());
        const int lane = juce::jlimit (0, kLanes - 1, (int) ((float) (e.y - r.getY()) / laneH));
        //  DOS CIFRAS Y NO UNA: donde esta el dedo y donde se SUELTA, por lo
        //  mismo que en los clips. El dedo sin pegar decide que hay debajo;
        //  el pegado es lo unico que se escribe.
        const int paso   = pasoDeX (e.x);
        const int pegado = (paso / u) * u;
        if (paso / pc >= totalBars) return;

        //  LA LUPA: un tramo horizontal, y se decide al soltar.
        if (herramienta == hLupa)
        {
            if (! arrastrando) { lupaViva = true; lupaX0 = e.x; }
            lupaX1 = e.x;
            repaint();
            return;
        }

        //  LA SELECCION, calcada del piano: apoyar dentro mueve, apoyar
        //  fuera vacia y marca el punto de pegado, y arrastrar desde fuera
        //  marca una banda.
        if (herramienta == hSel)
        {
            if (! arrastrando)
            {
                selLaneIni = lane; selPasoIni = pegado;
                moviendoSel = sel.activa()
                           && lane >= juce::jmin (sel.lane0, sel.lane1) && lane <= juce::jmax (sel.lane0, sel.lane1)
                           && paso >= sel.paso0 && paso < sel.paso1;
                dLaneSel = dPasoSel = 0;
                if (! moviendoSel)
                {
                    if (onVaciaSel) onVaciaSel();
                    if (onMarca) onMarca (lane, pegado);
                }
                return;
            }
            if (moviendoSel)
            {
                //  EN DELTAS Y ACUMULADO, no en absolutos: quien mueve la
                //  banda necesita cuanto se ha movido DESDE la ultima vez, o
                //  cada evento del raton la desplazaria otra vez entera.
                const int nl = lane - selLaneIni, np = pegado - selPasoIni;
                if (nl == dLaneSel && np == dPasoSel) return;
                if (onMueveSel) onMueveSel (nl - dLaneSel, np - dPasoSel);
                dLaneSel = nl; dPasoSel = np;
                return;
            }
            //  La banda va de division en division y coge la ultima entera.
            if (onBanda) onBanda (selLaneIni, juce::jmin (selPasoIni, pegado),
                                  lane, juce::jmax (selPasoIni, pegado) + u);
            return;
        }

        //  SILENCIAR UN BLOQUE: un toque, y solo un toque. Arrastrar por una
        //  fila silenciandolos todos es lo mismo que la canaleta ya tiene
        //  prohibido y por la misma razon: un roce se llevaria la cancion.
        if (herramienta == hMute)
        {
            if (! arrastrando && onMuteBloque)
            {
                const int i = bloqueEn (lane, paso);
                if (i >= 0) onMuteBloque (i);
            }
            return;
        }

        //  LAS TIJERAS PARTEN el bloque por el paso pegado. Un toque en el
        //  filo no es un corte, y eso lo decide quien tiene los datos.
        if (herramienta == hTijeras)
        {
            if (! arrastrando && onParteBloque)
            {
                const int i = bloqueEn (lane, paso);
                if (i >= 0) onParteBloque (i, pegado);
            }
            return;
        }

        //  LA MANO: EL FILO ESTIRA Y EL MEDIO MUEVE.
        //
        //  Las dos viven aqui y no en el lapiz, que es lo que hace que no
        //  cuesten el pincel: con el lapiz armado esta rejilla se comporta
        //  exactamente como siempre. Con la mano cada gesto tiene UN
        //  significado. Es la leccion de la SELECCION del piano: la
        //  herramienta es la quinta, no un gesto nuevo.
        //
        //  Y el asa con las dos condiciones que la vista de audio ya midio:
        //  un tercio del bloque por lado y como mucho un compas, y POR DEBAJO
        //  DE TRES COMPASES no hay asas - dos asas se comen un bloque corto y
        //  no queda nada que arrastrar.
        if (herramienta == hMano)
        {
            if (! arrastrando)
            {
                asaBloque = 0;
                bloqueIdx = -1;
                bloquePrimero = true;
                const int i = bloqueEn (lane, paso);
                if (i < 0) return;
                const auto& b = bloques[i];
                bloqueIdx   = i;
                bloqueCarril = b.lane;
                bloqueDesde = b.desdePaso;
                bloqueHasta = b.hastaPaso;
                const int ancho = b.hastaPaso - b.desdePaso;
                if (ancho >= kCompasesConAsa * pc)
                {
                    const int asaP = juce::jlimit (1, juce::jmax (1, ancho / 3), pc);
                    if (paso < b.desdePaso + asaP)       asaBloque = -1;
                    else if (paso >= b.hastaPaso - asaP) asaBloque = +1;
                }
                //  DONDE SE AGARRO, en divisiones desde el principio. Sin
                //  esto, arrastrar un bloque de cuatro por su tercer compas lo
                //  pega de un salto por su primero.
                agarre = ((paso - b.desdePaso) / u) * u;
                return;
            }

            if (bloqueIdx < 0 || bloqueIdx >= numBloques) return;

            if (asaBloque != 0)
            {
                //  UN ASA CAMBIA EL LARGO Y NO LA POSICION. Por el filo
                //  izquierdo lo que se mueve es el principio y el bloque
                //  sigue sonando por donde iba (eso lo resuelve la ficha con
                //  el offset); por el derecho, el final.
                int d = bloqueDesde, h = bloqueHasta;
                if (asaBloque < 0) d = juce::jmin (pegado, h - u);
                else               h = juce::jmax (pegado + u, d + u);
                if (d == bloqueDesde && h == bloqueHasta) return;
                if (onLargoBloque)
                {
                    onLargoBloque (bloqueIdx, d, h, bloquePrimero);
                    bloquePrimero = false;
                    bloqueDesde = d; bloqueHasta = h;
                }
                return;
            }

            const int destino = juce::jmax (0, pegado - agarre);
            if (onMueveBloque && (destino != bloqueDesde || lane != bloqueCarril))
            {
                onMueveBloque (bloqueIdx, lane, destino, bloquePrimero);
                bloquePrimero = false;
                bloqueHasta  = destino + (bloqueHasta - bloqueDesde);
                bloqueDesde  = destino;
                bloqueCarril = lane;
            }
            return;
        }

        //  El lapiz y la goma, por celda de division: una celda no se
        //  escribe dos veces seguidas, o pasar el dedo por encima de la
        //  misma la enciende y la apaga a la velocidad del raton.
        if (! onCell) return;
        if (arrastrando && lane == ultima.first && pegado == ultima.second) return;
        ultima = { lane, pegado };
        onCell (lane, pegado);
    }

    //  ------------------------------------------------------------------
    //  LA BANDA DE AUDIO
    //  ------------------------------------------------------------------

    //  El rectangulo de un compas en una pista, con la MISMA cuenta que la
    //  vista de patrones. Escrita una vez y usada por el pintado y por el
    //  gesto, que es la leccion de la canaleta: cuando el dibujo y el toque
    //  hacen la cuenta cada uno por su lado, un dia dejan de coincidir.
    juce::Rectangle<float> cajaPaso (int pista, int desdePaso, int hastaPaso) const
    {
        auto r = getLocalBounds();
        const int   pc     = juce::jmax (1, pasosCompas);
        const float pistaH = (float) r.getHeight() / (float) kAudioLanes;
        const float barW   = (float) (r.getWidth() - kGutter) / (float) barsView;
        const float pasoW  = barW / (float) pc;
        return { (float) r.getX() + (float) kGutter
                     + pasoW * (float) (desdePaso - primerCompas * pc),
                 (float) r.getY() + pistaH * (float) pista,
                 pasoW * (float) juce::jmax (1, hastaPaso - desdePaso), pistaH };
    }

    //  LOS CLIPS, DESPUES DE LOS BLOQUES y no celda a celda: un clip de cuatro
    //  compases es UN bloque y no cuatro copias del mismo, que es exactamente
    //  lo que `kContinued` existe para conseguir con los patrones. Aqui sale
    //  gratis porque el clip ya sabe donde acaba.
    void pintaClips (juce::Graphics& g)
    {
        auto r = getLocalBounds();
        const int pc   = juce::jmax (1, pasosCompas);
        const int base = primerCompas * pc;
        const int tope = base + barsView * pc;

        for (int i = 0; i < numClips; ++i)
        {
            const ClipVista& c = clips[i];
            if (! juce::isPositiveAndBelow (c.pista, kAudioLanes)) continue;
            const int d = juce::jmax (c.desdePaso, base);
            const int h = juce::jmin (c.hastaPaso, tope);
            if (h <= d) continue;                       // no cae en esta pagina

            //  EL AIRE SE ENCOGE ANTES QUE EL CLIP. Con la unidad en compases
            //  un clip nunca bajaba de una celda entera, asi que quitarle 1.5
            //  px por lado era gratis; en pasos, un clip de una division en
            //  1/64 mide ~5 px y los tres px de aire lo dejaban en dos -o en
            //  ancho negativo, que JUCE dibuja como nada. Un clip invisible es
            //  un clip que no se puede agarrar para deshacerlo.
            auto caja = cajaPaso (c.pista, d, h);
            caja = caja.reduced (juce::jmin (1.5f, caja.getWidth() * 0.25f), 1.5f);
            const auto col = (zati != nullptr && juce::isPositiveAndBelow (c.pad, zatis))
                                 ? Zati::colour (zati[c.pad]) : Zati::colour (c.pad);
            const bool mudo = (mudoAudio & (1u << (unsigned) c.pista)) != 0;

            if (mudo)
            {
                g.setColour (ZatiColours::groove (0.30f)); g.fillRect (caja);
                g.setColour (col.withAlpha (0.70f));       g.drawRect (caja, Metrics::filo);
            }
            else
            {
                g.setColour (col); g.fillRect (caja);
            }

            //  EL QUE SE ESTA MOVIENDO SE VE. Sin marca, arrastrar en una banda
            //  de cuatro pistas es soltar y buscar cual se movio.
            if (i == clipSel)
            {
                g.setColour (ZatiColours::playheadEdge); g.drawRect (caja.expanded (1.0f), Metrics::filo);
                g.setColour (ZatiColours::playhead);     g.drawRect (caja, Metrics::filoFoco);
            }

            //  Y LAS ASAS SE DIBUJAN donde se pueden coger: dos filos verticales
            //  en el primer y el ultimo compas. Un asa que existe y no se ve es
            //  un gesto que nadie encuentra, y una que se ve donde no existe
            //  -en un clip corto- es peor.
            if ((c.hastaPaso - c.desdePaso) >= kCompasesConAsa * pc)
            {
                g.setColour (ZatiColours::bestOn (col, ZatiColours::ink, juce::Colours::white)
                                 .withAlpha (0.55f));
                g.fillRect (caja.getX() + 2.0f, caja.getY() + 3.0f, 2.0f, caja.getHeight() - 6.0f);
                g.fillRect (caja.getRight() - 4.0f, caja.getY() + 3.0f, 2.0f, caja.getHeight() - 6.0f);
            }

            //  LA ONDA, DENTRO DEL BLOQUE Y SOBRE EL CLIP ENTERO.
            //
            //  Las columnas se reparten sobre la caja del clip COMPLETO y no
            //  sobre el trozo visible: si se repartieran sobre lo que se ve, el
            //  mismo clip dibujaria una onda distinta segun por donde este
            //  cortado por el borde de la pagina, que es justo lo que impide
            //  usarla para colocar nada. Las columnas que caen fuera se saltan.
            if (c.onda != nullptr && c.columnas > 0)
            {
                const auto todo = cajaPaso (c.pista, c.desdePaso, c.hastaPaso);
                const float w   = todo.getWidth() / (float) c.columnas;
                if (w > 0.05f)
                {
                    const float medio = caja.getCentreY();
                    const float alto  = caja.getHeight() * 0.5f - 2.0f;
                    g.setColour ((mudo ? col : ZatiColours::bestOn (col, ZatiColours::ink,
                                                                   juce::Colours::white))
                                     .withAlpha (0.55f));
                    for (int k = 0; k < c.columnas; ++k)
                    {
                        const float x = todo.getX() + w * (float) k;
                        if (x + w <= caja.getX() || x >= caja.getRight()) continue;
                        const float lo = c.onda[k * 2], hi = c.onda[k * 2 + 1];
                        const float y0 = medio - hi * alto;
                        const float y1 = medio - lo * alto;
                        g.fillRect (x, y0, juce::jmax (1.0f, w - 0.5f),
                                    juce::jmax (1.0f, y1 - y0));
                    }
                }
            }

            g.setColour (mudo ? col.withAlpha (0.85f)
                              : ZatiColours::bestOn (col, ZatiColours::ink, juce::Colours::white));
            g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
            g.drawText (juce::String (c.pad + 1).paddedLeft ('0', 2), caja, juce::Justification::centred);
        }
    }

    //  Que clip cae bajo (pista, paso). El PRIMERO que lo contenga: dos clips
    //  encima no es un estado que esta app produzca, y elegir "el de arriba"
    //  sin que haya arriba seria inventarse una regla.
    int clipEn (int pista, int paso) const
    {
        for (int i = 0; i < numClips; ++i)
            if (clips[i].pista == pista
                && paso >= clips[i].desdePaso && paso < clips[i].hastaPaso)
                return i;
        return -1;
    }

    void tocaAudio (const juce::MouseEvent& e, bool arrastrando)
    {
        auto r = getLocalBounds();
        const float pistaH = (float) r.getHeight() / (float) kAudioLanes;
        const float barW   = (float) (r.getWidth() - kGutter) / (float) barsView;
        if (barW <= 0.0f || pistaH <= 0.0f) return;

        const int pc = juce::jmax (1, pasosCompas);
        const int u  = juce::jmax (1, divisionPaso());
        const int pista  = juce::jlimit (0, kAudioLanes - 1, (int) ((float) (e.y - r.getY()) / pistaH));
        //  DOS CIFRAS Y NO UNA: donde esta el dedo y donde se SUELTA.
        //
        //  El dedo sin pegar es lo que decide que hay debajo -un asa, el
        //  interior del clip, un hueco- y el pegado es lo unico que se escribe.
        //  Confundirlas hace que agarrar un clip por su mitad lo mueva media
        //  division antes de que nadie arrastre nada.
        const int paso   = pasoDeX (e.x);
        const int pegado = (paso / u) * u;
        if (paso / pc >= totalBars) return;

        if (! arrastrando)
        {
            arrastrado = clipEn (pista, paso);
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
                //  EL ASA MIDE UN COMPAS, no una division. Con la unidad en
                //  pasos, «el primer paso» seria un asa de 5 px en 1/64 - o
                //  sea, un asa que no se puede coger. Un tercio del clip por
                //  lado y como mucho un compas, que en compases enteros da
                //  exactamente lo de siempre -el primer compas y el ultimo- y
                //  en sub-compas deja el asa del tamaño del dedo.
                const auto& c = clips[arrastrado];
                const int  ancho = c.hastaPaso - c.desdePaso;
                if (ancho >= kCompasesConAsa * pc)
                {
                    const int asaP = juce::jlimit (1, juce::jmax (1, ancho / 3), pc);
                    if (paso < c.desdePaso + asaP)      asa = -1;
                    else if (paso >= c.hastaPaso - asaP) asa = +1;
                }
            }
            if (arrastrado < 0)
            {
                //  Un hueco: aqui no hay nada que mover, asi que el gesto solo
                //  puede significar poner algo.
                if (onClipNuevo) onClipNuevo (pista, pegado);
                return;
            }
            //  DONDE SE AGARRO, en DIVISIONES desde el principio del clip. Sin
            //  esto, arrastrar un clip de cuatro compases por su tercer compas
            //  lo pega de un salto por su primero: el bloque se mueve un trozo
            //  que la persona no pidio, y es lo primero que se nota.
            //
            //  Y redondeado a la division y no al paso suelto: el destino sale
            //  de restar el agarre al paso pegado, asi que un agarre en pasos
            //  crudos devolveria un sitio que no cae en ninguna raya - el clip
            //  se quedaria a un paso de la rejilla que se ve.
            agarre = ((paso - clips[arrastrado].desdePaso) / u) * u;
            //  LAS TIJERAS PARTEN POR DONDE CAYO EL DEDO, pegado a la misma
            //  division que se ve. Y ANTES que la goma, que si no un toque con
            //  las tijeras encima de un clip con la brocha en VACIAR lo
            //  borraria en vez de partirlo.
            if (herramienta == hTijeras)
            {
                if (onClipParte) onClipParte (arrastrado, pegado);
                arrastrado = -1;
                return;
            }
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
            int d = c.desdePaso, h = c.hastaPaso;
            if (asa < 0) d = juce::jmin (pegado, h - u);
            else         h = juce::jmax (pegado + u, d + u);
            if (d == c.desdePaso && h == c.hastaPaso) return;
            if (onClipLargo) onClipLargo (arrastrado, d, h);
            return;
        }

        const int nuevoPaso = juce::jmax (0, pegado - agarre);
        if (nuevoPaso == clips[arrastrado].desdePaso && pista == clips[arrastrado].pista) return;
        if (onClipMueve) onClipMueve (arrastrado, pista, nuevoPaso);
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
    //  Y LAS TIJERAS, que es la quinta y no un gesto nuevo: un clip se parte
    //  por donde cae el dedo, que es lo que se pidio -«para poder cortarlo y
    //  colocarlo donde debe»-. El vocabulario ya existia en el piano.
    //  Y SEL Y LUPA, la sexta y la septima, por lo mismo: seleccionar un
    //  rango y acercar un tramo son modos armados y no gestos nuevos.
    enum Herramienta { hLapiz = 0, hGoma, hMano, hMute, hTijeras, hSel, hLupa };
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
    //  Y DESDE QUE EL BLOQUE ES POR PASOS, la caja es el trozo visible del
    //  bloque `[d, h)` y cada columna es un paso de la rejilla que se ve
    //  -`kBarSteps` por compas-, que se traduce al paso del patron con el
    //  `offset` del bloque: la segunda mitad de un bloque partido enseña la
    //  segunda mitad del patron, que es lo que suena.
    void pintaPasos (juce::Graphics& g, juce::Rectangle<float> caja,
                     const BloqueVista& b, int d, int h, bool mudo) const
    {
        const int patron = b.bank + 1;
        if (patPasos == nullptr || patron < 1 || patron > juce::jmin (kMaxPat, patN)) return;
        const auto& filas = filasDe[(size_t) (patron - 1)];
        if (filas.empty() || caja.getHeight() < 6.0f || h <= d) return;

        const int largo = (patLargos != nullptr)
                            ? juce::jlimit (1, patPasos_, patLargos[patron - 1]) : patPasos_;
        const int pc = juce::jmax (1, pasosCompas);

        constexpr float kMinFila = 2.0f;
        const int cabenF = juce::jmax (1, (int) (caja.getHeight() / kMinFila));
        const int nF     = juce::jmin ((int) filas.size(), cabenF);
        const float fh   = caja.getHeight() / (float) nF;
        //  Columnas: una por paso de la rejilla que se ve, o sea kBarSteps
        //  por compas y las que le toquen a un trozo de compas.
        const int cols   = juce::jmax (1, (int) std::lround ((double) (h - d) * StepGrid::kBarSteps / (double) pc));
        const float cw   = caja.getWidth() / (float) cols;

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

            for (int c = 0; c < cols; ++c)
            {
                const int abs = d + (int) ((long long) c * pc / StepGrid::kBarSteps);
                const int st  = (b.offset + (abs - b.desdePaso)) % largo;
                if (st < 0 || ! patPasos[((size_t) (patron - 1) * (size_t) patPasos_ + (size_t) st)
                               * (size_t) patPads + (size_t) pad])
                    continue;
                g.fillRect (caja.getX() + cw * (float) c, y,
                            juce::jmax (1.0f, cw - 0.6f), mh);
            }
        }
    }

    //  `bank` 0..7 es un patron; negativo es un golpe suelto -(pad+1).
    juce::Colour blockColour (int bank) const
    {
        if (bank >= 0) return Zati::colour (bank);
        const int pad = -bank - 1;
        if (zati == nullptr || ! juce::isPositiveAndBelow (pad, zatis))
        {
            ++zatisFueraDeRango;
            //  Su propio color, que es lo unico honesto cuando la tabla no
            //  lo tiene: Zati::colour ya envuelve con el modulo de los ocho.
            return Zati::colour (pad);
        }
        return Zati::colour (zati[pad]);
    }
    //  El bloque que ocupa (carril, paso): su indice en la tabla o -1. El
    //  PRIMERO que lo contenga, como con los clips.
    int bloqueEn (int lane, int paso) const
    {
        for (int i = 0; i < numBloques; ++i)
            if (bloques[i].lane == lane && paso >= bloques[i].desdePaso && paso < bloques[i].hastaPaso)
                return i;
        return -1;
    }

    const BloqueVista* bloques = nullptr;
    int numBloques = 0;
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
    int lA = 0, lB = 0;       // el tramo en bucle, [A,B)
    //  La banda seleccionada y el punto de pegado, que pinta esta rejilla y
    //  guarda la ficha. Ver setSel y setMarca.
    Sel sel;
    int marcaLane = -1, marcaPaso = -1;
    //  El gesto de SEL en curso, como en el piano.
    int  selLaneIni = 0, selPasoIni = 0, dLaneSel = 0, dPasoSel = 0;
    bool moviendoSel = false;
    //  Y el de la LUPA: de que x a que x va el tramo.
    bool lupaViva = false;
    int  lupaX0 = 0, lupaX1 = 0;

    //  Los pasos de los ocho patrones, y que pads usa cada uno.
    int barsView = kBarsViewDef;
    //  Lo que diga `pasosPorCompas()`. El 16 de arranque es el paso guardado
    //  por defecto y se sustituye en el primer `refreshSong`.
    int pasosCompas = 16;

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
    std::vector<BloqueVista> sombra;
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
    int  bloqueIdx = -1, bloqueCarril = -1, bloqueDesde = 0, bloqueHasta = 0;
    bool bloquePrimero = true;        // el primer cambio de largo del gesto

    int              agarre = 0;      // por que compas suyo lo agarro
    int              asa = 0;         // -1 filo izquierdo, +1 derecho, 0 el medio
    unsigned         mudoAudio = 0;   // un bit por pista de audio silenciada
    juce::Colour     modoTinte { juce::Colours::transparentBlack };
};
