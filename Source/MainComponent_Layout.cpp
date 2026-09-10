#include "MainComponentInterno.h"

// ==========================================================================
//  DONDE VA CADA COSA.
//
//  `resized()` y sus dos ayudantes de fila: **4315 lineas**, casi una cuarta
//  parte de lo que era `MainComponent.cpp`. Aqui vive la escalera de esta casa
//  -lo que no cabe se cae, y se cae por orden- y las tres reglas que mas veces
//  se han pagado midiendo: un `jlimit` es un clamp HACIA ARRIBA disfrazado de
//  suelo, un tope que se supera en silencio esconde en vez de proteger, y lo
//  que no puede encoger se aparta primero.
// ==========================================================================

void MainComponent::layoutModuleBar (juce::Rectangle<int> row, juce::TextButton** mb, int vInset, int count)
{
    //  Vale para cualquier fila de tapas, no solo para la barra de modulos: la
    //  fila REV / LOOP / AUTOCUT / BOMBEO tiene el mismo problema y peor, que
    //  "AUTOCUT" pide 52 px y a cuartos le tocaban 42, y el arabe de AUTO CHOP
    //  pide 85.
    //  DOCE, no ocho. Esto recortaba a ocho EN SILENCIO, asi que una fila de
    //  nueve dejaba la novena tapa sin colocar - con las coordenadas de la
    //  ultima vez que se maqueto, encima de sus hermanas. El banco lo saco en
    //  tableta: LOOP a 368,476 solapando once veces. Un tope que se pasa sin
    //  decir nada no protege, esconde; el que hay ahora es el tamano real del
    //  array y ninguna fila de esta app se acerca.
    const int kMods = juce::jlimit (1, 12, count);
    //  Y EL AIRE DE ARRIBA Y ABAJO NO PUEDE COMERSE EL DEDO. Casi todos los
    //  que llaman le pasan 4 a una fila de Metrics::btn -44 px- y la tapa sale
    //  a 36: cuatro por debajo del minimo de 40, en toda la app a la vez. El
    //  banco lo saco en veintiseis sitios de golpe -MASTER, PISTAS, WAV,
    //  CUADRAR, RACK...- y son un solo fallo, contado veintiseis veces. Se
    //  acota aqui y no en cada llamada porque aqui es donde se sabe cuanto
    //  alto hay; el que llama solo sabe cuanto aire querria.
    vInset = juce::jlimit (0, juce::jmax (0, (row.getHeight() - Metrics::hit) / 2), vInset);
    //  La MISMA fuente con la que drawButtonText va a dibujar la tapa. Medir
    //  con otra es como se responde "cabe" a una pregunta que no se ha hecho:
    //  ya paso una vez en este proyecto, con getTextButtonFont.
    const auto capFont = ZatiColours::monoFont (11.0f, true).withExtraKerningFactor (0.06f);

    //  EL REPARTO PROPORCIONAL VA CON EL AIRE CORTO, y no es un descuido.
    //
    //  El aire que la cadena de dibujo se come de verdad son kChrome -treinta
    //  px: el reduced de la tapa y lo que drawButtonText quita por lado- y
    //  parecia obvio reservar eso aqui tambien, para que la pregunta de
    //  moduleBarFits y este reparto contasen lo mismo. Se probo y sale PEOR, y
    //  la razon es que este numero no decide cuanto mide una tapa: decide su
    //  PESO frente a las demas. Con treinta, "XY" pesa 40 y "AJUSTES" 66;
    //  con dieciseis, 26 y 52. El aire fijo comprime la proporcion y le quita
    //  a los rotulos largos para darselo a los cortos, que no lo necesitan.
    //  Medido en las 644 corridas: los apretones bajaron de 73 a 6 y
    //  aparecieron 126 rotulos CORTADOS; repartiendo ademas la escasez,
    //  "CANCION" pasaba de apretada por tres a cortada por seis. Un numero que
    //  pondera no se elige por ser el que mide.
    //
    //  Donde SI hace falta el aire completo es donde se DECIDE - moduleBarFits
    //  y el suelo del dedo de abajo - porque alli la pregunta es "¿cabe?" y no
    //  "¿cuanto le toca?".
    constexpr int kChrome = 2 * Metrics::sm + 2 * (Metrics::halfGap / 2) + 2 * Metrics::margenTapa;
    int need[12] {}; int total = 0;
    for (int i = 0; i < kMods; ++i)
    {
        need[i] = (int) std::ceil (juce::GlyphArrangement::getStringWidth (capFont, mb[i]->getButtonText()))
                + 2 * Metrics::sm;
        total += need[i];
    }

    //  Y UN SUELO DE DEDO POR TAPA, cuando la fila da para el.
    //
    //  Repartir por el texto es lo correcto -"VACIAR" pide mas que "TAP"- pero
    //  el reparto no sabe nada del dedo: en 280x653 la fila del transporte le
    //  daba a TAP 36 px de ancho, cuatro por debajo del minimo, mientras
    //  VACIAR se llevaba 60 de sobra. Se sube el suelo solo si las tapas caben
    //  todas a Metrics::hit y si al subirlo la fila sigue cabiendo: forzarlo
    //  cuando no cabe convierte un ancho corto en la ULTIMA tapa -que se lleva
    //  lo que queda- en un ancho negativo, que es peor que el problema.
    //  Y CON EL AIRE COMPLETO en la cuenta, no con los 2*Metrics::sm del
    //  reparto: la cadena de dibujo se come ademas el reduced de la tapa y lo
    //  que drawButtonText quita por lado - los mismos kChrome que cuenta
    //  moduleBarFits. Subir el suelo con la cuenta corta le robaba esos catorce
    //  pixeles a las tapas largas, y "AJUSTES", "MEZCLA" y "CANCION" de la cara
    //  salian apretadas por uno, dos y tres pixeles en las siete pantallas.
    //  Un arreglo que cambia un hallazgo por otro no es un arreglo.
    if (row.getWidth() >= kMods * Metrics::hit)
    {
        int nec[12] {}; int conSuelo = 0;
        for (int i = 0; i < kMods; ++i)
        {
            nec[i] = juce::jmax (Metrics::hit, need[i] - 2 * Metrics::sm + kChrome);
            conSuelo += nec[i];
        }

        //  Y SI NO CABE CON EL SUELO PUESTO, NO SE PONE. Ni se le quita a nadie.
        //
        //  El intento de repartir el exceso -quitarselo a la tapa mas ancha,
        //  que tiene letra de sobra, hasta que la fila cupiera- suena razonable
        //  y esta mal: empuja los rotulos largos hasta el suelo del dedo y ahi
        //  ya no caben, asi que se CORTAN. Medido en las 644 corridas: los
        //  apretones bajaron de 73 a 30 y aparecieron 173 rotulos cortados -
        //  "AJUSTES" pedia 52 px de letra y tenia 30, y "CANCION" igual, en las
        //  siete pantallas y los cuatro idiomas.
        //
        //  Entre un rotulo apretado y uno cortado no hay duda: drawFittedText
        //  aprieta hasta el 0.9 y se sigue leyendo, y una tapa de 36 px se
        //  sigue tocando. Cambiar un apreton por un corte no es un arreglo.
        if (conSuelo <= row.getWidth())
        {
            for (int i = 0; i < kMods; ++i) need[i] = nec[i];
            total = conSuelo;
        }
    }

    //  EL FILO SE ALINEA POR LA FILA, NO POR LA TAPA.
    //
    //  Cada tapa se recorta `halfGap / 2` por lado, y ese recorte es correcto
    //  ENTRE hermanas -es el hueco que las separa- y sobra en los DOS extremos:
    //  dejaba la fila entera dos pixeles dentro del rectangulo que se le dio.
    //  Medido en la cara a 412x915: el cristal, los cuatro bancos y los
    //  dieciseis pads van de 14 a 398 y las pestanas de modulo, el transporte y
    //  la fila de efectos de 16 a 396. Tres filos izquierdos en la pantalla que
    //  no se puede evitar, que es lo que se ve como que la cara «esta un poco
    //  descolocada» sin que ninguna de las once reglas del banco pueda decirlo:
    //  dos pixeles de margen no solapan, no se salen y no cortan un rotulo.
    //
    //  Se arregla como ya se arreglo la paleta de CANCION y las tres pestanas
    //  del secuenciador -*el aire se lo come la FILA una sola vez*- y aqui
    //  ademas gratis, porque arregla de golpe TODAS las filas de la app: esta
    //  funcion es la que las coloca.
    //
    //  Y se expande DESPUES de las preguntas de reparto y no antes: `need`,
    //  `total` y el suelo del dedo se deciden con el ancho que el que llama
    //  dio, asi que ninguna fila cambia de opinion sobre si cabe entera o se
    //  parte en dos. Lo unico que cambia es que los cuatro pixeles se reparten
    //  entre las tapas en vez de tirarse por los bordes.
    const auto dada = row;
    row = row.expanded (Metrics::halfGap / 2, 0);

    const int spare = juce::jmax (0, row.getWidth() - total);
    for (int i = 0; i < kMods; ++i)
    {
        const int w = need[i] + spare * need[i] / juce::jmax (1, total);
        mb[i]->setBounds ((i < kMods - 1 ? row.removeFromLeft (juce::jmax (24, w)) : row)
                              .reduced (Metrics::halfGap / 2, vInset));
    }

    {
        juce::Rectangle<int> puesta;
        for (int i = 0; i < kMods; ++i)
            if (mb[i]->isVisible() && ! mb[i]->getBounds().isEmpty())
                puesta = puesta.isEmpty() ? mb[i]->getBounds()
                                          : puesta.getUnion (mb[i]->getBounds());
        UiAudit::fila (dada, puesta);
    }

    //  UNA FILA, UN TRATO: los iconos de una fila salen todos o no sale
    //  ninguno.
    //
    //  reparteTapa decide tapa por tapa -donde el rotulo no cabe entero, el
    //  dibujo no sale- y esa regla es correcta y sale MAL en una fila. Medido
    //  en la cara: PADS, SEC, MEZCLA, XY y AJUSTES tenian sitio y CANCION no,
    //  asi que la barra salia con cinco iconos y un hueco. Eso no se lee como
    //  "aqui no cabia", se lee como una tapa a la que le falta algo.
    //
    //  Se pregunta AQUI porque aqui es donde existe la fila: reparteTapa solo
    //  ve una tapa, y quien sabe cuales son hermanas es quien las coloca.
    filaDeIconos (mb, kMods);
}

// Flat-style overlay rings (drawn over the step buttons' plain fill, never
// blended into it): yellow marks the step selected for NOTE editing, red
// marks the live playhead — only when viewing the pattern that's actually
// sounding, so a chain playing a different bank doesn't ring the wrong grid.
void MainComponent::layoutPadGrid (juce::Rectangle<int> area, int cols, int rows, int gap)
{
    // The pads are the instrument, so they take the room rather than leaving
    // it. They were true squares, centred, which on a tall phone left a band
    // of dead chassis above and below while the targets stayed small. Now the
    // cell fills the height it is given and is allowed to run up to a fifth
    // taller than it is wide — past that they stop reading as pads.
    //  Never taller than wide. The ceiling used to be a fifth over square,
    //  which is where the "the pads change shape while the app is opening"
    //  came from: the first pass had the room to hit that ceiling and the
    //  second did not. A pad is a square, and resized() now books it as one.
    //  A SQUARE THAT FITS BOTH WAYS.
    //
    //  The cell used to be jlimit (cellW * 3/4, cellW, roomPerRow), and jlimit
    //  clamps UP as readily as down: when the room per row fell below three
    //  quarters of the width - a short phone, a rotated one, a small window -
    //  the cell was clamped back UP to a size the area did not have, and
    //  withSizeKeepingCentre then centred a grid taller than its own box. The
    //  overflow went out both ends. Measured: at 360x640 the pads sat 7 px
    //  over the effects row, and rotated to 915x412 four of them were laid out
    //  past the bottom of the window entirely, on top of the transport keys.
    //
    //  A missing pixel has to come out of the pad, not out of the section
    //  next to it. One number derived from BOTH constraints can never exceed
    //  either, and it keeps the pad square - which is what it is for.
    //  Width still decides the cell - a pad grid that does not reach the sides
    //  of the face reads as a widget dropped on it, and shrinking to a small
    //  centred square was the first fix and the wrong one. Height only ever
    //  takes away: square while there is room for square, flatter than square
    //  when there is not, and never one pixel taller than the box it was
    //  handed.
    //  ...Y EL SUELO ERA LA MISMA TRAMPA OTRA VEZ.
    //
    //  Todo el parrafo de arriba explica que jlimit clampa HACIA ARRIBA y que
    //  por eso la rejilla se salia por los dos extremos - y luego la linea de
    //  abajo ponia jmax (24, ...), que es un clamp hacia arriba escrito con
    //  otro nombre. Con menos de 24 px por fila la celda volvia a subir a 24 y
    //  withSizeKeepingCentre centraba una rejilla mas alta que su caja.
    //
    //  Medido por el simulador de sesiones sorteadas, no por las siete
    //  pantallas fijas del banco: en 412x480 -un movil en pantalla partida- los
    //  pads 9 a 16 se dibujaban 15 px DENTRO de la fila de efectos y 8 dentro
    //  de la de bancos. Dieciseis solapes que ninguna de las siete medidas
    //  podia ver, porque ninguna es estrecha y baja a la vez.
    //
    //  El suelo se queda, pero acotado a lo que la caja tiene: una celda de
    //  menos de 24 px es un pad incomodo y el banco lo dice como TOUCH, que es
    //  una queja. Una celda de 24 en una caja de 18 es un pad ENCIMA de otra
    //  cosa, que es un fallo.
    const int cellW = (area.getWidth() - (cols - 1) * gap) / cols;
    const int room  = (area.getHeight() - (rows - 1) * gap) / rows;
    const int cellH = juce::jmin (juce::jmax (24, juce::jmin (cellW, room)), juce::jmax (1, room));

    auto grid = area.withSizeKeepingCentre (cols * cellW + (cols - 1) * gap,
                                            rows * cellH + (rows - 1) * gap);

    //  The plate the pads are bolted to. Remembered rather than recomputed in
    //  paint(), because the grid is centred inside whatever room is left and
    //  only this function knows where that landed.
    //  Five, not eight: the eight were the room four screw heads needed at
    //  the corners. Without them the plate can hug the pads, and the three
    //  pixels it gives back become distance to the section above it.
    padPlateArea = grid.expanded (ZatiLookAndFeel::kPlateLip, ZatiLookAndFeel::kPlateLip);

    // SP-style numbering: pad 01 sits BOTTOM-left, 16 top-right — logical row
    // r of the pad index maps to visual row (rows-1-r).
    //  Only the bank on screen is laid out; the other forty-eight are hidden.
    //  They still exist, still hold their sample and their settings, and still
    //  sound when the sequencer asks for them - a bank you cannot see is not a
    //  bank that stopped playing.
    const int base = currentBank * kPadsPerBank;

    for (int i = 0; i < kNumPads; ++i)
        if (auto* p = pads[i])
            p->setVisible (i >= base && i < base + kPadsPerBank);

    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
        {
            const int idx = base + r * cols + c;
            const int vr  = rows - 1 - r;
            if (auto* p = pads[idx])
                p->setBounds (grid.getX() + c * (cellW + gap),
                             grid.getY() + vr * (cellH + gap),
                             cellW, cellH);
        }
}

void MainComponent::resized()
{
    //  Lo que cada tarjeta pide se apunta en esta pasada y no en la anterior:
    //  sin vaciarla, un maquetado deja el suyo encima del de antes y el banco
    //  juzgaria una ficha que ya no esta abierta.
    UiAudit::tarjetas.clear();
    UiAudit::filas.clear();

    //  Height reserved on a seam that carries an engraved name.
    constexpr int kSeamLabelH = 12;

    editInfoArea = {};
    //  Both plates are only laid out on the main face; clearing them here
    //  stops a stale rectangle from being painted under another view.
    padPlateArea = ctrlPlateArea = {};

    //  Margin. Everything used to start 8 px from the glass, which on a phone
    //  reads as the app being too big for the screen rather than as a machine
    //  sitting on it. The extra costs the LCD height, not the controls, since
    //  the screen is what absorbs whatever is left.
    //  ...and inside that, whatever the system is painting on top of us.
    //  From Android 15 the window is the whole screen and the status bar and
    //  the gesture pill sit over it, so the header was under the clock and the
    //  status line under the pill. safeArea is zero everywhere else.
    //  The margin to the glass is a HORIZONTAL idea: it is what stops the pad
    //  grid touching the sides. Vertically the system bars already hold the
    //  face off the clock and the gesture pill, so the same fourteen applied
    //  top and bottom - and then ten more on each - was margin stacked behind
    //  margin, and the instrument ended up floating in the middle of its own
    //  screen with dead paper above and below it.
    auto area = safeArea().reduced (ZatiLookAndFeel::kFaceMargin,
                                    ZatiLookAndFeel::kEdgeV);

    //  TWO COLUMNS WHEN THE SCREEN IS WIDER THAN IT IS TALL.
    //
    //  Every band of this face is stacked, which is the right answer on a
    //  phone held upright and a hopeless one on anything else: rotated to
    //  915x412 there are 412 pixels of height to hold a header, a screen, a
    //  module bar, transport keys, a knob plate, six effects AND four rows of
    //  pads. The face did not fail gracefully, it overflowed - the pads were
    //  laid out below the bottom of the window, on top of the transport.
    //
    //  Rotating is not an error state to survive, it is the second layout an
    //  instrument gets for free: what you WATCH and what you SET on the left,
    //  what you PLAY on the right, which is how a groovebox is arranged on a
    //  desk anyway. Same components, same code below - only the rectangle the
    //  pads are given changes.
    //
    //  The pad column is booked as tall as it is wide, because the grid inside
    //  it is square; the left column keeps a floor so the screen and the knobs
    //  never get squeezed into a strip.
    faceColumn = {};
    juce::Rectangle<int> padCol;
    wideFace = area.getWidth() >= area.getHeight() * 5 / 4 && area.getWidth() >= 560;

    if (wideFace)
    {
        const int want = juce::jlimit (220, juce::jmax (220, area.getWidth() - 320), area.getHeight());
        padCol = area.removeFromRight (want);
        area.removeFromRight (ZatiLookAndFeel::kAir * 2);
        faceColumn = area;
    }

    // The LCD grows to absorb whatever the face doesn't need (the pads are
    // width-bound squares) — the screen is the protagonist.
    int screenH;
    //  ...except for the one seam that carries controls. See the block after
    //  the budget: `padSeamExtra` is what the PADS seam borrows so the four
    //  bank chips are a finger tall with air over and under, and
    //  `padBottomGive` is the part of it that comes out of the band under the
    //  grid rather than out of the screen.
    int padSeamExtra = 0, padBottomGive = 0;
    //  Y LO QUE EL PRESUPUESTO LE RESERVA A LA REJILLA, que es el TESTIGO de
    //  que la cuenta entera cuadra.
    //
    //  Los pads son el elastico de esta columna: todo lo demas tiene un alto
    //  fijo y ellos se llevan lo que quede, asi que un termino de mas o de
    //  menos en CUALQUIER banda del presupuesto aterriza aqui y en ningun otro
    //  sitio. Una cifra en vez de catorce, y exacta.
    int padsNeed = 0;
    //  Y si el cristal se quedo clavado en su suelo — o sea si la cara va
    //  sobre-suscrita. Ahi los pads absorben el deficit A PROPOSITO y la
    //  diferencia es la escalera y no un fallo.
    bool cristalApretado = false;
    //  Y CUANTO MIDE LA FILA DE MODULOS, que hasta ahora era una constante de
    //  26 px. Son las seis tapas que abren las fichas -PADS SEC CANCION MEZCLA
    //  XY AJUSTES-, o sea lo que mas se toca de la cara despues de los pads, y
    //  estaban catorce pixeles por debajo del dedo en las siete pantallas. Es
    //  el mismo fallo que kFxRow contado una fila mas arriba.
    //
    //  Pero a diferencia de aquel, este NO se puede subir siempre: catorce
    //  pixeles no los tiene una cara que ya va sobre-suscrita en las dos
    //  pantallas cortas, y forzarlo se los quitaria a los pads, que ya estan en
    //  su suelo. Se pide lo que hay - la misma pregunta que ya deciden BANCO,
    //  PADS y la tira del paso: sube a cuarenta donde el cristal puede pagarlo
    //  sin bajar de SU suelo, y se queda en veintiseis donde no.
    int moduleH = ZatiLookAndFeel::kModule;
    {
        //  Spelled out term by term and in layout order, because this used to
        //  be two hand-totalled constants that had drifted: the header was
        //  counted as 30 when it is Metrics::tab, and the FX row as 32 when it
        //  is Metrics::hit. Fourteen pixels the pads were assumed to have and
        //  did not - and since layoutPadGrid clamps its cell to a MINIMUM
        //  height, missing room becomes overflow rather than smaller pads.
        //  The VU and the step LEDs live inside the screen now, so the face
        //  no longer spends two strips and four gaps on them - all of it goes
        //  back to the panel that shows them.
        const int aboveScreen = ZatiLookAndFeel::kHeader + ZatiLookAndFeel::kAir;
        //  Rotated there is width to spare and no height at all, so the five
        //  module tabs and the three transport keys share one row instead of
        //  taking two. Thirty pixels back, and the row that gets them is the
        //  effects row, which was coming out 22 tall - a key you PLAY with,
        //  squeezed so a menu could keep its own line.
        //  Y ESTE PRESUPUESTO SE HABIA QUEDADO VIEJO POR LOS DOS LADOS.
        //
        //  Contaba un Metrics::xs entre las pestanas y el transporte que la
        //  maqueta ya NO coloca -se quito cuando la tapa paso a pintarse tres
        //  cuartos, porque cada fila trae cinco px vacios por lado-, asi que
        //  reservaba cuatro pixeles que nadie usa. La misma regla escrita dos
        //  veces con una copia sin actualizar.
        //
        //  Y los dos kAir de los extremos dan a cosas que NO son tapa -el
        //  cristal arriba, la costura de CONTROL abajo-, asi que ahi el hueco
        //  que se VE es el reservado MAS los cinco de la tapa. Se descuentan,
        //  que es lo que iguala el ritmo: quince, diez y quince pasan a diez,
        //  diez y diez.
        auto belowScreenCon = [this] (int mh)
        {
            const int tapa = ZatiLookAndFeel::aireTapaVertical (ZatiLookAndFeel::kTransport);
            const int arriba = wideFace ? tapa : ZatiLookAndFeel::aireTapaVertical (mh);
            return juce::jmax (0, ZatiLookAndFeel::kAir - arriba)
                 + (wideFace ? ZatiLookAndFeel::kTransport
                             : mh + ZatiLookAndFeel::kTransport)
                 + juce::jmax (0, ZatiLookAndFeel::kAir - tapa);
        };
        int belowScreen = belowScreenCon (moduleH);
        const int bottomStrip = ZatiLookAndFeel::kStatus
                              + ZatiLookAndFeel::kAir + Metrics::sm;

        //  A pad is a SQUARE, and the budget says so.
        //
        //  It used to reserve room for pads a fifth taller than they are wide,
        //  and then layoutPadGrid clamped them back down and centred what was
        //  left - so the difference between what was booked and what was used
        //  turned into two bands of dead chassis, one above the grid and one
        //  below. On this phone that was the pads arriving at 1.19 x wide on
        //  the first layout pass and settling at 1.03 x once the safe area
        //  came through: a fifth of a pad row, reserved and then thrown away.
        //
        //  Booking them square recovers all of it at once, and it also means
        //  the pads no longer change SHAPE between the first pass and the
        //  second - they only move.
        //  ...and in two columns the pads are not in this budget at all: they
        //  are in the other one, together with the seam that names them.
        const int cellW    = (area.getWidth() - 3 * ZatiLookAndFeel::kPadGap) / 4;
        padsNeed = wideFace ? 0 : 4 * cellW + 3 * ZatiLookAndFeel::kPadGap;
        //  Y SIN EL `Metrics::sm` QUE YA NADIE COLOCA.
        //
        //  La costura de EFECTOS se reservaba `kAir + layoutAir + kSeamLabelH +
        //  Metrics::sm` y el `sm` se quito de donde se COLOCA -su parrafo esta
        //  ochenta lineas mas abajo: «LA MISMA COSTURA QUE CONTROL, sin el
        //  Metrics::sm de mas»- y se quedo aqui. La misma regla escrita dos
        //  veces con una copia sin actualizar, que es el mismo fallo que este
        //  presupuesto ya se comio con el `Metrics::xs` entre las pestañas y el
        //  transporte y con la costura de EFECTOS.
        //
        //  Ocho pixeles que se restan de `freeH`, o sea que se le quitan al
        //  CRISTAL — la unica banda de esta columna que puede dar — y acaban en
        //  la rejilla de pads, que se lleva lo que sobre. Medido con el testigo
        //  de abajo antes de tocarlo: la rejilla recibia `padsNeed + 8` en las
        //  cinco pantallas de pie.
        const int bodyNeed = ZatiLookAndFeel::kCtrlPlate + ZatiLookAndFeel::kAir
                           + ZatiLookAndFeel::kFxRow + ZatiLookAndFeel::kAir
                           + padsNeed + (wideFace ? 2 : 3) * kSeamLabelH;

        //  ...and what it recovers goes into the SEAMS, not into one pool.
        //
        //  Height left over is worth more spread along the six places where
        //  one section meets the next than added to any single box: it is what
        //  makes a face read as laid out rather than as packed. The LCD keeps
        //  whatever the seams do not take, so on a short screen the seams stay
        //  at their base and the screen is the one that gives.
        //  Two of the six seams carry an engraved name (CONTROL over the knob
        //  plate, PADS over the pad plate; EFECTOS already had room in its
        //  own). Reserving the lettering here rather than hoping the seam is
        //  fat enough is what makes those two labels safe on a short screen:
        //  they are laid out, not squeezed in.
        //  SEIS DE PIE Y CINCO GIRADO, contadas y no supuestas. De pie son
        //  cabecera-cristal, cristal-pestañas, transporte-CONTROL, la banda de
        //  debajo de la rejilla, CONTROL-EFECTOS y EFECTOS-PADS. Girado la
        //  ultima no existe en esta columna: los pads viven en `padCol` y su
        //  costura se paga alli, asi que la sexta se reservaba y no la colocaba
        //  nadie — once pixeles muertos debajo de la fila de efectos, que es
        //  exactamente el mismo fallo que el `Metrics::sm` de arriba con otro
        //  numero. Medido con el testigo: la columna izquierda salia con
        //  `sm + layoutAir` de sobra apaisado.
        const int kSeams       = wideFace ? 5 : 6;
        constexpr int kAirMax  = 11;   // past this the face reads as loose
        //  The screen is the protagonist and it is also the ONLY band that may
        //  give: everything else on this column is a target a finger has to
        //  land on. Ninety-six is what it takes to read a waveform and two
        //  meters; rotated, where the panel is wide and short, the same
        //  information fits in less height and the pixels are worth more to
        //  the effects row than to the wave.
        //  EL CRISTAL ES LA BANDA QUE DA, y su suelo de pie eran 96 px que
        //  nadie habia contrastado con lo que hay debajo. En 360x640 el
        //  cristal se quedaba clavado en ese suelo, asi que no le quedaba nada
        //  que prestarle a la costura de los bancos: los chips A B C D salian
        //  a 37x20 -la MITAD del dedo- y en 393x851 a 36. Un espectro se MIRA;
        //  un chip se toca.
        //
        //  PERO SE BAJA LO QUE HAGA FALTA Y NO MAS. El primer arreglo puso el
        //  suelo plano en 64 y eso le quitaba treinta y dos pixeles al espectro
        //  TAMBIEN en las pantallas donde los chips llegaban al dedo sin
        //  tocarlo - se nota, y con razon: es la unica banda de esta cara que
        //  esta para mirarla. El suelo sigue siendo 96 y solo cede lo que la
        //  costura no consigue por otro lado, con 64 como tope duro: por
        //  debajo de ahi no caben la onda y los dos medidores, y 56 es lo que
        //  el mismo cristal mide apaisado, donde la informacion es la misma en
        //  menos alto.
        constexpr int kSueloCristal = 96;   // de pie, lo que pide una onda con sus dos medidores
        constexpr int kTopeCristal  = 64;   // hasta donde puede ceder antes de dejar de decir nada
        const int kMinScreen = wideFace ? 56 : kSueloCristal;

        //  Ver moduleH: la fila de las seis fichas sube al dedo solo donde el
        //  cristal puede pagar los catorce pixeles quedandose en su suelo, y en
        //  360x640 y en el Fold cerrado no puede.
        //
        //  Y ESTO SE INTENTO DESHACER, MEDIDO, Y NO SE PUDO - que es la parte
        //  que faltaba escrita. Son 1296 incumplimientos del dedo, el grupo mas
        //  grande que le queda a la app, asi que la sospecha razonable era que
        //  esta condicion preguntase contra el suelo del cristal (96) teniendo
        //  el tope duro (64) disponible, como ya hace la costura de los bancos.
        //  Se cambio y la condicion SIGUE fallando, porque el problema no era
        //  el cristal:
        //
        //      360x640  libre con las pestanas a 40: -66 px   pad 77x41
        //      280x653  libre con las pestanas a 40:  27 px   pad 57x44
        //
        //  En la primera la cara ya va sobre-suscrita en 66 px con las pestanas
        //  a 26, y en la segunda lo que quedaria para el cristal son 27 de los
        //  64 que necesita para decir algo. Quien absorbe la diferencia es la
        //  rejilla de pads, y los pads NO tienen de donde: estan en 41 y en 44
        //  px de alto, uno y cuatro por encima del dedo. Darle los catorce a
        //  las pestanas los deja en 27 y en 30.
        //
        //  Un pad de 27 es peor negocio que una pestana de 26: el pad es con lo
        //  que se toca. Se queda como esta, y ahora con la cifra al lado para
        //  que el siguiente que lo vea no repita la medida.
        if (! wideFace
            && area.getHeight() - aboveScreen - belowScreenCon (Metrics::hit)
                 - bottomStrip - bodyNeed >= kMinScreen)
        {
            moduleH = Metrics::hit;
            belowScreen = belowScreenCon (moduleH);
        }

        const int freeH = area.getHeight() - aboveScreen - belowScreen - bottomStrip - bodyNeed;

        layoutAir = (freeH > kMinScreen)
                      ? juce::jlimit (0, kAirMax, (freeH - kMinScreen) / (kSeams + 2))
                      : 0;

        screenH = juce::jmax (kMinScreen, freeH - layoutAir * kSeams);
        //  Y SI EL CRISTAL SE QUEDO CLAVADO EN SU SUELO, la cara va
        //  sobre-suscrita y quien absorbe el deficit es la rejilla. Ahi el
        //  testigo de abajo mide la ESCALERA y no un descuadre.
        cristalApretado = (freeH - layoutAir * kSeams) < kMinScreen;

        //  AIRE PARA LOS CHIPS DE BANCO.
        //
        //  They ride in the PADS seam so they cost the face no height, and the
        //  seam is kAir + layoutAir + kSeamLabelH = 22..33. Take the eight the
        //  cap needs off that and the chips came out 22 px tall - a hair over
        //  half the forty every other target on this machine is held to, on
        //  the control that decides WHICH SIXTEEN PADS you are playing.
        //
        //  The height is there, it was just parked where nothing uses it. Two
        //  places, in this order, and neither of them is a control:
        //
        //    1. The band under the grid. It removes kAir + layoutAir - up to
        //       twenty-one pixels - purely so the pads do not sit on the status
        //       sentence, and Metrics::sm is plenty for that.
        //    2. Whatever the LCD holds over its floor. The screen is the
        //       protagonist and it is also the band that gives, which is the
        //       rule already written above; this is the same rule with one more
        //       claimant.
        //
        //  Rotated, the pad column is its own rectangle and the grid inside it
        //  is width-bound, so the seam simply takes what it wants - there is
        //  nothing below it to give and nothing above it to lose.
        //  A finger, its air over and under, AND the lip of the plate below -
        //  which is painted five pixels above the grid and is therefore five
        //  pixels of the seam that the chips cannot have.
        const int bankSeamWant = kBankSeamWant;
        const int padSeamHave  = ZatiLookAndFeel::kAir + (wideFace ? 0 : layoutAir)
                               + kSeamLabelH;
        int want = juce::jmax (0, bankSeamWant - padSeamHave);

        if (! wideFace)
        {
            //  Down to Metrics::xs, not Metrics::sm. Four pixels between the
            //  last pad row and the status sentence is still four pixels; the
            //  chips are the ones with nothing to spare.
            padBottomGive = juce::jmin (want, juce::jmax (0, ZatiLookAndFeel::kAir
                                                            + layoutAir - Metrics::xs));
            want -= padBottomGive;

            //  Primero lo que el cristal puede dar por encima de su suelo...
            int fromScreen = juce::jmin (want, juce::jmax (0, screenH - kMinScreen));
            want -= fromScreen;

            //  ...y solo si la costura SIGUE sin llegar, lo que le falte de los
            //  pixeles que van del suelo al tope. Ver kSueloCristal: se cede lo
            //  que haga falta y ni uno mas, que es la diferencia entre repartir
            //  la escasez y quitarle altura al espectro por si acaso.
            if (want > 0)
            {
                const int extra = juce::jmin (want, juce::jmax (0, screenH - fromScreen - kTopeCristal));
                fromScreen += extra;
                want -= extra;
            }

            screenH -= fromScreen;
            padSeamExtra = padBottomGive + fromScreen;
        }
        else
        {
            padSeamExtra = want;
        }
    }

    // --- Top chrome ---
    const int faceTop = area.getY();          // donde empieza la cara, para el panel XY
    headerArea = area.removeFromTop (ZatiLookAndFeel::kHeader);
    area.removeFromTop (ZatiLookAndFeel::kAir + layoutAir);

    //  VU above the screen and the step strip below it, so the two readouts
    //  frame the LCD instead of sitting among the controls. Both are watched,
    //  not touched, so they belong together up here.
    screenBezel = area.removeFromTop (screenH);
    //  La barra de trabajo, al pie del cristal y por dentro: es donde la
    //  maquina ya cuenta las cosas, y asi no le quita alto a nada.
    busyArea = screenBezel.reduced (Metrics::sm, Metrics::xs)
                          .removeFromBottom (Metrics::hit - 6);
    busyBar.setBounds (busyArea);
    if (busyJobs > 0) busyBar.toFront (false);
    cristal.setBounds (screenBezel);
    //  El bisel se dibuja 5 px sobresalido, y la fila que viene debajo deja su
    //  propio hueco: la tapa se pinta tres cuartos de alta y centrada. Se
    //  descuenta ese hueco o la separacion que se VE es la reservada mas cinco,
    //  que es lo que hacia que del cristal a las pestanas hubiera quince y de
    //  las pestanas al transporte diez. Ver ZatiLookAndFeel::aireTapaVertical.
    area.removeFromTop (juce::jmax (0, ZatiLookAndFeel::kAir
                                         - ZatiLookAndFeel::aireTapaVertical (
                                               wideFace ? ZatiLookAndFeel::kTransport : moduleH))
                        + layoutAir);

    //  Six modules and three transport keys will not fit across a phone in one
    //  row: LOAD came out as "LO...". They split again, but the module bar
    //  stays slim at 32 while the transport keeps its full 44 — the original
    //  complaint was that the menu was as heavy as PLAY, and that still holds.
    //  Rotated, the two bands become one: the eight caps have the width for it
    //  and the column has no height to spare. The module tabs still read as
    //  lighter than the transport - they are narrower, not just shorter.
    if (wideFace)
    {
        auto row = area.removeFromTop (ZatiLookAndFeel::kTransport);
        tabBarArea = row;
        //  Mismo reparto proporcional que en vertical, y por la misma razon:
        //  a partes iguales entre seis, CANCION pedia 36 px y tenia 32 en el
        //  unico sitio donde la barra comparte fila con el transporte.
        auto tabs = row.removeFromLeft (row.getWidth() * 6 / 10);
        juce::TextButton* mb[6] = { &padsButton, &secButton, &songButton, &mixButton, &xyButton, &setButton };
        layoutModuleBar (tabs, mb, ZatiLookAndFeel::kAir / 2);

        //  CUATRO DONDE CABEN: el interruptor de modo entra a la IZQUIERDA de
        //  PLAY y se lleva la mitad de su ancho, que es de donde sale. Ver
        //  ponModoCancion: la pregunta que contesta -que toca PLAY- se hace
        //  justo antes de pulsarlo, asi que su sitio es este y no otra ficha.
        //
        //  Y girado no caben: aqui el transporte comparte renglon con las seis
        //  pestañas y se queda con 190 px, o sea 42 por tapa - "PATTERN" pide
        //  41 px de letra y tenia 36. Se cae, como se cae SEGUIR, y el
        //  interruptor sigue estando en las otras dos filas de transporte.
        //  CINCO DONDE CABEN, Y LA ESCALERA DICE EN QUE ORDEN SE CAEN.
        //
        //  SOLO entra aqui porque es lo mismo que REC —una tapa armada que
        //  cambia lo que hace tocar un pad— y no cabe siempre: cinco por
        //  cuarenta son 200 px y esta fila mide 190 girada.
        //
        //  Y cae DESPUES de MODO, que es al reves de como se leen: el
        //  interruptor de modo esta ademas en las filas de transporte de SEC y
        //  de CANCION, asi que perderlo aqui lo deja a un toque; SOLO desde la
        //  cara no esta en ningun otro sitio y perderlo es perderlo. «Cada
        //  mando en UN sitio en cada pantalla, nunca en dos, y nunca en
        //  ninguno.»
        juce::TextButton* tr5[5] = { &loadButton, &recButton, &soloButton, &modoBtn, &playButton };
        juce::TextButton* tr4[4] = { &loadButton, &recButton, &soloButton, &playButton };
        const bool cabeModo = moduleBarFits (row.getWidth(), tr5, 5);
        const bool cabeSolo = cabeModo || moduleBarFits (row.getWidth(), tr4, 4);

        modoBtn.setVisible (cabeModo);
        if (! cabeModo) modoBtn.setBounds ({});
        soloButton.setVisible (cabeSolo);
        if (! cabeSolo) soloButton.setBounds ({});

        //  EL FILO SE ALINEA POR LA FILA, NO POR LA TAPA — lo mismo que hace
        //  `layoutModuleBar` y por lo mismo: el `reduced (aireTapa, 0)` de cada
        //  tapa es el hueco ENTRE hermanas y sobra en los dos extremos, asi que
        //  esta fila salia dos pixeles dentro de los pads que tiene debajo. Se
        //  expande DESPUES de las dos preguntas de `moduleBarFits`, que se
        //  hacen con el ancho que se dio: ninguna tapa cambia de opinion sobre
        //  si cabe.
        const auto dada = row;
        row = row.expanded (Metrics::aireTapa, 0);

        const int n = 3 + (cabeSolo ? 1 : 0) + (cabeModo ? 1 : 0);
        const int u = row.getWidth() / n;
        loadButton.setBounds (row.removeFromLeft (u).reduced (Metrics::aireTapa, 0));
        recButton.setBounds  (row.removeFromLeft (u).reduced (Metrics::aireTapa, 0));
        if (cabeSolo)
            soloButton.setBounds (row.removeFromLeft (u).reduced (Metrics::aireTapa, 0));
        if (cabeModo)
            modoBtn.setBounds (row.removeFromLeft (u).reduced (Metrics::aireTapa, 0));
        playButton.setBounds (row.reduced (Metrics::aireTapa, 0));
        UiAudit::fila (dada, loadButton.getBounds().getUnion (playButton.getBounds()));
    }
    else
    {
    tabBarArea = area.removeFromTop (moduleH);
    {
        auto row = tabBarArea;
        juce::TextButton* mb[6] = { &padsButton, &secButton, &songButton, &mixButton, &xyButton, &setButton };
        layoutModuleBar (row, mb, 0);
    }
    //  SIN AIRE DE MAQUETA ENTRE ESTAS DOS FILAS, porque ya lo traen puesto:
    //  cada tapa se pinta tres cuartos de alta y centrada, o sea con cinco px
    //  vacios por arriba y otros cinco por abajo. Los cuatro que habia aqui se
    //  sumaban a esos diez y las dos filas quedaban a catorce - mas separadas
    //  que antes de aligerarlas, que es lo contrario de lo que se buscaba.
    //  Ver ZatiLookAndFeel::capaDe.

    {
        auto row = area.removeFromTop (ZatiLookAndFeel::kTransport);
        //  PLAY se lleva dos cuartos y el modo se queda con uno de ellos: es
        //  literalmente la mitad de PLAY, que era el ancho que sobraba en esta
        //  fila. Las cuatro tapas quedan iguales, y donde no quepan se cae el
        //  modo - la misma pregunta que se hace girado.
        //  CINCO DONDE CABEN, Y LA ESCALERA DICE EN QUE ORDEN SE CAEN.
        //
        //  SOLO entra aqui porque es lo mismo que REC —una tapa armada que
        //  cambia lo que hace tocar un pad— y no cabe siempre: cinco por
        //  cuarenta son 200 px y esta fila mide 190 girada.
        //
        //  Y cae DESPUES de MODO, que es al reves de como se leen: el
        //  interruptor de modo esta ademas en las filas de transporte de SEC y
        //  de CANCION, asi que perderlo aqui lo deja a un toque; SOLO desde la
        //  cara no esta en ningun otro sitio y perderlo es perderlo. «Cada
        //  mando en UN sitio en cada pantalla, nunca en dos, y nunca en
        //  ninguno.»
        juce::TextButton* tr5[5] = { &loadButton, &recButton, &soloButton, &modoBtn, &playButton };
        juce::TextButton* tr4[4] = { &loadButton, &recButton, &soloButton, &playButton };
        const bool cabeModo = moduleBarFits (row.getWidth(), tr5, 5);
        const bool cabeSolo = cabeModo || moduleBarFits (row.getWidth(), tr4, 4);

        modoBtn.setVisible (cabeModo);
        if (! cabeModo) modoBtn.setBounds ({});
        soloButton.setVisible (cabeSolo);
        if (! cabeSolo) soloButton.setBounds ({});

        //  EL FILO SE ALINEA POR LA FILA, NO POR LA TAPA — lo mismo que hace
        //  `layoutModuleBar` y por lo mismo: el `reduced (aireTapa, 0)` de cada
        //  tapa es el hueco ENTRE hermanas y sobra en los dos extremos, asi que
        //  esta fila salia dos pixeles dentro de los pads que tiene debajo. Se
        //  expande DESPUES de las dos preguntas de `moduleBarFits`, que se
        //  hacen con el ancho que se dio: ninguna tapa cambia de opinion sobre
        //  si cabe.
        const auto dada = row;
        row = row.expanded (Metrics::aireTapa, 0);

        const int n = 3 + (cabeSolo ? 1 : 0) + (cabeModo ? 1 : 0);
        const int u = row.getWidth() / n;
        loadButton.setBounds (row.removeFromLeft (u).reduced (Metrics::aireTapa, 0));
        recButton.setBounds  (row.removeFromLeft (u).reduced (Metrics::aireTapa, 0));
        if (cabeSolo)
            soloButton.setBounds (row.removeFromLeft (u).reduced (Metrics::aireTapa, 0));
        if (cabeModo)
            modoBtn.setBounds (row.removeFromLeft (u).reduced (Metrics::aireTapa, 0));
        playButton.setBounds (row.reduced (Metrics::aireTapa, 0));
        UiAudit::fila (dada, loadButton.getBounds().getUnion (playButton.getBounds()));
    }
    }
    //  Y ESTA COSTURA EMPIEZA DONDE ACABA LA TAPA, no donde acaba su fila.
    //
    //  El transporte deja cinco px vacios por debajo -la tapa se pinta tres
    //  cuartos-, asi que reservar kAir entero aqui daba quince de separacion
    //  vista contra los diez de arriba. engraveIn centra la palabra en la
    //  costura, o sea que ese desajuste se llevaba tambien la mitad del aire
    //  que queda sobre los mandos.
    ctrlSeamTop = area.getY() - ZatiLookAndFeel::aireTapaVertical (ZatiLookAndFeel::kTransport);
    area.removeFromTop (juce::jmax (0, ZatiLookAndFeel::kAir
                                         - ZatiLookAndFeel::aireTapaVertical (ZatiLookAndFeel::kTransport))
                        + layoutAir + kSeamLabelH);   // CONTROL rides here

    // Status pinned to the bottom; DESHACER sits on its right when armed, so
    // an undoable action announces itself where the result was reported.
    {
        auto strip = area.removeFromBottom (ZatiLookAndFeel::kStatus);
        //  ...and the pads do not sit on the sentence. Metrics::sm is the floor
        //  that guarantees it; anything this band was holding above that floor
        //  has gone to the PADS seam, where four controls were living in 22 px.
        area.removeFromBottom (juce::jmax (Metrics::xs,
                                           ZatiLookAndFeel::kAir + layoutAir - padBottomGive));
        if (undoButton.isVisible())
            undoButton.setBounds (strip.removeFromRight (96).reduced (Metrics::aireTapa, 0));
        if (redoButton.isVisible())
            redoButton.setBounds (strip.removeFromRight (96).reduced (Metrics::aireTapa, 0));
        status.setBounds (strip);
    }
    area.removeFromBottom (Metrics::sm);

    // --- Machine face: CTRL 1-3 and their readout, the six FX, pads ---
    {
        auto mrow = area.removeFromTop (ZatiLookAndFeel::kCtrlPlate);
        ctrlPlateArea = mrow.expanded (Metrics::margenPlato, Metrics::aireTapa);   // the plate they sit on
        juce::Slider* mk[3] = { &macroCtrl1, &macroCtrl2, &macroCtrl3 };

        //  EL PLATO ES DE QUIEN LO OCUPA. Con un efecto que trae su propia
        //  cara -hoy el EQ- la superficie se lleva el plato ENTERO y los tres
        //  mandos se van: apagados Y sin limites, que son las dos mitades de
        //  la misma regla.
        //
        //  Entero y no dos tercios, y eso es una medida: en 280x653 a cada
        //  nodo le tocan 48 px -por encima del dedo- y con el reparto a dos
        //  tercios puesto a proposito, **31**. Un nodo que no se puede agarrar
        //  es peor que un mando que hay que ir a buscar al XY.
        if (eqCurva.isVisible())
        {
            //  Sin las dos bandas de rotulo: la curva se explica sola -tiene
            //  su linea de cero y sus decadas- y esos 34 px son el 40 % de su
            //  alto. El nombre del efecto ya esta encendido en su ranura.
            eqCurva.setVisible (true);
            eqCurva.setBounds (mrow.reduced (Metrics::margenPlato, Metrics::margenPlato));
            //  Y la miniatura de los otros diez no pinta nada aqui: el EQ trae
            //  su propia cara, que es la version grande de lo mismo.
            platoMini.setVisible (false);
            platoMini.setBounds ({});
            //  APAGADOS *Y* SIN LIMITES, que son las dos mitades de la misma
            //  regla y aqui solo estaba puesta una. Un control encendido y de
            //  0x0 pasa las ocho reglas de geometria -no solapa, no se sale,
            //  no corta un rotulo- y es lo que tuvo a SEGUIR visible desde el
            //  primer dia. El banco lo canto en cuanto hubo una ficha que
            //  abre el EQ: **807 CERO** en `eq` y `eqb`.
            for (auto* k : mk) { k->setVisible (false); k->setBounds ({}); }
        }
        else
        {
            eqCurva.setVisible (false);
            eqCurva.setBounds ({});
            for (auto* k : mk) k->setVisible (true);

            //  Y LOS OTROS DIEZ ENSEÑAN LO QUE SON EN EL MISMO PLATO.
            //
            //  Tres mandos dicen lo que le has PEDIDO al efecto y ninguno lo
            //  que esta haciendo: el EQ contesta esa pregunta con su curva y
            //  los demas no contestaban nada. La miniatura va donde ya vive esa
            //  respuesta -a la izquierda del plato, como el visor de un aparato
            //  al lado de sus mandos- y no en la fila del rack, que es donde
            //  estuvo una tanda y donde no encajaba.
            //
            //  UN TERCIO Y CON SUELO, que es una medida y no una proporcion
            //  elegida: cada celda de mando pierde `2 * margenPlato` en su
            //  inset y el mando no puede bajar del dedo, asi que a las tres se
            //  les garantiza eso antes de dar un pixel al visor.
            //
            //  Y EL SUELO SALE DEL TOKEN. Era `hit + 20` escrito a mano, y ese
            //  veinte ERA el `reduced (10, 0)` de la celda del mando contado a
            //  mano dos lineas mas arriba: la misma regla escrita dos veces, y
            //  la que se quedara vieja dejaria el visor comiendole el dedo al
            //  mando sin que nada fallara. Ver Metrics::margenPlato.
            const int cellMin = Metrics::hit + 2 * Metrics::margenPlato;
            const int visorW  = juce::jlimit (0, juce::jmin (120, mrow.getWidth() - 3 * cellMin),
                                              mrow.getWidth() / 3);
            platoMini.setVisible (visorW >= 48);
            if (visorW >= 48)
            {
                platoMini.setBounds (Lang::takeStart (mrow, visorW)
                                         .reduced (Metrics::margenPlato, Metrics::margenPlato));
            }
            else
            {
                //  APAGADO *Y* SIN LIMITES, las dos mitades de la misma regla.
                platoMini.setBounds ({});
            }

            const int w = mrow.getWidth() / 3;
            for (int i = 0; i < 3; ++i)
            {
                auto cell = (i < 2 ? mrow.removeFromLeft (w) : mrow);
                //  The plate keeps its height and the two labels give theirs
                //  up, so the knob inside grows by ten pixels without the section
                //  taking one from the pads.
                cell.removeFromTop (ZatiLookAndFeel::kCtrlName);
                cell.removeFromBottom (ZatiLookAndFeel::kCtrlChip);
                mk[i]->setBounds (cell.reduced (Metrics::margenPlato, 0));
            }
        }
        //  Y LA COSTURA EMPIEZA DONDE ACABA LA TINTA, no donde acaba el
        //  rectangulo que se reservo. El plato se pinta hasta su borde -son
        //  dos px por debajo de `mrow`, que es lo que `expanded (4, 2)` le
        //  anade- asi que tomar `area.getY()` metia esos dos en el hueco y la
        //  palabra salia alta. Ver el parrafo de engraveIn en el pintor: es la
        //  misma correccion que `ctrlSeamTop` ya hacia por el otro lado.
        fxSeamTop = ctrlPlateArea.isEmpty() ? area.getY() : ctrlPlateArea.getBottom();
        //  LA MISMA COSTURA QUE CONTROL, sin el Metrics::sm de mas.
        //
        //  Las tres bandas grabadas de la cara se reservaban con tres formulas
        //  distintas y esta llevaba ocho pixeles que las otras no. engraveIn
        //  centra la palabra en su costura, asi que ese ocho salia repartido en
        //  cuatro por lado: medido en 412x915, CONTROL quedaba con 13 px de
        //  aire alrededor y EFECTOS con 18, y en 360x640 con 10 y 15. Es poco
        //  de mirar y mucho de leer - una cara con tres ritmos distintos se lee
        //  como tres maquetas pegadas.
        //
        //  Igualadas: 13/14 contra 14/15 en 412x915 y 10/10 contra 11/11 en
        //  360x640, que es el redondeo de layoutAir y no una diferencia.
        //
        //  PADS se queda mas alta a proposito y no es una excepcion olvidada:
        //  su costura ALOJA los chips A B C D de 40 px, y la palabra va
        //  centrada con ellos porque son una sola fila. Ahi el aire grande es
        //  la consecuencia de un control, no un descuido de maquetado.
        //
        //  Los ocho pixeles no se pierden: el sobrante de la cara lo reparte
        //  layoutAir entre las costuras, y ese si es el mismo para las tres.
        area.removeFromTop (ZatiLookAndFeel::kAir + layoutAir + kSeamLabelH);
        fxRowArea = area.removeFromTop (ZatiLookAndFeel::kFxRow);
        {
            auto row = fxRowArea;
            const int sw = row.getWidth() / kNumRanuras;
            //  EL AIRE SE CEDE ANTES QUE EL DEDO, Y SOLO LO QUE SOBRA.
            //
            //  Estas seis tapas se repartian el ancho y luego cada una se comia
            //  dos pixeles por lado: veinticuatro tirados en la fila. En el Fold
            //  cerrado tocaban a 42 y salian a 38, dos por debajo del dedo, y
            //  eran 648 de los 2507 incumplimientos que quedaban en las 756
            //  corridas - el grupo mas grande de la app, y no por falta de sitio
            //  sino por el hueco entre tapas.
            //
            //  Se cede lo que sobra sobre Metrics::hit y ni un pixel mas, que es
            //  la misma regla con la que el cristal le presta a la costura de
            //  los bancos. Donde el ancho da de sobra el aire sigue siendo el de
            //  siempre; donde no da, la separacion se cierra antes que la tapa
            //  deje de poder tocarse. Un hueco es estetica, el dedo no.
            const int aire = juce::jlimit (0, Metrics::aireTapa, (sw - Metrics::hit) / 2);
            //  Y EL FILO SE ALINEA POR LA FILA, NO POR LA TAPA.
            //
            //  El `aire` de arriba es el hueco ENTRE hermanas y sobra en los
            //  dos extremos: esta fila salia de 16 a 396 mientras el cristal,
            //  los cuatro bancos y los dieciseis pads van de 14 a 398 - o sea
            //  dos filos distintos en la pantalla que no se puede evitar, y es
            //  la fila que la queja nombro. Se expande una vez, DESPUES de que
            //  `aire` este decidido, asi que el reparto del dedo no se mueve.
            const auto dada = row;
            row = row.expanded (aire, 0);
            //  Y el ancho de celda se vuelve a repartir sobre lo expandido: con
            //  el `sw` de antes las cinco primeras se quedaban igual y la sexta
            //  se llevaba los cuatro pixeles enteros, que es cambiar un filo
            //  torcido por una tapa mas ancha que sus hermanas.
            const int sc = row.getWidth() / kNumRanuras;
            for (int f = 0; f < kNumRanuras; ++f)
                fxButtons[f]->setBounds ((f < kNumRanuras - 1 ? row.removeFromLeft (sc) : row).reduced (aire, 0));
            UiAudit::fila (dada, fxButtons[0]->getBounds()
                                     .getUnion (fxButtons[kNumRanuras - 1]->getBounds()));
        }
        //  In two columns the pads have a column of their own and the seam
        //  above them is simply the room the square grid does not use, so the
        //  engraving lands there without anything being reserved for it.
        //  THE BANK CHIPS, MEASURED LIKE EVERYTHING ELSE.
        //
        //  They ride in the seam the engraved PADS already occupies, so they
        //  cost the face no height - but riding somewhere is not the same as
        //  being squeezed into it. They take the seam's full height less its
        //  air, they are `Metrics::gap` apart like every other row on this
        //  machine, and the engraved rule is told to stop before them instead
        //  of running underneath.
        //
        //  TWO AND TWO, one pair at each end of the seam. All four pinned to
        //  the trailing end left the word floating in a wide empty half with a
        //  clump of controls jammed against one edge - the seam read as
        //  lopsided at every one of the seven test sizes. Split down the
        //  middle, the engraved PADS sits exactly between the pairs and the
        //  rule breaks symmetrically on both sides of it.
        auto placeBanks = [this] (juce::Rectangle<int> seam)
        {
            //  THE BOTTOM OF A CAP IS ITS SHADOW, and the plate's lip is drawn
            //  five pixels above the grid. Centring the chip in the whole seam
            //  therefore centred it against a floor that is not where the face
            //  visibly ends: eight pixels of air over the caps, two under the
            //  shadows, and the shadow of every chip resting on the plate edge.
            //  Take the lip off the seam FIRST and then centre in what is left,
            //  so the air above the cap and the air below the shadow are the
            //  same number and that number is Metrics::halfGap.
            seam = seam.withTrimmedBottom (ZatiLookAndFeel::kPlateLip);

            //  Ceiling at Metrics::hit, not Metrics::tab. The seam is now given
            //  the height for a finger (see padSeamExtra), and a 32 px ceiling
            //  would have taken the extra and thrown eight of it away.
            //
            //  ...and a final clamp to the seam itself, because jlimit clamps UP
            //  as readily as down and that is how this exact bug is written
            //  three times over in this file's history. On a 360x640 the seam
            //  is worth nineteen usable pixels and the floor of twenty-two put
            //  a chip THREE PIXELS TALLER THAN ITS OWN SEAM, centred, so it
            //  overhung the effects row above and the plate lip below by one
            //  and a half each. A missing pixel comes out of the chip, never
            //  out of the section next to it.
            const int room    = seam.getHeight() - Metrics::gap;
            const int h       = juce::jmin (seam.getHeight(),
                                            juce::jmin (Metrics::hit, juce::jmax (20, room)));
            const int perSide = kNumBanks / 2;
            //  Half the seam belongs to the word; each pair gets one of the
            //  remaining quarters, so a pair plus its air can never grow into
            //  the room PADS needs however wide the screen is.
            int w = juce::jlimit (30, 46,
                                  (seam.getWidth() / 4 - (perSide - 1) * Metrics::gap) / perSide);

            //  Y EL SUELO DEL DEDO TAMBIEN A LO ANCHO, si la palabra sigue
            //  cabiendo entre los dos pares. Repartir la costura en cuartos es
            //  lo que parece justo y no lo es: la palabra grabada no necesita
            //  la mitad del ancho, necesita lo que MIDE. Con los cuartos los
            //  chips salian a 37 px en un 360x640, 35 en el Fold abierto y 30
            //  en el cerrado, con el alto ya en 40 - o sea a un toque de ser
            //  tocables y fallando por el otro lado.
            //
            //  Se pregunta con el texto puesto y en el idioma que toque, que es
            //  como se reparten ya las filas de modulos: "PADS" no mide lo
            //  mismo que la palabra china. Y si no cabe, se queda como estaba -
            //  subir el suelo cuando no cabe es como se convierte un chip
            //  estrecho en una palabra que no se dibuja.
            if (w < Metrics::hit)
            {
                const int anchoPares = 2 * (perSide * Metrics::hit + (perSide - 1) * Metrics::gap);
                const auto fuente = ZatiColours::labelFont (Metrics::fMeta, 0.30f);
                //  Lo que engraveIn reserva alrededor: nueve px de aire a cada
                //  lado de la palabra y diez de margen del rayado.
                const int palabra = (int) std::ceil (juce::GlyphArrangement::getStringWidth (fuente, T ("PADS")))
                                  + 2 * 9 + 2 * 10;
                if (seam.getWidth() - anchoPares >= palabra) w = Metrics::hit;
            }

            const int total = perSide * w + (perSide - 1) * Metrics::gap;

            //  takeStart/takeEnd, not removeFromLeft/Right: in Arabic the pair
            //  that reads first has to be the one on the right, or A B C D runs
            //  backwards across a face whose every other row was mirrored.
            auto lead  = Lang::takeStart (seam, total).withSizeKeepingCentre (total, h);
            auto trail = Lang::takeEnd   (seam, total).withSizeKeepingCentre (total, h);
            bankRowLeftArea  = lead;
            bankRowRightArea = trail;

            for (int b = 0; b < bankButtons.size(); ++b)
            {
                auto& row = (b < perSide ? lead : trail);
                bankButtons[b]->setBounds (Lang::takeStart (row, w));
                if (b % perSide < perSide - 1) Lang::takeStart (row, Metrics::gap);
            }
        };

        //  ...plus whatever the budget managed to borrow for the chips. The
        //  engraved word stays centred in the seam whatever it grows to, so the
        //  extra reads as air around the controls and not as a gap in the face.
        //  Lo que el panel XY puede ocupar: desde donde empieza la cara hasta
        //  donde empieza la costura de PADS, y en dos columnas solo la columna
        //  izquierda - la de los pads es de los pads.
        faceTopArea = wideFace ? faceColumn
                               : juce::Rectangle<int> (area.getX(), faceTop,
                                                       area.getWidth(),
                                                       juce::jmax (0, area.getY() - faceTop));

        //  EL TESTIGO DEL PRESUPUESTO, que es una cifra y no catorce.
        //
        //  «Aprovechar el espacio al maximo» se contesta con un numero o no se
        //  contesta, y lo que no habia mirado nadie NUNCA es si lo que la cara
        //  RESERVA es lo que la cara COLOCA. Hay dos precedentes escritos de
        //  que se desvia -el `Metrics::xs` de las pestañas y el `Metrics::sm`
        //  de esta misma costura- y las dos veces se encontro a mano.
        //
        //  No hace falta un libro de catorce bandas: los pads son el ELASTICO
        //  de esta columna -todo lo demas tiene alto fijo y ellos se llevan lo
        //  que quede- asi que un termino de mas o de menos en cualquier sitio
        //  del presupuesto aterriza aqui y solo aqui. Se mide ANTES de que la
        //  costura les pida prestado, que ese prestamo es deliberado y esta
        //  medido.
        {
            const int costura = ZatiLookAndFeel::kAir + (wideFace ? 0 : layoutAir)
                              + kSeamLabelH + padSeamExtra;
            UiAudit::caraSobra (wideFace ? area.getHeight()
                                         : area.getHeight() - costura - padsNeed,
                                cristalApretado);
        }

        if (wideFace)
        {
            padSeamTop = padCol.getY();
            auto seam = padCol.removeFromTop (ZatiLookAndFeel::kAir + kSeamLabelH + padSeamExtra);
            placeBanks (seam);
            layoutPadGrid (padCol, 4, 4, ZatiLookAndFeel::kPadGap);
        }
        else
        {
            //  Y descontando lo que la fila de efectos deja vacio DENTRO de
            //  su propio rectangulo: la tapa se pinta tres cuartos, asi que
            //  entre la ultima tinta del efecto y `area.getY()` hay cinco px
            //  que el hueco ya tiene y la banda no contaba. Es la correccion
            //  de `ctrlSeamTop` -su comentario la cuenta- aplicada a la
            //  costura que estaba sin ella: sin esto PADS se dibujaba en 462.5
            //  con el hueco visible de 431 a 487, o sea centro 459.
            padSeamTop = area.getY()
                       - ZatiLookAndFeel::aireTapaVertical (fxRowArea.getHeight());

            //  Y LO QUE LE SIGA FALTANDO A LA COSTURA SE LO PRESTAN LOS PADS.
            //
            //  El cristal es la banda que da mientras le quede algo por encima
            //  de su suelo, y en un 360x640 no le queda: la cara esta
            //  sobre-suscrita -lo que se reserva pasa de lo que hay- y quien
            //  absorbe la diferencia es la rejilla, que se lleva el resto. Asi
            //  que los chips se quedaban en 20 px mientras el pad de al lado
            //  tenia 48, ocho por encima de su propio suelo. Ocho pixeles de
            //  pad valen menos que veinte de chip: un pad de 43 sigue siendo un
            //  dedo y un chip de 20 es medio.
            //
            //  Se presta lo que sobra sobre Metrics::hit y ni un pixel mas, que
            //  es la diferencia entre repartir la escasez y mover el problema
            //  de sitio - el error que ya costo 173 rotulos cortados una vez.
            {
                int seamH = ZatiLookAndFeel::kAir + layoutAir + kSeamLabelH + padSeamExtra;
                const int falta = juce::jmax (0, kBankSeamWant - seamH);
                if (falta > 0)
                {
                    const int celda = (area.getHeight() - seamH - 3 * ZatiLookAndFeel::kPadGap) / 4;
                    seamH += juce::jlimit (0, falta, (celda - Metrics::hit) * 4);
                }
                auto seam = area.removeFromTop (seamH);
                placeBanks (seam);
            }
            layoutPadGrid (area, 4, 4, ZatiLookAndFeel::kPadGap);
        }
    }

    // --- Floating sheets (each sized by its own content, capped at 86%) ---
    const auto full = safeArea();
    //  Centred, not risen from the bottom. A bottom sheet at 86% buried the pad
    //  grid exactly while you were editing a pad — you lost sight of the thing
    //  you were adjusting. Centred at 78% x 92% the instrument stays visible
    //  behind the scrim and the window reads as temporary.
    //  The sheet covers the WHOLE window, not just the safe area.
    //
    //  Every rectangle below - the card and each control in it - is worked out
    //  in MainComponent coordinates from `full`, and then handed to children
    //  of the sheet. That only lines up if the sheet's own origin is (0,0):
    //  setBounds(full) put it at the system inset instead, so on Android 15
    //  the whole card and its contents were displaced downward by the height
    //  of the status bar, and sideways by the left inset in landscape. On a
    //  desktop, where the insets are zero, it was invisible.
    //
    //  Covering everything is also the better scrim: a dimmed sheet that stops
    //  short of the status bar reads as a panel with a gap behind it.
    auto sheetFromBottom = [&full, this] (Sheet& s, int desiredH)
    {
        s.setBounds (getLocalBounds());
        //  APAISADO, LA TARJETA PUEDE SER MAS ALTA.
        //
        //  El 78% viene de que la ficha no tape el instrumento que estas
        //  ajustando: en vertical, por debajo de la tarjeta se siguen viendo
        //  los pads, y eso es lo que hace que la ficha se lea como temporal.
        //  Girado no queda nada que proteger - la cara ya esta en dos columnas
        //  y la tarjeta cubre el ancho entero - y en cambio 412 px de alto por
        //  0.78 son 321, que es de donde salian los controles de altura CERO.
        //
        //  Medido en 915x412: con 0.78, la ficha de PASO dejaba un mando de
        //  393x0 - la REJILLA, la ultima de su columna - y con 0.90 cabe.
        const int h = juce::jmin (desiredH, altoTarjeta (full));
        //  LO QUE PIDE Y LO QUE HAY, apuntado. Este `jmin` recorta EN SILENCIO
        //  y lo que falta se lo come lo ultimo que se maqueta: es la causa de
        //  la fila de CADENA, de la REJILLA a 217x0, de las cuatro tapas de
        //  CARCASA a 4 px y de la celda de la linea de tiempo cayendo a 9. Las
        //  nueve reglas ven el sintoma y solo cuando lo que se cae es medible.
        //  Ver UiAudit::tarjeta.
        const int w = anchoTarjeta (full.getWidth());
        auto sheet = juce::Rectangle<int> (0, 0, w, h).withCentre (full.getCentre());
        //  Y LO QUE QUEDA VER DEBAJO, apuntado con el rectangulo YA colocado y
        //  no con la formula: el tope de pie se deriva de que asome un pad
        //  entero -y desde `onFuera` esos pads se tocan- asi que quien lo
        //  comprueba tiene que mirar donde acabo la tarjeta, no repetir la
        //  cuenta que la puso. La tarjeta se CENTRA, asi que el hueco de abajo
        //  no es «ventana menos alto».
        if (s.isVisible())
            UiAudit::tarjeta (desiredH, altoTarjeta (full),
                              s.desplazable || s.listaPropia,
                              full.getBottom() - sheet.getBottom(),
                              full.getWidth() > full.getHeight()
                                  ? 0 : ZatiLookAndFeel::kStatus + Metrics::hit,
                              sheet, Metrics::margenFichaX, Metrics::margenFichaY,
                              (int) s.getProperties()["capa"]);
        s.sheetBounds = sheet;
        auto dentro = sheet.reduced (Metrics::margenFichaX, Metrics::margenFichaY);

        //  LA QUE NO SE DESPLAZA, EXACTAMENTE COMO ESTABA. Ver Sheet::hazDesplazable.
        if (! s.desplazable)
            return dentro;

        //  Y LA QUE SI: la tarjeta sigue midiendo lo que el tope permite -no
        //  tapa la maquina entera- pero el CONTENIDO mide lo que pidio, y lo
        //  que no se ve se alcanza arrastrando. Cuando lo pedido cabe, esto
        //  devuelve el mismo rectangulo de siempre y no cambia nada: el
        //  desplazamiento solo existe en las pantallas donde antes se caian
        //  filas enteras.
        s.vista.setBounds (dentro);
        const int pedido = desiredH - 2 * Metrics::margenFichaY;
        const bool sobra = pedido > dentro.getHeight();
        //  La barra solo se lleva su ancho cuando la hay, o cada fila sale
        //  ocho pixeles corta en las pantallas que no la necesitaban.
        const int barW = sobra ? s.vista.getScrollBarThickness() : 0;
        s.cuerpo.setSize (juce::jmax (40, dentro.getWidth() - barW),
                          juce::jmax (dentro.getHeight(), pedido));
        return s.cuerpo.getLocalBounds();
    };
    //  LA REJILLA DE DIECISEIS PARA ELEGIR PAD. Ver abrePadPicker.
    //
    //  Se maqueta la PRIMERA de las fichas a proposito: es la unica que se
    //  dibuja ENCIMA de otra, asi que sus limites no dependen de los de nadie -
    //  y asi queda claro que no le quita un pixel a la ficha que hay debajo.
    if (padPickAbierto)
    {
        //  Lo que pide, sumado y no probado: dos margenes, la cabecera, el aire,
        //  cuatro filas de tapa con sus tres huecos, el aire y la fila de
        //  bancos. 2*12 + 40 + 12 + 4*44 + 3*4 + 8 + 40 = 312.
        const int quiere = 2 * Metrics::md + Metrics::hit + Metrics::md
                           + 4 * Metrics::btn + 3 * Metrics::xs
                           + Metrics::sm + Metrics::hit;
        auto inner = sheetFromBottom (padPickSheet, quiere);

        auto titleRow = inner.removeFromTop (Metrics::hit);
        padPickCloseBtn.setBounds (Lang::takeEnd (titleRow, Metrics::hit)
                                     .withSizeKeepingCentre (Metrics::hit, Metrics::hit));
        inner.removeFromTop (Metrics::sm);

        //  LA FILA DE BANCOS SE APARTA PRIMERO, que es la regla de la casa:
        //  cuatro tapas no encogen y las filas de la rejilla si.
        auto bancos = inner.removeFromBottom (Metrics::hit);
        inner.removeFromBottom (Metrics::sm);

        for (auto* b : padPickBtns) if (b != nullptr) { b->setVisible (false); b->setBounds ({}); }

        const int filaH = juce::jmax (Metrics::hit,
                                      (inner.getHeight() - 3 * Metrics::xs) / 4);
        for (int r = 0; r < 4; ++r)
        {
            auto row = inner.removeFromTop (filaH);
            const int w = row.getWidth() / 4;
            for (int c = 0; c < 4; ++c)
            {
                //  De abajo arriba, como la cara y como el RACK: el 01 abajo a
                //  la izquierda. Numerar al reves seria un mapa distinto del
                //  mismo instrumento.
                const int i = currentBank * kPadsPerBank + (3 - r) * 4 + c;
                padPickBtns[i]->setVisible (true);
                padPickBtns[i]->setBounds ((c < 3 ? row.removeFromLeft (w) : row)
                                               .reduced (Metrics::aireTapaDensa, 0));
            }
            inner.removeFromTop (Metrics::xs);
        }

        {
            const int w = bancos.getWidth() / kNumBanks;
            for (int b = 0; b < kNumBanks; ++b)
                padPickBankBtns[b]->setBounds ((b < kNumBanks - 1 ? bancos.removeFromLeft (w) : bancos)
                                                 .reduced (Metrics::aireTapaDensa, 0));
        }
    }

    //  LA REJILLA DE DIECISEIS CANALES. Ver canalSheet en la cabecera.
    //
    //  La misma cuenta que la de pads menos la fila de bancos: dieciseis
    //  canales caben exactos en cuatro por cuatro y no hay sesenta y cuatro
    //  entre los que pasear. 2*12 + 40 + 12 + 4*44 + 3*4 = 264.
    if (canalPickAbierto)
    {
        const int quiere = 2 * Metrics::md + Metrics::hit + Metrics::md
                           + 4 * Metrics::btn + 3 * Metrics::xs;
        auto inner = sheetFromBottom (canalSheet, quiere);

        auto titleRow = inner.removeFromTop (Metrics::hit);
        canalCloseBtn.setBounds (Lang::takeEnd (titleRow, Metrics::hit)
                                   .withSizeKeepingCentre (Metrics::hit, Metrics::hit));
        inner.removeFromTop (Metrics::sm);

        const int filaH = juce::jmax (Metrics::hit,
                                      (inner.getHeight() - 3 * Metrics::xs) / 4);
        for (int r = 0; r < 4; ++r)
        {
            auto row = inner.removeFromTop (filaH);
            const int w = row.getWidth() / 4;
            for (int c = 0; c < 4; ++c)
            {
                //  De abajo arriba, como la cara, el RACK y la rejilla de pads.
                const int i = (3 - r) * 4 + c;
                canalBtns[i]->setBounds ((c < 3 ? row.removeFromLeft (w) : row)
                                             .reduced (Metrics::aireTapaDensa, 0));
            }
            inner.removeFromTop (Metrics::xs);
        }
    }

    //  EL MENU DE UNA RANURA. Ver abreMenuRanura.
    //
    //  Tambien de las que se dibujan ENCIMA, asi que va aqui arriba con la
    //  rejilla de pads y por lo mismo: sus limites no dependen de los de nadie
    //  y no le quita un pixel a lo que hay debajo.
    if (ranuraEditada >= 0)
    {
        const bool conVaciar = ranuraVaciarBtn.isVisible();

        //  LAS COLUMNAS SE PIDEN, como las filas. Ver `menuRanuraColumnas`:
        //  `cols` estaba clavado en 3 debajo de un comentario que prometia que
        //  «el numero de columnas y el de filas salen de la TABLA», y solo lo
        //  cumplia la mitad. Con once tipos daba lo mismo -tres columnas y
        //  cuatro filas en las siete pantallas-; con veintiuno son siete filas
        //  y apaisado no caben.
        //
        //  Se pregunta con el tope de tarjeta de esta ventana y con el ancho
        //  que va a tener dentro, que son exactamente los dos numeros con los
        //  que `sheetFromBottom` decide. Escribirlos aqui a ojo seria la misma
        //  regla en dos sitios.
        const auto zona = safeArea();
        const int topeAlto = altoTarjeta (zona);
        const int anchoDentro = anchoTarjeta (zona.getWidth()) - 2 * Metrics::margenFichaX;
        const int cols  = menuRanuraColumnas (kNumFx, topeAlto, anchoDentro, conVaciar);
        const int filas = (kNumFx + cols - 1) / cols;
        const int quiere = menuRanuraPide (filas, conVaciar);
        auto inner = sheetFromBottom (ranuraSheet, quiere);

        auto titleRow = inner.removeFromTop (Metrics::hit);
        ranuraCloseBtn.setBounds (Lang::takeEnd (titleRow, Metrics::hit)
                                    .withSizeKeepingCentre (Metrics::hit, Metrics::hit));
        //  El titulo se queda con lo que sobra del renglon, y lo PUBLICA para
        //  que el pintor no vuelva a calcularlo: una banda deducida dos veces
        //  son dos bandas.
        ranuraTituloBanda = centraEnRenglon (titleRow.withHeight (Metrics::bandaTitulo));
        inner.removeFromTop (Metrics::sm);

        //  VACIAR SE APARTA PRIMERO, que es la regla de la casa: una fila de
        //  tapas no encoge y las filas de la rejilla si.
        if (conVaciar)
        {
            ranuraVaciarBtn.setBounds (inner.removeFromBottom (Metrics::btn));
            inner.removeFromBottom (Metrics::sm);
        }
        else
        {
            ranuraVaciarBtn.setBounds ({});
        }

        const int filaH = juce::jmax (Metrics::hit,
                                      (inner.getHeight() - (filas - 1) * Metrics::xs) / filas);
        for (int r = 0; r < filas; ++r)
        {
            auto row = inner.removeFromTop (filaH);
            const int w = row.getWidth() / cols;
            for (int c = 0; c < cols; ++c)
            {
                const int f = r * cols + c;
                if (f >= ranuraBtns.size()) break;
                //  Una fila incompleta reparte sus celdas al ANCHO DE COLUMNA
                //  y no se reparte lo que queda entre las que hay: una celda
                //  del doble de ancho que sus hermanas se lee como otra cosa,
                //  no como la ultima de la lista.
                const bool ultima = (f == ranuraBtns.size() - 1) || (c == cols - 1);
                ranuraBtns[f]->setBounds ((ultima && c == cols - 1 ? row
                                                                   : row.removeFromLeft (w))
                                            .reduced (Metrics::aireTapaDensa, 0));
                if (f == ranuraBtns.size() - 1) break;
            }
            inner.removeFromTop (Metrics::xs);
        }
    }

    //  LA FICHA DE UNA BANDA DEL EQ. Pide: dos margenes, la cabecera, el aire,
    //  las filas de chips de TIPO y el mando Q con su lectura.
    if (eqBandaSheet.isVisible())
    {
        const int filasTipo = 2;                       // cinco chips no caben en una
        const int quiere = 2 * Metrics::md + Metrics::hit + Metrics::md
                           + filasTipo * Metrics::btn + Metrics::xs
                           + Metrics::sm + ZatiLookAndFeel::kKnobRow;
        auto inner = sheetFromBottom (eqBandaSheet, quiere);

        auto titleRow = inner.removeFromTop (Metrics::hit);
        eqBandaCloseBtn.setBounds (Lang::takeEnd (titleRow, Metrics::hit)
                                     .withSizeKeepingCentre (Metrics::hit, Metrics::hit));
        eqBandaTituloBanda = centraEnRenglon (titleRow.withHeight (Metrics::bandaTitulo));
        inner.removeFromTop (Metrics::sm);

        //  El mando SE APARTA PRIMERO por la regla de siempre: los chips son
        //  una fila de tapas y no encogen, el mando tiene suelo y se lleva lo
        //  que quede.
        auto qRow = inner.removeFromBottom (juce::jmin (ZatiLookAndFeel::kKnobRow, inner.getHeight() / 2));
        inner.removeFromBottom (Metrics::sm);
        eqQKnob.setBounds (qRow.withSizeKeepingCentre (juce::jmin (qRow.getWidth(), 120),
                                                       qRow.getHeight()));

        //  POR EL TEXTO y no a quintos: «PASO BAJO» pide el doble que
        //  «CAMPANA». `layoutModuleBar` es quien ya sabe repartir asi, y quien
        //  sube el suelo al dedo cuando cabe.
        //  En dos filas siempre: cinco tapas en una sola le tocan 45 px en
        //  280x653 y «PASO ALTO» pide mas. Tres arriba y dos abajo.
        juce::TextButton* arriba[3] = { eqTipoBtns[0], eqTipoBtns[1], eqTipoBtns[2] };
        juce::TextButton* abajo [2] = { eqTipoBtns[3], eqTipoBtns[4] };
        layoutModuleBar (inner.removeFromTop (Metrics::btn + Metrics::xs), arriba, Metrics::xs, 3);
        layoutModuleBar (inner.removeFromTop (Metrics::btn), abajo, 0, 2);
    }
    else if (! eqBandaSheet.sheetBounds.isEmpty())
    {
        eqBandaSheet.sheetBounds = {};
    }

    auto placeKnobRow = [] (juce::Rectangle<int> row, juce::Slider** ks, int n = 3)
    {
        const int w = row.getWidth() / juce::jmax (1, n);
        for (int i = 0; i < n; ++i)
        {
            auto cell = (i < n - 1 ? row.removeFromLeft (w) : row);
            cell.removeFromTop (ZatiLookAndFeel::kKnobName);   // gap for knob name
            ks[i]->setBounds (cell.reduced (Metrics::halfGap, 0));
        }
    };

    // PADS sheet, dos paginas. Ver PadPage en la cabecera: SONIDO es lo que
    // suena el pad y EL PAD es lo que el pad es. Cada pagina pide la altura que
    // va a usar, asi que la ficha encoge cuando lo de dentro ocupa menos - la
    // de EL PAD no arrastra el hueco de la onda, que no lleva.
    {
        constexpr int secH = 15 + 2 * ZatiLookAndFeel::kTextPad;
        //  644 y 418 salen de sumar lo que lleva cada pagina, no de probar:
        //  ver el desglose de cada bloque mas abajo.
        const int sheetInnerW = anchoTarjetaInterior (full.getWidth());
        //  Lo que pide cada pagina, sumado y no probado:
        //  SONIDO  = titulo+pestanas+margenes (116) + secH + 86 + 86 + 86 + 56 + 8
        //  RECORTE = 116 + secH + 34+4+34+8 + hit + 8 + 180 de onda
        //  EL PAD  = 116 + 3*secH + 2*86 + 2*hit + 3*sm + chip
        //  sheetFromBottom recorta si no cabe, y de eso se ocupa el reparto.
        //  116 son el titulo, las pestanas y los margenes de la tarjeta; el
        //  resto lo dice altoContenidoElPad, que es la MISMA funcion con la que
        //  el maquetado decide si tiene que apretar. Ver su comentario: mientras
        //  fueron dos numeros, mudar los envios al RACK dejo la peticion vieja y
        //  la ficha reservaba 134 px que nadie usaba.
        const int rigH = 116 + altoContenidoElPad (sheetInnerW);
        //  LA FILA DE CHOKE PUEDE SER DOS.
        //
        //  CHOKE se lleva un tercio escaso y en el se meten su casilla y sus
        //  dos teclas: en 280x653 al numero le quedan 30 px, y ahi dice tambien
        //  "off" - que en arabe es مغلق y pide 28 de letra, asi que se encogia.
        //  No se arregla quitandole sitio a MODO y NORMALIZAR, que ya piden el
        //  suyo: donde los tres no caben, CHOKE se queda una fila entera y las
        //  otras dos bajan. Es lo que ya hacen las pestanas de AJUSTES y la
        //  barra de modulos del pad.
        const int anchoFila3 = sheetInnerW - 2 * Metrics::margenFichaX;
        const bool chokeSolo = ! padRowFits (anchoFila3 * 68 / 100, { &modeButton, &normButton })
                             || anchoFila3 * 32 / 100 < 12 + 34 + Metrics::gap + 2 * Metrics::stepKey;
        //  438 y no 352: la fila del filtro son 86 mas. Ver el desglose.
        const int wantH = (padPage == padPageSound) ? 438 + secH
                                                    + (chokeSolo ? Metrics::hit + Metrics::halfGap : 0)
                        : (padPage == padPageTrim)  ? 436 + secH + 2 * (ZatiLookAndFeel::kTrimRow + Metrics::xs)
                                                    //  Y la fila de la muestra puede ser DOS desde que
                                                    //  esta RECORTAR: la misma pregunta que la coloca.
                                                    + (padMuestraWraps (sheetInnerW - 2 * Metrics::margenFichaX)
                                                         ? Metrics::hit + Metrics::halfGap : 0)
                                                    : rigH;
        auto inner = sheetFromBottom (padSheet, wantH);
        auto titleRow = inner.removeFromTop (Metrics::hit);
        padCloseButton.setBounds (Lang::takeEnd (titleRow, Metrics::hit).withSizeKeepingCentre (Metrics::hit, Metrics::hit));
        Lang::takeEnd (titleRow, Metrics::xs);
        previewButton.setBounds (Lang::takeEnd (titleRow, 68).reduced (0, 0));
        //  LA PUERTA A LA REJILLA DE DIECISEIS, en la cabecera y a la derecha,
        //  igual que en la pagina del piano: las dos fichas que editan "el pad
        //  que tengas elegido" lo cambian desde el mismo sitio. Ver
        //  padPickSheet. Y solo si queda ancho para ella y para el titulo: el
        //  rotulo son dos cifras, asi que le basta el dedo.
        {
            Lang::takeEnd (titleRow, Metrics::xs);
            const bool cabe = titleRow.getWidth() >= Metrics::hit * 2;
            padPadPickBtn.setVisible (cabe);
            padPadPickBtn.setBounds (cabe ? Lang::takeEnd (titleRow, Metrics::hit)
                                              .withSizeKeepingCentre (Metrics::hit, Metrics::hit)
                                          : juce::Rectangle<int>());
        }

        //  Las pestanas, debajo del titulo y en las dos paginas.
        inner.removeFromTop (Metrics::xs);
        {
            auto tabRow = inner.removeFromTop (Metrics::hit);
            juce::TextButton* tb[3] = { &padSoundBtn, &padTrimBtn, &padRigBtn };
            layoutModuleBar (tabRow, tb, 0, 3);
        }
        inner.removeFromTop (Metrics::sm);

        //  LOS GRUPOS DE ESTA FICHA. Se apuntan aqui, mientras se reparte el
        //  alto, con las coordenadas que la maqueta acaba de dar - no se
        //  maquetan, asi que no cuestan un pixel. `cierra` toma el borde de
        //  arriba y cierra por donde `inner` haya llegado, que es el fondo de
        //  lo ultimo colocado: llamarlo DESPUES del aire de separacion metia
        //  ese aire dentro del panel y los dejaba tocandose otra vez.
        padGrupos.clear();
        auto cierra = [this, &inner] (int y0)
        {
            if (inner.getY() > y0)
                padGrupos.add ({ inner.getX(), y0, inner.getWidth(), inner.getY() - y0 });
        };

        if (padPage == padPageRig)
        {
            //  EL PAD: 3*secH + 40 (la puerta del RACK) + 8 + 40 (corte) + 8
            //  + 40 (fuente) + 8 + chip + margenes.
            //  ¿HAY SITIO PARA LA PAGINA ENTERA?
            //
            //  En apaisado - 915x412 - la ficha no puede pasar del 78% de 412,
            //  que son 321, y esta pagina pide 481. Lo que se sale por abajo no
            //  desaparece: se queda con altura CERO, y un boton de altura cero
            //  se ve en el volcado como un control de 265x0. Asi que la pagina
            //  se mide contra el hueco que le han dado y, si no cabe, los seis
            //  envios pasan a una sola fila y las muestras de color - que son
            //  una etiqueta y no un ajuste - se quedan fuera.
            const int needFull = altoContenidoElPad (inner.getWidth());
            //  APRETADO NO ES LO MISMO QUE ESTRECHO, y confundirlos costo una
            //  medida: en 280x653 la pagina tampoco cabe de alto, se fue por
            //  la rama de la fila unica, y cinco tapas en 225 px son 45 px
            //  cada una - AUTO CHOP, GRABAR MIC y AUTOCUT truncados los tres.
            //  Fundir las dos secciones solo sirve cuando lo que sobra es
            //  ANCHO, que es lo que pasa en apaisado y no en un movil de pie.
            const bool tight = inner.getHeight() < needFull;
            const bool merge = tight && padRowFits (inner.getWidth(),
                                                    { &autocutButton, &duckButton, &chopButton,
                                                      &micButton, &resampleButton });

            padRigTight = merge;

            //  LOS SEIS ENVIOS ESTABAN AQUI Y EN EL RACK, los mismos seis
            //  valores movidos desde dos fichas distintas. Dos sitios para una
            //  cosa no son dos comodidades: son dos maquetados que mantener,
            //  dos formas de que uno se quede viejo, y una pregunta -"¿cual de
            //  las dos es la buena?"- que no deberia existir.
            //
            //  Se queda el RACK, que es el que dice algo mas: apaga el envio
            //  cuyo efecto esta cerrado -"lo que pongas ahora es lo que usara
            //  cuando lo enciendas"- y trae su propio selector de pad, asi que
            //  se pueden repasar los dieciseis sin cerrar nada. Aqui queda la
            //  puerta, que ademas devuelve 86 px de alto a la pagina mas
            //  apretada de la ficha.
            const int gEnvios = inner.getY();
            padSectionArea[0] = inner.removeFromTop (secH);   // pintado: ENVIOS
            {
                //  Las dos puertas de este pad: a donde va -ENVIOS- y que toca
                //  -PIANO-. Repartidas por el texto que llevan, como el resto.
                //  Y LA CUARTA SOLO CUANDO LA HAY. Un pad de instrumento la
                //  lleva y los otros no: una tapa que abre una ficha vacia es
                //  peor que no tenerla, y ademas le quitaria ancho a las tres
                //  que si sirven siempre. Quien la apaga es el bloque de
                //  visibilidad de arriba, que corre en TODAS las paginas.
                const bool esInstr = vstButton.isVisible();

                //  Y EL CANAL, que no es una puerta a otra ficha sino a DONDE VA
                //  este pad — y es lo que hace que la fila de la cara
                //  signifique algo al cambiar de pad. Va aqui y no en la fila
                //  de CHOKE porque es reparto y no ajuste: la seccion se llama
                //  EFECTOS y esto es por donde el pad llega a ellos.
                //
                //  Y SE CAE A SU PROPIA FILA DONDE NO CABEN TODAS, que es la
                //  misma escalera que la fila de FUENTE y la de CHOKE: con el
                //  CANAL dentro son cuatro tapas y en 280x653 a RACK le tocaban
                //  35 px de ancho contra un dedo de 40. La pregunta la contesta
                //  `padPuertasWraps`, que es la MISMA con la que se presupuesta
                //  el alto — dos cuentas parecidas son dos reglas.
                if (padPuertasWraps (inner.getWidth()))
                {
                    juce::TextButton* cb[1] = { &padCanalBtn };
                    layoutModuleBar (inner.removeFromTop (Metrics::hit), cb, 0, 1);
                    inner.removeFromTop (Metrics::xs);
                    juce::TextButton* pb[4] = { &padRackBtn, &pianoButton,
                                                &nivelesButton, &vstButton };
                    layoutModuleBar (inner.removeFromTop (Metrics::hit), pb, 0, esInstr ? 4 : 3);
                }
                else
                {
                    juce::TextButton* pb[5] = { &padCanalBtn, &padRackBtn, &pianoButton,
                                                &nivelesButton, &vstButton };
                    layoutModuleBar (inner.removeFromTop (Metrics::hit), pb, 0, esInstr ? 5 : 4);
                }
            }
            cierra (gEnvios);
            inner.removeFromTop (Metrics::sm);

            if (merge)
            {
                //  Apaisado: CORTE y FUENTE se funden en una sola seccion de
                //  cinco tapas. La ficha mide 841 px de ancho ahi y solo 321
                //  de alto, asi que lo que sobra es exactamente lo que a lo
                //  otro le falta - y dos titulos de seccion con sus dos filas
                //  cuestan 138 px de alto para decir lo mismo que una.
                const int gPad = inner.getY();
                padSectionArea[1] = inner.removeFromTop (secH);   // pintado: EL PAD
                padSectionArea[2] = {};
                auto rr = inner.removeFromTop (Metrics::hit);
                juce::TextButton* pb[5] = { &autocutButton, &duckButton,
                                            &chopButton, &micButton, &resampleButton };
                layoutModuleBar (rr, pb, 0, 5);
                cierra (gPad);
            }
            else
            {
                const int gCorte = inner.getY();
                padSectionArea[1] = inner.removeFromTop (secH);   // pintado: CORTE
                {
                    auto rr = inner.removeFromTop (Metrics::hit);
                    juce::TextButton* pb[2] = { &autocutButton, &duckButton };
                    layoutModuleBar (rr, pb, 0, 2);
                }
                cierra (gCorte);
                inner.removeFromTop (Metrics::sm);

                const int gFuente = inner.getY();
                padSectionArea[2] = inner.removeFromTop (secH);   // pintado: FUENTE
                //  Tres formas de poner un sonido en un pad: cortar uno que ya
                //  tienes, grabar la sala, o imprimir lo que la maquina esta
                //  tocando. En una fila cuando caben las tres palabras y en dos
                //  cuando no: en 280x653 son 225 px para AUTO CHOP, GRABAR MIC
                //  y REMUESTREAR, que piden 272 entre las tres. Antes esta fila
                //  se salia por abajo de la ficha y por eso el banco no la veia.
                if (padSourceWraps (inner.getWidth()))
                {
                    auto ra = inner.removeFromTop (Metrics::hit);
                    juce::TextButton* p2[2] = { &chopButton, &micButton };
                    layoutModuleBar (ra, p2, 0, 2);
                    inner.removeFromTop (Metrics::xs);
                    auto rb = inner.removeFromTop (Metrics::hit);
                    juce::TextButton* p1[1] = { &resampleButton };
                    layoutModuleBar (rb, p1, 0, 1);
                }
                else
                {
                    auto rr = inner.removeFromTop (Metrics::hit);
                    juce::TextButton* pb[3] = { &chopButton, &micButton, &resampleButton };
                    layoutModuleBar (rr, pb, 0, 3);
                }
                cierra (gFuente);
            }
            inner.removeFromTop (Metrics::sm);

            //  El color es una ETIQUETA, no diseno de sonido, y llego a ser lo
            //  mas llamativo de la ficha: una fila entera de 44 px con dos
            //  teclas y una barra de color saturado gritando por encima de
            //  PITCH. Son ocho muestras en una tira de altura de chip - y son
            //  lo primero que se cae cuando no hay hueco, por ser lo unico de
            //  esta pagina que no cambia como suena nada.
            zatiSwatchArea = (inner.getHeight() < Metrics::chip)
                                 ? juce::Rectangle<int>()
                                 : inner.removeFromTop (Metrics::chip).reduced (Metrics::halfGap, 0);
            editInfoArea = {};
        }
        else if (padPage == padPageSound)
        {

        padSectionArea[0] = inner.removeFromTop (secH);   // painted: SONIDO

        //  Los 86 son el alto comodo de un mando con su nombre y su numero, y
        //  no son un derecho: en apaisado la ficha se queda en 321 px y esta
        //  pagina pide 373, asi que las dos filas de mandos se apretaban hasta
        //  que la tercera - CHOKE, MODO, NORMALIZAR - se salia por abajo con
        //  altura cero. Sale de lo que hay, con 60 de suelo.
        //  LA FILA DE ABAJO SE APARTA PRIMERO.
        //
        //  Los tres mandos se llevaban de arriba lo que pedian y esta fila se
        //  quedaba con lo que sobrara: medido girado, CINTA, MODO y NORMALIZAR
        //  salian de 37 px. Es el mismo fallo que el TEMPO del secuenciador y
        //  se arregla igual - se reserva lo que no puede encoger y los mandos,
        //  que SI pueden (tienen suelo de 60), se reparten el resto.
        auto filaBaja = inner.removeFromBottom (ZatiLookAndFeel::kKnobName + Metrics::hit);

        {
            const int forKnobs = inner.getHeight();
            //  SE PIDE LO QUE HAY. `jlimit (60, kKnobRow, forKnobs/3)` es un
            //  clamp HACIA ARRIBA disfrazado de suelo -el fallo mas repetido de
            //  esta casa, ya pagado en layoutPadGrid con `jmax (24, ...)` y en
            //  la celda de paso con `jlimit (12, 26, ...)`-: cuando el tercio
            //  no llega a 60 lo SUBE a 60, se piden 180 px donde hay menos,
            //  removeFromTop no se queja y la tercera fila -CORTE, RESO y
            //  ANCHO- se queda con lo que sobre. Vivo en apaisado, donde esta
            //  pagina pide 438 px en una tarjeta que puede medir 371.
            const int knobH = juce::jmin (ZatiLookAndFeel::kKnobRow,
                                          juce::jmax (juce::jmin (60, forKnobs / 3), forKnobs / 3));
            juce::Slider* k1[3] = { &pitchSlider, &fineSlider, &volSlider };
            juce::Slider* k2[3] = { &panSlider, &attackSlider, &releaseSlider };
            //  Y la tercera fila son TRES desde que existe el ANCHO. Era de
            //  dos celdas anchas con este argumento: "media fila vacia se lee
            //  como un mando que falta". Con tres la fila esta llena, que es lo
            //  que ese argumento pedia, y no cuesta un pixel de alto - que es
            //  lo unico que a esta ficha le falta.
            //
            //  ANCHO aqui y no al lado de PAN, que seria su pareja: la fila de
            //  PAN ya lleva ATTACK y RELEASE, y separarlos para juntar esta
            //  pareja romperia la otra. Una fila mas cuesta 60 px en la pagina
            //  mas apretada de la ficha.
            juce::Slider* k3[3] = { &cutSlider, &resoSlider, &anchoSlider };
            placeKnobRow (inner.removeFromTop (knobH), k1);
            placeKnobRow (inner.removeFromTop (knobH), k2);
            placeKnobRow (inner.removeFromTop (knobH), k3);
        }
        //  UN SOLO PANEL EN ESTA PAGINA, y es el de ABAJO. Los nueve mandos
        //  dicen COMO SUENA el pad y las tres tapas de abajo QUE HACE -a quien
        //  corta, si es cinta o tono, si se normaliza- asi que son dos grupos;
        //  lo que no hay es AIRE entre ellos. `filaBaja` se aparta del fondo y
        //  los mandos se comen lo que queda, asi que en la rama apretada estan
        //  pegados y en la otra los separa Metrics::halfGap - cuatro, y el
        //  panel se sale dos por arriba y dos por abajo. Medido en 412x915: los
        //  dos paneles se tocaban y la pagina entera salia sobre una sola losa,
        //  que es exactamente lo que se lee sin dibujar ninguno.
        //
        //  Con uno solo hay UNA frontera y se ve: el panel de abajo contra la
        //  tarjeta desnuda de los mandos. Un panel no dice "esto es un grupo",
        //  dice "esto y aquello no son lo mismo", y para eso hace falta uno y
        //  no dos.

        //  A third row for the two controls that are not dials: CHOKE, which
        //  is a pair of increment buttons, and the tape/tone switch. Giving
        //  them a knob-sized cell was what turned CHOKE into two tall slabs
        //  that swallowed their column.
        {
            auto r3 = filaBaja;
            r3.removeFromTop (ZatiLookAndFeel::kKnobName);   // gap for the names
            //  Tres celdas, no tres tercios. NORMALIZAR es la palabra mas
            //  larga de la ficha y en 280x653 pedia 66 px de un tercio que
            //  daba 55: el banco lo saco como TRUNC en cuanto entro el boton.
            //  Repartirlo a mano fue perseguirse la cola - 42 dejaba
            //  NORMALIZAR dos pixeles corto y 44 truncaba el MODO arabe, que
            //  es mas ancho que el castellano. CHOKE se queda con su tercio
            //  escaso, que es lo que piden sus dos teclas, y los otros dos se
            //  reparten POR EL TEXTO QUE LLEVAN, que es lo mismo que hacen
            //  las barras de modulos y lo unico que se ajusta solo en cuatro
            //  idiomas.
            const int w3 = chokeSolo ? r3.getWidth() : r3.getWidth() * 32 / 100;
            //  Sin recorte vertical: la fila mide Metrics::hit justo, que es
            //  el dedo minimo, y quitarle 3 arriba y 3 abajo dejaba tres
            //  controles de 34 px que el banco saca como TOUCH. Encima hay 16
            //  px de rotulo y debajo Metrics::sm, asi que a 40 no toca nada.
            //  Metrics::aireTapa y no un 6: es el margen de todas las demas
            //  filas, y con seis CHOKE entraba cuatro pixeles mas que las dos
            //  tapas que tiene debajo en el mismo panel - filas que empiezan
            //  en 10 y en 6, que es lo que Tests/paneles.py llama FILAS.
            //  Y EL FILO DE FUERA ES EL DE LA FILA, no el de la tapa.
            //
            //  `reduced (aireTapa, 0)` mete la celda por los DOS lados, y ese
            //  aire solo tiene sentido en el que da a la hermana: por el otro
            //  dejaba CHOKE dos pixeles mas dentro que la fila de tapas que
            //  tiene debajo en el mismo panel, que es lo que `Tests/paneles.py`
            //  llama FILAS. El comentario de aqui ya contaba esta misma
            //  historia con el numero anterior; lo que cambio es que desde que
            //  `layoutModuleBar` alinea sus filas con el rectangulo que recibe,
            //  la referencia es el filo y no `aireTapa`.
            //
            //  Y la celda gana los dos pixeles, que en la fila mas apretada de
            //  la ficha no sobran: CHOKE se queda con su tercio escaso.
            auto celdaChoke = r3.removeFromLeft (w3);
            auto chokeCell = chokeSolo ? celdaChoke
                                       : celdaChoke.withTrimmedRight (Metrics::aireTapa);
            //  JUCE stacks a slider's +/- buttons whenever the space left for
            //  them is taller than it is wide, and on a narrow screen the
            //  readout was eating enough of the cell to trigger exactly that -
            //  two 17-pixel slivers. Reserve the buttons their width first.
            //  70 was a hand-picked number that made CHOKE's keys a different
            //  size from every other stepper's. Same reservation as the rest.
            //  TREINTA Y CUATRO, no treinta: el suelo estaba puesto para un
            //  numero de dos cifras y la casilla dice tambien "off" - y en
            //  arabe "off" es مغلق, que pide 28 px de letra. Con treinta le
            //  quedaban 26 y se encogia. Un suelo que solo cuenta el caso
            //  facil no es un suelo.
            chokeSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false,
                                         juce::jmax (34, chokeCell.getWidth() - Metrics::gap - 2 * Metrics::stepKey),
                                         Metrics::readout);
            chokeSlider.setBounds (chokeCell);
            //  NORMALIZAR va aqui y no en la fila de REV/LOOP porque
            //  pertenece al nivel, y el nivel es esta seccion.
            juce::TextButton* r3b[2] = { &modeButton, &normButton };
            //  Y con CHOKE solo en su fila, MODO y NORMALIZAR bajan a un
            //  renglon propio que sale de lo que quedaba de `inner` - o sea
            //  ENCIMA de filaBaja. El grupo es la union de los dos, que es lo
            //  que se lee: las tres cosas que no son mandos.
            const int gBajo = inner.getY();
            if (chokeSolo)
            {
                inner.removeFromTop (Metrics::xs);
                layoutModuleBar (inner.removeFromTop (Metrics::hit), r3b, 0, 2);
            }
            else
            {
                layoutModuleBar (r3, r3b, 0, 2);
            }
            padGrupos.add (chokeSolo
                             ? filaBaja.getUnion (juce::Rectangle<int> (inner.getX(), gBajo,
                                                                        inner.getWidth(),
                                                                        inner.getY() - gBajo))
                             : filaBaja);
        }

        padSectionArea[1] = {};

        padSectionArea[2] = {};
        zatiSwatchArea = {};
        editInfoArea = {};
        }
        else
        {
        //  RECORTE: la regla, la onda y las tres cosas que se le hacen a la
        //  muestra que se esta mirando.
        const int gRecorte = inner.getY();
        padSectionArea[0] = inner.removeFromTop (secH);   // pintado: RECORTE
        padSectionArea[1] = {};
        padSectionArea[2] = {};

        const int labelW = ZatiLookAndFeel::kTrimLabel;
        auto ctrlRow = [&inner, labelW] (int h) { auto r = inner.removeFromTop (h); r.removeFromLeft (labelW); return r; };
        startSlider.setBounds (ctrlRow (ZatiLookAndFeel::kTrimRow)); inner.removeFromTop (Metrics::xs);
        endSlider.setBounds   (ctrlRow (ZatiLookAndFeel::kTrimRow)); inner.removeFromTop (Metrics::xs);
        //  Los dos fundidos justo debajo de los dos bordes que suavizan, con la
        //  misma forma de fila: rotulo a la izquierda y valor a la derecha. En
        //  otra pagina, o con otra forma, no se leerian como lo que son - lo que
        //  le pasa al borde de arriba.
        fadeInSlider.setBounds  (ctrlRow (ZatiLookAndFeel::kTrimRow)); inner.removeFromTop (Metrics::xs);
        fadeOutSlider.setBounds (ctrlRow (ZatiLookAndFeel::kTrimRow));
        //  LAS CUATRO ASAS SON UN GRUPO: INICIO y FIN dicen DONDE empieza y
        //  acaba el trozo, y los dos SUAVE que le pasa a esos dos bordes. La
        //  fila de debajo -REV, BUCLE, QUITAR RUIDO- es otra cosa: dice COMO se
        //  recorre lo que se acaba de marcar. Cuatro renglones seguidos y una
        //  fila de tapas debajo se leian como cinco cosas en una lista.
        cierra (gRecorte);
        inner.removeFromTop (Metrics::sm);

        //  REV y LOOP viven aqui, con el recorte, y no en la barra de EL PAD:
        //  las dos deciden COMO SE RECORRE el trozo que se acaba de marcar,
        //  igual que START y END deciden cual es. Estaban al lado de AUTOCUT
        //  y BOMBEO, que son cosas del pad y no de la muestra. QUITAR RUIDO va
        //  con ellas por lo mismo: es de la muestra.
        {
            const int gLectura = inner.getY();
            //  CUATRO TAPAS Y NO TRES desde que esta RECORTAR, asi que la fila
            //  se parte donde no caben - con la MISMA pregunta que se hace al
            //  presupuestar el alto (`padMuestraWraps`), o la ficha reserva una
            //  fila que no usa o usa una que no reservo. Es lo que ya pasa con
            //  las puertas del pad y con CHOKE.
            juce::TextButton* pb[4] = { &reverseButton, &loopButton,
                                        &denoiseButton, &recorteButton };
            if (padMuestraWraps (inner.getWidth()))
            {
                auto r1 = inner.removeFromTop (Metrics::hit);
                layoutModuleBar (r1, pb, 0, 2);
                inner.removeFromTop (Metrics::xs);
                auto r2 = inner.removeFromTop (Metrics::hit);
                layoutModuleBar (r2, pb + 2, 0, 2);
            }
            else
            {
                auto rr = inner.removeFromTop (Metrics::hit);
                layoutModuleBar (rr, pb, 0, 4);
            }
            cierra (gLectura);
        }
        inner.removeFromTop (Metrics::sm);
        //  Las muestras de color son de la otra pagina. Sin borrarlo, la tira
        //  se seguia pintando aqui - en las coordenadas donde estaba EN LA
        //  OTRA PAGINA -, que es el fallo clasico de un area guardada en un
        //  miembro y no vuelta a calcular.
        zatiSwatchArea = {};

        //  The cut itself, with its fragments and its draggable trim handles.
        //  It used to be a painted, untouchable card here while the real one
        //  lived on the face; now the interactive one is where the editing is.
        editInfoArea = inner;
        waveform.setBounds (inner);

        //  Las tres tapas del zoom, ENCIMA de la onda y pegadas a su esquina
        //  de abajo a la derecha, que es la unica parte de la pantalla donde
        //  no hay ni rotulo ni asa. Se colocan en coordenadas de la ficha
        //  porque son hermanas de la onda, no hijas suyas: hijas, un arrastre
        //  sobre ellas seria un arrastre sobre la onda.
        {
            //  Solo si queda pantalla debajo de ellas. En apaisado la ficha no
            //  puede pasar de 321 px y a la onda le quedan 56: tres tapas de
            //  40 encima de 56 no son un zoom, son una barra tapando lo unico
            //  que se estaba mirando - y colocadas donde no caben, salen a
            //  altura cero, que es un control que no se puede pulsar.
            const bool room = inner.getHeight() >= 2 * Metrics::hit;
            juce::TextButton* zb[3] = { &zoomOutButton, &zoomFitButton, &zoomInButton };
            for (auto* b : zb) b->setVisible (room);

            if (room)
            {
                //  El aire de abajo se RESERVA, no se recorta de la tapa: la
                //  fila mide Metrics::hit -el minimo justo- y quitarle
                //  halfGap por debajo dejaba las tres en 36 px. Se pide una
                //  fila mas alta y el hueco sale de ella.
                auto strip = inner.removeFromBottom (Metrics::hit + Metrics::halfGap)
                                  .withTrimmedBottom (Metrics::halfGap);
                //  CENTRADAS y no pegadas al filo. Tres tapas de zoom colgando
                //  de una esquina se leen como si sobraran; en medio de la
                //  onda se leen como los mandos de la onda, que es lo que son.
                strip = strip.withSizeKeepingCentre (juce::jmin (3 * Metrics::hit + 2 * Metrics::halfGap,
                                                                strip.getWidth()),
                                                     strip.getHeight());
                const int w = juce::jmax (24, (strip.getWidth() - 2 * Metrics::halfGap) / 3);
                for (int i = 0; i < 3; ++i)
                {
                    zb[i]->setBounds (Lang::takeStart (strip, w));
                    if (i < 2) Lang::takeStart (strip, Metrics::halfGap);
                }
            }
        }
        }
    }

    // La ficha del MANUAL: titulo, subtitulo y todo lo demas es la lista, que
    // se desplaza. Pide de alto lo que le den - hasta el tope del 78% - porque
    // aqui cuanta mas se lea de una vez, mejor.
    {
        auto inner = sheetFromBottom (manualSheet, 1200);
        auto titleRow = inner.removeFromTop (Metrics::hit);
        manualCloseButton.setBounds (Lang::takeEnd (titleRow, Metrics::hit)
                                        .withSizeKeepingCentre (Metrics::hit, Metrics::hit));
        inner.removeFromTop (Metrics::bandaSubtitulo + Metrics::sm);   // pintado: el subtitulo

        manualScroll.setBounds (inner);
        const int barW = manualScroll.getScrollBarThickness();
        manualBody.setSize (juce::jmax (40, inner.getWidth() - barW),
                            juce::jmax (inner.getHeight(),
                                        manualContentHeight (inner.getWidth() - barW)));
    }

    // BROWSE sheet: the tallest of them all — the file list wants the room.
    {
        auto inner = sheetFromBottom (browseSheet, full.getHeight());   // clamps to the 86% cap
        auto titleRow = inner.removeFromTop (Metrics::hit);
        browseCloseButton.setBounds (Lang::takeEnd (titleRow, Metrics::hit).withSizeKeepingCentre (Metrics::hit, Metrics::hit));

        //  CUATRO ACCIONES NO CABEN EN UNA FILA ESTRECHA, igual que las cuatro
        //  pestanas de AJUSTES. Eran tres y entraban; FABRICA las puso en
        //  cuatro y el banco lo canto: en 280x653 "CARGAR KIT" pide 75 px y la
        //  tapa le dejaba 61, y con ella se apretaban LOAD KIT, FACTORY y
        //  تحميل. layoutModuleBar reparte proporcionalmente pero no se NIEGA
        //  cuando no hay sitio: aprieta y sigue.
        //
        //  Se pregunta con la misma cuenta que hace el reparto - misma fuente,
        //  mismo margen - y si no caben, dos filas de dos.
        //  EN MODO CARPETA LAS ACCIONES SON OTRA. Elegir donde cae el rebote no
        //  tiene nada que ver con cargar un sonido, un kit o la fabrica: la
        //  unica accion posible es "esta". Ensenar las otras cuatro apagadas
        //  seria ensenar cuatro controles muertos, que es lo que esta app no
        //  hace desde la tira del paso.
        const bool eligiendoCarpeta = (browseModo == browseCarpeta);
        for (auto* b : { &browseLoadButton, &browseKitButton,
                         &browseFactoryButton, &browseSystemButton,
                         &browseKitsDirButton })
        {
            b->setVisible (! eligiendoCarpeta);
            if (eligiendoCarpeta) b->setBounds ({});
        }
        browseUseDirBtn.setVisible (eligiendoCarpeta);
        if (! eligiendoCarpeta) browseUseDirBtn.setBounds ({});

        if (eligiendoCarpeta)
        {
            auto actions = inner.removeFromBottom (Metrics::btn);
            juce::TextButton* ub[1] = { &browseUseDirBtn };
            layoutModuleBar (actions, ub, 0, 1);
        }
        else
        {
        //  Cinco, y la fila se parte en 2+3 cuando no caben. Arriba las dos
        //  puertas por las que entra el sonido -un fichero a un pad, o el
        //  catalogo entero- y abajo las tres que van a una CARPETA: repartir la
        //  que estas viendo, las tuyas, y el selector del sistema.
        //
        //  Y se parte 2+3 y no 3+2 porque los rotulos largos van donde se
        //  reparte entre menos. Antes el mas largo era "CARGAR KIT" -75 px con
        //  74, y 83 con 80 en arabe-; desde que FABRICA es la puerta de
        //  INSTRUMENTOS el mas largo es ese, que en 280x653 pide 90 y tenia 79:
        //  el unico rotulo CORTADO de las 812 corridas. Sube al par y CARGAR
        //  KIT baja al trio, donde sus 75 caben en 79.
        juce::TextButton* pb[5] = { &browseLoadButton, &browseFactoryButton,
                                    &browseKitButton, &browseKitsDirButton,
                                    &browseSystemButton };
        const bool actionsFit = moduleBarFits (inner.getWidth(), pb, 5);

        if (actionsFit)
        {
            auto actions = inner.removeFromBottom (Metrics::btn);
            layoutModuleBar (actions, pb, 0, 5);
        }
        else
        {
            auto lower = inner.removeFromBottom (Metrics::btn);
            layoutModuleBar (lower, pb + 2, 0, 3);
            inner.removeFromBottom (Metrics::xs);
            auto upper = inner.removeFromBottom (Metrics::btn);
            layoutModuleBar (upper, pb, 0, 2);
        }
        }
        inner.removeFromBottom (Metrics::sm);
        if (browser != nullptr) browser->setBounds (inner);
    }

    // PROJECT sheet: list of saved projects + the four actions.
    {
        //  Height follows the list, instead of claiming 70% of the screen and
        //  leaving whatever the projects did not fill as a white hole. With no
        //  projects saved that hole was most of the card, which reads as
        //  something failing to load rather than as an empty list.
        //  ONE card, whichever page is showing. Its height is the height of
        //  that page: a settings card that stayed as tall as its tallest page
        //  would open with a hole in it half the time.
        const bool onAudio = (setPage == pageAudio);
        const bool onProj  = (setPage == pageProjects);
        const bool onGest  = (setPage == pageGestures);
        const bool onMidi  = (setPage == pageMidi);
        const bool onAsp   = (setPage == pageAspecto);

        const int listRowH = juce::jmax (22, projList.getRowHeight());
        const int listH    = juce::jlimit (1, 8, projModel.names.size()) * listRowH;

        //  Y la altura de la tarjeta cuenta las DOS filas cuando hacen falta,
        //  o la ficha se queda corta y lo que se sale es lo que se maqueta al
        //  final. Se pregunta con el ancho que va a tener el interior.
        //  Y CON EL ANCHO DE LA TARJETA, no con el de la VENTANA.
        //
        //  `setSheet.getWidth()` es el componente que ocupa la pantalla entera,
        //  asi que esta cuenta preguntaba "caben las pestanas" con 412 px
        //  mientras el maquetado las coloca con los 347 del interior de la
        //  tarjeta: una decia que si y el otro que no, se reservaba UNA fila y
        //  se usaban DOS, y lo que se comia la diferencia era la ultima fila que
        //  se maqueta - la de CARCASA, a 4 px de alto. Una cuenta, un dueno.
        const int setInnerW = juce::jmax (1, anchoTarjetaInterior (full.getWidth()));
        const bool tabsFitH = setTabsFit (setInnerW);
        const int tabsH = (tabsFitH ? Metrics::tab : Metrics::tab * 2 + Metrics::xs) + Metrics::sm;
        //  The gestures page is a printed list: one row per gesture, and the
        //  card is exactly as tall as the list is. See paintGesturesPage.
        const int gestRowH = 30;
        //  Y LA FILA DE MANUAL/TOUR SE PIDE AQUI, que es donde no se pedia.
        //
        //  Se le quitaba al mismo rectangulo cien lineas mas abajo, con un
        //  `removeFromBottom` que no aparecia en `wanted`: la tarjeta pedia
        //  alto para seis filas y despues se gastaba 48 px en una fila que
        //  nadie habia presupuestado, asi que a la lista le quedaban 140 px
        //  para 180 de filas y la ULTIMA salia a 20 px -a 16 en 280x653, con
        //  las pestañas partidas en dos-. Es la misma cuenta que ya costo la
        //  fila de CADENA y la REJILLA a 217x0: lo que falta se lo come en
        //  silencio lo ultimo que se maqueta.
        const int gestPieH = Metrics::hit + Metrics::sm;
        //  La pagina de MIDI: dos bloques de rotulo + tapa + selector, y el
        //  texto que explica la nota de cada pad.
        const int midiH = Metrics::md * 2 + Metrics::hit + Metrics::sm + tabsH
                            + (14 + Metrics::hit + Metrics::xs + Metrics::hit + Metrics::sm) * 2
                            + 40 + Metrics::sm;
        //  LOS CHIPS DE IDIOMA Y CARCASA NO CABEN A CUARTOS.
        //
        //  La fila reparte el ancho a partes iguales, y en 280x653 a cada uno
        //  le tocan 46 px: "ESPANOL" pide 52 y salia cortado, y con el ENGLISH,
        //  el arabe y GRAFITO - dieciseis rotulos cortados por repartir a ojo.
        //  Si no caben, dos filas de dos, que es lo que ya hacen las pestanas
        //  de esta misma ficha y la barra de modulos del pad.
        //
        //  La pregunta se hace UNA vez y aqui, porque de la respuesta depende
        //  la altura que se pide: preguntarla otra vez abajo con otro ancho es
        //  como una fila se queda sin sitio. Ver dosColumnasSet.
        //  DOS filas de chips y no cuatro: IDIOMA y CARCASA se fueron a su
        //  propia pagina, aqui quedan BUFER y RELOJ.
        //  TRES filas de chips desde que la cuenta atras esta aqui: BUFER,
            //  RELOJ y CUENTA.
            const int filasChips = 3 * (Metrics::hit + Metrics::xs);
        //  EL RECUADRO DE AUDIO MIDE LO MISMO EN LOS CUATRO SITIOS.
        //
        //  Ese 158 estaba escrito a mano cuatro veces -dos veces para pedir el
        //  alto y dos para colocarlo, con dos aritmeticas distintas- y es
        //  exactamente el tipo de numero que se queda viejo en tres sitios de
        //  cuatro. Es cuanto texto pinta paintAudioInfo, o sea suyo.
        const int estAltoAudio = 16 + Metrics::sm + tabsH + kAltoAudioInfo + Metrics::xs + filasChips
                               + Metrics::xs + 14 + Metrics::hit + Metrics::sm;
        //  DOS COLUMNAS CUANDO LA DE UNA NO CABE, y la pregunta es esa y no
        //  otra. La condicion anterior comparaba la altura consigo misma menos
        //  dos filas -"346 < 334"- y era falsa siempre por doce pixeles, asi
        //  que en 915x412 la ficha se quedaba en una columna que pide 458 px
        //  dentro de una tarjeta que puede medir 346: las cuatro tapas de
        //  IDIOMA salian a 8 px de alto y las cuatro de CARCASA a CERO.
        //  removeFromTop no se queja, devuelve lo que queda.
        //
        //  El tope es el mismo que aplica sheetFromBottom - 0.90 girado, 0.78
        //  de pie - menos el margen vertical de la tarjeta. Escrito aqui
        //  porque aqui es donde se decide, y decidirlo con otro numero es
        //  como se llega a una fila de altura cero.
        const int topeCarta = altoTarjeta (full) - 2 * Metrics::margenFichaY;
        const bool dosColumnasSet = setInnerW >= 560 && estAltoAudio > topeCarta;
        const int anchoChip = (dosColumnasSet ? setInnerW / 2 - Metrics::sm : setInnerW) - 44;
        auto chipsCaben = [this, anchoChip] (juce::OwnedArray<juce::TextButton>& btns)
        {
            juce::TextButton* arr[8] {};
            const int n = juce::jmin (8, btns.size());
            for (int i = 0; i < n; ++i) arr[i] = btns[i];
            return n <= 2 || moduleBarFits (anchoChip, arr, n);
        };
        const bool partirLang = ! chipsCaben (langButtons);
        const bool partirSkin = ! chipsCaben (skinButtons);
        const bool partirMov  = ! chipsCaben (movButtons);
        const int filasExtra = (partirLang ? Metrics::hit + Metrics::xs : 0)
                             + (partirSkin ? Metrics::hit + Metrics::xs : 0)
                             + (partirMov  ? Metrics::hit + Metrics::xs : 0);

        //  CUANTAS FILAS DE CHIPS LLEVA LA PAGINA DE AUDIO: BUFER, RELOJ,
        //  CUENTA, MONITOR y TOMAS. Escrito UNA vez porque lo piden dos sitios —lo que
        //  la pagina pide de alto y lo que el recuadro de AUDIO tiene que
        //  cederles— y ya se pago: el `2 *` de mas abajo se quedo en dos el dia
        //  que entro la CUENTA, asi que la fila de abajo se quedaba con lo que
        //  sobrara. Medido con el MONITOR puesto: tres tapas de 4 px de alto.
        constexpr int kFilasChipsAudio = 5;

        const int wanted = onMidi ? midiH
            : onAudio
            ? Metrics::md * 2 + 16 + Metrics::sm + tabsH + kAltoAudioInfo + Metrics::xs
                + 14 + Metrics::hit + Metrics::sm
                + (Metrics::hit + Metrics::xs) * kFilasChipsAudio + Metrics::sm
            : onAsp
              ? Metrics::md * 2 + 16 + Metrics::sm + tabsH
                  + (Metrics::hit + Metrics::xs) * 3 + filasExtra + Metrics::sm
            : onGest
              ? Metrics::md * 2 + 16 + Metrics::sm + tabsH
                  + kNumGestures * gestRowH + gestPieH + Metrics::sm
              : Metrics::md * 2 + 16 + 14 + Metrics::sm + tabsH
                  + Metrics::hit + 14 + Metrics::sm
                  + Metrics::btn * 2 + Metrics::xs * 2 + 8 + listH + Metrics::sm;

        auto inner = sheetFromBottom (setSheet, wanted);

        //  EL TITULO SOLO, Y LA X CON LAS PESTANAS.
        //
        //  La x colgaba de un renglon de 40 px que no llevaba nada mas, asi que
        //  entre el nombre de la ficha y la fila de pestanas quedaba una banda
        //  vacia que no era ni aire ni contenido. Ahora el titulo ocupa lo que
        //  mide -16 px- y la x se va a la fila de las pestanas, que es donde
        //  hay un renglon de verdad: mismo alto, misma linea, y veinticuatro
        //  pixeles menos de tarjeta.
        inner.removeFromTop (Metrics::bandaTitulo);

        if (onProj) inner.removeFromTop (Metrics::bandaSubtitulo);   // painted: which project
        inner.removeFromTop (Metrics::sm);

        //  The tab row, directly under the title on both pages so it does not
        //  move when you switch.
        {
            //  CUATRO PESTANAS NO CABEN EN UNA FILA ESTRECHA.
            //
            //  Eran tres y entraban; la de MIDI las puso en cuatro y el banco
            //  lo canto en la corrida siguiente: en 280x653 "PROYECTOS" pide 56
            //  px de letra y la tapa le dejaba 42, y con ella se recortaban
            //  tambien GESTOS, PROJECTS y المشاريع. Dieciseis rotulos cortados
            //  por una pestana nueva.
            //
            //  No se arregla acortando los rotulos - "PROYS" no es una palabra -
            //  sino preguntando si caben, que es lo que padRowFits ya hacia para
            //  la ficha del pad. Si no caben, dos filas de dos: la tarjeta crece
            //  32 px en el movil mas estrecho que existe y en todos los demas se
            //  queda como estaba.
            const bool tabsFit = setTabsFit (inner.getWidth());
            juce::TextButton* tb[5] = { &pageAudioBtn, &pageMidiBtn, &pageAspBtn,
                                        &pageProjBtn, &pageGestBtn };
            auto reparte = [] (juce::Rectangle<int> row, juce::TextButton** b, int n)
            {
                for (int i = 0; i < n; ++i)
                {
                    const int w = row.getWidth() / (n - i);
                    b[i]->setBounds ((i == n - 1 ? row : Lang::takeStart (row, w))
                                         .reduced (Metrics::aireTapa, 0));
                }
            };

            {
                //  LA X VIVE EN ESTA FILA. Ver arriba: sola en su renglon
                //  dejaba una banda vacia entre el titulo y las pestanas.
                auto primera = inner.removeFromTop (Metrics::tab);
                setCloseButton.setBounds (Lang::takeEnd (primera, Metrics::hit)
                                              .withSizeKeepingCentre (Metrics::hit, Metrics::hit));
                Lang::takeEnd (primera, Metrics::xs);

                if (tabsFit)
                {
                    reparte (primera, tb, 5);
                }
                else
                {
                    //  Tres arriba y dos abajo: los rotulos largos van donde se
                    //  reparte entre MENOS, y los tres cortos -AUDIO, MIDI,
                    //  ASPECTO- aguantan el reparto entre tres.
                    reparte (primera, tb, 3);
                    inner.removeFromTop (Metrics::xs);
                    reparte (inner.removeFromTop (Metrics::tab), tb + 3, 2);
                }
            }

            //  Estas cinco se reparten a mano y no por layoutModuleBar, asi que
            //  hay que preguntarles aparte: sin esto la fila salia con AUDIO,
            //  MIDI y GESTOS dibujados y PROYECTOS -que es la palabra larga- con
            //  un hueco.
            filaDeIconos (tb, 5);
            inner.removeFromTop (Metrics::sm);

            //  LOS GRUPOS DE ESTA FICHA. Ver el mismo bloque en EL PAD: se
            //  apuntan mientras se reparte el alto, con las coordenadas que la
            //  maqueta acaba de dar, asi que no cuestan un pixel.
            setGrupos.clear();

            //  Whatever is left of the card belongs to the gestures list.
            if (onGest)
            {
                //  El boton del manual, al pie de la pagina de gestos: los
                //  gestos son la mitad de las preguntas y el manual es la otra
                //  mitad, asi que estan en el mismo sitio.
                //  MANUAL y TOUR comparten renglon y se reparten por el TEXTO
                //  que llevan: a mitades, "MANUAL" y الدليل caben y 参数锁定 no,
                //  y repartir a ojo es como se cortaron dieciseis rotulos en la
                //  fila de IDIOMA.
                manualButton.setVisible (true);
                tourButton.setVisible (true);
                {
                    auto row = inner.removeFromBottom (Metrics::hit);
                    juce::TextButton* mb[2] = { &manualButton, &tourButton };
                    layoutModuleBar (row, mb, 0, 2);
                }
                inner.removeFromBottom (Metrics::sm);
                gesturesArea = inner;
            }
            else
            {
                //  Y con sus limites vaciados: un componente invisible que
                //  conserva sus coordenadas sigue estando ahi para todo lo que
                //  mida geometria. Es el fallo de las tapas de banco - y estaba
                //  aplicado a UNA de las dos tapas de esta fila, con el
                //  comentario que lo explica escrito justo entre las dos.
                manualButton.setVisible (false);
                manualButton.setBounds ({});
                tourButton.setVisible (false);
                tourButton.setBounds ({});
                gesturesArea = {};
            }
        }

        if (onMidi)
        {
            auto block = [this, &inner] (juce::TextButton& btn, juce::ComboBox& box)
            {
                //  El rotulo, el interruptor y la lista de puertos son UN
                //  grupo: SALIDA y ENTRADA son dos cosas y aqui se leian como
                //  cuatro filas seguidas.
                const int g0 = inner.getY();
                inner.removeFromTop (Metrics::bandaSubtitulo);       // pintado: el rotulo
                auto row = inner.removeFromTop (Metrics::hit);
                btn.setBounds (Lang::takeStart (row, juce::jmax (96, row.getWidth() / 3))
                                   .reduced (Metrics::aireTapa, 0));
                inner.removeFromTop (Metrics::xs);
                box.setBounds (inner.removeFromTop (Metrics::hit).reduced (Metrics::aireTapa, 0));
                setGrupos.add ({ inner.getX(), g0, inner.getWidth(), inner.getY() - g0 });
                inner.removeFromTop (Metrics::sm);
            };
            block (midiOutBtn, midiOutBox);
            block (midiInBtn,  midiInBox);
            midiArea = inner.removeFromTop (40);                // pintado: la nota
            audioInfoArea = bufRowArea = rateRowArea = langRowArea = skinRowArea = movRowArea = {};
            cuentaRowArea = monRowArea = tomasRowArea = {};
            for (auto* b : cuentaButtons) if (b != nullptr) { b->setVisible (false); b->setBounds ({}); }
            for (auto* b : monButtons)    if (b != nullptr) { b->setVisible (false); b->setBounds ({}); }
            for (auto* b : tomasButtons)  if (b != nullptr) { b->setVisible (false); b->setBounds ({}); }
            pruebasLabelArea = {};
            projNameRowArea = projPathRowArea = {};
        }
        else if (onAudio)
        {
            midiArea = {};

            //  DOS COLUMNAS CUANDO LA TARJETA ES ANCHA Y BAJA.
            //
            //  Esta pagina pide 158 px de informacion mas cuatro filas de chips
            //  - buffer, frecuencia, idioma y carcasa -, o sea unos 350. Con el
            //  movil girado la tarjeta se queda en el 78% de 412, y despues del
            //  titulo y las pestanas quedan unos 200: las dos ultimas filas se
            //  iban al vacio. removeFromTop de un rectangulo agotado no falla,
            //  devuelve altura CERO, asi que los ocho chips de IDIOMA y CARCASA
            //  salian de 189x0 - existentes, invisibles e IMPOSIBLES DE PULSAR.
            //
            //  Medido en 915x412: 8 controles de altura cero en esta ficha, y el
            //  selector de idioma entre ellos. Girar el telefono dejaba la app
            //  sin forma de cambiar de idioma.
            //
            //  Apaisado, la tarjeta sobra de ancho -841 px- y falta de alto. Dos
            //  columnas cambian exactamente eso: la informacion a un lado, los
            //  cuatro chips al otro, y la altura que hace falta se parte por la
            //  mitad.
            const bool dosColumnas = dosColumnasSet;

            //  LAS TRES ACCIONES DE AUDIO, EN SU SECCION.
            //
            //  Colgaban del renglon del TITULO -lo que sobra a la derecha del
            //  nombre de la ficha- y de ahi salian dos cosas mal: en la pantalla
            //  ancha se leian como parte de la cabecera, pegadas a la x, y en la
            //  estrecha no cabian y bajaban a una fila propia, asi que la misma
            //  ficha tenia dos sitios distintos para lo mismo segun el telefono.
            //  Ahora es siempre una seccion de la pagina de AUDIO, que es lo que
            //  son: MEDIR mide la salida, CUADRAR la compensa y TEST manda un
            //  tono por ella.
            auto ponPruebas = [this] (juce::Rectangle<int>& donde)
            {
                donde.removeFromTop (Metrics::xs);
                pruebasLabelArea = donde.removeFromTop (Metrics::bandaSubtitulo);
                auto fila = donde.removeFromTop (Metrics::hit);
                juce::TextButton* ab[3] = { &quantButton, &measureButton, &testButton };
                layoutModuleBar (fila, ab, 0, 3);
                //  El rotulo PRUEBAS y sus tres tapas son UNA cosa, igual que en
                //  el secuenciador el nombre y el mando que lleva debajo.
                setGrupos.add (pruebasLabelArea.getUnion (fila));
                donde.removeFromTop (Metrics::sm);
            };

            juce::Rectangle<int> columnaChips = inner;
            if (dosColumnas)
            {
                auto izda = inner.removeFromLeft (inner.getWidth() / 2 - Metrics::sm);
                inner.removeFromLeft (Metrics::sm);
                audioInfoArea = izda.removeFromTop (juce::jmin (kAltoAudioInfo, izda.getHeight()));
                ponPruebas (izda);
                columnaChips = inner;
            }
            else
            {
                //  LO QUE NO PUEDE ENCOGER SE APARTA PRIMERO. El recuadro de
                //  AUDIO son 158 px de texto PINTADO: encoge sin que nadie se
                //  entere. Las cuatro filas de chips no, y en 280x653 la
                //  tarjeta tiene 485 px para 558 de contenido, asi que
                //  removeFromTop le daba a CARCASA lo que quedaba - ACERO y
                //  LACA a CERO de alto, existentes e imposibles de tocar.
                //  Es el mismo fallo que el TEMPO del secuenciador, contado en
                //  otro sitio.
                //  Y las tres pruebas cuentan como mueble que no encoge, igual
                //  que los chips: si no se restan aqui, el recuadro se queda
                //  con su altura entera y la fila de PRUEBAS se cae por abajo.
                const int chipsNecesarios = kFilasChipsAudio * (Metrics::hit + Metrics::xs)
                                          + Metrics::xs + 14 + Metrics::hit + Metrics::sm;
                audioInfoArea = inner.removeFromTop (
                                    juce::jlimit (0, kAltoAudioInfo, inner.getHeight() - Metrics::xs - chipsNecesarios));
                inner.removeFromTop (Metrics::xs);
                ponPruebas (inner);
                columnaChips = inner;
            }

            //  Repartidos POR EL TEXTO QUE LLEVAN y no a partes iguales, y en
            //  dos filas cuando ni asi caben. `partir` viene de arriba, de
            //  donde se pidio la altura: decidirlo aqui otra vez con otro
            //  ancho es como una fila se queda con altura cero.
            auto chipRow = [this, &columnaChips] (juce::OwnedArray<juce::TextButton>& btns,
                                                  int labelW, bool partir)
            {
                juce::TextButton* arr[8] {};
                const int n = juce::jmin (8, btns.size());
                for (int i = 0; i < n; ++i) arr[i] = btns[i];

                auto row = columnaChips.removeFromTop (Metrics::hit);
                auto r = row;
                Lang::takeStart (r, labelW);

                //  CERO TAPAS ES UN CASO REAL: resized() corre desde el
                //  constructor -por retranslateUi- y ahi estas listas todavia
                //  estan vacias. layoutModuleBar pide el texto de la primera y
                //  la primera es un puntero nulo: la app se cerraba al
                //  arrancar, y la caja negra lo dijo en una linea, "CAIDA senal
                //  11 en arranque".
                if (n <= 0) { columnaChips.removeFromTop (Metrics::xs); return row; }

                if (! partir || n <= 2)
                {
                    layoutModuleBar (r, arr, 0, n);
                }
                else
                {
                    const int mitad = (n + 1) / 2;
                    layoutModuleBar (r, arr, 0, mitad);
                    columnaChips.removeFromTop (Metrics::xs);
                    auto row2 = columnaChips.removeFromTop (Metrics::hit);
                    auto r2 = row2;
                    Lang::takeStart (r2, labelW);
                    layoutModuleBar (r2, arr + mitad, 0, n - mitad);
                    row = row.getUnion (row2);
                }
                columnaChips.removeFromTop (Metrics::xs);
                return row;
            };
            bufRowArea  = chipRow (bufButtons,  Metrics::canalonSeccion, false);
            rateRowArea = chipRow (rateButtons, Metrics::canalonSeccion, false);
            //  Y LA CUENTA ATRAS, en la misma columna y con el mismo canalon.
            //  Tres chips, no cuatro: son SIN, 1 y 2 - `armaCuentaAtras` admite
            //  hasta ocho compases y ofrecer ocho seria ofrecer siete que nadie
            //  usa. El clic no tiene chip porque su tapa ya existe en CANCION.
            cuentaRowArea = chipRow (cuentaButtons, Metrics::canalonSeccion, false);
            //  Y EL MONITOR, dos chips en la misma columna: es la otra mitad de
            //  «como se prepara una toma», y como la cuenta es una preferencia
            //  de la persona y de su aparato.
            monRowArea = chipRow (monButtons, Metrics::canalonSeccion, false);
            //  Y EL BANCO DE TOMAS, la tercera de la misma pregunta: donde cae
            //  lo que grabes. Cuatro chips, las mismas letras que la fila de
            //  bancos de la cara.
            tomasRowArea = chipRow (tomasButtons, Metrics::canalonSeccion, false);
            //  UN panel para las DOS, y no uno por fila: entre ellas hay
            //  `Metrics::xs` -cuatro- y dos paneles que se salen dos por lado
            //  dejan CERO de hueco, que se lee igual que no dibujar ninguno. Es
            //  el mismo intento fallido que ya esta contado aqui abajo para
            //  BUFER y RELOJ, y `Tests/paneles.py` lo canto en la primera
            //  corrida: PEGADOS 28.
            //
            //  Y ademas es la lectura correcta: las dos son «como se prepara
            //  una toma» -cuantos compases para coger aire y si te oyes por los
            //  cascos- contra el reloj del aparato de abajo.
            if (cuentaButtons.size() + monButtons.size() + tomasButtons.size() > 0)
                setGrupos.add (cuentaRowArea.getUnion (monRowArea).getUnion (tomasRowArea));
            //  UN panel para las dos filas y no uno por fila, que fue el primer
            //  intento y salio igual que no dibujar nada: entre BUFER y RELOJ
            //  hay Metrics::xs -cuatro- y el panel se sale dos por arriba y dos
            //  por abajo, asi que los dos se TOCABAN y se leian como una losa.
            //  Es el mismo fallo que ya costo un intento en el secuenciador,
            //  alli con cuatro pixeles por lado en vez de dos.
            //
            //  Y ademas es la lectura correcta: los dos son el reloj del
            //  aparato -cuanto tarda en contestar y a que velocidad va- contra
            //  las tres PRUEBAS de arriba, que son cosas que se HACEN.
            //  Y SOLO SI HAY CHIPS QUE ENVOLVER. Sin dispositivo de audio las
            //  dos listas estan vacias -chipRow devuelve la fila igual, que es
            //  correcto: el renglon existe- y quedaba una losa de 88 px detras
            //  de dos rotulos y de nada mas. Un panel dice "estos van juntos";
            //  sin estos, no dice nada. Lo saco Tests/paneles.py como VACIO en
            //  28 corridas.
            if (bufButtons.size() + rateButtons.size() > 0)
                setGrupos.add (bufRowArea.getUnion (rateRowArea));
            //  IDIOMA y CARCASA viven ahora en su pagina.
            langRowArea = skinRowArea = movRowArea = {};
            projNameRowArea = projPathRowArea = {};
        }
        else if (onAsp)
        {
            //  LA PAGINA DE ASPECTO: el idioma y la carcasa, que es lo unico
            //  de esta ficha que cambia como SE VE la maquina. Estaban en AUDIO
            //  al lado del reloj y del bufer porque ahi habia sitio.
            midiArea = audioInfoArea = bufRowArea = rateRowArea = cuentaRowArea = monRowArea = tomasRowArea = {};
            for (auto* b : cuentaButtons) if (b != nullptr) { b->setVisible (false); b->setBounds ({}); }
            for (auto* b : monButtons)    if (b != nullptr) { b->setVisible (false); b->setBounds ({}); }
            for (auto* b : tomasButtons)  if (b != nullptr) { b->setVisible (false); b->setBounds ({}); }
            pruebasLabelArea = {};
            projNameRowArea = projPathRowArea = {};

            juce::Rectangle<int> columnaChips = inner;
            auto chipRow = [this, &columnaChips] (juce::OwnedArray<juce::TextButton>& btns,
                                                  int labelW, bool partir)
            {
                juce::TextButton* arr[8] {};
                const int n = juce::jmin (8, btns.size());
                for (int i = 0; i < n; ++i) arr[i] = btns[i];

                auto row = columnaChips.removeFromTop (Metrics::hit);
                auto r = row;
                Lang::takeStart (r, labelW);
                //  Cero tapas es un caso real: resized() corre desde el
                //  constructor y estas listas estan vacias. Ver el mismo
                //  guardia en la pagina de AUDIO.
                if (n <= 0) { columnaChips.removeFromTop (Metrics::xs); return row; }

                if (! partir || n <= 2)
                {
                    layoutModuleBar (r, arr, 0, n);
                }
                else
                {
                    const int mitad = (n + 1) / 2;
                    layoutModuleBar (r, arr, 0, mitad);
                    columnaChips.removeFromTop (Metrics::xs);
                    auto row2 = columnaChips.removeFromTop (Metrics::hit);
                    auto r2 = row2;
                    Lang::takeStart (r2, labelW);
                    layoutModuleBar (r2, arr + mitad, 0, n - mitad);
                    row = row.getUnion (row2);
                }
                columnaChips.removeFromTop (Metrics::xs);
                return row;
            };
            langRowArea = chipRow (langButtons, Metrics::canalonSeccion, partirLang);
            skinRowArea = chipRow (skinButtons, Metrics::canalonSeccion, partirSkin);
            movRowArea  = chipRow (movButtons,  Metrics::canalonSeccion, partirMov);
            //  Y ESTA PAGINA SIGUE SIN PANELES con tres filas por lo mismo que
            //  con dos: son filas de la MISMA pregunta -como se ve la maquina-
            //  asi que un panel las cubriria todas, y eso agrupa exactamente lo
            //  mismo que no dibujar nada. Un panel dice "estos van juntos y
            //  esos no", y aqui sigue sin haber esos.
        }
        else
        {
            midiArea = {};
            skinRowArea = {};
            projNameRowArea = inner.removeFromTop (Metrics::hit);
            {
                auto r = projNameRowArea;
                Lang::takeStart (r, 60);
                //  Aire SOLO a los lados. El renglon mide Metrics::hit -40, el
                //  minimo- y quitarle cuatro por arriba y cuatro por abajo
                //  dejaba la caja donde se escribe el nombre del proyecto en
                //  32: es el mismo reduced (x, n) que ya costo las tapas de la
                //  mesa y las de la cadena.
                projNameBox.setBounds (r.reduced (Metrics::aireTapa, 0));
            }
            projPathRowArea = inner.removeFromTop (Metrics::bandaSubtitulo);
            //  El nombre del proyecto y la ruta donde vive son UNA cosa; las
            //  seis tapas de abajo, otra. Sin panel, la caja de escribir se
            //  leia como una fila mas de la lista que hay debajo.
            setGrupos.add (projNameRowArea.getUnion (projPathRowArea));
            inner.removeFromTop (Metrics::sm);

            const int gAcciones = inner.getBottom();
            {
                auto fila = inner.removeFromBottom (Metrics::btn);
                juce::TextButton* pe[2] = { &projExportButton, &projKitButton };
                layoutModuleBar (fila, pe, 0, 2);
            }
            inner.removeFromBottom (Metrics::xs);

            //  Repartidas POR EL TEXTO y no a cuartos: en 280x653 a cada una le
            //  tocaban 43 px y "GUARDAR" pide 52, asi que salia cortada y
            //  BORRAR apretada. Es la misma barra que usan los modulos.
            {
                auto actions = inner.removeFromBottom (Metrics::btn);
                juce::TextButton* pa[4] = { &projSaveButton, &projLoadButton,
                                            &projNewButton,  &projDeleteButton };
                layoutModuleBar (actions, pa, 0, 4);
            }
            setGrupos.add ({ inner.getX(), inner.getBottom(),
                             inner.getWidth(), gAcciones - inner.getBottom() });
            inner.removeFromBottom (Metrics::sm);

            projList.setBounds (inner);
            bufRowArea = rateRowArea = langRowArea = movRowArea = audioInfoArea = {};
        }
    }

    // EXPORT sheet: what will be rendered, then the two products.
    {
        //  Con la fila de CAMBIAR contada: pedir sin ella y colocarla igual es
        //  como una fila se queda con altura cero, que en esta app ya tiene
        //  nombre y medidas.
        //  Y CON LA FILA DE EN VIVO CONTADA, por lo mismo. El banco lo canto en
        //  la primera corrida: la tapa salia a 28 px de alto en las siete
        //  pantallas -pedida sin ella, `removeFromBottom` devuelve lo que
        //  queda- o sea 28 TOUCH nuevos de un solo control.
        auto inner = sheetFromBottom (exportSheet, 32 + 96 + Metrics::hit + Metrics::sm
                                                     + Metrics::btn * 2 + Metrics::sm * 2
                                                     + Metrics::hit + Metrics::sm);
        exportGrupos.clear();

        auto titleRow = inner.removeFromTop (Metrics::hit);
        exportCloseButton.setBounds (Lang::takeEnd (titleRow, Metrics::hit).withSizeKeepingCentre (Metrics::hit, Metrics::hit));

        inner.removeFromTop (96);   // painted: source, length, destination, status

        //  CAMBIAR va arriba del todo de lo tocable, pegado a la linea pintada
        //  que dice el destino: una tapa que cambia un dato tiene que estar al
        //  lado del dato, no en la fila de las acciones finales - donde se leeria
        //  como una tercera forma de exportar.
        {
            auto fila = inner.removeFromTop (Metrics::hit);
            juce::TextButton* db[1] = { &exportDirBtn };
            layoutModuleBar (Lang::takeEnd (fila, juce::jmin (fila.getWidth(),
                                                              juce::jmax (110, fila.getWidth() / 3))),
                             db, 0, 1);
            //  Y EL DESTINO NO LLEVA PANEL, que es la misma decision que la
            //  pagina de ASPECTO y la pagina SONIDO de EL PAD: es UNA tapa, y
            //  un panel alrededor de un solo control no agrupa nada — con el
            //  de abajo ya hay UNA frontera y se ve. (El primer intento
            //  ademas envolvia el hueco: `Lang::takeEnd` MUTA la fila, asi que
            //  lo que quedaba era el lado por el que no hay nada.)
            inner.removeFromTop (Metrics::sm);
        }

        auto row = inner.removeFromBottom (Metrics::btn);
        exportCancelButton.setBounds (row);
        //  Y EL REBOTE EN VIVO, en su PROPIA fila justo encima. MASTER y PISTAS
        //  son dos PRODUCTOS del mismo rebote offline; esto es otro MODO, y una
        //  cuarta palabra en esa fila le quitaria ancho a las tres que ya estan
        //  medidas -se reparten por el texto-.
        {
            auto fila = inner.removeFromBottom (Metrics::hit);
            inner.removeFromBottom (Metrics::sm);
            juce::TextButton* lb[1] = { &exportLiveButton };
            layoutModuleBar (fila, lb, 0, 1);
        }
        //  Tres tapas: el formato primero porque se elige ANTES de decidir si
        //  es master o pistas, y repartidas por el texto - "PISTAS" y "MASTER"
        //  no miden lo mismo que "WAV".
        {
            juce::TextButton* eb[3] = { &exportFmtBtn, &exportMasterButton, &exportStemsButton };
            layoutModuleBar (row, eb, 4, 3);
        }

        //  Y LAS DOS FILAS DE ABAJO EN UN SOLO PANEL, que es el intento fallido
        //  que esta casa ya escribio tres veces -los tres de la tira del paso y
        //  el par MONITOR/CUENTA de AJUSTES-: entre ellas hay `Metrics::sm` y
        //  cada panel se sale `panelAireY` por lado, asi que dos saldrian
        //  TOCANDOSE, que se lee igual que no dibujar ninguno. Y ademas es la
        //  lectura buena: el formato, el master, las pistas y el rebote en vivo
        //  son todos «como sale el fichero». CANCELAR se queda fuera porque no
        //  es una forma de exportar, es no hacerlo.
        {
            auto fila = row;
            for (auto* b : { (juce::Component*) &exportLiveButton })
                if (b->isVisible() && ! b->getBounds().isEmpty())
                    fila = fila.getUnion (b->getBounds());
            if (! fila.isEmpty()) exportGrupos.add (fila);
        }
    }

    // RACK sheet: which pad, and how much of it reaches each effect.
    {
        const int chipRowH = Metrics::hit;
        //  sheetFromBottom takes the card's OUTER height and hands back the
        //  inside, so the vertical margin it removes has to be part of what we
        //  ask for - without it the last send row fell off the bottom edge.
        //  LA FILA SE QUEDA COMO ESTABA, y eso es una vuelta atras medida.
        //
        //  La miniatura de un efecto estuvo aqui una tanda: la fila crecia a 74
        //  px y la mitad derecha se partia en horizontal. Se deshizo mirando la
        //  foto — el dibujo quedaba flotando encima del fader, sin alinear con
        //  el canalon, y la fila entera perdia el orden que tenia. Y ademas el
        //  sitio era el equivocado: lo que dice QUE ES un efecto va donde el EQ
        //  ya lo dice, o sea en el PLATO. Ver FxMini.h y layoutFace.
        const int filaFx = 48;

        //  Y LA CABECERA RESERVA LO QUE SE PINTA.
        //
        //  Reservaba `Metrics::hit` para el titulo y 14 px mas «painted: which
        //  pad this is», y el pintor dibuja el titulo en 0..16 y la ayuda en
        //  16..30 — o sea los dos DENTRO del renglon de 40, con la banda de 14
        //  vacia debajo. No solapaba nada -por eso TAPADO daba 0- y son catorce
        //  pixeles muertos con la reserva en un sitio y el dibujo en otro, que
        //  es exactamente como se acaba pintando encima de algo. La cruz mide
        //  un dedo y manda sobre el alto de la banda; el texto cabe debajo.
        const int cabecera = juce::jmax (Metrics::hit, 16 + 14);
        auto inner = sheetFromBottom (rackSheet, Metrics::md * 2 + cabecera
                                                   + (chipRowH + Metrics::xs) * 4
                                                   + Metrics::sm + kNumRanuras * filaFx + Metrics::sm);
        auto titleRow = inner.removeFromTop (cabecera);
        rackCloseButton.setBounds (Lang::takeEnd (titleRow, Metrics::hit).withSizeKeepingCentre (Metrics::hit, Metrics::hit));

        //  CUATRO POR CUATRO, COMO LA CARA. El rack elige un pad del banco que
        //  esta en pantalla; los otros cuarenta y ocho se esconden en vez de
        //  colocarse, que el rack es "cual de ESTOS dieciseis estoy mandando"
        //  y no un directorio.
        //
        //  Eran dos filas de ocho y en 280 px le tocaban 26 px de ancho a cada
        //  tapa - dos tercios de un dedo, y no hay reparto que lo arregle: ocho
        //  por cuarenta son 320 y la tarjeta mide 225. En cuatro por cuatro
        //  cada una pasa a 56, y ademas queda con la MISMA forma que la rejilla
        //  de pads de la cara, asi que el numero 07 esta donde la mano ya sabe.
        //  Cuesta dos filas mas, que con la ficha desplazandose no duelen.
        //
        //  Y de abajo arriba, tambien como la cara: el 01 abajo a la izquierda.
        //  Numerar al reves aqui seria un mapa distinto del mismo instrumento.
        for (auto* b : rackPadBtns) if (b != nullptr) b->setVisible (false);

        for (int r = 0; r < 4; ++r)
        {
            auto row = inner.removeFromTop (chipRowH);
            const int w = row.getWidth() / 4;
            for (int c = 0; c < 4; ++c)
            {
                //  Y SIN EL BANCO, que es lo que cambia desde que el selector
                //  elige CANAL: dieciseis canales caben exactos en cuatro por
                //  cuatro, asi que no hay sesenta y cuatro entre los que pasear
                //  ni una fila de bancos que mantener.
                const int i = (3 - r) * 4 + c;
                rackPadBtns[i]->setVisible (true);
                //  Sin aire VERTICAL: la fila ya mide Metrics::hit y quitarle
                //  un pixel por arriba y otro por abajo deja las tapas dos por
                //  debajo del dedo minimo para ganar un hueco que la fila de al
                //  lado ya paga con Metrics::xs. Es el mismo reduced (x, n) que
                //  costo 656 tapas en la cara.
                rackPadBtns[i]->setBounds ((c < 3 ? row.removeFromLeft (w) : row)
                                               .reduced (Metrics::aireTapaDensa, 0));
            }
            inner.removeFromTop (Metrics::xs);
        }
        inner.removeFromTop (Metrics::sm);

        //  The name of the effect is painted in the gutter, so the fader gets
        //  the width instead of a label component competing for it.
        //
        //  Y EN DOS COLUMNAS CUANDO NO CABEN LOS SEIS.
        //
        //  Seis filas de 48 son 288 px, y con el telefono girado la tarjeta se
        //  queda en 321 menos titulo, nombre y las dos filas de pads: los tres
        //  ultimos envios salian de 751x0. Un fader de altura cero es un fader
        //  que no existe - medido, tres de seis - y ademas ancho: 751 px de
        //  ancho para un mando que no se puede tocar, mientras la tarjeta sobra
        //  de anchura por los dos lados.
        //
        //  Tres y tres. Lo que falta de alto lo hay de ancho, que es lo mismo
        //  que hace la pagina de AUDIO y lo que hace la cara con wideFace.
        //  (`filaFx` se decide arriba, junto a lo que la tarjeta pide: quien
        //  reserva y quien coloca tienen que contar lo mismo.)

        //  UNA FILA DEL RACK: el canalon a la izquierda -que desde esta tanda
        //  es una TAPA y no texto pintado, porque es la puerta al menu de la
        //  ranura- y el fader con lo que queda.
        //
        //  Seis pixeles arriba y abajo de una fila de 48 dejan el fader en 36,
        //  cuatro por debajo del dedo. El aire entre filas ya lo da la fila
        //  siguiente; el que se le quita al control sale del control.
        auto colocaFilaRack = [this] (int s, juce::Rectangle<int> row)
        {
            auto canalon = Lang::takeStart (row, 54);
            if (rackSlotBtns[s] != nullptr)
                rackSlotBtns[s]->setBounds (canalon.reduced (Metrics::aireTapaDensa, Metrics::halfGap));
            //  Y LA TAPA DE APAGAR, entre el canalon y el fader.
            //
            //  Lo que cuesta sale del FADER y no del canalon, que es donde
            //  sobra ancho: medido, en 412x915 el fader pasa de 289 px a 245 y
            //  en la tarjeta mas estrecha de 157 a 113 — sigue muy por encima
            //  del dedo, que es lo que `expo.py` mide. Quitarselo al canalon
            //  habria dejado «FLT» sin sitio para su dibujo, que es la mitad de
            //  lo que esa tapa dice.
            //  Y DOS PIXELES MAS DE LOS QUE MIDE EL DEDO, que es lo que el
            //  `reduced (1, 4)` de abajo se lleva: pedir `Metrics::hit` clavado
            //  deja la tapa en 38 y el banco lo canta -medido, 38x40-. Es el
            //  mismo `reduced` que ya cuesta dos pixeles en el canalon, contado
            //  donde se PIDE y no donde se coloca.
            auto mute = Lang::takeStart (row, Metrics::hit + 2);
            if (rackMuteBtns[s] != nullptr)
                rackMuteBtns[s]->setBounds (mute.reduced (Metrics::aireTapaDensa, Metrics::halfGap));
            if (rackSends[s] != nullptr)
                rackSends[s]->setBounds (row.reduced (Metrics::aireTapaDensa, Metrics::halfGap));
        };
        const bool dosCol = inner.getWidth() >= 560 && inner.getHeight() < kNumRanuras * filaFx;
        if (dosCol)
        {
            auto izda = inner.removeFromLeft (inner.getWidth() / 2 - Metrics::sm);
            inner.removeFromLeft (Metrics::sm);
            for (int f = 0; f < kNumRanuras; ++f)
            {
                auto& col = (f < kNumRanuras / 2) ? izda : inner;
                auto row = col.removeFromTop (filaFx);
                colocaFilaRack (f, row);
            }
        }
        else
        {
            for (int f = 0; f < kNumRanuras; ++f)
            {
                auto row = inner.removeFromTop (filaFx);
                colocaFilaRack (f, row);
            }
        }
    }

    //  INSTRUMENTOS: el pack, la rejilla de dieciseis y una linea que dice
    //  donde va a caer lo que se toque.
    {
        const int filaPack = Metrics::hit;
        const int filaInst = Metrics::hit;
        //  UNA LISTA Y NO UNA REJILLA, que es lo que el banco corrigio.
        //
        //  Empezo en cuatro por cuatro copiando el selector del RACK y la cara,
        //  y esa forma es para NUMEROS: el 07 esta donde la mano ya sabe porque
        //  ocupa una posicion, no porque se lea. Un instrumento es una PALABRA,
        //  y en 360x640 una celda de esa rejilla mide 62 px: "ELECTRIC PIANO"
        //  pide 120. Medido: 121 rotulos CORTADOS y 61 apretones nuevos, o sea
        //  el arreglo de maquetado que esta casa ya ha rechazado dos veces -
        //  cambiar un apreton por un corte no es un arreglo.
        //
        //  De ancho entero, hasta la pantalla mas estrecha da 217 px, y una
        //  lista de nombres se lee de arriba abajo. Sale mas alta que la
        //  tarjeta y por eso esta ficha se desplaza.
        const int cuantos = (instPack >= 0 && instPack < (int) instCatalogo.size())
                                ? (int) instCatalogo[(size_t) instPack].instr.size() : 0;

        //  REJILLA O LISTA, y no es una preferencia: son dos cosas distintas.
        //
        //  Un instrumento de SINTES va a UN pad, asi que el menu puede tener la
        //  forma del sitio donde va - la rejilla de pads - y encima se elige el
        //  destino tocandolo. Un pack de disco o un banco de fabrica van a un
        //  banco ENTERO: no hay un pad por instrumento que enseñar, sus nombres
        //  no caben en una celda de 56 px y no tienen dibujo que los sustituya.
        //  Esos se quedan en lista, que es donde un nombre se lee.
        const bool rejilla = (instPack >= 0 && instPack < (int) instCatalogo.size())
                          && ! instCatalogo[(size_t) instPack].instr.empty()
                          && instCatalogo[(size_t) instPack].instr[0].familiaSintes >= 0;

        const int filas = juce::jmax (1, cuantos);
        const int alto4x4 = (Metrics::hit + Metrics::xs) * 4;
        //  Y LA DE LOS INSTRUMENTOS, EN DOS COLUMNAS DE OCHO.
        //
        //  Con la palabra al lado del dibujo no cabe en cuatro columnas y no es
        //  cuestion de apretarla: en 412x915 la celda de cuatro mide 84 px, su
        //  caja de rotulo 74, y "CUERDA PULS" pide 83 - o sea que el nombre no
        //  entra AUNQUE se caiga el dibujo. Por eso las dieciseis salian mudas.
        //  En dos columnas la celda pasa a 173 px y la caja a 163: el nombre
        //  entero, el dibujo de 18 al lado y 76 px de sobra. Cuesta cuatro
        //  filas mas, que es alto y no ancho, y esta ficha ya se desplaza.
        const int alto2col = (Metrics::hit + Metrics::xs) * 8;
        auto inner = sheetFromBottom (instSheet, Metrics::md * 2 + Metrics::hit
                                                   + Metrics::md + filaPack
                                                   + Metrics::sm
                                                   + (rejilla ? (Metrics::hit + Metrics::sm
                                                                 + alto4x4 + Metrics::md + alto2col)
                                                              : (filaInst + Metrics::xs) * filas)
                                                   + Metrics::sm + 40);
        auto titleRow = inner.removeFromTop (Metrics::hit);
        instCloseButton.setBounds (Lang::takeEnd (titleRow, Metrics::hit)
                                       .withSizeKeepingCentre (Metrics::hit, Metrics::hit));
        //  LA BANDA DEL TITULO LA PUBLICA EL MAQUETADO, no la rehace el pintor.
        //
        //  paintInstSheetContent repetia esta cuenta por su cuenta y con otro
        //  origen -de la tarjeta y no del cuerpo, y con un reduced de mas- asi
        //  que el titulo caia encima de la primera tapa y el nombre del pack
        //  encima de la segunda. Es la misma regla que ya siguen las bandas de
        //  AJUSTES y los paneles del secuenciador: una cuenta, un dueno.
        instTitleArea = centraEnRenglon (titleRow.reduced (Metrics::lg, 0).withHeight (Metrics::bandaTitulo));
        inner.removeFromTop (Metrics::sm);

        //  MENOS Y MAS EN LOS EXTREMOS y el nombre del pack pintado en medio.
        //  Es lo mismo que hace el RACK con el nombre del efecto: un rotulo que
        //  solo se lee no necesita ser un componente, y siendolo le quitaria el
        //  ancho a las dos tapas que si se tocan.
        {
            auto fila = inner.removeFromTop (filaPack);
            const int w = juce::jmin (Metrics::hit * 2, fila.getWidth() / 3);
            instPackDownBtn.setBounds (fila.removeFromLeft (w));
            instPackUpBtn  .setBounds (fila.removeFromRight (w));
            instPackArea = fila;
        }
        inner.removeFromTop (Metrics::sm);

        //  De arriba abajo, al reves que el RACK y que la cara: ahi el 01 va
        //  abajo porque son PADS y la rejilla de pads empieza abajo; una lista
        //  de palabras se lee al derecho.
        for (auto* b : instBtns)      if (b != nullptr) { b->setVisible (false); b->setBounds ({}); }
        for (auto* b : instDestBtns)  if (b != nullptr) { b->setVisible (false); b->setBounds ({}); }
        for (auto* b : instBancoBtns) if (b != nullptr) { b->setVisible (false); b->setBounds ({}); }

        //  Una lambda para las dos rejillas: es la MISMA forma -la del selector
        //  del RACK y la de la cara- y escribirla dos veces son dos reglas.
        //  De abajo arriba: el 01 abajo a la izquierda. Numerar al reves aqui
        //  seria un mapa distinto del mismo instrumento.
        auto pon4x4 = [] (juce::Rectangle<int> caja, juce::OwnedArray<juce::TextButton>& bs)
        {
            for (int r = 0; r < 4; ++r)
            {
                auto row = caja.removeFromTop (Metrics::hit);
                const int w = row.getWidth() / 4;
                for (int c = 0; c < 4; ++c)
                {
                    const int i = (3 - r) * 4 + c;
                    if (i >= bs.size()) continue;
                    bs[i]->setVisible (true);
                    //  Sin aire vertical, que es el reduced (x, n) que ya costo
                    //  656 tapas en la cara.
                    bs[i]->setBounds ((c < 3 ? row.removeFromLeft (w) : row)
                                      .reduced (Metrics::aireTapaDensa, 0));
                }
                caja.removeFromTop (Metrics::xs);
            }
        };

        //  Y LA DE LOS NOMBRES, en dos columnas y al DERECHO.
        //
        //  De arriba abajo y no de abajo arriba como la de pads: la de pads es
        //  un MAPA -el 01 abajo a la izquierda, donde esta en la cara- y esta
        //  es una lista de palabras, que se lee por donde se empieza a leer.
        //  Son dos preguntas distintas y ahora tambien se ven distintas, que
        //  era la otra mitad del problema: dos rejillas de cuatro por cuatro,
        //  una encima de la otra, se leen como dos mitades de lo mismo.
        auto pon2col = [] (juce::Rectangle<int> caja, juce::OwnedArray<juce::TextButton>& bs)
        {
            const int n = juce::jmin (16, bs.size());
            for (int r = 0; r < 8; ++r)
            {
                auto row = caja.removeFromTop (Metrics::hit);
                const int w = row.getWidth() / 2;
                for (int c = 0; c < 2; ++c)
                {
                    const int i = r * 2 + c;
                    if (i >= n) continue;
                    bs[i]->setVisible (true);
                    bs[i]->setBounds ((c == 0 ? row.removeFromLeft (w) : row)
                                      .reduced (Metrics::aireTapaDensa, 0));
                }
                caja.removeFromTop (Metrics::xs);
            }
        };

        if (rejilla)
        {
            {
                auto fila = inner.removeFromTop (Metrics::hit);
                const int w = fila.getWidth() / kNumBanks;
                for (int b4 = 0; b4 < kNumBanks; ++b4)
                {
                    instBancoBtns[b4]->setVisible (true);
                    instBancoBtns[b4]->setBounds ((b4 < kNumBanks - 1 ? fila.removeFromLeft (w) : fila)
                                                      .reduced (Metrics::aireTapaDensa, 0));
                }
            }
            inner.removeFromTop (Metrics::sm);
            pon4x4 (inner.removeFromTop (alto4x4), instDestBtns);
            inner.removeFromTop (Metrics::sm);
            pon2col (inner.removeFromTop (alto2col), instBtns);
            //  Y AQUI, con las celdas ya colocadas: reparteTapa mide el ancho
            //  del componente, asi que preguntarlo antes seria preguntarle a
            //  una tapa de cero pixeles. Es lo mismo que hace layoutModuleBar
            //  con filaDeIconos.
            rejillaDeIconos (instBtns, Sintes::kFamilias);
        }
        else
        {
            for (int i = 0; i < instBtns.size(); ++i)
            {
                if (i >= cuantos) break;
                auto fila = inner.removeFromTop (filaInst);
                inner.removeFromTop (Metrics::xs);
                instBtns[i]->setVisible (true);
                instBtns[i]->setBounds (fila);
            }
        }

        //  Y EL PIE LO PUBLICA EL MAQUETADO.
        //
        //  El pintor lo colocaba con una cuenta suya -"debajo del nombre del
        //  pack, mas una fila por instrumento"- que solo vale para la LISTA:
        //  con la rejilla puesta, esas dieciseis filas son 704 px y las dos
        //  frases que explican la ficha se pintaban FUERA del cuerpo, o sea
        //  que no las ha visto nadie desde que la rejilla existe. Es la misma
        //  regla que ya costo el titulo y el nombre del pack: una cuenta, un
        //  dueno, y el dueno es quien coloca.
        inner.removeFromTop (Metrics::sm);
        instPieArea = inner.removeFromTop (40);
    }

    // ------------------------------------------------------------------
    //  LA FICHA DEL INSTRUMENTO. Cabecera con el dibujo de la familia, un
    //  teclado que se toca y la lista de los dieciseis presets.
    //
    //  De ancho entero la lista, por lo mismo que la de instrumentos: en la
    //  pantalla mas estrecha una rejilla de cuatro columnas deja 62 px por
    //  celda y "TREMOLO ST" pide 85. Una lista de nombres se lee de arriba
    //  abajo, sale mas alta que la tarjeta, y por eso esta ficha se desplaza.
    // ------------------------------------------------------------------
    {
        const int cabecera = 56;      // el dibujo grande y el nombre de la familia
        const int teclas   = 72;      // una octava que se pueda tocar con el dedo
        const int filaP    = Metrics::hit;

        auto inner = sheetFromBottom (vstSheet, Metrics::md * 2 + Metrics::hit
                                                  + Metrics::md + cabecera
                                                  + Metrics::sm + filaP
                                                  + Metrics::sm + Metrics::hit + teclas
                                                  + Metrics::sm + 40);

        auto titleRow = inner.removeFromTop (Metrics::hit);
        vstCloseButton.setBounds (Lang::takeEnd (titleRow, Metrics::hit)
                                      .withSizeKeepingCentre (Metrics::hit, Metrics::hit));
        vstTitleArea = centraEnRenglon (titleRow.reduced (Metrics::lg, 0).withHeight (Metrics::bandaTitulo));
        inner.removeFromTop (Metrics::sm);

        //  LA CABECERA: el dibujo a la izquierda y el nombre de la familia al
        //  lado. El del PRESET se fue de aqui a su propia pantalla, que es
        //  donde se cambia: escrito en los dos sitios eran dos rotulos que
        //  dicen lo mismo y uno de ellos lejos de la tapa que lo mueve.
        {
            auto fila = inner.removeFromTop (cabecera).reduced (Metrics::lg, 0);
            vstPanelCab = fila;
            vstIconArea = Lang::takeStart (fila, cabecera).reduced (Metrics::halfGap);
            fila.removeFromLeft (Metrics::sm);
            vstNombreArea = fila;
        }
        inner.removeFromTop (Metrics::sm);

        //  EL PRESET: menos, UNA PANTALLA con el nombre, y mas. La pantalla es
        //  lo que la persona pidio con estas palabras -"una flecha, un
        //  cuadradito y otra flecha"- y ademas es lo que separa un dato que
        //  CAMBIA de un rotulo grabado en el chasis: en un aparato de verdad,
        //  lo que cambia se lee sobre cristal.
        {
            auto fila = inner.removeFromTop (filaP).reduced (Metrics::lg, 0);
            vstPanelPre = fila;
            const int w = juce::jmin (Metrics::hit * 2, fila.getWidth() / 4);
            vstPreDown.setBounds (fila.removeFromLeft (w));
            vstPreUp  .setBounds (fila.removeFromRight (w));
            vstPreArea = fila.reduced (Metrics::halfGap, Metrics::keyAir);
        }
        inner.removeFromTop (Metrics::sm);

        //  Y EL TECLADO CON SU OCTAVA, en un panel: los dos son la parte que se
        //  TOCA, y el mapa de las cinco raices vive entre las dos flechas
        //  porque dice exactamente lo que la octava cambia.
        {
            auto caja = inner.removeFromTop (Metrics::hit + teclas);
            vstPanelTec = caja.reduced (Metrics::lg - Metrics::halfGap, 0);
            auto fila = caja.removeFromTop (Metrics::hit).reduced (Metrics::lg, 0);
            //  UN TERCIO Y NO UN CUARTO: estas dos tapas llevan la palabra
            //  -"OCT -"- y las del preset solo el signo. Con un cuarto, en
            //  280x653 y en arabe "أوكتاف +" pedia 50 px de letra y tenia 42.
            //  El reparto sale del texto que lleva la tapa, no de la fila.
            const int w = juce::jmin (Metrics::hit * 2, fila.getWidth() / 3);
            vstOctDown.setBounds (fila.removeFromLeft (w));
            vstOctUp  .setBounds (fila.removeFromRight (w));
            vstOctArea = fila.reduced (Metrics::halfGap, Metrics::halfGap);
            vstTeclado.setBounds (caja.reduced (Metrics::lg, Metrics::keyAir));
        }
    }

    // AUTO CHOP sheet: how many pieces, where they land, and one red verb.
    {
        chopGrupos.clear();
        const int explainH = 40, plannedH = 40;
        //  Una fila mas que antes: la de COMO se corta, encima de la de en
        //  cuantos trozos, porque el modo cambia lo que significa el numero.
        auto inner = sheetFromBottom (chopSheet, Metrics::md * 2 + Metrics::hit + explainH
                                                   + Metrics::md + 14 + Metrics::hit
                                                   + Metrics::md + 14 + Metrics::hit
                                                   + Metrics::sm + Metrics::hit
                                                   + Metrics::md + plannedH
                                                   //  Y LA VISTA PREVIA, que si no se pide no existe.
                                                   //
                                                   //  sheetFromBottom da lo que se le pide y recorta al
                                                   //  78%; lo que falte se lo come lo ULTIMO que se
                                                   //  maqueta, en silencio. Se anadio el componente sin
                                                   //  sumar su alto aqui y salia de cero en las tres
                                                   //  pantallas: existente, invisible y sin que ninguna
                                                   //  de las seis reglas lo viera, porque un componente
                                                   //  de 0x0 no solapa ni se sale. Es el mismo fallo que
                                                   //  ya dejo EUCLIDES en 217x0.
                                                   + kChopVistaH + Metrics::sm
                                                   + Metrics::sm + Metrics::btn);
        auto titleRow = inner.removeFromTop (Metrics::hit);
        chopCloseButton.setBounds (Lang::takeEnd (titleRow, Metrics::hit).withSizeKeepingCentre (Metrics::hit, Metrics::hit));
        //  LA PUERTA A LA REJILLA DE DIECISEIS, en el mismo rincon que en EL
        //  PAD y en el piano: las TRES fichas que editan «el pad que tengas
        //  elegido» lo cambian desde el mismo sitio. Caja fija de Metrics::hit
        //  -no entra en el reparto por texto, que ahi el rotulo son dos cifras
        //  y le tocarian 38 px- y solo si queda ancho para ella y para el
        //  titulo. Cero de ALTO: comparte el renglon que ya existe.
        {
            Lang::takeEnd (titleRow, Metrics::xs);
            const bool cabe = titleRow.getWidth() >= Metrics::hit * 2;
            chopPadPickBtn.setVisible (cabe);
            chopPadPickBtn.setBounds (cabe ? Lang::takeEnd (titleRow, Metrics::hit)
                                                 .withSizeKeepingCentre (Metrics::hit, Metrics::hit)
                                           : juce::Rectangle<int>());
        }

        inner.removeFromTop (explainH);                 // painted: what this does
        inner.removeFromTop (Metrics::sm);

        //  LOS DOS GRUPOS DE ESTA FICHA, que llevaban una tanda declarados y sin
        //  dueño: `chopGrupos` estaba en el `.h` -al lado de un comentario que
        //  daba por agrupados EXPORTAR y el TROCEADO- y no lo escribia ni lo
        //  leia nadie. EXPORTAR si; este seguia siendo una columna de renglones
        //  sueltos sobre el mismo fondo, que es exactamente la queja que trajo
        //  los paneles a las otras cuatro fichas.
        //
        //  Son DOS y no uno: COMO dice de que manera se corta y TROZOS en
        //  cuantos, y entre los dos hay `Metrics::sm` -la frontera de la casa-
        //  asi que los dos paneles se salen `panelAireY` por lado y queda el
        //  hueco de cuatro que se ve. Y CUADRAR se queda fuera, como CANCELAR
        //  en EXPORTAR: un panel alrededor de un solo control no agrupa nada.
        //
        //  Cada uno envuelve su banda de ROTULO y su fila, que es lo que hace
        //  que el nombre pintado quede dentro de la placa y no encima del filo.
        //  No cuestan un pixel: se deducen de lo que `resized()` ya reserva.
        auto grupoComo = inner.removeFromTop (Metrics::bandaSubtitulo);      // pintado: "COMO"
        {
            auto row = inner.removeFromTop (Metrics::hit);
            grupoComo = grupoComo.getUnion (row);
            juce::TextButton* mb[2] = { &chopEvenBtn, &chopHitsBtn };
            layoutModuleBar (row, mb, 0, 2);
        }
        chopGrupos.add (grupoComo);

        inner.removeFromTop (Metrics::sm);
        auto grupoTrozos = inner.removeFromTop (Metrics::bandaSubtitulo);    // pintado: "TROZOS"

        {
            auto row = inner.removeFromTop (Metrics::hit);
            grupoTrozos = grupoTrozos.getUnion (row);
            const int w = row.getWidth() / chopCountBtns.size();
            for (int i = 0; i < chopCountBtns.size(); ++i)
                chopCountBtns[i]->setBounds ((i < chopCountBtns.size() - 1 ? row.removeFromLeft (w) : row)
                                                 .reduced (Metrics::aireTapa, 0));
        }
        chopGrupos.add (grupoTrozos);

        inner.removeFromTop (Metrics::sm);
        chopSafeButton.setBounds (inner.removeFromTop (Metrics::hit).reduced (Metrics::aireTapa, 0));
        //  EL VERBO ROJO SE APARTA PRIMERO.
        //
        //  Se colocaba con lo que quedara despues de todo lo demas, y girado
        //  no quedaba: 26 px de alto para la unica tapa de la ficha que hace
        //  algo irreversible. Se reserva del fondo, y lo que encoge es el
        //  hueco pintado que explica donde caen los trozos - un texto se lee
        //  igual con dos lineas menos, una tapa de 26 px no se toca igual.
        auto verbo = inner.removeFromBottom (Metrics::btn);
        inner.removeFromBottom (Metrics::sm);

        inner.removeFromTop (Metrics::sm);

        //  LA VISTA PREVIA SE QUEDA CON LO QUE HAY, y lo que encoge es el texto.
        //
        //  Se aparta despues del verbo rojo -que ya se reservo del fondo- y
        //  antes del parrafo, que es la misma escalera que esta ficha ya tenia:
        //  lo que no puede encoger va primero. Un texto se lee igual con dos
        //  lineas menos; una onda de treinta pixeles no dice donde cae un
        //  corte, que es justo para lo que existe.
        //
        //  Donde ni con esas cabe, no se dibuja: media vista previa es peor que
        //  ninguna, porque parece que la muestra es esa.
        {
            const int quiere = kChopVistaH;
            if (inner.getHeight() >= quiere + Metrics::sm)
            {
                chopVista.setVisible (true);
                chopVista.setBounds (inner.removeFromTop (quiere));
                inner.removeFromTop (Metrics::sm);
            }
            else
            {
                chopVista.setVisible (false);
                chopVista.setBounds ({});
            }
        }

        inner.removeFromTop (juce::jmin (plannedH, inner.getHeight()));   // painted: where they land
        chopGoButton.setBounds (verbo.reduced (Metrics::aireTapa, 0));
    }

    // SONG sheet: palette, timeline, page row.
    {
        //  EL CARRIL DEJA DE ESTAR CLAVADO EN CUARENTA.
        //
        //  Era una constante escrita a mano, asi que compactar las filas de
        //  arriba NO agrandaba el area de trabajo: dejaba la tarjeta mas corta
        //  y el carril donde estaba. Medido antes de tocar nada: 42.0 px en un
        //  movil grande, en tableta y en el Fold - los tres capados por el 40 -
        //  y 28.8 y 20.2 en las dos estrechas, que es donde `sheetFromBottom`
        //  ya se comia la diferencia.
        //
        //  Ahora pide un SUELO y se lleva lo que sobre hasta un tope. Es la
        //  regla de siempre -lo que no puede encoger se aparta primero y el
        //  elastico se queda el resto- aplicada a lo unico para lo que esta
        //  pagina existe. El tope existe porque un carril de doscientos pixeles
        //  no es mas util que uno de setenta: lo que sobra por encima se lo
        //  queda la maquina, que se sigue viendo detras de la tarjeta.
        const int laneMin = juce::jmax (Metrics::celdaCancion, 34);
        const int laneMax = 72;
        int laneH = laneMin;
        //  Y la fila de modos puede ser DOS desde que son cuatro tapas, asi que
        //  la altura que se pide lo cuenta: pedirla de una y usar dos es como
        //  un control se queda con altura cero.
        //  La tira de herramientas cuesta su fila, y se cuenta AQUI: pedir sin
        //  ella y colocarla abajo es como un control se queda con altura cero -
        //  el fallo que esta misma ficha ya pago con la fila de paginas.
        //  Y AHORA ES INCONDICIONAL: la rejilla es UNA sola y lleva las
        //  cuatro herramientas siempre - patrones y clips comparten carril, asi
        //  que la tira sirve para los dos.
        const int filasHerr = Metrics::hit + Metrics::halfGap;
        int filasModo = Metrics::hit;
        //  Y la de herramientas puede ser dos por lo mismo: son cinco tapas y
        //  en arabe INSERTAR y QUITAR piden bastante mas ancho que en ingles.
        int filasUtil = Metrics::hit;
        //  El ancho de la COLUMNA, decidido aqui arriba porque de el depende en
        //  cuantas filas caen las tapas y de eso depende la altura que se pide.
        const int colUtil = wideFace ? juce::jlimit (200, 340, full.getWidth() / 3) : 0;
        {
            //  Y ESTE es el ancho con el que se cuentan las filas: girado, las
            //  tapas no cruzan la tarjeta, viven en una columna de 300 px. Se
            //  contaban con el ancho de la PANTALLA - 815 px - asi que las
            //  nueve "caben en una fila", se pedia altura para UNA y abajo se
            //  maquetaban TRES. Las dos ultimas filas se salian de la tarjeta y
            //  el pie entero -PLAY, el largo y las paginas- quedaba en 72x0:
            //  la ficha de CANCION no tenia boton de play en apaisado, que es
            //  justo la orientacion en la que se pidio.
            const int anchoUtil = wideFace ? colUtil - 2 * Metrics::margenFichaX
                                           : anchoTarjetaInterior (safeArea().getWidth());
            //  LA MISMA FILA QUE SE VA A MAQUETAR, y no otra parecida: en la
            //  vista de audio son CINCO -las dos brochas, GRABAR, el clic y el
            //  modo- y preguntar por cuatro seria pedir con una cuenta y
            //  colocar con otra, que es como una fila se sale de la tarjeta.
            juce::TextButton* sb[5] = { &songPadModeBtn, &songRecBtn, &songClickBtn,
                                        (juce::TextButton*) &autoBtn, &songModeBtn };
            const int nb = 5;
            if (! moduleBarFits (anchoUtil, sb, nb)) filasModo = 2 * Metrics::hit + Metrics::halfGap;

            //  DOBLAR baja aqui desde la fila de modos: con la rejilla fundida
            //  esa fila son las cinco de la banda de audio - las dos brochas,
            //  GRABAR, el clic y el modo- y no queda hueco. Y ademas es donde
            //  le toca: doblar la cancion es una herramienta de ARREGLO, como
            //  insertar un compas o quitarlo, no una brocha.
            juce::TextButton* su[10] = { &songLeftBtn, &songRightBtn, &songShortBtn,
                                         &songLongBtn, &songInsertBtn, &songRemoveBtn,
                                         &songCopyBtn, &songPasteBtn, &songLoopBtn,
                                         &songDoubleBtn };
            //  Nueve tapas no caben en una fila salvo en tableta, y en 280 px
            //  tampoco en dos - medido con siete, ADELANTE pedia 60 px y tenia
            //  51. Tres escalones, y el que se elige aqui es el mismo que se
            //  maqueta abajo, que pedir uno y usar otro deja la ultima fila
            //  con altura cero.
            //  Y DESDE QUE SON ICONOS SIN ROTULO, LA PREGUNTA ES EL DEDO Y NO
            //  EL TEXTO. `moduleBarFits` mide rotulos, y con el rotulo vacio
            //  diria que si siempre: nueve tapas en 225 px son 25, la mitad de
            //  un dedo, y el banco lo cantaria entero. Nueve por cuarenta, y
            //  donde no caben, en dos filas.
            juce::ignoreUnused (su);
            filasUtil = (anchoUtil >= 10 * Metrics::hit) ? Metrics::hit
                      : (anchoUtil >= 5 * Metrics::hit) ? 2 * Metrics::hit + Metrics::halfGap
                                                        : 4 * Metrics::hit + 3 * Metrics::halfGap;
        }
        //  Girado, la tarjeta no tiene que ser tan alta como la suma de las
        //  filas: las filas estan en una columna al lado de la rejilla, asi
        //  que la altura que se pide es la de la COLUMNA - que es la mayor de
        //  las dos - y no la de las dos apiladas.
        //
        //  Y EL PIE -paginas y transporte- NO va en la columna cuando esta
        //  girada. Girado la columna ya pide 268 px de sus tres bloques y la
        //  tarjeta solo puede medir el 86% de 412; meter ahi otros 92 la
        //  desbordaba y la ultima fila salia con altura cero. Debajo de la
        //  linea de tiempo hay sitio de sobra: cuatro carriles pasan de 68 px
        //  a 49, que sigue siendo compas y medio de dedo.
        //  CUANTAS FILAS OCUPAN LA PALETA Y LAS PAGINAS, decidido AQUI y no
        //  abajo, porque de ellas depende la altura que se pide y abajo ya es
        //  tarde. Al partir las ocho tapas de la paleta en dos filas sin
        //  contarlas aqui, la linea de tiempo -que se lleva lo que sobra- cayo
        //  a 9 px por carril en 280x653: la cuarta parte del suelo. Es el mismo
        //  fallo que ya costo el TEMPO del secuenciador y la fila de
        //  herramientas de esta misma ficha, contado por tercera vez.
        const int anchoPaleta = wideFace ? colUtil - 2 * Metrics::margenFichaX
                                         : anchoTarjetaInterior (safeArea().getWidth());

        //  LA FILA DE PAGINAS -1, 9, 17...- PASA A SER UNA BARRA QUE SE ARRASTRA.
        //
        //  Ocho tapas numeradas eran un salto por pagina y nada mas: para ver
        //  el compas 12 con la vista en 8 habia que ir a la pagina 2 y la mitad
        //  del estribillo quedaba fuera. Una barra desplaza COMPAS A COMPAS y
        //  ademas dibuja donde esta el cabezal sobre los sesenta y cuatro, que
        //  es lo que la fila de tapas hacia pintando de rojo la que tocaba.
        //
        //  Y NO CUESTA ALTO: mide `Metrics::hit` -es un control que se ajusta
        //  ARRASTRANDO, y esta casa ya tiene medido que esos son los que menos
        //  pueden permitirse ser finos- que es exactamente lo que costaba la
        //  fila de una pagina; donde las paginas no cabian a dedo se partia en
        //  DOS y ahi la barra devuelve 44 px a los carriles.
        //
        //  Solo cuando hace falta: con la cancion por defecto -ocho compases- y
        //  la vista en ocho no hay nada que desplazar, asi que no se pide. Es la
        //  misma pregunta que la barra del secuenciador.
        const bool songHayBarra = engine.getSongLength() > songGrid.getCompasesVista();
        const int altoBarraVista = songHayBarra ? BarraVista::kGrueso : 0;

        //  LO QUE PIDE LA FICHA, con la paleta de una fila o de dos. Escrito una
        //  vez y preguntado dos, que es la unica forma de que la respuesta valga:
        //  el primer intento comparo el tope con una cuenta inventada aqui mismo
        //  -medio presupuesto- y dijo que cabia cuando no cabia.
        //  LA FILA DE PESTANAS, CONTADA. Es la misma regla que el secuenciador
        //  ya paga con PASOS/PIANO/PATRON y cuesta `Metrics::tab` de alto; no
        //  contarla aqui seria pedir con una cuenta y colocar con otra, que es
        //  como un carril se queda en nueve pixeles.
        //
        //  Y en la vista de AUDIO la columna es OTRA: no hay paleta de patrones
        //  ni herramientas de arreglo, asi que pedir el alto de la vista de
        //  patrones dejaria la banda de audio con ciento y pico pixeles de aire
        //  vacio debajo - una tarjeta que da un salto de tamano al cambiar de
        //  pestana se lee como un fallo, y una que reserva sitio para lo que no
        //  hay se lee peor.
        auto pideCancion = [&] (int filasPal)
        {
            const int pie = altoBarraVista + Metrics::xs + Metrics::btn + Metrics::xs;
            const int col = filasPal * Metrics::hit + (filasPal - 1) * Metrics::halfGap + Metrics::xs
                          + filasHerr + filasModo + filasUtil + Metrics::sm * 2;
            juce::ignoreUnused (col);
            const int rej = Playlist::kLanes * laneH;
            return Metrics::md * 2 + Metrics::hit
                 + (wideFace ? juce::jmax (col, rej + pie) : col + Metrics::sm + pie + rej);
        };

        //  Y LA LINEA DE TIEMPO MANDA SOBRE LA PALETA.
        //
        //  Partir las ocho tapas en dos filas las hace tocables -de 26 px de
        //  ancho a 56- y cuesta 48 px de alto, que de pie salen de lo unico para
        //  lo que existe esta pagina: los cuatro carriles. sheetFromBottom
        //  recorta al 78 % y lo que falta se lo come lo ultimo que se maqueta,
        //  asi que el carril caia de 21 px a NUEVE sin que nadie se enterase.
        //  Una tapa estrecha se acierta con cuidado; un carril de nueve pixeles
        //  no se acierta.
        //  Y con la FORMA de la ventana, no con `wideFace`. Son dos preguntas
        //  distintas: wideFace pide ademas 560 px de ancho -es "la cara cabe en
        //  dos columnas"- y sheetFromBottom decide el tope con w > h a secas.
        //  En 480x412 -la pantalla partida que ya saco dieciseis solapes-
        //  wideFace es falso y la tarjeta mide 0.90: la pregunta de si la
        //  paleta cabe en dos filas se hacia con 50 px menos de los que hay, y
        //  la paleta se quedaba de ocho sin necesidad.
        const int topeCancion = altoTarjeta (full);

        //  Y LA ESCALERA DE LAS PAGINAS SE VA CON ELLAS. Existia porque ocho
        //  tapas numeradas no caben a dedo en 280 px y se partian en dos filas,
        //  llevandose 44 px de los cuatro carriles -medido, el carril caia de
        //  21 px a DIEZ-. Una barra ocupa una fila y solo una pase lo que pase,
        //  asi que esa pregunta ya no existe.

        const bool paletaAnchaCabe = (anchoPaleta / kNumPatterns - 2 >= Metrics::hit);
        const int filasPaleta = (paletaAnchaCabe || pideCancion (2) > topeCancion) ? 1 : 2;
        const int porFilaPal  = kNumPatterns / juce::jmax (1, filasPaleta);

        const int altoPie  = altoBarraVista + Metrics::xs + Metrics::btn + Metrics::xs;
        const int altoCol  = filasPaleta * Metrics::hit
                           + (filasPaleta - 1) * Metrics::halfGap + Metrics::xs   // paleta
                           + filasHerr + filasModo + filasUtil + Metrics::sm * 2;
        //  Y AQUI SE REPARTE LO QUE SOBRA. Se pide la ficha con el carril en
        //  su SUELO -que es lo que el resto de la maqueta necesita para caber-
        //  y lo que quede hasta el tope de la tarjeta se lo llevan los cuatro
        //  carriles, acotado por arriba. Donde no sobra nada, el suelo, que es
        //  exactamente lo que habia en las dos pantallas estrechas.
        {
            const int pedidoConSuelo = Metrics::md * 2 + Metrics::hit
                                     + (wideFace ? juce::jmax (altoCol, Playlist::kLanes * laneMin + altoPie)
                                                 : altoCol + Metrics::sm + altoPie
                                                   + Playlist::kLanes * laneMin);
            const int sobra = topeCancion - pedidoConSuelo;
            if (sobra > 0)
                laneH = juce::jmin (laneMax, laneMin + sobra / Playlist::kLanes);
        }
        const int altoRej  = Playlist::kLanes * laneH;
        auto inner = sheetFromBottom (songSheet,
                                      Metrics::md * 2 + Metrics::hit
                                        + (wideFace ? juce::jmax (altoCol, altoRej + altoPie)
                                                    : altoCol + Metrics::sm + altoPie + altoRej));
        auto titleRow = inner.removeFromTop (Metrics::hit);
        songCloseButton.setBounds (Lang::takeEnd (titleRow, Metrics::hit).withSizeKeepingCentre (Metrics::hit, Metrics::hit));

        //  LAS DOS PESTANAS VAN EN EL RENGLON DEL TITULO Y NO EN UNA FILA
        //  PROPIA, y esto se midio antes de decidirlo.
        //
        //  El secuenciador las lleva en su fila y ahi cuesta `Metrics::tab`. Se
        //  copio, y el banco lo canto en la primera corrida: en 280x653 la
        //  celda de la linea de tiempo paso de 20.2 px a **9x25**, porque esta
        //  ficha ya llegaba al tope del 78% y `sheetFromBottom` se come lo que
        //  falta de lo ULTIMO que se maqueta - que aqui es lo unico para lo que
        //  la pagina existe. Es el mismo fallo que ya costo la fila de CADENA y
        //  la rejilla de la pagina PATRON, escrito una vez mas.
        //
        //  El renglon del titulo ya esta ahi, mide `Metrics::hit` y solo lleva
        //  una palabra pintada y una cruz: dos tapas cortas caben al lado sin
        //  costar un pixel de alto. El titulo se aparta solo, que es lo que
        //  `antesDe` hace desde que existe.
        {
            //  UNA sola desde que la ficha tiene una rejilla y no dos: el
            //  interruptor de vista se retiro con las dos vistas. Dice el
            //  ESTADO -cuantos compases se ven- y no cuesta un pixel de alto,
            //  que es lo unico que en esta ficha no sobra.
            auto zona = Lang::takeEnd (titleRow, juce::jmin (titleRow.getWidth() / 2,
                                                             Metrics::hit * 3));
            juce::TextButton* vb[1] = { &songZoomBtn };
            layoutModuleBar (zona, vb, 1, 1);
        }

        //  APAISADO, LA TARJETA SE PARTE EN DOS.
        //
        //  Girado, esta ficha ponia sus tres filas de tapas cruzando los 900
        //  px de ancho y le dejaba a la LINEA DE TIEMPO -que es para lo unico
        //  que existe la pagina- unos 80 px de alto para cuatro carriles:
        //  diecinueve pixeles por carril, la mitad de un dedo. Era la maqueta
        //  vertical estirada, no una maqueta apaisada.
        //
        //  Las tapas se van a una columna de la izquierda, apiladas, y la
        //  linea de tiempo se queda con el ancho que sobra Y CON TODO EL ALTO.
        //  Es la misma decision que ya tomo la ficha del secuenciador, y por
        //  la misma razon: la pantalla tiene la forma que tiene.
        auto columna = wideFace ? Lang::takeStart (inner, colUtil) : juce::Rectangle<int>();
        if (wideFace) Lang::takeStart (inner, Metrics::gap);
        auto& panel = wideFace ? columna : inner;

        //  LOS GRUPOS DE ESTA FICHA, y SIN ROTULO. Las otras dos los heredan de
        //  bandas de nombre que ya estaban reservadas; aqui no hay ninguna, y
        //  ponerlas costaria tres renglones de 14 px que salen de lo unico para
        //  lo que existe la pagina - los cuatro carriles de la linea de tiempo,
        //  que en 280x653 ya andan por 21 px. El panel agrupa igual sin decir
        //  como se llama el grupo: lo que separa una paleta de una fila de
        //  herramientas no es su nombre, es que sean dos bloques.
        songGrupos.clear();
        auto cierraSong = [this, &panel] (int y0)
        {
            if (panel.getY() > y0)
                songGrupos.add ({ panel.getX(), y0, panel.getWidth(), panel.getY() - y0 });
        };

        //  El borde de arriba del panel de LA BROCHA, que empieza en la paleta
        //  y acaba en la fila de modos. Ver el cierre, dos bloques mas abajo.
        const int gBrocha = panel.getY();

        // Palette: P1..P8.
        //
        //  Y SE MAQUETA SIEMPRE desde que la rejilla es UNA: la paleta elige
        //  que PATRON pinta la brocha, y con los clips en el mismo carril esa
        //  pregunta no desaparece nunca. Antes se apagaba en la vista de audio
        //  -que ya no existe- y habia que apagarla Y no colocarla, las dos
        //  cosas, o `resized()` le devolvia sus 44 px al maquetar.
        {
            auto row = panel.removeFromTop (Metrics::hit);
            //  OCHO EN UNA FILA SOLO SI CABEN A DEDO.
            //
            //  Girado ya se partia en dos, con el argumento de que en una
            //  columna de 300 px las ocho salen a 37 y el numero no se lee. El
            //  argumento es bueno y la condicion era mala: de pie en un movil
            //  estrecho salen a 26 px, peor todavia, y ahi no se partia. La
            //  pregunta es el ANCHO que hay, no como esta girado el telefono.
            //  La MISMA respuesta que decidio la altura, no una nueva: pedir
            //  con una cuenta y colocar con otra es como un carril se queda en
            //  nueve pixeles.
            const int porFila = porFilaPal;
            for (int f = 0; f < kNumPatterns / porFila; ++f)
            {
                auto fila = (f == 0) ? row : panel.removeFromTop (Metrics::hit);
                //  EL FILO SE ALINEA POR LA FILA, NO POR LA TAPA.
                //
                //  Esta paleta sobresalia UN pixel por los dos lados respecto a
                //  la fila de brochas que comparte panel con ella -filas que
                //  empiezan en 5 y en 6, dicho por Tests/paneles.py- porque
                //  llevaba su margen escrito a mano y layoutModuleBar usa
                //  Metrics::aireTapa.
                //
                //  Y ponerle aireTapa a cada tapa SALIO PEOR, que es lo que
                //  hacia falta medir antes de creerselo: son ocho en una fila y
                //  cada pixel de margen se lo quitan a las ocho, asi que en
                //  412x915 pasaban de 41x40 a **39x40** - por debajo del dedo
                //  minimo, y expo.py saco 28 TOUCH nuevos. Cambiar un desliz de
                //  un pixel por un objetivo que no se puede tocar no es un
                //  arreglo, que es la misma regla que ya deshizo dos veces el
                //  reparto de las barras de modulos.
                //
                //  Asi que el pixel que falta se lo come la FILA una sola vez:
                //  el filo de fuera queda en aireTapa como el de cualquier otra
                //  fila, el hueco entre tapas sigue siendo el de siempre, y las
                //  ocho pierden 1/8 de pixel en vez de dos cada una.
                //  Y el filo de FUERA queda donde el de la fila: desde que
                //  `layoutModuleBar` alinea las suyas con el rectangulo que
                //  recibe, dejarlo en `aireTapa` ponia esta paleta dos pixeles
                //  mas dentro que la fila de brochas que comparte panel con
                //  ella - las mismas «filas que empiezan en 4 y en 6» que este
                //  parrafo ya arreglo una vez con la referencia anterior.
                fila = fila.expanded (Metrics::aireTapaDensa, 0);
                const int w = fila.getWidth() / porFila;
                for (int i = 0; i < porFila; ++i)
                {
                    const int k = f * porFila + i;
                    songPatBtns[k]->setBounds ((i < porFila - 1 ? fila.removeFromLeft (w) : fila)
                                                   .reduced (Metrics::aireTapaDensa, 0));
                }
                if (f + 1 < kNumPatterns / porFila) panel.removeFromTop (Metrics::halfGap);
            }
            panel.removeFromTop (Metrics::xs);
        }
        // Brush modes + song mode + DOBLAR.
        //
        //  Repartidos POR EL TEXTO QUE LLEVAN y no a cuartos: "CANCION" pide el
        //  doble que "DOBLAR", y a cuartos el arabe de SONIDO se cortaba. Es lo
        //  mismo que hacen las barras de modulos, y con la misma pregunta
        //  delante - si los cuatro no caben en una fila, dos filas de dos - que
        //  es lo que ya hicieron las cuatro pestanas de AJUSTES.
        {
            //  EN AUDIO, TRES Y NO CUATRO: DOBLAR duplica los compases de
            //  patron y en una banda de clips no significa nada todavia. Una
            //  tapa que se pulsa y no hace nada es peor que no tenerla.
            //  EN AUDIO SON CINCO: las dos brochas, el modo, GRABAR y el
            //  metronomo. GRABAR y CLIC van aqui y no en una fila propia
            //  porque una fila cuesta 44 px de lo unico para lo que existe la
            //  pagina - los carriles - y esta fila ya esta puesta.
            //  LA TIRA DE HERRAMIENTAS: cuatro iconos sin rotulo.
            //
            //  Se reparte por el DEDO y no por el texto -no hay texto- asi que
            //  la pregunta es 4 x Metrics::hit y no `moduleBarFits`, que mide
            //  rotulos. Con cuatro son 160 px y caben en las siete pantallas,
            //  incluida la mas estrecha que nadie fabrica.
            {
                auto fila = panel.removeFromTop (Metrics::hit);
                const int n = songToolBtns.size();
                if (n > 0)
                {
                    //  A `Metrics::hit` por tapa y NO estirada: cuatro iconos
                    //  a todo lo ancho de la tarjeta se leerian como cuatro
                    //  tapas de otra cosa - lo que dice que son una familia es
                    //  que miden lo que mide un dedo y nada mas.
                    //
                    //  Y ARRIMADA AL FILO Y NO CENTRADA, que es lo que el banco
                    //  de paneles pidio: esta tira vive dentro del panel de la
                    //  brocha, y ahi «las filas empiezan todas en el mismo
                    //  sitio» no tiene excepcion legitima - centrada arrancaba
                    //  en 60 px donde sus hermanas arrancan en 6, y salieron 28
                    //  hallazgos en las siete pantallas. Alinearla no le cuesta
                    //  un pixel a la tapa; lo unico que se pierde es el centrado.
                    //
                    //  Y LA CELDA ES EL DEDO MAS LO QUE EL AIRE SE COME, que es
                    //  la unica forma de que la tapa ACABE midiendo cuarenta.
                    //  La primera version pidio la celda de `Metrics::hit`
                    //  clavada y la recorto `(aireTapa, 4)`: las cuatro salieron
                    //  a 36x32 y el banco lo canto con 222 TOUCH nuevos - en una
                    //  ficha donde ninguna de las once reglas duras se movio.
                    //  Es el mismo fallo que ya costo una medida en la tapa de
                    //  apagar del rack, donde el `reduced (1, 4)` de la fila
                    //  dejaba 38x40.
                    //
                    //  Y EL AIRE VERTICAL ES CERO Y NO CUATRO: la fila mide
                    //  `Metrics::hit` justo, asi que no hay holgura de la que
                    //  sacarlo. Es lo que `layoutModuleBar` hace desde que se
                    //  midio -acotar el aire a lo que sobra por encima del
                    //  dedo- y esta tira no pasa por ella porque se reparte por
                    //  el dedo y no por el texto.
                    const int celda = juce::jmin (Metrics::hit + 2 * Metrics::aireTapa,
                                                  fila.getWidth() / n);
                    //  Por el borde de ENTRADA y no por la izquierda: en
                    //  arabe la fila empieza a la DERECHA, y leer siempre la
                    //  izquierda daba por bueno alli justo lo que se rechaza
                    //  aqui - siete hallazgos, uno por pantalla y solo en `ar`.
                    //  Es la misma leccion que el propio banco de paneles tiene
                    //  escrita sobre por que mide el borde de entrada.
                    //  Y EL FILO DE FUERA SE ALINEA POR LA FILA Y NO POR
                    //  LA TAPA, que es lo mismo que ya hacen `layoutModuleBar`
                    //  y la paleta de patrones: la celda le quita `aireTapa` a
                    //  cada tapa, asi que sin expandir la zona la PRIMERA
                    //  arrancaba dos pixeles mas dentro que sus hermanas de
                    //  panel — «filas que empiezan en 4/6 px», 28 hallazgos en
                    //  `Tests/paneles.py` con las once reglas duras en cero. El
                    //  aire entre tapas es aire ENTRE hermanas y no vale en los
                    //  dos extremos de la fila.
                    auto ancha = fila.expanded (Metrics::aireTapa, 0);
                    auto zona  = Lang::takeStart (ancha, celda * n);
                    for (int i = 0; i < n; ++i)
                        songToolBtns[i]->setBounds (zona.removeFromLeft (celda)
                                                        .reduced (Metrics::aireTapa, 0));
                }
                panel.removeFromTop (Metrics::xs);
            }

            //  LAS MISMAS CINCO SIEMPRE. Eran cinco en la banda de audio y
            //  tres en la de patrones, o sea dos filas que mantener; con la
            //  rejilla fundida GRABAR, el clic y AUTO valen para lo que haya
            //  en el carril, que es todo.
            juce::TextButton* sb[5] = { &songPadModeBtn, &songRecBtn, &songClickBtn,
                                        (juce::TextButton*) &autoBtn, &songModeBtn };
            const int nBrochas = 5;
            if (moduleBarFits (panel.getWidth(), sb, nBrochas))
            {
                layoutModuleBar (panel.removeFromTop (Metrics::hit), sb, 0, nBrochas);
            }
            else
            {
                //  Y EN DOS FILAS DONDE NO CABEN, que es la misma pregunta
                //  que ya deciden BANCO y PADS. En audio, arriba las dos
                //  brochas y abajo lo que ACTUA - grabar, el clic y el modo.
                const int arriba = 1;   // la brocha SONIDO; lo que ACTUA baja
                layoutModuleBar (panel.removeFromTop (Metrics::hit), sb, 0, arriba);
                panel.removeFromTop (Metrics::xs);
                juce::TextButton* sc[4] = { &songRecBtn, &songClickBtn,
                                            (juce::TextButton*) &autoBtn, &songModeBtn };
                layoutModuleBar (panel.removeFromTop (Metrics::hit), sc, 0, 4);

            }
            //  LA PALETA Y LAS BROCHAS SON UN SOLO PANEL, no dos. Entre las dos
            //  filas hay Metrics::xs y dos paneles a cuatro pixeles se tocan -
            //  o sea que dibujar dos ahi es dibujar cero, que es el intento que
            //  ya esta contado en el secuenciador.
            //
            //  Y separarlas costaria cuatro pixeles de alto que salen de lo
            //  unico para lo que existe esta pagina: en 280x653 el carril anda
            //  por 21 px y son cuatro carriles. Ademas la lectura buena es esa:
            //  P1..P8 dice QUE pinta la brocha y SONIDO con QUE pinta -
            //  las dos son la brocha. Lo otro son las nueve herramientas, que
            //  actuan sobre lo que YA esta puesto.
            cierraSong (gBrocha);
            panel.removeFromTop (Metrics::sm);
        }

        //  LAS HERRAMIENTAS DE ARREGLO, en su propia fila y separadas de las
        //  brochas: las de arriba eligen QUE se pinta y estas mueven lo que ya
        //  esta puesto. Mezcladas en una sola fila, INSERTAR quedaba al lado de
        //  SONIDO y las dos parecian la misma clase de cosa.
        //  Y SON DIEZ Y SE MAQUETAN SIEMPRE: DOBLAR baja aqui desde la fila de
        //  modos -que con la rejilla fundida ya son cinco tapas- y las diez
        //  mueven la LINEA DE TIEMPO, o sea lo que haya en el carril, patrones
        //  y clips. La MISMA lista que decidio la altura, no una parecida.
        {
            const int gUtil = panel.getY();
            juce::TextButton* su[10] = { &songLeftBtn, &songRightBtn, &songShortBtn,
                                         &songLongBtn, &songInsertBtn, &songRemoveBtn,
                                         &songCopyBtn, &songPasteBtn, &songLoopBtn,
                                         &songDoubleBtn };
            //  La MISMA pregunta que decidio la altura y no otra parecida: por
            //  el DEDO, que estas nueve son iconos sin rotulo y `moduleBarFits`
            //  mide rotulos. Pedir con una cuenta y colocar con otra es como
            //  una fila se sale de la tarjeta.
            //  Y LA ESCASEZ SE REPARTE, que con diez es lo que separa una fila
            //  corta de una tapa huerfana: cuatro filas de tres dejarian la
            //  ultima con UNA, y una tapa sola al final de una rejilla se lee
            //  como que falta algo. Se reparten 3, 3, 2 y 2.
            const int filas = (panel.getWidth() >= 10 * Metrics::hit) ? 1
                            : (panel.getWidth() >= 5  * Metrics::hit) ? 2 : 4;
            int puesto = 0;
            for (int f = 0; f < filas; ++f)
            {
                const int quedan = 10 - puesto;
                const int enEsta = (quedan + (filas - f) - 1) / (filas - f);
                layoutModuleBar (panel.removeFromTop (Metrics::hit), su + puesto, 0, enEsta);
                puesto += enEsta;
                if (f < filas - 1) panel.removeFromTop (Metrics::halfGap);
            }
            cierraSong (gUtil);
            panel.removeFromTop (Metrics::sm);
        }

        //  EL TRANSPORTE y el largo, en la misma fila: PLAY primero porque es
        //  lo que se toca mas, y el largo con lo que sobre. Girado va DEBAJO DE
        //  LA LINEA DE TIEMPO y no al final de la columna - ver la altura.
        auto& pie = wideFace ? inner : panel;
        auto bottom = pie.removeFromBottom (Metrics::btn);
        {
            const int pw = juce::jmax (Metrics::hit * 2, bottom.getWidth() / 4);
            //  DOS de aire y no seis: la fila mide 44 y seis por lado dejaban
            //  PLAY en 32, por debajo del suelo de 40. Es la tapa que se toca
            //  con la cancion rodando, asi que es la ultima que puede encoger.
            songPlayBtn.setBounds (Lang::takeStart (bottom, pw).reduced (Metrics::halfGap, Metrics::keyAir));
            //  Idem: seis por arriba y seis por abajo de una fila de 44 dejan
            //  el mando del largo de la cancion en 32.
            songLenSlider.setBounds (bottom.reduced (Metrics::halfGap,
                                                     juce::jmax (0, (bottom.getHeight() - Metrics::hit) / 2)));
        }
        pie.removeFromBottom (Metrics::xs);

        //  LA BARRA DE LA LINEA DE TIEMPO, donde estaba la fila de paginas.
        //
        //  Su rango va en COMPASES: el primero visible, cuantos se ven -que es
        //  lo que dice el zoom- y los que la cancion tiene. Asi el pulgar mide
        //  la fraccion que se esta mirando y se arrastra compas a compas, en
        //  vez de saltar de ocho en ocho.
        //
        //  Y DIBUJA EL CABEZAL sobre el total, que es lo que la fila de tapas
        //  hacia pintando de rojo la pagina que tocaba: sin eso, con la cancion
        //  rodando fuera de la vista no habria forma de saber por donde va.
        if (songHayBarra)
        {
            songBarra.setVisible (true);
            songBarra.setBounds (pie.removeFromBottom (BarraVista::kGrueso));
            songBarra.ponRango (songGrid.getPrimerCompas(),
                                songGrid.getCompasesVista(),
                                engine.getSongLength());
        }
        else
        {
            //  Apagar Y vaciar los limites, las dos cosas: un componente
            //  invisible que conserva sus coordenadas sigue estando ahi para
            //  todo lo que mida geometria.
            songBarra.setVisible (false);
            songBarra.setBounds ({});
        }
        inner.removeFromBottom (Metrics::xs);

        songGrid.setBounds (inner);
    }

    // MIX sheet: the sixteen channel strips of one bank, in a panel that scrolls.
    {
        //  The rows no longer negotiate with the card for their height: they
        //  are Metrics::row, always, and the card shows as many of them as it
        //  has room for. What used to be a 24 px row on a small phone - with
        //  a 20 px mute button on it - is now a scroll.
        //  FORTY-FOUR, NOT FORTY. At Metrics::hit the row lost one pixel top
        //  and bottom to its own air and two more to each control's, and M and
        //  S came out 36x36 on every screen in the matrix - under the forty the
        //  rest of the app is held to, on the two keys you hit fastest while
        //  something is playing. Four pixels of row is what buys them.
        const int rowH = Metrics::row;
        //  Dieciseis pads o dieciseis canales: son el mismo numero hoy, y se
        //  escribe con el de la pagina que se esta maquetando para que el dia
        //  que uno de los dos cambie no haya que acordarse de esto.
        const int kMixFilas = (mixPage == mixPageCanales) ? kNumCanales : kPadsPerBank;
        //  La fila de chips es de la pagina de PADS: en CANALES no hay bancos
        //  que elegir, asi que la ficha pide `Metrics::tab` menos y las
        //  dieciseis tiras se quedan con ese alto.
        const int tabsH = (mixPage == mixPageCanales) ? 0 : Metrics::tab + Metrics::sm;
        //  Y el master cuenta como mueble: es una fila fija que no se desplaza,
        //  asi que si no entra en la cuenta se la come al Viewport y la mesa
        //  pierde media fila de canal en las pantallas justas.
        const int masterH = Metrics::btn + Metrics::xs;
        const int mixFurniture = Metrics::md * 2 + Metrics::hit + Metrics::sm + tabsH
                               + Metrics::btn + masterH + Metrics::lg;
        auto inner = sheetFromBottom (mixSheet,
                                      mixFurniture + (wideFace ? kMixFilas / 2 : kMixFilas) * rowH);
        auto titleRow = inner.removeFromTop (Metrics::hit);
        mixCloseButton.setBounds (Lang::takeEnd (titleRow, Metrics::hit).withSizeKeepingCentre (Metrics::hit, Metrics::hit));
        inner.removeFromTop (Metrics::sm);

        //  EL INTERRUPTOR DE VISTA VA EN EL RENGLON DEL TITULO, no en la fila
        //  de chips — y eso es una medida y no una preferencia.
        //
        //  Estuvo en la fila de los bancos, como una quinta tapa, y el banco lo
        //  canto: cinco chips en 280x653 dejan los cuatro bancos en **29 px de
        //  ancho** contra un dedo de 40, o sea 35 TOUCH nuevos en pantallas que
        //  ya estaban. Es exactamente el caso que la ficha de CANCION ya tenia
        //  resuelto y escrito: con DOS vistas basta un interruptor, y el
        //  renglon del titulo mide `Metrics::hit` y solo lleva una palabra
        //  pintada y una cruz, asi que una tapa corta cabe ahi sin costar un
        //  pixel de alto. El titulo se aparta solo con `antesDe`.
        {
            auto zona = Lang::takeEnd (titleRow, juce::jmin (titleRow.getWidth() / 2,
                                                             Metrics::hit * 2));
            juce::TextButton* vb[1] = { &mixVistaBtn };
            layoutModuleBar (zona, vb, 2, 1);
        }

        {
            //  Y LA FILA DE CHIPS SIGUE SIENDO LA DE LOS BANCOS, con sus cuatro
            //  tapas de siempre. En la pagina de CANALES no hay bancos que
            //  elegir -un canal no vive en un banco- asi que no se maqueta y
            //  la ficha pide `Metrics::tab` menos: la pagina nueva no le quita
            //  alto a una ficha que ya pide 936 px en un tope de 499.
            if (mixPage != mixPageCanales)
            {
                auto tabs = inner.removeFromTop (Metrics::tab);
                juce::TextButton* pb[kNumBanks] = { mixBankBtns[0], mixBankBtns[1],
                                                   mixBankBtns[2], mixBankBtns[3] };
                layoutModuleBar (tabs, pb, 0, kNumBanks);
                inner.removeFromTop (Metrics::sm);
            }
        }

        //  Aire SOLO a los lados: la fila mide Metrics::btn -44- y quitarle
        //  cuatro por arriba y cuatro por abajo dejaba las dos tapas en 36,
        //  por debajo del dedo minimo de 40. Es el mismo fallo que
        //  layoutModuleBar tenia con el mismo numero, escrito a mano aqui.
        auto bottom = inner.removeFromBottom (Metrics::btn);
        //  En CANALES la fila es SOLO el RACK, y se lleva el renglon entero:
        //  SIN SOLO no se maqueta alli -ver showMixPage- porque el solo es de
        //  un pad y esa pagina no tiene ninguno.
        if (mixPage == mixPageCanales)
        {
            rackButton.setBounds (bottom.reduced (Metrics::halfGap,
                                                  (Metrics::btn - Metrics::hit) / 2));
        }
        else
        {
            rackButton.setBounds (bottom.removeFromRight (bottom.getWidth() / 3)
                                      .reduced (Metrics::halfGap, (Metrics::btn - Metrics::hit) / 2));
            mixClearSolo.setBounds (bottom.reduced (Metrics::halfGap, (Metrics::btn - Metrics::hit) / 2));
        }
        inner.removeFromBottom (Metrics::xs);

        //  EL MASTER, encima de las dos tapas y debajo de los canales. Fuera
        //  del Viewport a proposito: los dieciseis canales se desplazan y este
        //  no, que un master que hay que buscar arrastrando no es un master.
        {
            auto fila = inner.removeFromBottom (Metrics::btn);
            //  El rotulo se lleva lo mismo que el nombre de un canal, para que
            //  el fader del master empiece donde empiezan los otros dieciseis:
            //  una mesa se lee por donde estan los pomos, y uno desalineado se
            //  lee como otra cosa.
            masterLabel.setBounds (Lang::takeStart (fila, juce::jlimit (48, 92, fila.getWidth() * 24 / 100))
                                       .reduced (Metrics::aireTapa, 0));
            //  Y el numero cae si no cabe, con el mismo criterio que los
            //  canales: un fader que no se puede apuntar es peor que un fader
            //  sin cifra.
            const bool cabeCifra = fila.getWidth() - Metrics::gap - 52 >= 70;
            masterFader.setTextBoxStyle (cabeCifra ? juce::Slider::TextBoxRight : juce::Slider::NoTextBox,
                                         false, 52, Metrics::readout);
            masterFader.setBounds (fila.reduced (Metrics::aireTapa, (Metrics::btn - Metrics::hit) / 2));
            inner.removeFromBottom (Metrics::xs);
        }

        mixScroll.setBounds (inner);

        //  APAISADO, DOS COLUMNAS DE OCHO.
        //
        //  Dieciseis canales apilados son 792 px de contenido, y girado la
        //  ventana tiene 412: la mesa entera son TRES pantallas de arrastre
        //  para mirar dieciseis faders, con 900 px de ancho vacios al lado.
        //  Dos columnas de ocho parten el arrastre por la mitad y usan el
        //  ancho que la pantalla ya tiene. Cada fila sigue siendo la misma
        //  fila: no se quita ningun control, se reparte el sitio.
        const int columnas = wideFace ? 2 : 1;
        //  Dieciseis pads de un banco o dieciseis canales: la cuenta es la
        //  misma, que es lo que hace que la pagina nueva no traiga geometria
        //  nueva.
        const int cuantas  = mixPage == mixPageCanales ? kNumCanales : kPadsPerBank;
        const int porCol   = cuantas / columnas;
        const int contentH = porCol * rowH;
        //  Leave the bar its width only when there IS a bar, or every row is
        //  eight pixels short on the screens that did not need one.
        const int barW = contentH > inner.getHeight() ? mixScroll.getScrollBarThickness() : 0;
        mixRows.setSize (juce::jmax (80, inner.getWidth() - barW), contentH);

        auto rows = mixRows.getLocalBounds();
        const int anchoCol = rows.getWidth() / columnas;
        juce::Rectangle<int> columna;

        //  LAS DIECISEIS TIRAS DE CANAL, con la misma fila menos el pan y el
        //  solo: el sitio en la imagen es del PAD -es lo que se coloca- y el
        //  solo se queda en el pad, que es donde `effectiveGain` lo resuelve.
        //  En su hueco va la cuenta de pads, que la pinta `paintMixRows`.
        if (mixPage == mixPageCanales)
        {
            for (int c = 0; c < kNumCanales; ++c)
            {
                if (c % porCol == 0)
                    columna = (c / porCol < columnas - 1)
                                ? Lang::takeStart (rows, anchoCol) : rows;

                auto row = columna.removeFromTop (rowH).reduced (columnas > 1 ? Metrics::halfGap : 0, Metrics::aireTapaDensa);
                canRowX[(size_t) c] = row.getX();
                row.removeFromLeft (juce::jlimit (48, 92, columna.getWidth() * 24 / 100));

                canMutes[c]->setBounds (row.removeFromRight (Metrics::hit).reduced (0, Metrics::aireTapaDensa));
                row.removeFromRight (Metrics::halfGap);

                auto faderCell = row.reduced (Metrics::aireTapa, Metrics::aireTapaDensa);
                const bool tight = faderCell.getWidth() - Metrics::gap - 46 < 70;
                canFaders[c]->setTextBoxStyle (tight ? juce::Slider::NoTextBox : juce::Slider::TextBoxRight,
                                               false, 46, Metrics::readout);
                canFaders[c]->setBounds (faderCell);
            }
        }
        else
        for (int i = mixBank * kPadsPerBank; i < (mixBank + 1) * kPadsPerBank; ++i)
        {
            const int enBanco = i - mixBank * kPadsPerBank;
            if (enBanco % porCol == 0)
                columna = (enBanco / porCol < columnas - 1)
                            ? Lang::takeStart (rows, anchoCol) : rows;

            auto row = columna.removeFromTop (rowH).reduced (columnas > 1 ? Metrics::halfGap : 0, Metrics::aireTapaDensa);
            mixRowX[(size_t) i] = row.getX();
            row.removeFromLeft (juce::jlimit (48, 92, columna.getWidth() * 24 / 100));   // chip + number + name
            //  Padding here is not decoration, it is the hit area coming off
            //  the control. The pan was losing twelve pixels of a forty-pixel
            //  row to margins and ending up shorter than the M and S beside it.
            //  M and S are two different decisions about the channel, not one
            //  two-letter control, so they get the same air as everything else
            //  on the row.
            //  Reduced vertically only. Two pixels off each side of a cell that
            //  is exactly Metrics::hit wide is a 36 px key, and the air was
            //  already there: the halfGap between them is what separates M from
            //  S, so taking it out of the key as well paid for the same gap
            //  twice and left both under the floor on every screen measured.
            mixSolos[i]->setBounds (row.removeFromRight (Metrics::hit).reduced (0, Metrics::aireTapaDensa));
            row.removeFromRight (Metrics::halfGap);
            mixMutes[i]->setBounds (row.removeFromRight (Metrics::hit).reduced (0, Metrics::aireTapaDensa));
            row.removeFromRight (Metrics::halfGap);
            //  ...and the pan is a target too, so it gets a floor rather than a
            //  share: a third of the row came to twenty-six pixels of travel on
            //  a 280 px screen, for a control that has to go both ways from
            //  centre. Where the floor and a fader worth aiming at do not both
            //  fit, THE PAN GOES - it is the one control on this row that has a
            //  full-size knob of its own one tap away in the PADS sheet, and a
            //  fader you cannot aim has no such second home. Hidden, not
            //  shrunk: jlimit would have clamped it back up to a width the row
            //  does not have and drawn it over the fader.
            //  Y LA ESCALERA PASA A SER DE TRES, con el ANCHO cayendo PRIMERO.
            //
            //  El pan dice DONDE esta el sonido y el ancho CUANTO ocupa: los
            //  dos son de la misma decision y por eso van juntos. Y el orden
            //  no es arbitrario — de los dos, el pan es el que se mueve en
            //  cada mezcla y el ancho el que se toca una vez, asi que donde
            //  solo cabe uno se queda el pan. Ninguno de los dos desaparece
            //  de la app: los dos tienen su mando a tamaño real en EL PAD, a
            //  un toque.
            //  Con los dos puestos cada uno pide un CUARTO y no un tercio: a
            //  tercios, dos deslizadores dejan al fader la tercera parte de la
            //  fila y en 412x915 la cuenta salia por dos pixeles -94 contra el
            //  suelo de 96- o sea que el ancho no aparecia en NINGUNA pantalla.
            //  El reparto de uno solo se queda en su tercio, que es el medido.
            const int panW    = juce::jlimit (Metrics::hit + 4, 78, row.getWidth() / 3);
            const int dosW    = juce::jlimit (Metrics::hit + 4, 78, row.getWidth() / 4);
            const bool roomAncho = row.getWidth() - 2 * dosW >= 96;
            const bool room = roomAncho || (row.getWidth() - panW >= 96);
            const int celda = roomAncho ? dosW : panW;
            mixPans[i]->setVisible (room);
            //  Y APAGADO EN UNA MUESTRA MONO: no hay lado que abrir ni cerrar,
            //  que es lo que su mando de EL PAD ya hace. Apagado y no
            //  escondido, que esconderlo daria una fila con dos formas.
            //  Y la pregunta es la MISMA que la de EL PAD -el buffer que
            //  sostiene la interfaz, nunca el que adopto el hilo de audio-
            //  o serian dos reglas y una diria que si donde la otra dice
            //  que no.
            const bool estereo = uiSample[(size_t) i] != nullptr
                                 && uiSample[(size_t) i]->buffer.getNumChannels() > 1;
            mixAnchos[i]->setVisible (roomAncho);
            mixAnchos[i]->setEnabled (estereo);
            if (roomAncho)
                mixAnchos[i]->setBounds (row.removeFromRight (celda)
                                        .reduced (Metrics::aireTapa, Metrics::aireTapaDensa));
            else
                mixAnchos[i]->setBounds ({});
            if (room)
                mixPans[i]->setBounds (row.removeFromRight (celda)
                                          .reduced (Metrics::aireTapa, Metrics::aireTapaDensa));

            //  On a narrow phone the level's number was eating the level.
            //  Forty-six pixels of readout plus its air out of an eighty-five
            //  pixel cell left thirty for the fader itself - a control you set
            //  by where the thumb is, reduced to a control you cannot aim.
            //  Where it does not fit, the number goes and the fader stays: the
            //  exact figure is one tap away in the PADS sheet, and a mixer is
            //  read by the shape of its faders, not by sixteen decimals.
            auto faderCell = row.reduced (Metrics::aireTapa, Metrics::aireTapaDensa);
            const bool tight = faderCell.getWidth() - Metrics::gap - 46 < 70;
            mixFaders[i]->setTextBoxStyle (tight ? juce::Slider::NoTextBox : juce::Slider::TextBoxRight,
                                           false, 46, Metrics::readout);
            mixFaders[i]->setBounds (faderCell);
        }
    }

    // El panel XY, si esta abierto: la mitad de arriba de la cara, y ni un
    // pixel dentro de la costura de PADS. Ver XyPanel en la cabecera.
    if (xyPanel.isVisible() && ! faceTopArea.isEmpty())
    {
        //  EL PANEL SE QUEDA CON LO QUE USA, no con la mitad de la cara.
        //
        //  Antes ocupaba faceTopArea entera y centraba dentro un cuadrado del
        //  lado menor: en un hueco de 380x700 eso son 380 de mando y 320 de
        //  NADA, repartidos en dos franjas vacias, arriba y abajo. La foto que
        //  lo enseno tenia el mando en la esquina de abajo a la izquierda y un
        //  tercio del panel en blanco encima.
        //
        //  Ahora el lado del cuadrado sale del ancho -que es lo que sobra en
        //  vertical- y la altura del panel sale del lado. Sin centrar, sin
        //  hueco: si no cabe, el que se recorta es el mando.
        const int fixed = 2 * Metrics::md          // margenes de arriba y abajo
                        + Metrics::hit             // titulo
                        + 14                       // que hace soltar el dedo
                        + Metrics::sm
                        + Metrics::hit             // las seis ranuras, en UNA fila
                        + Metrics::sm;
        const int side = juce::jlimit (60,
                                       juce::jmax (60, faceTopArea.getWidth() - 2 * Metrics::margenFichaX),
                                       faceTopArea.getHeight() - fixed);
        xyPanel.setBounds (faceTopArea.withHeight (juce::jmin (faceTopArea.getHeight(),
                                                              fixed + side)));

        auto inner = xyPanel.getLocalBounds().reduced (Metrics::margenFichaX, Metrics::margenFichaY);

        auto titleRow = inner.removeFromTop (Metrics::hit);
        xyCloseButton.setBounds (Lang::takeEnd (titleRow, Metrics::hit)
                                    .withSizeKeepingCentre (Metrics::hit, Metrics::hit));
        Lang::takeEnd (titleRow, Metrics::xs);
        //  MOMENTANEO / FIJO en la fila del titulo, no en una fila propia al
        //  fondo: es un interruptor de dos estados y estaba gastando 40 px de
        //  alto mas su rotulo para decir una palabra.
        {
            //  El hueco de la palabra son 2*md y no 2*sm: con 2*sm el banco
            //  saco MOMENTANEO pidiendo 70 px de los 63 que le quedaban en las
            //  siete pantallas. La fuente con la que se mide aqui no es
            //  exactamente la que dibuja la tapa, asi que el margen se pone
            //  por arriba y no se afina al pixel.
            const auto capFont = ZatiColours::monoFont (11.0f, true).withExtraKerningFactor (0.06f);
            const int w = juce::jlimit (88, juce::jmax (88, titleRow.getWidth() / 2),
                                        (int) std::ceil (juce::GlyphArrangement::getStringWidth (
                                            capFont, xyLatchButton.getButtonText())) + 2 * Metrics::md);
            xyLatchButton.setBounds (Lang::takeEnd (titleRow, w).reduced (0, 0));
        }
        inner.removeFromTop (Metrics::bandaSubtitulo);              // pintado: que hace soltar el dedo
        inner.removeFromTop (Metrics::sm);

        //  Los seis, en una fila y del mismo ancho que los seis de la cara.
        //  En dos columnas de tres al lado del mando eran una rejilla que hay
        //  que leer; en fila son la misma barra que ya esta aprendida.
        {
            auto row = inner.removeFromTop (Metrics::hit);
            for (int f = 0; f < xyFxButtons.size(); ++f)
                xyFxButtons[f]->setBounds (Lang::takeStart (row, row.getWidth() / (kNumRanuras - f))
                                             .reduced (Metrics::halfGap / 2, 0));
            inner.removeFromTop (Metrics::sm);
        }

        //  Cuadrado: el lado es el menor de los dos que quedan. Es lo unico
        //  que este componente tiene que garantizar - si un eje se barre con un
        //  gesto y el otro con dos, los dos parametros no se tocan igual.
        const int sq = juce::jmax (60, juce::jmin (inner.getWidth(), inner.getHeight()));
        xyPad.setBounds (inner.withSizeKeepingCentre (sq, sq));
    }

    // SEC sheet, two pages: PASOS is the grid and what plays it; PASO is the
    // step you tapped, plus the chain and the swing. See SeqPage in the header
    // for why it stopped being one card.
    {
        const int lanes = StepGrid::kLanes;
        constexpr int nameH = 14 + ZatiLookAndFeel::kTextPad;
        //  A named control is three things: its name, itself, and the air under
        //  it. Budget the band, never the control on its own.
        constexpr int bandH = nameH + Metrics::hit + Metrics::sm;

        const bool onGrid  = (seqPage == seqPageGrid);
        const bool onPiano = (seqPage == seqPagePiano);
        pianoGrupos.clear();

        const int patLen   = engine.getPatternLength (selectedPattern);
        const int bars     = juce::jmax (1, patLen / kStepCols);

        //  ASK FOR WHAT YOU WILL ACTUALLY GET. sheetFromBottom clamps the card
        //  at 78% of the window and says nothing; whatever the layout asked
        //  for beyond that is simply taken off the last thing laid out. The old
        //  card asked for 800 px on a 640 px phone, and the 300 px of shortfall
        //  came out of the grid - sixteen lanes in 55 px, three and a half
        //  pixels a lane. So the lane height is now DERIVED from the cap rather
        //  than clamped up to a number the card was never going to have, and
        //  `wanted` can never exceed `capH`.
        //  Y APAISADO EL TOPE ES 0.90, como en sheetFromBottom y como en la
        //  otra pagina de esta MISMA funcion (topeSeq). Estaba clavado al 78%,
        //  asi que en 915x412 esta cuenta trabajaba con 321 px de tarjeta
        //  cuando la tarjeta iba a medir 371: cincuenta px de menos, y de ahi
        //  sale la altura del carril de la rejilla.
        const int capH   = altoTarjeta (full);
        const int chrome = Metrics::md * 2          // the card's own margins
                         + Metrics::hit             // title row
                         + Metrics::sm
                         + Metrics::tab             // the two tabs
                         + Metrics::sm;

        //  Rotated, the card is short and wide: sixteen lanes cannot share 200
        //  px of height AND leave room for four stacked controls under them.
        //  So in landscape the grid takes the whole height of the card and the
        //  controls stand in a column beside it - which is the shape the window
        //  already is, instead of the shape a phone is.
        const int sideCol = wideFace ? juce::jlimit (150, 260, full.getWidth() / 4) : 0;

        //  EL ANCHO QUE LA REJILLA VA A TENER, decidido aqui porque de el sale
        //  cuantas columnas caben y de eso si hace falta la barra. Es la misma
        //  regla que ya obliga a preguntar por el ancho de la COLUMNA y no por
        //  el de la pantalla en CANCION: pedir con una cuenta y colocar con
        //  otra es como una fila se sale de la tarjeta.
        const int anchoRejilla = wideFace
                                   ? anchoTarjetaInterior (full.getWidth()) - sideCol - Metrics::gap
                                   : anchoTarjetaInterior (safeArea().getWidth());
        //  CUANTAS COLUMNAS ENTERAS CABEN con esa altura de carril. La celda es
        //  CUADRADA -el lado sale del ALTO, que es lo que se pidio para que
        //  entren los dieciseis pads lo mas altos posible- asi que el ancho de
        //  una columna es el alto del carril por el zoom. La cuenta esta
        //  escrita aqui y en StepGrid::numCols, y las dos tienen que decir lo
        //  mismo: aqui se DECIDE si hace falta la barra y alli se DIBUJA.
        auto colsCon = [&] (int lh)
        {
            const float cw = juce::jmax (1.0f, (float) lh * seqZoomW);
            return juce::jlimit (1, StepGrid::kMaxCols,
                                 (int) ((float) (anchoRejilla - StepGrid::kGutter) / cw));
        };
        const int costeBarra = Metrics::hit + Metrics::halfGap;

        int laneH  = 0;
        int wanted = 0;

        //  CUANTAS FILAS DE LA TIRA DEL PASO, decidido AQUI y no dentro de una
        //  de las dos ramas, porque las dos paginas dependen de la respuesta:
        //  la de la rejilla para colocarla, y la del patron para NO repetir lo
        //  que la tira ya lleva. Dos sitios que mueven el mismo numero es
        //  exactamente lo que esta ficha tenia y no puede volver a tener.
        int tiraFilas = 0;
        //  Y SOLO SI HAY PASO TOCADO. Sin paso elegido estos mandos no tienen
        //  sobre que actuar - es lo que decia el renglon del pie, "toca un paso
        //  para editarlo" - asi que ensenarlos es ensenar controles muertos.
        const bool pasoAqui = (selectedStep >= 0);
        if (pasoAqui)
        {
            const int base = wideFace ? 0 : bandH + costeBarra + bandH;
            auto cabe = [&] (int filas)
            {
                const int coste = filas * (nameH + Metrics::hit)
                                + (filas - 1) * Metrics::halfGap + Metrics::sm;
                //  Girado la rejilla no comparte columna con los mandos -esos
                //  van al lado- pero SI con la tira, que va debajo de ella. El
                //  primer intento daba por buena la tira apaisada sin mirar, y
                //  la tarjeta apaisada mide 321 px: la celda de paso se quedaba
                //  en 5.2 px de alto, la sexta parte del suelo. La cuenta es la
                //  misma en las dos orientaciones; lo unico que cambia es que
                //  girado no hay filas apiladas encima.
                return (capH - chrome - base - coste) / lanes >= kMinLaneH;
            };
            //  Tres filas: NOTA/GOLPE, REPETIR/CORTE y los cuatro bloqueos.
            //  La tercera cuesta 58 px y en 412x915 baja la celda de 16.9 a
            //  13.3 - por encima del suelo de 12, asi que en un telefono
            //  normal los cuatro salen. Donde no quepa, se cae entera: los
            //  bloqueos son lo ultimo que llega y lo primero que sobra.
            tiraFilas = cabe (3) ? 3 : (cabe (2) ? 2 : (cabe (1) ? 1 : 0));
        }
        seqTiraFilas = tiraFilas;

        if (onGrid)
        {
            //  Lo que la pagina lleva SIEMPRE: PATRON/LARGO, el compas cuando
            //  hay mas de uno, y el TEMPO. Las dos filas que se pueden quitar
            //  -los bancos de pads y el COPIAR/PEGAR del patron- no entran
            //  aqui a proposito: se toman despues y solo si, hechas las
            //  cuentas, los carriles siguen por encima del suelo.
            int stacked = wideFace ? 0 : bandH + bandH;

            //  LA TIRA DEL PASO, que es por lo que esta ficha tenia dos
            //  paginas y el proyecto anterior ninguna.
            //
            //  Editar un paso costaba un viaje: tocarlo en PASOS, cambiar de
            //  pestana a PASO, mover la nota, volver. Cuatro toques para subir
            //  un semitono, y con la rejilla fuera de la vista mientras se
            //  edita - o sea sin ver lo que se esta cambiando. Ahora los
            //  cuatro mandos del paso viven DEBAJO de la rejilla, y solo
            //  cuando hay un paso tocado: sin paso elegido no tienen sobre que
            //  actuar, y ocupar sitio para nada es lo que le sobra a esta
            //  pagina.
            //
            //  Son los MISMOS componentes que la pagina PASO, no copias: solo
            //  una de las dos paginas se maqueta a la vez, asi que no hay dos
            //  sitios que mantener ni dos que puedan quedarse viejos.
            //
            //  Lo que la tira se lleva, ya decidido arriba.
            stacked += tiraFilas * (nameH + Metrics::hit)
                     + juce::jmax (0, tiraFilas - 1) * Metrics::halfGap
                     + (tiraFilas > 0 ? Metrics::sm : 0);
            //  Y SIN SUELO, que es lo que fallaba.
            //
            //  Aqui ponia jlimit (12, 26, ...) con un parrafo explicando que
            //  doce es un suelo y no un objetivo. Pero jlimit clampa TAMBIEN
            //  HACIA ARRIBA: donde solo habia sitio para ocho pedia doce, la
            //  tarjeta salia mas alta que el tope del 78 % y sheetFromBottom
            //  se lo comia en silencio - de lo ULTIMO que se maqueta, que es
            //  la rejilla. Medido: celda de 8 px de alto en 360x640 pidiendo
            //  doce. Pedir lo que hay y quitar filas prescindibles hasta que
            //  quepa es lo que de verdad sube la celda.
            //  LA BARRA SOLO SI HAY ALGO QUE DESPLAZAR, y eso ya no es «mas de
            //  un compas»: con la celda cuadrada las columnas que entran salen
            //  del ALTO, asi que un patron de un compas puede no caber y uno de
            //  dos puede caber entero. Se pregunta en DOS pasadas y no en una:
            //  con la barra puesta el carril es mas bajo, la celda mas
            //  estrecha y caben MAS columnas, asi que la respuesta solo puede
            //  ir en un sentido y esto termina.
            //
            //  Y donde con la barra ya cabria todo se queda igual: el pulgar
            //  llena la pista, que es exactamente lo que «no hay nada que
            //  desplazar» tiene que verse.
            const int sinBarra = juce::jmin (Metrics::hit,
                                             juce::jmin ((capH - chrome - stacked) / lanes,
                                                         (anchoRejilla - StepGrid::kGutter)
                                                           / juce::jmax (1, AudioEngine::kBarSteps)));
            seqHayBarra = colsCon (juce::jmax (1, sinBarra)) < patLen;
            if (seqHayBarra) stacked += costeBarra;

            //  Y EL TOPE SUBE DE 26 A UN DEDO. Veintiseis era el alto de un
            //  carril cuando el ancho lo repartia otro numero; desde que la
            //  celda es cuadrada ese tope es tambien el ancho, y una celda que
            //  se pinta con el dedo arrastrado no puede tener por tope menos de
            //  lo que mide el dedo.
            //  Y EL ANCHO MANDA TAMBIEN, que es la otra mitad de «cuadradas».
            //
            //  Con el lado sacado SOLO del alto, en 412x915 el carril salia a
            //  37 px y de un patron de dieciseis pasos entraban QUINCE: una
            //  columna de menos, un trozo de tarjeta sin usar a la derecha y
            //  una barra para llegar al paso 16 de un compas. «Aprovecha el
            //  espacio a lo ancho para que las cuadriculas de forma Default
            //  carguen en cuadrado», que fue como se pidio.
            //
            //  El lado es el MENOR de los dos: el que deja los dieciseis pads
            //  lo mas altos posible y el que hace que un COMPAS entero quepa
            //  a lo ancho. Donde manda el alto -una pantalla estrecha y
            //  alta- entran las columnas que entren y la barra llega al
            //  resto, que es la escalera de siempre; donde manda el ancho, el
            //  compas entra entero y lo que sobra de alto se lo queda la
            //  maquina, que se sigue viendo detras de la tarjeta.
            const int ladoPorAncho = (anchoRejilla - StepGrid::kGutter)
                                       / juce::jmax (1, AudioEngine::kBarSteps);
            laneH  = juce::jmin (Metrics::hit,
                                 juce::jmin ((capH - chrome - stacked) / lanes,
                                             ladoPorAncho));
            //  Girado la tarjeta se queda con TODO el alto: la rejilla esta al
            //  lado de los controles, no debajo, asi que pedir "lo que suman
            //  las filas" -que apaisado es cero- dejaba la tarjeta baja y la
            //  columna sin sitio para su ultima fila.
            wanted = wideFace ? capH
                              : chrome + stacked + lanes * juce::jmax (1, laneH);
        }
        else
        {
            //  La fila de herramientas puede ser DOS, asi que la altura que se
            //  pide lo cuenta: pedirla de una y usar dos es como un control se
            //  queda con altura cero.
            //  OCHO desde que la FILA de un pad se puede copiar y pegar. La
            //  lista tiene que ser LA MISMA que la de abajo: la primera version
            //  dejo esta en cinco y la de abajo en seis, y HUMANIZAR no se
            //  colocaba nunca - existente, invisible e imposible de tocar.
            juce::TextButton* pb5[8] = { &patLeftBtn, &patRightBtn, &patDoubleBtn,
                                         &seqHumanBtn, &copyPatBtn, &pastePatBtn,
                                         &copyRowBtn, &pasteRowBtn };
            //  El ancho util de la tarjeta, con la misma cuenta que usa la
            //  ficha de CANCION: aqui todavia no existe `inner`, y estimarlo
            //  a ojo es como se pide una altura que luego no vale.
            const int anchoDeLaTarjeta = anchoTarjetaInterior (safeArea().getWidth());
            const int anchoCol = wideFace ? (anchoDeLaTarjeta - Metrics::gap) / 2 : anchoDeLaTarjeta;
            //  Una fila si las ocho caben, dos si caben de cuatro en cuatro, y
            //  si no, tres: tres, tres y dos. `filasUtil` es lo que se paga DE
            //  MAS sobre la primera fila, que ya la cuenta bandH.
            //  Y LAS DOS MITADES, no solo la primera. Preguntar por las cuatro
            //  primeras -ATRAS, ADELANTE, DOBLAR, HUMANIZAR- y colocar tambien
            //  las cuatro segundas -COPIAR, PEGAR, COPIAR FILA, PEGAR FILA- es
            //  responder a otra pregunta: las de abajo son mas largas. Medido:
            //  en 344x882 "COPIAR FILA" pedia 82 px de letra y tenia 76, y en
            //  360x640 82 contra 81. La fila decia que cabia porque nadie habia
            //  medido esa fila.
            const int filasTools = moduleBarFits (anchoCol, pb5, 8) ? 1
                                 : (moduleBarFits (anchoCol, pb5, 4)
                                    && moduleBarFits (anchoCol, pb5 + 4, 4)) ? 2 : 3;
            const int filasUtil = (filasTools - 1) * (Metrics::hit + Metrics::halfGap);

            //  LO QUE LA TIRA YA LLEVA, AQUI NO SE REPITE.
            //
            //  Las dos pestanas movian los mismos cuatro mandos del paso, que
            //  es justo lo que esta app no permite: dos sitios para lo mismo
            //  son dos maquetados que mantener y una pregunta -"¿cual de las
            //  dos es la buena?"- que no deberia existir. Ahora el dueno del
            //  paso es la TIRA, y esta pagina se queda solo con lo que la tira
            //  no pudo llevarse: en las dos pantallas mas estrechas la tira es
            //  de una fila y REPETIR y CORTE se quedan aqui; si es de dos, aqui
            //  no queda nada del paso y la pestana pasa a llamarse PATRON.
            //  Y SOLO SI HAY PASO TOCADO. Sin paso elegido estos mandos no
            //  tienen sobre que actuar - es lo que decia el renglon del pie,
            //  "toca un paso para editarlo" - asi que ensenarlos aqui es
            //  ensenar controles muertos. Con paso tocado viven en la tira, y
            //  aqui solo aparecen los que la tira no pudo llevarse.
            const int notaCost  = (pasoAqui && tiraFilas == 0) ? bandH : 0;
            const int golpeCost = (pasoAqui && tiraFilas <  2) ? bandH : 0;
            //  Y los cuatro bloqueos, que caen aqui cuando la tira no llego a
            //  su tercera fila. Se cuenta DONDE SE DECIDE y no solo donde se
            //  coloca: sin esta linea la columna cree tener 58 px que no son
            //  suyos y la ultima fila se queda en cero de alto - el mismo
            //  fallo que ya costo un mando de 393x0 en apaisado.
            //
            //  Y SOLO SI LA PAGINA LOS AGUANTA. Esta pagina ya iba al limite:
            //  con la banda puesta a ciegas, en 280x653 y en 360x640 el mando
            //  de REJILLA -el ultimo de la pagina- salia de 217x0 y el de
            //  EUCLIDES de 217x31, porque sheetFromBottom recorta al 78 % y lo
            //  que falta se lo come lo ultimo que se maqueta, en silencio. Se
            //  pregunta con el mismo tope que aplica sheetFromBottom, que es
            //  el unico numero con el que la respuesta es la de la tarjeta que
            //  se va a dibujar.
            const int topeSeq = altoTarjeta (full);
            //  La altura que pide la pagina, con y sin la banda de bloqueos y
            //  con la MISMA cuenta: preguntar con una y colocar con otra es
            //  como se llega a un control de altura cero.
            //  Lo que cuesta la fila de la CADENA con su QUITAR CADENA: la
            //  primera cosa que se cae cuando la pagina no cabe. Ver abajo.
            //  Con las filas que la cadena vaya a usar: preguntarlo aqui con una
            //  cuenta y colocarlo abajo con otra es como una fila se queda con
            //  altura cero, que es el fallo que esta ficha ya tuvo dos veces.
            //  El ancho es el mismo que usa la fila de herramientas.
            const int filasCadena = (anchoCol / kNumPatterns - 4 >= Metrics::hit) ? 1 : 2;
            const int cadenaCost = nameH + filasCadena * Metrics::hit
                                 + (filasCadena - 1) * Metrics::halfGap + Metrics::xs
                                 + Metrics::hit + Metrics::sm;
            auto stepBandsCon = [&] (int lc, int cadena)
            {
                return cadena                            // CADENA + QUITAR CADENA
                     + notaCost                              // NOTA, si la tira no la lleva
                     + golpeCost                             // GOLPE / REPETIR y CORTE
                     + lc                                    // BLOQUEOS, si la tira no los lleva
                     + bandH + filasUtil                     // PATRON: las herramientas
                     + nameH + Metrics::hit                  // SWING
                     + Metrics::sm + nameH + Metrics::hit    // EUCLIDES
                     + Metrics::sm + nameH + Metrics::hit;   // REJILLA
            };
            //  The line at the foot that says which step is being edited is
            //  laid out, not squeezed in under the last control: unbudgeted it
            //  was drawn straight across the swing slider's track.
            //  EN DOS COLUMNAS MANDA LA MAS ALTA, no la primera.
            //
            //  Apaisado esta pagina se parte en dos: CADENA y NOTA a la
            //  izquierda, GOLPE, SWING y REJILLA a la derecha. La altura que se
            //  pedia modelaba la columna corta, asi que la tarjeta salia mas
            //  baja de lo que la otra necesita y la ULTIMA fila de la derecha
            //  -REJILLA- se quedaba sin sitio: medido en 915x412, un mando de
            //  393x0, o sea existente, invisible e imposible de tocar.
            //
            //  Dos columnas caben en la altura de la MAYOR. Es la misma cuenta
            //  que ya estaba, hecha entera.
            auto colACon = [&] (int cadena)
            {
                return cadena                                         // CADENA + QUITAR CADENA
                     + notaCost                                       // NOTA, si queda aqui
                     + bandH + filasUtil;                             // PATRON
            };
            auto colBCon = [&] (int lc)
            {
                return golpeCost                                      // GOLPE, si queda aqui
                     + lc                                             // BLOQUEOS, si quedan aqui
                     + nameH + Metrics::hit                           // SWING
                     + Metrics::sm + nameH + Metrics::hit             // EUCLIDES
                     + Metrics::sm + nameH + Metrics::hit;            // REJILLA
            };
            auto pide = [&] (int lc, int cadena)
            {
                return chrome + (wideFace ? juce::jmax (colACon (cadena), colBCon (lc))
                                          : stepBandsCon (lc, cadena))
                              + Metrics::sm + kSeqFootH;
            };

            //  CABE O NO CABE, con la misma pregunta que ya deciden BANCO y
            //  PADS en la pagina de la rejilla.
            //
            //  Esta pagina se pasaba del tope en las dos pantallas mas
            //  estrechas y nadie se enteraba: sheetFromBottom recorta al 78 %
            //  y lo que falta se lo come lo ULTIMO que se maqueta. Medido en
            //  280x653 sin paso tocado -o sea sin nada de lo nuevo-: EUCLIDES
            //  salia de 217x31 y REJILLA de 217x0, existente, invisible e
            //  imposible de tocar.
            //
            //  Se cae por orden: primero la banda de los cuatro bloqueos, que
            //  solo existe con un paso tocado; y despues la CADENA con su
            //  QUITAR CADENA, que en 280 px reparte ocho tapas a 24 px de
            //  ancho - ya por debajo del dedo minimo en esa pantalla, o sea
            //  rota alli de todas formas. Y si al quitar la cadena vuelve a
            //  haber sitio, los bloqueos vuelven: la prioridad es esa y no el
            //  orden en que se pregunta.
            const bool quiereLock = (pasoAqui && tiraFilas < 3);
            int lockCost = quiereLock ? bandH : 0;
            int cadenaAqui = cadenaCost;
            if (pide (lockCost, cadenaAqui) > topeSeq && lockCost > 0) lockCost = 0;
            if (pide (lockCost, cadenaAqui) > topeSeq)                 cadenaAqui = 0;
            if (quiereLock && lockCost == 0 && pide (bandH, cadenaAqui) <= topeSeq) lockCost = bandH;
            seqLocksAqui  = (lockCost > 0);
            seqCadenaAqui = (cadenaAqui > 0);
            wanted = pide (lockCost, cadenaAqui);
        }

        //  LA PAGINA DEL PIANO pide lo suyo: la rejilla de tono se lleva todo lo
        //  que sobre, que es lo contrario de las otras dos - aqui lo unico que
        //  importa es cuantas notas se ven a la vez, y veinticinco filas en
        //  200 px son ocho pixeles por tecla.
        if (onPiano)
        {
            //  La fila de tapas puede ser DOS. Ver la maqueta: cinco no caben en
            //  las pantallas estrechas, y pedir una fila y colocar dos es como
            //  la rejilla del piano se queda sin sitio.
            //  NUEVE desde que la pagina tiene transporte: el modo y PLAY
            //  van delante, que es lo que se toca mientras se escribe -oir lo
            //  que llevas es la mitad de escribirlo- y las tres herramientas
            //  al final, juntas.
            //  ONCE desde que la pagina tiene SEL y el zoom horizontal, y con
            //  las MISMAS tapas que se van a colocar: preguntar por nueve y
            //  poner once es pedir con una cuenta y colocar con otra, que es
            //  como la rejilla se queda sin sitio.
            juce::TextButton* pb5[11] = { &seqModoBtn, &seqPlayBtn,
                                          &pianoOctDownBtn, &pianoOctUpBtn, &pianoVerBtn,
                                          &pianoZoomBtn,
                                          &pianoClearBtn, &pianoLapizBtn, &pianoGomaBtn,
                                          &pianoCorteBtn, &pianoSelBtn };
            const int anchoDeLaTarjeta = anchoTarjetaInterior (safeArea().getWidth());
            //  Apaisado no hay fila de tapas que pedir: se van a la columna
            //  de al lado. Pedir una fila que luego no se coloca es pedir 48 px
            //  de mas de lo unico que escasea girado.
            const int filasTapas = wideFace ? 0 : (moduleBarFits (anchoDeLaTarjeta, pb5, 11) ? 1 : 2);
            filasTapasPiano = filasTapas;
            wanted = chrome + pianoGrid.getFilas() * PianoRoll::kAltoObjetivo + Metrics::sm + 14
                   + filasTapas * Metrics::hit
                   + juce::jmax (0, filasTapas - 1) * Metrics::halfGap;
        }

        auto inner = sheetFromBottom (seqSheet, wanted);

        auto titleRow = inner.removeFromTop (Metrics::hit);
        seqCloseButton.setBounds (Lang::takeEnd (titleRow, Metrics::hit).withSizeKeepingCentre (Metrics::hit, Metrics::hit));
        //  LA PUERTA A LA REJILLA DE DIECISEIS, en la cabecera de la FICHA y no
        //  en la fila de PAD -/+ de la pagina del piano.
        //
        //  Estuvo alli y salio medido: esa fila se reparte POR EL TEXTO, y el
        //  rotulo de la puerta son dos cifras -lo mas corto de los tres- asi
        //  que en arabe y en chino, donde "PAD +" mide mas, le tocaban 38 px.
        //  Veinticuatro casos por debajo del dedo en 360x640. Y no se arregla
        //  subiendo el suelo: layoutModuleBar solo lo sube si TODAS las tapas
        //  caben a 40 con su texto, y ahi no caben - forzarlo cambia un apreton
        //  por un corte, que no es un arreglo.
        //
        //  Aqui es una caja fija de Metrics::hit, o sea el dedo exacto, y
        //  ademas queda en el MISMO sitio que en la ficha del pad: las dos
        //  fichas que editan "el pad que tengas elegido" lo cambian desde el
        //  mismo rincon. Y vale para las tres paginas, que las tres actuan
        //  sobre el pad elegido - el piano dibuja sus notas, la tira edita su
        //  paso y EUCLIDES reescribe su fila.
        {
            Lang::takeEnd (titleRow, Metrics::xs);
            const bool cabe = titleRow.getWidth() >= Metrics::hit * 2;
            pianoPadPickBtn.setVisible (cabe);
            pianoPadPickBtn.setBounds (cabe ? Lang::takeEnd (titleRow, Metrics::hit)
                                                .withSizeKeepingCentre (Metrics::hit, Metrics::hit)
                                            : juce::Rectangle<int>());
        }

        //  LA VENTANA DE PISTAS VA EN LA FILA DEL TITULO, y no con los bancos
        //  ni con el transporte, que fueron los dos primeros intentos y los dos
        //  fallaron por lo mismo: no existen donde hacen falta.
        //
        //  La fila de bancos se cae en 360x640, en el Fold y apaisado -es de
        //  las prescindibles, y se va para que la celda vuelva a su suelo-, que
        //  son justo las tres pantallas donde ver ocho pistas en vez de
        //  dieciseis cambia mas: de 15.0 px de celda a 30.0. Y la fila del
        //  transporte reparte la mitad de su ancho entre PLAY, TAP y VACIAR:
        //  una cuarta tapa pide 160 px donde hay 173 en un movil grande y 150
        //  en el Fold. Ofrecer el arreglo solo donde no hace falta es no
        //  ofrecerlo.
        //
        //  La del titulo existe en las TRES paginas y en las siete pantallas
        //  -lleva la cruz de cerrar- y ya sostiene tapas: PAD -/+ viven ahi en
        //  la pagina del piano. El titulo se pinta con drawFittedText y encoge;
        //  una tapa no.
        if (seqPage == seqPageGrid)
        {
            seqPistasBtn.setVisible (true);
            seqPistasBtn.setBounds (Lang::takeEnd (titleRow, 64)
                                      .withSizeKeepingCentre (64, Metrics::hit));
            Lang::takeEnd (titleRow, Metrics::halfGap);

            //  Y EL ZOOM DE ANCHO AL LADO DEL DE ALTO, que son la misma
            //  pregunta por los dos ejes: uno dice cuantas PISTAS se ven y el
            //  otro cuantos PASOS. Separarlos seria dos sitios para decidir el
            //  tamano de la misma celda.
            //
            //  Con la escalera de siempre y no clavado: la fila del titulo ya
            //  sostiene la cruz, el selector de pad y la ventana de pistas, y
            //  en 280x653 una cuarta tapa deja al titulo sin renglon. Donde no
            //  cabe se apaga Y se le vacian los limites -las dos cosas- y la
            //  celda se queda cuadrada, que es como arranca: lo que se pierde
            //  es poder cambiarla, no el estado bueno.
            const int anchoZoom = 56;
            const bool cabeZoom = titleRow.getWidth() >= anchoZoom + Metrics::hit * 2;
            seqZoomBtn.setVisible (cabeZoom);
            if (cabeZoom)
            {
                seqZoomBtn.setBounds (Lang::takeEnd (titleRow, anchoZoom)
                                        .withSizeKeepingCentre (anchoZoom, Metrics::hit));
                Lang::takeEnd (titleRow, Metrics::halfGap);
            }
            else
            {
                seqZoomBtn.setBounds ({});
            }
        }
        else
        {
            seqPistasBtn.setVisible (false);
            seqPistasBtn.setBounds ({});
            seqZoomBtn.setVisible (false);
            seqZoomBtn.setBounds ({});
        }

        inner.removeFromTop (Metrics::sm);

        {
            auto tabs = inner.removeFromTop (Metrics::tab);
            //  EL FILO SE ALINEA POR LA FILA, NO POR LA TAPA.
            //
            //  Las tres pestanas llevaban su `reduced (aireTapa, 0)` cada una,
            //  asi que el filo de fuera caia DOS pixeles dentro de `inner`
            //  mientras la rejilla se coloca en `inner` clavado y los paneles
            //  se dibujan `panelAireX` por FUERA: tres bordes izquierdos
            //  distintos en la misma ficha, que es lo que se ve como que la
            //  cuadricula no cuadra con los bloques de arriba y de abajo.
            //
            //  Es el mismo arreglo que ya se pago en la paleta de CANCION y
            //  con la misma cuenta: el aire se lo come la FILA una sola vez.
            //  Aqui se hace expandiendo la fila antes de partirla -y no
            //  recortando lado a lado- porque `Lang::takeStart` invierte la
            //  direccion en arabe y «el lado que da a la hermana» no es el
            //  mismo en las cuatro compilaciones. Expandido, el hueco entre
            //  tapas sigue siendo `2 * aireTapa` y el de fuera pasa a cero.
            tabs = tabs.expanded (Metrics::aireTapa, 0);
            const int tercio = tabs.getWidth() / 3;
            seqGridBtn .setBounds (Lang::takeStart (tabs, tercio).reduced (Metrics::aireTapa, 0));
            seqPianoBtn.setBounds (Lang::takeStart (tabs, tercio).reduced (Metrics::aireTapa, 0));
            seqStepBtn .setBounds (tabs.reduced (Metrics::aireTapa, 0));
            //  UNA FILA, UN TRATO. Estas tres se reparten a tercios a mano -no
            //  pasan por layoutModuleBar- asi que nadie les preguntaba si el
            //  dibujo cabe en las TRES: dos con dibujo y una sin el no se lee
            //  como «aqui no cabia», se lee como una tapa a la que le falta
            //  algo. Es el mismo caso que obligo a la llamada explicita en
            //  AJUSTES. Despues de colocarlas, que `reparteTapa` mide sobre los
            //  limites que acaban de ponerse.
            {
                juce::TextButton* tb[3] = { &seqGridBtn, &seqPianoBtn, &seqStepBtn };
                filaDeIconos (tb, 3);
            }
            inner.removeFromTop (Metrics::sm);
        }

        //  Where each control's own name gets painted. paintSeqSheetContent
        //  used to reconstruct these bands from the control's bounds, which
        //  meant the name of a HIDDEN control was still drawn - four ghost
        //  captions floating over the grid the moment the card grew a second
        //  page. Now the layout records the band it reserved and paint draws
        //  only the ones that were reserved this pass.
        seqLabelBands.clear();
        seqFootArea = {};
        auto nameBand = [this, nameH] (juce::Rectangle<int>& col, const char* key)
        {
            auto b = col.removeFromTop (nameH);
            seqLabelBands.add ({ b, juce::String (key) });
            return b;
        };

        //  APAGADAS Y SIN SITIO, ANTES DE DECIDIR NADA.
        //
        //  Esto tiene que correr SIEMPRE, y el primer intento lo metio dentro
        //  del if (onGrid) - donde en la pagina PASO no corre nunca, asi que
        //  las cuatro tapas se quedaban con las coordenadas de la ultima vez
        //  que se maqueto PASOS: encima de los ocho botones de compas, 128
        //  solapes en las 476 corridas. Y ocultar no basta, hay que quitarles
        //  el sitio: un componente invisible que conserva sus limites sigue
        //  estando ahi para todo lo que mida geometria.
        for (auto* b : seqBankButtons) { b->setVisible (false); b->setBounds ({}); }

        //  Y LAS TRES DEL PATRON, POR LO MISMO Y CON MAS MOTIVO.
        //
        //  Viven en la pagina PASO. Al volver a PASOS nadie las escondia y
        //  nadie les quitaba el sitio, asi que se quedaban con las
        //  coordenadas de la ultima vez que se maqueto PASO: dibujadas encima
        //  de la fila de COMPAS, tapando sus botones y comiendose sus toques.
        //  Es el mismo fallo que el parrafo de aqui arriba describe para las
        //  tapas de banco, repetido tres lineas mas abajo.
        //
        //  El banco no lo cazo porque abre UNA ficha por arranque: con
        //  ZATI_OPEN=sec la pagina PASO no se maqueta nunca y las tres siguen
        //  en 0x0, que no solapa con nada. Hace falta cambiar de pagina dentro
        //  del mismo proceso, que es lo que hace la persona y lo que ahora
        //  hace ZATI_PAGES.
        for (auto* b : { &patLeftBtn, &patRightBtn, &patDoubleBtn,
                         &copyRowBtn, &pasteRowBtn })
        {
            b->setVisible (false);
            b->setBounds ({});
        }

        //  LA PAGINA DEL PIANO. Cabecera con PAD - / PAD +, la rejilla de tono
        //  con todo lo que sobre, y una fila de tapas debajo.
        if (onPiano)
        {
            {
                //  DOS TAPAS Y NO TRES: la que abre la rejilla de dieciseis
                //  vive en la cabecera de la FICHA, que es donde puede tener su
                //  dedo entero. Ver el parrafo de pianoPadPickBtn mas arriba.
                juce::TextButton* pn[2] = { &pianoPadDownBtn, &pianoPadUpBtn };
                //  EL SITIO SE PIDE MIDIENDO EL ROTULO, no a sextos de la fila.
                //
                //  A sextos, en 344x882 a las dos tapas les tocaban 43 px y
                //  "PAD +" pide 37 con sus margenes: quedaban 25 y el rotulo se
                //  cortaba. En chino y en arabe, peor. Se crece hasta que cabe,
                //  con el tope en dos tercios de la fila para que al titulo le
                //  quede algo - drawFittedText encoge, pero un titulo de cero
                //  no se lee.
                int anchoPad = titleRow.getWidth() / 6;
                const int tope = titleRow.getWidth() * 2 / 3;
                while (anchoPad * 2 < tope && ! moduleBarFits (anchoPad * 2, pn, 2))
                    anchoPad += 4;
                anchoPad = juce::jmax (Metrics::hit, anchoPad);
                //  vInset CERO: la cabecera mide justo 40, que es el suelo del
                //  dedo, y dos pixeles por lado eran regalar los cuatro que
                //  faltaban.
                layoutModuleBar (Lang::takeEnd (titleRow, anchoPad * 2), pn, 0, 2);
            }
            inner.removeFromTop (Metrics::bandaSubtitulo);                 // pintado: que se esta mirando

            //  APAISADO LAS CINCO TAPAS SE VAN A SU COLUMNA, que es la misma
            //  regla que ya usa la pagina de la rejilla y por el mismo motivo:
            //  girado sobra ancho y falta alto, y una fila de tapas cuesta 48
            //  px de lo unico que aqui escasea. Medido en 915x412: con la fila
            //  abajo la rejilla se queda en 184 px y la fila de una nota en
            //  14.2, por debajo del suelo; con la columna al lado son 232 y
            //  17.8. Las cinco caben apiladas -5 x 40 mas cuatro huecos son
            //  216- y a la rejilla le sobra ancho de todas formas: 35 px por
            //  columna donde solo hacen falta veinte.
            //
            //  Y se pregunta si caben ANTES de apilarlas. En 915x412 la columna
            //  tiene 232 y sobran dieciseis, pero "apaisado" es cualquier
            //  ventana ancha: en una mas baja removeFromTop reparte lo que hay
            //  y las ultimas tapas salen de alto cero sin que nadie se queje.
            //  Un tope que se supera en silencio no protege, esconde - ya paso
            //  con layoutModuleBar y sus ocho tapas.
            //  DOS OCTAVAS SOLO DONDE SE PUEDEN TOCAR. Veinticinco filas son
            //  19.3 px en un movil grande -se lee, se acierta- y 12 en un
            //  360x640, por debajo del suelo. Una eleccion que la persona toma
            //  a sabiendas es distinta de un defecto que se cuela, pero ofrecer
            //  una opcion que deja la rejilla rota no es ofrecer nada: donde no
            //  cabe, la tapa no esta - y si venia puesta del fichero de
            //  preferencias, la rejilla vuelve a una octava.
            //
            //  Se pregunta con las seis tapas puestas, que es el caso peor: si
            //  la respuesta es que no, la tapa se va y sobra sitio, no falta.
            {
                const int filasT = wideFace ? 0 : filasTapasPiano;
                const int alto = inner.getHeight() - filasT * Metrics::hit
                               - juce::jmax (0, filasT - 1) * Metrics::halfGap
                               - (filasT > 0 ? Metrics::sm : 0);
                const bool cabe = alto / PianoRoll::kFilasMax >= kSueloNota;
                pianoVerBtn.setVisible (cabe);
                if (! cabe)
                {
                    pianoVerBtn.setBounds ({});
                    if (pianoGrid.getFilas() != PianoRoll::kFilasMin)
                    {
                        pianoGrid.setFilas (PianoRoll::kFilasMin);
                        pianoBase = juce::jlimit (-24, pianoGrid.baseMax(), pianoBase);
                    }
                }
            }

            //  LA BARRA VERTICAL DEL TONO, o el par de tapas de OCTAVA.
            //
            //  Un salto de doce semitonos deja una melodia que cruza un DO
            //  partida entre dos vistas y no hay forma de centrarla: eso es
            //  «cambiar de pantalla todo el rato» por el otro eje. La barra
            //  recorre `pianoBase` semitono a semitono, y cuesta `Metrics::hit`
            //  de ANCHO de la rejilla — que es honesto: ese ancho lo paga quien
            //  pidio celdas mas grandes.
            //
            //  Donde ese ancho deja la columna del paso por debajo de su suelo
            //  se queda el par de tapas. Cada mando en UN sitio en cada
            //  pantalla, nunca en dos y nunca en ninguno, que es la regla de
            //  REPETIR y CORTE en la tira del paso.
            {
                const int libre = inner.getWidth() - PianoRoll::kGutter - BarraVista::kGrueso;
                pianoHayBarraVert = libre / juce::jmax (1, pianoCols) >= Metrics::celdaPaso;
            }
            pianoOctDownBtn.setVisible (! pianoHayBarraVert);
            pianoOctUpBtn  .setVisible (! pianoHayBarraVert);
            if (pianoHayBarraVert)
            {
                pianoOctDownBtn.setBounds ({});
                pianoOctUpBtn  .setBounds ({});
            }

            //  Y la columna se mide con las tapas que HAY, no con seis: cuando
            //  VER no esta, seis por cuarenta piden 260 px y la tarjeta
            //  apaisada tiene 232, asi que la pregunta salia que no, las tapas
            //  volvian a la fila de abajo y la rejilla perdia los 48 px que
            //  esta columna existe para devolverle - de 17.8 px por nota a
            //  14.2, por debajo del suelo. Preguntar por un mueble que no se va
            //  a colocar es la version de maquetado de reservar y tirar.
            //  Y SI LAS SIETE NO CABEN EN LA COLUMNA, SE CAE VER.
            //
            //  Apaisado las tapas van en una columna al lado y no en una fila
            //  debajo, y ese sitio no se pide en `wanted`: si la columna no las
            //  admite, se van al fondo y se comen 92 px de lo unico que
            //  escasea. Medido al meter LAPIZ: la fila de nota paso de 17.8 px
            //  a 14.2, por debajo del suelo de 16. VER es la prescindible -ya
            //  es la primera que se cae cuando dos octavas no se pueden tocar-
            //  asi que se cae tambien aqui.
            //  Y DONDE UNA COLUMNA NO LAS ADMITE, DOS.
            //
            //  Meter LAPIZ hizo siete tapas donde habia seis, y una columna de
            //  seis pide 284 px de alto que apaisado no hay: se caian a la fila
            //  del fondo y se comian 49 px de lo unico que escasea girado - la
            //  fila de nota paso de 17.8 px a 14.2, por debajo del suelo de 16.
            //
            //  Dos columnas y no que se caiga una tapa, porque apaisado lo que
            //  sobra es ANCHO: es la misma regla que puso la columna aqui en
            //  primer lugar. Con la mitad, seis piden 140 px de alto.
            //  Y LAS CUATRO NUEVAS TAMBIEN VAN A LA COLUMNA. Sin ellas aqui
            //  salian visibles y de 0x0 apaisado -48 hallazgos del banco-,
            //  porque esta es la unica lista que las coloca girado.
            juce::TextButton* pbCol[13] = { &seqModoBtn, &seqPlayBtn,
                                            &pianoOctDownBtn, &pianoOctUpBtn, &pianoVerBtn,
                                            &pianoZoomBtn,
                                            &pianoClearBtn, &pianoLapizBtn, &pianoGomaBtn,
                                            &pianoCorteBtn, &pianoSelBtn,
                                            &pianoCopiaBtn, &pianoPegaBtn };
            const auto altoDe = [] (int n) { return n * Metrics::hit + (n - 1) * Metrics::halfGap; };
            int nVisibles = 0;
            for (auto* b2 : pbCol) if (b2->isVisible()) ++nVisibles;

            const int porColumna = (wideFace && inner.getHeight() < altoDe (nVisibles))
                                     ? (nVisibles + 1) / 2 : nVisibles;
            const int columnas   = (porColumna > 0 && porColumna < nVisibles) ? 2 : 1;
            //  Las dos columnas caben en el ancho de UNA: partir la altura no
            //  puede costar el doble de ancho, que es de lo que vive la rejilla
            //  girada. Una tapa queda en 110 px y "OCTAVA -" pide 90 con su aire.
            const int anchoCol   = columnas > 1 ? (sideCol - Metrics::gap) / 2 : sideCol;
            const int anchoLado  = anchoCol * columnas + (columnas - 1) * Metrics::gap;

            if (wideFace && inner.getHeight() >= altoDe (porColumna)
                         && inner.getWidth() > anchoLado + Metrics::hit * 4)
            {
                auto side = Lang::takeEnd (inner, anchoLado);
                Lang::takeEnd (inner, Metrics::gap);
                juce::Rectangle<int> col = Lang::takeStart (side, anchoCol);
                int puestas = 0;
                //  Y LAS DOS COLUMNAS SON UN PANEL Y NO DOS. Entre ellas hay
                //  `Metrics::gap` y `panelAireX` vale la mitad de eso a cada
                //  lado, asi que dos paneles saldrian TOCANDOSE — que se lee
                //  igual que no dibujar ninguno. Es el intento fallido que esta
                //  casa ya escribio dos veces: los tres de la tira del paso y
                //  el par MONITOR/CUENTA de AJUSTES. Uno, deducido de lo que se
                //  acaba de colocar, asi que no cuesta un pixel.
                juce::Rectangle<int> grupo;
                for (int i = 0; i < 13; ++i)
                {
                    if (! pbCol[i]->isVisible()) continue;
                    if (puestas == porColumna)
                    {
                        Lang::takeStart (side, Metrics::gap);
                        col = Lang::takeStart (side, anchoCol);
                        puestas = 0;
                    }
                    auto celda = col.removeFromTop (Metrics::hit);
                    pbCol[i]->setBounds (celda.reduced (Metrics::aireTapa, 0));
                    grupo = grupo.isEmpty() ? celda : grupo.getUnion (celda);
                    col.removeFromTop (Metrics::xs);
                    ++puestas;
                }
                if (! grupo.isEmpty()) pianoGrupos.add (grupo);
            }
            else
            {
                //  La fila de tapas se aparta ANTES: es lo que no puede encoger.
                auto tapas = inner.removeFromBottom (Metrics::hit);
                inner.removeFromBottom (Metrics::sm);

                //  TRES y no cuatro: PLAY es del transporte y ya esta en la
                //  pagina de la rejilla, con TAP y VACIAR. Una tapa que hace lo
                //  mismo en dos paginas de la MISMA ficha es la version pequena
                //  del fallo que se acaba de quitar.
                //  CINCO tapas, y si no caben en una fila, dos: OCTAVA -/+ y
                //  VACIAR arriba, las dos herramientas debajo. En 280 px cinco
                //  a lo ancho dejan "TIJERAS" en 31 de los 48 que pide.
                //  Y CON LA SELECCION PUESTA, DOS TAPAS MAS: COPIAR y PEGAR.
                //
                //  Solo con seleccion, que es la regla de la tira del paso -un
                //  control que no puede hacer nada no es informacion, es ruido-
                //  y ademas es lo unico que hace que quepan: esta fila ya sale
                //  de nueve y se parte en dos en media pantalla. PEGAR ademas
                //  pide portapapeles: pegar lo que no se ha copiado no es nada.
                const bool haySel = ! pianoSel.empty();
                const bool hayPeg = ! pianoPortapapeles.empty();
                pianoCopiaBtn.setVisible (haySel);
                pianoPegaBtn .setVisible (hayPeg);
                if (! haySel) pianoCopiaBtn.setBounds ({});
                if (! hayPeg) pianoPegaBtn .setBounds ({});

                juce::TextButton* pbTodas[12];
                int nb = 0;
                pbTodas[nb++] = &seqModoBtn;
                pbTodas[nb++] = &seqPlayBtn;
                if (pianoOctDownBtn.isVisible()) pbTodas[nb++] = &pianoOctDownBtn;
                if (pianoOctUpBtn  .isVisible()) pbTodas[nb++] = &pianoOctUpBtn;
                if (pianoVerBtn.isVisible()) pbTodas[nb++] = &pianoVerBtn;
                pbTodas[nb++] = &pianoZoomBtn;
                pbTodas[nb++] = &pianoClearBtn;
                pbTodas[nb++] = &pianoLapizBtn;
                pbTodas[nb++] = &pianoGomaBtn;
                pbTodas[nb++] = &pianoCorteBtn;
                pbTodas[nb++] = &pianoSelBtn;
                if (haySel) pbTodas[nb++] = &pianoCopiaBtn;
                if (hayPeg && nb < 12) pbTodas[nb++] = &pianoPegaBtn;
                juce::TextButton** pb = pbTodas;
                if (moduleBarFits (tapas.getWidth(), pb, nb))
                {
                    layoutModuleBar (tapas, pb, 0, nb);
                    pianoGrupos.add (tapas);
                }
                else
                {
                    //  Tres y tres, y en ese orden: arriba lo que mueve la
                    //  VISTA -las dos octavas y cuantas se ven- y abajo lo que
                    //  toca las NOTAS. Antes eran tres y dos partidas por donde
                    //  cayera, con VACIAR arriba entre dos flechas de vista.
                    //  EL CORTE SE BUSCA, no se escribe. Con el corte fijo en
                    //  nb-3 -que valia con siete- nueve dejaban seis tapas
                    //  arriba; a mitades, cinco, y en 344x882 en arabe
                    //  "أوكتاف -" pide 69 px de letra y tenia 53. Se prueba
                    //  desde el reparto equilibrado hacia abajo y se coge el
                    //  primero en el que caben LAS DOS filas: preguntar por una
                    //  sola es como una fila corta empuja a la otra.
                    int arriba = (nb + 1) / 2;
                    for (int k = arriba; k >= 2; --k)
                        if (moduleBarFits (tapas.getWidth(), pb, k)
                            && moduleBarFits (tapas.getWidth(), pb + k, nb - k))
                        { arriba = k; break; }
                    layoutModuleBar (tapas, pb, 0, arriba);
                    auto fila2 = inner.removeFromBottom (Metrics::hit);
                    inner.removeFromBottom (Metrics::xs);
                    layoutModuleBar (fila2, pb + arriba, 0, nb - arriba);
                    //  DOS FILAS Y UN SOLO PANEL. Son dos preguntas -arriba lo
                    //  que mueve la VISTA, abajo lo que toca las NOTAS- y aun
                    //  asi no pueden ser dos paneles: entre las dos filas hay
                    //  `halfGap` y cada panel se sale `panelAireY` por lado, o
                    //  sea que quedarian pegados y eso se lee igual que no
                    //  dibujar ninguno. Separarlas con `Metrics::sm` costaria
                    //  cuatro pixeles de la unica cosa que en esta pagina no
                    //  sobra: la fila de nota. Es el mismo desenlace que la
                    //  tira del paso, que eran tres paneles tocandose y paso a
                    //  ser uno.
                    pianoGrupos.add (tapas.getUnion (fila2));
                }
            }

            //  Y LA VENTANA, QUE AQUI NO SE PODIA MOVER MAS QUE DE COMPAS EN
            //  COMPAS.
            //
            //  Eran las MISMAS tapas que la rejilla y por eso mismo saltaban de
            //  dieciseis en dieciseis: `base = selectedBar * 16`. Con
            //  `pianoCols` en 32 la ventana seguia arrancando en multiplo de
            //  dieciseis, o sea que la mitad de las posiciones posibles no
            //  existian. La barra es la MISMA de la pagina de la rejilla —una
            //  ventana, un dueño— y solo cambia cuantas columnas caben.
            //
            //  Y se cae antes que la fila de nota, que es lo que la guardia de
            //  «que queden tres dedos» no preguntaba: fallar de fila en esta
            //  rejilla no falla el toque, escribe otro tono y no lo dice nadie.
            const int costeBarras = BarraVista::kGrueso + Metrics::halfGap;
            const bool hayBarraH = engine.getPatternLength (selectedPattern) > pianoCols
                                && inner.getHeight() > Metrics::hit * 3
                                && (inner.getHeight() - costeBarras) / pianoGrid.getFilas()
                                       >= Metrics::celdaNota;
            seqBarra.setVisible (hayBarraH);
            if (hayBarraH)
            {
                seqBarra.setBounds (inner.removeFromBottom (BarraVista::kGrueso));
                inner.removeFromBottom (Metrics::xs);
            }
            else
            {
                seqBarra.setBounds ({});
            }

            pianoBarra.setVisible (pianoHayBarraVert);
            if (pianoHayBarraVert)
            {
                pianoBarra.setBounds (Lang::takeEnd (inner, BarraVista::kGrueso));
            }
            else
            {
                pianoBarra.setBounds ({});
            }

            pianoGrid.setBounds (inner);
        }
        else if (onGrid)
        {
            //  In landscape the four controls stand in their own column and the
            //  grid keeps the full height; in portrait they stack under it in
            //  the order the work happens.
            auto side = wideFace ? Lang::takeEnd (inner, sideCol) : juce::Rectangle<int>();
            if (wideFace) Lang::takeEnd (inner, Metrics::gap);

            auto& col = wideFace ? side : inner;

            //  PATRON and LARGO share one row upright, so they share one band -
            //  split in two, a name over each control. One caption stretched
            //  across both is how NOTA once came to look like part of CADENA.
            {
                auto band = col.removeFromTop (nameH);
                if (wideFace)
                    seqLabelBands.add ({ band, juce::String ("PATRON") });
                else
                {
                    auto half = Lang::takeStart (band, band.getWidth() / 2);
                    seqLabelBands.add ({ half, juce::String ("PATRON") });
                    seqLabelBands.add ({ band, juce::String ("LARGO")  });
                }
            }
            {
                auto row = col.removeFromTop (Metrics::hit);
                //  An IncDecButtons slider gives its two keys whatever the text
                //  box leaves, so a narrow box on a wide row turns them into a
                //  pair of slabs twice the size of anything else on the card.
                //  Reserve the box first and the keys come out finger-sized.
                //  Stacked in the side column, each takes the whole width.
                const int w1 = wideFace ? row.getWidth() : row.getWidth() / 2;
                patternSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false,
                                               juce::jmax (40, w1 - 2 * Metrics::gap - 2 * Metrics::stepKey),
                                               Metrics::readout);
                if (wideFace)
                {
                    patternSlider.setBounds (row.reduced (Metrics::aireTapa, 0));
                    col.removeFromTop (Metrics::sm);
                    nameBand (col, "LARGO");
                    lengthSlider.setBounds (col.removeFromTop (Metrics::hit).reduced (Metrics::aireTapa, 0));
                }
                else
                {
                    patternSlider.setBounds (row.removeFromLeft (w1).reduced (Metrics::aireTapa, 0));
                    lengthSlider.setBounds  (row.reduced (Metrics::aireTapa, 0));
                }
            }
            col.removeFromTop (Metrics::sm);

            //  APAISADO: EL TEMPO SE APARTA ANTES QUE NADA OPCIONAL.
            //
            //  En la columna lateral las filas se van quitando de arriba y el
            //  TEMPO se quitaba del fondo AL FINAL, o sea de lo que hubiera
            //  sobrado. Medido en 915x412: el deslizador de BPM y la tapa TAP
            //  quedaban en NUEVE pixeles de alto. Un control de nueve pixeles
            //  no esta apretado, no existe. Se aparta primero, y las filas
            //  opcionales se preguntan si caben en lo que queda.
            juce::Rectangle<int> tempoRes;
            if (wideFace)
                tempoRes = col.removeFromBottom (Metrics::hit + nameH + Metrics::sm);

            //  QUE CABE Y QUE NO, con la cuenta de verdad y hecha UNA vez.
            //
            //  Se resta lo que las lineas de mas abajo van a llevarse, una por
            //  una y CON SU ROTULO: la reserva decia hit*2 + sm*2 y se dejaba
            //  fuera los dos rotulos - COMPAS y TEMPO, 17 px cada uno - asi
            //  que creia tener 34 px mas de los que iba a tener.
            const int tempoCost = Metrics::hit + nameH + Metrics::sm;
            const int barsCost  = seqHayBarra ? costeBarra : 0;
            //  Y LA TIRA DEL PASO TAMBIEN CUENTA AQUI.
            //
            //  Se aparta del fondo mas abajo, asi que a esta altura col aun la
            //  incluye: sin restarla, las dos filas prescindibles creian tener
            //  126 px que no eran suyos y se quedaban las dos. Medido en
            //  412x915 con un paso tocado: la celda de paso bajaba a 13 px -
            //  el suelo es 12- teniendo sitio de sobra para 21 si BANCO y PADS
            //  se caian, que es justo para lo que existe esa pregunta.
            const int tiraCost  = tiraFilas * (nameH + Metrics::hit)
                                + juce::jmax (0, tiraFilas - 1) * Metrics::halfGap
                                + (tiraFilas > 0 ? Metrics::sm : 0);
            const int lanesH    = juce::jmax (0, col.getHeight() - tempoCost - barsCost - tiraCost);
            const int rowCost   = nameH + Metrics::hit + Metrics::sm;
            //  Las dos filas prescindibles se deciden en orden y contando la
            //  una a la otra: primero la de COPIAR/PEGAR, que es la que menos
            //  falta hace aqui -las dos tapas estan tambien en PASO- y despues
            //  la de bancos de pads.
            //  De pie la pregunta es "¿la celda de paso sigue por encima del
            //  suelo?", porque estas filas salen de la rejilla. Girado la
            //  rejilla esta al lado y no pierde nada: la pregunta es si la
            //  COLUMNA tiene sitio, contando lo que aun le queda por poner.
            //  Y CON LA TIRA PUESTA, EL LISTON SUBE.
            //
            //  Doce es el suelo de la celda, no un objetivo. Con la tira del
            //  paso debajo, la pagina hace ya dos trabajos - escribir el patron
            //  y editar el paso - y los atajos son el tercero y el que menos
            //  falta hace: A B C D estan en la cara y COPIAR/PEGAR en la pagina
            //  PASO. Medido en 412x915 con un paso tocado: con las dos filas
            //  puestas la celda queda en 12.9 px, y sin ellas en 21. Se pide
            //  dieciseis para dejarlas, que es lo que separa "cabe" de "se ve".
            const int sueloCelda = tiraFilas > 0 ? 16 : kMinLaneH;
            const bool copyRowFits = wideFace
                ? (col.getHeight() - barsCost - tiraCost >= rowCost)
                : ((lanesH - rowCost) / kPadsPerBank >= sueloCelda);
            const bool bankRowFits = wideFace
                ? (col.getHeight() - barsCost - tiraCost - (copyRowFits ? rowCost : 0) >= rowCost)
                : ((lanesH - (copyRowFits ? rowCost : 0) - rowCost) / kPadsPerBank >= sueloCelda);

            //  COPIAR Y PEGAR EL BANCO, pero solo donde sobra sitio.
            //
            //  Esta fila cuesta 65 px y salen enteros de la rejilla. Medido
            //  con el banco: en 280x653 y en 360x640 con un patron de dos
            //  compases -que ademas gasta la fila de COMPAS- la celda de paso
            //  bajaba a NUEVE y a OCHO pixeles de alto con el suelo puesto en
            //  doce, porque el suelo estaba escrito con jlimit y jlimit clampa
            //  TAMBIEN HACIA ARRIBA: pedia doce donde habia ocho y medio, la
            //  tarjeta se pasaba del tope y la diferencia la pagaba lo ultimo
            //  que se maqueta, que es justo la rejilla.
            //
            //  Donde no cabe no se pierde la funcion: las dos tapas estan
            //  tambien en la pagina PASO, con las otras tres que actuan sobre
            //  el patron entero. Es la misma regla que ya seguia la fila de
            //  bancos de pads, y por la misma razon.
            copyPatBtn.setVisible  (copyRowFits);
            pastePatBtn.setVisible (copyRowFits);
            if (copyRowFits)
            {
                nameBand (col, "BANCO");
                auto row = col.removeFromTop (Metrics::hit);
                copyPatBtn.setBounds  (Lang::takeStart (row, row.getWidth() / 2).reduced (Metrics::aireTapa, 0));
                pastePatBtn.setBounds (row.reduced (Metrics::aireTapa, 0));
                col.removeFromTop (Metrics::sm);
            }
            else
            {
                //  Y sin coordenadas, que un componente invisible que conserva
                //  sus limites sigue estando ahi para todo lo que mida
                //  geometria. Ver la fila de bancos de pads.
                copyPatBtn.setBounds ({});
                pastePatBtn.setBounds ({});
            }

            //  El banco de pads, JUSTO ENCIMA de la rejilla que va a cambiar -
            //  la relacion entre las dos tiene que ser obvia sin leer nada -
            //  PERO SOLO SI CABE SIN ENCOGERLA.
            //
            //  Esta fila cuesta 66 px y salen enteros de la rejilla, que en las
            //  dos pantallas mas estrechas ya esta en el suelo: medido, la
            //  celda pasaba de 12 px de alto a OCHO en 280x653 y en 360x640, y
            //  doce ya era la tercera parte de un dedo. Una comodidad que
            //  encoge lo unico para lo que existe la ficha no es una comodidad.
            //
            //  Donde no cabe, las tapas A B C D de la cara siguen estando: se
            //  pierde el atajo, no la funcion. Y donde cabe - que son cinco de
            //  las siete pantallas del banco - se gana escribir un bombo del
            //  banco A y un bajo del D sin cerrar nada.

            //  Y SIN COORDENADAS CUANDO NO SE DIBUJAN.
            //
            //  Ocultarlas no basta: el banco mide SOLAPES sobre los limites que
            //  quedan puestos, y en la pagina PASO estas cuatro se quedaban
            //  encima de los ocho botones de compas - 128 solapes en las 476
            //  corridas, y el primer fallo duro de toda la sesion. Un
            //  componente que no se ve pero conserva su sitio sigue estando ahi
            //  para todo lo que mire geometria.
            const bool showBank = bankRowFits && onGrid;
            if (showBank)
            {
                for (auto* b : seqBankButtons) b->setVisible (true);
                nameBand (col, "PADS");
                auto row = col.removeFromTop (Metrics::hit);

                const int bw = row.getWidth() / kNumBanks;
                for (int b = 0; b < seqBankButtons.size(); ++b)
                    seqBankButtons[b]->setBounds ((b < kNumBanks - 1 ? row.removeFromLeft (bw) : row)
                                                      .reduced (Metrics::aireTapa, 0));
                col.removeFromTop (Metrics::sm);
            }

            //  TEMPO last, at the foot of whichever container it is in: it is
            //  the one number on this page that belongs to the machine rather
            //  than to the pattern.
            {
                auto& fuente = wideFace ? tempoRes : col;
                auto row = fuente.removeFromBottom (Metrics::hit);
                //  Cuatro en la fila del tempo: el deslizador, TAP a su lado
                //  porque marcar y ver el numero es el mismo gesto, y luego
                //  VACIAR. Copiar y pegar van encima, con el patron.
                //  CINCO en la fila del tempo desde que el transporte esta
                //  aqui: PLAY primero, que es lo que mas se toca mientras se
                //  escribe un patron, y el resto igual que estaba.
                //  El deslizador se queda con la mitad y las TRES tapas se
                //  reparten la otra POR EL TEXTO QUE LLEVAN. A quintos iguales
                //  -que fue el primer intento al meter PLAY- VACIAR pedia 45 px
                //  y tenia 31 en 280x653: PLAY es corto y VACIAR no, y repartir
                //  a partes iguales le da lo mismo a los dos.
                //  El deslizador cede ancho hasta que las tres tapas caben.
                //  A la mitad justa -que fue el segundo intento- VACIAR pedia
                //  45 px y tenia 31 en 280x653: el numero del tempo se lee
                //  igual en un tercio de fila, y un rotulo cortado no.
                //  Y SEGUIR con ellas, que era una tapa MUERTA: showSeqPage la
                //  encendia con la pagina de la rejilla y nadie le daba nunca
                //  unas coordenadas, asi que llevaba desde que existe visible y
                //  de 0x0 - imposible de tocar y contando como control en todo
                //  lo que mide geometria. Entra donde las cuatro caben; donde
                //  no, se apaga Y se le vacian los limites, que es lo que hay
                //  que hacer con las dos cosas a la vez.
                //  Y EL MODO CON ELLAS, a la izquierda de PLAY: la fila del
                //  transporte de esta ficha contesta lo mismo que la de la
                //  cara. Entra por la misma escalera que SEGUIR - donde no
                //  quepa, se apaga Y se le vacian los limites.
                juce::TextButton* tb5[5] = { &seqModoBtn, &seqPlayBtn, &tapButton,
                                             &clearButton, &seqFollowBtn };
                juce::TextButton* tb4[4] = { &seqModoBtn, &seqPlayBtn, &tapButton, &clearButton };
                juce::TextButton* tb3[3] = { &seqPlayBtn, &tapButton, &clearButton };
                int paraTapas = row.getWidth() / 2;
                //  Y con sitio para el dedo, no solo para la letra: moduleBarFits
                //  mide el TEXTO, y cuatro rotulos cortos caben de sobra en una
                //  fila donde a cada tapa le tocan 35 px.
                const bool cabeSeguir = paraTapas >= 5 * Metrics::hit
                                          && moduleBarFits (paraTapas, tb5, 5);
                const bool cabeModo = cabeSeguir
                                    || (paraTapas >= 4 * Metrics::hit
                                        && moduleBarFits (paraTapas, tb4, 4));
                if (! cabeModo && ! moduleBarFits (paraTapas, tb3, 3))
                    paraTapas = row.getWidth() * 2 / 3;
                seqFollowBtn.setVisible (cabeSeguir);
                if (! cabeSeguir) seqFollowBtn.setBounds ({});
                seqModoBtn.setVisible (cabeModo);
                if (! cabeModo) seqModoBtn.setBounds ({});

                //  Y EL ROTULO DEL TEMPO SE QUEDA EN EL NUMERO cuando la fila
                //  se estrecha: "120 bpm" pide 52 px y en 280 la casilla se
                //  queda en 33. El numero solo se lee igual - la fila lleva su
                //  nombre encima, TEMPO, asi que la unidad la dice el rotulo -
                //  y un rotulo cortado no se lee de ninguna manera.
                auto celdaBpm = Lang::takeStart (row, row.getWidth() - paraTapas)
                                    .reduced (Metrics::aireTapa, 0);
                bpmSlider.setTextValueSuffix (celdaBpm.getWidth() >= 150 ? " bpm" : juce::String());
                bpmSlider.setBounds (celdaBpm);
                if      (cabeSeguir) layoutModuleBar (row, tb5, 0, 5);
                else if (cabeModo)   layoutModuleBar (row, tb4, 0, 4);
                else                 layoutModuleBar (row, tb3, 0, 3);
                seqLabelBands.add ({ fuente.removeFromBottom (nameH), juce::String ("TEMPO") });
                fuente.removeFromBottom (Metrics::sm);
            }

            //  LA TIRA DEL PASO, debajo de la rejilla y encima del tempo.
            if (tiraFilas > 0)
            {
                //  Se aparta del FONDO de la rejilla: lo que no puede encoger
                //  se reserva primero, y los carriles se reparten lo que queda.
                auto tira = inner.removeFromBottom (
                                tiraFilas * (nameH + Metrics::hit)
                              + (tiraFilas - 1) * Metrics::halfGap);
                inner.removeFromBottom (Metrics::sm);

                //  showSeqPage las apaga al entrar en PASOS y solo resized
                //  puede encenderlas, que es el unico que sabe si caben. Misma
                //  regla que las tapas de banco.
                auto fila = [&] (juce::Slider& izq, const char* nIzq,
                                 juce::Slider& der, const char* nDer)
                {
                    auto banda = tira.removeFromTop (nameH);
                    auto mitad = Lang::takeStart (banda, banda.getWidth() / 2);
                    //  Grupo 1: la tira entera es un panel. Ver SeqLabel.
                    seqLabelBands.add ({ mitad, juce::String (nIzq), 1, 1 });
                    seqLabelBands.add ({ banda, juce::String (nDer), 1, 1 });

                    auto row = tira.removeFromTop (Metrics::hit);
                    auto celdaIzq = Lang::takeStart (row, row.getWidth() / 2).reduced (Metrics::aireTapa, 0);
                    auto celdaDer = row.reduced (Metrics::aireTapa, 0);
                    //  Las dos teclas primero y el numero con lo que quede: sin
                    //  esta reserva JUCE apila el + sobre el - en cuanto la
                    //  casilla se come el ancho. Es la misma cuenta que GOLPE.
                    auto caja = [] (juce::Slider& sl, juce::Rectangle<int> celda)
                    {
                        if (sl.getSliderStyle() == juce::Slider::IncDecButtons)
                            sl.setTextBoxStyle (juce::Slider::TextBoxLeft, false,
                                                juce::jmax (34, celda.getWidth()
                                                                - Metrics::gap - 2 * Metrics::stepKey),
                                                Metrics::readout);
                    };
                    caja (izq, celdaIzq);
                    caja (der, celdaDer);
                    izq.setBounds (celdaIzq);
                    der.setBounds (celdaDer);
                    izq.setVisible (true);
                    der.setVisible (true);
                };

                fila (noteSlider, "NOTA", velSlider, "GOLPE");
                if (tiraFilas > 1)
                {
                    tira.removeFromTop (Metrics::xs);
                    fila (rollSlider, "REPETIR", lockSlider, "CORTE");
                }
                if (tiraFilas > 2)
                {
                    tira.removeFromTop (Metrics::xs);
                    //  CUATRO en la misma fila y no dos: ver la declaracion de
                    //  atkPasoSlider. Una fila mas costaria otros 58 px y en un
                    //  telefono la rejilla no los tiene.
                    auto banda = tira.removeFromTop (nameH);
                    auto row   = tira.removeFromTop (Metrics::hit);
                    juce::Slider* cuatro[4] = { &atkPasoSlider, &relPasoSlider,
                                                &iniPasoSlider, &panPasoSlider };
                    const char*   nombres[4] = { "ATAQUE", "CAIDA", "INICIO", "PAN" };
                    for (int i = 0; i < 4; ++i)
                    {
                        //  Lo que queda dividido por lo que queda, y no el ancho
                        //  entre cuatro: en arabe takeStart muerde por el otro
                        //  lado y repartir por el total deja la ultima celda
                        //  fuera por el redondeo.
                        const int ancho = banda.getWidth() / (4 - i);
                        seqLabelBands.add ({ Lang::takeStart (banda, ancho), juce::String (nombres[i]), 1, 1 });
                        auto celda = Lang::takeStart (row, row.getWidth() / (4 - i))
                                         .reduced (Metrics::aireTapa, 0);
                        //  El mando primero y el numero con lo que quede, que es
                        //  la misma cuenta que ya hacen GOLPE y REPETIR: un
                        //  giratorio por debajo de 24 px no se agarra, y donde
                        //  no quepan los dos el numero se va - dice OFF o los
                        //  milisegundos, y las dos cosas caben en el rotulo.
                        const int paraNum = celda.getWidth() - Metrics::hit;
                        cuatro[i]->setTextBoxStyle (paraNum >= 34 ? juce::Slider::TextBoxRight
                                                                  : juce::Slider::NoTextBox,
                                                    false, juce::jmax (34, paraNum), Metrics::readout);
                        cuatro[i]->setBounds (celda);
                        cuatro[i]->setVisible (true);
                    }
                }
            }

            //  Y las que no se colocan, sin sitio: un componente invisible que
            //  conserva sus limites sigue estando ahi para todo lo que mida
            //  geometria. Es el fallo de las tapas de banco, otra vez.
            {
                juce::Slider* tiraMandos[4] = { &noteSlider, &velSlider, &rollSlider, &lockSlider };
                const int puestos = juce::jmin (2, tiraFilas) * 2;
                for (int i = puestos; i < 4; ++i)
                {
                    tiraMandos[i]->setVisible (false);
                    tiraMandos[i]->setBounds ({});
                }
                //  Y los cuatro bloqueos, que van juntos o no van: un
                //  componente invisible que conserva sus limites sigue estando
                //  ahi para todo lo que mida geometria - es el fallo de las
                //  tapas de banco, contado por tercera vez.
                if (tiraFilas < 3)
                    for (auto* sl : { &atkPasoSlider, &relPasoSlider, &iniPasoSlider, &panPasoSlider })
                    {
                        sl->setVisible (false);
                        sl->setBounds ({});
                    }
            }

            //  LA BARRA, DEBAJO DE LA REJILLA y encima de la tira: es lo que
            //  desplaza la ventana, asi que va pegada a lo que desplaza. Se
            //  aparta del fondo DESPUES de la tira, que es la que se decidio
            //  arriba con `costeBarra` dentro.
            seqBarra.setVisible (seqHayBarra);
            if (seqHayBarra)
            {
                seqBarra.setBounds (inner.removeFromBottom (Metrics::hit));
                inner.removeFromBottom (Metrics::xs);
            }
            else
            {
                //  Apagar Y vaciar los limites, las dos cosas.
                seqBarra.setBounds ({});
            }

            stepGrid.setBounds (inner);
        }
        else
        {
            //  Las tres del patron vuelven, que PASOS las apaga al salir - y
            //  las dos de COPIAR/PEGAR tambien, que alli su fila es
            //  prescindible y puede haberlas dejado apagadas.
            for (auto* b : { &patLeftBtn, &patRightBtn, &patDoubleBtn,
                             &copyPatBtn, &pastePatBtn, &copyRowBtn, &pasteRowBtn })
                b->setVisible (true);

            //  The foot line first, so no column can lay a control over it.
            seqFootArea = inner.removeFromBottom (kSeqFootH);
            inner.removeFromBottom (Metrics::sm);

            //  Two columns rotated, one stacked upright - the same four groups
            //  either way, so the card never has to be taller than it is wide.
            auto colA = wideFace ? inner.removeFromLeft ((inner.getWidth() - Metrics::gap) / 2) : inner;
            auto colB = wideFace ? inner.withTrimmedLeft (Metrics::gap) : juce::Rectangle<int>();
            auto& second = wideFace ? colB : colA;

            if (seqCadenaAqui)
            {
                nameBand (colA, "CADENA");
                {
                    //  Y EN DOS FILAS DE CUATRO DONDE OCHO NO CABEN A DEDO, la
                    //  misma cuenta que la paleta de CANCION y el selector del
                    //  rack: ocho por cuarenta son 320 px y la columna de un
                    //  movil estrecho mide 217, asi que a cada tapa le tocaban
                    //  24. No hay reparto que arregle eso; hay que doblar.
                    //  Con el aire de la tapa dentro: se reducen dos por lado.
                    const int porFila = (colA.getWidth() / kNumPatterns - 4 >= Metrics::hit) ? kNumPatterns : 4;
                    for (int f = 0; f * porFila < kNumPatterns; ++f)
                    {
                        auto row = colA.removeFromTop (Metrics::hit);
                        const int pw = row.getWidth() / porFila;
                        for (int c2 = 0; c2 < porFila; ++c2)
                        {
                            const int i2 = f * porFila + c2;
                            //  Aire SOLO a los lados: la fila mide Metrics::hit
                            //  -el minimo- y quitarle dos por arriba y dos por
                            //  abajo dejaba las ocho tapas en 36.
                            patternButtons[i2]->setBounds ((c2 < porFila - 1 ? row.removeFromLeft (pw) : row)
                                                               .reduced (Metrics::aireTapa, 0));
                            patternButtons[i2]->setVisible (true);
                        }
                        if ((f + 1) * porFila < kNumPatterns) colA.removeFromTop (Metrics::halfGap);
                    }
                    colA.removeFromTop (Metrics::xs);
                }

                //  QUITAR CADENA es de la CADENA y no del paso: compartia
                //  renglon con NOTA porque los dos cabian en una linea, y eso
                //  costo que la pagina entera pareciera "el paso". Ahora va con
                //  su grupo, que es lo que es, y NOTA solo aparece si la tira de
                //  la rejilla no se la ha llevado.
                chainClearButton.setBounds (colA.removeFromTop (Metrics::hit).reduced (Metrics::aireTapa, 0));
                chainClearButton.setVisible (true);
                colA.removeFromTop (Metrics::sm);
            }
            else
            {
                //  Y con los limites vaciados, no solo apagadas: un componente
                //  invisible que conserva sus coordenadas sigue estando ahi para
                //  todo lo que mida geometria. Es el fallo de las tapas de banco.
                for (int i2 = 0; i2 < kNumPatterns; ++i2)
                {
                    patternButtons[i2]->setVisible (false);
                    patternButtons[i2]->setBounds ({});
                }
                chainClearButton.setVisible (false);
                chainClearButton.setBounds ({});
            }

            if (pasoAqui && tiraFilas == 0)
            {
                nameBand (colA, "NOTA DEL PASO");
                auto row = colA.removeFromTop (Metrics::hit);
                noteSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false,
                                            juce::jmax (40, row.getWidth() - 2 * Metrics::gap - 2 * Metrics::stepKey),
                                            Metrics::readout);
                noteSlider.setBounds (row.reduced (Metrics::aireTapa, 0));
                noteSlider.setVisible (true);
                colA.removeFromTop (Metrics::sm);
            }

            //  EL PATRON ENTERO: desplazarlo y doblarlo. Ver patLeftBtn - van
            //  aqui y no en PASOS porque alli la fila saldria entera de la
            //  rejilla, que ya esta en su suelo de 12 px por carril en las dos
            //  pantallas mas estrechas.
            //
            //  Las dos flechas son cuadradas y DOBLAR se queda con el resto:
            //  una flecha no tiene texto que ensanchar, y repartir a tercios
            //  daba dos tapas enormes con un simbolo diminuto dentro.
            nameBand (colA, "PATRON");
            const int idxPatron = seqLabelBands.size() - 1;
            {
                //  Repartidas POR EL TEXTO QUE LLEVAN, con la misma barra que
                //  usan los modulos: a tercios, ADELANTE se cortaba en ingles
                //  y en arabe mientras ATRAS dejaba media tapa vacia.
                //  CINCO, no tres: COPIAR y PEGAR el banco tambien actuan
                //  sobre el patron entero, y en la pagina de la rejilla su
                //  fila es prescindible - se cae cuando la celda de paso
                //  bajaria del suelo -. Una funcion que solo vive en una fila
                //  prescindible es una funcion que en las pantallas estrechas
                //  no existe.
                //
                //  Y sobre todo: aqui se les da SITIO. Ponerlas visibles en
                //  esta pagina sin colocarlas las dejaba con las coordenadas
                //  de PASOS, encima de la fila CADENA - ocho solapes, uno por
                //  cada tapa de patron, que es el fallo que trajo esta ronda.
                //  SEIS desde que HUMANIZAR vive aqui. Y la lista tiene que
                //  ser LA MISMA que la que decidio la altura unas lineas mas
                //  arriba: la primera version dejo esta en cinco y la de la
                //  altura en seis, asi que HUMANIZAR no se colocaba nunca y se
                //  quedaba en 0x0 - existente, invisible e imposible de tocar.
                juce::TextButton* pb[8] = { &patLeftBtn, &patRightBtn, &patDoubleBtn,
                                            &seqHumanBtn, &copyPatBtn, &pastePatBtn,
                                            &copyRowBtn, &pasteRowBtn };
                //  La MISMA pregunta que decidio la altura, con el mismo ancho.
                const int nFilas = moduleBarFits (colA.getWidth(), pb, 8) ? 1
                                 : (moduleBarFits (colA.getWidth(), pb, 4)
                                    && moduleBarFits (colA.getWidth(), pb + 4, 4)) ? 2 : 3;
                //  Y el panel del grupo tiene que saberlo: sin esto se pintaba
                //  detras de la primera fila y las de abajo quedaban fuera de
                //  su propio grupo.
                if (juce::isPositiveAndBelow (idxPatron, seqLabelBands.size()))
                    seqLabelBands.getReference (idxPatron).filas = nFilas;

                if (nFilas == 1)
                {
                    layoutModuleBar (colA.removeFromTop (Metrics::hit), pb, 0, 8);
                }
                else if (nFilas == 2)
                {
                    layoutModuleBar (colA.removeFromTop (Metrics::hit), pb, 0, 4);
                    colA.removeFromTop (Metrics::xs);
                    layoutModuleBar (colA.removeFromTop (Metrics::hit), pb + 4, 0, 4);
                }
                else
                {
                    layoutModuleBar (colA.removeFromTop (Metrics::hit), pb, 0, 3);
                    colA.removeFromTop (Metrics::xs);
                    layoutModuleBar (colA.removeFromTop (Metrics::hit), pb + 3, 0, 3);
                    colA.removeFromTop (Metrics::xs);
                    layoutModuleBar (colA.removeFromTop (Metrics::hit), pb + 6, 0, 2);
                }
                colA.removeFromTop (Metrics::sm);
            }

            //  What the step DOES: how hard, and how many times.
            //
            //  Y SOLO LO QUE LA TIRA NO SE HAYA LLEVADO. Con la tira de dos
            //  filas aqui no queda nada del paso; con la de una, GOLPE ya esta
            //  arriba y aqui quedan REPETIR y CORTE. Repetir el mando en las
            //  dos paginas era el fallo, no la solucion.
            if (pasoAqui && tiraFilas < 2)
            {
                nameBand (second, tiraFilas == 0 ? "GOLPE" : "REPETIR");
                //  TRES celdas y no dos: el bloqueo del corte es del PASO, y
                //  este es el sitio donde vive todo lo que es del paso. Va
                //  aqui y no en una banda propia porque una banda cuesta 65 px
                //  y esta pagina ya se pasa del tope en la pantalla mas
                //  estrecha - una funcion que solo cabe en pantallas grandes
                //  no existe en las pequenas.
                auto row = second.removeFromTop (Metrics::hit);
                if (tiraFilas == 0)
                {
                    velSlider.setBounds (Lang::takeStart (row, row.getWidth() * 5 / 12).reduced (Metrics::aireTapa, 0));
                    velSlider.setVisible (true);
                }
                auto rollCell = Lang::takeStart (row, tiraFilas == 0 ? row.getWidth() * 4 / 12
                                                                     : row.getWidth() / 2).reduced (Metrics::aireTapa, 0);
                //  Las dos teclas primero y el numero con lo que quede; y si
                //  no queda, el numero se va. Medido en 280x653 con la celda
                //  repartida a tres: las teclas se llevaban 80 de 75 px y el
                //  numero quedaba en DOS - "1" pedia 8. Un numero de dos
                //  pixeles no es un numero apretado, es una raya.
                const int paraNum = rollCell.getWidth() - Metrics::gap - 2 * Metrics::stepKey;
                rollSlider.setTextBoxStyle (paraNum >= 30 ? juce::Slider::TextBoxLeft
                                                          : juce::Slider::NoTextBox,
                                            false, juce::jmax (30, paraNum), Metrics::readout);
                rollSlider.setBounds (rollCell);
                lockSlider.setBounds (row.reduced (Metrics::aireTapa, 0));
                rollSlider.setVisible (true);
                lockSlider.setVisible (true);
                second.removeFromTop (Metrics::sm);
            }

            //  Y las que la tira SI se llevo, sin sitio aqui: un componente
            //  invisible que conserva sus limites sigue estando ahi para todo
            //  lo que mida geometria. Ver las tapas de banco.
            {
                juce::Slider* delPaso[4] = { &noteSlider, &velSlider, &rollSlider, &lockSlider };
                //  Se apagan las que se llevo la TIRA - las primeras - y las
                //  cuatro cuando no hay paso tocado. Al reves, que fue el
                //  primer intento, apagaba justo las que se acababan de
                //  colocar aqui.
                const int hasta = pasoAqui ? juce::jmin (2, tiraFilas) * 2 : 4;
                for (int i = 0; i < hasta; ++i)
                {
                    delPaso[i]->setVisible (false);
                    delPaso[i]->setBounds ({});
                }
            }

            //  Y LOS CUATRO BLOQUEOS, cuando la tira no llego a la tercera
            //  fila. Misma regla que REPETIR y CORTE: lo que la tira no se
            //  lleva vive aqui, y cada mando aparece en UN sitio en cada
            //  pantalla. Sin esto los cuatro solo existirian donde caben tres
            //  filas, o sea en ninguna pantalla estrecha - "una funcion que
            //  solo cabe en pantallas grandes no existe en las pequenas".
            if (seqLocksAqui)
            {
                nameBand (second, "BLOQUEOS");
                auto row = second.removeFromTop (Metrics::hit);
                juce::Slider* cuatro[4] = { &atkPasoSlider, &relPasoSlider,
                                            &iniPasoSlider, &panPasoSlider };
                for (int i = 0; i < 4; ++i)
                {
                    auto celda = Lang::takeStart (row, row.getWidth() / (4 - i))
                                     .reduced (Metrics::aireTapa, 0);
                    const int paraNum = celda.getWidth() - Metrics::hit;
                    cuatro[i]->setTextBoxStyle (paraNum >= 34 ? juce::Slider::TextBoxRight
                                                              : juce::Slider::NoTextBox,
                                                false, juce::jmax (34, paraNum), Metrics::readout);
                    cuatro[i]->setBounds (celda);
                    cuatro[i]->setVisible (true);
                }
                second.removeFromTop (Metrics::sm);
            }
            else
            {
                for (auto* sl : { &atkPasoSlider, &relPasoSlider, &iniPasoSlider, &panPasoSlider })
                {
                    sl->setVisible (false);
                    sl->setBounds ({});
                }
            }

            nameBand (second, "SWING");
            swingSlider.setBounds (second.removeFromTop (Metrics::hit).reduced (Metrics::aireTapa, 0));
            second.removeFromTop (Metrics::sm);

            //  EUCLIDES, con el swing y la rejilla: los tres dicen COMO suena
            //  el patron entero, no que hay escrito en el.
            nameBand (second, "EUCLIDES");
            {
                auto cell = second.removeFromTop (Metrics::hit).reduced (Metrics::aireTapa, 0);
                euclidSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false,
                                              juce::jmax (40, cell.getWidth() - Metrics::gap - 2 * Metrics::stepKey),
                                              Metrics::readout);
                euclidSlider.setBounds (cell);
            }
            second.removeFromTop (Metrics::sm);

            nameBand (second, "REJILLA");
            {
                auto cell = second.removeFromTop (Metrics::hit).reduced (Metrics::aireTapa, 0);
                //  Las dos teclas primero, el numero con lo que quede: es la
                //  misma reserva que CHOKE y que GOLPE, y por la misma razon -
                //  sin ella JUCE apila el + sobre el - en cuanto la casilla se
                //  come el ancho, y quedan dos rendijas de 17 px.
                gridSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false,
                                            juce::jmax (40, cell.getWidth() - Metrics::gap - 2 * Metrics::stepKey),
                                            Metrics::readout);
                gridSlider.setBounds (cell);
            }
        }
    }
    //  EL MUELLE DEL TOUR, y va AL FINAL de resized() a proposito: su sitio
    //  depende de donde haya quedado el control que el paso senala, y eso solo
    //  se sabe cuando el resto ya se ha maquetado. Colocarlo antes seria
    //  colocarlo con las coordenadas de la vuelta anterior.
    {
        tourSheet.setBounds (getLocalBounds());
        tourFoco = tourObjetivo (tourPaso);
        //  Ver UiAudit::tourPaso: que "este paso no señala nada" sea un numero.
        UiAudit::tourPaso  = tourPaso;
        UiAudit::tourFocoW = tourFoco.getWidth();
        UiAudit::tourFocoH = tourFoco.getHeight();
        //  Y lo que dicen las dos salidas: en el paso de la PUERTA la tercera
        //  tapa deja de decir SALTAR y ofrece seguir con los once que quedan.
        UiAudit::tourSig     = tourNextBtn.getButtonText().toStdString();
        UiAudit::tourTercera = tourSkipBtn.getButtonText().toStdString();
        //  Y en que pagina quedo la mesa, que es lo unico que separa «el
        //  paso 11 abre la mesa» de «la abre donde lo explica».
        UiAudit::tourMixPag  = (mixPage == mixPageCanales ? 1 : 0);

        const int anchoDock = getWidth();
        const int alto = Metrics::md * 2 + 16 + Metrics::xs
                       + tourBodyHeight (anchoDock - 2 * Metrics::margenFichaX)
                       + Metrics::sm + Metrics::hit;

        //  EN LA MITAD CONTRARIA A LA DEL OBJETIVO. Es lo unico que garantiza
        //  que el texto no tape lo que se esta senalando sin tener que negociar
        //  posiciones - que es donde el proyecto anterior se dejo dos redisenos.
        //  Y DENTRO DEL AREA SEGURA, no de la ventana. El velo si cubre la
        //  ventana entera -es lo que oscurece la maquina- pero la tarjeta lleva
        //  texto, y arriba del todo estan la hora y la senal: en un movil con
        //  barra de estado el titulo del paso salia DEBAJO del reloj, ilegible
        //  y pareciendo un fallo de pintado. Abajo es lo mismo con la barra de
        //  gestos. La cara ya se maqueta asi desde el principio; el muelle se
        //  escribio con getLocalBounds y se quedo fuera de esa regla.
        auto ventana = safeArea();
        const bool objetivoArriba = tourFoco.isEmpty()
                                  || tourFoco.getCentreY() < ventana.getCentreY();
        //  Y ACOTADO A LA MITAD LARGA DE LA VENTANA. El alto sale de medir el
        //  parrafo mas largo, que es lo correcto, pero medir no es lo mismo que
        //  caber: con la letra a catorce y un idioma que escriba mas, un muelle
        //  sin tope taparia la maquina que esta senalando - y el paso entero
        //  consiste en que se vea.
        const int tope = ventana.getHeight() * 55 / 100;
        const int altoDock = juce::jmin (alto, tope);
        tourDock = objetivoArriba ? ventana.removeFromBottom (altoDock)
                                  : ventana.removeFromTop (altoDock);
        //  Y si aun asi se solapan -un objetivo que ocupa media pantalla-, manda
        //  el texto: sin leerlo el foco no explica nada.
        auto inner = tourDock.reduced (Metrics::margenFichaX, Metrics::margenFichaY);
        inner.removeFromTop (Metrics::bandaTitulo + Metrics::xs);      // pintado: titulo y puntos
        {
            auto row = inner.removeFromBottom (Metrics::hit);
            juce::TextButton* tb[3] = { &tourSkipBtn, &tourBackBtn, &tourNextBtn };
            layoutModuleBar (row, tb, 0, 3);
            inner.removeFromBottom (Metrics::sm);
        }
        tourBodyArea = inner;
    }

}
