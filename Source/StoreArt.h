#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "Zati.h"
#include "Iconos.h"
#include "PadArt.h"
#include "Kits.h"

// ============================================================================
//  EL GRAFICO DESTACADO DE LA FICHA, dibujado por la propia app.
//
//  Google Play pide un banner de 1024x500 y NO puede ser una captura: es lo
//  primero que se ve en la ficha y una foto de la interfaz recortada a 1024x500
//  se lee como un error de maquetado.
//
//  Se dibuja aqui y no con una herramienta de imagenes por una razon concreta:
//  el chasis, la tinta, el acento y los ocho colores de zati son TOKENS que se
//  mueven - hay cuatro carcasas y la paleta ha cambiado tres veces en este
//  proyecto. Un banner hecho fuera se queda con la paleta del dia que se hizo,
//  y nadie se entera hasta que alguien compara la ficha con la app. Hecho con
//  ZatiColours, el banner ES la app: se regenera y sale al dia.
//
//  Solo del banco. No hay ningun camino desde la interfaz que llegue aqui.
// ============================================================================
namespace StoreArt
{
    inline juce::Image featureGraphic (int w = 1024, int h = 500)
    {
        juce::Image img (juce::Image::ARGB, w, h, true);
        juce::Graphics g (img);

        const auto b = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);

        //  El cuerpo, con el mismo degradado que la cara de la maquina.
        g.setGradientFill (juce::ColourGradient (ZatiColours::chassisTop, 0.0f, 0.0f,
                                                 ZatiColours::chassisBot, 0.0f, (float) h, false));
        g.fillRect (b);

        //  La tira de ocho colores, que es la firma de la caja: en la app va
        //  bajo el nombre y aqui hace de borde superior.
        {
            const float sw = (float) w / (float) Zati::kNumColours;
            for (int i = 0; i < Zati::kNumColours; ++i)
            {
                g.setColour (Zati::colour (i));
                g.fillRect (sw * (float) i, 0.0f, sw - 1.0f, 10.0f);
            }
        }

        //  Cada linea se MIDE contra el hueco que le queda y se encoge la
        //  fuente hasta que cabe. La primera version fiaba el ancho a la
        //  cuenta de los caracteres y salieron dos rotulos cortados con
        //  puntos suspensivos - "A..." por ARTiFACTS - en lo primero que se ve
        //  de la app en la tienda.
        auto drawFitted = [&g] (const juce::String& text, juce::Rectangle<float> box,
                                juce::Font font)
        {
            for (int i = 0; i < 12; ++i)
            {
                if (juce::GlyphArrangement::getStringWidth (font, text) <= box.getWidth()) break;
                font = font.withHeight (font.getHeight() * 0.94f);
            }
            g.setFont (font);
            g.drawText (text, box, juce::Justification::topLeft, false);
        };

        //  El texto se para antes del bloque de tapas. Sin el tope, la primera
        //  version escribia ARTiFACTS por debajo de la tapa 02.
        const float artW = (float) h * 0.30f * 2.14f;
        const float textX = (float) w * 0.055f;
        const float textW = (float) w - artW - textX * 2.0f - 24.0f;

        //  El nombre, con el mismo tipo y el mismo tracking que el de la cara.
        g.setColour (ZatiColours::ink);
        auto title = juce::Rectangle<float> (textX, (float) h * 0.20f, textW, (float) h * 0.30f);

        //  LA MARCA, la misma que lleva la cara de la maquina. Un banner con
        //  un dibujo que la app no tiene es un banner de otra app.
        {
            const float lado = (float) h * 0.13f;
            auto m = Iconos::marca();
            m.applyTransform (juce::AffineTransform::scale (lado / 24.0f)
                                  .translated (textX, title.getY() - lado - (float) h * 0.045f));
            g.fillPath (m);
        }
        //  El nombre COMPLETO, que es como se llama la app en la tienda. En la
        //  cara de la maquina sigue poniendo ZATI a secas: ahi es la marca
        //  serigrafiada en el chasis, y "ZATI SAMPLER" cruzado por la cabecera
        //  se comeria la fila entera para decir lo que la maquina ya es.
        drawFitted ("ZATI SAMPLER", title, ZatiColours::labelFont ((float) h * 0.26f, 0.20f));

        g.setColour (ZatiColours::inkDim);
        drawFitted (juce::String::fromUTF8 ("GROOVEBOX  \xc2\xb7  ARTiFACTS"),
                    title.translated (0.0f, (float) h * 0.30f),
                    ZatiColours::monoFont ((float) h * 0.058f, true).withExtraKerningFactor (0.16f));

        g.setColour (ZatiColours::ink.withAlpha (0.75f));
        drawFitted (juce::String::fromUTF8 ("64 PADS  \xc2\xb7  SECUENCIADOR  \xc2\xb7  6 EFECTOS"),
                    title.translated (0.0f, (float) h * 0.44f),
                    ZatiColours::monoFont ((float) h * 0.050f, false).withExtraKerningFactor (0.10f));

        //  Cuatro tapas de pad a la derecha, con su relieve: dicen lo que es
        //  la app sin tener que leer nada, y son el unico dibujo del banner.
        //
        //  Y SON LAS TAPAS DE LA APP, no unas parecidas. Este bloque llevaba
        //  su propio dibujo -sombra, relleno y numero escritos aqui- porque se
        //  escribio antes de que `PadArt` existiera, y nunca se migro: un
        //  TERCER dibujo de la misma cosa, que es justo lo que PadArt se
        //  extrajo para evitar. El sintoma ya estaba: pintaba el zati A PLENO,
        //  o sea C* 59.4 de croma contra los 39.3 que mide un pad de la cara -
        //  el grafico de la ficha de Play saliendo x1.51 mas saturado que la
        //  maquina que vende.
        {
            const float side = (float) h * 0.30f;
            const float gap  = side * 0.14f;
            const float x0   = (float) w - (2.0f * side + gap) - (float) w * 0.055f;
            const float y0   = (float) h * 0.5f - (side + gap * 0.5f);
            const float escala = side / PadArt::kAltoRef;

            for (int i = 0; i < 4; ++i)
            {
                const auto r = juce::Rectangle<float> (
                                   x0 + (float) (i % 2) * (side + gap),
                                   y0 + (float) (i / 2) * (side + gap), side, side);
                const auto frag = Zati::colour (i * 2);
                const auto cuerpo = PadArt::cuerpoDe (frag, true);

                PadArt::fondo  (g, r, cuerpo, frag, PadArt::Banda::solida, escala);
                PadArt::numero (g, r, i + 1, ZatiColours::textOn (cuerpo).withAlpha (0.92f), escala);
                PadArt::borde  (g, r, PadArt::bordeDe (frag, true), true, escala);
            }
        }

        return img;
    }

    // ========================================================================
    //  EL ICONO DEL LANZADOR: LA REJILLA CON LA Z ENCENDIDA.
    //
    //  Habia DOS logos y ninguno miraba al otro. `Iconos::marca()` es un pad
    //  con la Z recortada dentro y vive en la cabecera y en el banner; el
    //  icono de la app era un PNG de 1024 hecho a mano - una placa blanca con
    //  dieciseis pads grises y cuatro de colores sueltos - que no genera nadie,
    //  no mide nadie y no comparte un solo token con la app.
    //
    //  Ahora es la rejilla 4x4 con la Z dibujada por los pads ENCENDIDOS: los
    //  dos logos a la vez -la rejilla dice la maquina, lo que se enciende dice
    //  el nombre- y ademas es lo que la app hace. Las diez celdas salen de
    //  `Iconos::marcaCelda`, o sea del mismo sitio que la Z de la cabecera.
    //
    //  Y se GENERA, por lo mismo que el banner de aqui arriba: un dibujo hecho
    //  fuera se queda con la paleta del dia que se hizo y nadie se entera hasta
    //  que alguien compara la ficha con la app. Este PNG es ademas el que
    //  `Tests/store.py` reduce a 512 para la ficha de Play.
    // ========================================================================
    //  LOS CINCO ESTILOS. Todos usan el MISMO material -la rejilla, marcaCelda,
    //  PadArt y los zatis- y ninguno inventa un dibujo nuevo: son cinco formas
    //  de jugar el mismo contenido, que es lo que se pidio.
    //  QUE SONIDO SE VE POR EL HUECO. Es una ENTRADA del banco y no una
    //  constante escondida, por lo mismo que `ZATI_SKIN`: los tres candidatos
    //  -un golpe corto de ACUSTICA, un colchon de TONOS y una textura- se
    //  eligen midiendo cuanto del hueco es onda, y sin poder cambiarlo desde
    //  fuera habria que recompilar para mirar cada uno.
    //  WIND, banco C (TEXTURA). Elegido por las dos cifras y no por gusto:
    //  un golpe corto -SNARE del banco A- deja el hueco al 4.2 % de onda, o
    //  sea no se ve, porque un sonido que decae son cuatro quintos de silencio
    //  y por tres tiras finas eso es nada; un colchon de TONOS da 24.8 % y una
    //  textura 28.7 %, las dos en la banda buena. Entre esas dos, la textura
    //  tiene la silueta dentada que se lee como una ONDA - un colchon dibuja
    //  una loma, que se lee como una envolvente.
    inline int sonidoDeLaMarca = 39;        // WIND, banco C

    enum class Estilo { rejilla, invertida, unColor, conTira, marcaSola,
                        //  LA MARCA CON EL COLOR DE LOS PADS, por tres caminos
                        //  distintos. Los tres respetan la regla que hace que la
                        //  marca funcione en las cuatro carcasas: la Z es un
                        //  HUECO y no un trazo, asi que nunca hay que elegir un
                        //  segundo color para la letra.
                        marcaVentana,   // la Z deja ver los ocho zatis
                        marcaTapa,      // la tapa ES los ocho, la Z el chasis
                        marcaBanda,     // una tapa, con la banda de un pad arriba
                        //  Y LA Z ES UNA VENTANA A LA ONDA, que es lo que le
                        //  faltaba a la marca para decir que esto es un
                        //  SAMPLER: un pad con una Z podria ser cualquier app.
                        //  Mismo mecanismo que las tres de arriba - la Z sigue
                        //  siendo un HUECO - cambiando lo que se pone detras.
                        marcaOnda };

    //  EL QUE SE INSTALA, escrito UNA vez.
    //
    //  Nueve estilos y solo uno acaba en el lanzador. Sin esta linea el estilo
    //  de fabrica vive en el valor por defecto de `appIcon`, en el de
    //  `writeIcon`, en el `getIntValue()` de `ZATI_ICONO_ESTILO` -que da 0
    //  cuando la variable no esta- y en la cabeza de quien corra el banco: son
    //  cuatro sitios que nadie obliga a decir lo mismo, y el banco mediria un
    //  icono distinto del que se publica.
    //
    //  LA MARCA SOLA, que es la que aguanta el tamano al que se mira. A 48 px
    //  -lo que mide en el cajon de aplicaciones- el hueco de la Z son tres
    //  tiras de dos pixeles, asi que todo lo que se pone DETRAS de ella sale
    //  como una mancha: la onda de un sonido de fabrica medida con 7, 9, 11,
    //  13 y 56 columnas da el mismo borron a esa escala, y no es el numero de
    //  columnas - es que por una rendija de dos pixeles no se lee un dibujo.
    //  Los estilos que meten color detras se quedan tras `ZATI_ICONO_ESTILO`,
    //  que es como se pueden volver a mirar sin reescribirlos.
    inline constexpr Estilo kEstiloDeFabrica = Estilo::marcaSola;

    //  DONDE CAE LA MARCA DENTRO DEL ICONO, escrito UNA vez.
    //
    //  Lo usan el dibujo y la mascara con la que el banco sabe que pixeles son
    //  hueco de la Z. Si cada uno lo calculara por su cuenta, el dia que la
    //  marca se moviera un pixel el banco seguiria midiendo donde estaba - que
    //  es la clase de regla escrita dos veces que este proyecto lleva
    //  encontrando desde el principio.
    //
    //  El mismo margen que la rejilla: una fila de pad por lado, o sea la
    //  marca ocupa 4 de 6. Ver el parrafo de la fila invisible mas abajo.
    inline juce::Rectangle<float> cajaDeLaMarca (float L) noexcept
    {
        const float zona = L * (float) Iconos::kLadoMarca / (float) (Iconos::kLadoMarca + 2);
        return { (L - zona) * 0.5f, (L - zona) * 0.5f, zona, zona };
    }

    inline juce::AffineTransform transformaDeLaMarca (float L) noexcept
    {
        const auto caja = cajaDeLaMarca (L);
        return juce::AffineTransform::scale (caja.getWidth() / 24.0f)
                   .translated (caja.getX(), caja.getY());
    }

    //  LA MASCARA DE LA MARCA, para el banco y solo para el banco.
    //
    //  Las reglas de las marcas preguntan por el HUECO de la Z -cuanto de el
    //  es onda, y si la letra se lee contra la tapa- y desde el PNG no hay
    //  forma de saber que pixel es hueco: por el hueco se ve el cristal, y el
    //  cristal es un color mas. Deducirlo en Python de las constantes del
    //  dibujo es exactamente el fallo que ya cometio la regla de la mascara
    //  del lanzador -repetia el numero en vez de medir- asi que lo dice quien
    //  lo dibuja: blanco donde hay tapa, negro donde hay hueco.
    inline juce::Image mascaraMarca (int lado)
    {
        lado = juce::jmax (16, lado);
        juce::Image img (juce::Image::ARGB, lado, lado, true);
        juce::Graphics g (img);
        g.fillAll (juce::Colours::black);

        //  TRES valores y no dos, que es donde la primera version se equivoco:
        //  `Iconos::marca()` es la tapa MENOS la Z, asi que pintarla en blanco
        //  sobre negro deja del mismo color el hueco de la letra y todo lo que
        //  hay FUERA del pad - y el banco contaba el chasis como hueco, 1096
        //  pixeles donde la tapa entera son 1024. La silueta primero dice
        //  donde esta el pad; la marca encima, donde esta la tapa. Lo que
        //  queda gris es el hueco y nada mas.
        const auto af = transformaDeLaMarca ((float) lado);
        juce::Path silueta;
        silueta.addRoundedRectangle (0.0f, 0.0f, 24.0f, 24.0f, 3.5f);
        silueta.applyTransform (af);
        g.setColour (juce::Colours::grey);
        g.fillPath (silueta);

        auto m = Iconos::marca();
        m.applyTransform (af);
        g.setColour (juce::Colours::white);
        g.fillPath (m);
        return img;
    }

    inline juce::Image appIcon (int lado, bool conNumero = false,
                                Estilo estilo = kEstiloDeFabrica)
    {
        lado = juce::jmax (16, lado);
        juce::Image img (juce::Image::ARGB, lado, lado, true);
        juce::Graphics g (img);

        const auto L = (float) lado;

        //  El cuerpo, con el mismo degradado que la cara de la maquina. Opaco:
        //  un icono con alfa se lo pinta el lanzador por debajo y deja de ser
        //  nuestro.
        g.setGradientFill (juce::ColourGradient (ZatiColours::chassisTop, 0.0f, 0.0f,
                                                 ZatiColours::chassisBot, 0.0f, L, false));
        g.fillRect (0.0f, 0.0f, L, L);

        //  LA MARCA SOLA: un pad gigante con la Z recortada, o sea exactamente
        //  lo que lleva la cabecera. No hay rejilla que dibujar, asi que sale
        //  por aqui y se acaba. Se le da el mismo margen que a la rejilla -una
        //  fila de pad por lado- para que la mascara del lanzador no se la
        //  coma, y el bloque de profundidad detras, que es lo que la separa de
        //  una pegatina.
        if (estilo == Estilo::marcaSola || estilo == Estilo::marcaVentana
            || estilo == Estilo::marcaTapa || estilo == Estilo::marcaBanda
            || estilo == Estilo::marcaOnda)
        {
            const auto caja = cajaDeLaMarca (L);
            const auto af = transformaDeLaMarca (L);

            //  SIN BLOQUE DE PROFUNDIDAD, y no por ahorrar: la Z es un HUECO,
            //  asi que cualquier cosa que se dibuje detras se ve POR DENTRO de
            //  la letra. Con el bloque puesto salia una segunda Z gris dentro
            //  de la primera. La cabecera la dibuja plana por lo mismo, y la
            //  marca es un logo y no una tapa que se hunda al pulsarla.

            auto m = Iconos::marca();
            m.applyTransform (af);

            //  LOS OCHO ZATIS DETRAS, para que se vean POR EL HUECO. Es la
            //  unica forma de meter el color de los pads sin darle un color a
            //  la letra: la Z sigue siendo lo que se quita, y lo que asoma por
            //  ella es la firma de la caja.
            //  Y RECORTADOS A LA SILUETA DE LA TAPA, que es la tapa SIN el
            //  hueco: pintar las bandas en el rectangulo entero las deja
            //  asomando por las cuatro esquinas redondeadas -se veia un filo
            //  rojo y uno magenta por fuera del icono-.
            juce::Path silueta;
            silueta.addRoundedRectangle (0.0f, 0.0f, 24.0f, 24.0f, 3.5f);
            silueta.applyTransform (af);

            //  Y AL 62 %, QUE ES COMO LA APP PINTA ESTOS MISMOS OCHO.
            //
            //  No es un numero nuevo: `PadArt::cuerpoDe` es lo que lleva el
            //  cuerpo de un pad cargado, y esto es un campo de color del
            //  tamano de una tapa. A pleno los ocho miden C* 59.4 de croma
            //  contra los 39.3 que mide un pad de la cara - o sea el icono
            //  saliendo x1.51 mas saturado que la maquina que abre, que es la
            //  misma pregunta comparativa con la que ya se juzga su contraste.
            //  El zati a pleno existe en la app en la BANDA de 5 px de un pad
            //  y en el pad que suena; donde los ocho salen juntos, solo lo
            //  activo va a pleno (la tira de la cabecera pinta el resto a 0.45
            //  y el muestrario del pad a 0.38).
            if (estilo == Estilo::marcaVentana)
            {
                juce::Graphics::ScopedSaveState guarda (g);
                g.reduceClipRegion (silueta);
                const float bw = caja.getWidth() / (float) Zati::kNumColours;
                for (int i = 0; i < Zati::kNumColours; ++i)
                {
                    g.setColour (PadArt::cuerpoDe (Zati::colour (i), true));
                    g.fillRect (caja.getX() + bw * (float) i, caja.getY(), bw, caja.getHeight());
                }
            }

            //  O LA ONDA DE UN SONIDO DE FABRICA DE VERDAD, vista por el
            //  hueco. Es lo que le faltaba a la marca para decir SAMPLER: un
            //  pad con una Z podria ser cualquier app; un pad con una Z que
            //  deja ver una onda no.
            //
            //  De `Kits::render` y no de un garabato: la onda que sale del
            //  icono es una de las sesenta y cuatro que trae la app. Cacheada
            //  en un `static` porque sintetizar cuesta y el icono se dibuja
            //  hasta cinco veces por corrida del banco - con el indice al
            //  lado, que `ZATI_ICONO_SONIDO` lo mueve y una cache sin llave
            //  devolveria el candidato anterior.
            //
            //  Y DETRAS DE LA ONDA NO VA UN CRISTAL, que fue el primer intento
            //  y salio medido: el hueco quedaba oscuro sobre una tapa que en
            //  tres carcasas de cuatro es la TINTA, o sea oscura tambien - la
            //  Z contra la tapa daba 1.01 en PAPEL con el suelo comparativo en
            //  1.60. La tapa es el acento y el acento se elige para leerse
            //  sobre el CHASIS, asi que el chasis es la unica superficie que
            //  contrasta con ella por construccion. El hueco deja ver el
            //  cuerpo de la maquina, igual que en `marcaSola`, y lo que se
            //  anade es la onda.
            //
            //  La onda va con LOS OCHO ZATIS y no con un color elegido a mano:
            //  es el material que ya usan las otras tres marcas, y en tres
            //  carcasas el acento es acromatico - un color del sistema se
            //  separa de la tapa por croma y no solo por luminancia.
            if (estilo == Estilo::marcaOnda)
            {
                //  UNA ONDA GENERICA, y no la envolvente de un sonido de
                //  fabrica de verdad, que es lo que habia y lo que este
                //  fichero defendia. Se deshizo midiendo, y la razon es de
                //  ESCALA y no de gusto: la envolvente real tiene su detalle
                //  en los milisegundos y el icono se mira a 48 px. Barrida con
                //  7, 9, 11, 13 y 56 columnas, a esa escala da el mismo borron
                //  -con 56, en el icono mas pequeno que JUCE escribe cada
                //  columna mide 0.43 px- y a 256 px sigue siendo una mancha
                //  cruzando la Z: la silueta solo aparece a 1024.
                //
                //  Lo que si sobrevive es un patron de POCAS formas GRANDES.
                //  Barras con hueco entre ellas, que es como se dibuja el
                //  audio en cualquier aparato, y el hueco es lo que hace el
                //  trabajo: por el se ve el chasis, asi que la tira de la Z
                //  sale partida en trozos en vez de rellena de color. Una
                //  envolvente continua llena la rendija entera y por eso no se
                //  leia.
                //
                //  Nueve barras: el hueco de la Z son tres tiras y el ancho de
                //  la tapa 24 unidades, asi que nueve pasos son 2.67 unidades
                //  cada uno - a 48 px de icono, 3.6 px de barra mas hueco, que
                //  es lo minimo que sobrevive a la reduccion. Con mas barras
                //  el hueco se cierra y vuelve el borron.
                {
                    //  Las alturas, a mano y a proposito: una onda de verdad
                    //  no hace falta y ademas no cabe. Lo que tiene que decir
                    //  el dibujo es «audio», y eso lo dice una silueta
                    //  irregular con un pico claro - una montana simetrica se
                    //  lee como un grafico y no como un sonido.
                    static constexpr float kAlturas[] =
                        { 0.42f, 0.86f, 0.58f, 1.00f, 0.70f, 0.94f, 0.52f, 0.78f, 0.38f };
                    const int nb = (int) (sizeof (kAlturas) / sizeof (float));

                    //  Y LA ALTURA, medida y no elegida: a 5.5 las barras
                    //  cruzan las DOS barras de la Z y la letra se lee como un
                    //  codigo de barras; a 4.0 empiezan a comerselas. A 3.5
                    //  quedan dentro de la cintura -la Z vive entre 6 y 18 de
                    //  24- y la letra sale entera con la onda dentro.
                    const float cy = 12.0f, halfH = 3.5f;
                    const float paso = 24.0f / (float) nb;
                    const float ancho = paso * 0.62f;      // el resto es hueco

                    juce::Path onda;
                    for (int b = 0; b < nb; ++b)
                    {
                        const float h = juce::jmax (0.6f, kAlturas[b] * halfH);
                        const float x = paso * ((float) b + 0.5f) - ancho * 0.5f;
                        //  Redondeada por los extremos, como la barra de un
                        //  medidor: a 48 px un canto vivo se come un pixel y
                        //  la barra se lee como un cuadrado.
                        onda.addRoundedRectangle (x, cy - h, ancho, h * 2.0f, ancho * 0.5f);
                    }
                    onda.applyTransform (af);

                    //  UN TONO Y NO OCHO, Y AL 62 %.
                    //
                    //  La primera version pintaba la onda con los ocho zatis a
                    //  pleno, y eso son NUEVE tonos a maxima saturacion dentro
                    //  de una marca de 48 px - contando la tapa. Medido en
                    //  croma (C* de Lab): los ocho a pleno dan 59.4 contra los
                    //  39.3 que mide un pad cargado de la cara, o sea el icono
                    //  x1.51 mas saturado que la maquina que abre.
                    //
                    //  El tono es DERIVADO y no elegido a mano: el que ese pad
                    //  tiene en la maquina. Y pintado como se pinta un pad
                    //  -`PadArt::cuerpoDe`, que ya existe- en vez de con un
                    //  alfa escrito aqui.
                    juce::Graphics::ScopedSaveState guarda (g);
                    g.setColour (PadArt::cuerpoDe (Zati::colour (Zati::forPad (sonidoDeLaMarca)), true));
                    g.fillPath (onda);
                }
            }

            //  O LA TAPA ES LOS OCHO y el hueco deja ver el chasis, que es lo
            //  contrario: aqui el color esta donde estan los pads y la letra es
            //  el cuerpo de la maquina.
            if (estilo == Estilo::marcaTapa)
            {
                juce::Graphics::ScopedSaveState guarda (g);
                g.reduceClipRegion (m);
                const float bw = caja.getWidth() / (float) Zati::kNumColours;
                for (int i = 0; i < Zati::kNumColours; ++i)
                {
                    //  Al 62 %, por lo mismo que la ventana de aqui arriba.
                    g.setColour (PadArt::cuerpoDe (Zati::colour (i), true));
                    g.fillRect (caja.getX() + bw * (float) i, caja.getY(), bw, caja.getHeight());
                }
            }
            else
            {
                g.setColour (ZatiColours::accent);
                g.fillPath (m);
            }

            //  O LA BANDA DE UN PAD, que es la mas fiel de las tres: un pad
            //  cargado lleva su zati en una banda solida arriba (ver
            //  PadArt::fondo), y esta marca ES un pad. La banda va por encima
            //  de la barra de la Z -que empieza en 6 de 24- asi que no la toca.
            //
            //  Y ESTA SE QUEDA A PLENO, que es la excepcion de las cuatro y no
            //  un descuido: la banda de un pad cargado va a pleno en la app
            //  (PadArt::fondo, `solida ? frag : ...`). Lo que se atenua es el
            //  CAMPO de color, no la banda - bajar esta seria dejar de
            //  parecerse justo a lo que copia.
            if (estilo == Estilo::marcaBanda)
            {
                juce::Graphics::ScopedSaveState guarda (g);
                g.reduceClipRegion (m);
                //  CINCO DE CUARENTA Y OCHO, no de veinticuatro. La banda
                //  de un pad mide 5 px sobre el alto de referencia de 48
                //  (PadArt::kAltoRef) y la marca se dibuja en una caja de 24,
                //  asi que 5/24 la sacaba del DOBLE de gruesa que la del pad
                //  al que dice parecerse. Salio al medir el croma: es la unica
                //  de las cuatro marcas que se queda a pleno, y entonces cuanta
                //  superficie ocupa deja de ser un detalle.
                const auto banda = caja.withHeight (caja.getHeight() * 5.0f / 48.0f)
                                       .translated (0.0f, caja.getHeight() * 1.0f / 48.0f);
                const float bw = banda.getWidth() / (float) Zati::kNumColours;
                for (int i = 0; i < Zati::kNumColours; ++i)
                {
                    g.setColour (Zati::colour (i));
                    g.fillRect (banda.getX() + bw * (float) i, banda.getY(), bw, banda.getHeight());
                }
            }

            return img;
        }

        //  EL MARCO ES UNA FILA DE PADS QUE NO SE DIBUJA.
        //
        //  El margen no es un porcentaje elegido a ojo: se imagina la rejilla
        //  con UNA FILA MAS POR CADA LADO -o sea 6x6 y no 5x5, que una fila por
        //  cada lado son dos filas- ocupando el marco entero, y esa fila de
        //  fuera no se pinta. Asi el aire de alrededor mide exactamente un pad
        //  y el icono se lee como un trozo de la maquina y no como un dibujo
        //  centrado en un cuadrado.
        //
        //  Y hace falta que sobre sitio, no es decoracion: JUCE escribe el
        //  icono como LEGACY -drawable-{ldpi,mdpi,hdpi,xhdpi}, sin adaptive
        //  icon ni ic_launcher_round, ver
        //  jucer_ProjectExport_Android.h::writeIcons- asi que de Android 8 en
        //  adelante el lanzador lo mete en su mascara. Con la fila invisible,
        //  la esquina de la tapa mas exterior queda a 0.471 x lado del centro
        //  contra un radio de 0.5: dentro. El icono de hoy la tenia a 0.527 y
        //  se cortaba bajo mascara redonda.
        const int total = Iconos::kLadoMarca + 2;              // la fila de fuera, por los dos lados
        const float paso = L / (float) total;
        const float hueco = paso * 0.14f;                      // en PROPORCION, como la cara:
        const float celda = paso - hueco;                      // un hueco fijo desaparece a 48 px
        const float x0 = paso, y0 = paso;                      // se empieza en la primera visible

        //  LA TAPA ES LA MISMA QUE LA DE LA CARA, no una parecida. Ver
        //  PadArt::tapa: bloque de profundidad, cuerpo al 62 % del zati, banda
        //  solida arriba y borde del propio color. La escala sale del alto de
        //  la celda contra el alto para el que estan escritos esos numeros, o
        //  la banda de 5 px seria un pelo en una celda de 146.
        const float escala = celda / PadArt::kAltoRef;

        //  Y LA PLACA DEBAJO, que es donde los pads estan atornillados.
        //
        //  Faltaba, y no era decoracion: el cuerpo de un pad es su zati al
        //  62 % de ALFA, o sea que lo que hay debajo decide como sale. Sin
        //  placa, los diez encendidos caian directamente sobre el chasis
        //  oscuro y salian apagados - el peor par contra un vacio media 2.12
        //  con el liston en 3.00. En la cara nunca caen ahi: van sobre una
        //  placa hundida, un escalon de tono por encima. Con ella el color es
        //  el que se ve en la app.
        //
        //  Las mismas tres pasadas que pinta la cara: relleno, filo y el labio
        //  que coge la luz. Y la fila invisible de alrededor deja de ser aire
        //  vacio para ser el chasis asomando por fuera de la placa, que es
        //  exactamente lo que se ve al mirar la maquina.
        const auto placa = juce::Rectangle<float> (x0, y0, paso * (float) Iconos::kLadoMarca,
                                                   paso * (float) Iconos::kLadoMarca)
                               .reduced (hueco * 0.5f)
                               .expanded (hueco);
        {
            const float rp = 4.0f * escala;
            g.setColour (ZatiColours::plate);
            g.fillRoundedRectangle (placa, rp);
            g.setColour (ZatiColours::plateEdge);
            g.drawRoundedRectangle (placa.reduced (0.5f * escala), rp, 1.0f * escala);
            g.setColour (ZatiColours::white.withAlpha (0.55f));
            g.drawRoundedRectangle (placa.reduced (1.6f * escala), rp, 1.0f * escala);
        }

        //  LA TIRA DE OCHO COLORES, que es la firma de la caja: en la app va
        //  bajo el nombre y en el banner hace de borde de arriba. Aqui va DENTRO
        //  de la placa y por tanto CUESTA alto de rejilla - la celda encoge, y
        //  eso es justo lo que hay que medir antes de creerselo.
        float bajaRejilla = 0.0f;
        if (estilo == Estilo::conTira)
        {
            const float alto = placa.getHeight() * 0.055f;
            const auto tira = placa.reduced (3.0f * escala).withHeight (alto);
            const float sw = tira.getWidth() / (float) Zati::kNumColours;
            for (int i = 0; i < Zati::kNumColours; ++i)
            {
                g.setColour (Zati::colour (i));
                g.fillRect (tira.getX() + sw * (float) i, tira.getY(),
                            sw - 1.0f * escala, alto);
            }
            bajaRejilla = alto + 3.0f * escala;
        }

        for (int f = 0; f < Iconos::kLadoMarca; ++f)
            for (int c = 0; c < Iconos::kLadoMarca; ++c)
            {
                //  La tapa se encoge por arriba lo que el bloque de
                //  profundidad ocupa por abajo, o la fila de abajo se saldria
                //  de su celda: es el mismo withTrimmedBottom que hace la cara.
                juce::Rectangle<float> r (x0 + (float) c * paso + hueco * 0.5f,
                                          y0 + (float) f * paso + hueco * 0.5f + bajaRejilla,
                                          celda, celda - bajaRejilla * 0.25f);
                r = r.withTrimmedBottom (ZatiLookAndFeel::kCapLift * escala);

                //  Y EL COLOR DE UN ENCENDIDO NO SE ELIGE: es el que ese pad
                //  tiene en la maquina. La cara numera de abajo arriba -el 01
                //  abajo a la izquierda- y Zati::forPad reparte los ocho
                //  colores en ese orden, asi que el icono es literalmente el
                //  banco A con la Z encendida. Sale la barra de arriba en
                //  frios y la de abajo en calidos, y eso no lo decidio nadie
                //  aqui: es el orden de corte de la app.
                //
                //  El mismo (3 - fila) * 4 + col que usa el selector del RACK,
                //  por lo mismo: numerar al reves seria un mapa distinto del
                //  mismo instrumento.
                const int pad = (Iconos::kLadoMarca - 1 - f) * Iconos::kLadoMarca + c;
                //  LA Z AL REVES es la logica de marca(): alli la Z es un HUECO
                //  recortado en un pad, no un trazo encima. Aqui son los
                //  dieciseis cargados y los SEIS de la Z apagados.
                const bool cargado = (estilo == Estilo::invertida) ? ! Iconos::marcaCelda (f, c)
                                                                   : Iconos::marcaCelda (f, c);

                //  Y EL VACIO VA SIN SU ZATI, que es lo unico que separa este
                //  dibujo de la cara.
                //
                //  En la app un pad vacio lleva su color a un sexto de fuerza:
                //  «este pad SERIA turquesa», que a un palmo y con la onda, el
                //  numero y el nombre delante es un susurro util. En un icono
                //  de 48 px no hay onda, ni numero, ni nombre - solo queda el
                //  color, y dieciseis susurros distintos suman lo bastante para
                //  taparse con los diez que si suenan. Medido: el peor par
                //  encendido/apagado caia a 1.63 con el liston de esta casa en
                //  3.00, y la Z dejaba de leerse como forma.
                //
                //  Asi que el vacio se dibuja con la MISMA receta -bloque,
                //  cuerpo, banda de contorno y borde- cambiando solo de que
                //  color es el susurro: el gris del borde de un pad en vez de
                //  su zati. La tapa sigue siendo la de la app; lo que se quita
                //  es la unica capa que aqui no puede hacer su trabajo.
                //  UN SOLO COLOR: los encendidos en el acento, que es como esta
                //  maquina declara «esto esta activo». Se lee de mucho mas lejos
                //  -la Z es una sola mancha- y se pierde la tira de colores, que
                //  es la firma de la caja.
                const auto frag = ! cargado ? ZatiColours::padBorder
                                : (estilo == Estilo::unColor ? ZatiColours::accent
                                                             : Zati::colour (Zati::forPad (pad)));

                const auto cuerpo = PadArt::cuerpoDe (frag, cargado);
                PadArt::fondo (g, r, cuerpo, frag,
                               cargado ? PadArt::Banda::solida : PadArt::Banda::fantasma,
                               escala);
                if (conNumero)
                    PadArt::numero (g, r, pad + 1,
                                    ZatiColours::textOn (cuerpo).withAlpha (cargado ? 0.92f : 0.72f),
                                    escala);
                PadArt::borde (g, r, PadArt::bordeDe (frag, cargado), cargado, escala);
            }

        return img;
    }

    //  LAS DOS Z TIENEN QUE SER LA MISMA Z.
    //
    //  `Iconos::marcaCelda` dice que celdas se encienden en el icono y
    //  `Iconos::marca()` dibuja la Z recortada de la cabecera. Son dos escrituras
    //  de la misma idea, y dos escrituras de la misma idea se separan en cuanto
    //  alguien toque una - el juego entero se deshace sin que falle nada.
    //
    //  Asi que se rasteriza la Z DE VERDAD -el hueco de marca(), o sea donde el
    //  path NO contiene el punto- sobre la misma rejilla de 4x4 y se cuentan los
    //  desacuerdos. Con la caja de 24 y celdas de 6, se muestrea 5x5 dentro de
    //  cada una y manda la mayoria: la diagonal es fina y un solo punto en el
    //  centro no la ve.
    inline int desacuerdoMarca()
    {
        const auto p = Iconos::marca();
        //  LA Z NO OCUPA LA CAJA, OCUPA EL CUADRADO DE DENTRO. marca() es una
        //  tapa de 24 con la Z recortada entre 6 y 18, asi que rasterizar sobre
        //  los 24 mete las cuatro esquinas del margen en la cuenta y da DIEZ
        //  desacuerdos de dieciseis con las dos Z siendo la misma. La primera
        //  version lo hacia y por poco se "arregla" el dibujo que estaba bien.
        const float bordeZ = 6.0f, ladoZ = 12.0f;
        const float paso = ladoZ / (float) Iconos::kLadoMarca;
        int mal = 0;

        for (int f = 0; f < Iconos::kLadoMarca; ++f)
            for (int c = 0; c < Iconos::kLadoMarca; ++c)
            {
                int dentro = 0, total = 0;
                for (int a = 1; a <= 5; ++a)
                    for (int b = 1; b <= 5; ++b)
                    {
                        const float x = bordeZ + (float) c * paso + paso * (float) b / 6.0f;
                        const float y = bordeZ + (float) f * paso + paso * (float) a / 6.0f;
                        //  El hueco: la tapa esta rellena y la Z es lo que se
                        //  quita, asi que "es Z" es NO estar dentro del path.
                        if (! p.contains (x, y)) ++dentro;
                        ++total;
                    }

                const bool esZ = dentro * 2 > total;
                if (esZ != Iconos::marcaCelda (f, c)) ++mal;
            }

        return mal;
    }

    //  LOS DIECISEIS PADS DE LA CARA, TAL Y COMO SALEN.
    //
    //  El icono se juzga contra la maquina que abre y no contra un numero
    //  prestado. La primera version pedia el liston de «apagado contra
    //  encendido» de una TAPA -3.00- y esto no es una tapa: es un pad cargado
    //  al lado de uno vacio, un par que skins.py no tabula. Y en LACA la app
    //  misma sale baja ahi, porque el cuerpo de un pad es su zati al 62 % de
    //  ALFA sobre una placa oscura.
    //
    //  Aqui solo se DAN los colores -los del banco A, compuestos sobre la
    //  placa, que es donde estan de verdad-. Las cuentas las hace el banco con
    //  la formula que ya tiene: dos formulas de luminancia son dos formulas.
    inline void pintaPadsDeLaCara (std::ostream& out)
    {
        auto hex = [] (juce::Colour c) { return c.toDisplayString (false); };

        out << ",\"cara_cargados\":[";
        for (int i = 0; i < 16; ++i)
            out << (i ? "," : "") << "\""
                << hex (ZatiColours::plate.overlaidWith (
                            PadArt::cuerpoDe (Zati::colour (Zati::forPad (i)), true))) << "\"";
        out << "],\"cara_vacios\":[";
        for (int i = 0; i < 16; ++i)
            out << (i ? "," : "") << "\""
                << hex (ZatiColours::plate.overlaidWith (
                            PadArt::cuerpoDe (Zati::colour (Zati::forPad (i)), false))) << "\"";
        out << "]";
    }

    inline void writeIcon (const juce::String& path, int lado, bool conNumero = false,
                           Estilo estilo = kEstiloDeFabrica)
    {
        juce::File f (path);
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        juce::FileOutputStream out (f);
        if (! out.openedOk()) return;
        juce::PNGImageFormat png;
        png.writeImageToStream (appIcon (lado, conNumero, estilo), out);
        out.flush();
    }

    //  La mascara al lado del icono, cuando el banco la pide. Ver
    //  StoreArt::mascaraMarca.
    inline void writeMask (const juce::String& path, int lado)
    {
        juce::File f (path);
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        juce::FileOutputStream out (f);
        if (! out.openedOk()) return;
        juce::PNGImageFormat png;
        png.writeImageToStream (mascaraMarca (lado), out);
        out.flush();
    }

    inline void writeFeature (const juce::String& path, int w, int h)
    {
        juce::File f (path);
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        juce::FileOutputStream out (f);
        if (! out.openedOk()) return;
        juce::PNGImageFormat png;
        png.writeImageToStream (featureGraphic (w, h), out);
        out.flush();
    }
}
