#include "MainComponentInterno.h"

// ==========================================================================
//  LO QUE SE DIBUJA.
//
//  El fondo de la cara y el contenido de cada ficha. Es la otra mitad de la
//  pregunta que contesta el maquetado y no la misma: `resized()` dice DONDE va
//  cada cosa y esto dice COMO se ve, y ninguna llama a la otra.
//
//  Y aqui viven los rotulos que se PINTAN y no son componentes -media pantalla
//  de esta app-, que es por lo que hicieron falta `UiAudit::rotulo` y
//  `antesDe`: las reglas del banco recorren el arbol de COMPONENTES y un
//  titulo dibujado no esta en el.
// ==========================================================================

void MainComponent::paint (juce::Graphics& g)
{
    //  LA OTRA MITAD DE LO QUE CUESTA UN CUADRO. `pintaCuadro` alimenta y
    //  marca; quien PINTA es esto, y las dos corren en el hilo de mensajes.
    //  Medir solo la primera diria que un fotograma cuesta cero, que es
    //  exactamente el numero con el que la defensa del vblank no saltaria
    //  jamas. Con RAII porque esta funcion tiene salidas por varios sitios.
    struct Cronometro
    {
        double& donde;
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        ~Cronometro() { donde += juce::Time::getMillisecondCounterHiRes() - t0; }
    } cronometro { cuadroGastoMs };

    //  El contador del banco. Ver UiAudit::fondosPintados: esta funcion solo
    //  corre cuando hay que repintar ventana entera, asi que contarla aqui
    //  cuenta fotogramas completos sin instrumentar nada mas.
    ++UiAudit::fondosPintados;
    {
        const auto c = g.getClipBounds();
        UiAudit::pixelesPintados += (long long) c.getWidth() * (long long) c.getHeight();
    }

    //  La cara es la capa 0, y se pinta antes que las fichas. Ver
    //  UiAudit::capaActual.
    UiAudit::capaActual = 0;
    UiAudit::origenPintado = { 0, 0 };

    auto full = getLocalBounds().toFloat();

    pintaFondo (g);

    //  1b. Structure. A white field with rows of caps on it is a list of
    //  buttons; an instrument has plates, seams and engraved lettering, and
    //  all three are drawn with lines rather than shaded. These cost no
    //  layout height at all - the seams live in gaps that already existed and
    //  the plate is drawn behind controls that were already positioned.
    auto rule = [&g] (float x1, float x2, float y, float alpha)
    {
        g.setColour (ZatiColours::ink.withAlpha (alpha));
        g.fillRect (x1, y, x2 - x1, 1.0f);
        g.setColour (ZatiColours::white.withAlpha (0.9f));      // the engraved highlight
        g.fillRect (x1, y + 1.0f, x2 - x1, 1.0f);
    };

    //  A label that rides its seam, the way silkscreen does on hardware: the
    //  line breaks for the word instead of running behind it.
    //
    //  Centred, with the rule coming in from BOTH edges to meet it. Hung off
    //  the left it read as a caption sitting on top of a line; brought to the
    //  middle with the line arriving from either side it reads as one piece of
    //  lettering that the seam was engraved around - which is what it is, and
    //  what the three of them together are supposed to say about the face.
    //  ...and centred in the seam, not hung from the top of the section below
    //  it. Placing the word a fixed six pixels over the zone put every bit of
    //  the seam's slack ABOVE the lettering and none under it, so the label
    //  read as glued to the plate beneath rather than as sitting in its own
    //  band. It takes the two edges of the gap and puts itself in the middle
    //  of them, so the air is the same above and below whatever the seam is
    //  worth on this screen.
    //  The rule runs the width of the ZONE the name belongs to, which is the
    //  whole face in portrait and one of the two columns when the window is
    //  wider than it is tall - a rule for the pads that crossed the screen and
    //  the knobs on its way there would be naming all three.
    auto engraveIn = [&g, &full, &rule] (const juce::String& text, int seamTop, int zoneTop,
                                         juce::Rectangle<int> span = {})
    {
        const auto s = span.isEmpty() ? full : span.toFloat();

        auto engrave = [&g, &rule, &s] (const juce::String& t, float y)
        {
        g.setFont (ZatiColours::labelFont (Metrics::fMeta, 0.30f));
        const float tw  = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), t);
        const float cx  = s.getCentreX();
        const float x0  = cx - tw * 0.5f;
        const float gap = 9.0f;                       // air the rule leaves around the word

        rule (s.getX() + 10.0f, x0 - gap, y, 0.16f);
        rule (x0 + tw + gap, s.getRight() - 10.0f, y, 0.16f);

        g.setColour (ZatiColours::ink.withAlpha (0.42f));
        g.drawText (t, (int) x0 - 1, (int) (y - 5.0f), (int) tw + 3, 11,
                    juce::Justification::centred);
        };

        engrave (text, (float) (seamTop + zoneTop) * 0.5f);
    };

    // 2. The pad plate: the pads are bolted to a recessed panel, not floating
    //    on the face. One tone step down, one hairline, four screws.
    if (! padPlateArea.isEmpty())
    {
        //  The tonal step has to be big enough to see. At 3% the plate was
        //  technically there and read as a rendering artefact; the face needs
        //  three distinct values - chassis, plate, cap - or the whole thing
        //  stays white on white however many lines are drawn on it.
        auto pp = padPlateArea.toFloat();
        g.setColour (ZatiColours::plate);
        g.fillRoundedRectangle (pp, 4.0f);
        g.setColour (ZatiColours::plateEdge);
        g.drawRoundedRectangle (pp.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (ZatiColours::white.withAlpha (0.55f));      // lip catching the light
        g.drawRoundedRectangle (pp.reduced (1.6f), 4.0f, 1.0f);

    }

    //  The control plate: CTRL 1-3 and the six modules are one zone, and
    //  giving it its own plate is what turns the middle of the face from a
    //  white field with rows on it into a section of an instrument.
    if (! ctrlPlateArea.isEmpty())
    {
        auto cp = ctrlPlateArea.toFloat();
        g.setColour (ZatiColours::plate);
        g.fillRoundedRectangle (cp, 4.0f);
        g.setColour (ZatiColours::plateEdge);
        g.drawRoundedRectangle (cp.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (ZatiColours::white.withAlpha (0.55f));
        g.drawRoundedRectangle (cp.reduced (1.6f), 4.0f, 1.0f);

    }

    //  A plate, a name. EFECTOS used to be the only engraved word on the
    //  face, which made it look like a caption someone forgot to remove
    //  rather than like silkscreen: the other two plated zones - the knobs
    //  and the pads - had no title at all, and the eye reads one label among
    //  three unlabelled neighbours as an accident.
    //
    //  So the rule is now literal and it is the same for all three: every
    //  zone that sits on a PLATE gets its name engraved on the seam directly
    //  above it, left-aligned at the same inset, with the rule breaking for
    //  the word. Nothing that is not on a plate gets one - the LCD says what
    //  it is by being a screen, and the transport keys say it by being
    //  labelled LOAD, REC and PLAY. resized() reserves the seam height for
    //  these, so they can never land on the section above.
    if (! ctrlPlateArea.isEmpty())
        engraveIn (T ("CONTROL"), ctrlSeamTop, ctrlPlateArea.getY(), faceColumn);

    if (! fxRowArea.isEmpty())
        engraveIn (T ("EFECTOS"), fxSeamTop, fxRowArea.getY(), faceColumn);

    if (! padPlateArea.isEmpty())
    {
        //  ...and it stops where the bank chips start, on BOTH sides. A rule
        //  that runs under four controls is not naming a zone, it is crossing
        //  them out. With a pair at each end the word ends up centred in what
        //  is left between them, which is the middle of the seam - that is the
        //  symmetry, and it falls out of the geometry instead of being nudged.
        //  Compared by x and not by name: in Arabic the leading pair is the
        //  one on the right, and trimming "left by lead" would clip the wrong
        //  end and let the rule run straight through the chips.
        auto span = wideFace ? padPlateArea.expanded (ZatiLookAndFeel::kAir, 0)
                             : full.toNearestInt();
        const bool leadIsLeft = bankRowLeftArea.getX() <= bankRowRightArea.getX();
        const auto nearSide = leadIsLeft ? bankRowLeftArea  : bankRowRightArea;
        const auto farSide  = leadIsLeft ? bankRowRightArea : bankRowLeftArea;
        if (! nearSide.isEmpty())
            span.setLeft  (juce::jmax (span.getX(),     nearSide.getRight() + Metrics::gap));
        if (! farSide.isEmpty())
            span.setRight (juce::jmin (span.getRight(), farSide.getX()      - Metrics::gap));
        //  An empty span means "no span" to engraveIn, and it would answer by
        //  drawing the rule across the WHOLE face - straight through the chips
        //  it was just told to avoid. On a window too narrow for both pairs and
        //  a word, the word wins and the rule simply does not appear.
        if (span.getWidth() > 2)
            engraveIn (T ("PADS"), padSeamTop, padPlateArea.getY(), span);
    }

        //  Which of the six owns the three knobs. A tap both switches an
        //  effect and hands it the knobs, and until now only the switching
        //  showed - so with two effects on there was nothing on screen saying
        //  whose parameters CTRL 1-3 were holding. A wedge in the seam above
        //  the button, pointing from the knobs down at the effect they
        //  belong to.
        //  La cuna apunta a la RANURA donde vive el tipo enfocado, que ya no
        //  es su indice: con la fila en ranuras, `fxButtons[focusedFx]` es la
        //  tapa que ESTA en ese sitio y no la del efecto. Y si el tipo no esta
        //  puesto no hay a donde apuntar, asi que no se dibuja.
        if (const int sFoco = slotDeFx (focusedFx); sFoco >= 0)
            if (auto* fb = fxButtons[sFoco])
            {
                //  In the band BELOW the rule, which is now empty: the word
                //  moved to the middle of the seam and takes the rule's line
                //  with it, so the pixels between that line and the caps are
                //  free - and they are the right place for a pointer, because
                //  it is nearer the thing it points at than to the lettering.
                const float cx = (float) fb->getBounds().getCentreX();
                const float y  = (float) fb->getY() - 3.0f;
                juce::Path wedge;
                wedge.addTriangle (cx - 5.0f, y - 6.0f, cx + 5.0f, y - 6.0f, cx, y);
                g.setColour (ZatiColours::ink.withAlpha (0.75f));
                g.fillPath (wedge);
            }

    // 3. Recessed LCD bezel around the scope, with the screws that hold the
    //    window down. This is the one object on the face that should read as
    //    hardware rather than as a rectangle of dark paint.
    if (! screenBezel.isEmpty())
    {
        auto r = screenBezel.toFloat();
        //  A thinner bezel, now that nothing is bolted through it. The five
        //  pixels were there to give twelve screw heads somewhere to sit, and
        //  twelve screw heads on a phone are twelve dots of noise at the exact
        //  size where they stop reading as hardware and start reading as
        //  dirt. Three pixels of frame say the same thing and hand the other
        //  two back to the glass.
        g.setColour (ZatiColours::knobBody2);
        g.fillRoundedRectangle (r.expanded (3.0f), 3.0f);
        g.setColour (ZatiColours::knobEdge.withAlpha (0.7f));
        g.drawRoundedRectangle (r.expanded (3.0f).reduced (0.5f), 3.0f, 1.2f);
    }

    // 3. Header: the wordmark, the open project, and a printed colour band
    //    across the whole face.
    //
    //    The band is the only colour on the chassis, and it is not decoration:
    //    it is the state of the kit, one segment per zati, lit where a pad of
    //    that colour has a sound in it and nearly out where none does. It used
    //    to be eight nine-pixel squares hiding in the top right corner - the
    //    same information, but small enough that nobody would ever look at it,
    //    and cramped into a corner instead of belonging to the machine.
    //
    //    Across the width it reads the way a printed stripe on a piece of
    //    studio gear reads: it tells you what the box is before it tells you
    //    anything else. No touch targets here - a readout, not a control.
    if (! headerArea.isEmpty())
    {
        auto h = headerArea;
        g.setColour (ZatiColours::ink);

        //  LA MARCA, que es un PAD con la Z cortada dentro - ver Iconos::marca.
        //  El nombre estaba solo, con la primera palabra de la cara siendo la
        //  unica cosa de la maquina sin dibujo. Y la Z es un HUECO y no un
        //  trazo encima: asi la marca funciona en las cuatro carcasas con un
        //  solo color, que es la misma regla que gobierna los iconos.
        const int ladoMarca = juce::jlimit (18, 26, h.getHeight() - 4);
        {
            auto m = Iconos::marca();
            const auto caja = juce::Rectangle<float> ((float) h.getX(),
                                                      (float) h.getCentreY() - (float) ladoMarca * 0.5f,
                                                      (float) ladoMarca, (float) ladoMarca);
            m.applyTransform (juce::AffineTransform::scale ((float) ladoMarca / 24.0f)
                                  .translated (caja.getX(), caja.getY()));
            g.fillPath (m);
        }

        const auto fuenteTitulo = ZatiColours::displayFont (Metrics::fTitle).withExtraKerningFactor (0.16f);
        const int xTitulo = h.getX() + ladoMarca + Metrics::sm;
        auto anchoDe = [&fuenteTitulo] (const char* t)
        {
            return (int) std::ceil (juce::GlyphArrangement::getStringWidth (fuenteTitulo, t));
        };

        //  EL NOMBRE ENTERO, Y LA ESCALERA DE SIEMPRE SI NO CABE.
        //
        //  La cabecera decia "ZATI" a secas con el argumento de que ahi es la
        //  marca serigrafiada en el chasis y el nombre completo se comeria la
        //  fila. Lo segundo era la parte medible, y medido no era verdad: la
        //  palabra corta mide 47 px y la larga 152, y las siete pantallas
        //  tienen sitio para las dos - la mas estrecha que nadie fabrica,
        //  280x653, deja 172 y sobran veinte.
        //
        //  Donde no quepa se cae a la corta, que es la misma pregunta que ya
        //  deciden BANCO, PADS, la tira del paso y las seis pestanas, hecha
        //  con el TEXTO puesto y no con el ancho de la ventana. Y se ha visto
        //  caer, que es lo que separa una escalera de una linea que imprime
        //  OK: el punto exacto es una ventana de 260 px -W - 108 >= 152- y
        //  medido, a 240 sale "ZATI".
        //
        //  El minimo que hay que dejarle a la linea del proyecto es el suyo,
        //  el mismo 40 con el que se apaga ocho lineas mas abajo: sin contarlo
        //  aqui, el nombre largo cabria empujando fuera lo unico de esta banda
        //  que DICE algo que cambia, y esa no es una eleccion que pueda tomar
        //  el rotulo que siempre pone lo mismo. Lo que si cuesta el nombre
        //  largo es que en 280x653 a esa linea le quedan 60 px en vez de 160
        //  y se elide - "SIN GUARD..." -, que es para lo que esa linea lleva
        //  la elipsis puesta desde que existe: un nombre de proyecto no tiene
        //  largo con el que contar y cortado a media letra se lee como un
        //  fallo, con puntos suspensivos como un nombre largo.
        const int paraNombre = h.getRight() - xTitulo - Metrics::md - 40;
        const char* nombre = anchoDe ("ZATI SAMPLER") <= paraNombre ? "ZATI SAMPLER" : "ZATI";

        //  Y EL ANCHO DEL NOMBRE SE MIDE, no se supone. Aqui habia un 140 y
        //  ocho lineas mas abajo un 138 para lo que iba detras: dos numeros a
        //  mano para el mismo borde, escritos a ojo con la fuente de aquel dia
        //  - y ahora ademas con una marca delante que los mueve.
        const int wTitulo = anchoDe (nombre);
        g.setFont (fuenteTitulo);
        //  APUNTADO, que es lo que no estaba. Esta banda se dibuja con
        //  drawText a pelo: no es un componente, asi que las seis reglas de
        //  expo.py no la ven -solo miran filas con `path`- y no pasaba por
        //  pintaTitulo, asi que tampoco salia en el volcado de rotulos. El
        //  nombre de la app era lo primero que se lee de la maquina y lo unico
        //  que ninguna prueba miraba: si se cortase, no fallaria, se
        //  publicaria.
        //
        //  Se llama a UiAudit::rotulo directo y no via pintaTitulo porque esa
        //  puerta dibuja con Lang::start() y con elipsis, y esta banda no hace
        //  ni lo uno ni lo otro: una marca no se refleja en arabe -el dibujo
        //  del pad y la palabra son una sola cosa y van juntos a la izquierda-
        //  y no se elide, se cae a la palabra corta.
        const auto cajaTitulo = juce::Rectangle<int> (xTitulo, h.getY(), wTitulo + 2, h.getHeight());
        UiAudit::rotulo (cajaTitulo, nombre, "titulo");
        g.drawText (nombre, cajaTitulo, juce::Justification::centredLeft);

        rule ((float) h.getX(), (float) h.getRight(), (float) h.getBottom() + 2.0f, 0.22f);

        //  The band between the wordmark and the strip was empty across the
        //  whole width of the machine, while the one thing you cannot see
        //  anywhere on the face - which project is open - was buried three
        //  taps deep in PROJ. It goes here, on the baseline of the wordmark.
        {
            //  Hard to the far edge, not trailing after the wordmark.
            //
            //  Floating just to the right of ZATI it read as a subtitle - part
            //  of the logo, drifting to a different place with every project
            //  name. Pinned to the opposite end it becomes the other half of a
            //  header: the machine on one side, what is loaded in it on the
            //  other, both anchored. That is how a piece of gear labels
            //  itself, and it stops moving when the name changes.
            const int nameX = xTitulo + wTitulo + Metrics::md;
            const int nameW = h.getRight() - nameX;
            if (nameW > 40)
            {
                //  EL RENGLON DE CONTINUIDAD. Aqui decia el nombre del
                //  proyecto y nada mas; ahora dice ademas cuanto trabajo hay
                //  dentro y de cuando es. Lo compone `lineaDeContinuidad`, que
                //  es quien sabe que campos caben — con el TEXTO puesto, no con
                //  el ancho de la ventana.
                const bool named = currentProject.isNotEmpty();
                const auto fuenteMeta = ZatiColours::monoFont (Metrics::fMeta, true)
                                            .withExtraKerningFactor (0.10f);
                const auto texto = lineaDeContinuidad (nameW, fuenteMeta);

                g.setColour (ZatiColours::ink.withAlpha (named ? 0.55f : 0.28f));
                g.setFont (fuenteMeta);

                //  APUNTADO, que es lo que no estaba. Esta linea se dibuja con
                //  `drawText` a pelo igual que su vecina de arriba: no es un
                //  componente, asi que ninguna de las once reglas de `expo.py`
                //  la ve, y hasta hoy tampoco salia en el volcado de rotulos
                //  que leen `plano.py` y `planos.py`. Es la unica de esta banda
                //  que dice algo que CAMBIA — y acaba de pasar de una palabra a
                //  tres campos, o sea exactamente el caso en que eso importa:
                //  si se metiera debajo de algo no fallaria, se publicaria.
                const auto cajaProy = juce::Rectangle<int> (nameX, h.getY(), nameW, h.getHeight());
                UiAudit::rotulo (cajaProy, texto, "proyecto");
                g.drawText (texto, cajaProy, juce::Justification::bottomRight, true);
            }
        }

        //  The band itself: eight segments, edge to edge, sitting on the rule.
        {
            const float x0 = (float) h.getX();
            const float w  = (float) h.getWidth() / (float) Zati::kNumColours;
            const float y0 = (float) h.getBottom() + 4.0f;
            const float bh = 4.0f;

            for (int i = 0; i < Zati::kNumColours; ++i)
            {
                bool used = false;
                for (int p = 0; p < kNumPads && ! used; ++p)
                    used = padHasSample[(size_t) p] && padZati[(size_t) p] == i;

                //  Lit or nearly out - never absent. A gap in the stripe would
                //  read as a printing fault; a dim segment reads as a colour
                //  you have not used yet.
                //  Even unused it has to READ as a printed stripe. At a fifth
                //  it was a smudge you would take for a rendering artefact;
                //  the difference between used and not is still obvious at
                //  these two values, and the machine keeps its colour whether
                //  you have loaded anything or not.
                g.setColour (Zati::colour (i).withAlpha (used ? 1.0f : 0.45f));
                g.fillRect (x0 + (float) i * w, y0, w - 1.0f, bh);
            }
        }
    }

    // 4. Machine face: CTRL labels (bank-dependent), VU strip, step LEDs.
    {
        g.setColour (ZatiColours::ink.withAlpha (0.85f));
        g.setFont (ZatiColours::labelFont (Metrics::fMeta, 0.16f));
        juce::Slider* ks[3] = { &macroCtrl1, &macroCtrl2, &macroCtrl3 };
        for (int i = 0; i < 3; ++i)
        {
            auto r = ks[i]->getBounds();
            //  Con un efecto que trae su propia cara los tres mandos no se
            //  maquetan, y sus rotulos son PINTADOS: sin esto se dibujarian en
            //  0,0 -encima de la cabecera- y la novena regla del banco los
            //  cazaria como rotulo tapado. Un rotulo no puede sobrevivir al
            //  control que nombra.
            if (r.isEmpty()) continue;
            const bool touched = macroTouched[(size_t) i];

            // Label names, readout measures — never the other way round.
            g.setColour (touched ? ZatiColours::ink : ZatiColours::ink.withAlpha (0.55f));
            g.setFont (ZatiColours::labelFont (touched ? 10.5f : 10.0f, 0.16f));
            g.drawText (touched ? macroParamLabel (i) : macroBaseLabel (i),
                        r.getX() - 8, r.getY() - ZatiLookAndFeel::kCtrlName + ZatiLookAndFeel::kTextPad,
                        r.getWidth() + 16, ZatiLookAndFeel::kCtrlName - 2 * ZatiLookAndFeel::kTextPad,
                        juce::Justification::centred);

            auto chip = juce::Rectangle<int> (r.getX() - 2, r.getBottom() + 2, r.getWidth() + 4,
                                              ZatiLookAndFeel::kCtrlChip - 4);
            g.setColour (ZatiColours::screenBg);
            g.fillRoundedRectangle (chip.toFloat(), 2.0f);
            g.setColour (touched ? ZatiColours::lcdFg : ZatiColours::lcdFg.withAlpha (0.8f));
            g.setFont (ZatiColours::monoFont (Metrics::fLabel, true));
            g.drawText (macroReadout (i), chip, juce::Justification::centred);
        }

    }
}

//  LA PAGINA DE ASPECTO: el idioma y la carcasa. Nada mas, y por eso existe -
//  estaban colgando de AUDIO, entre el reloj y el bufer, que es lo unico de
//  esta ficha con lo que no tienen nada que ver.
void MainComponent::paintAspectoPage (juce::Graphics& g)
{
    if (setSheet.sheetBounds.isEmpty()) return;

    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.12f));
    if (! langRowArea.isEmpty())
        { auto r = langRowArea; pintaTitulo (g, Lang::takeStart (r, 44), T ("IDIOMA"), "seccion"); }
    if (! skinRowArea.isEmpty())
        { auto r = skinRowArea; pintaTitulo (g, Lang::takeStart (r, 44), T ("CARCASA"), "seccion"); }
    if (! movRowArea.isEmpty())
        { auto r = movRowArea; pintaTitulo (g, Lang::takeStart (r, 44), T ("MOVIMIENTO"), "seccion"); }
}

// The audio path, measured rather than assumed. Everything here comes from
// the device itself; nothing is a constant we hope is true.
void MainComponent::paintAudioInfo (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.isEmpty()) return;

    g.setColour (ZatiColours::screenBg);
    g.fillRoundedRectangle (area.toFloat(), 3.0f);
    auto inner = area.reduced (10, 7);

    auto* dev = deviceManager.getCurrentAudioDevice();

    g.setColour (ZatiColours::lcdDim);
    g.setFont (ZatiColours::labelFont (Metrics::fMeta, 0.20f));
    g.drawText (T ("AUDIO"), inner.removeFromTop (12), juce::Justification::centredLeft);

    //  What the app decided this phone can carry. It is not a setting, it is
    //  a report - and it is true whether or not a stream ever opened, so it
    //  goes above the part that needs one.
    {
        auto r = inner.removeFromTop (14);
        g.setColour (ZatiColours::lcdDim);
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
        g.drawText (T ("EQUIPO"), r.removeFromLeft (54), juce::Justification::centredLeft);
        g.setFont (ZatiColours::monoFont (Metrics::fValue, true));
        g.drawFittedText (DeviceTier::describe(), r, juce::Justification::centredLeft, 1, 0.7f);
    }

    if (dev == nullptr)
    {
        g.setColour (ZatiColours::red);
        g.setFont (ZatiColours::monoFont (Metrics::fValue, true));
        g.drawText (T ("sin dispositivo de audio"), inner, juce::Justification::centredLeft);
        return;
    }

    const double sr    = dev->getCurrentSampleRate();
    const int    block = dev->getCurrentBufferSizeSamples();
    const int    outL  = dev->getOutputLatencyInSamples();
    auto msOf = [sr] (double samples) { return sr > 0.0 ? samples * 1000.0 / sr : 0.0; };

    //  Oboe's figure is already the whole path from writing a block to the
    //  speaker moving, buffer included, so adding our block size to it was
    //  counting the same milliseconds twice.
    const double devMs   = msOf ((double) outL);
    const double blockMs = msOf ((double) block);
    const double totalMs = devMs > 0.0 ? devMs : blockMs;

    auto line = [&g, &inner] (const juce::String& k, const juce::String& v, juce::Colour c)
    {
        auto r = inner.removeFromTop (14);
        g.setColour (ZatiColours::lcdDim);
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
        g.drawText (k, r.removeFromLeft (54), juce::Justification::centredLeft);
        g.setColour (c);
        g.setFont (ZatiColours::monoFont (Metrics::fValue, true));
        //  Half of these values come from the OS - device names, granted
        //  stream terms - so none of them has a length we can plan around.
        g.drawFittedText (v, r, juce::Justification::centredLeft, 1, 0.7f);
    };

    line (T ("ruta"),  dev->getTypeName() + " / " + dev->getName(), ZatiColours::lcdFg);
    line (T ("reloj"), juce::String ((int) sr) + " Hz", ZatiColours::lcdFg);
    const auto sizes = dev->getAvailableBufferSizes();
    const int  burst  = sizes.isEmpty() ? block : sizes.getFirst();
    line (T ("bufer"), juce::String (block) + " · " + juce::String (msOf (block), 1) + " ms"
                     + (block <= burst ? "  (" + T ("rafaga, el minimo") + ")"
                                       : "  (" + T ("rafaga %1", juce::String (burst)) + ")"),
          ZatiColours::lcdFg);

    //  Under ~15 ms a pad feels like a pad. Past ~30 ms you hear yourself
    //  arrive late and you start compensating, which is when an instrument
    //  stops being one.
    const auto verdict = totalMs <= 15.0 ? ZatiColours::lcdFg
                       : totalMs <= 30.0 ? ZatiColours::yellow
                                         : ZatiColours::red;
    line (T ("salida"), juce::String (totalMs, 1) + " ms  "
                    + (totalMs <= 15.0 ? T ("rapida")
                     : totalMs <= 30.0 ? T ("aceptable") : T ("LENTA")), verdict);

    //  Say WHOSE milliseconds these are. Our share is the block; everything
    //  past it belongs to the phone's audio path, and no setting in this app
    //  can give it back. Without this split a bad phone reads as a bad app.
    g.setColour (ZatiColours::lcdDim.withAlpha (0.85f));
    g.setFont (ZatiColours::monoFont (Metrics::fFine, false));
    juce::String note = T ("de esos, %1 ms son el bufer", juce::String (blockMs, 1));
    if (totalMs - blockMs > 20.0)
    {
        //  Once the probe has told us we never got an MMAP stream, the leftover
        //  milliseconds have a name. Saying "el telefono" invited another week
        //  of looking for a setting; naming AudioFlinger closes the question.
        if (block > burst)
            note += " - " + T ("baja el bufer");
        else if (fastPath.ran && fastPath.mmapKnown && ! fastPath.mmapUsed)
            note += " - " + T ("el resto es el mezclador de Android, sin MMAP en este movil");
        else
            note += " - " + T ("el resto es el telefono, no lo pone nadie mas bajo");
    }
    g.drawFittedText (note, inner.removeFromTop (11), juce::Justification::centredLeft, 1, 0.7f);

    //  Whether the phone allows the fast lane at all. JUCE already asks Oboe
    //  for exclusive + low latency, so if the answer here is "no soportado"
    //  the remaining milliseconds are the device's and no build of this app
    //  will get them back.
    const auto policy = AudioPath::mmapPolicy();
    const auto excl   = AudioPath::exclusivePolicy();
    line (T ("mmap"), AudioPath::describe (policy) + " · " + T ("excl") + " " + AudioPath::describe (excl),
          policy == AudioPath::Mmap::Never || excl == AudioPath::Mmap::Never ? ZatiColours::red
        : policy == AudioPath::Mmap::Unknown ? ZatiColours::lcdDim
                                             : ZatiColours::lcdFg);

    //  ...and whether it granted it to US. "disponible" above is a capability;
    //  this line is the verdict on an actual stream, which is the only one
    //  that decides what the pads feel like.
    line (T ("via"), AudioPath::describe (fastPath),
          fastPath.exclusive ? ZatiColours::lcdFg
        : ! fastPath.ran     ? ZatiColours::lcdDim
        : fastPath.mmapUsed  ? ZatiColours::yellow   // shared, but still MMAP
                             : ZatiColours::red);    // AudioFlinger's mixer

    //  The measurement, kept visually apart from everything the device
    //  merely claims about itself.
    if (measuring)
        line (T ("medido"), T ("escuchando..."), ZatiColours::yellow);
    else if (measuredMs >= 0.0f)
        line (T ("medido"), T ("%1 ms ida y vuelta", juce::String (measuredMs, 1))
                              + (measuredRate > 0.0
                                   ? "  " + Lang::ltr (juce::String (measuredRate / 1000.0, 1) + "k")
                                   : juce::String()),
              measuredMs <= 30.0f ? ZatiColours::lcdFg
            : measuredMs <= 60.0f ? ZatiColours::yellow : ZatiColours::red);

    g.setColour (ZatiColours::lcdDim.withAlpha (0.85f));
    g.setFont (ZatiColours::monoFont (Metrics::fFine, false));
    g.drawFittedText (measureNote.isNotEmpty() ? measureNote
                                               : T ("MEDIR emite un click y lo escucha con el micro"),
                      inner.removeFromTop (11), juce::Justification::centredLeft, 1, 0.7f);
}

//  The AUDIO card. What the device is doing, and the language it says it in.
void MainComponent::paintAudioSheetContent (juce::Graphics& g)
{
    if (setSheet.sheetBounds.isEmpty()) return;

    //  Del CUERPO y no de la tarjeta: esta ficha se desplaza, asi que
    //  sus bandas viven en coordenadas del cuerpo - las mismas de las
    //  que sale el maquetado. Ver Sheet::hazDesplazable.
    auto inner = setSheet.cuerpo.getLocalBounds();
    //  El titulo lo pinta paintSetTitle para las cuatro paginas; aqui solo se
    //  salta su banda para que lo de debajo caiga donde el maquetado lo puso.
    inner.removeFromTop (16);

    paintAudioInfo (g, audioInfoArea);

    //  El nombre de la seccion de las tres acciones. Ver resized(): colgaban
    //  del renglon del TITULO, al lado de la x, y ahi se leian como parte de
    //  la cabecera de la ficha y no como lo que son - tres cosas que se le
    //  hacen al audio.
    if (! pruebasLabelArea.isEmpty())
    {
        g.setColour (ZatiColours::inkDim);
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.12f));
        pintaTitulo (g, pruebasLabelArea, T ("PRUEBAS"), "seccion");
    }

    //  Row names for the three chip rows. The asterisk marks the driver's own
    //  burst size: on Android that is the fast path, and anything below it
    //  buys nothing.
    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.12f));
    if (! bufRowArea.isEmpty())
        { auto r = bufRowArea;  pintaTitulo (g,  Lang::takeStart (r, 44), T ("BUFER"), "seccion"); }
        if (! cuentaRowArea.isEmpty())
        { auto r = cuentaRowArea; pintaTitulo (g, Lang::takeStart (r, 44), T ("CUENTA"), "seccion"); }
    if (! rateRowArea.isEmpty())
        { auto r = rateRowArea; pintaTitulo (g,  Lang::takeStart (r, 44), T ("RELOJ"), "seccion"); }
}

// Sheet header: which pad is being filled and what is currently picked.
void MainComponent::paintBrowseSheetContent (juce::Graphics& g)
{
    if (browseSheet.sheetBounds.isEmpty()) return;

    auto inner = browseSheet.sheetBounds.reduced (Metrics::lg, Metrics::md);
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    //  Y EL ENCABEZADO DICE A QUE SE HA ENTRADO. El mismo navegador sirve para
    //  dos cosas y el titulo se quedaba en "CARGAR EN PAD 1 - elige una muestra
    //  wav/aiff/flac/ogg/mp3" mientras se elegia la carpeta del rebote: dos
    //  lineas mintiendo en la unica ficha cuyo trabajo es no equivocarse de
    //  sitio. Ver ModoBrowse.
    const bool eligiendoCarpeta = (browseModo == browseCarpeta);
    //  Por pintaTitulo y no por drawText: asi el banco lo VE. Este titulo
    //  existia desde el primer dia y `Tests/plano.py` decia "browse no tiene
    //  titulo: se abre y no dice donde estas" - que es lo que se lee cuando un
    //  rotulo se dibuja a mano. La ficha que mas necesita decir a que has
    //  entrado es justo esta: el mismo navegador carga una muestra y elige la
    //  carpeta del rebote.
    pintaTitulo (g, antesDe (inner.removeFromTop (16), browseCloseButton),
                 eligiendoCarpeta
                   ? T ("CARPETA DE EXPORTAR")
                   : T ("CARGAR EN PAD %1", juce::String (juce::jmax (0, browseTargetPad) + 1)),
                 "titulo", true);

    //  LO ELEGIDO LO DICE `selectionChanged`, no un `stat` dentro de `paint`.
    //  Esto llamaba a `existsAsFile()` y a `getFileName()` sobre el fichero
    //  senalado en cada repintado del navegador, y esa pregunta ya la contesta
    //  -con el mismo `existsAsFile`- la funcion que enciende CARGAR. Un dueno.
    const bool picked = ! eligiendoCarpeta && browsePickName.isNotEmpty();
    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.08f));
    //  Same reason as the pad sheet: this is a file name, and the close button
    //  shares the band.
    auto browseSubRow = antesDe (inner.removeFromTop (14), browseCloseButton);
    juce::String sub;
    if (picked)                  sub = browsePickName;
    else if (eligiendoCarpeta)   sub = browser != nullptr
                                         ? T ("entra donde quieras y pulsa USAR ESTA CARPETA")
                                         : juce::String();
    else                         sub = T ("elige una muestra  -  wav / aiff / flac / ogg / mp3");
    //  APUNTADO: se recorta con `antesDe` y nadie lo comprobaba. Un rotulo que
    //  el banco no ve puede acabar debajo de la cruz sin que las mil corridas
    //  digan nada — es como la mesa estuvo titulada «MIX» a mano durante meses.
    apunta (g, browseSubRow, sub, "dato");
    g.drawText (sub, browseSubRow, Lang::start(), true);
}

void MainComponent::paintBusy (juce::Graphics& g)
{
    if (busyJobs <= 0 || busyBar.getWidth() < 40) return;

    const auto r = busyBar.getLocalBounds().toFloat();

    //  Sobre el cristal de la pantalla y con sus colores: la maquina ya habla
    //  ahi, y una tarjeta de otro color encima seria un cartel del sistema
    //  operativo pegado sobre un instrumento.
    //  Opaca del todo: al 94% se leia el tempo por debajo del rotulo, y dos
    //  textos superpuestos es exactamente lo que esta barra viene a evitar.
    g.setColour (ZatiColours::screenBg);
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (ZatiColours::lcdFg.withAlpha (0.30f));
    g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);

    auto in = busyBar.getLocalBounds().reduced (Metrics::sm, 5);

    //  Lo que lleva, en segundos. Es el numero que convierte "esto no responde"
    //  en "esto esta tardando", y son dos cosas distintas.
    const double secs = juce::jmax (0.0, (juce::Time::getMillisecondCounterHiRes() - busyStartMs) / 1000.0);
    const auto   time = Lang::ltr (juce::String (secs, 1) + " s");

    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.10f));
    const int timeW = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), time)) + 6;
    auto timeCell = Lang::takeEnd (in, timeW);

    auto label = in.removeFromTop (14);
    g.setColour (ZatiColours::lcdFg);
    g.drawText (busyWhat, label, Lang::start(), true);
    g.setColour (ZatiColours::lcdDim);
    g.drawText (time, timeCell.withHeight (14).withY (label.getY()), Lang::end());

    //  La barra. Con progreso cuando se sabe - exportar y cargar un kit lo
    //  saben - y un bloque que va y viene cuando no: decodificar no puede
    //  decir cuanto falta sin mentir, y una barra que miente es peor que una
    //  que solo dice "sigo aqui".
    auto bar = in.removeFromTop (6);
    if (bar.getWidth() < 8) return;

    g.setColour (ZatiColours::lcdFg.withAlpha (0.16f));
    g.fillRoundedRectangle (bar.toFloat(), 1.5f);

    if (busyProgress >= 0.0f)
    {
        auto done = bar.withWidth ((int) ((float) bar.getWidth() * juce::jlimit (0.0f, 1.0f, busyProgress)));
        //  Con la tinta DE LA PANTALLA, no con el acento del chasis: en PAPEL
        //  el acento es casi negro, y una barra casi negra sobre el cristal
        //  oscuro es una barra que no se ve. Cada superficie con su tinta.
        g.setColour (ZatiColours::lcdFg);
        g.fillRoundedRectangle (done.toFloat(), 1.5f);
    }
    else
    {
        //  Dos segundos por vuelta, y el bloque mide un quinto: lo bastante
        //  lento como para no parecer nervioso y lo bastante rapido como para
        //  que se vea que se mueve en la primera mirada.
        const float t   = (float) std::fmod (secs, 2.0) / 2.0f;
        const float w   = (float) bar.getWidth() * 0.2f;
        const float ease = 0.5f - 0.5f * std::cos (t * juce::MathConstants<float>::twoPi);
        const float x   = (float) bar.getX() + ease * ((float) bar.getWidth() - w);
        g.setColour (ZatiColours::lcdFg);
        g.fillRoundedRectangle (x, (float) bar.getY(), w, (float) bar.getHeight(), 1.5f);
    }
}

void MainComponent::paintChopSheetContent (juce::Graphics& g)
{
    if (chopSheet.sheetBounds.isEmpty()) return;

    const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00B7);
    const int sp = juce::jmax (0, selectedPad);
    auto inner = chopSheet.sheetBounds.reduced (Metrics::lg, Metrics::md);

    auto titleRow = inner.removeFromTop (32).withTrimmedTop (8);
    //  Y de la puerta a la rejilla de dieciseis, que vive en este mismo
    //  renglon desde que esta ficha tambien puede cambiar de pad.
    titleRow = antesDe (antesDe (titleRow, chopCloseButton), chopPadPickBtn);
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    pintaTitulo (g, titleRow,
                 T ("AUTO CHOP") + "  " + dot + "  " + T ("PAD %1", juce::String (sp + 1))
                + (padName[(size_t) sp].isNotEmpty() ? "  " + dot + "  " + padName[(size_t) sp].toUpperCase()
                                                     : juce::String()), "titulo", true);

    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.06f));
    {
        //  Y se para antes del boton de cerrar. El titulo si lo hacia y esto
        //  no, y el boton mide 40 px sobre una fila pintada de 32: la primera
        //  linea - "...y los reparte por los pads." - pasaba por debajo de la
        //  x. El banco no puede verlo, porque mide componentes y esto es
        //  texto pintado a mano.
        auto para = antesDe (inner.removeFromTop (40), chopCloseButton);
        //  La explicacion cambia con el modo, porque lo que hace el boton
        //  cambia: dejar la de trozos iguales puesta en modo GOLPES seria la
        //  ficha describiendo lo que hacia antes.
        g.drawFittedText (chopByHits
                              ? T ("Busca donde empieza cada golpe y corta ahi, no a intervalos "
                                   "iguales. El pad de origen se queda con el primero.")
                              : T ("Parte este sample en trozos iguales y los reparte por los pads. "
                                   "El pad de origen se queda con el primero."),
                          para, Lang::start (juce::Justification::top), 3, 1.0f);
    }

    inner.removeFromTop (Metrics::md);
    g.setColour (ZatiColours::ink.withAlpha (0.75f));
    g.setFont (ZatiColours::labelFont (Metrics::fMeta, 0.16f));
    pintaTitulo (g, inner.removeFromTop (14), T ("COMO"));
    inner.removeFromTop (Metrics::hit + Metrics::md);

    g.setColour (ZatiColours::ink.withAlpha (0.75f));
    g.setFont (ZatiColours::labelFont (Metrics::fMeta, 0.16f));
    //  En GOLPES el numero es un TECHO y el rotulo lo dice, porque un boton que
    //  pone 16 y produce 9 trozos parece roto si nadie lo explica.
    //  Y por `apunta`: lo que se PINTA tiene que salir en el volcado de
    //  rotulos, o la regla de «ningun rotulo pintado debajo de un control» no
    //  puede verlo. Es la misma razon por la que `apunta` salio de dentro de
    //  `pintaTitulo` el dia que tuvo un segundo cliente.
    {
        const auto tTrozos = chopByHits ? T ("TROZOS (como mucho)") : T ("TROZOS");
        const auto banda   = inner.removeFromTop (14);
        apunta (g, banda, tTrozos, "capitulo");
        g.drawText (tTrozos, banda, Lang::start());
    }

    inner.removeFromTop (Metrics::hit + Metrics::sm + Metrics::hit + Metrics::md);

    //  The plan, in pad numbers. This is the whole point of the sheet: the
    //  old one-tap chop was destructive precisely because it never said this.
    const auto targets = chopTargets (chopSlices, chopOnlyEmpty);
    auto planned = inner.removeFromTop (40);

    if (uiSample[(size_t) sp] == nullptr)
    {
        g.setColour (ZatiColours::red);
        g.setFont (ZatiColours::monoFont (Metrics::fLabel, true));
        g.drawFittedText (T ("Este pad no tiene sonido que cortar."),
                          planned, Lang::start (juce::Justification::top), 1, 0.8f);
        return;
    }

    const int hits = (chopHitsFor == sp) ? (int) chopHits.size() : 0;
    const int n = chopByHits ? juce::jmin (targets.size(), hits) : targets.size();

    juce::StringArray nums;
    for (int i = 0; i < n; ++i)
        nums.add (juce::String (targets[i] + 1).paddedLeft ('0', 2));

    int overwritten = 0;
    for (int i = 1; i < n; ++i)
        if (padHasSample[(size_t) targets[i]]) ++overwritten;

    //  Cuantos golpes hay ahi dentro, que es la unica cifra que dice si este
    //  modo tiene algo que hacer con este sonido: un pad de un solo golpe no se
    //  trocea por golpes por mucho que se pida.
    if (chopByHits)
    {
        g.setColour (hits >= 2 ? ZatiColours::inkDim : ZatiColours::red);
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.06f));
        g.drawFittedText (hits >= 2 ? T ("%1 golpes encontrados", juce::String (hits))
                                    : T ("no hay golpes que separar aqui"),
                          planned.removeFromTop (18), Lang::start (juce::Justification::top), 1, 0.8f);
    }

    g.setColour (ZatiColours::ink.withAlpha (0.85f));
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.06f));
    g.drawFittedText (T ("va a pads: %1", nums.joinIntoString (" ")),
                      planned.removeFromTop (22), Lang::start (juce::Justification::top), 2, 0.8f);

    juce::String warn;
    if (! chopByHits && targets.size() < chopSlices)
        warn = "solo caben " + juce::String (targets.size()) + " sin pisar nada";
    else if (overwritten > 0)
        warn = "PISA " + juce::String (overwritten) + (overwritten == 1 ? " pad con sonido" : " pads con sonido");
    else
        warn = "no pisa ningun pad con sonido";

    g.setColour (overwritten > 0 ? ZatiColours::red : ZatiColours::inkDim);
    g.drawFittedText (warn, planned, Lang::start (juce::Justification::top), 1, 0.8f);
}

void MainComponent::paintExportSheetContent (juce::Graphics& g)
{
    if (exportSheet.sheetBounds.isEmpty()) return;

    auto inner = exportSheet.sheetBounds.reduced (Metrics::lg, Metrics::md);
    inner.removeFromTop (2);

    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    pintaTitulo (g, antesDe (inner.removeFromTop (18), exportCloseButton), T ("EXPORTAR"));
    inner.removeFromTop (10);

    // What is going to be rendered, and how long it will be. Stated before
    // you press, not after: a bounce is the one action here you cannot undo
    // by tapping again.
    const int steps = engine.lengthInSteps();
    const double secs = steps * (60.0 / juce::jmax (20.0, engine.getBpm())) * 0.25;
    int loaded = 0;
    for (auto& s : uiSample) if (s != nullptr) ++loaded;

    auto line = [&g, &inner] (const juce::String& k, const juce::String& v, juce::Colour vc)
    {
        auto r = inner.removeFromTop (17);
        g.setColour (ZatiColours::inkDim);
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.08f));
        g.drawText (k, Lang::takeStart (r, 76), Lang::start());
        g.setColour (vc);
        g.setFont (ZatiColours::monoFont (Metrics::fValue, true));
        g.drawText (v, r, Lang::start());
    };

    line (T ("fuente"), exportSourceLabel(), ZatiColours::ink);
    line (T ("duracion"), steps > 0 ? Lang::ltr (juce::String (secs, 1) + " s") + "  ·  "
                                        + T ("%1 compases", juce::String (steps / 16))
                                    : T ("vacio"),
          steps > 0 ? ZatiColours::ink : ZatiColours::red);
    line (T ("pistas"), T ("%1 pads con muestra", juce::String (loaded)), ZatiColours::ink);
    //  El destino DE VERDAD, no el de siempre escrito a mano. Decia
    //  "ZATI/Exports/..." pasara lo que pasara, asi que el dia que la carpeta
    //  se pudo elegir habria mentido en la unica linea que dice donde acaba el
    //  trabajo. Se ensena el nombre de la carpeta y no la ruta entera: en un
    //  telefono la ruta son sesenta caracteres de los que importan los ultimos
    //  quince.
    {
        //  Y LA RUTA SE CACHEA, que `paint` no es sitio para escribir en disco.
        //
        //  ProjectStore::exports() crea la carpeta, deja dentro un fichero de
        //  un byte, lo vuelve a leer y lo borra - `canReallyWriteInto`, que es
        //  correcto donde se DECIDE (startExport) y absurdo aqui: con el rebote
        //  en marcha, pollExport repinta esta ficha en cada tic, o sea E/S de
        //  verdad treinta veces por segundo en la misma carpeta en la que el
        //  hilo de exportacion esta escribiendo.
        if (destinoCache == juce::File())
            destinoCache = ProjectStore::exports();
        const auto dir = destinoCache;
        line (T ("destino"),
              Lang::ltr (dir.getParentDirectory().getFileName() + "/" + dir.getFileName()
                         + "/" + (currentProject.isNotEmpty() ? currentProject : juce::String ("ZATI"))),
              ZatiColours::inkDim);
    }

    inner.removeFromTop (6);

    // Progress, then the verdict.
    if (exportJob != nullptr)
    {
        auto bar = inner.removeFromTop (8).reduced (0, 0);
        //  El canal de la barra de progreso es un hueco; lo que lo llena es el
        //  acento. Con padBorder el canal salia mas claro que el relleno en las
        //  dos carcasas oscuras, y la barra parecia ir al reves.
        g.setColour (ZatiColours::groove (0.35f));
        g.fillRect (bar);
        g.setColour (ZatiColours::accent);
        g.fillRect (bar.withWidth ((int) ((float) bar.getWidth()
                        * juce::jlimit (0.0f, 1.0f, exportJob->progress.load (std::memory_order_relaxed)))));

        g.setColour (ZatiColours::inkDim);
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
        g.drawText (T ("escribiendo %1", juce::String (exportJob->passDone.load (std::memory_order_relaxed) + 1)
                                       + "/" + juce::String (exportJob->passTotal.load (std::memory_order_relaxed))),
                    inner.removeFromTop (16), Lang::start());
    }
    else if (exportStatus.isNotEmpty())
    {
        g.setColour (exportOk ? ZatiColours::accent : ZatiColours::red);
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
        g.drawFittedText (exportOk ? T ("listo: %1", exportStatus) : exportStatus,
                          inner.removeFromTop (24), Lang::start (juce::Justification::top), 2);
    }
    else
    {
        g.setColour (ZatiColours::inkDim.withAlpha (0.75f));
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, false));
        g.drawFittedText (T ("MASTER = un WAV con lo que oyes.  PISTAS = el master mas un WAV "
                             "por pad, para mezclar fuera."),
                          inner.removeFromTop (26), Lang::start (juce::Justification::top), 2);
    }
}

//  THE GESTURES PAGE.
//
//  Printed like the legend on a machine's lid: the gesture on the left in the
//  ink that names things, what it does on the right in the ink that says them.
//  No controls at all - there is nothing here to set, only something to know -
//  so it is one paint call and no components.
//
//  Six rows, and the list is short on purpose. A machine with thirty hidden
//  gestures has none, because nobody can hold thirty; these are the six that
//  save a sheet or a trip across the face while something is playing.
void MainComponent::paintGesturesPage (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.isEmpty() || setSheet.sheetBounds.isEmpty()) return;

    //  The same title the other two pages carry, in the same place. A page of
    //  a card that skips it reads as a different card.
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    //  El titulo lo pinta paintSetTitle para las cuatro paginas. Aqui habia
    //  un segundo "GESTOS" en otra caja: dos titulos para una pagina.

    struct Row { const char* how; const char* what; };
    const Row rows[kNumGestures] =
    {
        { "MANTEN UN PAD",      "abre sus ajustes sin sonar" },
        { "MANTEN UN EFECTO",   "coge los mandos sin apagarlo" },
        { "MANTEN CARGAR",      "abre la biblioteca en el pad elegido" },
        { "MANTEN PLAY",        "para y corta todo lo que suene" },
        { "ARRASTRA LA PANTALLA", "cambia de patron" },
        { "GOLPEA ARRIBA O ABAJO", "toca mas fuerte o mas flojo" },
    };

    const int rowH = juce::jmax (24, area.getHeight() / kNumGestures);

    for (int i = 0; i < kNumGestures; ++i)
    {
        auto r = area.removeFromTop (rowH);

        //  A hairline between rows, not a box around each: the page is a list
        //  on a card, and boxes would make six cards out of it.
        if (i > 0)
        {
            g.setColour (ZatiColours::ink.withAlpha (0.10f));
            g.fillRect (r.getX(), r.getY(), r.getWidth(), 1);
        }

        auto text = r.reduced (2, 0);

        g.setColour (ZatiColours::ink.withAlpha (0.92f));
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.06f));
        //  The gesture takes the width its own words need, and what it does
        //  takes the rest - a fixed split put "GOLPEA ARRIBA O ABAJO" over two
        //  lines in Spanish and left half the row empty in English.
        const int howW = juce::jlimit (90, text.getWidth() * 3 / 5,
                                       (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(),
                                                                                     T (rows[i].how)) + 10);
        auto howCell = Lang::takeStart (text, howW);
        g.drawFittedText (T (rows[i].how), howCell, Lang::start(), 2, 0.9f);

        g.setColour (ZatiColours::inkDim);
        g.setFont (ZatiColours::monoFont (Metrics::fFine));
        g.drawFittedText (T (rows[i].what), text, Lang::start(), 2, 0.85f);
    }
}

void MainComponent::paintInstSheetContent (juce::Graphics& g)
{
    if (instSheet.sheetBounds.isEmpty()) return;

    const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00B7);

    //  LAS DOS BANDAS SALEN DEL MAQUETADO, no se vuelven a calcular aqui.
    //
    //  Esta funcion las rehacia con otro origen -de la TARJETA y no del cuerpo,
    //  que es de donde sale el maquetado desde que la ficha se desplaza- y con
    //  un reduced de mas: el titulo se pintaba encima de la tapa SUBBASS y el
    //  nombre del pack encima de ELECTRIC PIANO. Ver Sheet::hazDesplazable.
    auto titleRow = antesDe (instTitleArea, instCloseButton);
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    //  EL TITULO DICE A DONDE VA ESTO, que es lo unico que no se puede deducir
    //  mirando una lista de nombres, y son dos destinos distintos: un kit se
    //  lleva el banco de delante y un instrumento un pad clavado del D.
    //  EL TITULO DICE A DONDE VA, que es lo unico que no se deduce mirando una
    //  rejilla de dibujos - y son dos destinos distintos: un instrumento va al
    //  pad elegido y un kit se lleva el banco de delante.
    const bool aUnPad = (! instCatalogo.empty()
                         && ! instCatalogo[(size_t) instPack].instr.empty()
                         && instCatalogo[(size_t) instPack].instr[0].familiaSintes >= 0);
    pintaTitulo (g, titleRow,
                 T ("INSTRUMENTOS") + "  " + dot + "  "
                     + (aUnPad ? T ("PAD %1", Lang::ltr (juce::String (instDestPad + 1)))
                               : T ("BANCO %1", juce::String::charToString ((juce::juce_wchar) ('A' + currentBank)))),
                 "titulo", true);


    //  EL NOMBRE DEL PACK, entre las dos tapas. Ver la maqueta: pintado y no
    //  un componente, para que el ancho se lo queden las que se tocan.
    {
        //  Se para antes de CADA tapa y en el lado en el que esta: recortar
        //  por ANCHO da igual en las dos, y en arabe PACK + esta a la
        //  izquierda, asi que el nombre del pack se le metia debajo.
        auto fila = antesDe (antesDe (instPackArea, instPackDownBtn), instPackUpBtn);
        g.setColour (ZatiColours::ink.withAlpha (0.9f));
        g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.10f));
        const auto nombre = instCatalogo.empty() ? juce::String ("-")
                          : instCatalogo[(size_t) instPack].nombre;
        //  Pintado, no un componente, asi que lo unico que lo mide es
        //  UiAudit::rotulo. El de dentro se llama ZATI -que ya esta en la
        //  lista de los que no se traducen- y los de disco son dato.
        pintaTitulo (g, fila, nombre, "seccion", true);
    }
    //  Y el pie se cuelga de la banda que el maquetado publico, no de una
    //  cuenta paralela. Ver resized: la que habia aqui solo valia para la
    //  lista, asi que con la rejilla puesta el pie caia fuera del cuerpo.
    auto inner = instPieArea;

    //  Y UNA LINEA QUE DICE QUE VA A PASAR. Tocar una tapa aqui se lleva los
    //  dieciseis pads del banco de delante, y eso no se puede deducir mirando
    //  una rejilla de nombres.
    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.06f));
    juce::String pie;
    //  Y CUANDO NO HAY NADA INSTALADO, DONDE SE PONEN. Es la misma falta que
    //  tenia ZATI/Kits antes de MIS KITS: quien se baja un pack no tiene forma
    //  de saber a que carpeta va, y un menu que dice "no hay nada" sin decir
    //  donde mirar deja el trabajo a medias. La ruta es fija, asi que decirla
    //  aqui no puede mentir - que es lo que le paso a la linea "destino" de
    //  EXPORTAR el dia que la carpeta se pudo elegir.
    //  Y EL PIE, para el caso de la rejilla: dos frases porque son dos gestos
    //  -elegir donde y elegir cual- y sin decirlo una rejilla de dieciseis
    //  dibujos encima de otra de dieciseis numeros no se explica sola.
    if (aUnPad)
    {
        pie = T ("Elige el pad arriba y el instrumento abajo. Va al pad %1.",
                 Lang::ltr (juce::String (instDestPad + 1)));
        g.drawFittedText (pie, inner.removeFromTop (40), Lang::start (juce::Justification::top), 2, 1.0f);
        return;
    }

    if (instCatalogo.size() <= 1)
    {
        pie = T ("Los packs van en %1", "ZATI/Instrumentos");
        if (! instCatalogo.empty())
            pie = T ("Toca uno y sus 16 presets van al banco %1. Lo que hubiera se pierde.",
                     juce::String::charToString ((juce::juce_wchar) ('A' + currentBank)))
                + "   " + pie;
    }
    else if (instCatalogo.empty())
    {
        pie = T ("No hay instrumentos instalados");
    }
    else
    {
        const auto& p = instCatalogo[(size_t) instPack];
        pie = p.abierto
                ? T ("Toca uno y sus 16 presets van al banco %1. Lo que hubiera se pierde.",
                     juce::String::charToString ((juce::juce_wchar) ('A' + currentBank)))
                : T ("Este pack no esta comprado.");
        pie += "  " + dot + "  "
             + T ("%1 de %2", juce::String (instPack + 1), juce::String ((int) instCatalogo.size()));
    }
    g.drawFittedText (pie, inner.removeFromTop (40), Lang::start (juce::Justification::top), 2, 1.0f);
}

void MainComponent::paintManualBody (juce::Graphics& g)
{
    auto r = manualBody.getLocalBounds().reduced (Metrics::sm, 0);
    r.removeFromTop (Metrics::sm);

    //  SE RECORRE LA TABLA, no una cuenta escrita al lado.
    //
    //  Aqui habia un `kManualChapters = 9` declarado en el .h mientras la
    //  tabla tenia diez filas y manualContentHeight las recorria todas: o sea
    //  que SI ALGO NO SUENA estaba traducido a los cuatro idiomas, reservaba
    //  su alto en el desplazamiento y no se pintaba NUNCA. La misma regla
    //  escrita dos veces, que es el fallo mas repetido de esta casa - y aqui
    //  no lo veia ninguna de las ocho reglas del banco, porque un capitulo que
    //  no se dibuja no es un componente. El comentario de manualContentHeight
    //  ya decia que las dos funciones tienen que recorrer «la misma tabla con
    //  las mismas alturas»; una de las dos no lo hacia.
    int c = -1;
    for (const auto& ch : kManual)
    {
        ++c;

        //  El titulo del capitulo con su filete, igual que las secciones de
        //  las fichas: asi el manual se lee como parte de la misma maquina.
        auto band = r.removeFromTop (kManualTitleH);
        const auto secText = T (ch.title);
        g.setColour (ZatiColours::ink.withAlpha (0.55f));
        g.setFont (ZatiColours::labelFont (Metrics::fMeta, 0.22f));
        //  Apuntado para el banco: es lo unico que hace que «cuantos capitulos
        //  se dibujan» sea una cifra y no una lectura. Ver Tests/plano.py.
        apunta (g, band, secText, "capitulo");
        g.drawText (secText, band, Lang::start());

        const float tw = juce::GlyphArrangement::getStringWidth (
                             ZatiColours::labelFont (Metrics::fMeta, 0.22f), secText);
        const float ly = (float) band.getCentreY() + 1.0f;
        g.setColour (ZatiColours::ink.withAlpha (0.18f));
        //  El filete sale por el lado por el que se lee, no siempre por la
        //  derecha: en arabe la linea va al reves y un filete a la derecha
        //  cruzaria por encima del titulo.
        auto ruleRow = band;
        const auto rule = Lang::takeEnd (ruleRow, juce::jmax (0, band.getWidth() - (int) tw - 8));
        g.fillRect ((float) rule.getX(), ly, (float) rule.getWidth(), 1.0f);

        for (const char* line : ch.lines)
        {
            if (line == nullptr) continue;
            auto row = r.removeFromTop (kManualLineH);

            //  El punto de color del capitulo, delante de cada linea: es lo
            //  unico que hace que un capitulo se vea como un bloque cuando lo
            //  que hay debajo es una lista de frases sueltas.
            auto dot = Lang::takeStart (row, 14);
            g.setColour (Zati::colour (c).withAlpha (0.9f));
            g.fillEllipse ((float) dot.getX() + 2.0f, (float) dot.getCentreY() - 2.5f, 5.0f, 5.0f);

            g.setColour (ZatiColours::ink.withAlpha (0.92f));
            g.setFont (ZatiColours::monoFont (Metrics::fMeta));
            g.drawFittedText (T (line), row.reduced (2, 0), Lang::start(), 2, 0.9f);
        }

        r.removeFromTop (kManualGap);
    }
}

void MainComponent::paintManualSheetContent (juce::Graphics& g)
{
    if (manualSheet.sheetBounds.isEmpty()) return;

    auto inner = manualSheet.sheetBounds.reduced (Metrics::lg, Metrics::md);
    //  Se para antes del boton de cerrar, como todas las demas fichas - y por
    //  el lado en el que ESTE, que en arabe es el izquierdo.
    auto titleRow = antesDe (inner.removeFromTop (16), manualCloseButton);

    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    pintaTitulo (g, antesDe (titleRow, manualCloseButton), T ("MANUAL"), "titulo", true);

    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.10f));
    //  EL NUMERO SALE DE LA TABLA, no de la frase.
    //
    //  Decia «en ocho capitulos» escrito a mano - en el rotulo, en la fila de
    //  Lang y en cuatro comentarios - con una tabla de diez. Un numero dentro
    //  de una frase traducida es la unica clase de constante que no se puede
    //  contrastar leyendo el codigo de al lado, asi que se interpola: el dia
    //  que entre un capitulo, la frase se entera sola en los cuatro idiomas.
    //  Y en LTR, que es la regla de la casa para las cifras latinas en arabe.
    //
    //  Apuntado y recortado antes de la x, como el titulo: se dibuja con
    //  drawText y no era un componente, asi que no lo veia nadie.
    pintaTitulo (g, antesDe (inner.removeFromTop (14), manualCloseButton),
                 T ("lo que hay que saber, en %1 capitulos",
                    Lang::ltr (juce::String (kManualChapterCount))),
                 "subtitulo");
}

// Build the chips from what THIS device actually offers. Nothing is
// hardcoded: a phone that only does 48 kHz shows one clock, and the burst
// sizes are the ones the driver will really accept.
//  LA PAGINA DE MIDI. Dos rotulos, y la unica cifra que hace falta saber para
//  enchufar algo: que nota manda cada pad.
void MainComponent::paintMidiPage (juce::Graphics& g, juce::Rectangle<int> area)
{
    //  Los rotulos de los dos bloques, sobre sus tapas. Se pintan desde las
    //  posiciones que dejo resized(), que es de donde salen todas las medidas
    //  de esta cara.
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
    g.setColour (ZatiColours::textOn (ZatiColours::chassisTop).withAlpha (0.55f));

    auto label = [&g] (juce::Rectangle<int> ctrl, const juce::String& text)
    {
        if (ctrl.isEmpty()) return;
        g.drawText (text, ctrl.withY (ctrl.getY() - 14).withHeight (14),
                    juce::Justification::centredLeft, false);
    };
    label (midiOutBtn.getBounds(), T ("MANDAR NOTAS A"));
    label (midiInBtn.getBounds(),  T ("RECIBIR NOTAS DE"));

    if (area.isEmpty()) return;

    //  Y la unica cifra que hace falta: sin ella hay que adivinar por que el
    //  modulo de al lado toca la nota equivocada.
    g.setColour (ZatiColours::textOn (ZatiColours::chassisTop).withAlpha (0.55f));
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, false));
    g.drawFittedText (T ("El pad 1 es la nota %1, y de ahi hacia arriba. Canal %2.",
                         Lang::ltr (juce::String (MidiIo::kBaseNote)),
                         Lang::ltr (juce::String (midi.getChannel()))),
                      area, juce::Justification::centredLeft, 2, 1.0f);
}

//  The chip and the name belong to the ROW, so they are painted by the panel
//  the rows live in - in its coordinates, which scroll with them. Painted on
//  the sheet behind the sliders, as they were, they stayed put while the
//  strips moved and every channel ended up wearing another channel's name.
void MainComponent::paintMixRows (juce::Graphics& g)
{
    //  LA PAGINA DE CANALES: el mismo chip y el mismo renglon, con la CUENTA DE
    //  PADS donde un pad lleva su nombre. Un canal no tiene nombre que enseñar
    //  y si tiene algo que decir de si mismo — cuantos le entran, que es lo que
    //  separa un canal vacio de uno que suena y no se oye.
    if (mixPage == mixPageCanales)
    {
        int cuentan[kNumCanales] = {};
        for (int p = 0; p < kNumPads; ++p)
            if (padHasSample[(size_t) p])
                ++cuentan[juce::jlimit (0, kNumCanales - 1, engine.getPadCanal (p))];

        for (int c = 0; c < kNumCanales; ++c)
        {
            if (canFaders[c] == nullptr || ! canFaders[c]->isVisible()) continue;
            const auto fr   = canFaders[c]->getBounds();
            const auto frag = Zati::colour (c);
            const bool has  = cuentan[c] > 0;

            auto chip = juce::Rectangle<int> (canRowX[(size_t) c] + 4, fr.getY() + 4, 22, fr.getHeight() - 8);
            g.setColour (has ? frag : ZatiColours::markOn (ZatiColours::chassisTop, 0.20f));
            g.fillRect (chip);
            g.setColour (has ? ZatiColours::bestOn (frag, ZatiColours::ink, juce::Colours::white)
                             : ZatiColours::inkDim);
            g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
            g.drawText (juce::String (c + 1).paddedLeft ('0', 2), chip, juce::Justification::centred);

            g.setColour (has ? ZatiColours::ink.withAlpha (0.8f) : ZatiColours::inkDim.withAlpha (0.5f));
            g.setFont (ZatiColours::monoFont (Metrics::fMeta));
            const int nameX = chip.getRight() + 6;
            g.drawText (has ? T ("%1 PADS", Lang::ltr (juce::String (cuentan[c])))
                            : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")),
                        nameX, fr.getY(), juce::jmax (24, fr.getX() - 6 - nameX), fr.getHeight(),
                        Lang::start(), true);
        }
        return;
    }

    //  Only the bank on show. Painting all sixty-four drew the chip, number and
    //  name of forty-eight strips whose sliders are hidden - at whatever
    //  coordinates they were left holding - straight over the sixteen in front.
    for (int i = mixBank * kPadsPerBank; i < (mixBank + 1) * kPadsPerBank; ++i)
    {
        if (mixFaders[i] == nullptr) continue;
        const auto fr = mixFaders[i]->getBounds();
        const auto frag = Zati::colour (padZati[(size_t) i]);
        const bool has = padHasSample[(size_t) i];

        auto chip = juce::Rectangle<int> (mixRowX[(size_t) i] + 4, fr.getY() + 4, 22, fr.getHeight() - 8);
        //  El chip de un canal sin sonido: marca medida contra la tarjeta, no
        //  el borde de la placa de pads - que en carcasa oscura sale mas claro
        //  que el fondo y hace que un canal vacio destaque mas que uno cargado.
        g.setColour (has ? frag : ZatiColours::markOn (ZatiColours::chassisTop, 0.20f));
        g.fillRect (chip);
        g.setColour (has ? ZatiColours::bestOn (frag, ZatiColours::ink, juce::Colours::white)
                         : ZatiColours::inkDim);
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
        g.drawText (juce::String (i + 1).paddedLeft ('0', 2), chip, juce::Justification::centred);

        g.setColour (has ? ZatiColours::ink.withAlpha (0.8f) : ZatiColours::inkDim.withAlpha (0.5f));
        g.setFont (ZatiColours::monoFont (Metrics::fMeta));
        //  The name gets everything between its colour chip and the fader,
        //  rather than a fixed 40 px that left a gap on one side and cut the
        //  name to eight characters on the other.
        const int nameX = chip.getRight() + 6;
        g.drawText (has && padName[(size_t) i].isNotEmpty() ? padName[(size_t) i].toUpperCase()
                                                            : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")),
                    nameX, fr.getY(), juce::jmax (24, fr.getX() - 6 - nameX), fr.getHeight(),
                    Lang::start(), true);
    }
}

void MainComponent::paintMixSheetContent (juce::Graphics& g)
{
    if (mixSheet.sheetBounds.isEmpty()) return;

    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    //  POR T() Y NO A MANO, que era la unica ficha cuyo titulo no se traducia.
    //
    //  La tabla tiene las dos filas -MIX vale MEZCLA en espanol, SOLO ACTIVO
    //  vale SOLO ACTIVE- y la PESTANA que abre esta ficha ya usa T("MIX"): o
    //  sea que tocabas MEZCLA y aterrizabas en una tarjeta titulada MIX, y en
    //  chino y en arabe se quedaba en MIX pasara lo que pasara.
    //
    //  No lo veia ningun banco: el titulo se PINTA, y las seis reglas de
    //  expo.py recorren el arbol de COMPONENTES. Un rotulo dibujado no es un
    //  componente y por eso llevaba aqui desde el principio.
    const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00B7);
    {
        const auto caja = antesDe (mixSheet.sheetBounds.reduced (14, 12).removeFromTop (16),
                                   mixCloseButton);
        const auto txt = engine.anySolo() ? T ("MIX") + "  " + dot + "  " + T ("SOLO ACTIVO") : T ("MIX");
        pintaTitulo (g, caja, txt);
    }
}

// ============================================================================
//  LA PORTADA DEL ARRANQUE: la cara no se enseña hasta que ha dejado de moverse.
//
//  «Cuando abres la app hace un ajuste a la pantalla», y es verdad. Con
//  targetSdk 36 la app va EDGE TO EDGE -la ventana es la pantalla entera y la
//  barra de estado y la pastilla de gestos se pintan encima- asi que la cara
//  tiene que restarse esos margenes. Y en el constructor no se pueden pedir:
//  View.getRootWindowInsets() no contesta hasta que la vista esta enganchada a
//  una ventana, asi que SystemInsets::get() devuelve CERO y el primer fotograma
//  sale con la cara debajo del reloj.
//
//  El ajuste llegaba en el primer o segundo latido -60 ms por tick- cuando
//  refreshSystemInsets veia que el numero habia cambiado y llamaba a resized().
//  Y el splash del sistema NO lo tapa, por diseno: Android lo retira en cuanto
//  la app dibuja su primer fotograma, que es precisamente el que va sin
//  ajustar. La barra «Iniciando» tampoco: es una BANDA, no un velo.
//
//  Asi que la cara no se enseña hasta que sus numeros son los definitivos, y
//  mientras tanto se pinta el chasis con la marca — el MISMO dibujo sobre el
//  MISMO fondo que el splash del sistema (el workflow ya comprueba que el color
//  del tema es el chasis de la carcasa de fabrica), asi que la entrega se lee
//  como una sola pantalla en vez de como tres.
//
//  Lo que NO se promete es que la marca salga al mismo tamano que el icono del
//  splash: ese lo decide Android en dp y desde aqui no se sabe. Lo que se
//  promete es el mismo dibujo sobre el mismo fondo.
void MainComponent::paintOverChildren (juce::Graphics& g)
{
    if (caraLista) return;

    pintaPortada (g);

    //  Y ESTO ES LO QUE DESBLOQUEA LA SESION. restoreSession() bloquea el hilo
    //  de mensajes -restaurar sintetiza, y llenar un banco de instrumentos son
    //  1238 ms medidos- asi que lanzarla antes de que la portada llegue a la
    //  pantalla la dejaria congelada sin nada dibujado. Ver timerCallback.
    portadaPintada = true;
}

//  EL TITULO DE LA REJILLA DE CANALES, que dice a que canal va el pad que se
//  esta editando: sin el, dieciseis cifras iguales no dicen de que hablan.
void MainComponent::paintCanalPickContent (juce::Graphics& g)
{
    if (canalSheet.sheetBounds.isEmpty()) return;

    auto inner = canalSheet.sheetBounds.reduced (Metrics::lg, Metrics::md);
    auto titulo = antesDe (inner.removeFromTop (Metrics::hit).withTrimmedTop (8).withHeight (24),
                           canalCloseBtn, Metrics::sm);

    const int sp = juce::jmax (0, selectedPad);
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    pintaTitulo (g, titulo,
                 T ("PAD %1", Lang::ltr (juce::String (sp + 1))) + "  "
                   + juce::String::charToString ((juce::juce_wchar) 0x00B7) + "  " + T ("CANAL"),
                 "titulo", true);
}

void MainComponent::paintPadPickContent (juce::Graphics& g)
{
    if (padPickSheet.sheetBounds.isEmpty()) return;

    auto inner = padPickSheet.sheetBounds.reduced (Metrics::lg, Metrics::md);
    //  El titulo se aparta de la cruz por `antesDe`, que decide el lado
    //  comparando los CENTROS. Estaba escrito a mano preguntando por el idioma,
    //  que es la cuenta que ya se arreglo seis veces y luego cinco mas: quien
    //  sabe donde esta la tapa es la tapa, no la lengua.
    auto titulo = antesDe (inner.removeFromTop (Metrics::hit).withTrimmedTop (8).withHeight (24),
                           padPickCloseBtn, Metrics::sm);

    const int sp = juce::jmax (0, selectedPad);
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    pintaTitulo (g, titulo,
                 T ("PAD %1", juce::String (sp + 1))
                   + (padName[(size_t) sp].isNotEmpty() ? "   " + padName[(size_t) sp] : juce::String()),
                 "titulo", true);
}

//  EL TITULO DEL MENU DE UNA RANURA. La banda la publica `resized()` y no se
//  calcula aqui: es la tercera vez que esta casa lo paga -el titulo de
//  INSTRUMENTOS, el nombre de su pack y su pie se pintaban desde la TARJETA y
//  se maquetaban desde el CUERPO, noventa pixeles de diferencia- y la regla
//  quedo escrita: la banda la publica el maquetado.
void MainComponent::paintRanuraContent (juce::Graphics& g)
{
    if (ranuraSheet.sheetBounds.isEmpty() || ranuraTituloBanda.isEmpty()) return;

    auto titulo = ranuraTituloBanda;

    //  Se aparta de la cruz por el lado que toque: en arabe el texto se va a
    //  la derecha, que es donde takeEnd la ha puesto. `antesDe` decide el lado
    //  comparando los CENTROS, que es lo que costo once hallazgos el dia que
    //  se escribio `setRight` a mano seis veces.
    titulo = antesDe (titulo, ranuraCloseBtn.getBounds());

    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    pintaTitulo (g, titulo, T ("RANURA %1", juce::String (ranuraEditada + 1)),
                 "titulo", true);
}

//  EL TITULO DICE LA BANDA Y SU FRECUENCIA -«EQ · BANDA 3 · 1.0 kHz»- porque
//  las cinco se parecen y el numero solo no dice cual estas tocando. Se aparta
//  de la cruz con `antesDe`, que decide el lado comparando los CENTROS: en
//  arabe la x esta a la izquierda y un `setRight` a mano no recorta nada.
void MainComponent::paintEqBandaContent (juce::Graphics& g)
{
    if (eqBandaSheet.sheetBounds.isEmpty() || eqBandaTituloBanda.isEmpty()) return;

    const int b = juce::jlimit (0, Eq5::kBands - 1, eqBandaSel);
    const float hz = eqEspejo.freqDe (b);
    const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00B7);
    const juce::String fr = hz >= 1000.0f ? juce::String (hz / 1000.0f, 1) + " kHz"
                                          : juce::String ((int) (hz + 0.5f)) + " Hz";

    auto titulo = antesDe (eqBandaTituloBanda, eqBandaCloseBtn.getBounds());
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    pintaTitulo (g, titulo,
                 "EQ " + dot + " " + T ("BANDA %1", juce::String (b + 1))
                       + " " + dot + " " + Lang::ltr (fr),
                 "titulo", true);
}

void MainComponent::paintPadSheetContent (juce::Graphics& g)
{
    if (padSheet.sheetBounds.isEmpty()) return;

    const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00B7);
    const int sp = juce::jmax (0, selectedPad);
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    //  The pad's name is a file name and files are named by whoever made
    //  them, so this line has no length it can count on. Stop it before the
    //  close button and let it shrink rather than run underneath.
    auto padTitleRow = padSheet.sheetBounds.reduced (14, 12).removeFromTop (16);
    //  Y DE LA TERCERA TAPA DE LA FILA: la puerta de la rejilla de dieciseis
    //  pads. Sin ella el titulo -"PAD 64 · ARP"- se le metia debajo en 280x653,
    //  doce hallazgos. Es la misma cuenta que ya hacian las otras dos.
    padTitleRow = antesDe (antesDe (antesDe (padTitleRow, previewButton), padPadPickBtn),
                           padCloseButton);
    //  Ellipsised rather than squeezed: a name long enough to need shrinking
    //  is long enough that shrinking will not save it, and a sentence cut off
    //  mid-letter reads as a bug where "..." reads as a long name.
    pintaTitulo (g, padTitleRow,
                 T ("PAD %1", juce::String (sp + 1))
                + (padName[(size_t) sp].isNotEmpty() ? "  " + dot + "  " + padName[(size_t) sp].toUpperCase() : juce::String()), "titulo", true);

    //  ...y los grupos, HUNDIDOS y debajo de todo lo demas. Los titulos de
    //  seccion con su raya ya decian donde empieza cada bloque y no donde
    //  ACABA: en la pagina del RECORTE, "RECORTE" nombraba las cuatro asas y
    //  tambien la fila de REV/BUCLE/RUIDO que hay debajo, que es de otra cosa.
    //  Ver pintaPaneles.
    pintaPaneles (g, padGrupos);

    {
        //  Group headers, each with a hairline running out to the right edge -
        //  the same engraved rule the machine face uses, so a sheet reads as
        //  three blocks (what the sound is, where it is cut, what the pad is)
        //  instead of eleven controls in a column.
        static const char* secSound[3] = { "SONIDO", "", "" };
        static const char* secTrim[3]  = { "RECORTE", "", "" };
        static const char* secRig[3]   = { "EFECTOS", "CORTE",   "FUENTE" };
        static const char* secRigT[3]  = { "EFECTOS", "EL PAD",  "" };
        const char* const* secNames = (padPage == padPageSound) ? secSound
                                    : (padPage == padPageTrim)   ? secTrim
                                                                 : (padRigTight ? secRigT : secRig);
        for (int i = 0; i < 3; ++i)
        {
            const auto r = padSectionArea[(size_t) i];
            if (r.isEmpty()) continue;

            g.setColour (ZatiColours::ink.withAlpha (0.55f));
            g.setFont (ZatiColours::labelFont (Metrics::fMeta, 0.22f));
            //  Sitting on the bottom edge of its band put the word straight
            //  onto the control under it. It keeps its own padding now, and
            //  the rule it rides moves with it.
            //  Centred in its band, not sunk to the bottom of it: trimming
            //  only the bottom left seven pixels of air over the word and
            //  three under it.
            const auto secText = T (secNames[i]);
            const auto textRow = r;
            apunta (g, textRow, secText, "capitulo");
            g.drawText (secText, textRow, Lang::start());

            const float tw = juce::GlyphArrangement::getStringWidth (
                                 ZatiColours::labelFont (Metrics::fMeta, 0.22f),
                                 secText);
            const float ly = (float) textRow.getCentreY() + 1.0f;
            g.setColour (ZatiColours::ink.withAlpha (0.18f));
            g.fillRect ((float) r.getX() + tw + 8.0f, ly,
                        juce::jmax (0.0f, (float) r.getRight() - ((float) r.getX() + tw + 8.0f)), 1.0f);
        }

        // Knobs: label above (same convention as FX).
        //
        //  CON SU COLOR, que es lo que faltaba. Este bloque ponia la fuente y
        //  no el color, asi que heredaba el ultimo que se hubiera puesto - y
        //  el ultimo era el del FILETE de la seccion, tinta al 18%. Resultado:
        //  PITCH, FINO, GANANCIA, PAN, ATAQUE, CAIDA, INICIO, FIN y los seis
        //  envios se pintaban a 1.4 de contraste, mas claros que las propias
        //  marcas del dial que hay debajo. El nombre de un control mas flojo
        //  que su decoracion es el fallo de siempre: se ve y no se lee.
        //
        //  0.55 es el mismo con el que la cara pinta los nombres de sus tres
        //  mandos, medido en Tests/skins.py como "rotulo de seccion".
        g.setColour (ZatiColours::ink.withAlpha (0.55f));
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.10f));
        //  placeKnobRow reserves 16 for the name and then insets the knob by
        //  2, so the band is the sixteen pixels that end two above the dial.
        //  Y LA PALABRA SALE DE LA TABLA, no de un literal aqui. La misma que
        //  `retranslateUi` pone como nombre accesible: lo que se ve y lo que
        //  lee TalkBack no pueden ser dos cadenas distintas mantenidas a mano.
        auto name = [this, &g] (juce::Slider& s)
        {
            if (const char* t = claveDeMando (s))
                g.drawText (T (t), bandAbove (s, ZatiLookAndFeel::kKnobName, 2, 6),
                            juce::Justification::centred);
        };
        if (padPage == padPageSound)
        {
            name (pitchSlider); name (fineSlider); name (volSlider);
            name (panSlider);
            name (attackSlider); name (releaseSlider);
            name (cutSlider); name (resoSlider);
            name (anchoSlider);
            name (chokeSlider);

            //  Same band, one pixel lower: the third row insets its cells by 3.
            g.drawText (T ("MODO"), bandAbove (modeButton, ZatiLookAndFeel::kKnobName, 3, 6), juce::Justification::centred);
        }
        else if (padPage == padPageTrim)
        {
            // Start/End stay linear (a trim range, not a knob): label to the left.
            g.setColour (ZatiColours::ink.withAlpha (0.55f));
            g.setFont (ZatiColours::monoFont (Metrics::fLabel, true).withExtraKerningFactor (0.06f));
            auto lab = [this, &g] (juce::Slider& s)
            {
                if (const char* t = claveDeMando (s))
                {
                    auto r = s.getBounds();
                    g.drawText (T (t), r.getX() - (ZatiLookAndFeel::kTrimLabel + 2), r.getY(),
                                ZatiLookAndFeel::kTrimLabel - 4, r.getHeight(), Lang::start());
                }
            };
            lab (startSlider); lab (endSlider);
            lab (fadeInSlider); lab (fadeOutSlider);
        }
        else
        {
            //  Los seis envios viven en el RACK y solo alli. Ver la maqueta:
            //  aqui queda la puerta y su rotulo lo lleva la tapa.
        }

        // ZATI row: the fragment colour this pad carries, named as well as
        // shown — the number and the name are the non-chromatic half.
        if (! zatiSwatchArea.isEmpty())
        {
            const int sel = padZati[(size_t) sp];
            const auto r  = zatiSwatchArea.toFloat();
            const float w = r.getWidth() / (float) Zati::kNumColours;

            for (int i = 0; i < Zati::kNumColours; ++i)
            {
                auto cell = juce::Rectangle<float> (r.getX() + (float) i * w, r.getY(),
                                                    w, r.getHeight()).reduced (2.0f, 0.0f);

                //  The one it carries is the only one at full strength and the
                //  only one wearing an outline. The other seven are there to
                //  be picked, not to be looked at.
                const bool on = (i == sel);
                g.setColour (Zati::colour (i).withAlpha (on ? 1.0f : 0.38f));
                g.fillRoundedRectangle (cell, 2.0f);

                if (on)
                {
                    g.setColour (ZatiColours::ink.withAlpha (0.75f));
                    g.drawRoundedRectangle (cell.reduced (0.5f), 2.0f, 1.4f);
                }
            }
        }

    }
}

void MainComponent::paintPianoSheetContent (juce::Graphics& g)
{
    if (seqSheet.sheetBounds.isEmpty()) return;

    auto inner = seqSheet.sheetBounds.reduced (Metrics::lg, Metrics::md);
    const int sp = juce::jmax (0, selectedPad);

    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    //  El titulo se aparta de las TRES tapas de la cabecera, y por el lado que
    //  toque. Antes se apartaba solo de la cruz y solo por la derecha: en arabe
    //  el texto se va a la derecha, que es justo donde takeEnd habia puesto las
    //  tapas, asi que el nombre del pad aterrizaba encima de ellas.
    auto titulo = antesDe (antesDe (antesDe (antesDe (inner.removeFromTop (16),
                                                      seqCloseButton,  Metrics::sm),
                                             pianoPadPickBtn, Metrics::sm),
                                    pianoPadDownBtn, Metrics::sm),
                           pianoPadUpBtn,   Metrics::sm);
    const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00B7);
    const juce::String tPiano = T ("PIANO") + "  " + dot + "  " + T ("PAD %1", juce::String (sp + 1))
                              + (padName[(size_t) sp].isNotEmpty() ? "   " + padName[(size_t) sp]
                                                                   : juce::String());
    pintaTitulo (g, titulo, tPiano, "titulo", false, 0.85f);

    //  Que se esta mirando, en notas y no en semitonos: "-12 a +12" no dice
    //  nada y "C-1 a C+1" dice exactamente donde esta la mano.
    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.10f));
    //  Y la ayuda se para donde el titulo, que va justo encima: la cabecera
    //  lleva PAD -, PAD + y la cruz, y este renglon se metia por debajo de las
    //  tres - "la rejilla para escribir" acababa detras de PAD -.
    auto ayuda = inner.removeFromTop (14);
    ayuda.setLeft  (titulo.getX());
    ayuda.setRight (titulo.getRight());
    const auto ayudaPiano = T ("toca el teclado para oir, la rejilla para escribir")
                              + "   " + dot + "   " + T ("OCTAVA") + " "
                              + PianoRoll::nombreDe (pianoBase)
                              + " - " + PianoRoll::nombreDe (pianoBase + pianoGrid.getFilas() - 1);
    apunta (g, ayuda, ayudaPiano, "dato");
    g.drawFittedText (ayudaPiano, ayuda, Lang::start(), 1, 0.8f);
}

//  The PROJECTS card. The name, where it lives, and the list.
void MainComponent::paintProjSheetContent (juce::Graphics& g)
{
    if (setSheet.sheetBounds.isEmpty()) return;

    //  Del CUERPO y no de la tarjeta: esta ficha se desplaza, asi que
    //  sus bandas viven en coordenadas del cuerpo - las mismas de las
    //  que sale el maquetado. Ver Sheet::hazDesplazable.
    auto inner = setSheet.cuerpo.getLocalBounds();
    inner.removeFromTop (16);          // el titulo, ver paintSetTitle

    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.08f));
    //  The close button hangs off the end of this same band, so the subtitle
    //  has to stop before it starts.
    //  Stop before the corner keys - on whichever side they are. In Arabic
    //  the close button is on the LEFT, and clamping the right edge to its x
    //  collapsed the row to nothing and the subtitle vanished entirely. Por
    //  `antesDe`, que es quien tiene esa regla: el lado lo dice la TAPA y no el
    //  idioma.
    auto subRow = antesDe (inner.removeFromTop (14), setCloseButton);
    const auto subProj = currentProject.isNotEmpty()
                             ? T ("abierto: %1", currentProject)
                             : (projModel.names.isEmpty()
                                    ? T ("sin proyectos - GUARDAR crea el primero")
                                    : T ("elige uno de la lista"));
    apunta (g, subRow, subProj, "dato");
    g.drawFittedText (subProj, subRow, Lang::start(), 1, 0.8f);

    //  NAME, and under it the folder these projects actually live in. The
    //  path is there because when a save goes missing the answer is almost
    //  always "it went somewhere else", and until now there was no way to see
    //  where that was from inside the app.
    if (! projNameRowArea.isEmpty())
    {
        auto r = projNameRowArea;
        g.setColour (ZatiColours::inkDim);
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.08f));
        pintaTitulo (g, Lang::takeStart (r, 60), T ("NOMBRE"), "seccion");
    }
    if (! projPathRowArea.isEmpty())
    {
        auto r = projPathRowArea;
        g.setColour (ZatiColours::inkDim.withAlpha (0.75f));
        g.setFont (ZatiColours::monoFont (Metrics::fFine, false));
        pintaTitulo (g, Lang::takeStart (r, 60), T ("CARPETA"), "seccion");
        //  Y ESTA TAMBIEN SE CACHEA, por lo mismo que `destinoCache` ocho
        //  cientas lineas mas arriba y con el mismo fallo: `ProjectStore::root
        //  ()` es `sub("Projects")`, y `sub` hace `createDirectory()`. O sea un
        //  syscall por repintado de PROYECTOS. El arreglo ya estaba escrito
        //  ahi al lado y no se aplico al vecino.
        if (raizCache == juce::File())
            raizCache = ProjectStore::root();
        apunta (g, r, Lang::ltr (raizCache.getFullPathName()), "dato");
        g.drawFittedText (Lang::ltr (raizCache.getFullPathName()),
                          r, Lang::start(), 1, 0.7f);
    }
}

void MainComponent::paintRackSheetContent (juce::Graphics& g)
{
    if (rackSheet.sheetBounds.isEmpty()) return;

    //  Del CUERPO y no de la tarjeta: esta ficha se desplaza, asi que
    //  sus bandas viven en coordenadas del cuerpo - las mismas de las
    //  que sale el maquetado. Ver Sheet::hazDesplazable.
    auto inner = rackSheet.cuerpo.getLocalBounds();
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00B7);
    //  Y DICE EL CANAL Y CUANTOS PADS LE ENTRAN, que es lo que hace falta
    //  saber antes de mover un fader: un envio de un canal vacio no se oye, y
    //  sin la cuenta no hay forma de distinguirlo de uno que no suena.
    int cuantos = 0;
    for (int p = 0; p < kNumPads; ++p) if (engine.getPadCanal (p) == canalActual) ++cuantos;
    auto titleRow = inner.removeFromTop (16);
    titleRow = antesDe (titleRow, rackCloseButton);
    pintaTitulo (g, titleRow,
                 T ("RACK") + "  " + dot + "  " + T ("CANAL %1", Lang::ltr (juce::String (canalActual + 1)))
                + "  " + dot + "  " + T ("%1 PADS", Lang::ltr (juce::String (cuantos))), "titulo", true);

    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.08f));
    {
        //  Y APARTADO DE LA CRUZ, que es lo que la regla del rotulo tapado
        //  saco en la primera corrida en que este renglon se apunto: la tapa
        //  mide un dedo entero -40 px- sobre un renglon de titulo de 16, asi
        //  que se derrama VEINTICUATRO sobre la banda de abajo y el texto le
        //  pasaba por debajo en 280x653 en es, en y ar -en chino no, que ahi
        //  la frase es mas corta-. Es el mismo caso que la cadena del
        //  secuenciador y el parrafo de AUTO CHOP, y se arregla igual: quien
        //  sabe de que lado esta la tapa es la TAPA y no el idioma.
        auto bandaRack = antesDe (inner.removeFromTop (14), rackCloseButton);
        apunta (g, bandaRack, T ("cuanto de este canal pasa por cada efecto"), "dato");
        g.drawFittedText (T ("cuanto de este canal pasa por cada efecto"),
                          bandaRack, Lang::start(), 1, 0.75f);
    }

    //  EL NOMBRE Y EL DIBUJO YA NO SE PINTAN AQUI: el canalon de la izquierda
    //  es una TAPA desde que la fila del rack es una RANURA y no un efecto,
    //  porque es la puerta al menu donde se cambia lo que hay dentro. Un rotulo
    //  pintado no se puede tocar, y este hay que tocarlo. Lo pone
    //  `refreshRack`, desde la misma tabla que las seis tapas de la cara.
    //
    //  Lo unico que queda aqui es el velo del fader, que depende de si el
    //  efecto de esa ranura esta encendido: un envio apagado no se esconde, se
    //  atenua - lo que pongas ahora es lo que usara cuando lo enciendas.
    for (int s = 0; s < kNumRanuras; ++s)
    {
        if (rackSends[s] == nullptr) continue;
        const int fx = enRanura (s);
        rackSends[s]->setAlpha (fx >= 0 && fxOn[(size_t) fx] ? 1.0f : 0.5f);
    }
}

// Sequencer sheet content: title/chain text + step-selection/playhead rings.
// Called from SeqOverlay::paint() (set as its paintContent callback) so it
// draws in the overlay's own paint pass, on top of everything else.
void MainComponent::paintSeqSheetContent (juce::Graphics& g)
{
    if (seqSheet.sheetBounds.isEmpty()) return;

    const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00B7);
    const int sp = juce::jmax (0, selectedPad);
    auto inner = seqSheet.sheetBounds.reduced (Metrics::lg, Metrics::md);

    //  La pagina del piano tiene su propia cabecera - dice de que PAD son las
    //  notas, que es lo unico que hace falta saber ahi - y su propia ayuda.
    if (seqPage == seqPagePiano)
    {
        paintPianoSheetContent (g);
        return;
    }

    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    const juce::String t = T (seqPage == seqPageStep ? "PATRON" : "PASOS")
                         + "  " + dot + "  " + T ("PAD %1", juce::String (sp + 1))
                         + (padName[(size_t) sp].isNotEmpty() ? "   " + padName[(size_t) sp] : juce::String())
                         + "   " + dot + "   P" + juce::String (selectedPattern + 1);
    //  EL TITULO SE PARA DONDE EMPIEZA LA TAPA DE CERRAR.
    //
    //  El nombre de la muestra entra aqui y no tiene largo: uno importado de
    //  Instagram es "instagram_1786902180894(44.1K)" y el renglon se metia por
    //  debajo del boton de cerrar, con el "P8" del final tapado. Acotado a
    //  donde empieza la tapa, y con drawFittedText, que encoge un poco antes
    //  de rendirse en vez de cortar a mitad de palabra.
    //  antesDe y no setRight: la x esta a la IZQUIERDA en arabe, asi que
    //  recortar siempre por la derecha no recorta nada en uno de los cuatro
    //  idiomas. Es el mismo fallo que ya costo 35 hallazgos en los titulos del
    //  pad y de la mesa, escrito cinco veces mas en sitios que el banco no
    //  podia ver porque nadie apuntaba el rotulo.
    //  Y DE LAS TRES TAPAS DE SU FILA, no solo de la cruz. "1-16" vive en el
    //  renglon del titulo en la pagina de la rejilla, y en 280x653 el titulo
    //  -"PASOS · PAD 64  ARP · P1"- se le metia debajo. No lo veia nadie porque
    //  este rotulo se dibujaba a mano y no lo apuntaba ninguna regla; en cuanto
    //  paso por `apunta`, once hallazgos. La tercera es la puerta de la rejilla
    //  de dieciseis pads, que vive en esta misma cabecera.
    auto tituloRow = antesDe (antesDe (antesDe (inner.removeFromTop (16), seqCloseButton, Metrics::sm),
                                       pianoPadPickBtn, Metrics::sm),
                              seqPistasBtn, Metrics::sm);
    pintaTitulo (g, tituloRow, t, "titulo", false, 0.85f);

    //  The bank selector and the chain toggles used to sit adjacent, look
    //  identical and never say which does what. Now each row is named, and the
    //  chain shows its ACTUAL ORDER — "P1 P1 P2 P3" — instead of eight
    //  switches you have to decode.
    juce::String chainStr;
    if (engine.getChainLength() <= 0)
        chainStr = T ("sin cadena - repite P%1", juce::String (selectedPattern + 1));
    else
    {
        chainStr = T ("cadena: ");
        for (int i = 0; i < engine.getChainLength(); ++i)
            chainStr += "P" + juce::String (engine.getChainSlot (i) + 1) + (i + 1 < engine.getChainLength() ? " " : "");
        if (engine.isPlaying())
            chainStr += "   " + dot + "  " + T ("suena P%1", juce::String (engine.getPlayingPattern() + 1));
    }
    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.10f));
    //  Apuntado al pintarlo y no calculado aparte, para que la banda que se
    //  repinta sea LA MISMA que se dibuja: dos cuentas del mismo rectangulo
    //  en dos sitios distintos es como quedan renglones a medio borrar.
    //  Y SE PARA ANTES DE «1-16». En 280x653 esa tapa baja del renglon del
    //  titulo a este, y el renglon de la cadena se le metia debajo en los
    //  cuatro idiomas: catorce hallazgos en la primera corrida con el rotulo
    //  ya apuntado. Es la misma historia que el titulo del secuenciador, que
    //  solo se apartaba de la cruz.
    //  Y de la CRUZ, que mide un dedo entero -40 px- sobre un renglon de
    //  titulo de 16: en la pantalla estrecha se derrama sobre esta banda. Es
    //  exactamente el mismo caso que ya estaba escrito para el parrafo de AUTO
    //  CHOP, «el boton mide 40 px sobre una fila pintada de 32».
    //  Las TRES de la cabecera y no una: cada una que se dejaba fuera saco su
    //  propio hallazgo -«1-16», luego la cruz, luego «64»- porque las tres
    //  miden un dedo y las tres se derraman sobre esta banda.
    seqChainBand = antesDe (antesDe (antesDe (inner.removeFromTop (14),
                                              seqPistasBtn,    Metrics::sm),
                                     seqCloseButton,  Metrics::sm),
                            pianoPadPickBtn, Metrics::sm);
    apunta (g, seqChainBand, chainStr, "dato");
    g.drawText (chainStr, seqChainBand, juce::Justification::centredLeft);

    //  Every control is named, over the control itself rather than over the
    //  row - two things sharing a line are two different jobs, and one label
    //  stretched across both was how NOTA came to look like part of the chain.
    //
    //  The bands come from resized(), which is the only thing that knows which
    //  page is showing. Deriving them here from each control's bounds drew the
    //  caption of every control on the card whether it was laid out or not:
    //  the moment the sheet grew a second page, PATRON / LARGO / TEMPO were
    //  still being painted - across the chain buttons of the OTHER page, at
    //  whatever coordinates they happened to hold from the last time they were
    //  visible.
    //  LOS GRUPOS, HUNDIDOS. Es lo que separa un panel de una lista.
    //
    //  Esta ficha tenia diez controles con su rotulo, uno detras de otro y
    //  todos sobre el mismo fondo: TEMPO parecia de la misma familia que
    //  PATRON y COMPAS de la misma que PADS. Lo que los agrupa es el sitio -
    //  los que van juntos ya estan juntos - pero eso no se ve si no se dibuja.
    //
    //  Los paneles NO se maquetan: se deducen de las bandas que resized() ya
    //  publica, que son exactamente las que se reservaron en esta pasada. Asi
    //  no cuestan un pixel de alto, que en esta ficha es lo unico que no
    //  sobra - la altura de un carril de la rejilla sale de lo que quede.
    {
        juce::Array<juce::Rectangle<int>> bloques;
        juce::Array<int> grupoDe;          // el `grupo` con el que nacio cada bloque

        for (const auto& lb : seqLabelBands)
        {
            if (lb.band.isEmpty()) continue;
            //  El rotulo y el control que lleva debajo son UNA cosa.
            const int filas = juce::jmax (1, lb.filas);
            const juce::Rectangle<int> b (lb.band.getX(), lb.band.getY(), lb.band.getWidth(),
                                          lb.band.getHeight()
                                            + filas * Metrics::hit + (filas - 1) * Metrics::halfGap);

            //  Se unen los de la MISMA FILA y solo esos: PATRON y LARGO
            //  comparten renglon y su banda esta partida en dos, asi que son un
            //  panel con dos nombres. Los de abajo NO se unen, que fue el
            //  primer intento - entre grupo y grupo hay exactamente el aire de
            //  Metrics::sm, asi que "unir lo que este a menos de sm" unia la
            //  pagina entera en un solo panel y no agrupaba nada.
            bool unido = false;
            for (int i = 0; i < bloques.size(); ++i)
                //  Mismo renglon, o mismo `grupo` explicito. Ver SeqLabel: la
                //  tira del paso son tres filas a cuatro pixeles y tres paneles
                //  ahi salen tocandose, que se lee igual que no dibujar
                //  ninguno.
                if ((lb.grupo != 0 && lb.grupo == grupoDe[i])
                    || (lb.grupo == 0 && grupoDe[i] == 0
                        && std::abs (bloques.getReference (i).getY() - b.getY()) < 3))
                {
                    bloques.getReference (i) = bloques.getReference (i).getUnion (b);
                    unido = true;
                    break;
                }
            if (! unido) { bloques.add (b); grupoDe.add (lb.grupo); }
        }

        pintaPaneles (g, bloques);

        g.setColour (ZatiColours::inkDim);
        g.setFont (ZatiColours::labelFont (Metrics::fMeta, 0.20f));

        for (const auto& lb : seqLabelBands)
        {
            auto band = lb.band;
            if (band.isEmpty()) continue;
            band.setWidth (juce::jmax (60, band.getWidth()));
            g.drawText (T (lb.key), band.translated (2, 0), Lang::start());
        }
    }

    //  The step page acts on ONE step, and until you have tapped one there is
    //  nothing for NOTA or GOLPE to act on. Saying so is the difference between
    //  a control that looks broken and a control that is waiting.
    //  Y solo mientras esta pagina siga teniendo algo del paso: con la tira de
    //  dos filas puesta, "toca un paso en PASOS para editarlo" mandaba a la
    //  otra pagina a hacer algo que ya no se hace aqui.
    if (seqPage == seqPageStep && seqTiraFilas < 2 && ! seqFootArea.isEmpty())
    {
        g.setColour (ZatiColours::inkDim.withAlpha (selectedStep < 0 ? 0.95f : 0.75f));
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.10f));
        const auto pieSec = selectedStep < 0
                              ? T ("toca un paso en la rejilla y sus mandos salen debajo")
                              : T ("editando el paso %1", Lang::ltr (juce::String (selectedStep + 1)));
        apunta (g, seqFootArea, pieSec, "dato");
        g.drawText (pieSec, seqFootArea, Lang::start());
    }

    // The grid paints its own playhead and lane colours (see StepGrid).
    // Ring the bank being edited on the chain-include row.
    if (auto* b = patternButtons[selectedPattern]; b != nullptr && b->isVisible())
    {
        g.setColour (ZatiColours::ink.withAlpha (0.7f));
        g.drawRect (b->getBounds(), 2);
    }
}

//  EL TITULO DE AJUSTES, con la pagina detras.
//
//  "AJUSTES  ·  MIDI" y no "MIDI" a secas: esta ficha tiene cuatro paginas y
//  cada una se titulaba con SU nombre -cuando se titulaba-, asi que tocabas
//  AJUSTES y aterrizabas en una tarjeta que decia "AUDIO". Es la gramatica que
//  RACK, XY y AUTO CHOP ya usan: donde estas y sobre que actuas.
void MainComponent::paintSetTitle (juce::Graphics& g)
{
    if (setSheet.sheetBounds.isEmpty()) return;

    //  Del CUERPO y no de la tarjeta: esta ficha se desplaza.
    auto banda = setSheet.cuerpo.getLocalBounds().removeFromTop (16);

    //  Y SE PARA ANTES DE LA TAPA DE CERRAR, en el lado en que este.
    //  El titulo se pintaba en la banda ENTERA y la x vive en el mismo
    //  renglon: "AJUSTES - AUDIO" pasaba por debajo de ella. En arabe la x
    //  esta a la IZQUIERDA, asi que recortar siempre por la derecha habria
    //  arreglado tres idiomas y roto el cuarto - es el mismo fallo que ya tuvo
    //  el subtitulo de PROYECTOS.
    banda = antesDe (banda, setCloseButton);
    const juce::String punto = juce::String::charToString ((juce::juce_wchar) 0x00B7);
    const char* pag = setPage == pageAudio ? "AUDIO"
                    : setPage == pageMidi  ? "MIDI"
                    : setPage == pageAspecto ? "ASPECTO"
                    : setPage == pageProjects ? "PROYECTOS" : "GESTOS";

    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    pintaTitulo (g, banda, T ("SET") + "  " + punto + "  " + T (pag), "titulo", true);
}

void MainComponent::paintSongSheetContent (juce::Graphics& g)
{
    if (songSheet.sheetBounds.isEmpty()) return;

    //  LOS TRES GRUPOS, sin rotulo. Ver resized(): la paleta dice QUE se pinta,
    //  las brochas COMO se pinta y las nueve herramientas que le pasa a lo que
    //  ya esta puesto, y las tres filas se leian como una escalera de tapas
    //  porque estan una debajo de otra. Ver pintaPaneles.
    pintaPaneles (g, songGrupos);

    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    pintaTitulo (g, antesDe (antesDe (songSheet.sheetBounds.reduced (14, 10).removeFromTop (16),
                                     songCloseButton),
                             songVistaBtn), T ("SONG"));

    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.10f));
    const juce::String hint = songBrush == 0 ? "toca un bloque para borrarlo"
                            : songBrush < 0  ? "toca un compas para soltar el sonido"
                                             : "toca un compas para poner el patron";
    //  The close button lives in this same row, so the hint has to stop short
    //  of it - right-aligning into the full width ran the sentence underneath
    //  the X and off the card. Fitted, so a longer wording shrinks instead of
    //  losing its last word.
    //  Y LA PISTA SE APARTA TAMBIEN DEL INTERRUPTOR DE VISTA, que vive en este
    //  mismo renglon desde que la linea de tiempo tiene dos vistas. Con una
    //  sola llamada a `antesDe` se apartaba solo de la cruz y se metia debajo
    //  de la tapa: el banco lo canto en la primera corrida - 40 TAPADO, «toca
    //  un compas para poner el patron» debajo de «PATRONES» en los cuatro
    //  idiomas. Dos tapas en un renglon son dos escalones, y `antesDe` decide
    //  el lado comparando los centros, asi que encadenarlas vale en los cuatro
    //  idiomas y no solo en tres.
    auto hintRow = antesDe (antesDe (songSheet.sheetBounds.reduced (14, 10).removeFromTop (16),
                                     songCloseButton),
                            songVistaBtn);
    apunta (g, hintRow, hint, "dato");
    g.drawFittedText (hint, hintRow, juce::Justification::centredRight, 1, 0.85f);
}

void MainComponent::paintTourSheetContent (juce::Graphics& g)
{
    //  EL FOCO. Se oscurece la maquina entera MENOS lo que se esta explicando,
    //  con cuatro rectangulos alrededor del hueco en vez de un Path con agujero:
    //  cuatro fillRect son cuatro operaciones que cualquier GPU hace de un
    //  tiron, y un path con regla par-impar sobre una ventana entera se pinta
    //  treinta veces por segundo mientras el transporte rueda.
    auto todo = tourSheet.getLocalBounds();
    const auto foco = tourFoco;
    g.setColour (juce::Colours::black.withAlpha (0.72f));
    if (foco.isEmpty())
    {
        g.fillRect (todo);
    }
    else
    {
        g.fillRect (todo.withBottom (foco.getY()));
        g.fillRect (todo.withTop (foco.getBottom()));
        g.fillRect (juce::Rectangle<int> (todo.getX(), foco.getY(),
                                          foco.getX() - todo.getX(), foco.getHeight()));
        g.fillRect (juce::Rectangle<int> (foco.getRight(), foco.getY(),
                                          todo.getRight() - foco.getRight(), foco.getHeight()));

        //  El anillo, en el acento: es lo unico que dice "esto de aqui" y tiene
        //  que leerse sobre cualquiera de las cuatro carcasas, asi que se elige
        //  el color del sistema y no un gris.
        g.setColour (ZatiColours::accent);
        g.drawRoundedRectangle (foco.toFloat().expanded (2.0f), 3.0f, 2.0f);
    }

    //  EL NUMERO DEL PASO, junto al control y no dentro: dentro taparia
    //  justamente lo que se senala. Se pone en el borde que tenga sitio, y si
    //  no hay objetivo no se pinta - un numero suelto en mitad de la pantalla
    //  no senala nada.
    if (! foco.isEmpty())
    {
        const int d = 26;
        juce::Rectangle<int> chapa (d, d);
        if (foco.getY() - d - 6 >= todo.getY())        chapa.setPosition (foco.getX() - 2, foco.getY() - d - 6);
        else if (foco.getBottom() + d + 6 <= todo.getBottom()) chapa.setPosition (foco.getX() - 2, foco.getBottom() + 6);
        else                                            chapa.setPosition (foco.getX() + 6, foco.getY() + 6);

        g.setColour (ZatiColours::accent);
        g.fillEllipse (chapa.toFloat());
        g.setColour (ZatiColours::textOn (ZatiColours::accent));
        g.setFont (ZatiColours::monoFont (12.0f, true));
        g.drawText (juce::String (tourPaso + 1), chapa, juce::Justification::centred);
    }

    //  EL MUELLE. Su sitio lo decidio resized(), en la mitad contraria a la del
    //  objetivo. Aqui solo se pinta.
    if (tourDock.isEmpty()) return;
    auto inner = tourDock.reduced (Metrics::lg, Metrics::md);

    g.setColour (ZatiColours::chassisTop);
    g.fillRect (tourDock);
    g.setColour (ZatiColours::ink.withAlpha (0.85f));
    g.drawRect (tourDock, 1);

    auto titleRow = inner.removeFromTop (16);
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    //  Por pintaTitulo y no por drawText: era el UNICO titulo de ficha que no
    //  publicaba su banda, o sea el unico que el volcado de rotulos no veia -y
    //  por tanto invisible para la regla de «ningun rotulo pintado debajo de un
    //  control» y para `Tests/plano.py`, que llevaba avisando de dos fichas sin
    //  titulo y no podia avisar de esta tercera porque el aviso se saca de lo
    //  apuntado.
    pintaTitulo (g, titleRow, T (ZatiTour::titulos[tourPaso]), "titulo", true);

    //  Los puntos, por el otro lado del titulo: dicen cuantos quedan, que es la
    //  unica pregunta que alguien se hace en el primer paso de algo que no ha
    //  pedido. Son un indicador y no un control - para moverse estan ATRAS y
    //  SIGUIENTE, que miden lo que mide un dedo; un punto de siete pixeles no
    //  se puede acertar y fingir que si es peor que no tenerlos.
    {
        auto marca = Lang::takeEnd (titleRow, kTourPasos * 9);
        for (int i = 0; i < kTourPasos; ++i)
        {
            auto pt = Lang::takeStart (marca, 9).withSizeKeepingCentre (5, 5);
            g.setColour (i == tourPaso ? ZatiColours::accent : ZatiColours::inkDim.withAlpha (0.35f));
            g.fillEllipse (pt.toFloat());
        }
    }

    inner.removeFromTop (Metrics::xs);
    if (tourBodyArea.isEmpty()) return;
    g.setColour (ZatiColours::ink.withAlpha (0.8f));
    g.setFont (tourBodyFont());
    //  Y sin encoger: el ultimo parametro de drawFittedText es cuanto puede
    //  apretar la letra antes que partir, y aqui vale uno a proposito. Un
    //  parrafo que se estrecha para caber es justo lo contrario de lo que este
    //  cambio busca - el muelle mide lo que el texto necesita, asi que caber es
    //  su problema y no el de la letra.
    g.drawFittedText (T (ZatiTour::cuerpos[tourPaso]), tourBodyArea,
                      Lang::start (juce::Justification::top), 8, 1.0f);
}

void MainComponent::paintVstSheetContent (juce::Graphics& g)
{
    if (vstSheet.sheetBounds.isEmpty()) return;

    auto* sb = uiSample[(size_t) juce::jlimit (0, kNumPads - 1, vstPad)].get();
    const int fam = (sb != nullptr) ? sb->familia : -1;
    const int pre = (sb != nullptr) ? sb->preset  : -1;
    const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00B7);

    //  LOS PANELES PRIMERO, que van DEBAJO de todo lo demas. Se deducen del
    //  maquetado -las tres bandas que resized() acaba de publicar- igual que
    //  los de la ficha del secuenciador, asi que no cuestan un pixel de alto:
    //  en esta ficha el alto es lo unico que no sobra, porque el teclado tiene
    //  que poder tocarse con el dedo.
    //
    //  Y POR `pintaPaneles`, que es de donde nunca debieron salir. Estaban
    //  rellenos a mano con `groove (0.16f)` y su propio aire, o sea la misma
    //  regla escrita dos veces con otro color y otro margen: `groove` es una
    //  SOMBRA -en LACA deja el panel a 5.3 de dE contra la tarjeta, por debajo
    //  del 6.0 que este proyecto exige- y ademas no publicaba `UiAudit::panel`,
    //  asi que los tres paneles de esta ficha eran INVISIBLES para
    //  `Tests/paneles.py`: la unica prueba que mide un panel.
    {
        juce::Array<juce::Rectangle<int>> grupos;
        for (const auto& r : { vstPanelCab, vstPanelPre, vstPanelTec })
            if (! r.isEmpty()) grupos.add (r);
        pintaPaneles (g, grupos);
    }

    auto titleRow = antesDe (vstTitleArea, vstCloseButton);
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    pintaTitulo (g, titleRow,
                 T ("INSTRUMENTO") + "  " + dot + "  "
                     + T ("PAD %1", Lang::ltr (juce::String (vstPad + 1))),
                 "titulo", true);

    //  EL DIBUJO DE LA FAMILIA, grande. Es lo que hace que esta ficha se
    //  reconozca antes de leer nada, y es el MISMO dibujo que lleva el pad.
    if (fam >= 0 && ! vstIconArea.isEmpty())
        Iconos::dibuja (g, Iconos::deFamilia (fam), vstIconArea.toFloat(),
                        ZatiColours::ink.withAlpha (0.92f));

    //  Y EL NOMBRE DE LA FAMILIA, solo. El del preset estaba tambien aqui y se
    //  fue a la pantalla: el mismo dato en dos sitios de la MISMA tarjeta, y
    //  uno de ellos lejos de las flechas que lo cambian.
    if (! vstNombreArea.isEmpty())
    {
        g.setColour (ZatiColours::ink);
        g.setFont (ZatiColours::displayFont (juce::jmin (26.0f,
                                                         (float) vstNombreArea.getHeight() * 0.46f)));
        pintaTitulo (g, vstNombreArea.withSizeKeepingCentre (vstNombreArea.getWidth(), 30),
                     fam >= 0 ? T (Sintes::tabla()[fam].nombre) : juce::String ("-"),
                     "titulo", true);
    }

    //  LA PANTALLA DEL PRESET, entre las dos flechas.
    //
    //  Con el numero delante: dieciseis nombres sin cuenta no dicen cuantos
    //  quedan, y una flecha que da la vuelta sin decirlo se lee como una que se
    //  ha quedado atascada. Y CENTRADO dentro del cristal, que es lo que hace
    //  que se lea como una pantalla y no como un rotulo suelto.
    if (! vstPreArea.isEmpty())
    {
        const juce::String txt = (fam >= 0 && pre >= 0)
            ? Lang::ltr (juce::String (pre + 1) + "/" + juce::String (Sintes::kPresets))
                  + "   " + juce::String (Sintes::tabla()[fam].p[pre].nombre)
            : juce::String ("-");

        g.setColour (ZatiColours::screenBg);
        g.fillRoundedRectangle (vstPreArea.toFloat(), 3.0f);
        g.setColour (ZatiColours::lcdDim);
        g.drawRoundedRectangle (vstPreArea.toFloat().reduced (0.5f), 3.0f, 1.0f);

        g.setColour (ZatiColours::lcdFg);
        g.setFont (ZatiColours::monoFont (Metrics::fLabel, true).withExtraKerningFactor (0.06f));
        //  Se APUNTA lo que el texto ocupa, como hace pintaTitulo: un rotulo
        //  pintado no es un componente y sin esto la regla que comprueba que
        //  ningun rotulo cae debajo de una tapa no lo ve. Centrado, asi que el
        //  rectangulo real es el centrado y no el de la izquierda.
        const int usado = juce::jmin (vstPreArea.getWidth(),
                                      (int) std::ceil (juce::GlyphArrangement::getStringWidth (
                                                           g.getCurrentFont(), txt)));
        UiAudit::rotulo (vstPreArea.withSizeKeepingCentre (usado, vstPreArea.getHeight()),
                         txt, "dato");
        g.drawText (txt, vstPreArea, juce::Justification::centred, true);
    }

    //  EL MAPA DE LAS CINCO RAICES, entre OCT - y OCT +.
    //
    //  Un instrumento de esta app son diez zonas: cinco raices -una por octava-
    //  por dos capas de fuerza, y al tocar se elige la mas cercana. Eso es lo
    //  que lo separa de una muestra afinada, y no se veia por ningun sitio: la
    //  ficha decia "OCTAVA 0" y ese cero no dice contra que. Aqui se ve cual de
    //  las cinco va a sonar, que es exactamente lo que las dos flechas mueven.
    if (! vstOctArea.isEmpty())
    {
        const int base = vstTeclado.getBase();
        auto caja = vstOctArea;
        const int n = 5;
        const int w = juce::jmax (8, caja.getWidth() / n);

        //  Y DONDE NO CABE EL MAPA, EL NUMERO. Cinco celdas de menos de veinte
        //  pixeles no son un mapa: son cinco manchas con un digito recortado
        //  dentro. Es la misma escalera que ya deciden BANCO y PADS - lo que no
        //  cabe se cae, y lo que queda dice lo mismo con menos.
        if (w < 20)
        {
            g.setColour (ZatiColours::ink.withAlpha (0.9f));
            g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.10f));
            pintaTitulo (g, caja, Lang::ltr (juce::String (base / 12)), "seccion", true);
        }
        else
        {
            //  La raiz que sonaria: la mas cercana a la nota de abajo del teclado.
            int viva = 0, coste = 1 << 20;
            for (int i = 0; i < n; ++i)
            {
                const int c = std::abs (base - (i - 2) * 12);
                if (c < coste) { coste = c; viva = i; }
            }

            for (int i = 0; i < n; ++i)
            {
                auto celda = caja.removeFromLeft (w).reduced (1, 0);
                const bool on = (i == viva);
                g.setColour (on ? ZatiColours::accent : ZatiColours::groove (0.34f));
                g.fillRoundedRectangle (celda.toFloat(), 2.0f);
                g.setColour (on ? ZatiColours::textOn (ZatiColours::accent)
                                : ZatiColours::inkDim);
                g.setFont (ZatiColours::monoFont (Metrics::fMeta, on));
                g.drawText (Lang::ltr (juce::String (i - 2)), celda, juce::Justification::centred);
            }
        }
    }

    auto inner = vstSheet.cuerpo.getLocalBounds().reduced (Metrics::lg, 0)
                     .withTop (vstPanelTec.isEmpty() ? vstSheet.cuerpo.getHeight() - 40
                                                     : vstPanelTec.getBottom() + Metrics::sm);
    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.06f));
    g.drawFittedText (T ("El teclado suena mientras lo tengas tocado. Las flechas cambian el preset."),
                      inner.removeFromTop (40), Lang::start (juce::Justification::top), 2, 1.0f);
}

//  Each row says three things: which effect, whether it is switched on at
//  all, and how much of THIS pad is going into it. The middle one matters
//  because a send at 100 into an effect whose own MIX is down makes no
//  sound, and without saying so the fader looks broken.
//  The sheet says three things, in the order you need them: what the button is
//  about to do, in words; how many pieces; and the exact list of pads it will
//  write. Nothing here is a surprise by the time the red button is reachable.
//  El titulo de la ficha XY dice las tres cosas que hay que saber sin tocar
//  nada: que efecto estas tocando, si esta sonando ahora mismo, y que hace
//  soltar el dedo. La ultima es la que decide si te atreves a usarlo en medio
//  de un tema.
void MainComponent::paintXySheetContent (juce::Graphics& g)
{
    //  El panel pinta su propia tarjeta: no hay Sheet debajo que la dibuje, y
    //  no la hay a proposito - una tarjeta con velo se traga los toques que
    //  van a los pads, que es justo lo que este panel no puede hacer.
    const auto card = xyPanel.getLocalBounds().toFloat();
    if (card.isEmpty()) return;
    constexpr float rad = 2.0f;

    g.setColour (ZatiColours::groove (0.55f));
    g.fillRoundedRectangle (card.translated (0.0f, 4.0f).withTrimmedBottom (4.0f), rad);
    g.setColour (ZatiColours::chassisTop);
    g.fillRoundedRectangle (card, rad);
    g.setColour (ZatiColours::ink.withAlpha (0.85f));
    g.drawRoundedRectangle (card.reduced (0.75f), rad, 1.5f);

    const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00B7);
    auto inner = xyPanel.getLocalBounds().reduced (Metrics::lg, Metrics::md);

    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    //  Se para antes del interruptor, que ahora comparte fila con el. Sin el
    //  tope, "XY - DLY - EN ESPERA" en arabe pasa por debajo de MOMENTANEO.
    {
        auto row = inner.removeFromTop (16);
        //  Convertidas a coordenadas de la cara: estas dos cuelgan de XyPanel.
        row = antesDe (antesDe (row, getLocalArea (&xyLatchButton, xyLatchButton.getLocalBounds())),
                       getLocalArea (&xyCloseButton, xyCloseButton.getLocalBounds()));
        pintaTitulo (g, row,
                 T ("XY") + "  " + dot + "  " + juce::String (fxDefs[xyFx].name)
                      + "  " + dot + "  " + (fxOn[(size_t) xyFx] ? T ("SUENA") : T ("EN ESPERA")), "titulo", true);
    }

    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.10f));
    {
        const auto ayuda = xyLatch ? T ("se queda donde lo dejes")
                                   : T ("entra al tocar y sale al soltar");
        //  Y se aparta de MOMENTANEO, que cuelga de XyPanel: sus limites hay
        //  que convertirlos a las coordenadas de la banda, que es la misma
        //  trampa que ya costo que el titulo del XY se metiera diez pixeles
        //  debajo de esa tapa.
        const auto banda = antesDe (inner.removeFromTop (14),
                                    getLocalArea (&xyLatchButton, xyLatchButton.getLocalBounds()));
        apunta (g, banda, ayuda, "dato");
        g.drawText (ayuda, banda, Lang::start());
    }

}
