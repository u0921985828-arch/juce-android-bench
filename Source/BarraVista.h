#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"

// ============================================================================
//  BarraVista — la barra que desplaza una rejilla de LIENZO.
//
//  Las tres rejillas que se pintan con el dedo arrastrado —la de pasos, el
//  piano y la linea de tiempo— enseñaban una ventana fija y se cambiaba de
//  ventana con una FILA DE NUMEROS: 1, 2, 3, 4 en el secuenciador y 1, 9, 17…
//  en la playlist. Eso son dos cosas mal a la vez. Una, que un numero no dice
//  cuanto queda ni donde estas dentro del total: para saber si el compas 17 es
//  el ultimo hay que contar las tapas. Y otra, que la ventana salta de compas
//  en compas, asi que una figura que cruza el filo no se puede mirar entera —
//  «cambiar de pantalla todo el rato», que fue como se pidio esto.
//
//  Y NO SE ARRASTRA LA REJILLA. La regla de la casa —«un arrastre vertical que
//  a veces escribe una nota y a veces mueve la pagina es un gesto que no se
//  puede aprender»— se conserva entera: el dedo sobre el lienzo sigue pintando
//  y nada mas. Quien desplaza es esta barra, que es un control aparte y por
//  eso no le quita ningun significado al gesto que ya habia.
//
//  UN COMPONENTE Y NO UN ROTULO PINTADO, que es la diferencia entre entrar en
//  las once reglas del banco —CERO, OFFSCREEN, el solape entre hermanos— y ser
//  invisible para todas ellas. Es la leccion que ya costo que el renglon del
//  rack se metiera debajo de la cruz.
//
//  El estado va en UNIDADES DE LA REJILLA —pasos, compases, semitonos— y no en
//  una fraccion: quien la usa ya piensa en pasos, y traducir a 0..1 en los dos
//  lados serian dos sitios donde el redondeo puede discrepar. El modelo es el
//  de WaveformDisplay, que ya lo tiene resuelto: la posicion se acota a
//  [0, total - visibles] y el pulgar mide `visibles / total` del recorrido.
// ============================================================================
class BarraVista : public juce::Component
{
public:
    //  Cuanto mide de grueso. Se ajusta ARRASTRANDO, y esta casa ya tiene
    //  medido que esos son los que menos pueden permitirse ser finos: fallar el
    //  agarre no es fallar un toque, es no poder mover la vista.
    static constexpr int kGrueso = Metrics::hit;

    void ponEje (bool esVertical) noexcept
    {
        if (esVertical == vertical) return;
        vertical = esVertical;
        repaint();
    }
    bool esVertical() const noexcept { return vertical; }

    //  El estado entero de una vez: sin esto habria tres llamadas y un instante
    //  en el que `primero` esta acotado contra un `total` que ya no es el suyo.
    void ponRango (int primeroNuevo, int visiblesNuevo, int totalNuevo)
    {
        const int t = juce::jmax (1, totalNuevo);
        const int v = juce::jlimit (1, t, visiblesNuevo);
        const int p = juce::jlimit (0, t - v, primeroNuevo);
        if (t == total && v == visibles && p == primero) return;
        total = t; visibles = v; primero = p;
        repaint();
    }

    int getPrimero()  const noexcept { return primero; }
    int getVisibles() const noexcept { return visibles; }
    int getTotal()    const noexcept { return total; }
    //  El ULTIMO que se ve, que es la mitad de la pregunta que el banco hace:
    //  una barra que llega al tope y no enseña el final no sirve de nada, y una
    //  que llega al final sin poder volver al principio tampoco.
    int getUltimo()   const noexcept { return primero + visibles - 1; }

    //  Donde esta el cabezal, en las mismas unidades. -1 es «no hay».
    //
    //  Lo hacia la FILA DE TAPAS: la del compas que sonaba se pintaba de rojo,
    //  que es lo que mantenia orientado a quien editaba el compas 1 mientras
    //  sonaba el 3. Al retirar la fila esa mitad se muda aqui, y aqui dice
    //  ademas DONDE cae sobre el total, que la fila no podia decir.
    void ponCabezal (int pos)
    {
        if (pos == cabezal) return;
        cabezal = pos;
        repaint();
    }

    std::function<void (int primero)> onMueve;

    //  Y SE DIBUJA CON LA GRAMATICA DE LA CASA, no con la de JUCE.
    //
    //  La primera version pintaba un canal redondeado gordo y un pulgar de un
    //  gris sacado de `markOn`, o sea una barra de scroll de serie metida en un
    //  aparato cuyos controles son TAPAS: un bloque de profundidad debajo, la
    //  cara encima y un filo de un pixel. Mirado en la foto no se leia como
    //  parte de la maquina - «estan totalmente fuera asi», que fue la queja.
    //
    //  Las tres piezas salen de donde ya estaban escritas y no se inventa
    //  ninguna: el canal es el mismo `groove` hundido que lleva el hueco de una
    //  celda, el pulgar es una TAPA -`capaDe` para el alto, `kCapLift` para el
    //  bloque, `ZatiColours::key` para la cara y radio 3, exactamente lo que
    //  hace `drawButtonBackground`- y el cabezal es `playhead`, que es el token
    //  del cabezal de tiempo en las tres rejillas. Un control nuevo que elige
    //  sus propios colores es como se acaba con dos paletas.
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const float largo = vertical ? r.getHeight() : r.getWidth();
        if (largo <= 0.0f) return;

        //  EL CANAL, hundido y FINO. Un canal gordo se lee como una segunda
        //  superficie; lo que dice «por aqui corre algo» es una ranura. El
        //  grosor es el del carril de un deslizador de la casa -cuatro
        //  pixeles- y no una fraccion del alto, que con la barra a cuarenta
        //  daba diecisiete y salia una losa.
        const float canal = 4.0f;
        auto pista = vertical
                       ? r.withSizeKeepingCentre (canal, r.getHeight() - 2.0f * Metrics::xs)
                       : r.withSizeKeepingCentre (r.getWidth() - 2.0f * Metrics::xs, canal);
        g.setColour (ZatiColours::groove (0.34f));
        g.fillRoundedRectangle (pista, canal * 0.5f);

        //  EL CABEZAL SOBRE EL TOTAL, y no sobre lo que se ve: es la unica
        //  marca de esta pantalla que dice por donde va la cancion cuando lo
        //  que suena esta fuera de la ventana. Cruza la barra ENTERA y no solo
        //  el canal - dentro de cuatro pixeles no se ve.
        if (cabezal >= 0 && cabezal < total)
        {
            const float t = (float) cabezal / (float) total;
            const float x = vertical ? pista.getY() + pista.getHeight() * (1.0f - t)
                                     : pista.getX() + pista.getWidth() * t;
            g.setColour (ZatiColours::playhead.withAlpha (0.85f));
            if (vertical) g.fillRect (r.getX() + 2.0f, x - 1.0f, r.getWidth() - 4.0f, 2.0f);
            else          g.fillRect (x - 1.0f, r.getY() + 2.0f, 2.0f, r.getHeight() - 4.0f);
        }

        //  EL PULGAR ES UNA TAPA. Misma cuenta que `drawButtonBackground`: la
        //  cara se pinta dentro del blanco del dedo -tres cuartos, centrada-
        //  con su bloque de profundidad debajo.
        //
        //  Y EL EJE QUE SE ADELGAZA ES EL CORTO, no siempre el alto. `capaDe`
        //  recorta la ALTURA, que es lo correcto para una fila de tapas y el
        //  eje equivocado para una barra vertical: alli el lado corto es el
        //  ANCHO, asi que el pulgar salia de cuarenta pixeles de gordo -el
        //  blanco entero del dedo- mientras el de la barra horizontal salia de
        //  treinta. Dos barras hermanas con dos grosores distintos, y la
        //  vertical ademas se lleva ese ancho de lo unico que escasea en el
        //  piano. Se adelgaza el corto y las dos se leen igual.
        auto p = pulgar();
        const float corto  = vertical ? p.getWidth() : p.getHeight();
        const float carne  = juce::jmax ((float) ZatiLookAndFeel::kCapMinH * 0.62f,
                                         corto * ZatiLookAndFeel::kCapFill);
        p = vertical ? p.withSizeKeepingCentre (juce::jmin (corto, carne), p.getHeight())
                     : p.withSizeKeepingCentre (p.getWidth(), juce::jmin (corto, carne));
        const float lift = ZatiLookAndFeel::kCapLift;
        auto cara = p.withTrimmedBottom (lift);

        g.setColour (ZatiColours::groove (0.42f));
        g.fillRoundedRectangle (cara.translated (0.0f, lift), 3.0f);
        g.setColour (ZatiColours::key);
        g.fillRoundedRectangle (cara, 3.0f);
        //  Y una tapa APAGADA lleva una RANURA y no un filo claro: es lo que
        //  la casa dibuja en el estado apagado y lo que separa un pulgar de
        //  una tapa encendida en las cuatro carcasas.
        g.setColour (ZatiColours::groove (0.42f));
        g.drawRoundedRectangle (cara.reduced (0.5f), 3.0f, 1.0f);

        //  Y LAS ESTRIAS SON LAS DE UN MANDO, o sea la tinta MEDIDA sobre la
        //  cara de la tapa y no un gris elegido: es la misma pareja
        //  tapa/marca que gobierna todo lo que se dibuja encima de un boton.
        g.setColour (ZatiColours::markOn (ZatiColours::key, 0.55f));
        const float c = vertical ? cara.getCentreY() : cara.getCentreX();
        const float aire = 5.0f;
        for (int i = -1; i <= 1; ++i)
        {
            if (vertical) g.fillRect (cara.getX() + aire, c + (float) i * 3.0f - 0.5f,
                                      cara.getWidth() - 2.0f * aire, 1.0f);
            else          g.fillRect (c + (float) i * 3.0f - 0.5f, cara.getY() + aire,
                                      1.0f, cara.getHeight() - 2.0f * aire);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const float pos = vertical ? (float) e.y : (float) e.x;
        auto p = pulgar();
        const float ini = vertical ? p.getY() : p.getX();
        const float fin = vertical ? p.getBottom() : p.getRight();

        if (pos >= ini && pos <= fin)
        {
            //  Arrastrar el pulgar: se guarda DONDE se agarro y no se salta al
            //  dedo, que es la misma cuenta que el agarre de un clip - sin ella
            //  el primer pixel de arrastre pega la vista de un salto.
            agarrePx  = pos;
            agarrePri = primero;
            return;
        }

        //  Y un toque en la pista salta UNA PAGINA hacia donde se toco. Saltar
        //  al dedo seria mas directo y es peor: la barra mide el total, asi que
        //  un pixel son varios pasos y un roce se lleva la vista al otro lado
        //  de la cancion.
        agarrePx = -1.0f;
        const bool adelante = vertical ? (pos < ini) : (pos > fin);
        mueve (primero + (adelante ? visibles : -visibles));
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (agarrePx < 0.0f) return;
        const float pos   = vertical ? (float) e.y : (float) e.x;
        const float largo = vertical ? (float) getHeight() : (float) getWidth();
        auto p = pulgar();
        const float libre = largo - (vertical ? p.getHeight() : p.getWidth());
        if (libre <= 0.0f) return;

        //  El recorrido util son los pixeles que el pulgar puede correr, no el
        //  largo de la barra: con el pulgar ocupando media barra, un pixel de
        //  dedo son dos de recorrido.
        const float porPx = (float) (total - visibles) / libre;
        const float d = (pos - agarrePx) * porPx;
        mueve (agarrePri + (int) std::lround (vertical ? -d : d));
    }

    void mouseUp (const juce::MouseEvent&) override { agarrePx = -1.0f; }

private:
    void mueve (int destino)
    {
        const int p = juce::jlimit (0, juce::jmax (0, total - visibles), destino);
        if (p == primero) return;
        primero = p;
        repaint();
        if (onMueve) onMueve (p);
    }

    juce::Rectangle<float> pulgar() const
    {
        auto r = getLocalBounds().toFloat();
        const float largo = vertical ? r.getHeight() : r.getWidth();
        //  El pulgar no baja de un dedo: con sesenta y cuatro compases a la
        //  vista de cuatro, `visibles/total` son 24 px en una barra de 380 y
        //  eso no se agarra. Lo que se pierde es proporcion, y la proporcion la
        //  dice ademas el cabezal.
        const float lp = juce::jlimit ((float) juce::jmin ((int) largo, kGrueso), largo,
                                       largo * (float) visibles / (float) total);
        const int   rec = juce::jmax (1, total - visibles);
        const float t = (float) primero / (float) rec;
        const float x = (largo - lp) * (vertical ? (1.0f - t) : t);
        return vertical ? juce::Rectangle<float> (r.getX(), r.getY() + x, r.getWidth(), lp).reduced (2.0f, 0.0f)
                        : juce::Rectangle<float> (r.getX() + x, r.getY(), lp, r.getHeight()).reduced (0.0f, 2.0f);
    }

    bool  vertical = false;
    int   primero = 0, visibles = 1, total = 1, cabezal = -1;
    float agarrePx = -1.0f;
    int   agarrePri = 0;
};
