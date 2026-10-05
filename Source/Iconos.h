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
        seleccion, lupa,
        pads, sec, piano, mezcla, cancion, xy, ajustes, rack, chop, instrumentos, manual,
        sonido, recorte, recortar,
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
        //  Y LOS OCHO DE LAS FAMILIAS NUEVAS. Mismo criterio: por lo que el
        //  instrumento ES. Los dos que no son un instrumento -FM y SYNC- se
        //  dibujan por su ALGORITMO, que es lo que son: una pila de operadores
        //  y una onda que se reinicia.
        insFm, insSync, insPiano, insAcordeon, insSitar, insCello, insCana, insTubo,
        midi, medir, altavoz, mano, momentaneo, niveles,
        cho, fla, pha, trm,
        rng, pit, wid, exc, trn, frz,
        wah, oct, amb, frm, fld, rot, png, duc, rep,
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
        //  --- Y EL DEL MUTE DEL RACK ----------------------------------------
        //
        //  Se pidio «al lado del boton del plugin, una opcion para sustituirlo
        //  o MUTEARLO»: sustituir ya se hacia desde el canalon y apagar seguia
        //  siendo exclusivo de la fila de la cara y del XY -el rack PINTABA el
        //  estado y no dejaba tocarlo-.
        //
        //  Y no vale `sinsolo` ni `altavoz` tachado: lo que esta tapa hace no
        //  es silenciar una fuente, es SACAR EL EFECTO DE EN MEDIO. El simbolo
        //  de eso lleva cien anos dibujado -el anillo abierto por arriba con
        //  la barra dentro- y no se parece a nada de esta tabla: `rec` es un
        //  circulo relleno con anillo y aqui el anillo esta PARTIDO, que es lo
        //  unico que lo hace legible a trece pixeles.
        apagar,
        //  --- Y LAS DOS QUE DICEN QUE LE HACE UNA FILA A LA SEÑAL -----------
        //
        //  «En el rack el envio al ecualizador, y algunos efectos, el visual
        //  molaria que fuese diferente». Dieciseis de los veintiun tipos
        //  SUSTITUYEN -`if (fxSustituye[f]) dry *= (1.0f - g)`, o sea que subir
        //  ese fader le QUITA seco al canal- y cinco SUMAN encima. Desde el
        //  dedo son dos cosas distintas: con DRV al 50 % oyes mitad sucio y
        //  mitad limpio, con DLY al 50 % oyes el canal ENTERO mas un eco.
        //
        //  Se intento dos veces y las dos se deshicieron MIRANDO LA FOTO: la
        //  pista del fader pintada entera -con la de serie de JUCE al lado, un
        //  inserto y un envio salian identicos- y el visor en la fila -flotaba
        //  sobre el fader y desordenaba la fila-. Lo que quedaba era el nombre
        //  accesible, o sea que un ojo humano no tenia NADA.
        //
        //  Una MARCA y no una palabra: tiene que leerse en los cuatro idiomas,
        //  y tres letras sin traducir solo se sostienen en los nombres de
        //  efecto. Y no cuesta un pixel porque no es una tapa nueva: ocupa el
        //  sitio del dibujo del tipo en el canalon, donde el NOMBRE -«FLT»- ya
        //  dice cual es el efecto. El icono es el adorno y la palabra la
        //  funcion; aqui la palabra ya hace su trabajo, asi que el dibujo puede
        //  decir lo otro.
        //
        //  La diferencia es de CAMINO: en un inserto la señal ATRAVIESA la
        //  caja, en un envio SIGUE de largo y una rama baja a ella.
        inserto, envio,
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
            case Id::seleccion: return "seleccion";    case Id::lupa: return "lupa";
            case Id::cuadrar: return "cuadrar";        case Id::pads: return "pads";
            case Id::sec: return "sec";                case Id::piano: return "piano";
            case Id::mezcla: return "mezcla";          case Id::cancion: return "cancion";
            case Id::xy: return "xy";                  case Id::ajustes: return "ajustes";
            case Id::rack: return "rack";              case Id::chop: return "chop";
            case Id::instrumentos: return "instrumentos"; case Id::manual: return "manual";
            case Id::sonido: return "sonido";          case Id::recorte: return "recorte";
            case Id::recortar: return "recortar";
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
            case Id::wah: return "wah";                case Id::oct: return "oct";
            case Id::amb: return "amb";                case Id::frm: return "frm";
            case Id::fld: return "fld";                case Id::rot: return "rot";
            case Id::png: return "png";                case Id::duc: return "duc";
            case Id::rep: return "rep";
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
            case Id::insFm: return "insFm";            case Id::insSync: return "insSync";
            case Id::insPiano: return "insPiano";      case Id::insAcordeon: return "insAcordeon";
            case Id::insSitar: return "insSitar";      case Id::insCello: return "insCello";
            case Id::insCana: return "insCana";        case Id::insTubo: return "insTubo";
            case Id::aspecto: return "aspecto";        case Id::reves: return "reves";
            case Id::ruido: return "ruido";            case Id::sinsolo: return "sinsolo";
            case Id::comprimir: return "comprimir";    case Id::mandar: return "mandar";
            case Id::recibir: return "recibir";
            case Id::sel: return "sel";                case Id::clic: return "clic";
            case Id::automacion: return "automacion";
            case Id::solo: return "solo";
            case Id::lista: return "lista";
            case Id::apagar: return "apagar";
            case Id::inserto: return "inserto";        case Id::envio: return "envio";
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

    //  EL GROSOR DE LINEA, UNO PARA TODOS.
    //
    //  Lo que se pinta de TRAZO ya tenia uno solo por construccion: `dibuja`
    //  estira el camino y despues lo engorda con `grosorPara`, asi que dos
    //  iconos de linea en la misma fila salen con el mismo pelo quiera o no
    //  quiera quien los dibujo. Pero una banda escrita a mano en el RELLENO
    //  -el rail de `midi`, las cuerdas de `insCello`, los tubos de `insTubo`,
    //  las tres unidades de `rack`- lleva el ancho que su autor tecleo, y
    //  tecleados habia VEINTICINCO anchos distintos entre 0.7 y 5.0 repartidos
    //  por treinta y tres iconos. Medido sobre el raster: la banda mas comun
    //  era de 2 px en veintiocho iconos, de 3 en ochenta, de 4 en veintitres y
    //  de 5 o 6 en nueve. Eso es lo que se ve en una fila de la cara: unos
    //  iconos de pelo fino y otros de rotulador, y el ojo lo lee como si los
    //  gordos estuvieran en negrita.
    //
    //  `kBanda` es ese ancho, el MISMO que `grosorPara` le da a la rejilla de
    //  24 -que es donde se dibuja todo-, y de aqui sale toda banda que sea una
    //  LINEA. Lo que es una FORMA no sale de aqui y no tiene por que: el
    //  cuadrado de `stop`, las secciones de `cancion`, las cunas de `trm` o las
    //  teclas negras de `piano` no son lineas y un grosor de linea no les dice
    //  nada. La cuenta se escribe aqui a mano porque `grosorPara` esta
    //  declarada mil lineas mas abajo; es el mismo apano que ya hacia
    //  `marcaTrazo`, y lo vigila `Tests/iconos.py`.
    inline constexpr float kBanda = 24.0f * 0.072f;      //  1.728

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

        //  UNA LINEA EN EL CAMINO QUE SE RELLENA, del grosor de la casa.
        //
        //  Hay bandas que tienen que ser macizas y no de contorno -una cuerda,
        //  un rail, un tubo, el aro de una campana- y esas no las puede dar
        //  `linea`, que va al camino que se traza. Lo que no puede pasar es que
        //  cada una se escriba con su ancho: `bandaH` y `bandaV` las dan con
        //  `kBanda` y con las puntas redondas, que es como las deja
        //  `PathStrokeType::rounded`, asi que una banda rellena y una linea
        //  trazada salen indistinguibles. Se centran en su EJE -`yc` o `xc`- y
        //  no en una esquina, que es como se piensa una linea.
        inline void bandaH (juce::Path& p, float x, float yc, float largo)
        {
            p.addRoundedRectangle (x, yc - kBanda * 0.5f, largo, kBanda, kBanda * 0.5f);
        }

        inline void bandaV (juce::Path& p, float xc, float y, float largo)
        {
            p.addRoundedRectangle (xc - kBanda * 0.5f, y, kBanda, largo, kBanda * 0.5f);
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

            //  Y DAN TRES CUARTOS DE VUELTA, no un arco por arriba.
            //
            //  Eran un arco bajo el borde de arriba con la punta debajo: la
            //  caja salia de 24 x 14, o sea 0.58 del marco por el lado corto.
            //  Un circulo casi cerrado es como se dibuja esto en cualquier
            //  aparato, toca los cuatro lados del marco, y el HUECO —por donde
            //  entra la punta— es lo que separa el uno del otro: DESHACER lo
            //  tiene arriba a la izquierda y REHACER arriba a la derecha, que
            //  es la mano de siempre.
            case Id::deshacer:
                L.addCentredArc (12.0f, 12.2f, 8.2f, 8.2f, 0.0f,
                                 0.0f, juce::MathConstants<float>::pi * 1.5f, true);
                punta (R, 3.8f, 11.0f, 0.0f, -1.0f, 4.4f);
                break;

            case Id::rehacer:
                L.addCentredArc (12.0f, 12.2f, 8.2f, 8.2f, 0.0f,
                                 juce::MathConstants<float>::pi * 0.5f,
                                 juce::MathConstants<float>::pi * 2.0f, true);
                punta (R, 20.2f, 11.0f, 0.0f, -1.0f, 4.4f);
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

            //  LA HOJA MAS ANCHA: con 13 px de ancho por 19 de alto la caja
            //  se quedaba en 0.67 del marco por el lado corto.
            case Id::nuevo:
                L.startNewSubPath (3.6f, 2.5f); L.lineTo (14.0f, 2.5f);
                L.lineTo (20.4f, 8.9f); L.lineTo (20.4f, 21.5f);
                L.lineTo (3.6f, 21.5f); L.closeSubPath();
                L.startNewSubPath (14.0f, 2.5f); L.lineTo (14.0f, 8.9f);
                L.lineTo (20.4f, 8.9f);
                linea (L, 12.0f, 12.0f, 12.0f, 19.0f);
                linea (L, 8.5f, 15.5f, 15.5f, 15.5f);
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

            //  ACORTAR y ALARGAR: LOS DOS TOPES DEL COMPAS, de arriba abajo.
            //
            //  Eran una barra corta y dos puntas, y la barra corta es lo que
            //  les dejaba la caja en 24 x 10 —0.42 del marco por el lado corto,
            //  el peor de los dos—. La barra LARGA ya se probo y se descarto
            //  por una razon que sigue valiendo: con la barra de arriba abajo y
            //  las puntas hacia fuera, ALARGAR era una CRUZ, y una cruz no dice
            //  "alargar", dice "mas".
            //
            //  Asi que lo que crece no es la barra del medio: son los TOPES.
            //  Dos paredes verticales —los dos extremos del compas— y las
            //  puntas entre ellas, que es como se acota una medida en un plano.
            //  Llena el marco por los dos lados y lo que los separa sigue
            //  siendo lo de antes: en ACORTAR las dos puntas se juntan en el
            //  medio y en ALARGAR empujan las paredes hacia fuera.
            case Id::acortar:
                linea (L,  4.6f,  3.4f,  4.6f, 20.6f);
                linea (L, 19.4f,  3.4f, 19.4f, 20.6f);
                linea (L,  4.6f, 12.0f,  8.2f, 12.0f);
                linea (L, 15.8f, 12.0f, 19.4f, 12.0f);
                punta (R, 10.0f, 12.0f, 1.0f, 0.0f, 3.8f);
                punta (R, 14.0f, 12.0f, -1.0f, 0.0f, 3.8f);
                break;

            case Id::alargar:
                linea (L,  5.6f,  3.4f,  5.6f, 20.6f);
                linea (L, 18.4f,  3.4f, 18.4f, 20.6f);
                linea (L,  8.0f, 12.0f, 16.0f, 12.0f);
                punta (R,  2.0f, 12.0f, -1.0f, 0.0f, 4.4f);
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
                //  Y EL GOLPE MAS ALTO LLEGA AL MARCO: con 14.5 de alto en 22
                //  de ancho la caja se quedaba a 0.67 por el lado corto.
                const float x[4] = { 3.6f, 9.4f, 13.2f, 19.6f };
                const float h[4] = { 11.0f, 18.2f, 8.0f, 15.0f };
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

            //  SELECCION: el marco de puntos de toda la vida. Se dibuja con
            //  ocho trazos cortos y no con un trazo discontinuo porque el
            //  camino de esta casa es un Path relleno a 24x24 y un patron de
            //  guiones no sobrevive al escalado de la tapa.
            case Id::seleccion:
                linea (L, 3.5f,  3.5f,  8.0f,  3.5f);
                linea (L, 12.0f, 3.5f, 16.5f,  3.5f);
                linea (L, 20.5f, 3.5f, 20.5f,  8.0f);
                linea (L, 20.5f, 12.0f, 20.5f, 16.5f);
                linea (L, 20.5f, 20.5f, 16.0f, 20.5f);
                linea (L, 12.0f, 20.5f, 7.5f,  20.5f);
                linea (L, 3.5f,  20.5f, 3.5f,  16.0f);
                linea (L, 3.5f,  12.0f, 3.5f,   7.5f);
                break;

            //  LUPA: el circulo y el mango, mirando abajo a la derecha como
            //  todas las de la casa.
            case Id::lupa:
                L.addEllipse (3.5f, 3.5f, 12.0f, 12.0f);
                linea (L, 14.5f, 14.5f, 20.5f, 20.5f);
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
                        //  LOS BLOQUES LLEGAN ARRIBA Y ABAJO: con filas de 5 px
                        //  de alto la caja salia de 24 x 14, o sea 0.58 del
                        //  marco por el lado corto.
                        const float cx = 2.0f + (float) x * 5.1f, cy = 3.8f + (float) y * 9.2f;
                        if (puesto[y][x]) R.addRoundedRectangle (cx, cy, 4.3f, 7.0f, 1.0f);
                        else              L.addRoundedRectangle (cx, cy, 4.3f, 7.0f, 1.0f);
                    }
                break;
            }

            //  Y EL TECLADO LLEGA ARRIBA Y ABAJO: 19 de ancho por 13 de alto
            //  dejaban el marco a 0.67 por el lado corto.
            //
            //  Y CON CINCO BLANCAS Y TRES NEGRAS CON SU HUECO. Alto y con dos
            //  rayas de arriba abajo era un recuadro con dos barras, o sea
            //  `pads` desenfocado: medido, 0.3654 contra la rejilla de cuatro
            //  por cuatro, el par mas cercano que no estaba antes. Lo que hace
            //  que un teclado se LEA como un teclado no es el recuadro: es que
            //  las negras van de dos en dos y de tres en tres —aqui dos, hueco,
            //  una— y que las rayas de las blancas solo existen ABAJO, donde
            //  las negras se acaban. Ninguna rejilla tiene eso.
            case Id::piano:
                L.addRectangle (2.6f, 3.2f, 18.8f, 17.6f);
                for (int i = 1; i <= 4; ++i)
                {
                    const float x = 2.6f + (float) i * 3.76f;
                    linea (L, x, 13.6f, x, 20.8f);
                }
                //  Y LAS NEGRAS SE QUEDAN EN 2.7, QUE NO SON LINEAS. Se
                //  probaron a `kBanda` -1.73- con el argumento de que en un
                //  teclado de verdad una negra mide poco mas de la mitad de una
                //  blanca, y salio MEDIDO que no: la pareja `pads`/`piano` cayo
                //  de 0.4028 a 0.3595 a los diecisiete pixeles a los que se
                //  comparan, porque un marco con tabiques finos desenfocado es
                //  la rejilla de `pads`. Una tecla negra es una TECLA, no una
                //  linea, y por eso no sale de `kBanda`: lo que da peso a este
                //  icono y lo separa de una rejilla es justamente que las
                //  negras sean macizas. Es el mismo reparto que deja fuera el
                //  cuadrado de `stop` o las cunas de `trm`.
                R.addRectangle ( 5.01f, 3.2f, 2.7f, 10.4f);
                R.addRectangle ( 8.77f, 3.2f, 2.7f, 10.4f);
                R.addRectangle (16.29f, 3.2f, 2.7f, 10.4f);
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
            //  Y LAS TRES UNIDADES SON LINEAS, DE `kBanda`. Eran bandas de
            //  3.4 px en una rejilla de 24 -el doble de lo que mide una linea
            //  de esta cara, y el icono entero es banda: medido, la banda mas
            //  comun de `rack` era de 4 px en el 81% de su tinta, el peor de
            //  los 140-. Un armario son tres estantes y un estante es una
            //  linea. Se va con el 0.94 de macizo, que era el descuento optico
            //  que se le hacia por pesar de mas: sin bandas gordas no hay nada
            //  que descontar.
            case Id::rack:
                for (int i = 0; i < 3; ++i)
                {
                    const float y = 2.6f + (float) i * 7.0f;
                    bandaH (R, 2.5f, y + 1.7f, 19.0f);
                    L.addEllipse (16.6f, y + 0.5f, 2.4f, 2.4f);
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
            //  Y OCHO BARRAS Y NO ONCE, para que quepan al grosor de la casa.
            //  Iban a 1.5 px de ancho, por debajo de `kBanda`, porque con once
            //  a 1.9 de paso no cabia nada mas gordo: a 1.73 se tocarian y la
            //  onda saldria un bloque. Ocho a 2.6 de paso dejan el mismo ancho
            //  de dibujo -de 2 a 22- con el pelo de la casa y el hueco entre
            //  barras medio pixel mas, que a los trece a los que esto se
            //  dibuja es lo que decide si se leen barras o una mancha.
            case Id::sonido:
            {
                const float h[8] = { 2.0f, 6.5f, 3.0f, 9.0f,
                                     4.5f, 7.5f, 2.5f, 5.0f };
                for (int i = 0; i < 8; ++i)
                    bandaV (R, 2.9f + (float) i * 2.6f, 12.0f - h[i], h[i] * 2.0f);
                break;
            }

            case Id::recorte:
                L.startNewSubPath (8.0f, 2.5f); L.lineTo (4.0f, 2.5f);
                L.lineTo (4.0f, 21.5f); L.lineTo (8.0f, 21.5f);
                L.startNewSubPath (16.0f, 2.5f); L.lineTo (20.0f, 2.5f);
                L.lineTo (20.0f, 21.5f); L.lineTo (16.0f, 21.5f);
                bandaV (R, 12.0f, 7.0f, 10.0f);
                break;

            //  RECORTAR no puede ser el dibujo de la pestana RECORTE: las dos
            //  se ven a la vez en la misma ficha -la pestana arriba y la tapa
            //  en la fila de la muestra- y dos veces el mismo trazo se lee como
            //  que una de las dos esta mal puesta.
            //
            //  Y lo que dice es otra cosa. La pestana dice DONDE estan las
            //  asas; la tapa dice que LO DE FUERA SE VA. Asi que el trozo del
            //  medio se queda a su altura y los dos de los lados estan CAIDOS
            //  y separados: se leen como dos pedazos que se desprenden.
            //  Tampoco es `tijeras`, que CORTA por un punto y deja las dos
            //  mitades.
            case Id::recortar:
                R.addRectangle ( 8.5f,  4.0f, 7.0f, 16.0f);   // lo que queda
                R.addRectangle ( 2.0f, 15.5f, 4.0f,  5.0f);   // lo que cae, izquierda
                R.addRectangle (18.0f, 15.5f, 4.0f,  5.0f);   // y derecha
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
            //
            //  Con RESONANCIA y RELLENO. La curva sola daba 0.146 de tinta en
            //  los dos -un trazo gris a 17 px-; la meseta con su pico en la
            //  rodilla y el hueco relleno hasta la base es la respuesta del
            //  filtro como la pinta cualquier analizador, y sigue leyendose
            //  en espejo.
            case Id::flt:
                linea (L, 2.0f, 20.0f, 22.0f, 20.0f);
                R.startNewSubPath (2.0f, 20.0f);
                R.lineTo (2.0f, 8.0f);  R.lineTo (9.5f, 8.0f);
                R.quadraticTo (11.5f, 8.0f, 12.5f, 4.5f);
                R.quadraticTo (13.5f, 8.0f, 14.5f, 9.0f);
                R.cubicTo (17.0f, 11.0f, 18.0f, 17.0f, 21.0f, 20.0f);
                R.closeSubPath();
                break;

            case Id::hpf:
                linea (L, 2.0f, 20.0f, 22.0f, 20.0f);
                R.startNewSubPath (22.0f, 20.0f);
                R.lineTo (22.0f, 8.0f); R.lineTo (14.5f, 8.0f);
                R.quadraticTo (12.5f, 8.0f, 11.5f, 4.5f);
                R.quadraticTo (10.5f, 8.0f, 9.5f, 9.0f);
                R.cubicTo (7.0f, 11.0f, 6.0f, 17.0f, 3.0f, 20.0f);
                R.closeSubPath();
                break;

            //  DRV: la onda RECORTADA contra sus dos topes, que es literalmente
            //  lo que hace un saturador.
            //  LOS DOS TECHOS, A LOS BORDES: con los renglones en 6.5 y 17.5
            //  la caja salia de 24 x 14.
            case Id::drv:
                linea (L, 2.0f, 3.4f, 22.0f, 3.4f);
                linea (L, 2.0f, 20.6f, 22.0f, 20.6f);
                L.startNewSubPath (2.0f, 12.0f);
                L.lineTo (4.5f, 3.4f);  L.lineTo (8.5f, 3.4f);
                L.lineTo (11.5f, 20.6f); L.lineTo (15.5f, 20.6f);
                L.lineTo (18.5f, 3.4f); L.lineTo (22.0f, 3.4f);
                break;

            //  DLY: el golpe y sus ecos, que BAJAN. El primero es el mas alto y
            //  los tres de detras van cayendo, que es lo que una linea de
            //  retardo hace con lo que entra.
            //
            //  EL LLENO Y LOS HUECOS ERAN FICCION. Decia aqui "el primero LLENO
            //  y los otros huecos, que es lo que separa el original de lo que
            //  devuelve la linea" y no se pintaba asi: un rectangulo TRAZADO de
            //  3.0 px de ancho tiene las dos paredes de `kBanda` -1.73 cada
            //  una- asi que se solapan y el hueco no existe. Lo que salia eran
            //  tres barras MACIZAS de 4.7 px al lado de una de 3.0, o sea los
            //  ecos mas GORDOS que el golpe: justo lo contrario de lo que el
            //  comentario prometia, y de paso tres grosores en un icono de
            //  cuatro trazos. Las cuatro son bandas de `kBanda` y lo que las
            //  separa es el ALTO, que es lo que ya se leia de verdad.
            case Id::dly:
                linea (L, 1.5f, 21.0f, 22.5f, 21.0f);
                bandaV (R,  4.0f,  4.0f, 17.0f);
                bandaV (R,  9.5f,  8.5f, 12.5f);
                bandaV (R, 14.5f, 12.5f,  8.5f);
                bandaV (R, 20.0f, 16.0f,  5.0f);
                break;

            //  BIT: la escalera. Un reductor de bits convierte una rampa en
            //  peldanos, y eso es exactamente el dibujo.
            //
            //  Y GRUESA, con la rampa que entro por encima. La escalera de
            //  trazo media 0.104 de tinta -el mas gris de los 109- y a los 17
            //  px a los que se lee eran cuatro peldanos de un pelo. Maciza
            //  hasta la base tampoco: un bloque que sube a la derecha es
            //  `niveles` -0.272 de distancia, el par mas cercano de todos-.
            //  Asi que es la misma escalera con cuerpo, y la rampa que le
            //  corta las esquinas es lo que entro: los dos a la vez.
            case Id::bit:
            {
                //  Y LOS CUATRO PELDANOS SE REPARTEN MAS ALTO. Iban de 20.0 a
                //  6.5 -13.5 px de salto- y con la banda de 3.4 lo pintado
                //  llegaba a 16.9 de alto, o sea el 0.70 justo del marco. Con
                //  la banda a `kBanda` lo pintado baja a 15.2 y el marco se
                //  queda en 0.67: no es que el dibujo este peor, es que el
                //  grosor ya no le estaba haciendo de relleno. Los peldanos
                //  pasan a 21.2 .. 4.7 -16.5 de salto- y el alto pintado sube a
                //  18.2, que son 0.76 del marco. La diagonal los sigue, que es
                //  la recta contra la que la escalera se lee.
                juce::Path escalera;
                escalera.startNewSubPath (2.0f, 21.2f);
                escalera.lineTo (7.0f, 21.2f);  escalera.lineTo (7.0f, 15.7f);
                escalera.lineTo (12.0f, 15.7f); escalera.lineTo (12.0f, 10.2f);
                escalera.lineTo (17.0f, 10.2f); escalera.lineTo (17.0f, 4.7f);
                escalera.lineTo (22.0f, 4.7f);
                //  LA ESCALERA, CON EL PELO DE LA CASA. Llevaba 3.4 -el doble
                //  de `kBanda`- y la recta de debajo va con el pelo normal: dos
                //  grosores en el mismo icono, que es el caso que no admite
                //  discusion. Las esquinas siguen en escuadra y las puntas a
                //  ras, que eso es el remate de una escalera y no su grosor.
                juce::PathStrokeType (kBanda, juce::PathStrokeType::mitered,
                                              juce::PathStrokeType::butt)
                    .createStrokedPath (R, escalera);
                linea (L, 2.0f, 21.2f, 22.0f, 4.7f);
                break;
            }

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
            //
            //  La original MACIZA y la copia de trazo. Dos trazos finos
            //  iguales median 0.137 de tinta y a 17 px eran una mancha
            //  ondulada; con la primera onda como banda se ve cual es la
            //  original y cual la copia, que es exactamente el efecto.
            case Id::cho:
            {
                auto onda = [] (juce::Path& p, float dx, float dy)
                {
                    p.startNewSubPath (2.0f + dx, dy);
                    p.cubicTo (6.0f + dx, dy - 5.0f, 9.0f + dx, dy + 5.0f, 13.0f + dx, dy);
                    p.cubicTo (16.0f + dx, dy - 3.5f, 18.0f + dx, dy + 3.5f, 21.0f + dx, dy);
                };
                //  LAS DOS, MAS SEPARADAS: pegadas al medio la caja salia de
                //  23 x 16. No se toca el tirador de la onda —la tinta se
                //  quedaria por debajo de lo que habia— se separan los dos
                //  renglones hasta donde los puntos de control caben en la
                //  rejilla: 5.6 arriba y 18.2 abajo.
                //  LA BANDA SE ENGORDA CON `kBanda` Y NO A OJO. Era la onda de
                //  arriba cerrada contra una copia 3.4 px mas abajo: el doble
                //  del pelo con el que se dibuja la de abajo, en el mismo
                //  icono, y encima de grosor DESIGUAL -separar dos ondas en
                //  vertical deja la banda fina donde la onda sube y gorda donde
                //  va plana-. Engordar el camino da las dos cosas: un grosor y
                //  el mismo que el resto.
                {
                    juce::Path o;
                    onda (o, 0.0f, 5.6f);                    // la original, como banda
                    juce::PathStrokeType (kBanda, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded)
                        .createStrokedPath (R, o);
                }
                onda (L, 1.0f, 18.2f);                       // y la copia, desfasada
                break;
            }

            //  FLA: el peine. Muescas EQUIESPACIADAS sobre el renglon, que es
            //  lo que hace un retardo corto sumado al seco — y lo que lo
            //  separa de PHA, cuyas muescas ni son tantas ni estan repartidas.
            //  Y LOS DIENTES BAJAN DESDE y=8, no desde y=6.
            //
            //  Los dos peines de esta familia -FLA y PHA- colgaban de una linea
            //  alta y su tinta se quedaba en la mitad de arriba de la caja:
            //  medido, el centro de lo que se PINTA caia a **4.0 px** del centro
            //  de la rejilla, contra 0.0 en ochenta y nueve iconos y 0.5 en
            //  veinte. `reparteTapa` centra la CAJA en la tapa, asi que en una
            //  fila con sus vecinos los dos se leian subidos. Ver
            //  Tests/iconos.py, regla del centro.
            //
            //  Y los dientes pasan a ser RECTOS y no curvas: el limite de una
            //  curva lo marcan sus puntos de control -y=20 para una tinta que
            //  llegaba a 13- asi que la caja que el volcado publica no era la
            //  que se pinta, y la regla del centro habria medido una y la de
            //  CABE la otra. Con lineas, lo declarado y lo dibujado son lo
            //  mismo. Un peine de dientes rectos ademas es lo que un flanger
            //  hace: ceros regulares, no ondas.
            //
            //  Y SE QUEDA COMO ESTA, medido: los dientes del 8 al 16 dan 0.335
            //  contra `pit` -desenfocados, cinco dientes cortos sobre un
            //  renglon son una onda apretada-, y se intento alejarlos dos veces.
            //  Curvos del 6 al 20, la tinta cae a 4 px del centro de su caja
            //  por lo de los puntos de control. Rectos del 6 al 19.5, la tinta
            //  sube a 0.405 y el peine se va a 0.301 de `sec` y a 0.339 de
            //  `sonido`: cinco puas gordas son una fila de bloques. Los dos
            //  numeros son peores que el que habia, asi que el que habia.
            case Id::fla:
                linea (L, 2.0f, 3.6f, 22.0f, 3.6f);
                for (int i = 0; i < 5; ++i)
                {
                    const float x = 4.0f + (float) i * 4.2f;
                    L.startNewSubPath (x - 1.3f, 3.6f);
                    L.lineTo (x, 20.4f - (float) i * 2.4f);
                    L.lineTo (x + 1.3f, 3.6f);
                }
                break;

            //  PHA: DOS muescas anchas y separadas sobre una linea plana. Un
            //  phaser no peina: pone unos pocos ceros y los pasea.
            //
            //  Las muescas RELLENAS y el renglon como banda: de trazo daba
            //  0.120 de tinta y a 17 px eran dos uves grises. Siguen siendo
            //  dos, anchas y separadas, que es lo que lo aparta de `fla`.
            case Id::pha:
                bandaH (R, 2.0f, 6.6f, 20.0f);
                R.startNewSubPath (4.0f, 8.0f);  R.lineTo (7.5f, 20.0f);  R.lineTo (11.0f, 8.0f);  R.closeSubPath();
                R.startNewSubPath (13.0f, 8.0f); R.lineTo (16.5f, 20.0f); R.lineTo (20.0f, 8.0f); R.closeSubPath();
                break;

            //  TRM: la ENVOLVENTE que late. El relleno dice amplitud y no
            //  tono, que es lo unico que un temblor cambia; dibujarlo como una
            //  onda lo dejaria a un pelo de `cho`.
            //
            //  Y SOBRE UN RENGLON, como las demas envolventes de esta tabla
            //  -`insPluck` sube de golpe y cae, `insColchon` abre despacio-:
            //  esta sube y baja DOS VECES, que es latir. Los tres lobulos
            //  centrados median 0.396 contra `pit` -los dos eran una
            //  oscilacion horizontal en el renglon del medio- y llenaban el
            //  83% de su caja; en barras verticales se iban a 0.33 de
            //  `sonido`, que son once barras sobre ese mismo renglon. Dos
            //  jorobas macizas sobre la base no son una onda ni son barras.
            //
            //  Y SEPARADAS, con un tramo de renglon entre las dos: pegadas por
            //  el valle median 0.375 contra `stop`, que desenfocado es la misma
            //  mancha cuadrada. Con el hueco, a 17 px son dos golpes y no uno.
            case Id::trm:
                linea (L, 1.5f, 20.5f, 22.5f, 20.5f);
                R.startNewSubPath (2.0f, 20.5f);
                R.quadraticTo (3.5f, 3.5f, 6.0f, 3.5f);
                R.quadraticTo (8.5f, 3.5f, 10.0f, 20.5f);
                R.closeSubPath();
                R.startNewSubPath (14.0f, 20.5f);
                R.quadraticTo (15.5f, 3.5f, 18.0f, 3.5f);
                R.quadraticTo (20.5f, 3.5f, 22.0f, 20.5f);
                R.closeSubPath();
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
                //  CON LA ONDA ALTA: con el tirador en 7.5 la cuadratica solo
                //  subia la mitad y la caja salia de 24 x 10 —0.42 del marco
                //  por el lado corto, empatada con `fla` en lo peor del juego—.
                //  Va en CUBICAS y no en cuadraticas por los PUNTOS DE CONTROL:
                //  una cubica con los dos tiradores a la misma altura llega a
                //  tres cuartos de ella, asi que para pintar 8.3 px basta
                //  declarar 11 y la caja que publica `limites` sigue dentro de
                //  la rejilla. Con una cuadratica habria que declarar 16.6 y
                //  CABE lo daria por fuera con el dibujo entero dentro.
                const float pasos[] = { 6.0f, 5.0f, 4.0f, 3.2f, 2.6f };
                float x = 2.0f;
                L.startNewSubPath (x, 12.0f);
                for (int i = 0; i < 5 && x < 22.0f; ++i)
                {
                    const float w = pasos[i];
                    L.cubicTo (x + w * 0.16f, 12.0f - 11.0f, x + w * 0.34f, 12.0f - 11.0f,
                               x + w * 0.5f, 12.0f);
                    L.cubicTo (x + w * 0.66f, 12.0f + 11.0f, x + w * 0.84f, 12.0f + 11.0f,
                               x + w, 12.0f);
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

            //  ==================================================================
            //  LOS DOS QUE SE PIDIERON.

            //  WAH: la banda estrecha Y EL RECORRIDO POR EL QUE VIAJA. El pico
            //  solo no valdria -a trece pixeles es un palo, o sea `rng` con una
            //  punta- y una curva de respuesta lo dejaria a un pelo de `flt` y
            //  de `eq`. Lo que lo hace suyo son las DOS marcas de los extremos:
            //  dicen que esa banda no esta quieta, que es toda la diferencia
            //  entre un wah y un filtro de banda.
            case Id::wah:
                linea (L, 2.0f, 19.0f, 22.0f, 19.0f);
                L.startNewSubPath (8.5f, 19.0f);
                L.quadraticTo (10.5f, 4.0f, 12.0f, 4.0f);
                L.quadraticTo (13.5f, 4.0f, 15.5f, 19.0f);
                linea (L, 3.5f, 16.0f, 3.5f, 10.5f);
                linea (L, 20.5f, 16.0f, 20.5f, 10.5f);
                break;

            //  OCT: UNA onda y su mitad, una encima de la otra. Es literalmente
            //  lo que el efecto hace -una octava arriba y una abajo- y es lo que
            //  lo separa de `cho`, que son dos ondas del MISMO periodo
            //  desplazadas, y de `pit`, que es una sola que se aprieta. La de
            //  arriba lleva cuatro ciclos y la de abajo uno: a trece pixeles lo
            //  que se lee es la RELACION, no los ciclos.
            case Id::oct:
            {
                //  Y LAS DOS ONDAS LLEGAN AL MARCO. Con los tiradores en 3.0 y
                //  12.0 arriba y 11.5 y 21.5 abajo, una cuadratica sube la MITAD
                //  de lo que declara: lo pintado eran 22 x 15 y el marco se
                //  quedaba a 0.67 por el lado corto.
                //
                //  El comentario que habia aqui decia que los tiradores no se
                //  podian bajar mas porque `Path::getBounds` acota por ellos y
                //  CABE daba `1.6 .. 24.3 FUERA` con el trazo dentro. Eso ya no
                //  es cierto y por eso se reescribe: `cajaPintada` aplana el
                //  camino y mide por donde pasa el trazo, asi que un tirador en
                //  25.0 que pinta hasta 21.5 ya no saca a nadie de su caja.
                L.startNewSubPath (2.0f, 6.0f);
                for (int i = 0; i < 4; ++i)
                {
                    const float x = 2.0f + (float) i * 5.0f;
                    L.quadraticTo (x + 1.25f,  0.0f, x + 2.5f, 6.0f);
                    L.quadraticTo (x + 3.75f, 12.0f, x + 5.0f, 6.0f);
                }
                L.startNewSubPath (2.0f, 18.0f);
                L.quadraticTo (7.0f,  11.0f, 12.0f, 18.0f);
                L.quadraticTo (17.0f, 25.0f, 22.0f, 18.0f);
                break;
            }

            //  AMB: LA SALA, y no otra reverb. `rev` son tres arcos que se
            //  abren -la cola, o sea algo que ya no tiene sitio-; un ambiente
            //  ES el sitio, asi que lo que se dibuja son las PAREDES con la
            //  fuente dentro y dos rayos que rebotan en ellas. Es el unico
            //  icono de la tabla con un recinto cerrado, asi que se separa de
            //  `rev` a trece pixeles sin mirarlo dos veces.
            //
            //  Y el rayo TOCA la pared y vuelve, que es lo que lo hace legible:
            //  dos rayas sueltas dentro de un rectangulo son un rectangulo con
            //  dos rayas.
            //  FRM: LAS DOS RESONANCIAS, que es lo que hay que ver para que no
            //  se confunda con `eq` -una curva con nodos- ni con `flt`. Dos
            //  picos estrechos sobre el mismo renglon, el segundo mas alto y
            //  mas fino: es literalmente lo que la vocal hace con el espectro,
            //  y es el unico icono de la tabla con dos puntas.
            case Id::frm:
                linea (L, 2.0f, 20.5f, 22.0f, 20.5f);
                L.startNewSubPath (3.0f, 20.5f);
                L.quadraticTo (6.5f,  20.0f,  7.5f,  9.0f);
                L.quadraticTo (8.5f,  20.0f, 12.0f, 19.5f);
                L.quadraticTo (14.5f, 19.5f, 15.5f,  4.5f);
                L.quadraticTo (16.5f, 19.5f, 21.0f, 20.0f);
                break;

            //  FLD: LA ONDA REBOTANDO CONTRA EL TECHO. El techo se dibuja -dos
            //  renglones- porque sin el no se entiende que la onda esta
            //  chocando, y lo que la separa de `drv` es justo eso: alli la onda
            //  se aplana contra el techo y aqui se DA LA VUELTA.
            //  Y LOS DOS ESPEJOS, A LOS BORDES: con los renglones en 6 y 18 la
            //  caja salia de 24 x 16.
            case Id::fld:
                linea (L, 2.0f,  3.4f, 22.0f,  3.4f);
                linea (L, 2.0f, 20.6f, 22.0f, 20.6f);
                L.startNewSubPath (2.0f, 20.6f);
                L.lineTo (6.0f,   3.4f);
                L.lineTo (10.0f, 20.6f);
                L.lineTo (14.0f,  3.4f);
                L.lineTo (18.0f, 20.6f);
                L.lineTo (22.0f,  3.4f);
                break;

            //  ROT: EL ALTAVOZ QUE GIRA. Una flecha que da la vuelta alrededor
            //  del cono. `rev` son tres arcos que se ABREN -la cola- y esto es
            //  UNO cerrado con punta: lo que se dibuja no es el sonido, es el
            //  aparato, que es lo que lo separa de todo lo demas de la familia.
            case Id::rot:
                R.addEllipse (10.0f, 10.0f, 4.0f, 4.0f);
                L.addCentredArc (12.0f, 12.0f, 7.5f, 7.5f, 0.0f,
                                 0.5f, juce::MathConstants<float>::twoPi - 0.9f, true);
                linea (L, 16.0f,  6.6f, 19.2f,  6.0f);
                linea (L, 16.0f,  6.6f, 17.6f,  9.4f);
                break;

            //  PNG: EL ECO QUE REBOTA. Dos paredes y el camino en zigzag entre
            //  ellas, con el golpe relleno y los rebotes a trazo. `dly` son
            //  ecos que caen en LINEA RECTA; aqui lo que se ve es que cambian
            //  de lado, que es todo lo que hay que entender.
            case Id::png:
                linea (L,  3.0f,  4.0f,  3.0f, 20.0f);
                linea (L, 21.0f,  4.0f, 21.0f, 20.0f);
                R.addEllipse (1.6f,  5.6f, 2.8f, 2.8f);
                linea (L,  3.0f,  7.0f, 21.0f, 11.0f);
                linea (L, 21.0f, 11.0f,  3.0f, 15.0f);
                linea (L,  3.0f, 15.0f, 21.0f, 18.5f);
                break;

            //  DUC: LA CURVA DEL BOMBEO. Cae de golpe y vuelve despacio, dos
            //  veces: es la forma exacta que la etapa calcula, y es lo que la
            //  separa de `cmp` -una transferencia, o sea nivel contra nivel- y
            //  de `trm`, que sube y baja igual de rapido en las dos mitades.
            case Id::duc:
                linea (L, 2.0f, 20.5f, 22.0f, 20.5f);
                L.startNewSubPath (2.0f, 5.0f);
                L.lineTo (2.0f, 17.0f);
                L.quadraticTo (6.0f, 16.0f, 12.0f, 5.0f);
                L.lineTo (12.0f, 17.0f);
                L.quadraticTo (16.0f, 16.0f, 22.0f, 5.0f);
                break;

            //  REP: EL TROZO QUE SE REPITE. Son BLOQUES y no picos, porque lo
            //  que se repite es un trozo entero de audio y no un golpe, y eso
            //  es lo que lo aparta de `dly`, que son cuatro lineas.
            //
            //  LAS TRES BARRAS, DE ARRIBA ABAJO Y EN BAJADA: con 10 px de alto
            //  en 18 de ancho la caja se quedaba en 0.58 del marco por el lado
            //  corto, y las tres iguales y altas se fueron a 0.3975 de `pads`
            //  —tres barras parejas, desenfocadas, son una rejilla—. En bajada
            //  dicen ademas lo que un repetidor hace: la primera es la que
            //  entro y las otras dos son copias que se van.
            //
            //  Y LOS BLOQUES SON MAS ANCHOS PARA QUE EL HUECO EXISTA. El
            //  primero relleno -lo que se grabo- y los dos de detras a trazo
            //  -lo que se devuelve-, que es la misma regla que separa el golpe
            //  de sus ecos en `dly`; lo que pasa es que no se pintaba. Con 4.3
            //  px de ancho, un rectangulo TRAZADO tiene las dos paredes de
            //  `kBanda` -1.73 cada una- y entre ellas queda una ranura de 0.84:
            //  los dos de detras salian practicamente macizos con una raya, o
            //  sea el mismo bloque que el primero. A 5.0 de ancho la ranura es
            //  de 1.54 y el hueco se ve, que es lo que el dibujo llevaba doce
            //  versiones prometiendo. Se probo tambien dejar los tres rellenos:
            //  la tinta se iba a 0.4900 contra un tope de 0.55, o sea a un dedo
            //  de ser una mancha, y se perdia la unica cosa que `rep` dice.
            case Id::rep:
                R.addRectangle ( 3.0f,  3.6f, 5.0f, 16.8f);
                L.addRectangle ( 9.6f,  7.0f, 5.0f, 13.4f);
                L.addRectangle (16.2f, 10.4f, 5.0f, 10.0f);
                break;

            case Id::amb:
                L.addRectangle (2.0f, 4.0f, 20.0f, 16.0f);
                R.addEllipse (6.5f, 10.5f, 3.0f, 3.0f);
                linea (L, 8.0f, 12.0f, 17.0f,  5.0f);
                linea (L, 17.0f, 5.0f, 20.5f, 11.5f);
                linea (L, 8.0f, 12.0f, 16.0f, 19.0f);
                linea (L, 16.0f, 19.0f, 20.5f, 14.5f);
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

            //  Y EL APARATO ES MAS ANCHO: 12 de ancho por 21 de alto dejaban
            //  el marco a 0.58 por el lado corto.
            case Id::sistema:
                L.addRoundedRectangle (3.6f, 1.5f, 16.8f, 21.0f, 2.6f);
                linea (L, 8.6f, 19.6f, 15.4f, 19.6f);
                linea (L, 3.6f, 5.6f, 20.4f, 5.6f);
                break;

            //  CADENA: dos eslabones enganchados. Es lo que un patron encadenado
            //  ES, y no se parece a nada mas del juego.
            case Id::cadena:
            {
                juce::Path a, b;
                //  Y LA PARED DEL ESLABON ES DE `kBanda`: estaba a 2.4, que es
                //  medio pelo mas de lo que mide una linea de esta cara. El hueco
                //  es el de fuera METIDO ese pelo, asi que la pared sale del mismo
                //  sitio que todo lo demas y no de un numero tecleado.
                a.addRoundedRectangle (0.0f, 6.5f, 13.0f, 8.0f, 4.0f);
                a.addRoundedRectangle (kBanda, 6.5f + kBanda,
                                       13.0f - 2.0f * kBanda, 8.0f - 2.0f * kBanda,
                                       4.0f - kBanda);
                a.setUsingNonZeroWinding (false);
                b = a;
                a.applyTransform (juce::AffineTransform::translation (2.0f, -1.0f));
                b.applyTransform (juce::AffineTransform::translation (9.0f, 4.0f));

                //  Y LA CADENA VA EN DIAGONAL A CUARENTA Y CINCO. Tumbada, los
                //  dos eslabones daban 24 x 16 y el marco se quedaba a 0.67 por
                //  el lado corto. Un giro no deforma nada —los eslabones siguen
                //  siendo los mismos, que es lo que un escalado por un lado
                //  solo no respeta— y a 45 grados una tira de 19 x 10 mide lo
                //  mismo de ancho que de alto. El eje de la cadena ya venia a
                //  35 grados —los eslabones estan desplazados 7 y 5— asi que lo
                //  que falta es 0.17 rad, y al otro lado: con -0.60 se tumba
                //  del todo y la caja sale de 21 x 12, que es PEOR.
                juce::Path par;
                par.addPath (a);
                par.addPath (b);
                par.applyTransform (juce::AffineTransform::rotation (0.17f, 12.0f, 12.0f));
                R.addPath (par);

                //  Y LOS HUECOS, HUECOS: la bandera va en el camino que se
                //  PINTA. `Path::addPath` copia los tramos y no la bandera, asi
                //  que ponerla en el eslabon y despues volcarlo en otro camino
                //  la pierde — y los dos eslabones salian macizos, una mancha
                //  de 0.388 de tinta en vez de una cadena.
                R.setUsingNonZeroWinding (false);
                break;
            }

            //  PATRON: el patron ENTERO, o sea el marco alrededor de los pasos.
            //  Las herramientas de esta pagina actuan sobre todo el bloque, no
            //  sobre un paso: el marco es justamente eso.
            case Id::patron:
                L.addRoundedRectangle (1.5f, 4.5f, 21.0f, 15.0f, 1.8f);
                bandaV (R,  5.8f, 8.0f, 8.0f);
                bandaV (R, 12.0f, 8.0f, 8.0f);
                bandaV (R, 18.2f, 8.0f, 8.0f);
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
            //  EL CUERPO MAS ANCHO Y EL ARCO MAS BAJO: 17 de ancho contra 21
            //  de alto dejaban el marco a 0.67 por el lado corto.
            case Id::fijo:
                R.addRoundedRectangle (2.8f, 10.0f, 18.4f, 12.0f, 1.9f);
                t.lleno = 0.88f;
                L.startNewSubPath (7.6f, 10.0f);
                L.lineTo (7.6f, 6.6f);
                L.cubicTo (7.6f, 2.0f, 16.4f, 2.0f, 16.4f, 6.6f);
                L.lineTo (16.4f, 10.0f);
                break;

            //  Y el abierto se abre DE VERDAD: el arco se levanta y se va a
            //  la derecha, que es el gesto de soltar.
            //
            //  El comentario que habia aqui decia que el cuerpo se separo
            //  porque con el arco en el mismo sitio los dos candados median
            //  0.086 y `Tests/iconos.py` los daba por el mismo dibujo. Eso ya
            //  no es cierto y por eso se reescribe: la prueba tiene desde
            //  entonces la tabla `ESTADOS`, que exime a este par exacto con su
            //  razon —«dos estados de un mismo mando SI pueden parecerse»—. O
            //  sea que el dibujo se estaba pagando para contentar a una regla
            //  que ya no lo pedia.
            //
            //  EL CUERPO ES EL MISMO QUE EL DE `fijo`, Y ESO NO ES UNA COPIA:
            //  es un candado en dos ESTADOS, no dos candados. Lo tenia distinto
            //  en las tres cosas que se pueden tener distintas, y medido con
            //  `ZATI_ICONOS` a 24 px salia asi:
            //
            //      fijo        caja x  3..20   ancho 18   tinta 0.3572
            //      momentaneo  caja x  1..22   ancho 22   tinta 0.3871
            //
            //  o sea que al conmutar la tapa el cuerpo encogia de 17.0 a 15.0,
            //  se corria 2.5 px a la izquierda -centro 12.0 contra 9.5-, perdia
            //  el relleno y la caja entera crecia CUATRO pixeles, dos por lado.
            //  Un estado que salta de sitio y de tamaño no se lee como el mismo
            //  objeto: se lee como que la tapa cambio de icono.
            //
            //  Lo unico que cambia es el ARCO, que es lo unico que cambia en un
            //  candado de verdad. Medido con el cuerpo ya igualado: caja x
            //  4..19 contra 3..20, alto 20 los dos, tinta 0.3416 contra 0.3572
            //  — o sea el mismo objeto en dos estados, que es lo que es.
            //
            //  Y ABIERTO ES UNA PATA ANCLADA Y LA OTRA LEVANTADA, que es como
            //  se abre un candado de verdad: el arco gira sobre su pasador y la
            //  pata libre SALE del cuerpo. Lo que habia era un arco que arrancaba
            //  de la izquierda del cuerpo y se iba volando hacia la derecha, con
            //  las dos patas en el aire: eso no es un candado abierto, es un asa
            //  suelta encima de una caja. Llego del telefono —«el candado abierto
            //  no es realista»— y es verdad.
            //
            //  El comentario que habia aqui decia que la version canonica —la
            //  pata derecha anclada y el extremo libre a la izquierda— se probo
            //  y salia `momentaneo se sale de su caja`, con el trazo en -0.4.
            //  Eso ya no es cierto y por eso se reescribe: ese -0.4 eran los
            //  PUNTOS DE CONTROL del arco y no el trazo, y desde `cajaPintada`
            //  la caja se mide por donde pasa la tinta. La version buena estaba
            //  descartada por una medida que medía otra cosa.
            //
            //  La pata anclada baja DENTRO del cuerpo —de 10.0 para abajo la
            //  tapa el relleno, que es lo que hace un pasador— y la libre no
            //  cuelga: se queda ARRIBA, a 4.4, porque un arco que se abre gira
            //  sobre el pasador y el extremo se LEVANTA. Primero se dejo
            //  colgando a 8.0, dos pixeles por encima del canto, y medido salio
            //  peor que lo que habia: 0.1314 contra `fijo` —era su mismo arco
            //  simetrico con una pata un poco mas corta, y dos pixeles de hueco
            //  no se ven a los trece a los que la tapa se dibuja—. Levantado es
            //  un gancho y no un arco, que es lo unico que separa esta tapa de
            //  `fijo`, y el hueco pasa a ser de seis pixeles.
            case Id::momentaneo:
                R.addRoundedRectangle (2.8f, 10.0f, 18.4f, 12.0f, 1.9f);
                t.lleno = 0.88f;
                L.startNewSubPath (16.4f, 11.4f);
                L.lineTo (16.4f, 6.2f);
                L.cubicTo (16.4f, 0.8f, 6.4f, 0.6f, 3.8f, 4.8f);
                break;

            //  MIDI: la clavija de cinco patillas, que es como se reconoce sin
            //  leer nada.
            case Id::midi:
                L.addEllipse (2.0f, 2.0f, 20.0f, 20.0f);
                bandaH (R, 8.5f, 4.7f, 7.0f);
                for (int i = 0; i < 5; ++i)
                {
                    const float a = juce::MathConstants<float>::pi * (0.15f + 0.175f * (float) i);
                    R.addEllipse (12.0f - std::cos (a) * 6.6f - 1.5f,
                                  12.0f + std::sin (a) * 6.6f - 1.5f, 3.0f, 3.0f);
                }
                break;

            //  MEDIR: la regla, con sus marcas desiguales.
            //  Y LA REGLA ES MAS ALTA: 21 de ancho por 9 de alto dejaban el
            //  marco a 0.50 por el lado corto.
            case Id::medir:
                L.addRectangle (1.6f, 3.4f, 20.8f, 17.2f);
                linea (L, 6.0f, 3.4f, 6.0f, 14.6f);
                linea (L, 10.0f, 3.4f, 10.0f, 10.6f);
                linea (L, 14.0f, 3.4f, 14.0f, 14.6f);
                linea (L, 18.0f, 3.4f, 18.0f, 10.6f);
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
                bandaV (R, 16.3f, 6.5f, 11.0f);
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

            //  INSERTO: la señal entra por la izquierda, ATRAVIESA la caja y
            //  sale por la derecha. Un solo camino y la caja EN el.
            //  LA CAJA, ALTA Y ESTRECHA: 10 x 10 en una tira de 21 dejaban el
            //  marco a 0.42 por el lado corto, y la primera version que llenaba
            //  —12.8 de ancho por 18 de alto— se fue a 0.2700 de `stop`, que es
            //  un cuadrado relleno de 17 x 17: desenfocadas, dos manchas
            //  compactas del mismo tamano son el mismo dibujo. Con 9 de ancho
            //  por 18 de alto es una PASTILLA metida en el camino, que es lo que
            //  un inserto es, y ya no es un cuadrado.
            case Id::inserto:
                linea (L, 1.5f, 12.0f, 7.5f, 12.0f);
                R.addRectangle (7.5f, 3.0f, 9.0f, 18.0f);
                linea (L, 16.5f, 12.0f, 22.5f, 12.0f);
                break;

            //  ENVIO: la señal SIGUE de largo por arriba y una rama baja a la
            //  caja. Dos caminos, y la caja colgando de uno — que es
            //  exactamente lo que el motor hace con DLY y REV.
            //  Y LA RAMA BAJA MAS: el renglon en 6 y la caja hasta 21 dejaban
            //  el marco a 0.67 por el lado corto.
            case Id::envio:
                linea (L, 1.5f, 3.4f, 22.5f, 3.4f);
                linea (L, 8.0f, 3.4f, 8.0f, 13.6f);
                R.addRectangle (3.6f, 13.6f, 8.8f, 8.0f);
                break;

            //  APAGAR: el anillo partido y la barra. Ver la enum.
            case Id::apagar:
                //  El arco NO se cierra por arriba -de 40 a 320 grados- que es
                //  lo que separa este dibujo de un circulo y lo que hace que a
                //  trece pixeles se lea como un interruptor y no como un punto.
                //  El arco en L y no en R: `R` se RELLENA y un arco relleno
                //  es un disco, no un anillo — mirado en la foto salia una
                //  mancha redonda que se leia como un mando. Es la misma
                //  distincion que ya usa `solo`: la diadema en L, los
                //  auriculares en R.
                L.addCentredArc (12.0f, 13.0f, 7.6f, 7.6f, 0.0f,
                                 juce::MathConstants<float>::pi * 0.22f,
                                 juce::MathConstants<float>::pi * 1.78f, true);
                linea (L, 12.0f, 3.4f, 12.0f, 11.0f);
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

            //  BAJOS: el CLAVIJERO de un bajo, con sus CUATRO clavijas - dos
            //  por lado y a distinta altura, como van de verdad para que no
            //  choquen - la cejuela y las cuatro cuerdas bajando por el
            //  mastil. Cuatro y no seis es lo que lo hace un bajo y no una
            //  guitarra.
            case Id::insBajo:
                R.addRoundedRectangle (7.6f, 1.4f, 8.8f, 9.8f, 2.2f);      // la pala
                //  Las cuatro clavijas con su boton, escalonadas.
                linea (L,  7.6f,  4.0f,  4.6f,  4.0f);
                R.addEllipse (2.0f, 2.6f, 2.8f, 2.8f);
                linea (L,  7.6f,  8.4f,  4.6f,  8.4f);
                R.addEllipse (2.0f, 7.0f, 2.8f, 2.8f);
                linea (L, 16.4f,  5.8f, 19.4f,  5.8f);
                R.addEllipse (19.2f, 4.4f, 2.8f, 2.8f);
                linea (L, 16.4f, 10.2f, 19.4f, 10.2f);
                R.addEllipse (19.2f, 8.8f, 2.8f, 2.8f);
                bandaH (R, 7.2f, 12.3f, 9.6f);                             // la cejuela
                for (int i = 0; i < 4; ++i)                                // y las cuatro cuerdas
                    linea (L, 9.0f + (float) i * 2.0f, 13.0f,
                              9.0f + (float) i * 2.0f, 22.0f);
                break;

            //  SUBS: tres barras que crecen hacia abajo. No es una onda: es
            //  "esto vive abajo", que es lo unico que un sub es.
            //
            //  Y CRECEN DE LARGO Y NO DE GORDO. Engordaban: 1.8, 3.0 y 4.6 px
            //  de alto, o sea tres grosores de linea distintos en un icono de
            //  tres trazos, y el de abajo casi el triple del pelo con el que
            //  esta dibujada la cara entera. Lo que dice "esto vive abajo" es
            //  que las barras se ENSANCHAN -10, 14 y 18 de largo, que ya las
            //  tenia-, no que se engorden; asi el dibujo dice lo mismo con un
            //  solo grosor.
            case Id::insSub:
                bandaH (R, 7.0f,  5.4f, 10.0f);
                bandaH (R, 5.0f, 11.5f, 14.0f);
                bandaH (R, 3.0f, 18.8f, 18.0f);
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
            //
            //  Y CON CUERPO: de trazo daba 0.160 de tinta y a 17 px el
            //  diapason era una U de un pelo con un punto al lado. Las puas y
            //  el mango van macizos y el martillo mas grande y con su palo:
            //  lo que golpea y lo que suena, los dos con peso.
            case Id::insEp:
                bandaV (R,  8.3f, 3.0f, 10.5f);                           // las dos puas
                bandaV (R, 15.7f, 3.0f, 10.5f);
                L.startNewSubPath (8.3f, 13.0f);                          // y la horquilla
                L.quadraticTo (12.0f, 18.0f, 15.7f, 13.0f);
                bandaV (R, 12.0f, 16.0f, 6.0f);                           // el mango
                R.addEllipse (1.5f, 5.0f, 5.0f, 5.0f);                    // el martillo
                linea (L, 4.0f, 10.0f, 4.0f, 20.0f);                      // y su palo
                break;

            //  ORGANOS: las tres barras de registro, cada una a su altura. Un
            //  organo se toca moviendo eso, no apretando teclas.
            //
            //
            //  Y YA NO: las barras de registro son, desenfocadas, `mezcla`
            //  -tres lineas verticales con un tirador cada una a su altura- y
            //  median 0.360 contra `cuadrar`, el par mas cercano de las
            //  familias de instrumentos. Se probaron dos cosas antes de esta y
            //  las dos se midieron: cinco tubos en monte sobre una base, a
            //  0.368 de `insTubo`, que en esta tabla ya ES los tubos; y cuatro
            //  barras macizas con tirador ancho sobre un panel, a 0.367 de
            //  `dly`, que son cuatro barras que bajan. Queda lo que un organo
            //  tiene y ningun otro instrumento de la tabla: DOS TECLADOS uno
            //  sobre otro, el de arriba metido, y la pedalera debajo. No es
            //  «un teclado», que es lo que PIANOS no dibuja a proposito: es
            //  el apilamiento. Y los dos manuales macizos median 0.369 contra
            //  `rack`, que son tres bandas apiladas: el de arriba va a trazo,
            //  con sus teclas negras, y el de abajo macizo.
            case Id::insOrgano:
                L.addRoundedRectangle (4.5f, 3.0f, 15.0f, 5.4f, 0.9f);     // el manual de arriba
                for (int i = 0; i < 3; ++i)
                    bandaV (R, 8.5f + (float) i * 4.2f, 3.0f, 3.0f);       // sus teclas negras
                //  EL DE ABAJO, DE CONTORNO Y NO MACIZO. Era una banda de 4.8 px
                //  en una rejilla de 24: la banda mas gorda de los 140 iconos
                //  junto con las de `rep` y `insSub`, casi tres veces el pelo con
                //  el que esta dibujado el manual de arriba, y en la misma cara.
                //  Un organo tiene dos manuales IGUALES -eso es lo que lo hace un
                //  organo y no un piano- asi que el de abajo se dibuja como el de
                //  arriba y la diferencia entre los dos deja de ser el grosor.
                L.addRoundedRectangle (2.0f, 10.3f, 20.0f, 5.4f, 0.9f);    // el de abajo
                for (int i = 0; i < 4; ++i)
                    bandaV (R, 6.2f + (float) i * 4.2f, 10.3f, 3.0f);      // y sus teclas
                for (int i = 0; i < 5; ++i)                                 // la pedalera
                    bandaV (R, 4.5f + (float) i * 4.2f, 17.6f, 4.5f);
                break;

            //  CUERDAS: LA VOLUTA, o sea la cabeza tallada de un instrumento de
            //  arco - el caracol, el clavijero y sus clavijas.
            //
            //  Eran cuatro lineas verticales con una diagonal encima, que es
            //  "unas rayas y un palo" y ademas la misma idea que CELLO, el de
            //  al lado en la tabla: los dos eran un arco cruzando algo. Lo que
            //  separa de verdad esta familia de la de al lado no es el arco
            //  -lo tienen las dos- sino la cabeza, y no hay otro dibujo en la
            //  tabla que sea una espiral.
            //
            //  Y EL CARACOL SE DIBUJA COMO ESPIRAL Y NO COMO DOS CIRCULOS: una
            //  elipse con otra dentro es, a 17 px, una diana. La espiral se
            //  traza punto a punto y se engorda con `createStrokedPath`, que es
            //  como ya se engorda la escalera de BIT: asi hay vuelta de verdad
            //  y no dos anillos.
            case Id::insCuerdas:
            {
                juce::Path espiral;
                for (int i = 0; i <= 40; ++i)               // vuelta y media, con hueco
                {
                    const float a = (float) i / 40.0f * 1.45f * juce::MathConstants<float>::twoPi;
                    const float r = 6.6f - (float) i / 40.0f * 4.4f;
                    const float x = 11.2f + r * std::cos (a - 1.1f);
                    const float y = 7.6f  + r * std::sin (a - 1.1f);
                    if (i == 0) espiral.startNewSubPath (x, y); else espiral.lineTo (x, y);
                }
                juce::PathStrokeType (kBanda, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded)
                    .createStrokedPath (R, espiral);
                R.addRoundedRectangle (9.7f, 12.8f, 4.6f, 6.8f, 1.0f);     // el clavijero
                //  CUATRO clavijas, dos por lado y a distinta altura, que son
                //  las que lleva: cuatro cuerdas, cuatro clavijas.
                //  Y LAS CLAVIJAS SALEN MAS: metidas, el dibujo media 16 de
                //  ancho por 24 de alto —0.67 del marco por el lado corto—.
                linea (L,  9.7f, 14.5f, 5.6f, 14.1f);
                R.addEllipse (2.9f, 12.8f, 2.8f, 2.8f);
                linea (L,  9.7f, 17.9f, 5.6f, 17.5f);
                R.addEllipse (2.9f, 16.2f, 2.8f, 2.8f);
                linea (L, 14.3f, 15.5f, 18.4f, 15.1f);
                R.addEllipse (18.3f, 13.8f, 2.8f, 2.8f);
                linea (L, 14.3f, 18.9f, 18.4f, 18.5f);
                R.addEllipse (18.3f, 17.2f, 2.8f, 2.8f);
                R.addRectangle (10.1f, 19.4f, 3.8f, 3.4f);                 // el arranque del mastil
            }
                break;

            //  COLCHONES: el regulador que abre y cierra. Un colchon es una
            //  envolvente lenta por los dos lados y nada mas.
            //
            //  Y RELLENO: de trazo daba 0.136 de tinta y a 17 px eran dos
            //  pelos que se abren. El hueco entre las dos curvas es el sonido
            //  que crece, asi que se pinta; la boca va curva para que no sea
            //  el triangulo de `play` mirado del reves.
            case Id::insColchon:
                R.startNewSubPath (2.0f, 12.0f);
                R.cubicTo (9.0f, 11.6f, 13.0f, 3.5f, 22.0f, 3.0f);
                R.quadraticTo (19.5f, 12.0f, 22.0f, 21.0f);
                R.cubicTo (13.0f, 20.5f, 9.0f, 12.4f, 2.0f, 12.0f);
                R.closeSubPath();
                break;

            //  PLUCKS: el golpe y su caida. Sube de golpe y se apaga, que es
            //  la definicion de pulsado.
            case Id::insPluck:
                linea (L, 2.0f, 21.0f, 22.0f, 21.0f);
                L.startNewSubPath (4.0f, 21.0f);
                L.lineTo (4.0f, 3.5f);
                L.cubicTo (10.0f, 4.5f, 12.5f, 19.5f, 22.0f, 20.0f);
                break;

            //  CAMPANAS: DOS, que el rotulo esta en plural y una sola no se
            //  distingue de las otras cosas de la tabla que son "ancho abajo y
            //  estrecho arriba". Macizas, con su ojo arriba -de trazo, que es
            //  por donde cuelgan- el aro de la boca y el badajo de la grande.
            //
            //  Medido, y por eso son dos: de trazo y sola era una U con un
            //  punto -0.180 de tinta-; maciza y de pie se puso a 0.391 de
            //  CUERDA PULS, que es una caja con un mastil, y a 0.396 de
            //  `recortar`; volteada se leia como un cono. Dos campanas de
            //  distinto tamano, una detras de otra, no se parecen a nada mas.
            case Id::insCampana:
                L.addEllipse (6.6f, 5.4f, 3.4f, 2.8f);                     // el ojo de la grande
                bandaV (R, 8.8f, 7.8f, 1.4f);
                R.startNewSubPath (3.0f, 18.0f);                           // y su cuerpo
                R.cubicTo (3.3f, 12.2f, 5.6f, 9.2f, 8.8f, 9.2f);
                R.cubicTo (12.0f, 9.2f, 14.3f, 12.2f, 14.6f, 18.0f);
                R.closeSubPath();
                bandaH (R, 1.8f, 19.55f, 14.0f);                           // el aro de la boca
                R.addEllipse (7.4f, 21.0f, 2.4f, 2.4f);                    // y el badajo
                L.addEllipse (16.4f, 1.4f, 2.8f, 2.4f);                    // la chica, detras
                bandaV (R, 18.2f, 3.4f, 1.2f);
                R.startNewSubPath (13.6f, 12.6f);
                R.cubicTo (13.8f, 8.2f, 15.6f, 5.8f, 17.9f, 5.8f);
                R.cubicTo (20.2f, 5.8f, 22.0f, 8.2f, 22.2f, 12.6f);
                R.closeSubPath();
                bandaH (R, 12.8f, 13.85f, 10.2f);
                break;

            //  METALES: la TROMPETA entera y no "un cono y una rayita" - la
            //  boquilla, el tudel, los TRES PISTONES de pie con sus varillas,
            //  y la campana abierta con su aro. Los tres pistones son lo que
            //  no tiene ningun otro dibujo de la tabla.
            //
            //  Y LA CAMPANA MANDA: con el tudel largo y la campana corta esto
            //  era un tubo con bultos encima, o sea VIENTOS - 0.398 medido. El
            //  triangulo que se abre es la firma de un metal, asi que se lleva
            //  la mitad del ancho y el tudel se acorta.
            case Id::insMetales:
                R.addRoundedRectangle (1.0f, 10.3f, 2.6f, 3.4f, 0.9f);     // la boquilla
                bandaH (R, 3.4f, 12.4f, 9.4f);                             // el tudel
                for (int i = 0; i < 3; ++i)                                // los tres pistones
                {
                    const float x = 4.9f + (float) i * 2.6f;
                    bandaV (R, x + 0.9f, 9.4f, 6.6f);
                    linea (L, x + 0.9f, 9.4f, x + 0.9f, 7.2f);
                }
                R.startNewSubPath (12.6f, 10.2f);                          // la campana
                R.lineTo (20.8f, 3.6f); R.lineTo (20.8f, 21.2f);
                R.lineTo (12.6f, 14.6f); R.closeSubPath();
                bandaV (R, 21.6f, 3.6f, 17.6f);                            // y su aro
                break;

            //  LEADS: una CUADRADA. Se gana la excepcion porque un lead ES eso:
            //  un pulso que corta por encima de la mezcla.
            //  LA CUADRADA, DE ARRIBA ABAJO: con 13 px de alto en 20 de ancho
            //  la caja se quedaba a 0.67 del marco por el lado corto.
            case Id::insLead:
                L.startNewSubPath (2.0f, 21.4f);
                L.lineTo (2.0f, 2.6f);  L.lineTo (9.0f, 2.6f);
                L.lineTo (9.0f, 21.4f); L.lineTo (16.0f, 21.4f);
                L.lineTo (16.0f, 2.6f); L.lineTo (22.0f, 2.6f);
                break;

            //  COROS: DOS personas y no una, una detras de la otra, y las dos
            //  MACIZAS. Un coro es mas de uno, y la silueta doble es lo que lo
            //  separa de cualquier icono de "persona" de la tabla.
            //
            //  Los hombros iban de trazo y la de atras se quedaba partida por
            //  la cabeza de la de delante: dos arcos cruzados no son dos
            //  personas. Macizas y sin tocarse son dos siluetas.
            case Id::insCoro:
                R.addEllipse (13.2f, 1.8f, 6.0f, 6.0f);                    // la de atras
                R.startNewSubPath (11.2f, 14.0f);
                R.cubicTo (11.5f, 8.8f, 20.9f, 8.8f, 21.2f, 14.0f);
                R.closeSubPath();
                R.addEllipse (3.6f, 8.6f, 7.2f, 7.2f);                     // y la de delante
                R.startNewSubPath (1.2f, 22.2f);
                R.cubicTo (1.6f, 15.2f, 13.0f, 15.2f, 13.4f, 22.2f);
                R.closeSubPath();
                break;

            //  CUERDA PULSADA: la guitarra entera - la caja con su CINTURA, la
            //  boca, el puente, las cuerdas que van del puente al mastil, el
            //  mastil largo y la pala CON SUS SEIS CLAVIJAS. Era la caja y la
            //  boca sueltas, que es media guitarra.
            //
            //  La cintura y el mastil largo son lo que la separa de CAMPANAS,
            //  que tambien es un cuerpo con algo encima: medido, con la caja
            //  ancha y el mastil corto los dos se ponian a 0.380. Y las seis
            //  clavijas son lo que la separa de BAJOS, que tiene cuatro: es la
            //  misma cuenta que hace cualquiera que mire una pala.
            case Id::insGuitarra:
                //  LA CAJA MAS ANCHA Y EL MASTIL MAS CORTO: con 14 px de ancho
                //  y la pala en y=1 el marco se quedaba a 0.58 por el lado
                //  corto. Se ensancha el CONTORNO —los mismos cuatro tramos,
                //  estirados desde el eje— y no el dibujo entero, que estirar
                //  por un lado solo saca la boca ovalada. Y los tiradores se
                //  estiran 1.38 y no 1.22: a 1.22 lo PINTADO seguia en 14.6, que
                //  es lo que de verdad cuenta desde que el marco se mide por
                //  donde pasa el trazo.
                L.startNewSubPath (12.0f, 9.4f);                           // la caja
                L.cubicTo (18.9f, 10.6f, 20.6f, 14.1f, 17.0f, 16.1f);
                L.cubicTo (21.4f, 18.9f, 18.6f, 22.5f, 12.0f, 22.5f);
                L.cubicTo (5.4f, 22.5f, 2.6f, 18.9f, 7.0f, 16.1f);
                L.cubicTo (3.4f, 14.1f, 5.1f, 10.6f, 12.0f, 9.4f);
                L.closeSubPath();
                R.addEllipse (9.7f, 13.4f, 4.6f, 4.6f);                    // la boca
                bandaH (R, 9.2f, 20.2f, 5.6f);                             // el puente
                for (int i = 0; i < 3; ++i)                                // las cuerdas
                    linea (L, 11.0f + (float) i * 1.0f, 19.4f,
                              11.0f + (float) i * 1.0f, 12.0f);
                bandaV (R, 12.0f, 4.6f, 5.0f);                             // el mastil, largo
                R.addRoundedRectangle (9.6f, 2.4f, 4.8f, 2.6f, 0.9f);      // la pala
                for (int i = 0; i < 3; ++i)                                // y sus seis clavijas
                {
                    const float y = 2.8f + (float) i * 0.9f;
                    R.addEllipse (7.6f, y, 1.6f, 1.6f);
                    R.addEllipse (14.8f, y, 1.6f, 1.6f);
                }
                break;

            //  MAZOS: la baqueta encima de las laminas.
            case Id::insMazo:
                bandaH (R, 3.0f, 16.95f, 18.0f);
                bandaH (R, 4.5f, 19.95f, 15.0f);
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

            //  VIENTOS: la travesera, con la embocadura ovalada en un extremo
            //  -el agujero por el que se sopla, de TRAZO para que sea un
            //  agujero y no un punto-, el bloque de llaves sobre el tubo y el
            //  pie ensanchado en el otro. Eran un tubo de trazo con cuatro
            //  puntos dentro, que a 17 px es una pastilla con unas motas.
            //
            //  Y VA INCLINADA, como se sostiene. No es un capricho: TUMBADA no
            //  se la puede distinguir de `pit` ni de `fla`, que son las otras
            //  dos cosas largas de la tabla que viven sobre el renglon del
            //  medio. Medido, con el tubo horizontal: 0.330 de `pit` con las
            //  llaves pegadas al tubo y 0.335 con las llaves colgando de una
            //  varilla -desenfocadas, cuatro bolas sobre una linea son una
            //  onda- y 0.334 de `fla`. Los dos peines y la onda apretada son
            //  horizontales por definicion; una flauta no, asi que la que se
            //  mueve es la flauta.
            case Id::insFlauta:
            {
                juce::Path cuerpo, boca;
                cuerpo.addRoundedRectangle (3.4f, 11.6f, 16.6f, 3.0f, 1.5f);   // el tubo
                boca.addEllipse (0.8f, 9.8f, 5.4f, 6.2f);                      // la embocadura
                cuerpo.addRoundedRectangle (19.2f, 9.2f, 2.8f, 7.8f, 1.1f);    // el pie
                for (int i = 0; i < 4; ++i)                                    // y las llaves
                    cuerpo.addRoundedRectangle (6.6f + (float) i * 3.3f, 8.4f,
                                                2.8f, 3.4f, 0.9f);
                //  Y MAS INCLINADA: a 0.42 rad el tubo media 23 x 16 y el
                //  marco se quedaba a 0.67 por el lado corto. El giro es lo
                //  unico que llena los dos lados sin tocar la flauta.
                //
                //  Y HACIA ABAJO Y NO HACIA ARRIBA. Con -0.60 el tubo sube a la
                //  derecha y eso ya lo hay: `bit` es una escalera que sube a la
                //  derecha, y desenfocadas dos bandas diagonales en el mismo
                //  sentido son el mismo dibujo —0.3935, el segundo par nuevo de
                //  esta tanda—. Al otro lado llena el marco igual y no se cruza
                //  con nadie.
                const auto giro = juce::AffineTransform::rotation (0.60f, 12.0f, 12.0f);
                cuerpo.applyTransform (giro);
                boca.applyTransform (giro);
                R.addPath (cuerpo);
                L.addPath (boca);
            }
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

            //  FM: la PILA DE OPERADORES, que es lo que esta forma es. No hay
            //  instrumento que dibujar -nadie ha visto una FM- asi que se
            //  dibuja el algoritmo: dos cajas, una encima de otra, y el cable
            //  que baja de la de arriba a la de abajo. Es como se ha dibujado
            //  la FM desde que existe.
            //  LOS DOS OPERADORES, MAS ANCHOS: 11 de ancho por 21 de alto
            //  dejaban el marco a 0.58 por el lado corto.
            case Id::insFm:
                L.addRoundedRectangle (3.6f, 2.5f, 16.8f, 7.5f, 2.2f);
                linea (L, 12.0f, 10.0f, 12.0f, 13.5f);
                L.addRoundedRectangle (3.6f, 13.5f, 16.8f, 7.5f, 2.2f);
                R.addEllipse (10.7f, 5.0f, 2.6f, 2.6f);
                break;

            //  SYNC: la onda que se REINICIA. Tres rampas cortadas en seco, y
            //  la barra rellena de la izquierda es el maestro que las corta.
            //  Y va en RAMPAS y no en barras verticales a proposito: CLAVES ya
            //  es un peine de barras, y desenfocados serian el mismo dibujo -
            //  que es lo que la prueba de pares no perdona.
            case Id::insSync:
                L.startNewSubPath (5.0f, 20.5f);
                L.lineTo (10.5f, 4.5f);  L.lineTo (10.5f, 20.5f);
                L.lineTo (16.0f, 4.5f);  L.lineTo (16.0f, 20.5f);
                L.lineTo (21.5f, 4.5f);  L.lineTo (21.5f, 20.5f);
                bandaV (R, 2.9f, 4.0f, 16.5f);
                break;

            //  PIANOS: el de cola VISTO DESDE ARRIBA -el ala y el teclado- y no
            //  un teclado de frente, que es PIANO ELEC. Un instrumento no se
            //  reconoce por su teclado cuando hay otro al lado con el mismo.
            case Id::insPiano:
                L.startNewSubPath (3.0f, 21.0f);
                L.lineTo (3.0f, 8.0f);
                L.cubicTo (8.0f, 2.0f, 17.0f, 2.5f, 21.0f, 9.5f);
                L.lineTo (21.0f, 21.0f);
                L.closeSubPath();
                bandaH (R, 5.0f, 18.4f, 14.0f);
                break;

            //  ACORDEON: las dos cajas y el FUELLE en medio. El zigzag es todo
            //  el dibujo: no hay otro instrumento que se pliegue.
            case Id::insAcordeon:
                L.addRoundedRectangle (1.5f, 4.0f, 4.5f, 16.0f, 1.2f);
                L.addRoundedRectangle (18.0f, 4.0f, 4.5f, 16.0f, 1.2f);
                L.startNewSubPath (6.0f, 4.5f);
                L.lineTo (10.0f, 19.5f); L.lineTo (14.0f, 4.5f); L.lineTo (18.0f, 19.5f);
                R.addEllipse (19.6f, 7.0f, 1.8f, 1.8f);
                R.addEllipse (19.6f, 11.0f, 1.8f, 1.8f);
                break;

            //  SITAR: la calabaza MACIZA abajo a la izquierda, el mastil
            //  largo en diagonal con sus TRASTES, y el clavijero arriba con
            //  dos clavijas atravesadas. La diagonal es lo que lo separa de
            //  CUERDA PULS, que es una caja de frente; los trastes y las
            //  clavijas son lo que lo separa de un circulo con un palo.
            case Id::insSitar:
                R.addEllipse (1.5f, 11.5f, 11.0f, 10.5f);                  // la calabaza
                //  El mastil, de la calabaza al clavijero: dos cantos
                //  paralelos a trazo y no una banda maciza, que los trastes
                //  encima de un macizo no se ven - se pintan con la misma
                //  tinta.
                linea (L, 6.6f, 14.3f, 18.4f, 2.5f);
                linea (L, 8.7f, 16.4f, 20.5f, 4.6f);
                linea (L, 18.4f, 2.5f, 20.5f, 4.6f);                       // y el remate
                //  CUATRO TRASTES, perpendiculares al mastil.
                for (int i = 0; i < 4; ++i)
                {
                    const float u = 0.26f + (float) i * 0.15f;
                    const float cx = 7.65f + 11.8f * u, cy = 15.35f - 11.8f * u;
                    linea (L, cx - 1.15f, cy - 1.15f, cx + 1.15f, cy + 1.15f);
                }
                //  Y LAS DOS CLAVIJAS del clavijero, con su boton.
                linea (L, 15.3f, 3.8f, 19.3f, 7.8f);
                linea (L, 16.9f, 2.2f, 20.9f, 6.2f);
                R.addEllipse (18.4f, 6.9f, 2.4f, 2.4f);
                R.addEllipse (20.0f, 5.3f, 2.4f, 2.4f);
                break;

            //  CELLOS: la caja de ocho CON LO QUE LLEVA DENTRO - las dos efes y
            //  el puente - el mastil con su cabeza, y EL ARCO cruzandola, con
            //  su vara, su crin y el talon. El arco es lo que dice que esto se
            //  toca frotando y no pulsando.
            //
            //  Las efes y el puente van MACIZOS dentro de una caja de TRAZO:
            //  es la unica forma de que un detalle se vea cuando el dibujo
            //  entero se pinta con una sola tinta.
            case Id::insCello:
                L.startNewSubPath (12.0f, 5.2f);
                L.cubicTo (16.5f, 6.9f, 16.5f, 10.9f, 13.4f, 12.8f);
                L.cubicTo (18.4f, 14.3f, 18.4f, 21.3f, 12.0f, 21.6f);
                L.cubicTo (5.6f, 21.3f, 5.6f, 14.3f, 10.6f, 12.8f);
                L.cubicTo (7.5f, 10.9f, 7.5f, 6.9f, 12.0f, 5.2f);
                L.closeSubPath();
                bandaV (R,  9.85f, 13.4f, 4.4f);                           // las dos efes
                bandaV (R, 14.15f, 13.4f, 4.4f);
                bandaH (R, 10.1f, 18.75f, 3.8f);                           // el puente
                bandaV (R, 12.0f, 2.6f, 2.8f);                             // el mastil
                bandaH (R, 10.5f, 1.95f, 3.0f);                            // y su cabeza
                //  EL ARCO: la vara y la crin, que son dos y no una.
                linea (L, 1.6f, 18.4f, 21.6f, 5.6f);
                linea (L, 2.4f, 19.8f, 22.4f, 7.0f);
                R.addRoundedRectangle (1.2f, 18.0f, 2.6f, 2.4f, 0.7f);     // y el talon
                break;

            //  CANAS: el oboe DE PIE - la cana doble arriba, el cuerpo conico
            //  macizo, las tres llaves saliendo por un lado con su varilla, y
            //  el pabellon abierto abajo. VIENTOS es un tubo tumbado; de pie,
            //  conico y con campana es otro dibujo, y ademas es como se
            //  sostiene un oboe.
            case Id::insCana:
                bandaV (R, 12.0f, 0.8f, 2.8f);                             // la cana
                R.startNewSubPath (10.4f, 3.6f);                           // el cuerpo, conico
                R.lineTo (13.6f, 3.6f); R.lineTo (14.4f, 16.0f);
                R.lineTo (9.6f, 16.0f); R.closeSubPath();
                //  Y EL PABELLON ABRE MAS: con la boca de 12.8 px el dibujo
                //  media 14 de ancho por 24 de alto, o sea 0.58 del marco por
                //  el lado corto.
                R.startNewSubPath (9.6f, 16.0f);                           // y el pabellon
                R.lineTo (14.4f, 16.0f); R.lineTo (20.8f, 22.2f);
                R.lineTo (3.2f, 22.2f); R.closeSubPath();
                for (int i = 0; i < 3; ++i)                                // las tres llaves
                {
                    const float y = 6.2f + (float) i * 3.4f;
                    linea (L, 13.9f, y, 16.4f, y);
                    R.addEllipse (16.0f, y - 1.3f, 2.6f, 2.6f);
                }
                break;

            //  TUBOS: los cinco tubos del organo, en MONTE y no en escalera.
            //  Rellenos y simetricos: CLAVES es un peine de barras de altura
            //  creciente dibujado a linea, y desenfocada una escalera de tubos
            //  seria el mismo icono.
            //
            //  Y SIN EL 0.90 DE MACIZO: los tubos iban a 2.8 px de ancho, mas
            //  de vez y media una linea de esta cara, y ese 0.90 era el
            //  descuento optico que se le hace a una mancha para que no pese el
            //  triple que un dibujo de trazo. Con los cinco a `kBanda` ya no hay
            //  mancha que descontar y el descuento solo lo dejaria pequeno.
            case Id::insTubo:
                bandaV (R,  3.8f, 11.5f,  9.0f);
                bandaV (R,  8.0f,  7.5f, 13.0f);
                bandaV (R, 12.2f,  3.5f, 17.0f);
                bandaV (R, 16.4f,  7.5f, 13.0f);
                bandaV (R, 20.6f, 11.5f,  9.0f);
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
    //  LA TABLA, A LA VISTA Y NO DENTRO DE LA FUNCION, para poder contarla.
    //
    //  EL LARGO SALE DE ELLA Y NO DE UN 16 ESCRITO DOS VECES. Antes habia un
    //  `t[16]` y un `familia < 16`: dos topes, y al subir las familias a
    //  veinticuatro el segundo habria devuelto `ninguno` para las ocho nuevas
    //  sin que nada fallara -ocho pads con el dibujo vacio, que ninguna regla
    //  del banco mira-. Un tope escrito dos veces son dos topes.
    inline constexpr Id kDibujoDeFamilia[] =
    {
        Id::insBajo, Id::insSub, Id::insEp, Id::insOrgano,
        Id::insCuerdas, Id::insColchon, Id::insPluck, Id::insCampana,
        Id::insMetales, Id::insLead, Id::insCoro, Id::insGuitarra,
        Id::insMazo, Id::insClav, Id::insFlauta, Id::insArpa,
        Id::insFm, Id::insSync, Id::insPiano, Id::insAcordeon,
        Id::insSitar, Id::insCello, Id::insCana, Id::insTubo
    };

    //  Y SE PUBLICA CUANTAS SON, que es lo que permite que quien conoce los dos
    //  numeros -`MainComponent`, que incluye Sintes.h y esto- exija que sean el
    //  mismo EN TIEMPO DE COMPILACION. Aqui no se puede: Iconos no sabe nada de
    //  Sintes, y no tiene por que saberlo.
    inline constexpr int kFamiliasConDibujo =
        (int) (sizeof (kDibujoDeFamilia) / sizeof (kDibujoDeFamilia[0]));

    inline Id deFamilia (int familia) noexcept
    {
        return juce::isPositiveAndBelow (familia, kFamiliasConDibujo)
                 ? kDibujoDeFamilia[familia] : Id::ninguno;
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
        //  EL DOBLE DEL GROSOR DE LA CASA, que es lo que hace que la Z de la
        //  marca sea de trazo GORDO como el nombre en la cabecera. Decia "el
        //  mismo grosor que los otros ochenta y seis" y llevaba 24 * 0.086:
        //  ese 0.086 era el de `grosorPara` de antes, y cuando `grosorPara`
        //  bajo a 0.072 este numero se quedo donde estaba. Ahora sale de
        //  `kBanda` y no se puede volver a quedar atras.
        juce::PathStrokeType (kBanda * 2.0f,
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

    //  LA CAJA DE LO QUE SE PINTA, QUE NO ES LA QUE DECLARA EL CAMINO.
    //
    //  `Path::getBounds` mete los PUNTOS DE CONTROL, y un tirador de una curva
    //  no se pinta: una cubica con los dos tiradores a 11 px del eje pinta 8.3.
    //  Mientras eso solo servia para decir "cabe" sobraba por el lado bueno;
    //  desde que `dibuja` escala por esa caja para que todos llenen el mismo
    //  marco es un error que se VE, y se midio: `cho` —dos ondas— se pintaba en
    //  22 x 16 dentro de un marco de 24, con cuatro filas vacias arriba y
    //  cuatro abajo, y lo mismo le pasaba a cada icono con una curva. Un icono
    //  de rectangulos llenaba su marco y el de al lado no, que es justo lo que
    //  esta regla existe para que no pase.
    //
    //  Asi que se APLANA el camino y se mide lo que queda. Es lo mismo que hace
    //  el rasterizador, o sea lo que el ojo ve.
    inline juce::Rectangle<float> cajaPintada (const juce::Path& p)
    {
        if (p.isEmpty()) return {};

        juce::PathFlatteningIterator it (p);
        float x0 = 1.0e9f, y0 = 1.0e9f, x1 = -1.0e9f, y1 = -1.0e9f;
        while (it.next())
        {
            x0 = juce::jmin (x0, it.x1, it.x2);  x1 = juce::jmax (x1, it.x1, it.x2);
            y0 = juce::jmin (y0, it.y1, it.y2);  y1 = juce::jmax (y1, it.y1, it.y2);
        }
        if (x1 < x0 || y1 < y0) return p.getBounds();
        return { x0, y0, x1 - x0, y1 - y0 };
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

    //  UN TRAZO CUALQUIERA CON LA MANO DE LA CASA: la misma caja, el mismo
    //  llenado y el mismo grosor que los dibujos de la tabla. Es lo que pinta
    //  tambien el icono que se dibuja a dedo -ver `aIcono`-, y por eso esta
    //  aparte: un dibujo de la persona y uno de la tabla, en la misma fila, se
    //  leen de la misma mano porque los pinta la misma funcion.
    inline void dibujaTrazo (juce::Graphics& g, Trazo t, juce::Rectangle<float> caja, juce::Colour c)
    {
        //  Cuadrada y centrada: un icono estirado a la caja que le toque sale
        //  con la rueda de AJUSTES ovalada y las tijeras torcidas.
        const float lado = juce::jmin (caja.getWidth(), caja.getHeight());
        if (lado < 4.0f) return;
        caja = caja.withSizeKeepingCentre (lado, lado);

        //  TODOS OCUPAN LA MISMA CAJA.
        //
        //  Cada dibujo se escribia dentro de la rejilla de 24 con el sitio que
        //  le pedia su forma, asi que unos llenaban el 100% y otros el 79%: en
        //  una fila se leen como si el pequeno estuviera mas lejos. Se mide lo
        //  que el trazo ocupa DE VERDAD -con su grosor- y se escala para que
        //  llene el mismo blanco, centrado. Uniforme, o sea sin deformar: un
        //  icono ancho y bajo sigue siendo ancho y bajo, solo que llenando.
        //
        //  Y ESO ES LA MITAD DEL MARCO. Un escalado uniforme lleva el lado
        //  LARGO al 96 % y deja el corto donde lo ponga la PROPORCION del
        //  dibujo, asi que dos iconos con la misma caja pueden ocupar blancos
        //  muy distintos: medido sobre los 140, el lado corto tenia la mediana
        //  en 0.83 de la caja y treinta y siete iconos por debajo de 0.70, con
        //  `fla` y `pit` en 0.42 —una banda de diez pixeles de alto en una caja
        //  de veinticuatro—. Aqui no se puede arreglar: estirar el lado corto
        //  hasta el marco es deformar, y deformar saca la rueda de AJUSTES
        //  ovalada y las tijeras torcidas. Se arregla en el DIBUJO, que es
        //  quien decide su proporcion, y lo vigila `Tests/iconos.py` con el
        //  mismo liston aplicado a los dos lados.
        const float m = grosorPara (24.0f) * 0.5f;
        auto lim = t.relleno.isEmpty() ? juce::Rectangle<float>() : cajaPintada (t.relleno);
        if (! t.linea.isEmpty())
        {
            const auto lb = cajaPintada (t.linea).expanded (m);
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

    inline void dibuja (juce::Graphics& g, Id id, juce::Rectangle<float> caja, juce::Colour c)
    {
        if (id == Id::ninguno || id == Id::kNum) return;
        dibujaTrazo (g, trazo (id), caja, c);
    }

    //  EL DIBUJO A DEDO, PASADO A ICONO. Del telefono: «un marco para dibujar
    //  el icono que tu quieras y que se transforme». Lo que se transforma es la
    //  MANO: un dedo tiembla, se pasa y deja esquinas donde queria curvas, y
    //  ese temblor es lo que separa un garabato de un icono. Asi que cada trazo
    //  se SIMPLIFICA -Ramer-Douglas-Peucker, al 3 % del dibujo: el temblor se
    //  va y las esquinas que se querian se quedan-, se SUAVIZA -dos pasadas de
    //  Chaikin, que redondean sin salirse del trazo-, se CIERRA si acaba donde
    //  empezo, y el conjunto se encaja en la rejilla de 24 de la tabla. Lo
    //  demas -caja, llenado y grosor- lo pone `dibujaTrazo`, como a los otros.
    using Puntos = std::vector<juce::Point<float>>;

    inline void simplifica (const Puntos& p, size_t a, size_t b, float eps, std::vector<bool>& queda)
    {
        if (b <= a + 1) return;
        const juce::Line<float> recta (p[a], p[b]);
        float peor = -1.0f; size_t cual = a;
        for (size_t i = a + 1; i < b; ++i)
        {
            const float d = recta.getLength() < 1.0e-4f ? p[i].getDistanceFrom (p[a])
                                                       : [&] { juce::Point<float> cerca; return recta.getDistanceFromPoint (p[i], cerca); }();
            if (d > peor) { peor = d; cual = i; }
        }
        if (peor <= eps) return;
        queda[cual] = true;
        simplifica (p, a, cual, eps, queda);
        simplifica (p, cual, b, eps, queda);
    }

    inline juce::Path aIcono (const std::vector<Puntos>& trazos)
    {
        float x0 = 1.0e9f, y0 = 1.0e9f, x1 = -1.0e9f, y1 = -1.0e9f;
        for (const auto& t : trazos)
            for (const auto& q : t)
            {
                x0 = juce::jmin (x0, q.x); y0 = juce::jmin (y0, q.y);
                x1 = juce::jmax (x1, q.x); y1 = juce::jmax (y1, q.y);
            }
        const auto caja = x1 < x0 ? juce::Rectangle<float>() : juce::Rectangle<float>::leftTopRightBottom (x0, y0, x1, y1);
        const float tam = juce::jmax (caja.getWidth(), caja.getHeight(), 1.0f);

        juce::Path out;
        for (const auto& t : trazos)
        {
            if (t.empty()) continue;
            Puntos p = t;
            if (p.size() > 2)
            {
                std::vector<bool> queda (p.size(), false);
                queda.front() = queda.back() = true;
                simplifica (p, 0, p.size() - 1, tam * 0.03f, queda);
                Puntos s;
                for (size_t i = 0; i < p.size(); ++i) if (queda[i]) s.push_back (p[i]);
                p = s;
            }
            //  Un toque sin arrastre es un PUNTO, y un punto se dibuja: la
            //  raya de una centesima que lo lleva sale redonda por la punta.
            if (p.size() == 1) { out.startNewSubPath (p[0]); out.lineTo (p[0].translated (0.01f, 0.0f)); continue; }

            //  Y SE CIERRA si el final cae cerca del principio: un dedo que da
            //  la vuelta a un circulo lo acaba a un quinto del dibujo del
            //  arranque, no encima (medido en la auditoria, bloque 22).
            const bool cerrado = p.size() > 2 && p.front().getDistanceFrom (p.back()) < tam * 0.2f;
            if (cerrado) p.back() = p.front();
            for (int vuelta = 0; vuelta < 2 && p.size() > 2; ++vuelta)
            {
                Puntos c;
                if (! cerrado) c.push_back (p.front());
                for (size_t i = 0; i + 1 < p.size(); ++i)
                {
                    c.push_back (p[i] * 0.75f + p[i + 1] * 0.25f);
                    c.push_back (p[i] * 0.25f + p[i + 1] * 0.75f);
                }
                if (! cerrado) c.push_back (p.back());
                else           c.push_back (c.front());
                p = c;
            }
            out.startNewSubPath (p[0]);
            for (size_t i = 1; i < p.size(); ++i) out.lineTo (p[i]);
            if (cerrado) out.closeSubPath();
        }
        if (out.isEmpty()) return out;
        //  A la rejilla de 24, centrado y sin deformar, con el aire del grosor.
        const float lado = 24.0f - 2.0f * grosorPara (24.0f);
        out.applyTransform (out.getTransformToScaleToFit (juce::Rectangle<float> (12.0f - lado * 0.5f, 12.0f - lado * 0.5f, lado, lado), true));
        return out;
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
        if (! t.relleno.isEmpty()) { r = cajaPintada (t.relleno); hay = true; }
        if (! t.linea.isEmpty())
        {
            const auto lb = cajaPintada (t.linea).expanded (w);
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

// ============================================================================
//  LAS CIFRAS DIBUJADAS.
//
//  Del telefono: «en los botones donde hay numeros no sean numeros de fuente de
//  texto, sino que hagas unos sprites con numeros un poco mas currados. Las
//  letras si que sean de texto, pero los numeros no, para dar un toque diferente
//  a la app».
//
//  Y es una decision que se sostiene sola: en esta maquina un numero casi nunca
//  es una palabra. Es la chapa de un pad, el indice de un canal, el compas, la
//  octava — cosas que se leen de un vistazo en una rejilla, no que se leen
//  leyendo. Un juego propio los hace reconocibles a tamano pequeno y le pone
//  cara a la app, que es lo que se pidio.
//
//  TRES REGLAS, y las tres salen de lo que este proyecto ya pago con los 112
//  iconos:
//
//   1. **El ancho lo sigue diciendo la FUENTE.** Cada cifra se dibuja dentro de
//      la celda que la fuente le habria dado, asi que lo que `expo.py` mide con
//      `GlyphArrangement::getStringWidth` y lo que se pinta son el mismo numero.
//      Un juego con su propio avance serian dos reglas, y la que se quedara
//      vieja dejaria `TRUNC` y `SQUEEZE` midiendo un rotulo que no existe.
//
//   2. **Tabular por construccion.** Todas las celdas miden lo mismo, asi que
//      «09» y «10» ocupan igual y una columna de cifras no baila. Es la misma
//      razon por la que la fuente mono de esta casa es mono.
//
//   3. **Se escriben en una rejilla y no a mano**, como los iconos: diez de
//      ancho por catorce de alto, trazo con juntas redondeadas, y el grosor
//      derivado del cuerpo. Nada de aqui sabe a que tamano se va a pintar.
namespace Cifras
{
    //  La rejilla. Diez por catorce es la proporcion de un digito de verdad
    //  -mas alto que ancho, y no cuadrado como un icono- y deja el hueco
    //  entre cifras fuera del glifo, que es donde la celda lo pone.
    inline constexpr float kW = 10.0f;
    inline constexpr float kH = 14.0f;

    //  Y EL SUELO, por lo mismo que `Iconos::kLadoMin`: por debajo de esto un
    //  trazo con antialias es una mancha y la fuente lee mejor. Quien dibuje
    //  pregunta antes; ver `ZatiLookAndFeel::drawButtonText`.
    inline constexpr float kAltoMin = 9.0f;

    //  EL TRAZO DE UNA CIFRA, en la rejilla de 10x14.
    //
    //  Geometricas y de un solo grosor: la app es un chasis acromatico con
    //  esquinas rectas, y una cifra con remates o con modulacion de grosor
    //  seria de otra maquina. Los ceros y los ochos se cierran con esquinas
    //  chaflanadas —ni redondas ni rectas— que es lo que las separa de una
    //  fuente cualquiera sin inventarse una forma que no se lea.
    inline juce::Path glifo (juce::juce_wchar c)
    {
        juce::Path p;
        const float x0 = 1.0f, x1 = kW - 1.0f, xm = kW * 0.5f;
        const float y0 = 1.0f, y1 = kH - 1.0f, ym = kH * 0.5f;
        const float ch = 2.2f;          // el chaflan de las esquinas

        auto caja = [&] (juce::Path& q)
        {
            //  Un rectangulo con las cuatro esquinas cortadas. Es la forma del
            //  chasis y la que hace que el 0 no se lea como una O.
            q.startNewSubPath (x0 + ch, y0);
            q.lineTo (x1 - ch, y0);  q.lineTo (x1, y0 + ch);
            q.lineTo (x1, y1 - ch);  q.lineTo (x1 - ch, y1);
            q.lineTo (x0 + ch, y1);  q.lineTo (x0, y1 - ch);
            q.lineTo (x0, y0 + ch);  q.closeSubPath();
        };

        switch (c)
        {
            case '0':
                caja (p);
                //  La barra: sin ella un 0 a trece pixeles es una D o una O.
                p.startNewSubPath (x0 + 1.4f, y1 - 1.4f);
                p.lineTo (x1 - 1.4f, y0 + 1.4f);
                break;

            case '1':
                //  Con base, que es lo que impide que a tamano pequeno se lea
                //  como el palo de un 4 o como una I.
                p.startNewSubPath (x0 + 0.6f, y0 + 3.0f);
                p.lineTo (xm, y0);
                p.lineTo (xm, y1);
                p.startNewSubPath (x0 + 0.8f, y1);
                p.lineTo (x1 - 0.8f, y1);
                break;

            case '2':
                p.startNewSubPath (x0, y0 + ch);
                p.lineTo (x0 + ch, y0);   p.lineTo (x1 - ch, y0);
                p.lineTo (x1, y0 + ch);   p.lineTo (x1, ym - 1.0f);
                p.lineTo (x0, y1);        p.lineTo (x1, y1);
                break;

            case '3':
                p.startNewSubPath (x0, y0);
                p.lineTo (x1 - ch, y0);   p.lineTo (x1, y0 + ch);
                p.lineTo (x1, ym - ch * 0.5f);
                p.lineTo (x1 - ch * 0.7f, ym);
                p.lineTo (x1, ym + ch * 0.5f);
                p.lineTo (x1, y1 - ch);   p.lineTo (x1 - ch, y1);
                p.lineTo (x0, y1);
                p.startNewSubPath (x0 + 1.6f, ym);
                p.lineTo (x1 - ch * 0.7f, ym);
                break;

            case '4':
                p.startNewSubPath (x1 - 2.0f, y0);
                p.lineTo (x0, y1 - 4.0f);
                p.lineTo (x1, y1 - 4.0f);
                p.startNewSubPath (x1 - 2.0f, y0);
                p.lineTo (x1 - 2.0f, y1);
                break;

            case '5':
                p.startNewSubPath (x1, y0);
                p.lineTo (x0, y0);        p.lineTo (x0, ym - 0.6f);
                p.lineTo (x1 - ch, ym - 0.6f);
                p.lineTo (x1, ym - 0.6f + ch);
                p.lineTo (x1, y1 - ch);   p.lineTo (x1 - ch, y1);
                p.lineTo (x0 + ch, y1);   p.lineTo (x0, y1 - ch);
                break;

            case '6':
                p.startNewSubPath (x1 - 0.6f, y0);
                p.lineTo (x0 + ch, y0);   p.lineTo (x0, y0 + ch);
                p.lineTo (x0, y1 - ch);   p.lineTo (x0 + ch, y1);
                p.lineTo (x1 - ch, y1);   p.lineTo (x1, y1 - ch);
                p.lineTo (x1, ym + 0.4f); p.lineTo (x1 - ch, ym - 0.6f);
                p.lineTo (x0, ym - 0.6f);
                break;

            case '7':
                p.startNewSubPath (x0, y0);
                p.lineTo (x1, y0);
                p.lineTo (x0 + 2.4f, y1);
                //  La travesana. En una rejilla es lo que separa el 7 del 1 de
                //  un vistazo, que es como se leen aqui.
                p.startNewSubPath (x0 + 1.4f, ym);
                p.lineTo (x1 - 1.8f, ym);
                break;

            case '8':
                caja (p);
                p.startNewSubPath (x0, ym - 0.6f);
                p.lineTo (x1, ym - 0.6f);
                break;

            case '9':
                p.startNewSubPath (x0 + 0.6f, y1);
                p.lineTo (x1 - ch, y1);   p.lineTo (x1, y1 - ch);
                p.lineTo (x1, y0 + ch);   p.lineTo (x1 - ch, y0);
                p.lineTo (x0 + ch, y0);   p.lineTo (x0, y0 + ch);
                p.lineTo (x0, ym - 0.4f); p.lineTo (x0 + ch, ym + 0.6f);
                p.lineTo (x1, ym + 0.6f);
                break;

            case '-':
                p.startNewSubPath (x0 + 0.5f, ym);
                p.lineTo (x1 - 0.5f, ym);
                break;

            default: break;
        }
        return p;
    }

    //  Si esta cadena se puede dibujar entera con el juego. Una sola letra y se
    //  pinta con la fuente: *entre una cifra dibujada y una palabra a medias no
    //  hay duda*, y mezclar los dos en un rotulo seria lo segundo.
    inline bool soloCifras (const juce::String& s)
    {
        if (s.isEmpty()) return false;
        for (auto c : s)
            if (! (juce::CharacterFunctions::isDigit (c) || c == '-'))
                return false;
        return true;
    }

    //  El grosor, derivado del alto como el de los iconos: un trazo fijo se ve
    //  gordo en la chapa de un pad y se pierde en un readout de 22 px.
    //
    //  Y DERIVADO CON LA MISMA CUENTA, que es lo que "como el de los iconos"
    //  tenia que querer decir. Era 0.115 del alto y un icono es 0.072 de su
    //  lado: un numero al lado de un icono del mismo tamano salia un SESENTA
    //  por ciento mas gordo, y numeros e iconos se ponen juntos en la misma
    //  fila por toda la cara -el readout de un mando, la cifra de un compas,
    //  el numero de un pad-. Medido en el raster de 24: la banda mas comun de
    //  una cifra era de 3 o 4 px y la de un icono de 2 o 3. Ahora las dos
    //  cuentas son la misma y el suelo tambien es el de `grosorPara`.
    inline float grosor (float alto) noexcept
    {
        return juce::jlimit (1.05f, 3.4f, alto * 0.072f);
    }

    //  DIBUJA `s` EN `r`, con el ancho que la FUENTE le habria dado.
    //
    //  El ancho se pide a `f` y no se inventa: ver la regla 1 de arriba. El
    //  alto sale de ese ancho y de la proporcion de la rejilla, y se acota al
    //  hueco, asi que una cifra en una caja baja encoge en vez de salirse.
    inline void dibuja (juce::Graphics& g, const juce::String& s,
                        juce::Rectangle<float> r, const juce::Font& f,
                        juce::Justification just, juce::Colour c)
    {
        if (s.isEmpty() || r.isEmpty()) return;

        const float anchoTexto = juce::GlyphArrangement::getStringWidth (f, s);
        if (anchoTexto <= 0.0f) return;
        const float celda = anchoTexto / (float) s.length();

        //  El glifo ocupa el 82% de su celda a lo ancho: el resto es el hueco
        //  entre cifras, que en una fuente vive dentro del avance y aqui
        //  tambien. Sin el, «11» sale pegado y se lee como una H.
        float alto = celda * 0.82f * (kH / kW);
        alto = juce::jmin (alto, r.getHeight());
        const float ancho = alto * (kW / kH) / 0.82f * (float) s.length();

        float x = r.getX();
        if (just.testFlags (juce::Justification::horizontallyCentred))
            x = r.getCentreX() - ancho * 0.5f;
        else if (just.testFlags (juce::Justification::right))
            x = r.getRight() - ancho;
        const float y = r.getCentreY() - alto * 0.5f;

        const float esc = alto / kH;
        const float paso = ancho / (float) s.length();
        const float dentro = (paso - kW * esc) * 0.5f;

        g.setColour (c);
        const juce::PathStrokeType trazo (grosor (alto),
                                          juce::PathStrokeType::curved,
                                          juce::PathStrokeType::rounded);
        for (int i = 0; i < s.length(); ++i)
        {
            auto p = glifo (s[i]);
            if (p.isEmpty()) continue;
            p.applyTransform (juce::AffineTransform::scale (esc)
                                .translated (x + (float) i * paso + dentro, y));
            g.strokePath (p, trazo);
        }
    }

    //  Y LAS CIFRAS EN UNA REJILLA DE ALFAS, que es lo que el banco compara.
    //  Mismo motivo que `Iconos::rasteriza`: lo que se juzga es lo que la app
    //  dibuja, no una idea de lo que dibuja.
    inline std::vector<juce::uint8> rasteriza (juce::juce_wchar c, int n)
    {
        juce::Image img (juce::Image::ARGB, n, n, true);
        {
            juce::Graphics g (img);
            auto p = glifo (c);
            if (! p.isEmpty())
            {
                const auto caja = juce::Rectangle<float> (0.0f, 0.0f, (float) n, (float) n);
                const float esc = juce::jmin (caja.getWidth() * 0.9f / kW,
                                              caja.getHeight() * 0.9f / kH);
                p.applyTransform (juce::AffineTransform::scale (esc)
                                    .translated (caja.getCentreX() - kW * esc * 0.5f,
                                                 caja.getCentreY() - kH * esc * 0.5f));
                g.setColour (juce::Colours::white);
                g.strokePath (p, juce::PathStrokeType (grosor ((float) n * 0.9f),
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
            }
        }

        std::vector<juce::uint8> px ((size_t) (n * n), 0);
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                px[(size_t) (y * n + x)] = bd.getPixelColour (x, y).getAlpha();
        return px;
    }
}
