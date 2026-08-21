#pragma once

#include <JuceHeader.h>
#include <vector>

// ============================================================================
//  LOS ICONOS, DIBUJADOS Y NO PEGADOS.
//
//  Un icono en un PNG es un icono con la paleta del dia que se exporto. Esta
//  app tiene CUATRO carcasas y la tinta de dos de ellas es clara: un juego de
//  sprites horneado sobre PAPEL sale invisible sobre GRAFITO, y nadie se
//  entera hasta que alguien mira la app en la carcasa oscura. Es exactamente
//  el fallo que ya costo una medida - la lista de proyectos, que se pintaba
//  con el chasis claro en las cuatro porque juce::ListBox guarda el color que
//  se le da y el suyo se ponia una vez, al construirla.
//
//  El argumento entero esta ya escrito en StoreArt.h para el grafico de la
//  tienda, y vale igual aqui: hecho con ZatiColours, el dibujo ES la app.
//
//  Ademas de la paleta: un trazo se dibuja al alto que haya. Estas tapas miden
//  26 px en dos de las siete pantallas y 44 en el resto, y un mapa de bits
//  pensado para 24 px escalado a 44 sale borroso justo en las pantallas donde
//  sobra sitio.
//
//  Se dibujan en una rejilla de 24x24 y se escalan a la caja que toque. El
//  grosor sale del lado, no de una constante: un trazo de dos pixeles es una
//  linea a 24 px y un pelo a 44.
//
//  QUE SE MIDE DE UN ICONO (ver Tests/iconos.py):
//   - que quepa en su caja - un trazo que se sale pinta sobre el rotulo;
//   - que tenga TINTA suficiente - un dibujo que ocupa el 4% de su caja es una
//     mancha, no un icono;
//   - y que no haya DOS QUE SE PAREZCAN, que es la unica regla que no se puede
//     juzgar mirando los iconos de uno en uno. Es la misma leccion que el banco
//     de la fabrica: sesenta y cuatro copias del mismo bombo sacan sobresaliente
//     en las cinco pruebas que miran un sonido por separado.
// ============================================================================
namespace Iconos
{
    enum class Id
    {
        ninguno = 0,
        play, stop, rec, tap,
        deshacer, rehacer,
        cargar, guardar, abrir, nuevo, borrar, exportar, carpeta,
        copiar, pegar, vaciar,
        insertar, quitar, acortar, alargar,
        atras, adelante, doblar, humanizar, goma, tijeras, loop, cuadrar,
        pads, sec, piano, mezcla, cancion, xy, ajustes, rack, chop, instrumentos, manual,
        sonido, recorte,
        kNum
    };

    inline const char* nombre (Id i)
    {
        switch (i)
        {
            case Id::play: return "play";              case Id::stop: return "stop";
            case Id::rec: return "rec";                case Id::tap: return "tap";
            case Id::deshacer: return "deshacer";      case Id::rehacer: return "rehacer";
            case Id::cargar: return "cargar";          case Id::guardar: return "guardar";
            case Id::abrir: return "abrir";            case Id::nuevo: return "nuevo";
            case Id::borrar: return "borrar";          case Id::exportar: return "exportar";
            case Id::carpeta: return "carpeta";        case Id::copiar: return "copiar";
            case Id::pegar: return "pegar";            case Id::vaciar: return "vaciar";
            case Id::insertar: return "insertar";      case Id::quitar: return "quitar";
            case Id::acortar: return "acortar";        case Id::alargar: return "alargar";
            case Id::atras: return "atras";            case Id::adelante: return "adelante";
            case Id::doblar: return "doblar";
            case Id::humanizar: return "humanizar";    case Id::goma: return "goma";
            case Id::tijeras: return "tijeras";        case Id::loop: return "loop";
            case Id::cuadrar: return "cuadrar";        case Id::pads: return "pads";
            case Id::sec: return "sec";                case Id::piano: return "piano";
            case Id::mezcla: return "mezcla";          case Id::cancion: return "cancion";
            case Id::xy: return "xy";                  case Id::ajustes: return "ajustes";
            case Id::rack: return "rack";              case Id::chop: return "chop";
            case Id::instrumentos: return "instrumentos"; case Id::manual: return "manual";
            case Id::sonido: return "sonido";          case Id::recorte: return "recorte";
            case Id::ninguno:
            case Id::kNum:
            default: return "ninguno";
        }
    }

    //  UN ICONO SON DOS CAMINOS Y NO UNO. Lo que se rellena y lo que se traza
    //  no se pueden mezclar en un solo Path: rellenar el contorno de unas
    //  tijeras da una mancha con forma de tijeras.
    struct Trazo { juce::Path linea, relleno; };

    namespace detalle
    {
        //  Una flecha: la punta es un triangulo RELLENO y no dos trazos en
        //  angulo. A 24 px dos trazos que se cruzan dejan un pixel de hueco en
        //  el vertice y la flecha se lee como una uve.
        inline void punta (juce::Path& p, float x, float y, float dx, float dy, float lado)
        {
            const float nx = -dy, ny = dx;    // perpendicular
            p.startNewSubPath (x, y);
            p.lineTo (x - dx * lado + nx * lado * 0.62f, y - dy * lado + ny * lado * 0.62f);
            p.lineTo (x - dx * lado - nx * lado * 0.62f, y - dy * lado - ny * lado * 0.62f);
            p.closeSubPath();
        }

        inline void linea (juce::Path& p, float x1, float y1, float x2, float y2)
        {
            p.startNewSubPath (x1, y1);
            p.lineTo (x2, y2);
        }

        //  La carpeta, que la usan tres iconos y se dibujaba tres veces.
        inline void carpetaBase (juce::Path& p, float y0)
        {
            p.startNewSubPath (3.0f, 20.0f);
            p.lineTo (3.0f, y0);
            p.lineTo (9.5f, y0);
            p.lineTo (11.5f, y0 + 2.5f);
            p.lineTo (21.0f, y0 + 2.5f);
            p.lineTo (21.0f, 20.0f);
            p.closeSubPath();
        }
    }

    //  El dibujo, en la rejilla de 24x24. Todo lo de aqui abajo son
    //  coordenadas de esa rejilla; nada sabe a que tamano se va a pintar.
    inline Trazo trazo (Id id)
    {
        using namespace detalle;
        Trazo t;
        auto& L = t.linea;
        auto& R = t.relleno;

        switch (id)
        {
            //  LOS TRES DEL TRANSPORTE, MEDIDOS: salian a la mitad de su caja
            //  -0.50 de 0.70- asi que en una fila junto a un icono normal se
            //  leian como si estuvieran mas lejos. Y el circulo relleno de REC
            //  contra el cuadrado relleno de STOP daba 0.079 de distancia, o
            //  sea el mismo dibujo: dos manchas compactas del mismo tamano se
            //  distinguen por sus esquinas y nada mas. REC lleva ahora ANILLO,
            //  que ademas es como se dibuja en un aparato de verdad.
            case Id::play:
                R.startNewSubPath (5.5f, 3.0f); R.lineTo (20.5f, 12.0f);
                R.lineTo (5.5f, 21.0f); R.closeSubPath();
                break;

            case Id::stop:
                R.addRoundedRectangle (3.5f, 3.5f, 17.0f, 17.0f, 1.5f);
                break;

            case Id::rec:
                L.addEllipse (3.0f, 3.0f, 18.0f, 18.0f);
                R.addEllipse (7.4f, 7.4f, 9.2f, 9.2f);
                break;

            //  TAP pone el tempo, asi que es un metronomo y no una mano: una
            //  mano tocando es lo que hace CUALQUIER tapa de esta app.
            case Id::tap:
                L.startNewSubPath (12.0f, 3.0f); L.lineTo (19.0f, 20.5f);
                L.lineTo (5.0f, 20.5f); L.closeSubPath();
                linea (L, 7.0f, 16.0f, 17.0f, 16.0f);
                linea (L, 12.0f, 16.0f, 16.0f, 6.0f);
                R.addRectangle (14.6f, 7.4f, 3.2f, 2.4f);
                break;

            case Id::deshacer:
                L.addCentredArc (12.0f, 13.5f, 7.0f, 6.0f, 0.0f,
                                 juce::MathConstants<float>::pi * 1.62f,
                                 juce::MathConstants<float>::pi * 0.30f, true);
                punta (R, 4.4f, 12.0f, 0.0f, -1.0f, 4.2f);
                break;

            case Id::rehacer:
                L.addCentredArc (12.0f, 13.5f, 7.0f, 6.0f, 0.0f,
                                 -juce::MathConstants<float>::pi * 0.30f,
                                 -juce::MathConstants<float>::pi * 1.62f, true);
                punta (R, 19.6f, 12.0f, 0.0f, -1.0f, 4.2f);
                break;

            //  CARGAR mete algo en la maquina: la flecha entra en la bandeja.
            case Id::cargar:
                L.startNewSubPath (3.5f, 15.0f); L.lineTo (3.5f, 20.5f);
                L.lineTo (20.5f, 20.5f); L.lineTo (20.5f, 15.0f);
                linea (L, 12.0f, 2.5f, 12.0f, 12.5f);
                punta (R, 12.0f, 17.0f, 0.0f, 1.0f, 4.6f);
                break;

            case Id::guardar:
                L.addRoundedRectangle (3.5f, 3.5f, 17.0f, 17.0f, 1.6f);
                L.addRectangle (8.0f, 12.5f, 8.0f, 8.0f);
                R.addRectangle (9.0f, 3.5f, 6.0f, 5.0f);
                break;

            //  ABRIR es la carpeta ABIERTA - el frente separado del cuerpo - y
            //  no la misma carpeta de `carpeta` con otro rotulo.
            case Id::abrir:
                carpetaBase (L, 6.0f);
                L.startNewSubPath (3.0f, 20.0f); L.lineTo (6.5f, 11.5f);
                L.lineTo (23.0f, 11.5f); L.lineTo (19.5f, 20.0f); L.closeSubPath();
                break;

            case Id::carpeta:
                carpetaBase (L, 5.0f);
                break;

            case Id::nuevo:
                L.startNewSubPath (5.5f, 2.5f); L.lineTo (13.5f, 2.5f);
                L.lineTo (18.5f, 7.5f); L.lineTo (18.5f, 21.5f);
                L.lineTo (5.5f, 21.5f); L.closeSubPath();
                L.startNewSubPath (13.5f, 2.5f); L.lineTo (13.5f, 7.5f);
                L.lineTo (18.5f, 7.5f);
                linea (L, 12.0f, 11.0f, 12.0f, 18.0f);
                linea (L, 8.5f, 14.5f, 15.5f, 14.5f);
                break;

            case Id::borrar:
                linea (L, 4.0f, 6.5f, 20.0f, 6.5f);
                L.startNewSubPath (9.5f, 6.5f); L.lineTo (9.5f, 3.5f);
                L.lineTo (14.5f, 3.5f); L.lineTo (14.5f, 6.5f);
                L.startNewSubPath (6.5f, 6.5f); L.lineTo (7.6f, 21.0f);
                L.lineTo (16.4f, 21.0f); L.lineTo (17.5f, 6.5f);
                break;

            //  EXPORTAR sale del aparato POR LA ESQUINA y no por arriba: con la
            //  flecha vertical era `cargar` del reves, y dos iconos que solo se
            //  diferencian en el sentido de una flecha se confunden a 24 px.
            case Id::exportar:
                L.startNewSubPath (12.5f, 3.5f); L.lineTo (3.5f, 3.5f);
                L.lineTo (3.5f, 20.5f); L.lineTo (20.5f, 20.5f); L.lineTo (20.5f, 11.5f);
                linea (L, 11.0f, 13.0f, 19.0f, 5.0f);
                punta (R, 21.0f, 3.0f, 0.72f, -0.72f, 4.4f);
                break;

            case Id::copiar:
                L.addRoundedRectangle (3.5f, 3.5f, 12.0f, 12.0f, 1.6f);
                L.addRoundedRectangle (8.5f, 8.5f, 12.0f, 12.0f, 1.6f);
                break;

            case Id::pegar:
                L.addRoundedRectangle (4.5f, 4.0f, 15.0f, 17.0f, 1.6f);
                R.addRoundedRectangle (8.5f, 2.0f, 7.0f, 4.5f, 1.0f);
                linea (L, 8.0f, 11.5f, 16.0f, 11.5f);
                linea (L, 8.0f, 15.5f, 16.0f, 15.5f);
                break;

            //  VACIAR no es la papelera: BORRAR se lleva un fichero y esto vacia
            //  lo que hay escrito. Con las cuatro celdas tachadas que llevaba
            //  antes era `pads` -0.30- y ademas `rack` -0.37-: una rejilla de
            //  bloques es el dibujo mas repetido de esta app.
            case Id::vaciar:
                L.addRoundedRectangle (3.0f, 3.0f, 18.0f, 18.0f, 2.0f);
                linea (L, 8.2f, 8.2f, 15.8f, 15.8f);
                linea (L, 15.8f, 8.2f, 8.2f, 15.8f);
                break;

            //  INSERTAR y QUITAR se dibujan por lo que le pasa al COMPAS del
            //  medio y no por un mas y un menos: dos iconos identicos salvo un
            //  trazo son el mismo icono. Aqui el de en medio ENTRA (esta abajo,
            //  hueco, con la flecha bajando) o SALE (arriba, fuera de la fila).
            case Id::insertar:
                R.addRectangle (2.5f, 13.0f, 6.0f, 8.0f);
                R.addRectangle (15.5f, 13.0f, 6.0f, 8.0f);
                L.addRectangle (9.5f, 13.0f, 5.0f, 8.0f);
                linea (L, 12.0f, 2.0f, 12.0f, 7.5f);
                punta (R, 12.0f, 11.0f, 0.0f, 1.0f, 3.8f);
                break;

            case Id::quitar:
                R.addRectangle (2.5f, 13.0f, 6.0f, 8.0f);
                R.addRectangle (15.5f, 13.0f, 6.0f, 8.0f);
                L.addRectangle (9.5f, 2.5f, 5.0f, 7.0f);
                linea (L, 9.5f, 17.0f, 14.5f, 17.0f);
                break;

            //  ACORTAR y ALARGAR: la barra del medio es CORTA. Con la barra de
            //  arriba abajo y las dos flechas hacia fuera, ALARGAR se dibujaba
            //  como una CRUZ - las puntas a 3.8 en una caja de 18 px reales no
            //  pesan lo que la barra - y una cruz no dice "alargar", dice
            //  "mas". Se vio en la captura, no en el volcado: la distancia
            //  entre los dos siguio siendo la misma.
            case Id::acortar:
                linea (L, 12.0f, 5.5f, 12.0f, 18.5f);
                linea (L, 2.5f, 12.0f, 8.0f, 12.0f);
                linea (L, 16.0f, 12.0f, 21.5f, 12.0f);
                punta (R, 9.6f, 12.0f, 1.0f, 0.0f, 3.8f);
                punta (R, 14.4f, 12.0f, -1.0f, 0.0f, 3.8f);
                break;

            case Id::alargar:
                linea (L, 12.0f, 8.5f, 12.0f, 15.5f);
                linea (L, 5.5f, 12.0f, 18.5f, 12.0f);
                punta (R, 2.0f, 12.0f, -1.0f, 0.0f, 4.4f);
                punta (R, 22.0f, 12.0f, 1.0f, 0.0f, 4.4f);
                break;

            case Id::atras:
                linea (L, 3.5f, 4.0f, 3.5f, 20.0f);
                linea (L, 21.0f, 12.0f, 10.0f, 12.0f);
                punta (R, 6.5f, 12.0f, -1.0f, 0.0f, 4.6f);
                break;

            case Id::adelante:
                linea (L, 20.5f, 4.0f, 20.5f, 20.0f);
                linea (L, 3.0f, 12.0f, 14.0f, 12.0f);
                punta (R, 17.5f, 12.0f, 1.0f, 0.0f, 4.6f);
                break;

            //  DOBLAR: el compas que hay y su copia, con la vuelta que los une.
            //
            //  La primera version eran cuatro pasos llenos y los mismos cuatro
            //  huecos a continuacion, y el banco lo canto: 0.27 contra `sec`,
            //  que es exactamente el mismo dibujo -una fila de barras
            //  alternadas- y ademas 0.37 contra `chop`. Tres iconos de filas
            //  de barras en la misma app son un icono repetido tres veces.
            case Id::doblar:
                R.addRectangle (2.0f, 10.0f, 8.0f, 10.5f);
                L.addRectangle (14.0f, 10.0f, 8.0f, 10.5f);
                L.addCentredArc (12.0f, 10.5f, 8.0f, 6.0f, 0.0f, -1.35f, 1.20f, true);
                punta (R, 19.6f, 8.4f, 0.36f, 0.93f, 3.8f);
                break;

            //  HUMANIZAR es lo que NO esta en la rejilla: cuatro golpes que no
            //  caen en su sitio y no miden lo mismo, sobre la linea que dice
            //  donde tendrian que estar.
            case Id::humanizar:
            {
                linea (L, 2.0f, 20.5f, 22.0f, 20.5f);
                const float x[4] = { 3.6f, 9.4f, 13.2f, 19.6f };
                const float h[4] = { 9.0f, 14.5f, 6.5f, 12.0f };
                for (int i = 0; i < 4; ++i)
                    R.addRectangle (x[i], 20.5f - h[i], 2.6f, h[i]);
                break;
            }

            case Id::goma:
                L.startNewSubPath (2.5f, 15.5f); L.lineTo (10.5f, 4.5f);
                L.lineTo (18.0f, 9.5f); L.lineTo (10.0f, 20.5f); L.closeSubPath();
                linea (L, 6.5f, 10.0f, 14.0f, 15.0f);
                linea (L, 10.0f, 20.5f, 21.5f, 20.5f);
                break;

            case Id::tijeras:
                L.addEllipse (3.0f, 15.5f, 5.5f, 5.5f);
                L.addEllipse (15.5f, 15.5f, 5.5f, 5.5f);
                linea (L, 19.0f, 3.0f, 7.5f, 16.5f);
                linea (L, 5.0f, 3.0f, 16.5f, 16.5f);
                break;

            case Id::loop:
                L.addRoundedRectangle (3.5f, 6.5f, 17.0f, 11.0f, 4.0f);
                R.addRectangle (10.0f, 4.6f, 6.0f, 4.0f);
                punta (R, 17.5f, 6.6f, 1.0f, 0.0f, 4.2f);
                break;

            //  CUADRAR lleva los golpes A la rejilla: las lineas de la rejilla
            //  y un golpe que se mueve a la de al lado.
            case Id::cuadrar:
                linea (L, 5.0f, 2.5f, 5.0f, 21.5f);
                linea (L, 12.0f, 2.5f, 12.0f, 21.5f);
                linea (L, 19.0f, 2.5f, 19.0f, 21.5f);
                R.addRectangle (3.6f, 5.0f, 2.8f, 6.0f);
                R.addRectangle (17.6f, 13.0f, 2.8f, 6.0f);
                linea (L, 14.0f, 16.0f, 15.6f, 16.0f);
                punta (R, 17.0f, 16.0f, 1.0f, 0.0f, 3.4f);
                break;

            //  PADS con AIRE. Con las celdas a 4 de 5 la rejilla se pinta casi
            //  llena y de lejos es un cuadrado solido: 0.40 contra `stop` y
            //  0.33 contra `rack`, y las tres tapas viven en la cara.
            case Id::pads:
                for (int y = 0; y < 4; ++y)
                    for (int x = 0; x < 4; ++x)
                        R.addRoundedRectangle (2.6f + (float) x * 5.2f, 2.6f + (float) y * 5.2f,
                                               3.4f, 3.4f, 0.8f);
                break;

            //  Ocho celdas, y CABEN. La primera version las repartia cada 5.4
            //  desde x=2 y la ultima acababa en 24.5 - mas el grosor del
            //  trazo, 25.5 en una rejilla de 24: se salia por la derecha y
            //  pintaba encima del rotulo. Lo canto el banco a la primera.
            case Id::sec:
                for (int i = 0; i < 4; ++i)
                {
                    R.addRectangle (1.9f + (float) i * 5.3f, 8.0f, 2.3f, 8.0f);
                    L.addRectangle (4.6f + (float) i * 5.3f, 8.0f, 2.3f, 8.0f);
                }
                break;

            case Id::piano:
                L.addRectangle (2.5f, 5.5f, 19.0f, 13.0f);
                linea (L, 8.83f, 5.5f, 8.83f, 18.5f);
                linea (L, 15.17f, 5.5f, 15.17f, 18.5f);
                R.addRectangle (6.9f, 5.5f, 3.4f, 7.6f);
                R.addRectangle (13.3f, 5.5f, 3.4f, 7.6f);
                break;

            //  MEZCLA con los tiradores REDONDOS. Con tres tacos rectangulares
            //  sobre tres lineas verticales era el mismo dibujo que `cuadrar`
            //  -0.36-, que tambien son lineas verticales con bloques encima.
            case Id::mezcla:
                for (int i = 0; i < 3; ++i)
                {
                    const float x = 5.0f + (float) i * 7.0f;
                    linea (L, x, 2.5f, x, 21.5f);
                }
                R.addEllipse (2.0f, 11.5f, 6.0f, 6.0f);
                R.addEllipse (9.0f, 5.0f, 6.0f, 6.0f);
                R.addEllipse (16.0f, 9.0f, 6.0f, 6.0f);
                break;

            case Id::cancion:
                linea (L, 2.0f, 21.0f, 22.0f, 21.0f);
                R.addRectangle (2.5f, 3.5f, 8.0f, 4.5f);
                R.addRectangle (12.0f, 3.5f, 4.0f, 4.5f);
                R.addRectangle (2.5f, 11.0f, 4.0f, 4.5f);
                R.addRectangle (8.0f, 11.0f, 12.0f, 4.5f);
                break;

            case Id::xy:
                L.addRectangle (2.5f, 2.5f, 19.0f, 19.0f);
                linea (L, 2.5f, 15.0f, 21.5f, 15.0f);
                linea (L, 8.5f, 2.5f, 8.5f, 21.5f);
                R.addEllipse (5.5f, 12.0f, 6.0f, 6.0f);
                break;

            case Id::ajustes:
            {
                //  Ocho dientes y un agujero. Es la rueda de siempre porque es
                //  la que se reconoce sin leer nada; inventar una aqui seria
                //  cambiar un icono universal por uno bonito.
                juce::Path rueda;
                rueda.addEllipse (4.0f, 4.0f, 16.0f, 16.0f);
                for (int i = 0; i < 8; ++i)
                {
                    juce::Path diente;
                    diente.addRectangle (10.6f, 1.2f, 2.8f, 5.0f);
                    diente.applyTransform (juce::AffineTransform::rotation (
                        juce::MathConstants<float>::twoPi * (float) i / 8.0f, 12.0f, 12.0f));
                    rueda.addPath (diente);
                }
                //  HUECA DE VERDAD y no con el agujero dibujado encima: el
                //  disco relleno daba 0.27 contra el cuadrado de `stop`, que
                //  es lo que pasa cuando dos iconos son dos manchas del mismo
                //  tamano. Con el centro CORTADO -regla par/impar- deja de ser
                //  una mancha y pasa a ser un anillo dentado.
                juce::Path hueco;
                hueco.addEllipse (8.0f, 8.0f, 8.0f, 8.0f);
                rueda.addPath (hueco);
                rueda.setUsingNonZeroWinding (false);
                R = rueda;
                break;
            }

            //  RACK: tres unidades con su MANDO, y el mando grande. Con el punto
            //  a 1.8 px las tres cajas se leian como tres barras y la rejilla
            //  de `pads` quedaba a 0.33 - y las dos son pestanas de la cara,
            //  o sea que se ven una al lado de la otra.
            case Id::rack:
                for (int i = 0; i < 3; ++i)
                {
                    const float y = 2.6f + (float) i * 6.6f;
                    L.addRoundedRectangle (2.5f, y, 19.0f, 5.4f, 1.2f);
                    R.addEllipse (4.2f, y + 1.0f, 3.4f, 3.4f);
                    R.addRectangle (10.0f, y + 2.1f, 8.5f, 1.2f);
                }
                break;

            //  CHOP: la muestra con sus MARCAS DE CORTE.
            //
            //  Dos intentos antes de acertar, los dos por lo mismo: en esta
            //  app hay muchas filas de bloques. La barra entera con tres
            //  lineas encima daba 0.37 contra `sec`, y partirla en cuatro
            //  trozos desiguales la llevo a 0.38 contra `sonido`, que son
            //  barras de alturas distintas. Lo que no tiene ningun otro icono
            //  del juego son las marcas: tres puntas apuntando al sitio por
            //  donde se corta.
            case Id::chop:
            {
                R.addRectangle (2.0f, 10.5f, 20.0f, 8.0f);
                const float x[3] = { 7.0f, 12.0f, 17.0f };
                for (int i = 0; i < 3; ++i)
                {
                    punta (R, x[i], 9.6f, 0.0f, 1.0f, 4.6f);
                    linea (L, x[i], 10.5f, x[i], 18.5f);
                }
                break;
            }

            //  INSTRUMENTOS es contenido que se instala: una caja cerrada con
            //  su cinta. No la carpeta - una carpeta es donde vive, no lo que es.
            case Id::instrumentos:
                L.startNewSubPath (12.0f, 2.5f); L.lineTo (21.5f, 7.0f);
                L.lineTo (21.5f, 17.0f); L.lineTo (12.0f, 21.5f);
                L.lineTo (2.5f, 17.0f); L.lineTo (2.5f, 7.0f); L.closeSubPath();
                linea (L, 2.5f, 7.0f, 12.0f, 11.5f);
                linea (L, 21.5f, 7.0f, 12.0f, 11.5f);
                linea (L, 12.0f, 11.5f, 12.0f, 21.5f);
                break;

            case Id::manual:
                L.startNewSubPath (12.0f, 5.5f);
                L.cubicTo (9.0f, 3.0f, 5.5f, 3.2f, 2.5f, 4.0f);
                L.lineTo (2.5f, 19.5f);
                L.cubicTo (5.5f, 18.7f, 9.0f, 18.5f, 12.0f, 21.0f);
                L.cubicTo (15.0f, 18.5f, 18.5f, 18.7f, 21.5f, 19.5f);
                L.lineTo (21.5f, 4.0f);
                L.cubicTo (18.5f, 3.2f, 15.0f, 3.0f, 12.0f, 5.5f);
                L.closeSubPath();
                linea (L, 12.0f, 5.5f, 12.0f, 21.0f);
                break;

            //  SONIDO es la ONDA, con su envolvente: un golpe que ataca y cae.
            //  SONIDO: una onda de verdad, con sus altibajos. Con la envolvente
            //  cayendo desde el ataque era una CUNA, y una cuna es el
            //  triangulo de `play`: 0.31 medidos entre las dos.
            case Id::sonido:
            {
                const float h[11] = { 2.0f, 6.5f, 3.0f, 9.0f, 4.5f, 7.5f,
                                      2.5f, 8.0f, 3.5f, 5.0f, 1.8f };
                for (int i = 0; i < 11; ++i)
                    R.addRectangle (2.0f + (float) i * 1.9f, 12.0f - h[i], 1.5f, h[i] * 2.0f);
                break;
            }

            case Id::recorte:
                L.startNewSubPath (8.0f, 2.5f); L.lineTo (4.0f, 2.5f);
                L.lineTo (4.0f, 21.5f); L.lineTo (8.0f, 21.5f);
                L.startNewSubPath (16.0f, 2.5f); L.lineTo (20.0f, 2.5f);
                L.lineTo (20.0f, 21.5f); L.lineTo (16.0f, 21.5f);
                R.addRectangle (10.6f, 7.0f, 2.8f, 10.0f);
                break;

            case Id::ninguno:
            case Id::kNum:
            default:
                break;
        }

        return t;
    }

    //  LA MARCA. Una tapa de pad con la Z cortada dentro, que es lo que esta
    //  maquina es: el nombre no flota al lado de un dibujo cualquiera, el
    //  dibujo ES un pad. Se corta con `setUsingNonZeroWinding(false)`, o sea
    //  que la Z es un HUECO y no un trazo encima - asi la marca funciona sobre
    //  cualquiera de las cuatro carcasas sin elegir un segundo color.
    inline juce::Path marca()
    {
        juce::Path p;
        p.addRoundedRectangle (0.0f, 0.0f, 24.0f, 24.0f, 3.5f);

        juce::Path z;
        z.startNewSubPath (6.0f, 6.0f);
        z.lineTo (18.0f, 6.0f);
        z.lineTo (18.0f, 9.2f);
        z.lineTo (11.4f, 14.8f);
        z.lineTo (18.0f, 14.8f);
        z.lineTo (18.0f, 18.0f);
        z.lineTo (6.0f, 18.0f);
        z.lineTo (6.0f, 14.8f);
        z.lineTo (12.6f, 9.2f);
        z.lineTo (6.0f, 9.2f);
        z.closeSubPath();

        p.addPath (z);
        p.setUsingNonZeroWinding (false);
        return p;
    }

    //  EL GROSOR SALE DEL LADO. Un trazo de 2 px es una linea en una caja de 24
    //  y un pelo en una de 44, y esta app dibuja las dos: la fila de modulos
    //  mide 26 px en dos de las siete pantallas y 44 en las otras cinco.
    inline float grosorPara (float lado) noexcept
    {
        return juce::jmax (1.15f, lado * 0.086f);
    }

    //  EL SUELO DE UN ICONO. Por debajo de esto un trazo de un pixel con
    //  antialias es una mancha gris, no un dibujo: se prefiere el rotulo solo.
    //  Ver ZatiLookAndFeel::reparteTapa, que es quien decide.
    static constexpr int kLadoMin = 13;

    inline void dibuja (juce::Graphics& g, Id id, juce::Rectangle<float> caja, juce::Colour c)
    {
        if (id == Id::ninguno || id == Id::kNum) return;

        //  Cuadrada y centrada: un icono estirado a la caja que le toque sale
        //  con la rueda de AJUSTES ovalada y las tijeras torcidas.
        const float lado = juce::jmin (caja.getWidth(), caja.getHeight());
        if (lado < 4.0f) return;
        caja = caja.withSizeKeepingCentre (lado, lado);

        auto t = trazo (id);
        const auto af = juce::AffineTransform::scale (lado / 24.0f)
                            .translated (caja.getX(), caja.getY());

        g.setColour (c);
        if (! t.relleno.isEmpty())
        {
            auto r = t.relleno; r.applyTransform (af);
            g.fillPath (r);
        }
        if (! t.linea.isEmpty())
        {
            auto l = t.linea; l.applyTransform (af);
            g.strokePath (l, juce::PathStrokeType (grosorPara (lado),
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        }
    }

    //  EL ICONO EN UNA REJILLA DE ALFAS, que es lo que el banco compara. Se
    //  rasteriza AQUI y no en Python porque lo que hay que comparar es lo que
    //  la app dibuja, no una idea de lo que dibuja: si un camino se sale de su
    //  caja o sale vacio, esto lo trae.
    inline std::vector<juce::uint8> rasteriza (Id id, int n)
    {
        juce::Image img (juce::Image::ARGB, n, n, true);
        {
            juce::Graphics g (img);
            dibuja (g, id, juce::Rectangle<float> (0.0f, 0.0f, (float) n, (float) n),
                    juce::Colours::white);
        }

        std::vector<juce::uint8> px ((size_t) (n * n), 0);
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                px[(size_t) (y * n + x)] = bd.getPixelColour (x, y).getAlpha();
        return px;
    }

    //  Y LOS LIMITES DEL DIBUJO EN SU REJILLA DE 24, para que "se sale de la
    //  caja" sea un numero. Con el trazo puesto, que es lo que se pinta: un
    //  camino que acaba en x=22 con un grosor de 2 pinta hasta 23.
    inline juce::Rectangle<float> limites (Id id)
    {
        auto t = trazo (id);
        const float w = grosorPara (24.0f) * 0.5f;

        juce::Rectangle<float> r;
        bool hay = false;
        if (! t.relleno.isEmpty()) { r = t.relleno.getBounds(); hay = true; }
        if (! t.linea.isEmpty())
        {
            const auto lb = t.linea.getBounds().expanded (w);
            r = hay ? r.getUnion (lb) : lb;
            hay = true;
        }
        return hay ? r : juce::Rectangle<float>();
    }
}
