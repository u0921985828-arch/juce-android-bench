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
    static constexpr int kCapas    = 2;    // suave / fuerte
    static constexpr int kZonas    = kRaices * kCapas;

    static_assert (kZonas <= SampleBuffer::kMaxZonas, "no caben las zonas");

    //  DO3 = 130.81 Hz como raiz central. Con +-24 semitonos el instrumento va
    //  de DO1 (32.7) a DO5 (523.2): de un sub a una linea de lead, que es lo
    //  que un pad tiene que cubrir sin cambiar de preset.
    static constexpr double kHzRaiz = 130.8127827;
    static constexpr int    kRaiz[kRaices] = { -24, -12, 0, 12, 24 };
    static constexpr int    kZonaRef = 2 * kCapas + 1;   // raiz 0, capa fuerte

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

    //  Sintetiza un preset entero -diez zonas- en un solo buffer con su tabla.
    //  HILO DE FONDO: reserva memoria y tarda. Nunca desde el de audio.
    SampleBuffer::Ptr sintetiza (int familia, int preset);
}
