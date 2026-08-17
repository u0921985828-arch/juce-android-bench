#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "Zati.h"
#include <array>
#include <cstring>

// ============================================================================
//  PianoRoll — las NOTAS de un pad, en una rejilla de tono contra tiempo.
//
//  Un sampler con un piano dentro es un sampler; sin el es una caja de ritmos.
//  Hasta ahora la unica forma de afinar un paso era abrir la pagina PASO y
//  mover el mando NOTA del paso que tuvieras tocado: un semitono, un paso, un
//  viaje. Escribir tres acordes asi son treinta y seis viajes, y por eso nadie
//  los escribia - la maquina tocaba percusion afinada y nada mas.
//
//  Aqui el eje vertical es el TONO y el horizontal el TIEMPO, que es como se
//  lee una melodia desde que existe el pentagrama. Un toque pone una nota; el
//  mismo toque la quita. Varias notas en la misma columna son un acorde, la
//  misma nota en columnas seguidas es un ritmo, y una escalera de notas por
//  columnas es un arpegio: las tres cosas que se pidieron salen del mismo
//  gesto sin nada que aprender.
//
//  Dibujada y no construida con botones, por lo mismo que la rejilla de pasos:
//  veinticinco tonos por dieciseis pasos son cuatrocientos componentes que
//  maquetar y repintar, y lo que hace falta es un componente que pinta y
//  acierta con el dedo.
//
//  LAS TECLAS NEGRAS SE PINTAN NEGRAS. Sin eso la rejilla son veinticinco
//  filas iguales y no hay forma de saber en que nota estas sin contarlas desde
//  abajo: el patron 2-3 de un teclado es lo unico que orienta la vista, y es
//  gratis - sale de saber si el semitono cae en {1,3,6,8,10}.
// ============================================================================
class PianoRoll : public juce::Component
{
public:
    //  Dos octavas y la raiz, de -12 a +12. Es lo que el motor admite por paso
    //  -stepNote esta acotado a +-24- y lo que cabe en la altura de una ficha
    //  sin que una tecla baje del dedo minimo. El boton de OCTAVA mueve la
    //  ventana dentro del rango entero.
    static constexpr int kFilas    = 25;    // semitonos visibles a la vez
    static constexpr int kGutter   = 34;    // la columna del teclado
    static constexpr int kMaxNotas = 4;     // raiz + tres del acorde

    //  (paso, semitono) — quien la usa decide si pone o quita.
    std::function<void (int paso, int semi)> onCelda;
    //  Un toque en el teclado: suena esa nota sin escribir nada, que es como
    //  se busca una melodia antes de escribirla.
    std::function<void (int semi)> onTecla;

    //  `notas` trae kMaxNotas semitonos por paso; -128 es "ninguna". `pasos`
    //  es cuantas columnas se dibujan, `base` el semitono de la fila de abajo.
    void setSource (const signed char* notas, int pasos, int base,
                    int pasoTocando, int zati, float fase = 0.0f)
    {
        datos = notas; nPasos = juce::jmax (1, pasos); semiBase = base;
        tocando = pasoTocando; color = zati;
        faseAct = juce::jlimit (0.0f, 1.0f, fase);

        //  REPINTAR SOLO SI HA CAMBIADO ALGO, por lo mismo que las otras dos
        //  rejillas: esto se llama en cada tick del temporizador y la ficha
        //  que lo contiene ocupa la ventana entera con un velo encima.
        const size_t n = (size_t) nPasos * (size_t) kMaxNotas;
        bool igual = datos != nullptr && visto
                  && nPasos == prevPasos && semiBase == prevBase
                  && tocando == prevTocando && color == prevColor
                  && std::abs (faseAct - prevFase) < 0.004f
                  && sombra.size() == n
                  && std::memcmp (sombra.data(), datos, n * sizeof (signed char)) == 0;
        if (igual) return;

        const auto antes = marcaDe (prevTocando);
        const bool soloCabezal = visto && datos != nullptr
                              && nPasos == prevPasos && semiBase == prevBase
                              && color == prevColor
                              && sombra.size() == n
                              && std::memcmp (sombra.data(), datos, n * sizeof (signed char)) == 0;

        sombra.resize (n);
        if (datos != nullptr) std::memcpy (sombra.data(), datos, n * sizeof (signed char));
        prevPasos = nPasos; prevBase = semiBase; prevTocando = tocando;
        prevColor = color; prevFase = faseAct;
        const bool primera = ! visto;
        visto = true;

        if (soloCabezal && ! primera)
        {
            auto zona = antes.getUnion (marcaDe (tocando));
            if (! zona.isEmpty()) { repaint (zona); return; }
        }
        repaint();
    }

    //  Donde cae la marca del paso que suena. La columna entera mas seis
    //  pixeles a cada lado, que es donde puede caer para cualquier fase.
    juce::Rectangle<int> marcaDe (int paso) const
    {
        const auto r = getLocalBounds();
        if (r.isEmpty() || paso < 0 || paso >= prevPasos) return {};
        const float ancho = (float) (r.getWidth() - kGutter) / (float) juce::jmax (1, prevPasos);
        const float x = (float) r.getX() + (float) kGutter + ancho * (float) paso;
        return juce::Rectangle<float> (x - 6.0f, (float) r.getY(), ancho + 12.0f, (float) r.getHeight())
                 .getSmallestIntegerContainer().getIntersection (r);
    }

    static bool esNegra (int semi)
    {
        const int n = ((semi % 12) + 12) % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }

    //  El nombre de la nota, con la raiz del pad como DO. No es la afinacion
    //  real de la muestra -eso no lo sabe nadie- sino la distancia a ella, que
    //  es lo unico que el motor entiende y lo unico que hace falta para tocar
    //  un acorde: la forma es la misma en cualquier tonalidad.
    static juce::String nombreDe (int semi)
    {
        static const char* kNombres[12] = { "C", "C#", "D", "D#", "E", "F",
                                            "F#", "G", "G#", "A", "A#", "B" };
        const int n = ((semi % 12) + 12) % 12;
        const int oct = (int) std::floor ((double) semi / 12.0);
        return juce::String (kNombres[n]) + (oct == 0 ? juce::String()
                                                      : juce::String (oct > 0 ? "+" : "") + juce::String (oct));
    }

    void paint (juce::Graphics& g) override
    {
        if (datos == nullptr) return;

        auto r = getLocalBounds();
        const float altoFila = (float) r.getHeight() / (float) kFilas;
        const float anchoCol = (float) (r.getWidth() - kGutter) / (float) nPasos;
        const auto tinta = Zati::colour (color);

        for (int f = 0; f < kFilas; ++f)
        {
            //  La fila de arriba es la nota mas AGUDA: un piano roll se lee
            //  como un pentagrama, con lo alto arriba. Dibujarlo al reves es
            //  lo primero que hace que nadie entienda la pantalla.
            const int semi = semiBase + (kFilas - 1 - f);
            const float y  = (float) r.getY() + altoFila * (float) f;
            const bool negra = esNegra (semi);

            //  EL TECLADO. Negras negras y blancas blancas, que es lo unico
            //  que orienta la vista sin contar filas desde abajo.
            auto tecla = juce::Rectangle<float> ((float) r.getX(), y, (float) kGutter, altoFila)
                             .reduced (1.0f, 0.5f);
            g.setColour (negra ? ZatiColours::groove (0.72f)
                               : ZatiColours::markOn (ZatiColours::chassisTop, 0.10f));
            g.fillRect (tecla);

            //  El DO de cada octava lleva su nombre; las demas no, que
            //  veinticinco rotulos en una columna de 34 px es una mancha.
            if (((semi % 12) + 12) % 12 == 0)
            {
                g.setColour (ZatiColours::textOn (negra ? ZatiColours::groove (0.72f)
                                                        : ZatiColours::chassisTop));
                g.setFont (ZatiColours::monoFont (Metrics::fTiny, true));
                g.drawText (nombreDe (semi), tecla, juce::Justification::centred);
            }

            for (int c = 0; c < nPasos; ++c)
            {
                const float x = (float) r.getX() + (float) kGutter + anchoCol * (float) c;
                auto celda = juce::Rectangle<float> (x, y, anchoCol, altoFila).reduced (0.8f);

                //  El hueco de una fila negra se hunde un poco mas: es la
                //  misma pista que da el teclado, repetida a lo ancho para que
                //  no haya que mirar a la izquierda en cada nota.
                g.setColour (ZatiColours::groove (negra ? 0.34f : (c % 4 == 0 ? 0.26f : 0.16f)));
                g.fillRect (celda);

                bool puesta = false;
                for (int k = 0; k < kMaxNotas; ++k)
                    if (datos[c * kMaxNotas + k] != -128 && (int) datos[c * kMaxNotas + k] == semi)
                        { puesta = true; break; }

                if (puesta)
                {
                    g.setColour (tinta);
                    g.fillRect (celda);
                    g.setColour (ZatiColours::ink.withAlpha (0.35f));
                    g.drawRect (celda, 1.0f);
                }
            }
        }

        //  El cabezal, encima de todo y en su color.
        if (tocando >= 0 && tocando < nPasos)
        {
            const float x = (float) r.getX() + (float) kGutter + anchoCol * (float) tocando;
            g.setColour (ZatiColours::groove (0.14f));
            g.fillRect (x, (float) r.getY(), anchoCol, (float) r.getHeight());
            const float xx = x + anchoCol * faseAct;
            g.setColour (ZatiColours::playheadEdge);
            g.fillRect (xx - 2.0f, (float) r.getY(), 4.0f, (float) r.getHeight());
            g.setColour (ZatiColours::playhead);
            g.fillRect (xx - 1.0f, (float) r.getY(), 2.0f, (float) r.getHeight());
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { toca (e, false); }
    void mouseDrag (const juce::MouseEvent& e) override { toca (e, true); }
    void mouseUp   (const juce::MouseEvent&)   override { ultima = -1; }

private:
    void toca (const juce::MouseEvent& e, bool arrastrando)
    {
        if (datos == nullptr) return;
        auto r = getLocalBounds();
        const float altoFila = (float) r.getHeight() / (float) kFilas;
        const int fila = juce::jlimit (0, kFilas - 1, (int) ((float) (e.y - r.getY()) / altoFila));
        const int semi = semiBase + (kFilas - 1 - fila);

        //  EL TECLADO SUENA, no escribe. Buscar la nota antes de ponerla es la
        //  mitad de escribir una melodia, y sin esto habria que escribirla,
        //  oirla y borrarla.
        if (e.x < r.getX() + kGutter)
        {
            if (! arrastrando && onTecla) onTecla (semi);
            return;
        }

        if (! onCelda) return;
        const float anchoCol = (float) (r.getWidth() - kGutter) / (float) nPasos;
        const int paso = juce::jlimit (0, nPasos - 1,
                                       (int) ((float) (e.x - r.getX() - kGutter) / anchoCol));

        //  Un arrastre pinta, pero solo al ENTRAR en una celda nueva: moverse
        //  dentro de una la encenderia y apagaria varias veces por segundo.
        const int clave = fila * 1000 + paso;
        if (arrastrando && clave == ultima) return;
        ultima = clave;
        onCelda (paso, semi);
    }

    const signed char* datos = nullptr;
    int nPasos = 16, semiBase = -12, tocando = -1, color = 0, ultima = -1;
    float faseAct = 0.0f;

    std::vector<signed char> sombra;
    int prevPasos = -1, prevBase = -99, prevTocando = -2, prevColor = -1;
    float prevFase = -1.0f;
    bool visto = false;
};
