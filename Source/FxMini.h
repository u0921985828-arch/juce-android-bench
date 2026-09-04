#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "AudioEngine.h"
#include "FxVisor.h"
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

    //  Para el banco: lo que se acaba de muestrear. Ver Tests/rack.py.
    const FxVisor::Curva& puntos() const noexcept { return curva; }

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

    static bool deTiempo (int f) noexcept { return FxVisor::deTiempo (f); }

private:
    int fx = -1;
    FxVisor::Curva curva {};
    FxVisor::Curva pintado {};

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
