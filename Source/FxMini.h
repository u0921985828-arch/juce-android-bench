#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "AudioEngine.h"
#include "Eq5.h"
#include "Dinamica.h"
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
//  LAS FORMULAS NO SE ESCRIBEN AQUI DOS VECES. El EQ pregunta a `Eq5`, que es
//  la misma clase que suena; el barrido de FLT sale de `AudioEngine::barridoDe`
//  y la rodilla de la dinamica de `Dinamica::bajaDb`, las dos extraidas en esta
//  misma tanda por tener un segundo cliente. Lo que si vive aqui es lo que ES
//  su propia definicion —un `min`, un escalon, una escalera de cuantizacion—.
// ============================================================================
class FxMini : public juce::Component
{
public:
    //  Cuarenta y ocho puntos y no doscientos: la tira mide 26 px de alto y en
    //  la pantalla mas estrecha unos 130 de ancho, asi que por encima de eso se
    //  muestrea mas fino que el pixel. Y es lo que el banco compara.
    static constexpr int kPuntos = 48;

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

    //  LOS TRES MANDOS DE AHORA. `eq` solo hace falta para el ecualizador y es
    //  el ESPEJO de la cara, nunca el `Eq5` del motor: ese lo lee el hilo de
    //  audio y preguntarle desde aqui seria leer sus coeficientes mientras los
    //  recalcula.
    void refresca (float p0, float p1, float p2, Eq5* eq)
    {
        //  MIX no entra: dice CUANTO de esto se oye, no que forma tiene. Lo
        //  que dibuja «cuanto» es el fader que hay justo debajo.
        juce::ignoreUnused (p2);
        muestrea (p0, p1, eq);

        bool movio = false;
        for (int i = 0; i < kPuntos && ! movio; ++i)
            movio = std::abs (curva[(size_t) i] - pintado[(size_t) i]) > 0.004f;
        if (! movio) return;

        pintado = curva;
        repaint();
    }

    //  Para el banco: lo que se acaba de muestrear. Ver Tests/rack.py.
    const std::array<float, kPuntos>& puntos() const noexcept { return curva; }

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

        g.setColour (ZatiColours::lcdFg);
        if (deTiempo (fx)) pintaBarras (g, dentro);
        else               pintaCurva  (g, dentro);
    }

    //  LOS DOS QUE SUMAN DIBUJAN TIEMPO Y LOS NUEVE QUE SUSTITUYEN DIBUJAN
    //  NIVEL. No es la misma pregunta que `AudioEngine::sustituye` aunque hoy
    //  den lo mismo: aquella dice lo que el motor HACE con el camino seco y
    //  esta dice que forma tiene el dibujo. Escritas por separado a proposito,
    //  o el banco no podria comprobar la primera contra la segunda.
    static bool deTiempo (int f) noexcept
    {
        return f == AudioEngine::kFxDly || f == AudioEngine::kFxRev;
    }

private:
    int fx = -1;
    std::array<float, kPuntos> curva {};
    std::array<float, kPuntos> pintado {};

    //  ------------------------------------------------------------------
    //  EL MUESTREO. Cada tipo deja `curva` en 0..1, donde 0 es el renglon de
    //  abajo. Lo que significa el eje X cambia con la familia y esa es la
    //  gracia: en un filtro es la frecuencia, en una transferencia el nivel de
    //  entrada, y en un eco el tiempo.
    void muestrea (float p0, float p1, Eq5* eq)
    {
        auto pon = [this] (int i, float v) { curva[(size_t) i] = juce::jlimit (0.0f, 1.0f, v); };

        //  EL DELAY SE ESCRIBE POR ECOS Y NO POR MUESTRA, que es la unica
        //  familia cuyo dibujo son PUNTOS y no una funcion continua: buscar
        //  «¿cae un eco en esta columna?» desde dentro del bucle es la misma
        //  cuenta al reves y sale un tren que aparece y desaparece segun el
        //  redondeo. La ventana son dos segundos, que es lo que cabe de un
        //  delay de un segundo con su primera repeticion.
        if (fx == AudioEngine::kFxDly)
        {
            curva.fill (0.0f);
            const float ms  = juce::jlimit (20.0f, 1000.0f, p0);
            const float fbk = juce::jlimit (0.0f, 0.95f, p1);
            for (int k = 1; k < 64; ++k)
            {
                const float seg = (float) k * ms;
                if (seg > 2000.0f) break;
                const int i = juce::jlimit (0, kPuntos - 1,
                                            (int) std::round (seg / 2000.0f * (float) (kPuntos - 1)));
                curva[(size_t) i] = juce::jmax (curva[(size_t) i], std::pow (fbk, (float) (k - 1)));
            }
            return;
        }

        //  De 20 Hz a 20 kHz en logaritmico, que es el mismo eje que ya usa la
        //  curva del EQ. Ver EqCurve::xDe.
        auto hzDe = [] (float t)
        {
            const float lo = std::log (Eq5::kFreqMin), hi = std::log (Eq5::kFreqMax);
            return std::exp (lo + t * (hi - lo));
        };
        //  De -60 a 0 dB de entrada, que es donde vive un umbral.
        auto dbDe = [] (float t) { return -60.0f + 60.0f * t; };
        //  Una campana de un polo, en modulo: lo que se ve de un filtro.
        auto polo = [] (float hz, float corte, bool alto)
        {
            const float x = hz / juce::jmax (1.0f, corte);
            const float m = alto ? x / std::sqrt (1.0f + x * x)
                                 : 1.0f / std::sqrt (1.0f + x * x);
            return juce::Decibels::gainToDecibels (juce::jmax (1.0e-4f, m));
        };
        //  -24..+12 dB de respuesta repartidos en el alto de la tira.
        auto dbAAlto = [] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 24.0f) / 36.0f); };

        for (int i = 0; i < kPuntos; ++i)
        {
            const float t = (float) i / (float) (kPuntos - 1);

            switch (fx)
            {
                case AudioEngine::kFxFlt:
                {
                    //  Ver AudioEngine::barridoDe: el reparto es suyo.
                    const auto b = AudioEngine::barridoDe (p0);
                    pon (i, ! b.activo ? dbAAlto (0.0f)
                                       : dbAAlto (polo (hzDe (t), b.hz, b.alto)
                                                  + (p1 > 1.0f ? resalte (hzDe (t), b.hz, p1) : 0.0f)));
                    break;
                }

                case AudioEngine::kFxHpf:
                    pon (i, dbAAlto (polo (hzDe (t), p0, true)
                                     + (p1 > 1.0f ? resalte (hzDe (t), p0, p1) : 0.0f)));
                    break;

                case AudioEngine::kFxEq:
                {
                    //  LA MISMA FORMULA QUE SUENA. `respuestaEnDb` evalua la
                    //  funcion de transferencia de verdad, no una campana de
                    //  adorno: 0.03 dB de desvio medido contra el motor.
                    //  Y LA SALIDA ENTRA EN LO QUE SE DIBUJA, que es el mismo
                    //  argumento escrito para la curva grande: es una ganancia
                    //  plana, o sea una suma en dB, y dejarla fuera daria una
                    //  miniatura centrada en cero con el efecto subiendo seis
                    //  decibelios. Viene por parametro y no del `Eq5` porque el
                    //  ESPEJO de la cara solo lleva las bandas: la salida vive
                    //  en `fxP[kFxEq][1]` y la aplica el motor.
                    float db = p1;
                    if (eq != nullptr) { eq->refresca(); db += eq->respuestaEnDb (hzDe (t)); }
                    pon (i, dbAAlto (db));
                    break;
                }

                case AudioEngine::kFxDrv:
                {
                    //  La transferencia: `tanh (k x) mk`, con los mismos dos
                    //  numeros que la etapa. Entrada -1..1 en el eje X.
                    const float k  = 1.0f + juce::jlimit (0.0f, 1.0f, p0) * 24.0f;
                    const float mk = 1.0f / (1.0f + juce::jlimit (0.0f, 1.0f, p0) * 2.5f);
                    const float x  = -1.0f + 2.0f * t;
                    pon (i, 0.5f + 0.5f * std::tanh (k * x) * mk);
                    break;
                }

                case AudioEngine::kFxBit:
                {
                    //  La escalera del cuantizador, con sus escalones de
                    //  verdad: `round (x levels) / levels`.
                    const float levels = juce::jmax (1.0f, std::pow (2.0f, juce::jlimit (1.0f, 16.0f, p0)) * 0.5f);
                    const float x = -1.0f + 2.0f * t;
                    pon (i, 0.5f + 0.5f * std::round (x * levels) / levels);
                    break;
                }

                case AudioEngine::kFxCmp:
                case AudioEngine::kFxDss:
                {
                    //  Ver Dinamica::bajaDb: la rodilla vive alli.
                    const auto modo = (fx == AudioEngine::kFxCmp ? Dinamica::compresor : Dinamica::deesser);
                    const float um  = Dinamica::umbralDeDb (modo, p0, p1);
                    const float rr  = Dinamica::ratioDe (modo, p1);
                    const float in  = dbDe (t);
                    pon (i, (in - Dinamica::bajaDb (in - um, rr) + 60.0f) / 60.0f);
                    break;
                }

                case AudioEngine::kFxGte:
                {
                    const float in = dbDe (t);
                    pon (i, in >= p0 ? (in + 60.0f) / 60.0f : 0.0f);
                    break;
                }

                case AudioEngine::kFxLim:
                {
                    const float in = dbDe (t);
                    pon (i, (juce::jmin (in, p0) + 60.0f) / 60.0f);
                    break;
                }

                case AudioEngine::kFxRev:
                {
                    //  La cola: cuanto dura -SIZE- y como se apaga -DAMP-.
                    const float size = juce::jlimit (0.0f, 1.0f, p0);
                    const float damp = juce::jlimit (0.0f, 1.0f, p1);
                    const float tau  = 0.12f + 0.75f * size * (1.0f - 0.55f * damp);
                    pon (i, std::exp (-t / juce::jmax (0.02f, tau)));
                    break;
                }

                default: pon (i, 0.5f); break;
            }
        }
    }

    //  El pico de resonancia de un filtro, que es lo unico que un polo simple
    //  no dice y lo unico que se mueve con el segundo mando de FLT y de HPF.
    static float resalte (float hz, float corte, float q)
    {
        const float d = std::log (hz / juce::jmax (1.0f, corte));
        return 6.0f * std::log (juce::jmax (0.31f, q)) * std::exp (-d * d * 6.0f);
    }

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
