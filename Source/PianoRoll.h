#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "Zati.h"
#include <array>
#include <cstring>
#include <algorithm>
#include <vector>

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
class PianoRoll : public juce::Component, public Rejilla
{
public:
    //  Las tres cifras del banco. El piano cambia de columnas con el zoom y
    //  de filas con OCTAVA -trece o veinticinco-, asi que escribirlas fuera
    //  seria medir la rejilla de ayer.
    int celdasAncho() const override { return numPasos(); }
    int celdasAlto()  const override { return filas; }
    int canalIzq()    const override { return kGutter; }
    float celdaAnchoPx() const override { return (float) juce::jmax (0, getWidth() - kGutter) / (float) juce::jmax (1, nPasos); }
    float celdaAltoPx()  const override { return (float) getHeight() / (float) juce::jmax (1, filas); }

    //  UNA OCTAVA Y SU RAIZ, trece filas. Eran veinticinco -dos octavas- y esa
    //  cuenta se hizo por el rango del motor y no por el dedo: veinticinco
    //  filas se reparten el alto que quede, y medido ficha por ficha la fila
    //  salia a 18.0 px en un movil grande, 10.8 en un 360x640, 11.2 en el
    //  Fold cerrado y 7.4 APAISADO — la sexta parte de un dedo. Poner una nota
    //  a ojo en 10 px es escribir la de al lado, y por eso "no se pueden
    //  colocar ergonomicamente": el gesto estaba bien, el blanco no.
    //
    //  No se arregla dando mas alto -no lo hay- ni desplazando la rejilla, que
    //  esta es de LIENZO y se pinta con el dedo arrastrado: un arrastre
    //  vertical que a veces escribe una nota y a veces mueve la pagina es un
    //  gesto que no se puede aprender. Se arregla enseñando MENOS a la vez, y
    //  la unidad en la que se enseña menos es la octava, no un numero redondo.
    //
    //  Y sale gratis dos veces mas. Con trece filas el boton de OCTAVA embaldosa
    //  el rango entero sin huecos ni sobras: -24..-12, -12..0, 0..12, 12..24,
    //  que es exactamente lo que setStepNote admite. Con veinticinco, la base
    //  en +12 dibujaba hasta +36 y las doce filas de arriba escribian notas que
    //  el motor recortaba a +24 — doce filas que mentian.
    //  Y CUANTAS SE VEN LO ELIGE QUIEN MIRA. Trece es una octava y su raiz y es
    //  lo que hace que una nota se pueda colocar; veinticinco son dos octavas y
    //  sirven para VER una melodia entera de un vistazo, que es la otra cosa
    //  que se le pide a un piano roll. Las dos cuentas tienen sentido y ninguna
    //  gana siempre: en un movil grande veinticinco filas siguen dando 17 px y
    //  eso se lee, aunque no se acierte comodamente.
    //
    //  Trece y veinticinco y nada en medio, porque el boton de OCTAVA mueve de
    //  doce en doce: con cualquier otra cuenta la ventana deja huecos o solapa,
    //  y una fila que no se puede alcanzar desde ningun paso del boton es una
    //  fila que miente. Con trece: -24..-12, -12..0, 0..12, 12..24. Con
    //  veinticinco: -24..0 y 0..24.
    static constexpr int kFilasMin = 13;   // una octava y su raiz
    static constexpr int kFilasMax = 25;   // dos octavas y su raiz
    void setFilas (int n) { filas = (n >= kFilasMax) ? kFilasMax : kFilasMin; repaint(); }
    int  getFilas() const noexcept { return filas; }
    //  El semitono mas alto que puede quedar abajo, para que el de arriba caiga
    //  clavado en el +24 que setStepNote admite y ni uno mas.
    int  baseMax()  const noexcept { return 24 - (filas - 1); }
    //  Lo que la ficha PIDE por fila. No es un suelo -la rejilla se queda con
    //  lo que sobre y quien manda es el alto de la tarjeta- sino el objetivo:
    //  trece por 34 son 442 px, que es lo que un movil grande puede dar
    //  (450 medidos en 412x915). Pedir los 40 del dedo serian 520 y ninguna
    //  pantalla los tiene, asi que la tarjeta quedaria clavada en su tope en
    //  las siete y el numero dejaria de decir nada.
    static constexpr int kAltoObjetivo = 34;
    //  VEINTISEIS Y NO TREINTA Y CUATRO. La columna del teclado sale del ancho
    //  de la rejilla, asi que cada pixel suyo es un pixel que no tiene la
    //  casilla del paso: en el Fold cerrado -225 px de tarjeta- con 34 la
    //  columna de un paso quedaba en 11.9, por debajo del suelo de 12 que ya
    //  cumple la rejilla de pasos en esa misma pantalla. Con 26 son 12.4, y en
    //  26 px sigue cabiendo el nombre de la octava, que es lo unico que se
    //  escribe ahi y solo en las filas de DO.
    static constexpr int kGutter   = Metrics::canalPiano;   // la columna del teclado
    static constexpr int kMaxNotas = 4;     // raiz + tres del acorde

    //  (paso, semitono) — quien la usa decide si pone o quita.
    std::function<void (int paso, int semi)> onCelda;
    //  Un toque en el teclado: suena esa nota sin escribir nada, que es como
    //  se busca una melodia antes de escribirla.
    std::function<void (int semi)> onTecla;

    //  EL LARGO DE UNA NOTA, en cuartos de paso. Arrastrar por la MISMA fila
    //  desde una nota la estira; arrastrar cambiando de fila sigue pintando
    //  notas, que es como se escribe un acorde o una escalera. Dos gestos que
    //  no se pisan porque uno es horizontal y el otro no.
    std::function<void (int paso, int semi, int cuartos)> onLargo;

    //  LA HERRAMIENTA. Dibujar es lo normal; la GOMA borra lo que toca -sin
    //  alternar, que arrastrar sobre notas puestas y quitadas encendia unas y
    //  apagaba otras- y las TIJERAS cortan la nota por donde se tocan, que es
    //  lo unico que "cortar" puede significar cuando el largo es del paso.
    //  Y EL LAPIZ, que es la goma por el otro lado: escribe y no alterna. La
    //  pregunta la contesta quien tiene los datos - pianoCellToggled - porque
    //  aqui no se sabe que hay escrito en una casilla.
    //  Y LA SELECCION, que es la QUINTA herramienta y no un gesto nuevo.
    //
    //  Esta rejilla es de LIENZO y se pinta con el dedo arrastrado, y arrastrar
    //  ya significa dos cosas: pintar notas cambiando de fila y estirar el
    //  largo por la misma fila. Meter «seleccionar» y «mover» encima serian
    //  CUATRO significados en un dedo, que es lo que esta casa lleva escrito
    //  que no se puede aprender. Con un modo el gesto es inequivoco, y sin el
    //  modo puesto el piano se comporta exactamente igual que antes.
    enum Herramienta { dibujar = 0, lapiz, goma, tijeras, sel };
    void setHerramienta (int h) { util = juce::jlimit (0, 4, h); }
    int  getHerramienta() const { return util; }

    //  Cuantas columnas se estan dibujando. Lo pide el banco para no repetir la
    //  constante: una prueba que lee el numero que ella misma se da cambia de
    //  opinion a la vez que el fallo.
    int  numPasos() const noexcept { return nPasos; }

    //  Borrar y cortar los resuelve quien tiene los datos, igual que onCelda.
    std::function<void (int paso, int semi)> onBorrar;
    std::function<void (int paso, int cuartos)> onCortar;

    //  LA SELECCION LA GUARDA QUIEN TIENE LOS DATOS, no este componente: aqui
    //  solo se dibuja y se reporta el gesto. Es el mismo reparto que el resto
    //  de la casa - el componente sabe geometria, el anfitrion sabe que hay
    //  escrito - y el que hizo que la tabla de zatis se pase en vez de
    //  preguntarle al motor por cada bloque.
    //
    //  La banda va en (paso, semi) y no en pixeles: quien la recibe no tiene
    //  por que saber cuanto mide una celda.
    std::function<void (int paso0, int semi0, int paso1, int semi1)> onBanda;
    std::function<void (int dPaso, int dSemi)> onMueveSel;
    std::function<void()> onVaciaSel;
    //  Si una celda esta seleccionada. Lo contesta el anfitrion porque es quien
    //  tiene el conjunto; aqui hace falta para dibujarla y para saber si un
    //  arrastre empieza DENTRO de la seleccion, que es lo que separa mover de
    //  volver a seleccionar.
    std::function<bool (int paso, int semi)> estaSel;

    //  `notas` trae kMaxNotas semitonos por paso; -128 es "ninguna". `pasos`
    //  es cuantas columnas se dibujan, `base` el semitono de la fila de abajo.
    //
    //  Y `desdePaso`, QUE ES EL PASO DEL PATRON DE LA PRIMERA COLUMNA. Sin el,
    //  la rejilla de fondo se tenia que teñir por la COLUMNA -`c % 4`- y esa
    //  cuenta solo coincide con el pulso cuando la ventana arranca en un
    //  multiplo de cuatro. `StepGrid` lo recibe desde el dia que la ventana es
    //  continua y su parrafo lo cuenta entero: «empezando en el paso 3, una
    //  linea cada cuatro columnas cae en 7 y 11, o sea marca el contratiempo y
    //  borra el pulso». El piano no lo recibio nunca — las notas se desplazan
    //  con la barra y la cuadricula se quedaba quieta, que es exactamente la
    //  queja: «se desplazan las notas pero no las cuadriculas, con lo que puede
    //  dar a confundirse donde pone uno las notas siguiendo los pasos».
    void setSource (const signed char* notas, int pasos, int base,
                    int pasoTocando, int zati, float fase = 0.0f,
                    const unsigned char* largos = nullptr, int desdePaso = 0)
    {
        datos = notas; nPasos = juce::jmax (1, pasos); semiBase = base;
        primerPaso = juce::jmax (0, desdePaso);
        cuartos = largos;
        tocando = pasoTocando; color = zati;
        faseAct = juce::jlimit (0.0f, 1.0f, fase);

        //  REPINTAR SOLO SI HA CAMBIADO ALGO, por lo mismo que las otras dos
        //  rejillas: esto se llama en cada tick del temporizador y la ficha
        //  que lo contiene ocupa la ventana entera con un velo encima.
        const size_t n = (size_t) nPasos * (size_t) kMaxNotas;
        const bool largosIguales = sombraLargos.size() == (size_t) nPasos
                                && (cuartos == nullptr
                                      ? std::all_of (sombraLargos.begin(), sombraLargos.end(),
                                                     [] (unsigned char v) { return v == 0; })
                                      : std::memcmp (sombraLargos.data(), cuartos,
                                                     (size_t) nPasos * sizeof (unsigned char)) == 0);
        //  Y `primerPaso` ENTRA EN LA COMPARACION, que es la trampa de este
        //  atajo: sin el, arrastrar la barra por una zona VACIA no repinta -las
        //  256 celdas salen identicas- y la cuadricula se queda donde estaba.
        //  O sea el mismo fallo mudado de sitio.
        bool igual = datos != nullptr && visto
                  && nPasos == prevPasos && semiBase == prevBase
                  && primerPaso == prevPrimer
                  && tocando == prevTocando && color == prevColor
                  && std::abs (faseAct - prevFase) < 0.004f
                  && sombra.size() == n
                  && std::memcmp (sombra.data(), datos, n * sizeof (signed char)) == 0
                  && largosIguales;
        if (igual) return;

        const auto antes = marcaDe (prevTocando);
        const bool soloCabezal = visto && datos != nullptr
                              && nPasos == prevPasos && semiBase == prevBase
                              && primerPaso == prevPrimer
                              && color == prevColor
                              && sombra.size() == n
                              && std::memcmp (sombra.data(), datos, n * sizeof (signed char)) == 0
                              && largosIguales;

        sombra.resize (n);
        if (datos != nullptr) std::memcpy (sombra.data(), datos, n * sizeof (signed char));
        sombraLargos.assign ((size_t) nPasos, 0);
        if (cuartos != nullptr)
            std::memcpy (sombraLargos.data(), cuartos, (size_t) nPasos * sizeof (unsigned char));
        prevPasos = nPasos; prevBase = semiBase; prevTocando = tocando;
        prevPrimer = primerPaso;
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
        const float altoFila = (float) r.getHeight() / (float) filas;
        const float anchoCol = (float) (r.getWidth() - kGutter) / (float) nPasos;
        const auto tinta = Zati::colour (color);

        for (int f = 0; f < filas; ++f)
        {
            //  La fila de arriba es la nota mas AGUDA: un piano roll se lee
            //  como un pentagrama, con lo alto arriba. Dibujarlo al reves es
            //  lo primero que hace que nadie entienda la pantalla.
            const int semi = semiBase + (filas - 1 - f);
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
                //  El pulso se tiñe por el PASO y no por la columna: son la
                //  misma cuenta solo cuando la ventana arranca en un borde de
                //  compas, y desde que hay barra de arrastre eso deja de estar
                //  garantizado.
                const bool pulso = (((primerPaso + c) % 4) == 0);
                g.setColour (ZatiColours::groove (negra ? 0.34f : (pulso ? 0.26f : 0.16f)));
                g.fillRect (celda);

                bool puesta = false;
                for (int k = 0; k < kMaxNotas; ++k)
                    if (datos[c * kMaxNotas + k] != -128 && (int) datos[c * kMaxNotas + k] == semi)
                        { puesta = true; break; }

                if (puesta)
                {
                    //  UNA NOTA ES UNA BARRA, no un cuadrado. El largo se
                    //  guarda en cuartos de paso, asi que una nota puede ocupar
                    //  cuatro casillas o un cuarto de una: sin esto, todo lo
                    //  que se escribe aqui dura lo mismo y da igual lo que
                    //  ponga en el motor - "no se ve" y "no esta" se parecen
                    //  demasiado.
                    const int cu = (cuartos != nullptr) ? (int) cuartos[c] : 0;
                    const float anchoNota = cu > 0
                                              ? juce::jmax (3.0f, anchoCol * (float) cu * 0.25f)
                                              : celda.getWidth();
                    auto barra = celda.withWidth (juce::jmin (anchoNota,
                                                             (float) r.getRight() - celda.getX()));
                    g.setColour (tinta);
                    g.fillRect (barra);
                    g.setColour (ZatiColours::ink.withAlpha (0.35f));
                    g.drawRect (barra, 1.0f);
                    //  Y el ARRANQUE marcado, que en una barra de cuatro
                    //  casillas es lo unico que dice donde empieza la nota.
                    if (cu > 4)
                    {
                        g.setColour (ZatiColours::ink.withAlpha (0.55f));
                        g.fillRect (barra.withWidth (2.0f));
                    }

                    //  Y LO SELECCIONADO SE VE. Una seleccion que no se dibuja
                    //  no es una seleccion: mover en bloque sin saber que
                    //  bloque se mueve es mover a ciegas. Con la misma marca
                    //  que el clip elegido de la banda de audio - anillo de
                    //  cabezal - para no inventar un tercer idioma.
                    if (estaSel != nullptr && estaSel (c, semi))
                    {
                        g.setColour (ZatiColours::playheadEdge);
                        g.drawRect (barra.expanded (1.0f), 1.4f);
                        g.setColour (ZatiColours::playhead);
                        g.drawRect (barra, 1.6f);
                    }
                }
            }
        }

        //  Y LAS LINEAS DE COMPAS, que es la mitad que de verdad se pidio.
        //
        //  El tinte de una celda vacia es un matiz -0.26 contra 0.16- y a
        //  quince pixeles de fila eso no se cuenta de un vistazo; una linea es
        //  una REFERENCIA. `StepGrid` las lleva desde que su ventana es
        //  continua y el piano se quedo sin ellas, que es la otra mitad de
        //  «puede dar a confundirse donde pone uno las notas siguiendo los
        //  pasos de los beat». Caen en los pasos multiplos de cuatro DEL
        //  PATRON, por lo mismo que el tinte.
        g.setColour (ZatiColours::groove (0.28f));
        for (int c = 1; c < nPasos; ++c)
            if (((primerPaso + c) % 4) == 0)
                g.fillRect ((float) r.getX() + (float) kGutter + anchoCol * (float) c - 0.5f,
                            (float) r.getY(), 1.0f, (float) r.getHeight());

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

        //  Y LA HERRAMIENTA ARMADA, VISTA DONDE SE ACTUA.
        //
        //  LAPIZ, GOMA, TIJERAS y SEL cambian lo que hace arrastrar, y hasta
        //  hoy eso solo se sabia mirando la fila de tapas: la encendida esta
        //  arriba y el dedo esta aqui. Un marco del color del acento alrededor
        //  del LIENZO —no de la ficha— dice «esta rejilla esta en un modo»
        //  justo donde cae el dedo. Sin herramienta no hay marco: dibujar es lo
        //  que la rejilla hace de por si, y un aviso permanente no avisa.
        if (util != dibujar)
        {
            auto marco = r.toFloat().reduced (0.75f);
            marco.setLeft (marco.getX() + (float) kGutter);
            g.setColour (ZatiColours::accent.withAlpha (0.9f));
            g.drawRect (marco, 1.5f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { gesto ((float) e.x, (float) e.y, false); }
    void mouseDrag (const juce::MouseEvent& e) override { gesto ((float) e.x, (float) e.y, true); }
    void mouseUp   (const juce::MouseEvent&)   override { suelta(); }

    //  EL GESTO, en pixeles y sin MouseEvent, para que el banco pueda medirlo.
    //
    //  Los cinco fallos del compas se midieron por `onCelda`, que es el
    //  callback: eso salta justo el codigo que decide QUE celda es, que es
    //  donde vive el arrastre. Un gesto que no se puede llamar es un gesto que
    //  no se mide - y el arrastre que cambiaba de fila escribia una nota por
    //  cada fila por la que pasaba el dedo sin que ninguna regla lo viera.
    void gesto (float x, float y, bool arrastrando)
    {
        if (datos == nullptr) return;
        auto r = getLocalBounds();
        const float altoFila = (float) r.getHeight() / (float) filas;
        const int fila = juce::jlimit (0, filas - 1, (int) ((y - (float) r.getY()) / altoFila));
        const int semi = semiBase + (filas - 1 - fila);

        //  EL TECLADO SUENA, no escribe. Buscar la nota antes de ponerla es la
        //  mitad de escribir una melodia, y sin esto habria que escribirla,
        //  oirla y borrarla.
        if (x < (float) (r.getX() + kGutter))
        {
            if (! arrastrando && onTecla) onTecla (semi);
            return;
        }

        const float anchoCol = (float) (r.getWidth() - kGutter) / (float) nPasos;
        const float dentro = (x - (float) r.getX() - (float) kGutter) / anchoCol;
        const int paso = juce::jlimit (0, nPasos - 1, (int) dentro);

        //  LA SELECCION, antes que el candado de fila: aqui arrastrar SI cruza
        //  filas, que es justo lo que una banda elastica tiene que hacer.
        //
        //  Tres respuestas y no dos, y la del medio es la que hace falta: si el
        //  arrastre empieza DENTRO de la seleccion se mueve el bloque entero, y
        //  si empieza fuera se selecciona de nuevo. Sin esa pregunta, mover
        //  seria un cuarto gesto y no habria forma de decir cual de los dos se
        //  quiso.
        if (util == sel)
        {
            if (! arrastrando)
            {
                pasoIni = paso; filaIni = fila; semiIni = semi;
                moviendo = (estaSel != nullptr && estaSel (paso, semi));
                if (! moviendo)
                {
                    //  Tocar fuera vacia: sin esto no habria forma de soltar la
                    //  seleccion sin seleccionar otra cosa.
                    if (onVaciaSel) onVaciaSel();
                    dPaso = dSemi = 0;
                }
                return;
            }

            if (moviendo)
            {
                //  EN DELTAS Y ACUMULADO, no en absolutos: quien mueve el
                //  bloque necesita cuanto se ha movido DESDE la ultima vez, o
                //  cada evento del raton lo desplazaria otra vez entero.
                const int nd = paso - pasoIni, ns = semi - semiIni;
                if (nd == dPaso && ns == dSemi) return;
                if (onMueveSel) onMueveSel (nd - dPaso, ns - dSemi);
                dPaso = nd; dSemi = ns;
                return;
            }

            if (onBanda) onBanda (pasoIni, semiIni, paso, semi);
            return;
        }

        //  ARRASTRAR NO CAMBIA DE FILA.
        //
        //  Estaba escrito al reves -"si el dedo cambia de fila se esta
        //  escribiendo un acorde o una escalera"- y desde el dedo eso es que
        //  bajar el dedo por la rejilla deja una nota en CADA fila por la que
        //  pasa. Un acorde se escribe levantando y volviendo a tocar, que son
        //  dos notas y dos toques; lo que no se puede es escribir cinco sin
        //  querer con un gesto.
        //
        //  Se pregunta antes que la herramienta, asi que la goma se queda
        //  tambien en su fila: borrar de mas es el mismo accidente.
        if (arrastrando && filaIni >= 0 && fila != filaIni) return;
        if (! arrastrando) { filaIni = fila; pasoIni = paso; ultimoLargo = -1; }

        //  LA GOMA borra y no alterna: pasar el dedo por encima de una fila con
        //  notas puestas y huecos encendia los huecos, que es lo contrario de
        //  borrar. Se repite por celda como el pintado, para no borrar la misma
        //  cincuenta veces por segundo.
        if (util == goma)
        {
            const int clave2 = fila * 1000 + paso;
            if (arrastrando && clave2 == ultima) return;
            ultima = clave2;
            if (onBorrar) onBorrar (paso, semi);
            return;
        }

        //  LAS TIJERAS cortan por donde se toca: el largo de la nota pasa a ser
        //  lo que va de su casilla hasta el dedo, en cuartos. Tocar dentro de la
        //  primera casilla la deja en un cuarto, que es lo mas corto que hay.
        if (util == tijeras)
        {
            if (arrastrando) return;
            if (onCortar)
            {
                //  Se busca hacia atras la casilla donde empieza la barra que se
                //  ha tocado: cortar por el dedo sin saber donde empieza la nota
                //  daria un largo medido desde el sitio equivocado.
                int ini = paso;
                for (int c = paso; c >= 0; --c)
                {
                    bool aqui = false;
                    for (int k = 0; k < kMaxNotas; ++k)
                        if (datos[c * kMaxNotas + k] != -128 && (int) datos[c * kMaxNotas + k] == semi)
                            { aqui = true; break; }
                    if (aqui) { ini = c; break; }
                }
                const int cu = juce::jlimit (1, 63, (int) ((dentro - (float) ini) * 4.0f) + 1);
                onCortar (ini, cu);
            }
            return;
        }

        if (! onCelda) return;

        //  ARRASTRAR POR LA MISMA FILA ES ESTIRAR LA NOTA.
        //
        //  Es el gesto de cualquier piano roll y no se pisa con el de pintar,
        //  porque uno es horizontal y el otro no: si el dedo cambia de fila se
        //  esta escribiendo un acorde o una escalera, y si se queda en la suya
        //  se esta diciendo cuanto dura la nota que se acaba de poner.
        if (arrastrando && fila == filaIni && pasoIni >= 0 && onLargo != nullptr)
        {
            const int cu = juce::jlimit (1, 63, (paso - pasoIni + 1) * 4);
            if (cu != ultimoLargo)
            {
                ultimoLargo = cu;
                onLargo (pasoIni, semi, cu);
            }
            return;
        }

        //  Un arrastre pinta, pero solo al ENTRAR en una celda nueva: moverse
        //  dentro de una la encenderia y apagaria varias veces por segundo.
        const int clave = fila * 1000 + paso;
        if (arrastrando && clave == ultima) return;
        ultima = clave;
        onCelda (paso, semi);
    }

    void suelta() { ultima = -1; filaIni = pasoIni = -1; ultimoLargo = -1; moviendo = false; dPaso = dSemi = 0; }

private:
    //  El arrastre de la seleccion: donde empezo y cuanto lleva movido.
    int  semiIni = 0;
    bool moviendo = false;
    int  dPaso = 0, dSemi = 0;

    const signed char* datos = nullptr;
    //  Un largo por PASO, en cuartos: las notas de un acorde comparten casilla
    //  y comparten largo, que es lo que un acorde es.
    const unsigned char* cuartos = nullptr;
    std::vector<unsigned char> sombraLargos;
    int filas = kFilasMin;              // ver setFilas
    int nPasos = 16, semiBase = -12, tocando = -1, color = 0, ultima = -1;
    //  El paso del patron de la primera columna. Lo mismo que `StepGrid`
    //  llama asi, y por lo mismo.
    int primerPaso = 0;
    //  Donde empezo el arrastre, para saber si estira o pinta.
    int filaIni = -1, pasoIni = -1, ultimoLargo = -1;
    int util = 0;                       // 0 dibujar, 1 goma, 2 tijeras
    float faseAct = 0.0f;

    std::vector<signed char> sombra;
    int prevPasos = -1, prevBase = -99, prevTocando = -2, prevColor = -1;
    int prevPrimer = -1;
    float prevFase = -1.0f;
    bool visto = false;
};
