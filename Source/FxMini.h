#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "AudioEngine.h"
#include "FxVisor.h"
#include "Analizador.h"
#include "UiAudit.h"

// ============================================================================
//  LA MINIATURA DE UN EFECTO: que es, con los numeros que tiene AHORA.
//
//  La fila del rack eran seis faderes identicos con una tapa de tres letras al
//  lado. Para DLY y REV eso es exactamente lo que son; para el EQ -que trae su
//  propia cara con curva, analizador y cinco tipos de banda- la fila no decia
//  absolutamente nada de lo que hay dentro. La pregunta que una fila de rack
//  tiene que contestar es «¿que le estoy mandando a esto?» y contestaba «un
//  numero».
//
//  NO ES UN ICONO. `iconoDeFx` ya existe y ya esta en el canalon: dice CUAL es
//  el efecto. Esto dice COMO ESTA PUESTO, o sea que es funcion de sus tres
//  parametros vivos y cambia cuando se mueve un mando. Un dibujo fijo aqui
//  seria el icono otra vez, en grande y ocupando alto.
//
//  Y ES UN COMPONENTE, no un rotulo pintado. Lo que se pinta a mano es
//  invisible para las diez reglas de `expo.py` -eso es lo que costo que el
//  renglon del rack se metiera bajo la cruz durante tandas, y los seis rotulos
//  que solo se apuntaron la tanda pasada-. Como componente entra en CERO y en
//  OFFSCREEN y lleva nombre para TalkBack; `TOUCH` no dispara, que esa regla
//  solo mira lo que trae `hit`.
//
//  CON `juce::Path` Y LA TINTA MEDIDA, como los noventa y cuatro iconos: hay
//  cuatro carcasas y en dos de ellas la tinta es clara, asi que un mapa de bits
//  horneado sale invisible en GRAFITO.
//
//  Y SOLO REPINTA SI SE MOVIO. Esta ficha tapa la cara, asi que un `repaint()`
//  incondicional son treinta fotogramas COMPLETOS por segundo -chasis, los
//  dieciseis pads y todo lo que hay debajo del velo-. Es exactamente el fallo
//  que `Tests/cpu.py` acaba de sacar en el analizador del EQ, que pasaba de
//  57.27 ventanas en 8 s a 2.66 con la misma guarda. La curva se muestrea en N
//  puntos y si ninguno se movio no se pide nada.
//
//  Y LO QUE DIBUJA NO VIVE AQUI: esta clase es el estado, la guarda de
//  repintado y el pintado. La FORMA la calcula `FxVisor::muestrea`, que salio
//  fuera del componente el dia que hubo que medirla contra el audio — el banco
//  del motor es una aplicacion de consola y no podia llamar a un metodo
//  privado de un `juce::Component`. Ahi esta escrito lo que cada uno dibuja y
//  de donde sale.
// ============================================================================
class FxMini : public juce::Component
{
public:
    //  Ver FxVisor: el numero de puntos es suyo, que es quien los calcula.
    static constexpr int kPuntos = FxVisor::kPuntos;

    FxMini()
    {
        setInterceptsMouseClicks (false, false);   // se mira, no se toca
        setWantsKeyboardFocus (false);
        curva.fill (0.5f);
        pintado.fill (-1.0f);                      // la primera pasada pinta
    }

    //  QUE EFECTO ES. -1 = ranura vacia: no se dibuja nada, pero la tira sigue
    //  midiendo lo mismo — media fila con dibujo y media sin el se lee como una
    //  celda rota, que es la regla de `filaDeIconos` y de `rejillaDeIconos`.
    void ponTipo (int nuevo)
    {
        if (nuevo == fx) return;
        fx = nuevo;
        pintado.fill (-1.0f);
        repaint();
    }

    int tipo() const noexcept { return fx; }

    //  LOS TRES MANDOS DE AHORA. El `Eq5*` que habia aqui se va con la rama
    //  del EQ: `fxTraeCara` manda -1 a este visor justo para el ecualizador,
    //  porque se lleva el plato entero con su curva, asi que aquel `case`
    //  estaba escrito y no se dibujaba nunca.
    void refresca (float p0, float p1, float p2)
    {
        //  MIX no entra: dice CUANTO de esto se oye, no que forma tiene. Lo
        //  que dibuja «cuanto» es el fader que hay justo debajo.
        juce::ignoreUnused (p2);
        FxVisor::muestrea (fx, p0, p1, curva);

        bool movio = false;
        for (int i = 0; i < kPuntos && ! movio; ++i)
            movio = std::abs (curva[(size_t) i] - pintado[(size_t) i]) > 0.004f;
        if (! movio) return;

        pintado = curva;
        repaint();
    }

    //  ------------------------------------------------------------------
    //  Y LA SEÑAL VIVA, que es la otra mitad.
    //
    //  Lo de arriba dibuja lo que el efecto HARIA con cualquier cosa que le
    //  entre: sale de la misma formula que suena y hay once filas del banco
    //  del motor que lo comprueban. Lo que no decia era si por ahi esta
    //  pasando algo AHORA — un visor perfecto de un bus mudo se lee igual que
    //  uno de un bus que esta trabajando.
    //
    //  Y cada familia tiene su forma de contestarlo, porque cada eje pregunta
    //  otra cosa:
    //
    //    - FRECUENCIA (FLT, HPF): el espectro de lo que SALE, debajo de la
    //      respuesta. Es la gramatica que ya usa la curva grande del EQ y la
    //      de cualquier filtro: aqui esta el sonido, y aqui lo que el filtro
    //      le hace.
    //    - TRANSFERENCIA (DRV, CMP, GTE, DSS, LIM): un PUNTO en la curva,
    //      donde el nivel que entra la cruza. En los cuatro de dinamica su
    //      altura es la reduccion MEDIDA -no la calculada- asi que el punto
    //      cayendo sobre la curva es, ademas, la comprobacion de que lo
    //      dibujado y lo que suena dicen lo mismo.
    //    - TIEMPO (DLY, REV): la cola de VERDAD, una columna cada 42 ms, con
    //      los ecos previstos delante.
    //    - ONDA (BIT): la onda que de verdad esta saliendo cuantizada.
    //
    //  Todas se pintan igual: la capa viva DETRAS y atenuada, lo previsto
    //  DELANTE y con la tinta del cristal. Dos gramaticas distintas en un
    //  visor de 64 px serian dos cosas que aprender.
    //  ------------------------------------------------------------------
    void ponVivo (bool v) { if (v != vivo) { vivo = v; repaint(); } }
    bool estaVivo() const noexcept { return vivo; }

    //  `lfoFase` es la de AHORA del motor, o -1 si este tipo no lleva LFO.
    //  Es el hermano de `reduccionDb`: las dos son cifras que el hilo de audio
    //  MIDE y la cara no puede calcular sin repetir la regla.
    void setMuestras (const float* pre, const float* post, int n,
                      double dtMs, float reduccionDb, float lfoFase = -1.0f)
    {
        if (fx < 0 || pre == nullptr || post == nullptr || n <= 0 || ! isVisible()) return;

        Viva v {};
        if (esFrecuencia (fx))
        {
            if (n < Analizador::kFft) return;
            ana.analiza (post, n, dtMs);
            //  Del espectro a las mismas columnas que la curva, que es lo
            //  unico que hace que un pico se lea DEBAJO del trozo de respuesta
            //  que lo esta tocando.
            for (int i = 0; i < kPuntos; ++i)
            {
                const float t  = (float) i / (float) (kPuntos - 1);
                const float hz = std::exp (std::log (Eq5::kFreqMin)
                                           + t * (std::log (Eq5::kFreqMax) - std::log (Eq5::kFreqMin)));
                //  -78..0 dB repartidos en el alto, que es el recorrido del
                //  analizador y no el de la respuesta: son dos ejes verticales
                //  distintos y mezclarlos seria dibujar una mentira.
                v.col[(size_t) i] = juce::jlimit (0.0f, 1.0f,
                                                  (ana.enHz (hz, 48000.0) - Analizador::kPiso)
                                                  / -Analizador::kPiso);
            }
        }
        else if (fx == AudioEngine::kFxBit)
        {
            //  La onda que sale, en las mismas columnas. Sin analisis: lo que
            //  este visor dibuja ES una onda.
            for (int i = 0; i < kPuntos; ++i)
            {
                const int k = juce::jlimit (0, n - 1,
                                            n - FxVisor::kVentanaBit
                                              + i * (FxVisor::kVentanaBit - 1) / (kPuntos - 1));
                v.col[(size_t) i] = juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * post[k]);
            }
        }
        else if (deTiempo (fx))
        {
            //  LA COLA, una columna cada 42 ms de RELOJ y no por cuadro: el
            //  dibujo cuelga del vblank, asi que por cuadro la cola se leeria
            //  al doble de velocidad en un panel de 120 Hz. Es el mismo fallo
            //  que las cinco constantes visuales tenian en ticks.
            colaMs += dtMs;
            const double paso = (double) FxVisor::kVentanaMs / (double) (kPuntos - 1);
            float pico = 0.0f;
            for (int i = juce::jmax (0, n - 512); i < n; ++i) pico = juce::jmax (pico, std::abs (post[i]));
            picoCola = juce::jmax (picoCola, pico);
            while (colaMs >= paso)
            {
                colaMs -= paso;
                for (int i = kPuntos - 1; i > 0; --i) cola[(size_t) i] = cola[(size_t) i - 1];
                cola[0] = picoCola;
                picoCola = 0.0f;
            }
            //  De izquierda -ahora- a derecha -hace dos segundos-, que es como
            //  el dibujo de los ecos ya reparte el tiempo.
            for (int i = 0; i < kPuntos; ++i) v.col[(size_t) i] = juce::jmin (1.0f, cola[(size_t) i]);
        }
        else if (FxVisor::deModulacion (fx))
        {
            //  MODULACION: el punto viaja por la curva a la fase del MOTOR, y
            //  eso es lo unico que enseña el mando RATE — su eje se mide en
            //  periodos, asi que la curva no puede. La fase la mide el hilo de
            //  audio y la publica `AudioEngine::getLfoFase`; calcularla aqui
            //  con el reloj de la cara seria la misma regla escrita dos veces
            //  y ademas iria a otra velocidad.
            if (lfoFase < 0.0f) return;
            v.punto = true;
            //  La ventana son `kPeriodosMod` periodos, asi que una vuelta del
            //  LFO es esa fraccion del ancho.
            const float t = std::fmod (lfoFase, 1.0f) / FxVisor::kPeriodosMod;
            v.px = juce::jlimit (0.0f, 1.0f, t);
            v.py = curva[(size_t) juce::jlimit (0, kPuntos - 1,
                                                (int) std::lround (v.px * (kPuntos - 1)))];
        }
        else
        {
            //  EL PUNTO DE TRABAJO. El pico de lo que entra dice DONDE cruza
            //  la curva, y en los cuatro de dinamica la reduccion medida dice
            //  a que altura sale.
            float pico = 0.0f;
            for (int i = juce::jmax (0, n - 512); i < n; ++i) pico = juce::jmax (pico, std::abs (pre[i]));
            v.punto = true;
            if (fx == AudioEngine::kFxDrv)
            {
                //  Su eje va de -1 a +1 en amplitud, asi que el pico cae en la
                //  columna (1 + a) / 2 — que es exactamente donde el banco del
                //  motor se equivoco la primera vez.
                v.px = juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * pico);
                v.py = curva[(size_t) juce::jlimit (0, kPuntos - 1,
                                                    (int) std::lround (v.px * (kPuntos - 1)))];
            }
            else
            {
                const float inDb = juce::jlimit (-60.0f, 0.0f,
                                                 juce::Decibels::gainToDecibels (pico, -60.0f));
                v.px = (inDb + 60.0f) / 60.0f;
                v.py = juce::jlimit (0.0f, 1.0f, (inDb - reduccionDb + 60.0f) / 60.0f);
            }
        }

        //  Y SOLO SI SE MOVIO, que es la regla de la casa y la que costo una
        //  medida en `EqCurve::setMuestras`: la caida es exponencial, asi que
        //  con la maquina en silencio esto baja hacia su suelo y no llega
        //  nunca — un `repaint` incondicional aqui son treinta fotogramas
        //  completos por segundo para no cambiar un pixel.
        bool movio = (v.punto != viva.punto)
                  || std::abs (v.px - viva.px) > 0.004f
                  || std::abs (v.py - viva.py) > 0.004f;
        for (int i = 0; i < kPuntos && ! movio; ++i)
            movio = std::abs (v.col[(size_t) i] - viva.col[(size_t) i]) > 0.004f;
        viva = v;
        if (movio) repaint();
    }

    //  Para el banco: lo que se acaba de muestrear. Ver Tests/rack.py.
    const FxVisor::Curva& puntos() const noexcept { return curva; }
    //  Y la capa viva, que es lo unico que separa un visor que mide de uno que
    //  dibuja bien y no mira nada.
    const FxVisor::Curva& vivos()  const noexcept { return viva.col; }
    float puntoX() const noexcept { return viva.punto ? viva.px : -1.0f; }
    float puntoY() const noexcept { return viva.punto ? viva.py : -1.0f; }

    static bool esFrecuencia (int f) noexcept
    { return f == AudioEngine::kFxFlt || f == AudioEngine::kFxHpf; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        if (r.getWidth() < 8.0f || r.getHeight() < 6.0f || fx < 0) return;

        //  ES UN CRISTAL, no un dibujo sobre el chasis. Sin fondo el trazo
        //  flotaba encima del plato y la fila se leia desordenada — mirado en
        //  la foto. Con el par que la app ya usa para todo lo que MUESTRA un
        //  numero -`screenBg` y `lcdFg`, el mismo de las casillas que hay
        //  justo debajo de los tres mandos- se lee como el visor del aparato,
        //  que es lo que es. Y no son tokens nuevos: `Tests/skins.py` ya mide
        //  ese par en las cuatro carcasas.
        g.setColour (ZatiColours::screenBg);
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (ZatiColours::lcdDim.withAlpha (0.55f));
        g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);

        auto dentro = r.reduced (3.0f, 3.0f);
        if (dentro.getWidth() < 6.0f || dentro.getHeight() < 5.0f) return;

        //  El renglon de referencia: el cero de una transferencia, el suelo de
        //  un tren de ecos. Sin el, una curva plana y una curva caida se
        //  dibujan igual de bien y no se sabe cual es cual.
        g.setColour (ZatiColours::lcdDim.withAlpha (0.55f));
        const float base = dentro.getBottom() - 0.5f;
        g.drawLine (dentro.getX(), base, dentro.getRight(), base, 1.0f);

        //  LA CAPA VIVA VA DETRAS Y ATENUADA. Delante taparia lo previsto, que
        //  es lo que este visor existe para decir; y al mismo tono no se
        //  sabria cual es cual.
        if (vivo) pintaVivo (g, dentro);

        g.setColour (ZatiColours::lcdFg);
        if (deTiempo (fx)) pintaBarras (g, dentro);
        else               pintaCurva  (g, dentro);

        //  Y EL PUNTO DE TRABAJO, DELANTE: es la unica pieza viva que no es un
        //  fondo sino una lectura, y detras de la curva no se veria.
        if (vivo && viva.punto)
        {
            const float x = dentro.getX() + dentro.getWidth()  * viva.px;
            const float y = dentro.getBottom() - dentro.getHeight() * viva.py;
            //  Y SU TINTA SE MIDE contra el cristal, que es la regla de la
            //  casa. El acento es lo primero que sale y esta MAL aqui: se
            //  elige para leerse sobre el CHASIS, y este punto cae sobre
            //  `screenBg`. Es el mismo fallo que costo tres rondas con la
            //  letra del icono, por el otro lado.
            g.setColour (ZatiColours::markOn (ZatiColours::screenBg, 1.0f));
            g.fillEllipse (x - 2.6f, y - 2.6f, 5.2f, 5.2f);
            //  Con un anillo del color del cristal alrededor: sobre la curva,
            //  que es del mismo tono, un disco solo se lee como un bulto.
            g.setColour (ZatiColours::screenBg);
            g.drawEllipse (x - 2.6f, y - 2.6f, 5.2f, 5.2f, 1.0f);
        }
    }

    static bool deTiempo (int f) noexcept { return FxVisor::deTiempo (f); }

private:
    //  Lo VIVO en una sola pieza: o son columnas -espectro, onda, cola- o es
    //  un punto de trabajo. Nunca las dos, porque nunca hay una familia que
    //  pregunte las dos cosas.
    struct Viva
    {
        FxVisor::Curva col {};
        bool  punto = false;
        float px = 0.0f, py = 0.0f;
    };

    int fx = -1;
    bool vivo = false;
    FxVisor::Curva curva {};
    FxVisor::Curva pintado {};
    Viva viva {};
    Analizador ana;
    //  La cola, en columnas de 42 ms, y el pico que se esta juntando para la
    //  siguiente. Con el pico y no con la media: una cola se lee por lo que
    //  llega, y promediar un eco con el silencio que tiene al lado lo borra.
    FxVisor::Curva cola {};
    double colaMs = 0.0;
    float  picoCola = 0.0f;

    void pintaCurva (juce::Graphics& g, juce::Rectangle<float> r)
    {
        juce::Path p;
        for (int i = 0; i < kPuntos; ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / (float) (kPuntos - 1);
            const float y = r.getBottom() - r.getHeight() * curva[(size_t) i];
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }

    //  ES UNA MANCHA Y NO UNA LINEA, en las tres familias que la usan: lo
    //  vivo es el fondo sobre el que se lee lo previsto, y una segunda linea
    //  al lado de la primera se lee como dos curvas discutiendo.
    void pintaVivo (juce::Graphics& g, juce::Rectangle<float> r)
    {
        if (viva.punto) return;

        //  La onda de BIT cruza el cero, asi que se rellena desde el CENTRO;
        //  un espectro y una cola no, y se rellenan desde el suelo. Rellenar
        //  la onda desde abajo la convertiria en una envolvente, que es otra
        //  cosa.
        const bool  desdeElCentro = (fx == AudioEngine::kFxBit);
        const float y0 = desdeElCentro ? r.getCentreY() : r.getBottom();

        juce::Path p;
        p.startNewSubPath (r.getX(), y0);
        for (int i = 0; i < kPuntos; ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / (float) (kPuntos - 1);
            p.lineTo (x, r.getBottom() - r.getHeight() * viva.col[(size_t) i]);
        }
        p.lineTo (r.getRight(), y0);
        p.closeSubPath();

        g.setColour (ZatiColours::lcdFg.withAlpha (0.28f));
        g.fillPath (p);
    }

    void pintaBarras (juce::Graphics& g, juce::Rectangle<float> r)
    {
        //  Un tren de ecos y una cola no son una linea: son lo que LLEGA en
        //  cada instante, asi que se dibujan como barras desde el suelo.
        const float w = juce::jmax (1.0f, r.getWidth() / (float) kPuntos - 0.6f);
        for (int i = 0; i < kPuntos; ++i)
        {
            const float v = curva[(size_t) i];
            if (v <= 0.004f) continue;
            const float x = r.getX() + r.getWidth() * (float) i / (float) (kPuntos - 1);
            const float h = r.getHeight() * v;
            g.fillRect (juce::Rectangle<float> (x - w * 0.5f, r.getBottom() - h, w, h));
        }
    }
};
