#pragma once

#include <JuceHeader.h>
#include "Kits.h"
#include "SampleBuffer.h"

// ============================================================================
//  Sintes — dieciseis instrumentos de dieciseis presets, para UN pad.
//
//  UN PAD SE CONVIERTE EN UN INSTRUMENTO. Hasta aqui un pad era un golpe: una
//  muestra que suena entera cada vez que se toca, y el PIANO ROLL -que es de un
//  pad y lleva el tono en el eje vertical- escribia notas sobre ese golpe. Con
//  una caja de 200 ms eso no es tocar un instrumento, es pitchear una muestra:
//  dos octavas arriba son cuatro veces la velocidad de lectura, o sea una
//  ardilla, y la nota se acaba cuando se acaba el fichero, no cuando la
//  sueltas.
//
//  Aqui un pad lleva un instrumento de verdad, y lo que lo hace de verdad son
//  dos cosas que no se ven en una captura:
//
//  MULTIZONA. Cada preset se sintetiza a CINCO raices, una por octava, y al
//  tocar una nota se elige la mas cercana: como mucho seis semitonos de
//  estiramiento en vez de veinticuatro. Cinco y no una porque setStepNote acota
//  en +-24 y con raices en -24,-12,0,+12,+24 el rango entero queda embaldosado
//  sin huecos ni sobras - exactamente la misma cuenta que hizo que el piano
//  pasara de veinticinco filas a trece.
//
//  DOS CAPAS DE FUERZA. La capa fuerte no es la suave mas alta: abre el filtro
//  y mete mas parciales. Una capa que solo sube el volumen es un fader con
//  pasos, y por eso la prueba mide DOS numeros -cuanto cambia el nivel y cuanto
//  cambia el centroide- y no uno. Es la misma regla que ya costo una medida en
//  QUITAR RUIDO: un silenciador saca el primero y no hacer nada saca el segundo.
//
// ----------------------------------------------------------------------------
//  POR QUE SINTETIZADOS Y NO GRABADOS.
//
//  Un multisample de verdad -un piano por notas y capas- son cientos de MB y no
//  hay de donde sacarlo aqui. Es el mismo argumento que ya esta escrito para
//  los iconos y para la mitad sintetizada de la fabrica: se dibuja/se genera
//  porque cuesta cero bytes de instalacion y porque es NUESTRO. Lo que se puede
//  prometer es lo que la persona pidio: no un Kontakt, pero con calidad.
//
//  Y SE SINTETIZAN AL PONERLOS EN UN PAD, no al arrancar. Los 64 de fabrica se
//  generan en el arranque porque son 64 golpes cortos; esto son 256 presets de
//  diez zonas cada uno, o sea dos ordenes de magnitud mas. Se genera el que se
//  usa, en el hilo del cargador y jamas en el de audio.
//
// ----------------------------------------------------------------------------
//  EL BUCLE, Y POR QUE LLEVA FUNDIDO CRUZADO Y NO CICLOS ENTEROS.
//
//  Lo primero que sale es cerrar el bucle en un numero entero de ciclos de la
//  raiz, y para un solo oscilador es exacto y gratis. No vale aqui: la mitad de
//  estas familias llevan varios osciladores DESAFINADOS entre si -que es lo que
//  hace que unas cuerdas suenen a cuerdas y no a un zumbido- y un conjunto
//  desafinado no tiene periodo comun. Forzarlo obligaria a cuantizar cada
//  desafinacion a multiplos de la fundamental del bucle, o sea a quitar
//  exactamente lo que se estaba buscando.
//
//  Asi que se rinde material de mas por detras del final y se cruza sobre el
//  principio del bucle: en la costura las dos mitades son la MISMA muestra, asi
//  que la union es continua por construccion. Lineal y no de potencia
//  constante, que el material a los dos lados esta correlacionado y el de
//  potencia constante le meteria un bulto de +3 dB.
// ============================================================================
namespace Sintes
{
    static constexpr int kFamilias = 16;
    static constexpr int kPresets  = 16;
    static constexpr int kRaices   = 5;    // -24 -12 0 +12 +24
    //  TRES CAPAS DE FUERZA Y NO DOS, y el numero que lo decide es el salto.
    //
    //  Con dos, el motor cambia de capa a mitad de recorrido y el salto medido
    //  es de **2.0 dB y x1.12 de agudos de golpe**: una rampa de fuerza suena
    //  a escalon, que es exactamente lo que un instrumento no puede hacer. Con
    //  tres entre los MISMOS extremos cada escalon vale ~1.0 dB y x1.06, por
    //  debajo del JND de sonoridad.
    //
    //  Se paga en memoria y en tiempo de sintesis -x1.5- y CERO en el hilo de
    //  audio, que es el unico presupuesto que no se puede gastar. Por eso no se
    //  hace lo otro que sale: mezclar dos capas en la voz serian cuatro
    //  `hermite4` por muestra y por voz, y este motor tiene sesenta y cuatro.
    //
    //  Quince zonas de las dieciseis que `kMaxZonas` permite. La dieciseisava
    //  no se gasta.
    static constexpr int kCapas    = 3;    // suave / media / fuerte
    static constexpr int kZonas    = kRaices * kCapas;

    static_assert (kZonas <= SampleBuffer::kMaxZonas, "no caben las zonas");

    //  DO3 = 130.81 Hz como raiz central. Con +-24 semitonos el instrumento va
    //  de DO1 (32.7) a DO5 (523.2): de un sub a una linea de lead, que es lo
    //  que un pad tiene que cubrir sin cambiar de preset.
    static constexpr double kHzRaiz = 130.8127827;
    static constexpr int    kRaiz[kRaices] = { -24, -12, 0, 12, 24 };
    static constexpr int    kZonaRef = 2 * kCapas + (kCapas - 1);   // raiz 0, capa fuerte

    //  Las dieciseis formas. No son dieciseis juegos de parametros sobre el
    //  mismo oscilador: son dieciseis ALGORITMOS. Con un solo motor y los
    //  numeros movidos pasaria lo que ya paso con los bancos A y B de la
    //  fabrica -seis sonidos que eran literalmente el mismo generador- y la
    //  prueba de pares lo cazaria igual que lo cazo alli.
    enum Forma
    {
        fBajo, fSub, fEp, fOrgano, fCuerdas, fColchon, fPluck, fCampana,
        fMetales, fLead, fCoro, fGuitarra, fMazo, fClav, fFlauta, fArpa
    };

    struct Preset
    {
        const char* nombre;
        float p1, p2, p3, p4;   // lo que significan lo dice cada forma
        float atk, dec, rel;    // segundos
        float brillo;           // multiplica el corte del filtro
    };

    struct Familia
    {
        const char* nombre;
        Forma       forma;
        //  SOSTIENE o no. Un organo dura lo que aguantes la nota y un
        //  marimbeo se apaga solo: el primero lleva bucle y el segundo no, y
        //  ponerle bucle a lo segundo seria una nota que no acaba nunca.
        bool        sostiene;
        Preset      p[kPresets];
    };

    const Familia* tabla();          // 16 familias
    juce::String   nombreDe (int familia, int preset);

    // ------------------------------------------------------------------------
    //  LAS DIECISEIS, POR TIPO.
    //
    //  La tabla esta en el orden en que se fueron escribiendo las formas, que
    //  es el orden de quien las hizo y no el de quien las busca: BAJOS, SUBS,
    //  PIANO ELEC, ORGANOS, CUERDAS... Un menu de dieciseis nombres sin agrupar
    //  se lee entero cada vez, porque no hay forma de saltarse la mitad.
    //
    //  CUATRO GRUPOS DE CUATRO, y el reparto sale redondo sin inventarse nada:
    //  las dieciseis familias se reparten cuatro y cuatro por como SUENAN -por
    //  la fuente, no por el registro-, asi que ningun grupo hay que rellenarlo
    //  y ninguno sobra. Si hubiera salido 5-4-4-3 la respuesta seria escribir
    //  la familia que falta, no apretar el reparto hasta que cuadre.
    //
    //  El orden de la TABLA no se toca: la familia se guarda por su indice en
    //  proyectos y sesiones, y reordenarla cambiaria el sonido de todo lo
    //  guardado. Lo que se ordena es como se ENSEÑA.
    static constexpr int kCategorias = 4;
    const char* const* categorias();                 // 4 nombres, para T()
    int         categoriaDe (int familia);           // 0..3
    //  Las dieciseis en orden de menu: primero las cuatro de la categoria 0,
    //  etc. Devuelve indices de la tabla.
    const int*  ordenDeMenu();                       // 16

    //  LO QUE LA GAMA DEL APARATO SE PUEDE PERMITIR, y lo que NO puede cambiar.
    //
    //  Un proyecto tiene que sonar **peor, no distinto**, en un telefono flojo:
    //  el mapa de zonas, las raices, las capas y la afinacion en cents son
    //  identicos en las cuatro gamas. Lo unico que esto mueve son los canales y
    //  el largo del cuerpo del bucle, que es memoria y no musica. Si moviera una
    //  zona, la misma cancion abierta en otro aparato tocaria otras notas.
    //
    //  Va POR ARGUMENTO y con su valor puesto, no por un ajuste global que haya
    //  que acordarse de poner antes: quien no diga nada se lleva la calidad
    //  entera, que es el fallo seguro de los dos. `DeviceTier` es del lado de la
    //  app y `Sintes` no lo conoce -el banco no enlaza ese fichero-, asi que el
    //  que sabe en que aparato esta es quien llama.
    struct Gama
    {
        bool   estereo   = true;
        double cuerpoSeg = 1.00;
    };

    //  Sintetiza un preset entero -quince zonas- en un solo buffer con su tabla.
    //  HILO DE FONDO: reserva memoria y tarda. Nunca desde el de audio.
    SampleBuffer::Ptr sintetiza (int familia, int preset, Gama g = {});

    // ------------------------------------------------------------------------
    //  Y UNA RECETA SE PUEDE MOVER, que es lo que separa «elegir un sonido» de
    //  «tener un instrumento».
    //
    //  Los dieciseis por dieciseis eran una tabla de SOLO LECTURA: la ficha del
    //  pad podia pasar de un preset al siguiente y no habia una sola forma de
    //  tocar ninguno. Un instrumento que no se toca es un sample con nombre.
    //
    //  Los ocho numeros son los que ya tenia un `Preset`: los CUATRO de la
    //  forma -que significan cosas distintas en cada una de las dieciseis, y
    //  eso es justo lo que las hace dieciseis instrumentos y no uno con los
    //  numeros movidos- mas ataque, caida, suelta y brillo, que significan lo
    //  mismo en todas.
    static constexpr int kMandos = 8;

    //  EL RECORRIDO SALE DE LA TABLA Y NO SE ESCRIBE.
    //
    //  Escrito a mano serian dos reglas -la tabla y el rango- y la que se
    //  quedara vieja dejaria un mando que llega donde la forma no admite, o
    //  uno que no llega a un preset que si existe. Derivado, un preset nuevo
    //  que se salga ensancha su mando solo.
    //
    //  Y en DOS poblaciones y no una, que es una decision y no un descuido:
    //
    //   · los cuatro de FORMA, del rango de las dieciseis filas de SU familia.
    //     Su significado es de la forma -«razon del modulador» no es «mas» de
    //     nada fuera de lo que esa forma admite- asi que la poblacion que lo
    //     define son sus propios presets.
    //   · ataque, caida, suelta y brillo, del rango de las 256. Ahi el limite
    //     es MUSICAL y no de la forma: un bajo con dos segundos de ataque es un
    //     bajo con dos segundos de ataque, y negarselo seria inventarse una
    //     regla que la tabla no dice.
    struct Rango { float lo, hi; };
    Rango rango (int familia, int i);

    //  COMO SE LLAMA EL MANDO i DE ESA FAMILIA. La CLAVE, sin traducir: quien
    //  traduce es `T()` en la cara, igual que los `param[]` de un efecto. Los
    //  cuatro primeros los dice la forma; los cuatro de atras son los mismos en
    //  las dieciseis.
    const char* mando (int familia, int i);

    //  LOS OCHO POR INDICE, que es lo que hace que la ficha no tenga que
    //  escribir ocho ramas -y que el fichero de proyecto los guarde en un
    //  bucle-.
    float valor    (const Preset& r, int i);
    void  ponValor (Preset& r, int i, float v);

    //  LA PUERTA ACOTADA. La receta llega de la cara y del `project.xml`, que
    //  puede estar corrupto o ser de otra epoca, asi que se acota DONDE SE
    //  ENTRA y no en quien llama - la misma regla que los seis parametros de un
    //  pad y que `setSongCell`.
    Preset acota (int familia, const Preset& r);

    //  Y la puerta de verdad: rinde LA RECETA QUE SE LE DE. `preset` se queda
    //  para decir de que fila salio, que es lo que el fichero guarda y lo que
    //  el interruptor de presets mueve.
    SampleBuffer::Ptr sintetiza (int familia, int preset, const Preset& receta, Gama g = {});

    // ------------------------------------------------------------------------
    //  PARA EL BANCO: una zona suelta, al factor de sobremuestreo que se pida.
    //
    //  Existe para que la regla del pliegue pueda comparar **el producto contra
    //  si mismo**: la misma zona rendida a 4x -lo que se entrega- y a 16x -el
    //  patron-, y lo que el primero tiene de mas en una banda es lo que se
    //  plego. Sin esto habria que medir «energia que no esta en k*f0», y eso
    //  marcaria a CAMPANAS, MAZOS y ARPAS, que son inarmonicos A PROPOSITO:
    //  obligaria a repetir en Python una lista de familias armonicas que ya vive
    //  en la tabla, y *una regla duplicada que no se contrasta son dos reglas*.
    //
    //  Y es la MISMA funcion generadora con otro numero, no una rama aparte: una
    //  rama para el patron mediria la rama y no el producto.
    //
    //  Rinde el REGIMEN -se salta el ataque- para que lo que se compare sea el
    //  cuerpo y no la subida. HILO DE FONDO: reserva y tarda.
    void rindeZona (int familia, int preset, const Preset& receta,
                    int raiz, int capa, float* destino, int len, int os);

    //  PARA EL BANCO: a que velocidad va el LFO de esta zona DESPUES de cuadrarlo
    //  con el largo del bucle, y cuanto dura ese bucle en segundos.
    //
    //  Existe porque el cuadre del LFO no se puede deducir del audio sin volver a
    //  estimar la velocidad -y estimarla es otra regla, con su propio error-. Lo
    //  que la regla quiere comprobar es exactamente lo que el generador decidio:
    //  `lfoHz * bucleSeg` entero, o cero si se congelo.
    //
    //  Cero en `lfoHz` significa CONGELADO, que es una respuesta y no un fallo:
    //  un LFO que no da ni una vuelta dentro del cuerpo no es deriva, es una
    //  rampa, y una rampa en bucle es un diente de sierra a la cadencia del
    //  bucle. Ver `largoBucle`.
    void lfoDeZona (int familia, int preset, const Preset& receta, int raiz,
                    double cuerpoSeg, double* lfoHz, double* bucleSeg);
}
