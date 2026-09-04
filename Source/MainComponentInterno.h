#pragma once

//  LO QUE COMPARTEN LAS CUATRO UNIDADES DE `MainComponent`.
//
//  `MainComponent.cpp` eran **19 290 lineas en un solo fichero** -254 funciones
//  miembro, un constructor de 2864 y un `resized()` de 4315-, asi que mover un
//  pixel de una ficha recompilaba tambien el arranque, los veintisiete pintores
//  y los catorce ganchos del banco. Se parte por AREA en cuatro unidades, y las
//  cuatro necesitan lo mismo.
//
//  Vivia en un `namespace` anonimo y en `static` sueltos, que con UNA unidad es
//  correcto y con cuatro son cuatro copias mas un aviso de «definida y no
//  usada» por cada unidad que no la llame. En `inline` hay una definicion y el
//  enlazador la une, que es lo que un ayudante compartido tiene que ser.
//
//  No se incluye desde fuera: lo incluyen las cuatro unidades y nadie mas.

#include "MainComponent.h"
#include "Kits.h"
#include "UiAudit.h"
#include "Lang.h"
#include "SystemInsets.h"
#include "Denoise.h"
#include "Onsets.h"
#include "DeviceTier.h"
#include "MediaStore.h"

//  REFERENCES, ALL OF THEM.
//
//  kKey and kStepOff were copies, taken once when this translation unit
//  was initialised - so they froze the palette of whatever skin the app
//  happened to start in. Switching the chassis then repainted the body and
//  left every key wearing the old one: on GRAFITO that is a pale cap with
//  pale text on it, which is a control you can see and cannot read.
//
//  A skin token is mutable by definition. Anything that names one has to
//  keep naming it, not remember what it said.
inline const juce::Colour& kPadLoaded = ZatiColours::amber;
inline const juce::Colour& kAccent    = ZatiColours::amber;
inline const juce::Colour  kRec       = ZatiColours::red;      // semantic, never skinned
inline const juce::Colour& kStepOff   = ZatiColours::key;
inline const juce::Colour& kKey       = ZatiColours::key;

//  WHICH TOKEN A CAP WAS PAINTED WITH, remembered on the cap itself.
//
//  applySkin used to restyle about eight buttons by name and leave the
//  other sixty wearing the palette they were built with. On a chassis
//  change the body, the plates and the LCD all moved and every key stayed
//  behind - a cream cap on a petrol machine, and on GRAFITO a pale cap
//  with pale text. A colour taken from a mutable token has to be RETAKEN,
//  and the only way to retake it is to know which token it was.
enum Role { roleKey = 0, roleAccent = 1, roleRec = 2, roleFixed = 3 };

//  LA GANANCIA DE UN PAD, EN DECIBELIOS.
//
//  Era un mando lineal de 0 a 1 con paso 0.01, y eso son dos fallos en el
//  mismo control. Arriba no habia margen: una muestra grabada baja se
//  quedaba baja, porque 1.0 era el tope y no existia forma de subirla sin
//  volver a grabarla. Y abajo la escala esta al reves de como se oye: de
//  0.01 a 0.02 hay 6 dB - un salto enorme - y de 0.99 a 1.00 hay 0.09 dB,
//  que no se oye. Cien pasos, y la mitad de ellos repartidos en los ultimos
//  0.8 dB del recorrido.
//
//  En decibelios el paso es constante para el oido en todo el recorrido, y
//  hay 12 dB por encima de la unidad para levantar lo que se grabo bajo.
//  El valor guardado sigue siendo la amplitud lineal, asi que los proyectos
//  de antes cargan exactamente igual: lo que cambia es la escala del mando,
//  no lo que hay debajo.
constexpr double kGainMinDb = -60.0;   // el ultimo paso de abajo es SILENCIO
constexpr double kGainMaxDb =  12.0;

inline float gainFromDb (double db) noexcept
{
    return db <= kGainMinDb ? 0.0f : (float) juce::Decibels::decibelsToGain (db);
}

inline double dbFromGain (float g) noexcept
{
    return juce::jlimit (kGainMinDb, kGainMaxDb,
                         juce::Decibels::gainToDecibels ((double) g, kGainMinDb));
}

//  El rotulo del mando: "-inf dB" abajo del todo, y signo siempre, porque
//  un "3 dB" sin signo no dice si sube o baja.
//  Y EL ENVOLTORIO DE DIRECCION AQUI DENTRO, que es donde esta escrito el
//  numero. Lang::ltr estaba puesto en cuatro lecturas y faltaba en las de
//  ganancia -el mando del pad, el master y los dieciseis de la mesa, que
//  pasan todos por aqui-: en arabe, "-inf dB" se reordena y sale "dB inf-",
//  y "+3.0 dB" pone el signo al otro lado. Es exactamente el fallo que
//  Lang.h cuenta con "OUT " + "-inf".
inline juce::String gainText (double db, bool withUnit)
{
    if (db <= kGainMinDb)
        return Lang::ltr (juce::String::fromUTF8 ("-\xe2\x88\x9e") + (withUnit ? " dB" : ""));

    const juce::String n = withUnit ? juce::String (db, 1) : juce::String ((int) std::round (db));
    return Lang::ltr ((db > 0.0 ? "+" : "") + n + (withUnit ? " dB" : ""));
}

inline juce::Colour roleColour (int role)
{
    switch (role)
    {
        case roleAccent: return ZatiColours::amber;
        case roleRec:    return ZatiColours::red;
        case roleKey:
        default:         return ZatiColours::key;
    }
}

inline void styleButton (juce::TextButton& b, juce::Colour c)
{
    b.getProperties().set ("role", c == ZatiColours::amber ? (int) roleAccent
                                 : c == ZatiColours::red   ? (int) roleRec
                                 : c == ZatiColours::key   ? (int) roleKey
                                                           : (int) roleFixed);

    //  Text is chosen by MEASURING it against the cap it lands on, in
    //  both states. See ZatiColours::textOn - the old dark-cap ternary
    //  assumed which of the two inks was the dark one, and that stops
    //  being true the moment the chassis can be dark.
    const auto onCap = b.findColour (juce::TextButton::buttonOnColourId);
    b.setColour (juce::TextButton::buttonColourId, c);
    b.setColour (juce::TextButton::textColourOffId, ZatiColours::textOn (c));
    b.setColour (juce::TextButton::textColourOnId,  ZatiColours::textOn (onCap));
}

//  "THIS CAP LIGHTS IN THE ACCENT" IS A ROLE, NOT A COLOUR.
//
//  Twenty-three places wrote `setColour (buttonOnColourId, kAccent)` at
//  construction, and applySkin refreshed three of them. Every other lit cap
//  in the app - the four bank chips, the bar selector, the settings tabs,
//  REV / LOOP / AUTOCUT, MODO, the song brushes, the pattern buttons -
//  kept the accent of whatever skin the app happened to start in. You could
//  not see it until you pressed one, which is exactly why it survived: the
//  face repainted correctly and then a single button lit up amber on a
//  petrol machine.
//
//  So the fact is recorded on the button and applySkin re-applies it to
//  every cap that carries the mark. The lit-state text is MEASURED against
//  the accent for the same reason the resting text is (see textOn): on a
//  dark accent, light ink; on a bright one, dark.
inline void litAccent (juce::TextButton& b)
{
    b.getProperties().set ("lit", 1);
    b.setColour (juce::TextButton::buttonOnColourId, ZatiColours::accent);
    b.setColour (juce::TextButton::textColourOnId,   ZatiColours::textOn (ZatiColours::accent));
}

// Cycle the 3 primaries across the 8 pattern banks so each has its own
// colour identity in the chain-include row.
// Pattern banks are told apart by TONE, not hue: the chassis carries no
// colour of its own, so three steps of ink stand in for what used to be
// three primaries. Re-read every call — the skin shifts the base tone.
inline juce::Colour patternRowColour (int idx)
{
    const juce::Colour tones[3] = { ZatiColours::accent,
                                    ZatiColours::accent.brighter (0.60f),
                                    ZatiColours::accent.brighter (1.25f) };
    return tones[(size_t) (idx % 3)];
}

// ============================================================================
//  EL ROTULO Y EL DIBUJO DE UNA TAPA DE TRANSPORTE VAN JUNTOS.
//
//  `X.setButtonText (on ? T("STOP") : T("PLAY"))` estaba escrito en SIETE
//  sitios. Anadir el icono al lado habria sido anadir un octavo sitio del que
//  olvidarse, y el sintoma seria una tapa que dice STOP con el triangulo de
//  PLAY dibujado: peor que no tener icono, porque los dos se contradicen.
// ============================================================================
//  UNA BANDA DE TITULO SE PARA ANTES DE LA TAPA QUE COMPARTE SU RENGLON, EN EL
//  LADO EN QUE ESTE.
//
//  Estaba escrito seis veces como `banda.setRight (jmin (derecha, tapa.getX()
//  - aire))`, y esa cuenta solo vale en tres idiomas de los cuatro: en arabe la
//  x de cerrar esta a la IZQUIERDA, asi que recortar por la derecha no recorta
//  nada y el titulo pasa por debajo de ella. Medido: 28 casos del titulo del
//  pad y 7 del de la mesa, todos en arabe, mas los de XY e INSTRUMENTOS.
//
//  El lado se decide comparando los CENTROS y no preguntando por el idioma:
//  quien sabe donde esta la tapa es la tapa.
inline juce::Rectangle<int> antesDe (juce::Rectangle<int> banda, juce::Rectangle<int> b,
                                 int aire = Metrics::xs)
{
if (b.isEmpty()) return banda;

if (b.getCentreX() >= banda.getCentreX())
    banda.setRight (juce::jmax (banda.getX(), juce::jmin (banda.getRight(), b.getX() - aire)));
else
    banda.setLeft  (juce::jmin (banda.getRight(), juce::jmax (banda.getX(), b.getRight() + aire)));
return banda;
}

//  Y LA MISMA, DANDOLE LA TAPA. Cuidado con el espacio de coordenadas: una
//  tapa devuelve sus limites relativos a SU PADRE, y la banda del titulo suele
//  venir de sheetBounds, que va en coordenadas de la cara. Los dos coinciden
//  mientras la tapa cuelgue de la ficha - y las de XY cuelgan de XyPanel, asi
//  que ahi la comparacion se hacia con 88 px de diferencia y el titulo se metia
//  diez pixeles debajo de MOMENTANEO. Para ese caso esta la version de arriba,
//  a la que se le pasa el rectangulo ya convertido.
inline juce::Rectangle<int> antesDe (juce::Rectangle<int> banda, const juce::Component& tapa,
                                 int aire = Metrics::xs)
{
if (! tapa.isVisible()) return banda;
return antesDe (banda, tapa.getBounds(), aire);
}

//  LA TAPA DEL MODO, que dice lo que SUENA y no lo que va a pasar si la tocas.
//
//  Es la misma gramatica que `transporte`: la tapa lleva el nombre del estado
//  en el que esta -PATRON o CANCION- con su dibujo, asi que se lee de un
//  vistazo sin acordarse de nada. Una tapa que dijera "IR A CANCION" obligaria
//  a mirar si esta encendida para saber donde estas.
//  Y CON CLAVE PROPIA, que es lo que arregla un HOMONIMO de la compilacion
//  espanola. La tapa decia `T("CANCION")` y la pestana que abre la ficha dice
//  `T("SONG")`, cuya fila en espanol es tambien «CANCION»: en modo cancion, y
//  girado —donde el transporte comparte renglon con las seis pestanas— habia
//  dos tapas seguidas con la MISMA palabra y el MISMO dibujo, una abriendo una
//  ficha y la otra cambiando lo que toca PLAY.
//
//  En las otras tres lenguas ya estaba bien: la fila decia «SONG MODE», «歌曲
//  模式» y «وضع الأغنية». O sea que era una fila con la traduccion buena y el
//  espanol prestado de otra clave — y `Tests/desglose.py` no podia verlo porque
//  mide el estado APAGADO, donde las cuatro dicen PATRON. Un estado que el banco
//  no abre es un estado sin medir.
inline void modoTapa (juce::TextButton& b, bool cancion)
{
b.setButtonText (T (cancion ? "MODO CANCION|modo" : "MODO PATRON|modo"));
b.getProperties().set ("icono", (int) (cancion ? Iconos::Id::cancion : Iconos::Id::patron));
b.repaint();
}

inline void transporte (juce::TextButton& b, bool rodando)
{
b.setButtonText (rodando ? T ("STOP") : T ("PLAY"));
b.getProperties().set ("icono", (int) (rodando ? Iconos::Id::stop : Iconos::Id::play));
}

//  Y la de OIR, que llevaba el triangulo METIDO EN EL ROTULO -"\u25b6 OIR"- desde
//  antes de que hubiera iconos. Un glifo dentro del texto no es un icono: no lo
//  ve reparteTapa, no se mide, y ademas cuenta como caracteres del rotulo, asi
//  que en arabe y en chino robaba ancho a la palabra.
inline void oirTapa (juce::TextButton& b, bool sonando)
{
b.setButtonText (T (sonando ? "STOP" : "OIR"));
b.getProperties().set ("icono", (int) (sonando ? Iconos::Id::stop : Iconos::Id::play));
}


// ============================================================================
//  Y CINCO BLOQUES QUE COMPARTEN DOS UNIDADES.
//
//  Estaban a medio fichero, entre las funciones que los usan, y ahi era
//  correcto mientras hubo una sola unidad. Al partirlo los saco el compilador
//  uno por uno, que es la unica forma de encontrarlos: `restyleTree` la usan
//  la piel y el maquetado; `kManual` y sus tres altos los PINTA
//  `paintManualBody` y los MIDE `resized()`; `bandAbove` las dos igual; los
//  dos mapas del arbol de proyecto los leen la apertura y el banco; y los
//  textos de `ZatiTour` los pinta el pintor y los mide la maqueta -la altura
//  del muelle sale del parrafo mas largo TRADUCIDO, que es por lo que estan
//  fuera de la funcion que los dibuja-.
// ============================================================================

// Restyle everything that captured accent-coloured values at construction —
// the rest of the UI reads ZatiColours at paint time and only needs repaint.
//  Every cap in the tree, not the eight that happened to be named here.
//
//  The buttons are children of nine different sheets and of the face itself,
//  so the walk is recursive; a cap whose colour was NOT a skin token - a
//  pattern chip wearing its own tone, a mute wearing red - is left alone,
//  which is what roleFixed means.
inline void restyleTree (juce::Component& c, const std::function<void (juce::TextButton&)>& fn)
{
    for (auto* k : c.getChildren())
    {
        if (auto* tb = dynamic_cast<juce::TextButton*> (k))
            fn (*tb);
        restyleTree (*k, fn);
    }
}

// ============================================================================
//  EL MANUAL, en capitulos de cuatro o cinco lineas.
//
//  De consulta y no de lectura: esto se mira con el telefono en la mano y en
//  mitad de algo, asi que cada linea tiene que valerse sola. El manual largo -
//  el que explica POR QUE la ganancia va en decibelios o por que cuatro de los
//  seis efectos restan el seco - es otra cosa y vive fuera.
//
//  Todo pasa por T(): un manual en castellano dentro de una compilacion en
//  chino no es un manual.
// ============================================================================
namespace
{
    struct ManualChapter { const char* title; const char* lines[5]; };

    constexpr int kManualChapterCount = 10;
    const ManualChapter kManual[kManualChapterCount] =
    {
        { "EMPEZAR", {
            "CARGAR y luego un pad abre la biblioteca en ese pad",
            "Un toque toca; una pulsacion larga configura",
            "Manten un pad para abrir su ficha sin que suene",
            //  Y la excepcion, aqui y no en otro capitulo: el gesto se aprende
            //  en el primero, asi que su excepcion va al lado o no se lee.
            "En un pad con instrumento, mantener es tocar: su ficha se abre desde PAD",
            nullptr } },
        { "PADS Y BANCOS", {
            "Cuatro bancos de dieciseis pads: los otros 48 siguen sonando",
            "Arrastra la rejilla para cambiar de banco",
            "El color de un pad lo acompana en la onda y en la rejilla",
            "CARGAR KIT reparte una carpeta entera por los pads",
            nullptr } },
        { "RECORTE", {
            "Arrastra las asas para mover el inicio y el fin",
            "Toca la onda en medio y suena desde ahi",
            "Pellizca para ampliar hasta x64; arrastra para mover la vista",
            "El zoom se centra en el recorte, no en donde estas mirando",
            nullptr } },
        { "SONIDO DEL PAD", {
            "CINTA afina cambiando la duracion; TONO la mantiene",
            "La ganancia va en decibelios, de -60 a +12",
            "NORMALIZAR deja el pico del recorte en -0.3 dBFS",
            "QUITAR RUIDO saca el siseo sin comerse lo que suena",
            "Doble toque en un mando: vuelve a su valor de siempre" } },
        //  Este capitulo prometia una pestana que ya no existe -"la pestana
        //  PASO"- desde que sus mandos bajaron a la tira que hay debajo de la
        //  rejilla y la pagina paso a llamarse PATRON. Un manual que nombra un
        //  sitio que no esta es peor que no tener manual: manda a buscar.
        { "SECUENCIADOR", {
            "Toca una celda para poner un paso; arrastra para pintar varios",
            "Toca un paso y sus mandos salen debajo de la rejilla",
            "PIANO escribe por tono; arrastra por la fila para alargar la nota",
            "REJILLA es lo que dura un paso, tresillos incluidos",
            "Ocho patrones, y la cadena decide en que orden suenan" } },
        { "INSTRUMENTOS", {
            "INSTRUMENTOS pone un sintetizador en el pad que elijas",
            "Su ficha trae los dieciseis presets y un teclado para probarlos",
            "En esos pads el dedo es una tecla: la nota dura lo que la aguantes",
            "INICIO y FIN recortan lo que da vueltas dentro de la nota",
            nullptr } },
        { "MEZCLA Y EFECTOS", {
            "Tocar un efecto lo enciende y le da los tres mandos",
            "Mantenlo pulsado para cogerle los mandos sin encenderlo",
            "RACK: un efecto y los 64 pads. EL PAD: los seis envios de uno",
            "El XY deja los pads tocables debajo, para las dos manos",
            "Verde hasta -12 dB, amarillo hasta -3, y el rojo se queda puesto" } },
        { "GUARDAR Y EXPORTAR", {
            "Un proyecto lleva sus muestras dentro y se puede mover entero",
            "La sesion se recupera sola al abrir la app",
            "MASTER es lo que oyes; PISTAS son los stems que suman a el",
            "Deshacer y rehacer, dieciseis pasos",
            nullptr } },
        { "MIDI", {
            "AJUSTES > MIDI: manda las notas de lo que suena a otro aparato",
            "El pad 1 es la nota 36, y de ahi hacia arriba",
            "RECIBIR deja que un teclado dispare los pads",
            "El secuenciador manda tambien, no solo tus dedos",
            nullptr } },
        { "SI ALGO NO SUENA", {
            "Mira la ganancia del pad y si hay un SOLO puesto en otro",
            "Mira su envio al efecto que estas oyendo",
            "Si la onda no reacciona estas ampliado: toca la tapa del medio",
            "AJUSTES > AUDIO ensena la latencia y el tamano de bloque",
            nullptr } },
    };

    constexpr int kManualLineH  = 30;   // una linea de texto y su aire
    constexpr int kManualTitleH = 26;
    constexpr int kManualGap    = 14;
}

//  THE BAND A LABEL LIVES IN.
//
//  Every caption on this face names the thing directly under it, and the
//  layout always reserves a strip for it - placeKnobRow takes 16, the SEC
//  rows take 14 + kTextPad, and so on. What kept going wrong is that the
//  PAINTING then ignored that strip and used its own offset instead: draw
//  twelve pixels of text fifteen above the control and you get three
//  pixels of air over the word and one under it, every time, everywhere.
//
//  So the band is stated once, as a rectangle, and the text is centred in
//  it. Symmetric by construction rather than by arithmetic that has to be
//  redone correctly at each of the places that needs it.
inline juce::Rectangle<int> bandAbove (const juce::Component& c, int bandH,
                                       int gapToTop, int sideBleed = 0)
{
    return { c.getX() - sideBleed,
             c.getY() - gapToTop - bandH,
             c.getWidth() + 2 * sideBleed,
             bandH };
}

//  El mapa de fuentes del arbol, leido de una vez antes de cargar nada.
//  Ver PadLoadJob::source y captureState: sin esto un troceado vuelve como
//  dieciseis sonidos sueltos que casualmente suenan igual.
//  Los mismos dos bucles y el mismo arbol: quien es trozo de quien, y quien es
//  un instrumento. Leidos ANTES de empezar a cargar por lo mismo que el primero
//  - el trabajo va en tandas de 25 ms y applyState no corre hasta el final.
inline void readInstMap (const juce::ValueTree& tree, std::array<int, AudioEngine::kNumPads>& out)
{
    out.fill (-1);
    auto padsTree = tree.getChildWithName ("PADS");
    if (! padsTree.isValid()) return;

    for (const auto& p : padsTree)
    {
        const int i = (int) p.getProperty ("i", -1);
        if (! juce::isPositiveAndBelow (i, AudioEngine::kNumPads)) continue;
        const int k = (int) p.getProperty ("inst", -1);
        out[(size_t) i] = (k >= 0 && k < Sintes::kFamilias * Sintes::kPresets) ? k : -1;
    }
}

inline void readSourceMap (const juce::ValueTree& tree, std::array<int, AudioEngine::kNumPads>& out)
{
    out.fill (-1);
    auto padsTree = tree.getChildWithName ("PADS");
    if (! padsTree.isValid()) return;

    for (const auto& p : padsTree)
    {
        const int i = (int) p.getProperty ("i", -1);
        if (! juce::isPositiveAndBelow (i, AudioEngine::kNumPads)) continue;
        const int f = (int) p.getProperty ("fuente", i);
        //  Solo hacia atras y solo a otro: un proyecto viejo no trae la
        //  propiedad y cada pad se queda con su fichero, que es como se
        //  guardo. Y una fuente que apunte hacia delante o a si misma seria un
        //  bucle en el cargador.
        out[(size_t) i] = (f >= 0 && f < i) ? f : -1;
    }
}

//  EL DIBUJO DE CADA TIPO DE EFECTO, en una sola tabla. Estaba escrito dentro
//  del bloque que reparte los iconos del constructor, que valia mientras solo
//  lo pidiera el constructor; desde que la fila de la cara son RANURAS hay que
//  volver a preguntarlo cada vez que una cambia de contenido, y una tabla
//  copiada en dos sitios son dos tablas. Es la misma razon por la que
//  `filaDeIconos` salio de dentro de `layoutModuleBar` en cuanto tuvo dos
//  clientes.
//
//  Un indice fuera de la tabla devuelve `ninguno` y no una entrada al azar:
//  una ranura VACIA vale -1 y pasa por aqui.
inline Iconos::Id iconoDeFx (int f) noexcept
{
    static const Iconos::Id kFx[] = { Iconos::Id::flt, Iconos::Id::hpf, Iconos::Id::drv,
                                      Iconos::Id::dly, Iconos::Id::bit, Iconos::Id::rev,
                                      Iconos::Id::eq,  Iconos::Id::cmp, Iconos::Id::gte,
                                      Iconos::Id::dss, Iconos::Id::lim };
    //  UNA FILA POR TIPO, y que lo diga el compilador. Es la misma lista corta
    //  en silencio que ya costo `fxSustituye` y `fxMixNow`: aqui el sintoma seria
    //  una fila con un hueco -o sea lo que `Tests/planos.py` existe para cazar-
    //  y el efecto nuevo llegando sin dibujo. Un tope que se supera en silencio
    //  no protege, esconde.
    static_assert (sizeof (kFx) / sizeof (kFx[0]) == (size_t) AudioEngine::kNumFx,
                   "iconoDeFx tiene que tener una fila por tipo");
    if (! juce::isPositiveAndBelow (f, (int) (sizeof (kFx) / sizeof (kFx[0]))))
        return Iconos::Id::ninguno;
    return kFx[f];
}

//  ==========================================================================
//  LA REJILLA DEL MENU DE RANURA SE PIDE, NO SE ESCRIBE.
//
//  `cols` estaba clavado en 3 con un comentario que prometia lo contrario -«el
//  numero de columnas y el de filas salen de la TABLA»- y solo lo cumplia la
//  mitad: las filas si, las columnas no. Con once tipos daba igual; con
//  veintiuno son SIETE filas, y siete filas piden 460 px con VACIAR contra los
//  **370** que da una tarjeta apaisada (915x412, tope del 0.90). Medido con el
//  clavado puesto, las filas reales salen 40,40,40,40,40,22,0: la sexta por
//  debajo del dedo y la septima de 0x0, o sea `TOUCH` y `CERO` en `expo.py`.
//  Y esta ficha NO se desplaza -es la regla de la casa, y aqui ademas la
//  tarjeta se dibuja encima de la rejilla de pads- asi que lo que no cabe no
//  se alcanza arrastrando.
//
//  Se pregunta con las DOS cosas y no con una: que las filas quepan de ALTO y
//  que la celda se pueda tocar de ANCHO. Con una sola, siete columnas cabrian
//  de pie en el Fold a 32 px de celda.
//
//  Y se prefiere el reparto que NO deja fila corta —«una celda del doble de
//  ancho que sus hermanas se lee como otra cosa», que ya estaba escrito ahi—:
//  se prueban antes los divisores. Con 21 eso da **3 columnas x 7 filas de
//  pie** y **7 x 3 apaisado**, que es la misma rejilla transpuesta; girado
//  sobra ancho y falta alto, que es la regla de siempre.
inline int menuRanuraPide (int filas, bool conVaciar) noexcept
{
    return 2 * Metrics::md + Metrics::hit + Metrics::md
           + filas * Metrics::btn + (filas - 1) * Metrics::xs
           + (conVaciar ? Metrics::sm + Metrics::btn : 0);
}

inline int menuRanuraColumnas (int n, int topeAlto, int anchoDentro, bool conVaciar) noexcept
{
    if (n <= 0) return 1;

    //  Un candidato vale si cabe de ALTO y se puede tocar de ANCHO, y ademas
    //  si deja mas de una fila: **una rejilla de una sola fila no es una
    //  rejilla, es la fila** — la de la cara, de seis ranuras, que es
    //  exactamente lo que este menu existe para no ser. Sin ese tercer
    //  requisito, once tipos salen a 11x1 en tableta y apaisado, que es donde
    //  once celdas de 64 px caben a lo ancho: la regla contestaria «cabe» a lo
    //  que esta casa ya decidio que no se lee, cuando el selector de dieciseis
    //  del RACK dejo de ser una fila.
    auto vale = [&] (int c)
    {
        if (c < 1 || c > n) return false;
        const int filas = (n + c - 1) / c;
        return filas >= 2
                 && menuRanuraPide (filas, conVaciar) <= topeAlto
                 && anchoDentro / c >= Metrics::hit;
    };

    //  MENOS COLUMNAS ES MEJOR: la rejilla mas alta que quepa es la que se
    //  recorre con el pulgar sin cruzar la pantalla, y es la forma que el
    //  telefono pide.
    int elegido = 0;
    for (int c = 3; c <= n && elegido == 0; ++c)
        if (vale (c)) elegido = c;

    //  Y SOLO SI ESA DEJA LA ULTIMA FILA CORTA se cambia por una que reparta
    //  exacto — «una celda del doble de ancho que sus hermanas se lee como
    //  otra cosa», que ya estaba escrito abajo—. Con 21 apaisado eso es lo que
    //  separa 5x5 (una huerfana, y cabiendo por SEIS pixeles) de **7x3**, que
    //  es la misma rejilla transpuesta y sobra de largo: girado sobra ancho y
    //  falta alto.
    if (elegido > 0 && n % elegido != 0)
        for (int c = elegido + 1; c <= n; ++c)
            if (n % c == 0 && vale (c)) { elegido = c; break; }

    //  Y si no cabe ninguno, tres: lo que hay se reparte y la escalera de
    //  `sheetFromBottom` recorta, que es lo que pasaba antes de esta funcion.
    return elegido > 0 ? elegido : 3;
}

//  Los textos, fuera de la funcion que los pinta porque los mide TAMBIEN la
//  maqueta: la altura del muelle sale del parrafo mas largo traducido al idioma
//  que este puesto, y pedir un numero fijo es como se llega a un muelle con
//  hueco de sobra en una lengua y con el texto cortado en otra.
namespace ZatiTour
{
    //  QUINCE PASOS, cada uno sobre un control DE VERDAD y en el orden en que se
    //  aprende el instrumento: primero lo que suena, luego como se escribe, luego
    //  que se le hace al sonido, y al final como sale de aqui.
    //
    //  Cortos a proposito. El proyecto anterior tiene veintisiete y aprendio lo
//  mismo por el
    //  camino - "pasos mas cortos" es una de sus versiones -: un parrafo largo
    //  encima de una maquina oscurecida no se lee, se salta.
    static const char* titulos[MainComponent::kTourPasos] =
        { "ZATI",
          "LOS PADS",
          "CUATRO BANCOS",
          "CARGAR, GRABAR, TOCAR",
          "LOS EFECTOS",
          "LOS TRES MANDOS",
          "LA REJILLA DE PASOS",
          "LO QUE HACE UN PASO",
          "EL PIANO",
          "EL PATRON ENTERO",
          "DENTRO DE UN PAD",
          "LA MESA Y EL RACK",
          "LA CANCION",
          "SACARLO DE AQUI",
          "Y LO DEMAS" };

    static const char* cuerpos[MainComponent::kTourPasos] =
        { "Un sampler entero en el telefono. Este recorrido senala cada pieza en su "
          "sitio; se salta cuando quieras y se vuelve a abrir desde AJUSTES.",

          "Dieciseis a la vista. Toca uno y suena; mantenlo pulsado y se abre todo "
          "lo que se le puede hacer.",

          "A, B, C y D: sesenta y cuatro pads en total. La rejilla ensena uno y "
          "los otros tres siguen sonando.",

          "CARGAR trae un fichero a un pad. REC graba lo que oiga el microfono. "
          "PLAY pone en marcha el patron. Con esto ya se toca: EMPEZAR cierra "
          "esto y VER MAS sigue con el secuenciador, los efectos y lo demas.",

          "Seis ranuras y un menu de efectos para llenarlas. Una vacia pone «+» "
          "y lo abre. Son de la maquina, no del pad: cada pad decide cuanto pasa "
          "por ellos.",

          "Los tres de arriba mueven el efecto que tengas abierto. Debajo de cada "
          "uno pone lo que hace en ese momento.",

          "Dieciseis pasos por dieciseis pads. Toca una casilla y ese pad suena "
          "ahi; arrastra el dedo para escribir varias seguidas.",

          "Con un paso tocado aparecen debajo sus mandos: nota, fuerza, "
          "repeticion, filtro y los cuatro bloqueos.",

          "La misma musica por tono en vez de por pasos. Varias notas en una "
          "columna son un acorde, y arrastrando se estira lo que dura cada una.",

          "Aqui vive lo que le pasa al patron entero: cadena, desplazar, doblar, "
          "humanizar, copiar y pegar, swing y rejilla.",

          "Recorte, afinado, filtro, envolvente y bucle. AUTO CHOP parte un break "
          "por sus golpes y lo reparte por los pads.",

          "La mesa pone los dieciseis a su nivel. El RACK dice cuanto de cada pad "
          "pasa por cada efecto, y cuales sustituyen y cuales suman.",

          "Los patrones colocados en el tiempo, en cuatro carriles. Un bloque "
          "dura lo que ocupa, no lo que dure su patron.",

          "La mezcla entera o una pista por pad, en WAV o en OGG, y a la carpeta "
          "que tu elijas.",

          "El idioma, las cuatro carcasas y el MANUAL, que cuenta todo esto con "
          "calma. Ya puedes empezar." };
}
