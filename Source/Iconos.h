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
        atras, adelante, doblar, humanizar, lapiz, goma, tijeras, loop, cuadrar,
        pads, sec, piano, mezcla, cancion, xy, ajustes, rack, chop, instrumentos, manual,
        sonido, recorte,
        flt, hpf, drv, dly, bit, rev, eq, cmp, gte, dss, lim,
        mic, remuestrear, bombeo, autocut, sistema, cadena, patron, pad, fijo,
        //  --- LAS DIECISEIS FAMILIAS DE Sintes.h -------------------------
        //
        //  Un pad con instrumento se distingue de un pad con un golpe por lo
        //  que DICE, y un pad es una tapa de 60 px con un numero y un nombre
        //  recortado: "CUERDA PULS" no cabe. El dibujo si.
        //
        //  Se dibujan por lo que el instrumento ES -un teclado, unos tubos, una
        //  campana- y no por su forma de onda, con dos excepciones que se ganan
        //  el sitio: LEADS es una cuadrada y CLAVES un peine de pulsos, porque
        //  eso es exactamente lo que suena. Cuatro dibujos de onda seguidos se
        //  parecerian entre si, que es lo que la prueba de pares no perdona.
        insBajo, insSub, insEp, insOrgano, insCuerdas, insColchon, insPluck,
        insCampana, insMetales, insLead, insCoro, insGuitarra, insMazo,
        insClav, insFlauta, insArpa,
        midi, medir, altavoz, mano, momentaneo, niveles,
        cho, fla, pha, trm,
        rng, pit, wid, exc, trn, frz,
        //  --- LOS SEIS QUE FALTABAN, y por que faltaban ------------------
        //
        //  No se eligieron mirando: salieron de `Tests/planos.py`, que dibuja
        //  las 32 pantallas con las coordenadas de verdad y agrupa las tapas
        //  por PADRE y por banda de y -o sea por FILA-. Una fila con unas
        //  cuantas dibujadas y otras no no se lee como "aqui no cabia", se lee
        //  como una tapa a la que le falta algo, que es exactamente la regla
        //  que ya obligo a `filaDeIconos` a decidir la fila entera de golpe.
        //  Cinco filas tenian ese hueco y ninguna de las seis reglas del banco
        //  podia verlo: un hueco de dibujo se maqueta perfecto.
        //
        //    ASPECTO   junto a AUDIO y MIDI, en las pestanas de AJUSTES
        //    REV       junto a BUCLE, en el recorte del pad
        //    QUITAR RUIDO  la misma fila
        //    SIN SOLO  junto a RACK, en la mesa
        //    WAV       junto a MASTER y PISTAS, en EXPORTAR
        //
        //  Y `mandar`/`recibir` no son un hueco sino una fila ENTERA sin un
        //  solo dibujo -la que la persona senalo-. Son un par de opuestos,
        //  como acortar/alargar, asi que no pueden ser el mismo dibujo con la
        //  flecha girada: la bandeja de MANDAR esta hueca y la de RECIBIR
        //  llena, que es lo que separa un espejo de una copia.
        aspecto, reves, ruido, sinsolo, comprimir, mandar, recibir,
        //  --- Y LOS TRES QUE `Tests/planos.py` SEGUIA CANTANDO -------------
        //
        //  La regla de la fila lleva tandas diciendo que faltan estos tres y
        //  nadie la corria: `planos.py` no estaba en `banco.yml` -solo
        //  `plano.py`, que es otra prueba- asi que sus siete huecos eran rojo
        //  permanente sin que nada fallara. Es *un numero que nadie mira se
        //  publica*, otra vez.
        //
        //    SEL   junto a VACIAR, LAPIZ, GOMA y TIJERAS, en el piano
        //    CLIC  junto a GRABAR y AUTO, en la banda de audio de CANCION
        //    AUTO  la misma fila
        //
        //  Y GRABAR no necesitaba dibujo nuevo: `rec` existe desde el primer
        //  dia y a esa tapa no se le habia asignado. Un icono que ya esta y no
        //  se usa es la mitad de un hueco.
        sel, clic, automacion,
        //  --- Y EL DE SOLO DESDE LA CARA ------------------------------------
        //
        //  `sinsolo` ya existe -el arco con sus dos auriculares y la barra
        //  encima- y es el OPUESTO, asi que no se puede reaprovechar: seria la
        //  misma tapa diciendo dos cosas. Y tampoco es ese sin la barra, que
        //  dos dibujos separados por un trazo son el mismo dibujo — la leccion
        //  que costo redibujar REV.
        //
        //  Lo que SOLO significa es «de todos estos, solo ese»: un auricular
        //  puesto y el otro hueco. La diferencia es de RELLENO y no de trazo,
        //  que es lo que separa un espejo de una copia — igual que la bandeja
        //  de MANDAR contra la de RECIBIR.
        solo,
        //  --- Y EL DE LA PAGINA DE PROYECTOS --------------------------------
        //
        //  `carpeta` lo llevaban CUATRO cosas distintas: CAMBIAR (donde cae el
        //  rebote), CARGAR KIT, USAR ESTA CARPETA y la pestana PROYECTOS. Las
        //  tres primeras son literalmente lo mismo -«senala una carpeta»- y
        //  ademas nunca salen dos en la misma fila, asi que ahi el dibujo
        //  repetido es correcto: una funcion, un dueno.
        //
        //  La cuarta no: PROYECTOS es una PAGINA de AJUSTES, hermana de AUDIO,
        //  MIDI y ASPECTO, y lo que ensena es una LISTA de cosas guardadas con
        //  su nombre. Una carpeta ahi dice «ficheros» donde pone «proyectos».
        lista,
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
            case Id::lapiz: return "lapiz";
            case Id::tijeras: return "tijeras";        case Id::loop: return "loop";
            case Id::cuadrar: return "cuadrar";        case Id::pads: return "pads";
            case Id::sec: return "sec";                case Id::piano: return "piano";
            case Id::mezcla: return "mezcla";          case Id::cancion: return "cancion";
            case Id::xy: return "xy";                  case Id::ajustes: return "ajustes";
            case Id::rack: return "rack";              case Id::chop: return "chop";
            case Id::instrumentos: return "instrumentos"; case Id::manual: return "manual";
            case Id::sonido: return "sonido";          case Id::recorte: return "recorte";
            case Id::flt: return "flt";                case Id::hpf: return "hpf";
            case Id::drv: return "drv";                case Id::dly: return "dly";
            case Id::bit: return "bit";                case Id::rev: return "rev";
            case Id::eq:  return "eq";                 case Id::cmp: return "cmp";
            case Id::gte: return "gte";                case Id::dss: return "dss";
            case Id::lim: return "lim";
            case Id::cho: return "cho";                case Id::fla: return "fla";
            case Id::pha: return "pha";                case Id::trm: return "trm";
            case Id::rng: return "rng";                case Id::pit: return "pit";
            case Id::wid: return "wid";                case Id::exc: return "exc";
            case Id::trn: return "trn";                case Id::frz: return "frz";
            case Id::mic: return "mic";                case Id::remuestrear: return "remuestrear";
            case Id::bombeo: return "bombeo";          case Id::autocut: return "autocut";
            case Id::sistema: return "sistema";        case Id::cadena: return "cadena";
            case Id::patron: return "patron";          case Id::pad: return "pad";
            case Id::fijo: return "fijo";              case Id::midi: return "midi";
            case Id::medir: return "medir";            case Id::altavoz: return "altavoz";
            case Id::mano: return "mano";              case Id::momentaneo: return "momentaneo";
            case Id::niveles: return "niveles";
            case Id::insBajo: return "insBajo";        case Id::insSub: return "insSub";
            case Id::insEp: return "insEp";            case Id::insOrgano: return "insOrgano";
            case Id::insCuerdas: return "insCuerdas";  case Id::insColchon: return "insColchon";
            case Id::insPluck: return "insPluck";      case Id::insCampana: return "insCampana";
            case Id::insMetales: return "insMetales";  case Id::insLead: return "insLead";
            case Id::insCoro: return "insCoro";        case Id::insGuitarra: return "insGuitarra";
            case Id::insMazo: return "insMazo";        case Id::insClav: return "insClav";
            case Id::insFlauta: return "insFlauta";    case Id::insArpa: return "insArpa";
            case Id::aspecto: return "aspecto";        case Id::reves: return "reves";
            case Id::ruido: return "ruido";            case Id::sinsolo: return "sinsolo";
            case Id::comprimir: return "comprimir";    case Id::mandar: return "mandar";
            case Id::recibir: return "recibir";
            case Id::sel: return "sel";                case Id::clic: return "clic";
            case Id::automacion: return "automacion";
            case Id::solo: return "solo";
            case Id::lista: return "lista";
            case Id::ninguno:
            case Id::kNum:
            default: return "ninguno";
        }
    }

    //  UN ICONO SON DOS CAMINOS Y NO UNO. Lo que se rellena y lo que se traza
    //  no se pueden mezclar en un solo Path: rellenar el contorno de unas
    //  tijeras da una mancha con forma de tijeras.
    //  Y CUANTO DE SU CAJA LLENA. Ver `dibuja`: todos se escalan para ocupar
    //  el mismo blanco, y eso esta bien para los de trazo y mal para los
    //  MACIZOS - un cuadrado relleno que llena su caja pesa el triple que unas
    //  tijeras del mismo tamano, y en una fila se lee como si estuviera en
    //  negrita. Los pocos que son una mancha por definicion -el cuadrado de
    //  STOP, el circulo de REC- se dibujan mas pequenos para pesar igual. Es
    //  el mismo ajuste optico que hace una tipografia con el punto y la O.
    struct Trazo { juce::Path linea, relleno; float lleno = 1.0f; };

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
                t.lleno = 0.72f;
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

            case Id::lista:
                //  Tres renglones con su punto delante, que es lo que una lista
                //  de nombres guardados es. Horizontal a proposito: `mezcla`
                //  son lineas VERTICALES con bloques encima y `sec` una fila de
                //  barras, asi que el eje ya separa este de los dos con los que
                //  se podria confundir.
                for (int i = 0; i < 3; ++i)
                {
                    const float y = 6.5f + (float) i * 5.5f;
                    R.addEllipse (3.5f, y - 1.4f, 2.8f, 2.8f);
                    L.startNewSubPath (9.0f, y);
                    L.lineTo (20.5f - (float) i * 2.5f, y);
                }
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
            //  VACIAR SE QUEDA SIN SU CAJA, y por una medida.
            //
            //  Era una caja redondeada con una equis dentro y `pad` es una
            //  caja redondeada con un punto dentro: a 24 px salen a 0.3676 y a
            //  los TRECE a los que se dibujan, a 0.2378 — por debajo del
            //  liston. Y la caja no aportaba nada: una equis dentro de un
            //  recuadro se lee como «cerrar la ventana», que es justo lo que
            //  esta tapa NO hace. Sola y grande dice vaciar.
            case Id::vaciar:
                linea (L, 4.5f, 4.5f, 19.5f, 19.5f);
                linea (L, 19.5f, 4.5f, 4.5f, 19.5f);
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

            //  EL LAPIZ es el cuerpo LARGO Y ESTRECHO con punta, y la goma de
            //  al lado es un cuadrilatero ancho con una linea cruzada: si los
            //  dos fueran un romboide en diagonal serian el mismo dibujo, que
            //  es lo que `Tests/iconos.py` existe para no dejar pasar.
            case Id::lapiz:
                L.startNewSubPath (7.5f, 20.0f); L.lineTo (5.0f, 21.5f);
                L.lineTo (4.0f, 18.8f); L.closeSubPath();          // la punta
                L.startNewSubPath (7.5f, 20.0f); L.lineTo (17.5f, 5.0f);
                L.lineTo (20.6f, 7.1f); L.lineTo (10.6f, 22.1f); L.closeSubPath();
                linea (L, 15.6f, 7.9f, 18.7f, 10.0f);             // la virola
                break;

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

            //  SEC: DOS CARRILES DE CUATRO PASOS, con los que suenan puestos.
            //
            //  Era una fila de ocho barras alternadas y no se sabia que era:
            //  a 18 px de lado son barras de dos pixeles: ni rejilla ni pasos,
            //  una trama. Un secuenciador se reconoce por la REJILLA con
            //  huecos - dos pistas y cuatro tiempos es lo minimo que dice
            //  "esto es un patron" - y se separa de `pads`, que es una rejilla
            //  LLENA de cuatro por cuatro.
            case Id::sec:
            {
                const bool puesto[2][4] = { { true, false, true, false },
                                            { false, true, false, true } };
                for (int y = 0; y < 2; ++y)
                    for (int x = 0; x < 4; ++x)
                    {
                        const float cx = 1.5f + (float) x * 5.5f, cy = 6.0f + (float) y * 7.0f;
                        if (puesto[y][x]) R.addRoundedRectangle (cx, cy, 4.5f, 5.0f, 0.8f);
                        else              L.addRoundedRectangle (cx, cy, 4.5f, 5.0f, 0.8f);
                    }
                break;
            }

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
                //  SEIS dientes y GORDOS, no ocho finos. A 18 px de lado, ocho
                //  dientes de 2.8 px salen a menos de dos pixeles cada uno con
                //  hueco de uno: se funden con el anillo y la rueda deja de
                //  parecer una rueda - que fue exactamente la queja.
                juce::Path rueda;
                rueda.addEllipse (4.5f, 4.5f, 15.0f, 15.0f);
                for (int i = 0; i < 6; ++i)
                {
                    juce::Path diente;
                    diente.addRoundedRectangle (9.6f, 1.0f, 4.8f, 6.0f, 1.0f);
                    diente.applyTransform (juce::AffineTransform::rotation (
                        juce::MathConstants<float>::twoPi * (float) i / 6.0f, 12.0f, 12.0f));
                    rueda.addPath (diente);
                }
                //  HUECA DE VERDAD y no con el agujero dibujado encima: el
                //  disco relleno daba 0.27 contra el cuadrado de `stop`, que
                //  es lo que pasa cuando dos iconos son dos manchas del mismo
                //  tamano. Con el centro CORTADO -regla par/impar- deja de ser
                //  una mancha y pasa a ser un anillo dentado.
                juce::Path hueco;
                hueco.addEllipse (8.4f, 8.4f, 7.2f, 7.2f);
                rueda.addPath (hueco);
                rueda.setUsingNonZeroWinding (false);
                R = rueda;
                break;
            }

            //  RACK: tres unidades con su MANDO, y el mando grande. Con el punto
            //  a 1.8 px las tres cajas se leian como tres barras y la rejilla
            //  de `pads` quedaba a 0.33 - y las dos son pestanas de la cara,
            //  o sea que se ven una al lado de la otra.
            //  EL RACK PASABA RASPANDO, y un liston que se cumple por dos
            //  decimas no protege.
            //
            //  Eran tres cajas con filo, un punto relleno y una barra rellena
            //  cada una: 0.550 de tinta contra un tope de 0.55, o sea una
            //  mancha a los trece pixeles a los que se dibuja — y el quinto par
            //  mas cercano de los 5565, contra `pads`, que es la otra mancha
            //  compacta. Un armario son sus unidades y las separa una linea, no
            //  tres marcos: el filo de fuera y dos tabiques dicen lo mismo con
            //  la mitad de la tinta, y el punto de cada unidad se queda porque
            //  es lo que dice que ahi hay un aparato y no un cajon.
            case Id::rack:
                for (int i = 0; i < 3; ++i)
                {
                    const float y = 2.6f + (float) i * 7.0f;
                    R.addRoundedRectangle (2.5f, y, 19.0f, 3.4f, 1.0f);
                    L.addEllipse (16.6f, y + 0.5f, 2.4f, 2.4f);
                }
                t.lleno = 0.94f;
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

            //  --- LOS SEIS EFECTOS -------------------------------------------
            //
            //  Un efecto se dibuja por lo que le HACE al sonido, no por una
            //  inicial: la fila de la cara son seis tapas de tres letras -FLT,
            //  HPF, DRV...- y tres letras en cuatro idiomas no dicen nada a
            //  quien abre la app por primera vez.
            //
            //  FLT y HPF son espejo el uno del otro a proposito: son la misma
            //  curva por los dos lados y asi se leen como pareja, igual que
            //  acortar y alargar.
            case Id::flt:
                linea (L, 2.0f, 20.0f, 22.0f, 20.0f);
                L.startNewSubPath (2.0f, 6.0f);
                L.lineTo (11.0f, 6.0f);
                L.cubicTo (15.5f, 6.0f, 16.0f, 17.5f, 21.5f, 17.5f);
                break;

            case Id::hpf:
                linea (L, 2.0f, 20.0f, 22.0f, 20.0f);
                L.startNewSubPath (22.0f, 6.0f);
                L.lineTo (13.0f, 6.0f);
                L.cubicTo (8.5f, 6.0f, 8.0f, 17.5f, 2.5f, 17.5f);
                break;

            //  DRV: la onda RECORTADA contra sus dos topes, que es literalmente
            //  lo que hace un saturador.
            case Id::drv:
                linea (L, 2.0f, 6.5f, 22.0f, 6.5f);
                linea (L, 2.0f, 17.5f, 22.0f, 17.5f);
                L.startNewSubPath (2.0f, 12.0f);
                L.lineTo (4.5f, 6.5f);  L.lineTo (8.5f, 6.5f);
                L.lineTo (11.5f, 17.5f); L.lineTo (15.5f, 17.5f);
                L.lineTo (18.5f, 6.5f); L.lineTo (22.0f, 6.5f);
                break;

            //  DLY: el golpe y sus ecos. El primero LLENO y los otros huecos,
            //  que es lo que separa el original de lo que devuelve la linea.
            case Id::dly:
                linea (L, 1.5f, 21.0f, 22.5f, 21.0f);
                R.addRectangle (2.5f, 4.0f, 3.0f, 17.0f);
                L.addRectangle (8.0f, 8.5f, 3.0f, 12.5f);
                L.addRectangle (13.0f, 12.5f, 3.0f, 8.5f);
                L.addRectangle (18.0f, 16.0f, 3.0f, 5.0f);
                break;

            //  BIT: la escalera. Un reductor de bits convierte una rampa en
            //  peldanos, y eso es exactamente el dibujo.
            case Id::bit:
                L.startNewSubPath (2.0f, 20.0f);
                L.lineTo (7.0f, 20.0f); L.lineTo (7.0f, 15.5f);
                L.lineTo (12.0f, 15.5f); L.lineTo (12.0f, 11.0f);
                L.lineTo (17.0f, 11.0f); L.lineTo (17.0f, 6.5f);
                L.lineTo (22.0f, 6.5f);
                break;

            //  --- LOS CUATRO DE MODULACION -----------------------------
            //
            //  Cuatro efectos de la MISMA familia son justo donde es facil
            //  dibujar cuatro veces lo mismo, que es lo que ya paso con
            //  MANDAR/RECIBIR y con REV contra `deshacer`. Asi que cada uno
            //  dibuja lo que lo separa de sus tres hermanos y no «modulacion»:
            //  CHO la COPIA, FLA el PEINE, PHA las MUESCAS y TRM la
            //  ENVOLVENTE.

            //  CHO: la misma onda dos veces y desfasada. El efecto ES la
            //  copia, y por eso son dos trazos iguales y no uno ondulado -que
            //  seria `automacion` con otro nombre-.
            case Id::cho:
                for (int c = 0; c < 2; ++c)
                {
                    const float dy = 4.5f + (float) c * 8.0f;
                    const float dx = (float) c * 2.6f;
                    L.startNewSubPath (2.0f + dx, dy + 2.5f);
                    L.cubicTo (6.0f + dx, dy - 2.5f, 9.0f + dx, dy + 7.5f, 13.0f + dx, dy + 2.5f);
                    L.cubicTo (16.0f + dx, dy - 1.5f, 18.0f + dx, dy + 5.0f, 21.0f - dx, dy + 2.5f);
                }
                break;

            //  FLA: el peine. Muescas EQUIESPACIADAS sobre el renglon, que es
            //  lo que hace un retardo corto sumado al seco — y lo que lo
            //  separa de PHA, cuyas muescas ni son tantas ni estan repartidas.
            case Id::fla:
                linea (L, 2.0f, 6.0f, 22.0f, 6.0f);
                for (int i = 0; i < 5; ++i)
                {
                    const float x = 4.0f + (float) i * 4.2f;
                    L.startNewSubPath (x - 1.6f, 6.0f);
                    L.quadraticTo (x, 20.0f, x + 1.6f, 6.0f);
                }
                break;

            //  PHA: DOS muescas anchas y separadas sobre una linea plana. Un
            //  phaser no peina: pone unos pocos ceros y los pasea.
            case Id::pha:
                L.startNewSubPath (2.0f, 7.0f);
                L.lineTo (5.0f, 7.0f);
                L.quadraticTo (7.5f, 19.5f, 10.0f, 7.0f);
                L.lineTo (13.0f, 7.0f);
                L.quadraticTo (16.0f, 21.0f, 19.0f, 7.0f);
                L.lineTo (22.0f, 7.0f);
                break;

            //  TRM: la ENVOLVENTE que late. El relleno dice amplitud y no
            //  tono, que es lo unico que un temblor cambia; dibujarlo como una
            //  onda lo dejaria a un pelo de `cho`.
            case Id::trm:
                for (int i = 0; i < 3; ++i)
                {
                    const float x = 3.0f + (float) i * 6.6f;
                    R.startNewSubPath (x, 12.0f);
                    R.quadraticTo (x + 2.6f, 2.5f, x + 5.2f, 12.0f);
                    R.quadraticTo (x + 2.6f, 21.5f, x, 12.0f);
                    R.closeSubPath();
                }
                t.lleno = 0.88f;
                break;

            //  ==================================================================
            //  LOS SEIS DE CARACTER. Cada uno dibuja lo que lo separa de sus
            //  hermanos y no «un efecto»: seis dibujos de la misma tanda son
            //  justo donde es facil dibujar seis veces lo mismo, que es lo que
            //  ya paso con MANDAR/RECIBIR y con REV contra `deshacer`.

            //  RNG: la portadora suprimida y sus DOS bandas laterales, que es
            //  literalmente lo que sale de un modulador en anillo. El palo del
            //  medio es un muñon -el tono original se ha ido- y los dos de los
            //  lados estan a la misma distancia. Va sobre un renglon y hacia
            //  ARRIBA, que es lo que lo separa de `fla`, cuyas muescas cuelgan.
            case Id::rng:
                linea (L, 2.0f, 19.0f, 22.0f, 19.0f);
                linea (L, 6.0f, 19.0f, 6.0f,  5.0f);
                linea (L, 18.0f, 19.0f, 18.0f, 5.0f);
                linea (L, 12.0f, 19.0f, 12.0f, 15.0f);
                break;

            //  PIT: UNA onda que se aprieta. El periodo se acorta de izquierda
            //  a derecha, o sea que la nota sube: dibujar dos ondas sueltas lo
            //  dejaria a un pelo de `cho`, y una flecha hacia arriba a un pelo
            //  de media tabla.
            case Id::pit:
            {
                const float pasos[] = { 6.0f, 5.0f, 4.0f, 3.2f, 2.6f };
                float x = 2.0f;
                L.startNewSubPath (x, 12.0f);
                for (int i = 0; i < 5 && x < 22.0f; ++i)
                {
                    const float w = pasos[i];
                    L.quadraticTo (x + w * 0.25f, 12.0f - 7.5f, x + w * 0.5f, 12.0f);
                    L.quadraticTo (x + w * 0.75f, 12.0f + 7.5f, x + w, 12.0f);
                    x += w;
                }
                break;
            }

            //  WID: el campo que se abre. Dos lineas que divergen desde un
            //  punto de abajo y el eje del medio, que es donde queda el mono.
            //  No es una flecha de dos puntas: `alargar` ya es eso y los dos
            //  espejos de esa pareja son el par mas cercano de la tabla.
            case Id::wid:
                linea (L, 12.0f, 21.0f, 3.0f,  4.0f);
                linea (L, 12.0f, 21.0f, 21.0f, 4.0f);
                linea (L, 12.0f, 21.0f, 12.0f, 8.5f);
                linea (L, 3.0f,  4.0f, 21.0f,  4.0f);
                break;

            //  EXC: el destello que se anade encima de la banda. La estrella
            //  de cuatro puntas no la lleva nadie mas en la tabla, y va a la
            //  DERECHA porque los agudos estan a la derecha en todos los ejes
            //  de esta app.
            case Id::exc:
                linea (L, 2.0f, 18.0f, 22.0f, 18.0f);
                linea (L, 2.0f, 18.0f, 12.0f, 18.0f);
                R.startNewSubPath (17.0f,  2.5f);
                R.quadraticTo (18.0f,  8.0f, 22.5f,  9.0f);
                R.quadraticTo (18.0f, 10.0f, 17.0f, 15.5f);
                R.quadraticTo (16.0f, 10.0f, 11.5f,  9.0f);
                R.quadraticTo (16.0f,  8.0f, 17.0f,  2.5f);
                R.closeSubPath();
                linea (L, 6.0f, 18.0f, 6.0f, 13.0f);
                break;

            //  TRN: la envolvente con el golpe exagerado. El pico sube por
            //  encima de la caja de la caida, que es lo que un moldeador hace
            //  y lo que lo separa de `trm` -tres lentes iguales- y de `ruido`.
            case Id::trn:
                R.startNewSubPath (4.0f, 21.0f);
                R.lineTo (7.0f,  3.0f);
                R.lineTo (10.0f, 21.0f);
                R.closeSubPath();
                t.lleno = 0.88f;
                L.startNewSubPath (11.5f, 12.5f);
                L.quadraticTo (16.0f, 13.5f, 22.0f, 20.5f);
                linea (L, 11.5f, 20.5f, 22.0f, 20.5f);
                break;

            //  FRZ: el copo. Seis brazos con su barba, que es la unica forma
            //  radial de la tabla entera — y es ademas el simbolo del sector
            //  para congelar, asi que no hay que aprenderlo.
            case Id::frz:
                for (int i = 0; i < 3; ++i)
                {
                    const float ang = 0.5235988f + (float) i * 1.0471976f;   // 30 + k*60 grados
                    const float dx = std::cos (ang) * 9.5f, dy = std::sin (ang) * 9.5f;
                    linea (L, 12.0f - dx, 12.0f - dy, 12.0f + dx, 12.0f + dy);
                    for (int lado = -1; lado <= 1; lado += 2)
                    {
                        const float bx = 12.0f + dx * 0.62f * (float) lado;
                        const float by = 12.0f + dy * 0.62f * (float) lado;
                        const float px = -dy * 0.30f, py = dx * 0.30f;
                        linea (L, bx, by, bx + dx * 0.22f * (float) lado + px,
                                          by + dy * 0.22f * (float) lado + py);
                        linea (L, bx, by, bx + dx * 0.22f * (float) lado - px,
                                          by + dy * 0.22f * (float) lado - py);
                    }
                }
                break;

            //  REV: la fuente y lo que rebota. Tres arcos que se abren.
            //  Y los arcos se acotan por su ANGULO y no solo por su radio: la
            //  primera version abria de 0.35 a pi-0.35 con radio 15.7 y el arco
            //  de fuera salia por arriba y por abajo -de -3.8 a 27.8 en una
            //  rejilla de 24-. El banco lo canto a la primera.
            case Id::rev:
                R.addEllipse (1.5f, 9.5f, 5.0f, 5.0f);
                for (int i = 0; i < 3; ++i)
                    L.addCentredArc (4.0f, 12.0f, 6.0f + (float) i * 3.6f, 6.0f + (float) i * 3.6f,
                                     0.0f, 0.62f, juce::MathConstants<float>::pi - 0.62f, true);
                break;

            //  EQ: la curva CON SUS NODOS, que es lo que lo separa de FLT y de
            //  HPF -los tres son una linea sobre una base- y de `mezcla`, que
            //  son faderes verticales, o sea justo el dibujo que uno pondria
            //  primero para un ecualizador y el que ya esta usado. Sube y baja
            //  -un pico y un valle-, que ademas es lo unico que un filtro no
            //  puede hacer: un barrido solo quita.
            //
            //  Y los nodos van RELLENOS mientras la curva es trazo, que es la
            //  misma regla que separa el golpe de sus ecos en DLY.
            case Id::eq:
                linea (L, 2.0f, 20.5f, 22.0f, 20.5f);
                L.startNewSubPath (2.0f, 13.0f);
                L.cubicTo (5.0f,  13.0f,  5.5f,  5.0f,   8.5f,  5.0f);
                L.cubicTo (11.5f,  5.0f, 12.0f, 15.5f,  15.0f, 15.5f);
                L.cubicTo (18.0f, 15.5f, 19.0f,  8.5f,  22.0f,  8.5f);
                R.addEllipse (7.0f,  3.5f, 3.0f, 3.0f);
                R.addEllipse (13.5f, 14.0f, 3.0f, 3.0f);
                break;

            //  --- LA FAMILIA DE DINAMICA -----------------------------------
            //
            //  CMP ES LA CURVA DE TRANSFERENCIA CON SU RODILLA, y no otra
            //  flecha hacia dentro: `comprimir` ya existe -dos flechas que se
            //  acercan- y dos iconos que se diferencian en el grosor del trazo
            //  son el mismo dibujo. Aqui lo que se dibuja es lo que el efecto
            //  HACE con el nivel: entrada abajo, salida a la izquierda, una
            //  recta a 45 grados hasta el umbral y a partir de ahi una
            //  pendiente menor. La rodilla se ve, que es lo unico que separa
            //  un compresor de un fader.
            case Id::cmp:
                linea (L, 3.0f, 21.0f, 21.0f, 21.0f);
                linea (L, 3.0f, 21.0f,  3.0f,  3.0f);
                L.startNewSubPath (5.0f, 19.0f);
                L.lineTo (12.5f, 11.5f);
                L.cubicTo (14.5f, 9.5f, 15.5f, 9.0f, 21.0f, 7.5f);
                R.addEllipse (11.0f, 10.0f, 3.0f, 3.0f);
                break;

            //  GTE: una puerta que se abre. Dos jambas y la hoja abierta hacia
            //  dentro, con su tirador — no un cuadrado con una flecha, que es
            //  lo que ya dice `cargar`.
            case Id::gte:
                linea (L,  3.0f,  3.0f,  3.0f, 21.0f);
                linea (L, 21.0f,  3.0f, 21.0f, 21.0f);
                linea (L,  3.0f, 21.0f, 21.0f, 21.0f);
                L.startNewSubPath (9.0f, 21.0f);
                L.lineTo (9.0f, 6.5f);
                L.lineTo (17.0f, 3.5f);
                L.lineTo (17.0f, 18.0f);
                L.closeSubPath();
                R.addEllipse (10.0f, 12.0f, 2.0f, 2.0f);
                break;

            //  DSS: la ese. La banda alta que se corta se dice con la LETRA,
            //  que es lo unico que separa este dibujo de «un filtro» — y con
            //  la tachadura, que es lo que se le hace.
            case Id::dss:
                L.startNewSubPath (16.0f, 6.5f);
                L.cubicTo (16.0f, 3.0f, 8.0f, 3.0f, 8.0f, 7.5f);
                L.cubicTo (8.0f, 12.0f, 16.0f, 11.5f, 16.0f, 16.0f);
                L.cubicTo (16.0f, 20.5f, 8.0f, 20.5f, 8.0f, 17.0f);
                linea (L, 3.5f, 20.5f, 20.5f, 3.5f);
                break;

            //  LIM: el techo. Una linea gruesa arriba y lo que llega desde
            //  abajo aplastandose contra ella — que es literalmente lo que
            //  hace, y no una flecha hacia abajo, que es `guardar`.
            case Id::lim:
                linea (L, 2.5f, 5.5f, 21.5f, 5.5f);
                L.startNewSubPath (4.0f, 21.0f);
                L.lineTo (7.5f, 21.0f);
                L.lineTo (9.5f, 8.0f);
                L.lineTo (12.0f, 8.0f);
                L.lineTo (13.5f, 15.0f);
                L.lineTo (16.5f, 15.0f);
                L.lineTo (18.0f, 8.0f);
                L.lineTo (20.5f, 8.0f);
                break;

            //  --- LO QUE ENTRA Y LO QUE SE MIDE --------------------------------
            case Id::mic:
                L.addRoundedRectangle (8.5f, 2.5f, 7.0f, 12.0f, 3.5f);
                L.startNewSubPath (4.5f, 11.5f);
                L.cubicTo (4.5f, 19.0f, 19.5f, 19.0f, 19.5f, 11.5f);
                linea (L, 12.0f, 18.5f, 12.0f, 21.5f);
                break;

            //  REMUESTREAR: lo que sale vuelve a entrar. Una caja y la vuelta.
            case Id::remuestrear:
                L.addRoundedRectangle (7.0f, 7.0f, 10.0f, 10.0f, 1.5f);
                L.addCentredArc (12.0f, 12.0f, 9.0f, 9.0f, 0.0f, 0.5f, 5.4f, true);
                punta (R, 12.2f, 3.2f, -1.0f, 0.0f, 3.4f);
                break;

            //  BOMBEO: el nivel que se HUNDE cuando entra el bombo y vuelve.
            //  El triangulo de arriba es quien lo hunde.
            case Id::bombeo:
                punta (R, 6.0f, 8.0f, 0.0f, 1.0f, 4.4f);
                L.startNewSubPath (1.5f, 12.5f);
                L.lineTo (5.0f, 12.5f);
                L.lineTo (6.5f, 20.5f);
                L.cubicTo (10.0f, 20.5f, 11.5f, 12.5f, 15.0f, 12.5f);
                L.lineTo (22.5f, 12.5f);
                break;

            //  AUTOCUT: el golpe nuevo se come al anterior. El de delante esta
            //  ENTERO y el de detras cortado por la mitad, con la cuchilla en
            //  medio.
            case Id::autocut:
                R.addRectangle (2.0f, 10.0f, 7.5f, 10.0f);
                L.addRectangle (14.0f, 4.0f, 7.5f, 16.0f);
                linea (L, 11.6f, 2.0f, 11.6f, 22.0f);
                punta (R, 11.6f, 6.5f, 0.0f, -1.0f, 3.4f);
                break;

            case Id::sistema:
                L.addRoundedRectangle (6.0f, 1.5f, 12.0f, 21.0f, 2.2f);
                linea (L, 9.5f, 19.5f, 14.5f, 19.5f);
                linea (L, 6.0f, 5.5f, 18.0f, 5.5f);
                break;

            //  CADENA: dos eslabones enganchados. Es lo que un patron encadenado
            //  ES, y no se parece a nada mas del juego.
            case Id::cadena:
            {
                juce::Path a, b;
                a.addRoundedRectangle (0.0f, 6.5f, 13.0f, 8.0f, 4.0f);
                a.addRoundedRectangle (2.4f, 8.9f, 8.2f, 3.2f, 1.6f);
                a.setUsingNonZeroWinding (false);
                b = a;
                a.applyTransform (juce::AffineTransform::translation (2.0f, -1.0f));
                b.applyTransform (juce::AffineTransform::translation (9.0f, 4.0f));
                R.addPath (a);
                R.addPath (b);
                break;
            }

            //  PATRON: el patron ENTERO, o sea el marco alrededor de los pasos.
            //  Las herramientas de esta pagina actuan sobre todo el bloque, no
            //  sobre un paso: el marco es justamente eso.
            case Id::patron:
                L.addRoundedRectangle (1.5f, 4.5f, 21.0f, 15.0f, 1.8f);
                R.addRectangle (4.5f, 8.0f, 2.6f, 8.0f);
                R.addRectangle (10.7f, 8.0f, 2.6f, 8.0f);
                R.addRectangle (16.9f, 8.0f, 2.6f, 8.0f);
                break;

            //  PAD: uno solo, con el dedo encima.
            case Id::pad:
                L.addRoundedRectangle (2.5f, 2.5f, 19.0f, 19.0f, 2.5f);
                R.addEllipse (8.0f, 8.0f, 8.0f, 8.0f);
                break;

            //  FIJO Y MOMENTANEO SON EL MISMO CANDADO, CERRADO Y ABIERTO.
            //
            //  La tapa dice una cosa u otra y llevaba el mismo dibujo en las
            //  dos: un candado cerrado junto a la palabra MOMENTANEO se
            //  contradice con ella, que es peor que no tener icono. El arco
            //  del abierto sale por el mismo sitio y se va hacia arriba y a la
            //  derecha, o sea el gesto de soltar.
            case Id::fijo:
                R.addRoundedRectangle (3.5f, 10.5f, 17.0f, 11.5f, 1.8f);
                t.lleno = 0.88f;
                L.startNewSubPath (7.5f, 10.5f);
                L.lineTo (7.5f, 7.0f);
                L.cubicTo (7.5f, 1.5f, 16.5f, 1.5f, 16.5f, 7.0f);
                L.lineTo (16.5f, 10.5f);
                break;

            //  Y el abierto se abre DE VERDAD: el arco se levanta y se va a
            //  la derecha, que es el gesto de soltar. Con el arco en el mismo
            //  sitio y solo un lado suelto, los dos candados median 0.086 de
            //  distancia - o sea el mismo dibujo - y ademas no se veia cual
            //  estaba abierto a 18 px.
            case Id::momentaneo:
                R.addRoundedRectangle (2.0f, 11.5f, 15.0f, 10.5f, 1.8f);
                L.startNewSubPath (5.5f, 11.5f);
                L.lineTo (5.5f, 8.5f);
                L.cubicTo (5.5f, 1.5f, 21.5f, 2.5f, 21.0f, 9.0f);
                break;

            //  MIDI: la clavija de cinco patillas, que es como se reconoce sin
            //  leer nada.
            case Id::midi:
                L.addEllipse (2.0f, 2.0f, 20.0f, 20.0f);
                R.addRectangle (8.5f, 3.4f, 7.0f, 2.6f);
                for (int i = 0; i < 5; ++i)
                {
                    const float a = juce::MathConstants<float>::pi * (0.15f + 0.175f * (float) i);
                    R.addEllipse (12.0f - std::cos (a) * 6.6f - 1.5f,
                                  12.0f + std::sin (a) * 6.6f - 1.5f, 3.0f, 3.0f);
                }
                break;

            //  MEDIR: la regla, con sus marcas desiguales.
            case Id::medir:
                L.addRectangle (1.5f, 7.5f, 21.0f, 9.0f);
                linea (L, 6.0f, 7.5f, 6.0f, 13.5f);
                linea (L, 10.0f, 7.5f, 10.0f, 11.0f);
                linea (L, 14.0f, 7.5f, 14.0f, 13.5f);
                linea (L, 18.0f, 7.5f, 18.0f, 11.0f);
                break;

            case Id::altavoz:
                R.startNewSubPath (2.0f, 9.0f);
                R.lineTo (6.5f, 9.0f); R.lineTo (11.5f, 4.0f);
                R.lineTo (11.5f, 20.0f); R.lineTo (6.5f, 15.0f);
                R.lineTo (2.0f, 15.0f); R.closeSubPath();
                L.addCentredArc (12.5f, 12.0f, 4.2f, 4.2f, 0.0f, 0.6f, 2.55f, true);
                L.addCentredArc (12.5f, 12.0f, 8.2f, 8.2f, 0.0f, 0.6f, 2.55f, true);
                break;

            //  MANO: los gestos de la maquina. DE TRAZO y no maciza: rellena
            //  pesaba 0.55 de tinta contra 0.15 de sus vecinas y se leia como
            //  una mancha negra en la fila de pestanas.
            case Id::mano:
                L.addRoundedRectangle (5.5f, 11.0f, 13.0f, 10.5f, 3.0f);
                L.addRoundedRectangle (6.8f, 6.0f, 2.6f, 7.0f, 1.3f);
                L.addRoundedRectangle (10.4f, 3.5f, 2.6f, 9.5f, 1.3f);
                L.addRoundedRectangle (14.0f, 4.5f, 2.6f, 8.5f, 1.3f);
                L.addRoundedRectangle (2.4f, 12.5f, 3.6f, 6.5f, 1.8f);
                break;

            //  DIECISEIS NIVELES: la rampa. Cuatro escalones que suben de
            //  izquierda a derecha, que es como los dieciseis pads reparten la
            //  fuerza. No es `bit`, que baja y es una escalera de trazo, ni
            //  `dly`, que baja y tiene el primero relleno.
            case Id::niveles:
                for (int i = 0; i < 4; ++i)
                    R.addRoundedRectangle (2.0f + (float) i * 5.4f,
                                           19.5f - (float) (i + 1) * 4.2f,
                                           4.2f, (float) (i + 1) * 4.2f, 0.8f);
                break;

            // --- LOS SEIS QUE TAPABAN UN HUECO EN SU FILA ----------------

            //  ASPECTO: el circulo mitad lleno, que es como se dibuja "claro o
            //  oscuro" en cualquier aparato. La pagina son las cuatro carcasas
            //  y los cuatro idiomas, o sea COMO SE VE la maquina, y esa es la
            //  mitad que se puede dibujar. Un pincel diria "pintar", que es
            //  otra cosa, y una paleta de cuatro colores diria "color" en un
            //  chasis que es acromatico a proposito.
            //
            //  Y no es un anillo con algo dentro: `rec` ya es un anillo. Lo
            //  que separa a los dos es que aqui la mitad esta MACIZA, que es
            //  justo lo que la prueba de pares mide -que parte de la tinta es
            //  distinta- y no el contorno, que en los dos es el mismo circulo.
            case Id::aspecto:
                L.addEllipse (2.5f, 2.5f, 19.0f, 19.0f);
                R.addPieSegment (2.5f, 2.5f, 19.0f, 19.0f,
                                 juce::MathConstants<float>::pi,
                                 juce::MathConstants<float>::twoPi, 0.0f);
                break;

            //  REV: la muestra al reves. Una cuna que CRECE hacia la derecha
            //  -o sea el golpe al final en vez de al principio, que es
            //  exactamente lo que suena- con la flecha del tiempo apuntando
            //  hacia atras debajo.
            //
            //  Se probo antes la flecha curva de dar la vuelta y sale a 0.19
            //  de `deshacer`, que es su mismo arco: dos iconos que solo se
            //  diferencian en el lado de la punta son el mismo dibujo. La cuna
            //  no se parece a nada de la fila porque no es una flecha.
            case Id::reves:
                R.startNewSubPath (3.0f, 13.5f);
                R.lineTo (20.0f, 2.5f); R.lineTo (20.0f, 13.5f);
                R.closeSubPath();
                linea (L, 20.0f, 18.5f, 6.5f, 18.5f);
                punta (R, 3.0f, 18.5f, -1.0f, 0.0f, 3.6f);
                break;

            //  QUITAR RUIDO: el siseo a la izquierda y la senal limpia a la
            //  derecha, con el corte en medio. Es la prueba dibujada -medio
            //  segundo de siseo y medio de siseo con un tono encima- y ademas
            //  dice las DOS mitades que esa medida exige: baja el suelo Y deja
            //  el tono. Un tachon sobre una onda diria "quitar la onda".
            case Id::ruido:
            {
                const float z[9] = { 4.0f, 9.5f, 5.5f, 10.5f, 4.5f, 9.0f, 6.0f, 10.0f, 5.0f };
                for (int i = 0; i < 8; ++i)
                    linea (L, 2.0f + (float) i * 1.15f, z[i],
                           2.0f + (float) (i + 1) * 1.15f, z[i + 1]);
                linea (L, 11.8f, 2.5f, 11.8f, 21.5f);
                linea (L, 13.5f, 12.0f, 21.5f, 12.0f);
                R.addRectangle (15.5f, 6.5f, 1.6f, 11.0f);
                break;
            }

            //  SIN SOLO: los cascos tachados. SOLO es escuchar UNO, asi que
            //  quitarlo es dejar de escuchar uno solo -no es "silencio", que
            //  es lo que diria un altavoz tachado y ademas es el mute de al
            //  lado-. La diadema es un arco y las dos orejas dos manchas: no
            //  se parece a `altavoz`, que es un cono con dos ondas.
            case Id::solo:
                //  El mismo arco que `sinsolo` -es el mismo aparato- con un
                //  auricular RELLENO y el otro de contorno, y sin la barra.
                L.addCentredArc (12.0f, 12.5f, 8.0f, 8.0f, 0.0f,
                                 -juce::MathConstants<float>::halfPi * 1.55f,
                                 juce::MathConstants<float>::halfPi * 1.55f, true);
                R.addRoundedRectangle (2.6f, 12.0f, 4.2f, 7.5f, 1.6f);
                L.addRoundedRectangle (17.2f, 12.0f, 4.2f, 7.5f, 1.6f);
                break;

            case Id::sinsolo:
                L.addCentredArc (12.0f, 12.5f, 8.0f, 8.0f, 0.0f,
                                 -juce::MathConstants<float>::halfPi * 1.55f,
                                 juce::MathConstants<float>::halfPi * 1.55f, true);
                R.addRoundedRectangle (2.6f, 12.0f, 4.2f, 7.5f, 1.6f);
                R.addRoundedRectangle (17.2f, 12.0f, 4.2f, 7.5f, 1.6f);
                linea (L, 3.0f, 21.0f, 21.0f, 3.0f);
                break;

            //  WAV / OGG: lo que pesa el fichero. Dos flechas apretando una
            //  caja, que es lo que hace un codec y lo que esa tapa cambia -de
            //  1 152 104 bytes a 58 588, veinte veces-. Una nota o una onda
            //  diria "audio", que es lo que ya dicen las dos tapas de al lado.
            case Id::comprimir:
                L.addRectangle (8.5f, 5.0f, 7.0f, 14.0f);
                linea (L, 2.0f, 12.0f, 5.0f, 12.0f);
                punta (R, 7.0f, 12.0f, 1.0f, 0.0f, 3.2f);
                linea (L, 22.0f, 12.0f, 19.0f, 12.0f);
                punta (R, 17.0f, 12.0f, -1.0f, 0.0f, 3.2f);
                break;

            //  MANDAR y RECIBIR, que son un par de opuestos y por tanto el
            //  sitio donde es mas facil dibujar dos veces lo mismo: la misma
            //  flecha girada da 0.0 en la prueba de pares -es literalmente el
            //  mismo dibujo- y ni siquiera se lee, porque a 13 px de alto el
            //  ojo ve "flecha" y no "hacia donde".
            //
            //  Lo que los separa no es el sentido de la flecha sino la
            //  BANDEJA: la de MANDAR esta hueca -lo que habia ya se fue- y la
            //  de RECIBIR esta llena. La flecha ayuda; la bandeja decide.
            case Id::mandar:
                L.startNewSubPath (3.0f, 13.0f);
                L.lineTo (3.0f, 20.5f); L.lineTo (21.0f, 20.5f); L.lineTo (21.0f, 13.0f);
                linea (L, 12.0f, 16.5f, 12.0f, 6.5f);
                punta (R, 12.0f, 2.5f, 0.0f, -1.0f, 4.2f);
                break;

            case Id::recibir:
                R.startNewSubPath (3.0f, 13.0f);
                R.lineTo (3.0f, 20.5f); R.lineTo (21.0f, 20.5f); R.lineTo (21.0f, 13.0f);
                R.closeSubPath();
                linea (L, 12.0f, 2.5f, 12.0f, 8.5f);
                punta (R, 12.0f, 12.5f, 0.0f, 1.0f, 4.2f);
                break;

            //  SEL: las CUATRO ESQUINAS de un marco, o sea lo que un marquesina
            //  deja al soltar el dedo. No un rectangulo entero, que en esta
            //  tabla ya es media docena de cosas -`copiar`, `pegar`, `pad`- y
            //  se separaria de ellas por el grosor del trazo y por nada mas,
            //  que es la definicion de dos iconos que son el mismo dibujo. Lo
            //  que la hace inconfundible es el HUECO en mitad de cada lado.
            case Id::sel:
            {
                const float x0 = 3.5f, x1 = 20.5f, y0 = 4.5f, y1 = 19.5f, c = 4.5f;
                linea (L, x0, y0, x0 + c, y0);   linea (L, x1 - c, y0, x1, y0);
                linea (L, x0, y1, x0 + c, y1);   linea (L, x1 - c, y1, x1, y1);
                linea (L, x0, y0, x0, y0 + c);   linea (L, x0, y1 - c, x0, y1);
                linea (L, x1, y0, x1, y0 + c);   linea (L, x1, y1 - c, x1, y1);
                break;
            }

            //  CLIC: una CAMPANA con su badajo, y no un metronomo -que es lo
            //  primero que sale-, porque el metronomo YA esta dibujado en esta
            //  tabla con el nombre `tap`: un triangulo con su varilla y su
            //  contrapeso. Dos dibujos del mismo aparato para dos tapas que
            //  ademas viven a dos filas una de otra es exactamente lo que la
            //  prueba de pares existe para no dejar pasar.
            case Id::clic:
                L.startNewSubPath (5.5f, 17.0f);
                L.cubicTo (5.5f, 10.0f, 7.5f, 6.5f, 12.0f, 6.5f);
                L.cubicTo (16.5f, 6.5f, 18.5f, 10.0f, 18.5f, 17.0f);
                L.closeSubPath();
                linea (L, 4.0f, 17.0f, 20.0f, 17.0f);
                linea (L, 12.0f, 3.0f, 12.0f, 6.5f);
                R.addEllipse (10.5f, 18.0f, 3.0f, 3.0f);
                break;

            //  AUTOMACION: la rampa de un carril, con sus nodos CUADRADOS. Los
            //  redondos son de `eq` y de `cmp` -las dos curvas de esta tabla- y
            //  aqui la linea es RECTA a trozos, que es lo que de verdad separa
            //  una automatizacion de una respuesta: una se escribe punto a
            //  punto y la otra sale de una formula. Sin ejes, que los de `cmp`
            //  son suyos.
            case Id::automacion:
            {
                const float x[4] = { 3.0f, 9.0f, 15.0f, 21.0f };
                const float y[4] = { 18.0f, 8.0f, 13.0f, 5.0f };
                L.startNewSubPath (x[0], y[0]);
                for (int i = 1; i < 4; ++i) L.lineTo (x[i], y[i]);
                for (int i = 0; i < 4; ++i) R.addRectangle (x[i] - 1.7f, y[i] - 1.7f, 3.4f, 3.4f);
                break;
            }

            // --- LOS DIECISEIS INSTRUMENTOS ------------------------------

            //  BAJOS: la pala de un bajo con sus cuatro clavijas. Se dibuja el
            //  mastil y no la onda porque la onda de un bajo y la de un lead se
            //  parecen demasiado - ver el comentario del enum.
            case Id::insBajo:
                L.addRoundedRectangle (8.5f, 2.0f, 7.0f, 13.0f, 2.0f);
                linea (L, 12.0f, 15.0f, 12.0f, 22.0f);
                R.addEllipse (4.6f, 4.0f, 2.6f, 2.6f);
                R.addEllipse (4.6f, 9.0f, 2.6f, 2.6f);
                R.addEllipse (16.8f, 4.0f, 2.6f, 2.6f);
                R.addEllipse (16.8f, 9.0f, 2.6f, 2.6f);
                break;

            //  SUBS: tres barras que ENGORDAN hacia abajo. No es una onda: es
            //  "esto vive abajo", que es lo unico que un sub es.
            case Id::insSub:
                R.addRoundedRectangle (7.0f,  4.5f, 10.0f, 1.8f, 0.9f);
                R.addRoundedRectangle (5.0f, 10.0f, 14.0f, 3.0f, 1.2f);
                R.addRoundedRectangle (3.0f, 16.5f, 18.0f, 4.6f, 1.6f);
                t.lleno = 0.90f;
                break;

            //  PIANO ELEC: cuatro teclas blancas y tres negras. Es el dibujo
            //  mas literal de los dieciseis y a proposito: un teclado no se
            //  confunde con nada.
            //  EL PIANO ELECTRICO ES LA VARILLA, no un tercer teclado.
            //
            //  Era `piano` con una tecla mas: mismo marco, mismas rayas,
            //  mismas negras. A 24 px la prueba de pares los daba por
            //  distintos -0.3676- y a los TRECE a los que se dibujan de verdad
            //  se juntan en 0.1987, o sea el mismo dibujo. Lo que separa un
            //  Rhodes de un piano no es una tecla: es el diapason que el
            //  martillo golpea, y eso no se parece a nada de la tabla.
            case Id::insEp:
                linea (L, 8.0f, 4.0f, 8.0f, 13.0f);          // las dos puas
                linea (L, 16.0f, 4.0f, 16.0f, 13.0f);
                L.startNewSubPath (8.0f, 13.0f);             // y la horquilla
                L.quadraticTo (12.0f, 17.5f, 16.0f, 13.0f);
                linea (L, 12.0f, 16.4f, 12.0f, 21.0f);       // el mango
                R.addEllipse (2.0f, 6.0f, 4.4f, 4.4f);       // el martillo
                break;

            //  ORGANOS: las tres barras de registro, cada una a su altura. Un
            //  organo se toca moviendo eso, no apretando teclas.
            case Id::insOrgano:
                linea (L, 5.5f, 2.5f, 5.5f, 21.5f);
                linea (L, 12.0f, 2.5f, 12.0f, 21.5f);
                linea (L, 18.5f, 2.5f, 18.5f, 21.5f);
                R.addRoundedRectangle (3.3f,  8.0f, 4.4f, 2.6f, 1.0f);
                R.addRoundedRectangle (9.8f, 14.5f, 4.4f, 2.6f, 1.0f);
                R.addRoundedRectangle (16.3f, 5.0f, 4.4f, 2.6f, 1.0f);
                break;

            //  CUERDAS: el ARCO cruzando las cuerdas. Sin el arco esto son
            //  cuatro lineas verticales, o sea cualquier otra cosa.
            case Id::insCuerdas:
                linea (L, 5.0f,  4.5f, 5.0f, 19.5f);
                linea (L, 9.7f,  4.5f, 9.7f, 19.5f);
                linea (L, 14.4f, 4.5f, 14.4f, 19.5f);
                linea (L, 19.1f, 4.5f, 19.1f, 19.5f);
                linea (L, 2.0f, 18.5f, 22.0f, 6.5f);
                break;

            //  COLCHONES: el regulador que abre y cierra. Un colchon es una
            //  envolvente lenta por los dos lados y nada mas.
            case Id::insColchon:
                L.startNewSubPath (2.0f, 12.0f);
                L.cubicTo (9.0f, 11.6f, 13.0f, 3.5f, 22.0f, 3.0f);
                L.startNewSubPath (2.0f, 12.0f);
                L.cubicTo (9.0f, 12.4f, 13.0f, 20.5f, 22.0f, 21.0f);
                break;

            //  PLUCKS: el golpe y su caida. Sube de golpe y se apaga, que es
            //  la definicion de pulsado.
            case Id::insPluck:
                linea (L, 2.0f, 21.0f, 22.0f, 21.0f);
                L.startNewSubPath (4.0f, 21.0f);
                L.lineTo (4.0f, 3.5f);
                L.cubicTo (10.0f, 4.5f, 12.5f, 19.5f, 22.0f, 20.0f);
                break;

            //  CAMPANAS: una campana, con su badajo debajo.
            case Id::insCampana:
                L.startNewSubPath (5.0f, 17.5f);
                L.cubicTo (5.5f, 8.0f, 8.5f, 3.5f, 12.0f, 3.5f);
                L.cubicTo (15.5f, 3.5f, 18.5f, 8.0f, 19.0f, 17.5f);
                L.closeSubPath();
                R.addEllipse (10.6f, 19.0f, 2.8f, 2.8f);
                break;

            //  METALES: la campana de un metal, o sea el cono que se abre.
            case Id::insMetales:
                L.startNewSubPath (3.0f, 10.0f);
                L.lineTo (14.0f, 4.0f);
                L.lineTo (14.0f, 20.0f);
                L.lineTo (3.0f, 14.0f);
                L.closeSubPath();
                L.startNewSubPath (17.5f, 6.0f);
                L.cubicTo (20.5f, 9.0f, 20.5f, 15.0f, 17.5f, 18.0f);
                break;

            //  LEADS: una CUADRADA. Se gana la excepcion porque un lead ES eso:
            //  un pulso que corta por encima de la mezcla.
            case Id::insLead:
                L.startNewSubPath (2.0f, 18.5f);
                L.lineTo (2.0f, 5.5f);  L.lineTo (9.0f, 5.5f);
                L.lineTo (9.0f, 18.5f); L.lineTo (16.0f, 18.5f);
                L.lineTo (16.0f, 5.5f); L.lineTo (22.0f, 5.5f);
                break;

            //  COROS: una persona. Es lo unico que separa una voz de un
            //  sintetizador que suena a voz.
            case Id::insCoro:
                R.addEllipse (8.6f, 3.0f, 6.8f, 6.8f);
                L.startNewSubPath (3.5f, 21.5f);
                L.cubicTo (4.0f, 13.0f, 20.0f, 13.0f, 20.5f, 21.5f);
                break;

            //  CUERDA PULSADA: el cuerpo de una guitarra, con su boca.
            case Id::insGuitarra:
                L.startNewSubPath (12.0f, 2.5f);
                L.cubicTo (18.5f, 4.0f, 20.0f, 9.0f, 17.0f, 12.0f);
                L.cubicTo (20.5f, 15.5f, 18.0f, 21.5f, 12.0f, 21.5f);
                L.cubicTo (6.0f, 21.5f, 3.5f, 15.5f, 7.0f, 12.0f);
                L.cubicTo (4.0f, 9.0f, 5.5f, 4.0f, 12.0f, 2.5f);
                L.closeSubPath();
                R.addEllipse (9.8f, 13.8f, 4.4f, 4.4f);
                break;

            //  MAZOS: la baqueta encima de las laminas.
            case Id::insMazo:
                R.addRoundedRectangle (3.0f, 16.0f, 18.0f, 1.9f, 0.9f);
                R.addRoundedRectangle (4.5f, 19.0f, 15.0f, 1.9f, 0.9f);
                linea (L, 8.0f, 12.5f, 17.0f, 4.0f);
                R.addEllipse (4.4f, 9.6f, 5.4f, 5.4f);
                break;

            //  CLAVES: un peine de pulsos muy estrechos. La otra excepcion: un
            //  clavinet es casi un impulso con una resonancia encima.
            case Id::insClav:
                linea (L, 2.0f, 21.0f, 22.0f, 21.0f);
                linea (L, 3.5f,  21.0f, 3.5f,  3.5f);
                linea (L, 7.2f,  21.0f, 7.2f,  7.0f);
                linea (L, 10.9f, 21.0f, 10.9f, 10.0f);
                linea (L, 14.6f, 21.0f, 14.6f, 12.5f);
                linea (L, 18.3f, 21.0f, 18.3f, 15.0f);
                linea (L, 22.0f, 21.0f, 22.0f, 17.5f);
                break;

            //  VIENTOS: el tubo con sus agujeros.
            case Id::insFlauta:
                L.addRoundedRectangle (2.0f, 9.0f, 20.0f, 6.0f, 3.0f);
                R.addEllipse (5.6f, 10.9f, 2.2f, 2.2f);
                R.addEllipse (9.6f, 10.9f, 2.2f, 2.2f);
                R.addEllipse (13.6f, 10.9f, 2.2f, 2.2f);
                R.addEllipse (17.6f, 10.9f, 2.2f, 2.2f);
                break;

            //  ARPAS: la columna curva, la base y las cuerdas, que van de mas
            //  larga a mas corta - que es lo que hace que un arpa sea un arpa y
            //  no un triangulo con rayas.
            case Id::insArpa:
                L.startNewSubPath (3.5f, 21.0f);
                L.cubicTo (4.5f, 9.0f, 11.0f, 3.0f, 19.5f, 2.5f);
                linea (L, 19.5f, 2.5f, 19.5f, 21.0f);
                linea (L, 3.5f, 21.0f, 19.5f, 21.0f);
                linea (L, 7.0f, 16.0f, 7.0f, 21.0f);
                linea (L, 10.5f, 11.0f, 10.5f, 21.0f);
                linea (L, 14.0f, 7.0f, 14.0f, 21.0f);
                linea (L, 17.0f, 4.5f, 17.0f, 21.0f);
                break;

            case Id::ninguno:
            case Id::kNum:
            default:
                break;
        }

        return t;
    }

    //  DE QUE FAMILIA ES CADA DIBUJO. Una tabla y no un switch repartido: quien
    //  pinta un pad no tiene por que saber como se llama el icono.
    inline Id deFamilia (int familia) noexcept
    {
        static const Id t[16] =
        {
            Id::insBajo, Id::insSub, Id::insEp, Id::insOrgano,
            Id::insCuerdas, Id::insColchon, Id::insPluck, Id::insCampana,
            Id::insMetales, Id::insLead, Id::insCoro, Id::insGuitarra,
            Id::insMazo, Id::insClav, Id::insFlauta, Id::insArpa
        };
        return (familia >= 0 && familia < 16) ? t[familia] : Id::ninguno;
    }

    //  LA MARCA. Una tapa de pad con la Z cortada dentro, que es lo que esta
    //  maquina es: el nombre no flota al lado de un dibujo cualquiera, el
    //  dibujo ES un pad. Se corta con `setUsingNonZeroWinding(false)`, o sea
    //  que la Z es un HUECO y no un trazo encima - asi la marca funciona sobre
    //  cualquiera de las cuatro carcasas sin elegir un segundo color.
    //  LA Z DIBUJADA COMO UN ICONO, y no recortada como un hueco.
    //
    //  Es la otra forma de poner el nombre en una tapa: en la app, un pad
    //  cargado con un instrumento lleva el DIBUJO de su familia donde iria la
    //  onda (ver Iconos::deFamilia y PadButton), asi que una tapa con una Z
    //  dibujada ahi se lee como «el pad que trae la maquina». La diferencia con
    //  `marca()` no es de estilo: aquella QUITA la letra de la tapa y esta la
    //  PINTA encima, asi que esta si elige un color - el mismo con el que un
    //  pad pinta su onda.
    //
    //  Con la caja y el trazo de los otros ochenta y seis (`grosorPara`), o
    //  seria un dibujo de otra familia metido entre ellos.
    inline juce::Path marcaTrazo()
    {
        juce::Path z;
        z.startNewSubPath (5.5f, 5.5f);
        z.lineTo (18.5f, 5.5f);
        z.lineTo (5.5f, 18.5f);
        z.lineTo (18.5f, 18.5f);

        juce::Path t;
        //  El mismo grosor que los otros ochenta y seis a esta caja: ver
        //  `grosorPara`, que esta declarada mas abajo y por eso se escribe la
        //  cuenta aqui - 24 * 0.086, o sea 2.06, y por dos porque la Z de la
        //  marca es de trazo GORDO, como el nombre en la cabecera.
        juce::PathStrokeType (24.0f * 0.086f * 2.0f,
                              juce::PathStrokeType::curved,
                              juce::PathStrokeType::rounded).createStrokedPath (t, z);
        return t;
    }

    //  LA Z SOLA, sin la tapa alrededor. La usan `marca()` -que la RESTA de la
    //  tapa- y las marcas que la PINTAN encima, asi que la letra se escribe una
    //  vez: dos copias de una letra se separan en cuanto alguien toca una.
    inline juce::Path marcaHueco()
    {
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

        return z;
    }

    inline juce::Path marca()
    {
        juce::Path p;
        p.addRoundedRectangle (0.0f, 0.0f, 24.0f, 24.0f, 3.5f);
        p.addPath (marcaHueco());
        p.setUsingNonZeroWinding (false);
        return p;
    }

    //  EL GROSOR SALE DEL LADO. Un trazo de 2 px es una linea en una caja de 24
    //  y un pelo en una de 44, y esta app dibuja las dos: la fila de modulos
    //  mide 26 px en dos de las siete pantallas y 44 en las otras cinco.
    //  EL MISMO GROSOR PARA TODOS, y mas fino que el primero: a 0.086 los
    //  iconos de trazo se leian mas pesados que los de relleno y el juego no
    //  parecia de la misma mano. Es una proporcion del lado y no una constante
    //  porque una tapa mide 26 px en dos pantallas y 44 en las otras cinco.
    inline float grosorPara (float lado) noexcept
    {
        return juce::jmax (1.05f, lado * 0.072f);
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

        //  TODOS OCUPAN LA MISMA CAJA.
        //
        //  Cada dibujo se escribia dentro de la rejilla de 24 con el sitio que
        //  le pedia su forma, asi que unos llenaban el 100% y otros el 79%: en
        //  una fila se leen como si el pequeno estuviera mas lejos. Se mide lo
        //  que el trazo ocupa DE VERDAD -con su grosor- y se escala para que
        //  llene el mismo blanco, centrado. Uniforme, o sea sin deformar: un
        //  icono ancho y bajo sigue siendo ancho y bajo, solo que llenando.
        const float m = grosorPara (24.0f) * 0.5f;
        auto lim = t.relleno.isEmpty() ? juce::Rectangle<float>() : t.relleno.getBounds();
        if (! t.linea.isEmpty())
        {
            const auto lb = t.linea.getBounds().expanded (m);
            lim = lim.isEmpty() ? lb : lim.getUnion (lb);
        }

        float esc = lado / 24.0f;
        float dx = caja.getX(), dy = caja.getY();
        if (! lim.isEmpty())
        {
            //  El 96% y no el 100%: el trazo se ensancha DESPUES de escalar, y
            //  un dibujo que llena la caja al ras se come su propio pelo por
            //  los bordes.
            const float objetivo = lado * 0.96f * juce::jlimit (0.5f, 1.0f, t.lleno);
            esc = juce::jmin (objetivo / lim.getWidth(), objetivo / lim.getHeight());
            dx = caja.getCentreX() - lim.getCentreX() * esc;
            dy = caja.getCentreY() - lim.getCentreY() * esc;
        }
        const auto af = juce::AffineTransform::scale (esc).translated (dx, dy);

        g.setColour (c);
        if (! t.relleno.isEmpty())
        {
            auto r = t.relleno; r.applyTransform (af);
            g.fillPath (r);
        }
        if (! t.linea.isEmpty())
        {
            auto l = t.linea; l.applyTransform (af);
            g.strokePath (l, juce::PathStrokeType (grosorPara (lado * 0.96f),
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
    //  LOS LIMITES DEL DIBUJO EN SU REJILLA DE 24. Desde que `dibuja` escala
    //  cada icono para que llene la misma caja, salirse aqui ya no pinta fuera
    //  -se encoge- pero sigue siendo la forma de ver que un dibujo se escribio
    //  torcido, y de que el banco pueda decir cuanto se esta reescalando.
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

    //  LA MISMA Z, PERO EN CELDAS.
    //
    //  El icono del lanzador es la rejilla 4x4 con la Z dibujada por los pads
    //  ENCENDIDOS (ver StoreArt::appIcon): la rejilla dice la maquina y lo que
    //  se enciende dice el nombre, que es ademas lo que la app hace. Y la
    //  cabecera se queda con la marca de arriba, porque a 24 px una rejilla de
    //  cuatro por cuatro son celdas de cinco pixeles, o sea una mancha: la
    //  MISMA Z recortada en un pad de cerca y dibujada por pads de lejos.
    //
    //  Los diez viven AQUI y no dentro del icono porque si no serian dos
    //  dibujos con la misma idea que se separan en cuanto alguien toque uno, y
    //  eso es la regla de esta casa: una regla escrita dos veces son dos
    //  reglas. El banco compara estas celdas con lo que sale en el PNG.
    //
    //  Fila de arriba entera, la diagonal bajando hacia la izquierda -igual que
    //  el trazo de marca(), que va de (18,9.2) a (11.4,14.8)- y la fila de
    //  abajo entera. Diez de dieciseis.
    static constexpr int kLadoMarca = 4;
    inline bool marcaCelda (int fila, int col) noexcept
    {
        if (fila == 0 || fila == 3) return true;      // las dos barras
        if (fila == 1) return col == 2;               // la diagonal, arriba
        if (fila == 2) return col == 1;               // y abajo
        return false;
    }
}
