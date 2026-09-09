#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "Lang.h"
#include "AudioEngine.h"
#include "Analizador.h"
#include "UiAudit.h"

// ============================================================================
//  SpectrumDisplay — the "screen", ported from the earlier project's
//  drawSpectrum().
//
//  v232 threw out the bar strip ("Removed the now-dead .lcd-wave bar styling")
//  and put a canvas there instead, showing the MASTER WAVEFORM SILHOUETTE:
//  getByteTimeDomainData over the analyser's whole window - fftSize 32768,
//  about 0.74 s at 44.1 kHz - decimated to one min/max column per 1.7 screen
//  pixels and drawn as a filled band between the two envelopes with a crisp
//  stroked edge above and below. In its own words: "the master's real signal
//  shape, not a slow left-to-right sweep".
//
//  There is no FFT in it at all. The window is long on purpose - at nearly a
//  second you watch a whole phrase land and decay, which is what makes it read
//  as an instrument's screen rather than as a level meter.
//
//  Fed decimated min/max columns by the engine (AudioEngine section 5c); the
//  raw 35000-sample window never crosses to the message thread.
// ============================================================================
class SpectrumDisplay : public juce::Component
{
public:
    SpectrumDisplay() = default;

    //  `dtMs` SON LOS MILISEGUNDOS DE VERDAD desde el cuadro anterior, y no un
    //  parametro de mas: las tres constantes de aqui abajo estaban escritas por
    //  TICK y documentadas contra treinta cuadros por segundo, que es lo que
    //  solo tenia la gama alta. En un movil de gama basica -diez cuadros- el
    //  mismo aviso de clip duraba NUEVE segundos y la aguja caia tres veces
    //  mas lento; desde que el dibujo cuelga del vblank serian ademas 60, 90 o
    //  120 segun el panel. Se aplican con `exp (-dt / tau)`, y los tau son los
    //  factores de siempre resueltos a los 33 ms contra los que se escribieron.
    static constexpr double kTauAgujaMs     = 100.0;   // -33 / ln (0.72)
    static constexpr double kTauRetencionMs = 2200.0;  // -33 / ln (0.985)
    static constexpr double kAvisoClipMs    = 3000.0;  // «~3 s a 30 cuadros»

    void setSamples (const float* src, int n, double dtMs, double fs = 48000.0)
    {
        count = juce::jmin (n, kCap);
        float pk = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            const float s = src[i];
            buf[i] = s;
            pk = juce::jmax (pk, std::abs (s));
        }

        //  Y EL ESPECTRO, QUE ES LO QUE ESTA CLASE PROMETIA Y NO HACIA.
        //
        //  Se llama `SpectrumDisplay` desde el primer dia y dentro no habia
        //  una sola FFT: dibuja la SILUETA de la onda del master por min/max,
        //  y con la maquina callada `colCount` vale cero, asi que lo unico que
        //  quedaba en 145 px de cristal era la linea base al 6 %. Eso es lo
        //  que se ve en la foto que llego del telefono.
        //
        //  `buf` llevaba escribiendose aqui y no lo leia NADIE - mil veinticuatro
        //  floats por cuadro para nada- y es justo la ventana que hace falta.
        //  El analizador es el MISMO que la curva del EQ y los visores del
        //  plato (`Source/Analizador.h`): ventana de Hann, FFT de 1024, ataque
        //  instantaneo con caida de 115 ms por bin y suelo en -78 dB. No hace
        //  falta un anillo nuevo en el hilo de audio: la cara ya recibe las
        //  muestras del master, que son las que dibujan la silueta.
        srHz = fs > 0.0 ? fs : 48000.0;
        if (count >= Analizador::kFft)
        {
            const auto antes = espectro.bines();
            std::copy (antes.begin(), antes.end(), previos.begin());
            espectro.analiza (buf, count, dtMs);
            hayEspectro = true;
        }
        peak = juce::jmax (peak * (float) std::exp (-dtMs / kTauAgujaMs), pk);
        //  RETENCION DE PICO. El medidor cae con 100 ms de constante, asi que
        //  un transitorio que llega a 0 dBFS ha desaparecido de la pantalla
        //  antes de que levantes la vista - y clipar es exactamente lo que hay
        //  que ver. Se retiene el maximo y se suelta despacio: veinte veces mas
        //  lento que la aguja, o sea unos dos segundos.
        hold = juce::jmax (hold * (float) std::exp (-dtMs / kTauRetencionMs), peak);
        if (hold >= 0.999f) clipMs = kAvisoClipMs;
        else if (clipMs > 0.0) clipMs = juce::jmax (0.0, clipMs - dtMs);

        //  A flat line twice running is the same picture, and this is the
        //  biggest component on the face: repainting it thirty times a second
        //  with nothing playing is the app's largest idle cost. v232 gates its
        //  own loop the same way (lcdShouldAnimate).
        //  Y EL ESPECTRO TAMBIEN CAE, asi que la guarda de silencio no puede
        //  mirar solo el nivel: la caida de un bin es exponencial y no llega
        //  al suelo nunca, o sea que con la maquina parada esto repintaria el
        //  cristal -y con el, el chasis y los dieciseis pads- para siempre.
        //  Es exactamente el fallo que `Tests/cpu.py` saco en el analizador
        //  del EQ, de 57.27 ventanas a 2.66, y se arregla igual: se repinta
        //  solo si algun bin se movio mas de una decima de decibelio, que es
        //  la mitad de lo que un pixel de este dibujo representa.
        bool movio = false;
        if (hayEspectro)
        {
            const auto& ahora = espectro.bines();
            for (size_t i = 0; i < ahora.size(); ++i)
                if (std::abs (ahora[i] - previos[i]) > 0.1f) { movio = true; break; }
        }

        const bool silent = (pk <= 0.0f && peak < 0.0005f);
        if (silent && wasSilent && ! movio) return;

        wasSilent = silent;
        repaint();
    }

    //  PARA EL BANCO, y no es un adorno: la regla que faltaba es que la app se
    //  vea IGUAL a 60 y a 120 Hz, y eso se mide en milisegundos de reloj sobre
    //  las dos piezas que de verdad caen — la aguja y el aviso de recorte —.
    //  Sin poder leerlas, la unica forma de comprobarlo seria repetir la
    //  formula en el banco, que es la trampa que este proyecto ya se comio con
    //  la mascara del lanzador: *un banco que repite la constante del codigo
    //  no prueba el codigo*.
    //  Y EL PICO DEL ESPECTRO, que es la cifra con la que el banco puede
    //  preguntar las DOS mitades: que se mueva con señal y que se quede quieto
    //  sin ella. Sale del ANALIZADOR -o sea del DSP- y no de una bandera que
    //  el codigo se ponga a si mismo, que es el fallo de `caraLista`.
    float  picoEspectro() const noexcept
    {
        float m = Analizador::kPiso;
        if (hayEspectro) for (auto v : espectro.bines()) m = juce::jmax (m, v);
        return m;
    }

    float  nivelAguja()  const noexcept { return peak; }
    double avisoClipMs() const noexcept { return clipMs; }

    //  SWIPE THE SCREEN TO CHANGE PATTERN BANK.
    //
    //  Switching bank while playing meant opening SEC, finding the PATRON
    //  stepper, and pressing it - a sheet over the pads, mid-take. The screen
    //  is the biggest thing on the face, it has no other gesture on it, and it
    //  is already where you are looking. A horizontal drag past a third of its
    //  width moves one bank; anything shorter is a tap that missed.
    std::function<void (int)> onSwipe;   // -1 previous, +1 next

    void mouseDown (const juce::MouseEvent& e) override { dragFromX = e.position.x; }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (onSwipe == nullptr || dragFromX < 0.0f) return;
        const float dx = e.position.x - dragFromX;
        dragFromX = -1.0f;

        //  A third of the panel, and never less than 60 px: on a narrow phone
        //  a proportional threshold alone is short enough to fire on a stray
        //  thumb roll.
        const float need = juce::jmax (60.0f, (float) getWidth() * 0.33f);
        if (std::abs (dx) >= need)
            onSwipe (dx > 0.0f ? 1 : -1);
    }

    void setReadout (const juce::String& s) { readout = s; repaint(); }
    void setBpm     (double b)              { bpm = b; }

    //  The engine hands over min/max columns already decimated. Re-bucketing
    //  them into however many the screen is wide is exact: the minimum of a
    //  group of minima IS the minimum.
    void setColumns (const float* mn, const float* mx, int n)
    {
        colCount = juce::jlimit (0, kMaxCols, n);
        for (int i = 0; i < colCount; ++i) { srcMin[i] = mn[i]; srcMax[i] = mx[i]; }
    }

    //  The two meters that used to live outside, on strips of their own above
    //  and below the panel. A hardware sampler puts them ON the screen: the
    //  level and the playhead are things you read WHILE watching the wave, and
    //  splitting them across three separate boxes made the face taller and the
    //  screen smaller for no gain at all.
    void setVu (float l, float r)
    {
        //  EL TESTIGO DE PICO SE ENGANCHA.
        //
        //  Un pico que satura dura un bloque - 1.3 ms a 48 kHz con buffer de
        //  64 - y la cara se repinta cada 60 ms: de cada 45 saturaciones se
        //  ve UNA, y siempre la que menos importa. Un medidor que solo pinta
        //  el nivel de ahora mismo no puede contar que has recortado; por eso
        //  todas las mesas tienen un testigo que se queda puesto. Aqui se
        //  queda 25 vueltas del temporizador, un segundo y medio, que es lo
        //  que se tarda en levantar la vista.
        const bool clipped = (l >= kClipLevel || r >= kClipLevel);
        if (l >= kClipLevel) clipL = kClipHoldTicks;
        if (r >= kClipLevel) clipR = kClipHoldTicks;

        const bool decaying = (clipL > 0 || clipR > 0);
        if (clipL > 0) --clipL;
        if (clipR > 0) --clipR;

        //  ...y mientras el testigo baja hay que repintar aunque el nivel no
        //  se mueva, o se queda encendido para siempre en un silencio.
        if (! clipped && ! decaying
            && std::abs (l - vuL) < 0.002f && std::abs (r - vuR) < 0.002f) return;
        vuL = l; vuR = r;
        repaint();
    }

    //  EL MEDIDOR DEL CANAL, en la MISMA banda y por eso a coste cero de alto.
    //
    //  Era la ultima fila viva de la lente del productor: «hay VU de master; el
    //  nivel de un pad solo se ve en la mesa» — y desde que el canal es lo que
    //  pasa por los efectos, ver uno trabajando obligaba a abrir la mesa, que
    //  tapa la rejilla.
    //
    //  Y AQUI DECIA «la tercera cabe entera sin pedir un pixel», que era falso
    //  y no lo habia medido nadie: la banda pide ahora sus 28 px y no 22. Ver
    //  `kMeterBand`.
    //
    //  Es el del canal del pad ELEGIDO y no los dieciseis: dieciseis tiras en
    //  la cara no caben, y ademas medir los dieciseis costaria sesenta y cuatro
    //  barridos en el hilo de audio para dibujar uno. Ver `AudioEngine::miraCanal`.
    //
    //  Y el numero en el canalon, donde ya estan la L y la R: sin el, la tira
    //  dice que ALGO suena y no de que — que es justo lo que un medidor de
    //  canal existe para decir.
    void setCanal (int canal, float nivel)
    {
        if (canal == canalNum && std::abs (nivel - canalVu) < 0.002f) return;
        canalNum = canal; canalVu = nivel;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        // LCD panel (square) + subtle top scan glow.
        g.setColour (ZatiColours::screenBg);
        g.fillRoundedRectangle (b, 2.0f);
        juce::ColourGradient glow (ZatiColours::lcdFg.withAlpha (0.05f), b.getCentreX(), b.getY(),
                                   ZatiColours::screenBg.withAlpha (0.0f), b.getCentreX(), b.getY() + b.getHeight() * 0.6f, false);
        g.setGradientFill (glow);
        g.fillRoundedRectangle (b, 2.0f);

        //  Scan lines. A black rectangle on paper reads as a hole cut in the
        //  panel; the same rectangle with a line structure in it reads as a
        //  screen switched on. Three pixels apart and barely there - at full
        //  strength it would be a texture competing with the waveform.
        g.setColour (ZatiColours::lcdFg.withAlpha (0.035f));
        for (float y = b.getY() + 2.0f; y < b.getBottom() - 1.0f; y += 3.0f)
            g.fillRect (b.getX() + 1.0f, y, b.getWidth() - 2.0f, 1.0f);

        g.setFont (ZatiColours::monoFont (Metrics::fLabel, true).withExtraKerningFactor (0.08f));

        //  THE SCREEN, REORGANISED.
        //
        //  It carried four pieces of text and a meter, and two of the four
        //  said nothing. "ZATI" is the name of the app, printed on an app you
        //  already opened, on the one surface that should only ever show what
        //  the instrument is DOING. "SIG" was a signal-present light nobody
        //  could name - which is the definition of a light that is not worth
        //  its pixels next to a level meter that says the same thing better.
        //
        //  What is left is arranged by what it is: the NUMBER at the top, the
        //  SHAPE in the middle with everything the other two gave back, and
        //  the METER along the bottom edge with the tempo beside it. Reading
        //  down the screen is now reading from the abstract to the physical.
        auto top = b.reduced (10.0f, 6.0f).removeFromTop (13.0f);
        g.setColour (ZatiColours::lcdFg.withAlpha (0.9f));
        //  El pico retenido, en rojo si toco el techo. Es la unica marca de la
        //  pantalla que dice algo que YA PASO, y por eso se queda: si has
        //  clipado, quieres enterarte aunque estuvieras mirando los pads.
        g.setColour (clipMs > 0.0 ? ZatiColours::red : ZatiColours::lcdDim);
        g.drawText (Lang::ltr (holdDb()), top, Lang::start (juce::Justification::topRight));
        g.setColour (ZatiColours::lcdDim);
        g.drawText (T ("OUT") + " " + Lang::ltr (peakDb()), top, Lang::start (juce::Justification::top));

        //  Waveform area: everything between the readout and the meter band.
        auto wave = b.reduced (8.0f, 0.0f);
        wave.removeFromTop (22.0f);
        wave.removeFromBottom (kMeterBand);
        const float cy = wave.getCentreY();
        const float halfH = wave.getHeight() * 0.5f - 2.0f;

        //  THE SILHOUETTE, with v232's numbers:
        //      BAR_PITCH  1.7 px per column
        //      mid = h/2, yamp = mid - 1
        //      baseline   white at 6%, 1 px
        //      band       globalAlpha 0.20
        //      edges      lineWidth 1.4, round joins and caps
        //      no glow    ("cheaper + cleaner")
        //
        //  Drawn in lcdFg rather than in v232's --accent, because on THIS face
        //  the accent is a near-black chassis colour - painting it on a
        //  near-black screen is the exact bug that left the meter invisible.
        juce::ignoreUnused (halfH);

        //  Y EL BANCO SABE DONDE CAE, que es lo que le hace falta para contar
        //  la tinta del espectro en la foto sin repetir aqui el recorte —
        //  «un banco que repite la constante del codigo no prueba el codigo».
        UiAudit::vuFila (wave.getSmallestIntegerContainer(), "onda");

        //  EL ESPECTRO VA DETRAS Y ATENUADO, y la onda delante: es la misma
        //  gramatica que el visor del plato -«la capa viva DETRAS y atenuada,
        //  lo nitido DELANTE»- y por eso no se inventa otra. Las dos dicen
        //  cosas distintas del mismo master: la silueta dice CUANTO y con que
        //  forma, y el espectro DONDE — que es lo unico que no se puede
        //  deducir mirando una onda.
        //
        //  EJE LOGARITMICO, por lo que ya esta escrito en el analizador: la
        //  primera decada -20 a 200 Hz- se lleva un tercio del ancho, que es
        //  donde vive todo lo que se mezcla. Lineal dejaria las ocho octavas
        //  de abajo en el 2 % del cristal.
        if (hayEspectro)
        {
            const int NC = juce::jlimit (24, kMaxCols, (int) (wave.getWidth() / 3.0f));
            const float lo = std::log (20.0f);
            const float hi = std::log ((float) juce::jmin (20000.0, srHz * 0.5));
            const float suelo = Analizador::kPiso;

            juce::Path esp;
            esp.startNewSubPath (wave.getX(), wave.getBottom());
            for (int i = 0; i < NC; ++i)
            {
                const float t  = (float) i / (float) (NC - 1);
                const float hz = std::exp (lo + t * (hi - lo));
                const float dB = espectro.enHz (hz, srHz);
                const float u  = juce::jlimit (0.0f, 1.0f, (dB - suelo) / (0.0f - suelo));
                esp.lineTo (wave.getX() + t * wave.getWidth(),
                            wave.getBottom() - u * wave.getHeight());
            }
            esp.lineTo (wave.getRight(), wave.getBottom());
            esp.closeSubPath();

            g.setColour (ZatiColours::lcdFg.withAlpha (0.13f));
            g.fillPath (esp);
        }

        {
            const float mid  = cy;
            const float yamp = wave.getHeight() * 0.5f - 1.0f;

            g.setColour (ZatiColours::lcdFg.withAlpha (0.06f));
            g.fillRect (wave.getX(), mid - 0.5f, wave.getWidth(), 1.0f);

            const int NB = juce::jlimit (24, kMaxCols, (int) (wave.getWidth() / 1.7f));

            if (colCount > 0 && NB > 1)
            {
                const float pitch = wave.getWidth() / (float) NB;
                const float per   = (float) colCount / (float) NB;

                float xs[kMaxCols], yUp[kMaxCols], yDn[kMaxCols];

                for (int bi = 0; bi < NB; ++bi)
                {
                    const int s0 = (int) ((float)  bi      * per);
                    const int s1 = juce::jmax (s0 + 1, (int) ((float) (bi + 1) * per));

                    float mn = 1.0e9f, mx = -1.0e9f;
                    for (int i = s0; i < s1 && i < colCount; ++i)
                    {
                        mn = juce::jmin (mn, srcMin[i]);
                        mx = juce::jmax (mx, srcMax[i]);
                    }
                    if (mx < mn) { mn = 0.0f; mx = 0.0f; }   // only the degenerate case

                    xs [bi] = wave.getX() + (float) bi * pitch + pitch * 0.5f;
                    yUp[bi] = mid + juce::jlimit (-1.0f, 1.0f, mn) * yamp;
                    yDn[bi] = mid + juce::jlimit (-1.0f, 1.0f, mx) * yamp;
                }

                //  The band: out along one envelope and back along the other.
                juce::Path band;
                band.startNewSubPath (xs[0], yUp[0]);
                for (int bi = 1; bi < NB; ++bi)  band.lineTo (xs[bi], yUp[bi]);
                for (int bi = NB - 1; bi >= 0; --bi) band.lineTo (xs[bi], yDn[bi]);
                band.closeSubPath();
                g.setColour (ZatiColours::lcdFg.withAlpha (0.20f));
                g.fillPath (band);

                //  ...and the two crisp edges over it.
                juce::Path up, dn;
                up.startNewSubPath (xs[0], yUp[0]);
                dn.startNewSubPath (xs[0], yDn[0]);
                for (int bi = 1; bi < NB; ++bi) { up.lineTo (xs[bi], yUp[bi]); dn.lineTo (xs[bi], yDn[bi]); }

                const juce::PathStrokeType stroke (1.4f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded);
                g.setColour (ZatiColours::lcdFg);
                g.strokePath (up, stroke);
                g.strokePath (dn, stroke);
            }
        }

        //  THE BOTTOM EDGE: the meter, and the tempo beside it.
        //
        //  The meter used to sit under the labels at the top, where it was one
        //  more line of furniture between you and the wave. On the bottom edge
        //  it is where a meter is on every machine that has one, it frames the
        //  screen instead of interrupting it, and the wave gets the sixteen
        //  pixels back.
        {
            auto band = b.reduced (8.0f, 0.0f).removeFromBottom (kMeterBand)
                         .reduced (0.0f, kMeterAire);

            //  The tempo takes the right end - the corner "SIG" used to
            //  occupy, and the one number you look for without looking away
            //  from what you are playing.
            //
            //  Y EN LAS DOS FILAS DEL MASTER, no en la banda entera: desde que
            //  son tres, centrarlo en las tres lo dejaba justo encima de donde
            //  la tercera dice de quien es.
            auto bpmCell = band.removeFromRight (68.0f);
            g.setColour (ZatiColours::lcdDim);
            g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
            g.drawText (Lang::ltr (juce::String (bpm, 1) + " BPM"),
                        bpmCell.withHeight (kMeterFila * 2.0f).withY (band.getY()),
                        juce::Justification::centredRight);

            band.removeFromRight (8.0f);
            auto gutter = band.removeFromLeft (10.0f);

            g.setColour (ZatiColours::lcdFg.withAlpha (0.55f));
            g.setFont (ZatiColours::monoFont (Metrics::fTiny, true));
            g.drawText ("L", gutter.withHeight (kMeterFila).withY (band.getY()), juce::Justification::centredLeft);
            g.drawText ("R", gutter.withHeight (kMeterFila).withY (band.getY() + kMeterFila), juce::Justification::centredLeft);

            const int nSeg = 32;
            const float segW = band.getWidth() / (float) nSeg;

            //  VERDE, AMARILLO, ROJO - y en DECIBELIOS, que es lo que hace
            //  que los tres colores signifiquen algo.
            //
            //  La tira era de dos colores y repartia los 32 segmentos con una
            //  raiz cuadrada del nivel. Ni la raiz ni el 82% al que empezaba
            //  el rojo estaban puestos contra un numero: echando la cuenta,
            //  ese 82% cae en 0.672 de amplitud, que son -3.4 dBFS - bien por
            //  casualidad - y el resto de la tira no cae en ningun sitio que
            //  se pueda nombrar. Con una curva asi no se puede decir "el verde
            //  acaba en -12" porque el verde no acaba en ningun decibelio.
            //
            //  En decibelios cada segmento vale lo mismo - 1.5 dB desde -48 -
            //  y entonces se puede decir donde estan las fronteras y que
            //  significan: verde hasta -12, que es donde se mezcla; amarillo
            //  de -12 a -3, que es el margen que queda; y rojo los dos
            //  ultimos, que ya es el techo. Un medidor de dos colores dice
            //  "vas bien" y "ya es tarde", y le falta justo el aviso.
            const float segDb = -kMeterFloorDb / (float) nSeg;   // 1.5 dB
            const int   segYellow = (int) std::round ((kMeterFloorDb - kYellowDb) / -segDb);
            const int   segRed    = (int) std::round ((kMeterFloorDb - kRedDb)    / -segDb);

            //  Y SE APUNTA LO QUE SE DIBUJA, en la MISMA funcion que lo
            //  dibuja: la primera version tenia el trazado en `row` y un
            //  `apunta` al lado calculando su rectangulo otra vez, o sea la
            //  misma regla escrita dos veces — y la que se quedara vieja
            //  daria un banco en verde con la fila fuera del cristal, que es
            //  exactamente el fallo que esta medida existe para cazar.
            //
            //  Se apunta el PASO ENTERO de la fila y no solo sus segmentos:
            //  lo que mas se salia era el rotulo, que es dos pixeles mas
            //  alto. Ver `UiAudit::vuFila`.
            auto row = [&] (int i, const char* que, float level, bool clipHeld)
            {
                const auto fila = band.withHeight (kMeterFila)
                                      .withY (band.getY() + 1.0f + kMeterFila * (float) i);
                UiAudit::vuFila (fila.getSmallestIntegerContainer(), que);

                const auto r = fila.withHeight (kMeterSeg);

                const float db  = juce::Decibels::gainToDecibels (juce::jlimit (0.0f, 1.0f, level),
                                                                 kMeterFloorDb);
                const int   lit = (int) std::round ((db - kMeterFloorDb) / segDb);

                for (int i = 0; i < nSeg; ++i)
                {
                    //  El color es del SEGMENTO, no del nivel: la tira se lee
                    //  como una regla de colores fijos y por donde va la luz
                    //  se sabe cuanto margen queda. Pintarla toda del color
                    //  del pico -que es lo que hace medio mundo- convierte el
                    //  medidor en una lampara.
                    const auto on = i >= segRed    ? ZatiColours::red
                                  : i >= segYellow ? ZatiColours::yellow
                                                   : ZatiColours::green;

                    //  Y el testigo enganchado enciende los dos ultimos aunque
                    //  el nivel ya haya bajado: ES la unica forma de enterarse
                    //  de un pico de un bloque.
                    const bool alight = i < lit || (clipHeld && i >= segRed);

                    //  Y apagado sigue siendo la tinta de la pantalla al 10%,
                    //  no el color del segmento a media luz: un rojo al 10%
                    //  sobre este cristal es casi negro y un verde al 10% no,
                    //  asi que la parte apagada saldria de tres tonos - una
                    //  tira que parece rota por la mitad.
                    g.setColour (alight ? on : ZatiColours::lcdFg.withAlpha (0.10f));
                    g.fillRect (band.getX() + (float) i * segW + 0.5f, r.getY(), segW - 1.0f, r.getHeight());
                }
            };

            const auto filaY = [&] (int i) { return band.getY() + 1.0f + kMeterFila * (float) i; };

            //  Y EL CRISTAL CON ELLAS, o «se sale» no tendria contra que.
            UiAudit::vuFila (getLocalBounds(), "cristal");

            row (0, "L", vuL, clipL > 0);
            row (1, "R", vuR, clipR > 0);

            //  Y LA TERCERA, la del canal, solo cuando hay uno que mirar: con
            //  una ficha abierta encima el motor deja de medir y una tira
            //  clavada en el suelo se lee como un canal mudo y no como uno que
            //  nadie esta midiendo.
            if (canalNum >= 0)
            {
                row (2, "canal", canalVu, false);

                //  Y DICE DE QUE ES, que es la otra mitad de por que no se
                //  entendia. Cortada por el filo no se leia; entera seguia
                //  siendo un «01» mudo al lado de una L y una R, o sea un
                //  tercer canal del master. La palabra va al filo de la
                //  DERECHA -que en las tres filas esta vacio desde que el BPM
                //  se centra en las dos de arriba- y no al canalon: alli
                //  caben dos cifras y ninguna lengua tiene una palabra de dos
                //  cifras.
                //
                //  Y se pregunta CON EL TEXTO PUESTO, que es la escalera que
                //  ya deciden BANCO, PADS y la cabecera de la cara: si la
                //  palabra no cabe en su idioma, se cae y quedan las dos
                //  cifras en el canalon, que es donde el master pone las
                //  suyas. Nunca las dos cosas, que seria decirlo dos veces.
                //
                //  Y SE PUBLICA POR CUAL DE LAS DOS SALIO, que es lo que
                //  separa una escalera de una linea que imprime OK: hoy la
                //  palabra cabe en las siete pantallas por los cuatro idiomas
                //  -68 px de celda contra los 44 que pide el arabe, que es la
                //  mas larga- asi que la rama corta no la ve nadie a menos que
                //  el banco pueda decir cual se tomo. Se imprime y no se
                //  juzga, como TOUCH: las dos son correctas.
                const auto nn = Lang::ltr (juce::String (canalNum + 1).paddedLeft ('0', 2));
                const auto entera = T ("CANAL") + " " + nn;

                g.setColour (ZatiColours::lcdFg.withAlpha (0.55f));
                g.setFont (ZatiColours::monoFont (Metrics::fTiny, true));

                const auto cajaFila = bpmCell.withHeight (kMeterFila).withY (filaY (2) - 1.0f);
                const auto cajaGut  = gutter.withHeight (kMeterFila).withY (filaY (2) - 1.0f);
                const auto ancho    = [&g] (const juce::String& t)
                { return juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), t); };

                //  Y SE APUNTA LO QUE SE DIBUJA Y SU CAJA, no la palabra larga
                //  y la celda ancha pase lo que pase: la rama corta mete dos
                //  cifras en un canalon de diez pixeles, que es la unica de
                //  las dos que de verdad puede no caber. Publicar siempre la
                //  primera daria un banco en verde justo en el caso que hay
                //  que vigilar.
                const bool cabe = ancho (entera) <= cajaFila.getWidth();
                const auto caja = cabe ? cajaFila : cajaGut;
                const auto dice = cabe ? entera   : nn;

                g.drawText (dice, caja, cabe ? juce::Justification::centredRight
                                             : juce::Justification::centredLeft);

                UiAudit::vuRotulo (dice, (int) std::ceil (ancho (dice)), (int) caja.getWidth());
            }
        }

        // LCD inner bezel.
        g.setColour (ZatiColours::knobEdge.withAlpha (0.25f));
        g.drawRoundedRectangle (b.reduced (1.0f), 2.0f, 1.2f);
    }

private:
    juce::String holdDb() const
    {
        if (hold < 0.0005f) return {};
        return juce::String ((int) juce::Decibels::gainToDecibels (hold)) + "dB";
    }

    juce::String peakDb() const
    {
        if (peak < 0.0005f) return juce::String ("-inf");
        return juce::String ((int) juce::Decibels::gainToDecibels (peak)) + "dB";
    }

    //  Cuanto se lleva la banda del medidor por el filo de abajo: TRES filas
    //  de segmentos, su canalon y el aire de arriba y de abajo.
    //
    //  Y VEINTIDOS ERAN DOS FILAS, NO TRES. El dia que entro la del canal se
    //  escribio al lado que «las dos del master ocupan hasta la 13, asi que la
    //  tercera cabe entera sin pedir un pixel». Los 13 eran ciertos y la
    //  tercera NO cabia: la banda util son 22 - 2*kMeterAire = 16 px, y a paso
    //  de kMeterFila la tercera pide de la 14 a la 21 -o sea CUATRO fuera por
    //  los segmentos y SEIS por el rotulo-. Contra el CRISTAL, que es lo que
    //  el banco mide porque es lo que se ve, la fila se salia TRES pixeles en
    //  las siete pantallas: en el telefono el numero salia partido por el filo
    //  de abajo, que es por lo que nadie podia leerlo y por lo que la pregunta
    //  que llego no fue «que es ese 01» sino «no entiendo que es eso».
    //
    //  Es *una afirmacion sin medida*, otra vez: la cuenta cabia en dos lineas
    //  y no la hizo nadie. Ahora sale de las piezas y no de un numero escrito
    //  a mano, asi que una cuarta fila -si algun dia la hay- no puede volver a
    //  salirse en silencio.
    static constexpr float kMeterFila = 7.0f;    // el paso de una fila
    static constexpr float kMeterSeg  = 5.0f;    // el alto de sus segmentos
    static constexpr float kMeterAire = 3.0f;    // arriba y abajo de la banda
    static constexpr int   kMeterFilas = 3;      // L, R y el canal

    static constexpr float kMeterBand = 1.0f + kMeterFila * (float) kMeterFilas
                                             + 2.0f * kMeterAire;   // 28

    //  El suelo de la tira y donde cambia de color. -48 dB porque por debajo
    //  de eso ya no se decide nada, y 32 segmentos caen justos a 1.5 dB.
    static constexpr float kMeterFloorDb = -48.0f;
    static constexpr float kYellowDb     = -12.0f;
    static constexpr float kRedDb        =  -3.0f;

    //  0.997 son -0.026 dBFS. Lo que llega es la magnitud del bloque despues
    //  del master - ultima etapa, detras del limitador de seguridad - y pedir
    //  1.0 exacto seria comparar floats por igualdad para encender una luz:
    //  la trigesimosegunda parte de un decibelio de margen cuesta nada y hace
    //  que el testigo dependa del nivel y no del redondeo.
    static constexpr float kClipLevel     = 0.997f;
    static constexpr int   kClipHoldTicks = 25;     // 25 x 60 ms = 1.5 s
    int clipL { 0 }, clipR { 0 };

    static constexpr int kCap = 1024;
    float        buf[kCap] {};
    Analizador   espectro;
    Analizador::Bines previos {};
    bool         hayEspectro = false;
    double       srHz = 48000.0;

    int          count { 0 };
    float        peak  { 0.0f };
    float        hold  { 0.0f };
    double       clipMs = 0.0;
    double       bpm   { 120.0 };
    //  Decimated min/max columns from the engine, oldest first.
    static constexpr int kMaxCols = AudioEngine::kMaxScopeColumns;
    float srcMin[kMaxCols] {}, srcMax[kMaxCols] {};
    int   colCount   = 0;

    float        vuL   { 0.0f }, vuR { 0.0f };
    //  Ver setCanal: -1 es «no hay canal que mirar», que no es lo mismo que uno
    //  en silencio.
    int          canalNum { -1 };
    float        canalVu  { 0.0f };
    bool         wasSilent { false };
    juce::String readout { "ZATI" };
    float        dragFromX { -1.0f };
};
