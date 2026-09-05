#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "Lang.h"
#include "AudioEngine.h"

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

    void setSamples (const float* src, int n, double dtMs)
    {
        count = juce::jmin (n, kCap);
        float pk = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            const float s = src[i];
            buf[i] = s;
            pk = juce::jmax (pk, std::abs (s));
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
        const bool silent = (pk <= 0.0f && peak < 0.0005f);
        if (silent && wasSilent) return;

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
    //  tapa la rejilla. La banda mide 22 px y las dos filas del master ocupan
    //  hasta la 13: la tercera cabe entera sin pedir un pixel.
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
            auto band = b.reduced (8.0f, 0.0f).removeFromBottom (kMeterBand).reduced (0.0f, 3.0f);

            //  The tempo takes the right end - the corner "SIG" used to
            //  occupy, and the one number you look for without looking away
            //  from what you are playing.
            auto bpmCell = band.removeFromRight (68.0f);
            g.setColour (ZatiColours::lcdDim);
            g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
            g.drawText (Lang::ltr (juce::String (bpm, 1) + " BPM"), bpmCell,
                        juce::Justification::centredRight);

            band.removeFromRight (8.0f);
            auto gutter = band.removeFromLeft (10.0f);

            g.setColour (ZatiColours::lcdFg.withAlpha (0.55f));
            g.setFont (ZatiColours::monoFont (Metrics::fTiny, true));
            g.drawText ("L", gutter.withHeight (7.0f).withY (band.getY()), juce::Justification::centredLeft);
            g.drawText ("R", gutter.withHeight (7.0f).withY (band.getY() + 7.0f), juce::Justification::centredLeft);

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

            auto row = [&] (float level, bool clipHeld, juce::Rectangle<float> r)
            {
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

            row (vuL, clipL > 0, band.withHeight (5.0f).withY (band.getY() + 1.0f));
            row (vuR, clipR > 0, band.withHeight (5.0f).withY (band.getY() + 8.0f));

            //  Y LA TERCERA, la del canal, solo cuando hay uno que mirar: con
            //  una ficha abierta encima el motor deja de medir y una tira
            //  clavada en el suelo se lee como un canal mudo y no como uno que
            //  nadie esta midiendo.
            if (canalNum >= 0)
            {
                //  Dos cifras siempre, como el rotulo del selector de pad: un
                //  «9» que pasa a «10» cambia de ancho, y aqui el hueco son
                //  diez pixeles.
                g.setColour (ZatiColours::lcdFg.withAlpha (0.55f));
                g.setFont (ZatiColours::monoFont (Metrics::fTiny, true));
                g.drawText (Lang::ltr (juce::String (canalNum + 1).paddedLeft ('0', 2)),
                            gutter.withHeight (7.0f).withY (band.getY() + 14.0f),
                            juce::Justification::centredLeft);
                row (canalVu, false, band.withHeight (5.0f).withY (band.getY() + 15.0f));
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

    //  How much of the panel the meter band takes along the bottom: two rows
    //  of segments, their L/R gutter, and air above and below.
    static constexpr float kMeterBand = 22.0f;

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
