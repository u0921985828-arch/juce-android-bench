#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "Eq5.h"
#include "Zati.h"
#include "Lang.h"

// ============================================================================
//  LA CURVA DEL EQ, Y LA PRIMERA CARA PROPIA DE UN EFECTO.
//
//  Hasta aqui un efecto se tocaba con los tres mandos de la cara, que son de
//  TODOS: CTRL 1, CTRL 2 y MIX. Con dos mandos libres un ecualizador de cinco
//  bandas no cabe -hacen falta diez numeros- asi que este es el efecto que
//  obliga a que un efecto pueda traer su propia superficie y ponerla en el
//  plato donde viven los tres.
//
//
//  UNA CURVA Y NO CINCO FADERES, y es una MEDIDA y no un gusto.
//
//  El plato mide 86 px de alto en las siete pantallas y el mando que hay dentro
//  tiene 52 px. Cinco faderes verticales en 52 px no son un recorrido: son un
//  dedo apoyado, y un deslizador que se ajusta ARRASTRANDO es el que menos
//  puede permitirse ser corto -fallar el agarre no es fallar un toque, es mover
//  otra cosa-. Con una curva cada banda da sus DOS numeros en un solo gesto: la
//  ganancia en Y y la frecuencia en X.
//
//  Y es ademas el idioma que esta app ya tiene en cuatro sitios -la rejilla de
//  pasos, el piano, la linea de tiempo y el pad XY-: un LIENZO que se pinta
//  entero y se acierta con el dedo, no una fila de componentes.
//
//
//  SE COGE EL NODO MAS CERCANO, PERO SOLO DENTRO DE UN DEDO. Es la misma regla
//  que `WaveformDisplay` usa con las asas de recorte. A cinco bandas en la
//  pantalla mas estrecha les tocan 48 px de separacion, o sea por encima del
//  dedo minimo; el limite existe para que un toque en el aire no arrastre la
//  banda del otro extremo. Roto a proposito -sin el limite- un arrastre de
//  arriba abajo por el centro se lleva una banda a +12.00 dB.
//
//
//  Y NO GUARDA NADA de lo que suena. Como el pad XY: es una superficie, y quien
//  tiene los diez numeros es el `Eq5` que se le presta. Aqui solo se lee para
//  pintar y se avisa de lo que el dedo pide - realimentar un valor propio es
//  como se llega a un mando que se mueve solo. Lo unico suyo son las magnitudes
//  del analizador, que no son un ajuste sino una medida de lo que suena AHORA.
//
//
//  EL ANALIZADOR VA DETRAS Y SON DOS. La mancha del fondo es lo que ENTRA al
//  EQ -es la que dice DONDE hay que tocar: ves el pico y pones el nodo encima- y
//  la linea de delante es lo que SALE -la que confirma que la correccion hizo lo
//  que querias-. Los dos en la MISMA rejilla logaritmica que la curva: si no
//  fuese la misma, el pico que ves a la izquierda no estaria donde el nodo que
//  lo corrige.
// ============================================================================
class EqCurve : public juce::Component
{
public:
    //  1024 a 48 kHz son 21 ms de ventana y bines de 47 Hz. Mas resolucion no
    //  se ve: a 412 px de ancho y en escala logaritmica, la primera octava se
    //  lleva 80 px y ahi 47 Hz ya son cuatro pixeles por bin.
    static constexpr int kFft   = 1024;
    static constexpr int kBines = kFft / 2;

    EqCurve()
    {
        setWantsKeyboardFocus (false);
        //  OPACA: se pinta el fondo entero, asi que repintarla no tiene que
        //  repintar el chasis, los dieciseis pads y el cristal que hay debajo.
        //  Con el analizador vivo esto se repinta treinta veces por segundo, y
        //  transparente serian treinta fotogramas de la CARA por segundo - que
        //  es exactamente el derroche que `Tests/cpu.py` existe para cazar.
        setOpaque (true);
        //  Y LAS DOS MEMORIAS EN EL SUELO Y NO A CERO. `std::array {}` deja
        //  ceros, y cero decibelios es FONDO DE ESCALA: el analizador abriria
        //  con las dos curvas pegadas al techo y bajando, que se lee como que
        //  la maquina esta saturando. Es el mismo fallo que el cero de
        //  `padAncho` -un valor por defecto que ademas es un valor valido-.
        suavePre.fill (kPiso);
        suavePost.fill (kPiso);
    }

    //  banda, frecuencia en Hz, ganancia en dB. Lo escribe quien lo reciba.
    std::function<void (int, float, float)> onBanda;
    std::function<void (bool)>              onTouch;
    //  Mantener sobre un nodo abre su menu: el TIPO de filtro y la Q, que son
    //  los dos numeros que un arrastre no puede llevar -un nodo tiene dos ejes
    //  y ya los gasta en donde y cuanto-. Es el mismo reparto que ya tienen las
    //  seis tapas de efecto: tocar hace, mantener ajusta.
    std::function<void (int)>               onNodo;

    //  De donde se lee lo que se pinta. Un puntero y no una copia: la curva que
    //  se dibuja tiene que ser la que SUENA, y una copia se queda vieja en
    //  cuanto el hilo de audio recalcula.
    //
    //  Y NO ES `const`, que es lo que costo una foto. Los coeficientes solo se
    //  recalculaban dentro de `Eq5::procesa` -o sea en el hilo de audio- y el
    //  ESPEJO desde el que la cara dibuja no procesa audio NUNCA, asi que su
    //  bandera `sucio` se quedaba puesta para siempre y `respuestaEnDb` evaluaba
    //  la tabla de la curva PLANA: una raya recta con los cinco nodos movidos.
    //  Con el puntero const no habia forma de pedirle que se pusiera al dia.
    void setFuente (Eq5* e) { fuente = e; repaint(); }

    //  Lo que entra y lo que sale del bus, del hilo de MENSAJES. Aqui se hacen
    //  las dos FFT: en el hilo de audio serian dos transformadas por bloque para
    //  PINTAR, que es trabajo de la cara pagado donde no se puede pagar.
    void setMuestras (const float* pre, const float* post, int n)
    {
        if (pre == nullptr || post == nullptr || n < kFft || ! isVisible()) return;
        analiza (pre,  n, suavePre);
        analiza (post, n, suavePost);
        repaint();
    }

    //  El analizador solo se dibuja si alguien lo alimenta: en un escritorio sin
    //  tarjeta de sonido nadie llama a `setMuestras`, y una mancha clavada en el
    //  suelo se lee como un fallo y no como silencio.
    void ponVivo (bool v) { if (v != vivo) { vivo = v; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        if (r.isEmpty() || fuente == nullptr) return;

        //  El cristal, como el del espectro y el de la rejilla: la curva es un
        //  instrumento de lectura y se lee sobre fondo hundido.
        g.setColour (ZatiColours::screenBg);
        g.fillRoundedRectangle (r, 3.0f);

        auto dentro = r.reduced (2.0f);
        if (dentro.getWidth() < 8.0f || dentro.getHeight() < 8.0f) return;

        //  Los coeficientes al dia ANTES de leerlos. Ver setFuente.
        fuente->refresca();

        pintaRejilla   (g, dentro);
        pintaAnalizador (g, dentro);
        pintaCurva     (g, dentro);
        pintaNodos     (g, dentro);
    }

    //  DOS PUERTAS SOLO PARA EL BANCO. En un escritorio sin bucle de mensajes
    //  el `Timer` no llega a disparar nunca, asi que sin ellas el mantener no
    //  se puede medir por el GESTO -habria que llamar a `onNodo` a mano, que es
    //  justo donde el fallo no existe-. La primera dice si el reloj sigue
    //  armado -o sea si el arrastre lo ha cancelado- y la segunda lo vence
    //  exactamente como lo venceria el sistema.
    bool mantenerArmado() const noexcept { return reloj.isTimerRunning(); }
    void venceElMantener()               { reloj.timerCallback(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        agarrada = masCercana (e.position);
        if (onTouch) onTouch (agarrada >= 0);
        //  El menu de la banda se arma al APOYAR y se cancela en cuanto el dedo
        //  se mueve: si no, cada arrastre acabaria abriendo un menu al soltar.
        bajoDedo = e.position;
        if (agarrada >= 0) reloj.arma (Metrics::holdMs, [this] { avisaNodo(); });
        mueve (e);
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.position.getDistanceFrom (bajoDedo) > 6.0f) reloj.stopTimer();
        mueve (e);
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        reloj.stopTimer();
        agarrada = -1;
        if (onTouch) onTouch (false);
        repaint();
    }

private:
    //  EL SUELO DEL ANALIZADOR. -78 dB deja ver el ruido de fondo de una
    //  grabacion de movil sin que el dibujo se pegue al borde de abajo, y por
    //  encima de -90, donde ya solo hay ruido de cuantizacion.
    static constexpr float kPiso  = -78.0f;
    static constexpr float kTecho =   0.0f;

    struct Reloj : public juce::Timer
    {
        std::function<void()> fn;
        void arma (int ms, std::function<void()> f) { fn = std::move (f); startTimer (ms); }
        void timerCallback() override { stopTimer(); if (fn) fn(); }
    };
    Reloj reloj;
    void avisaNodo() { if (agarrada >= 0 && onNodo) onNodo (agarrada); }

    //  Una ventana de Hann y la FFT. La ventana no es un adorno: sin ella el
    //  corte de los extremos mete faldones en TODOS los bines y el analizador
    //  sale con un suelo plano que no es el de la señal.
    void analiza (const float* datos, int n, std::array<float, kBines>& dst)
    {
        for (int i = 0; i < kFft; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi
                                                     * (float) i / (float) (kFft - 1));
            fftBuf[(size_t) i] = datos[n - kFft + i] * w;
        }
        std::fill (fftBuf.begin() + kFft, fftBuf.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (fftBuf.data());

        //  ATAQUE INSTANTANEO Y CAIDA LENTA, que es lo que hace legible un
        //  analizador: sin la caida lenta el dibujo tiembla treinta veces por
        //  segundo y no se puede leer un pico; sin el ataque rapido, un golpe de
        //  caja no llega a verse. Es el mismo par que gobierna un medidor.
        for (int k = 0; k < kBines; ++k)
        {
            const float mag = fftBuf[(size_t) k] * (2.0f / (float) kFft);
            const float dB  = juce::jmax (kPiso, juce::Decibels::gainToDecibels (mag, kPiso));
            float& s = dst[(size_t) k];
            s = (dB > s) ? dB : s + 0.25f * (dB - s);
        }
    }

    //  El eje X es LOGARITMICO, que es como se oye: repartido lineal, la mitad
    //  del ancho se gasta entre 10 y 20 kHz -donde casi no pasa nada- y todo lo
    //  que importa cae en el primer centimetro. Es la misma cuenta que ya hace
    //  el barrido de FLT.
    static float xDe (float hz) noexcept
    {
        const float lo = std::log (Eq5::kFreqMin), hi = std::log (Eq5::kFreqMax);
        return juce::jlimit (0.0f, 1.0f, (std::log (juce::jmax (1.0f, hz)) - lo) / (hi - lo));
    }
    static float hzDe (float t) noexcept
    {
        const float lo = std::log (Eq5::kFreqMin), hi = std::log (Eq5::kFreqMax);
        return std::exp (lo + juce::jlimit (0.0f, 1.0f, t) * (hi - lo));
    }

    //  Los decibelios de la curva a una y. El 0.92 deja que un +12 no toque el
    //  filo: un nodo pegado al borde no se puede coger.
    static float yDe (float dB, juce::Rectangle<float> dentro) noexcept
    {
        return dentro.getCentreY()
                 - (dB / Eq5::kGainMax) * (dentro.getHeight() * 0.5f) * 0.92f;
    }

    //  LA REJILLA, que es lo que convierte un dibujo en una MEDIDA: sin las
    //  decadas, 1 kHz y 10 kHz caen en sitios que no dicen nada, y sin las
    //  lineas de dB no se sabe si lo que se ve son tres decibelios o diez.
    void pintaRejilla (juce::Graphics& g, juce::Rectangle<float> dentro)
    {
        const float w = dentro.getWidth();

        //  Una por decada y con su rotulo: mas lineas compiten con la curva, y
        //  esos tres son ademas los puntos que cualquiera busca.
        static const float kHz[] = { 100.0f, 1000.0f, 10000.0f };
        static const char* kEt[] = { "100", "1k", "10k" };
        for (int i = 0; i < 3; ++i)
        {
            const float x = dentro.getX() + xDe (kHz[i]) * w;
            g.setColour (ZatiColours::lcdFg.withAlpha (0.10f));
            g.drawVerticalLine ((int) x, dentro.getY(), dentro.getBottom());
            g.setColour (ZatiColours::lcdFg.withAlpha (0.28f));
            g.setFont (ZatiColours::monoFont (8.0f, false));
            g.drawText (kEt[i], juce::Rectangle<float> (x + 2.0f, dentro.getBottom() - 10.0f,
                                                        26.0f, 9.0f),
                        juce::Justification::centredLeft);
        }

        //  Y las de +6 y -6, que son la mitad del recorrido: con solo la de
        //  cero, un realce de tres decibelios y uno de diez se dibujan igual de
        //  "un poco arriba".
        for (float dB : { 6.0f, -6.0f })
        {
            const float y = yDe (dB, dentro);
            g.setColour (ZatiColours::lcdFg.withAlpha (0.07f));
            g.drawHorizontalLine ((int) y, dentro.getX(), dentro.getRight());
        }

        //  La linea de cero, que es lo que convierte una curva en una MEDIDA:
        //  sin ella no se sabe si lo que se ve sube o baja.
        g.setColour (ZatiColours::lcdFg.withAlpha (0.22f));
        g.drawHorizontalLine ((int) dentro.getCentreY(), dentro.getX(), dentro.getRight());
    }

    //  El camino del analizador, en la MISMA rejilla que la curva.
    juce::Path caminoDe (const std::array<float, kBines>& mag,
                         juce::Rectangle<float> dentro, bool cerrado) const
    {
        const float w = dentro.getWidth(), h = dentro.getHeight();
        const int   n = juce::jmax (2, (int) w);
        const float porBin = 48000.0f / (float) kFft;

        juce::Path p;
        if (cerrado) p.startNewSubPath (dentro.getX(), dentro.getBottom());
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) i / (float) (n - 1);
            //  Un pixel de la izquierda cubre menos de un bin y uno de la
            //  derecha cubre cientos, asi que a la derecha se coge el MAXIMO del
            //  tramo: la media aplana los picos agudos justo donde el oido los
            //  nota.
            const int k0 = juce::jlimit (0, kBines - 1, (int) (hzDe (t) / porBin));
            const int k1 = juce::jlimit (k0, kBines - 1,
                                         (int) (hzDe (juce::jmin (1.0f, t + 1.0f / (float) n)) / porBin));
            float dB = kPiso;
            for (int k = k0; k <= k1; ++k) dB = juce::jmax (dB, mag[(size_t) k]);

            const float u = juce::jlimit (0.0f, 1.0f, (dB - kPiso) / (kTecho - kPiso));
            const float x = dentro.getX() + t * w;
            const float y = dentro.getBottom() - u * h;
            if (! cerrado && i == 0) p.startNewSubPath (x, y);
            else                     p.lineTo (x, y);
        }
        if (cerrado)
        {
            p.lineTo (dentro.getRight(), dentro.getBottom());
            p.closeSubPath();
        }
        return p;
    }

    //  LO QUE SUENA, DETRAS DE TODO Y EN DOS CAPAS. La ENTRADA rellena -una
    //  mancha dice "aqui hay energia" sin competir con nada- y la SALIDA en
    //  trazo fino encima. Dos manchas serian una sola mancha; dos lineas se
    //  confunden con la curva del EQ, que tambien es una linea.
    void pintaAnalizador (juce::Graphics& g, juce::Rectangle<float> dentro)
    {
        if (! vivo) return;

        g.setColour (ZatiColours::lcdFg.withAlpha (0.14f));
        g.fillPath (caminoDe (suavePre, dentro, true));

        g.setColour (ZatiColours::lcdFg.withAlpha (0.55f));
        g.strokePath (caminoDe (suavePost, dentro, false), juce::PathStrokeType (1.0f));
    }

    //  LA CURVA, evaluada en el filtro de verdad y no aproximada. Un punto por
    //  pixel: mas no se ve y menos hace escalones en la pendiente de un estante.
    void pintaCurva (juce::Graphics& g, juce::Rectangle<float> dentro)
    {
        const float w  = dentro.getWidth();
        const float y0 = dentro.getCentreY();
        const int   n  = juce::jmax (2, (int) w);

        juce::Path linea, relleno;
        relleno.startNewSubPath (dentro.getX(), y0);
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) i / (float) (n - 1);
            const float y = yDe (fuente->respuestaEnDb (hzDe (t)), dentro);
            const float x = dentro.getX() + t * w;
            if (i == 0) linea.startNewSubPath (x, y);
            else        linea.lineTo (x, y);
            relleno.lineTo (x, y);
        }
        relleno.lineTo (dentro.getRight(), y0);
        relleno.closeSubPath();

        //  RELLENO HASTA LA LINEA DE CERO, que es lo que se pidio -"que se vea
        //  como trabaja"-: una linea dice donde esta la curva y una superficie
        //  dice CUANTO se esta quitando y cuanto poniendo. Y a la linea de cero
        //  y no al borde de abajo, que es lo que hace que un recorte se dibuje
        //  como una mordida y no como menos relleno.
        g.setColour (ZatiColours::accent.withAlpha (0.20f));
        g.fillPath (relleno);
        g.setColour (ZatiColours::accent);
        g.strokePath (linea, juce::PathStrokeType (1.8f));
    }

    //  Y LOS CINCO NODOS, cada uno del zati de su banda. Cinco puntos del mismo
    //  color son cinco puntos; con el color del sistema, la banda que se toca se
    //  reconoce sin leer nada - y es el material que esta casa ya usa para
    //  distinguir dieciseis pads.
    void pintaNodos (juce::Graphics& g, juce::Rectangle<float> dentro)
    {
        const float y0 = dentro.getCentreY();

        for (int b = 0; b < Eq5::kBands; ++b)
        {
            const auto c    = centroDe (b, dentro);
            const bool viva = (b == agarrada);
            const auto col  = Zati::colour (b);

            //  La caida al cero: sin ella, el nodo de una banda plana no dice a
            //  que frecuencia esta cuando la curva pasa lejos de el.
            g.setColour (col.withAlpha (viva ? 0.55f : 0.20f));
            g.drawVerticalLine ((int) c.x, juce::jmin (c.y, y0), juce::jmax (c.y, y0));

            const float rad = viva ? 5.5f : 4.0f;
            //  El halo del color del cristal, que es lo que separa el nodo de la
            //  curva cuando los dos caen encima: sin el, un punto de acento
            //  sobre un trazo de acento es un bulto.
            g.setColour (ZatiColours::screenBg);
            g.fillEllipse (c.x - rad - 1.5f, c.y - rad - 1.5f, 2.0f * rad + 3.0f, 2.0f * rad + 3.0f);
            g.setColour (col);
            g.fillEllipse (c.x - rad, c.y - rad, 2.0f * rad, 2.0f * rad);

            if (viva)
            {
                g.setColour (col.withAlpha (0.75f));
                g.drawEllipse (c.x - 9.0f, c.y - 9.0f, 18.0f, 18.0f, 1.5f);
            }

            //  Y LOS DE PASO LLEVAN SU MARCA. Un nodo clavado en la linea de
            //  cero que no sube ni baja se lee como un nodo roto si no dice por
            //  que: la barra al lado es hacia donde CORTA.
            if (Eq5::esPaso (fuente->tipoDe (b)))
            {
                const float d = (fuente->tipoDe (b) == Eq5::pasoAlto) ? -1.0f : 1.0f;
                g.setColour (col.withAlpha (0.85f));
                g.fillRect (juce::Rectangle<float> (juce::jmin (c.x, c.x + d * (rad + 6.0f)),
                                                    c.y - 1.0f, 6.0f, 2.0f));
            }
        }
    }

    juce::Point<float> centroDe (int b, juce::Rectangle<float> dentro) const
    {
        return { dentro.getX() + xDe (fuente->freqDe (b)) * dentro.getWidth(),
                 yDe (fuente->gainVisible (b), dentro) };
    }

    int masCercana (juce::Point<float> p) const
    {
        if (fuente == nullptr) return -1;
        auto dentro = getLocalBounds().toFloat().reduced (2.0f);
        if (dentro.getWidth() < 8.0f) return -1;

        int mejor = -1; float mejorD = 1.0e9f;
        for (int b = 0; b < Eq5::kBands; ++b)
        {
            const float d = centroDe (b, dentro).getDistanceFrom (p);
            if (d < mejorD) { mejorD = d; mejor = b; }
        }
        //  Dentro de un dedo, como las asas del recorte. Sin el limite, un toque
        //  en el aire arrastraria la banda del otro extremo.
        return mejorD <= (float) Metrics::hit ? mejor : -1;
    }

    void mueve (const juce::MouseEvent& e)
    {
        if (agarrada < 0 || fuente == nullptr || ! onBanda) return;
        auto dentro = getLocalBounds().toFloat().reduced (2.0f);
        if (dentro.getWidth() < 8.0f || dentro.getHeight() < 8.0f) return;

        const float t  = (e.position.x - dentro.getX()) / dentro.getWidth();
        //  Y ACOTADA CONTRA SUS VECINAS, que no es cosa de esta cara: la regla
        //  de que dos bandas no se cruzan es del filtro y vive en Eq5. Escrita
        //  aqui tambien serian dos reglas, y un nodo que salta al otro lado del
        //  vecino no se puede arrastrar.
        const float hz = juce::jlimit (fuente->minDe (agarrada), fuente->maxDe (agarrada),
                                       hzDe (t));

        //  UN TIPO DE PASO NO TIENE GANANCIA. Su nodo se mueve en X y nada mas:
        //  arrastrarlo arriba movia un numero que el filtro ignora, o sea un
        //  gesto que no hace nada. Se conserva lo que hubiera para que cambiar a
        //  campana devuelva la banda donde estaba.
        const float dB = Eq5::esPaso (fuente->tipoDe (agarrada))
                           ? fuente->gainDe (agarrada)
                           : juce::jlimit (-Eq5::kGainMax, Eq5::kGainMax,
                                           (dentro.getCentreY() - e.position.y)
                                             / ((dentro.getHeight() * 0.5f) * 0.92f)
                                             * Eq5::kGainMax);

        onBanda (agarrada, hz, dB);
        repaint();
    }

    Eq5* fuente = nullptr;
    int  agarrada = -1;
    juce::Point<float> bajoDedo;
    bool vivo = false;

    juce::dsp::FFT fft { 10 };                       // 2^10 = 1024
    std::array<float, 2 * kFft> fftBuf {};
    std::array<float, kBines>   suavePre  {};
    std::array<float, kBines>   suavePost {};
};
