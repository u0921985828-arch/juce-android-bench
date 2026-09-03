#pragma once

#include <JuceHeader.h>

// ============================================================================
//  DeviceTier - una respuesta honesta a "que puede hacer este telefono", hecha
//  una vez en el arranque, y un punado de numeros sacados de ella.
//
//  La app estaba escrita para UNA maquina: cuarenta y ocho voces, latido de
//  interfaz de sesenta milisegundos, un minuto de grabacion en estereo, un
//  cuarto de giga de presupuesto por muestra. En el movil donde se escribio eso
//  esta bien. En uno de cuatro nucleos y 3 GB es un tartamudeo, y en uno de
//  gama alta es dejarse medio instrumento sin usar.
//
//  Asi que las constantes pasan a ser un perfil. Nada de esto es un ajuste que
//  haya que ir a buscar: el aparato se clasifica con lo que el sistema cuenta
//  de si mismo, y todo numero que cueste CPU o memoria se lee del resultado.
//
//  Lo que decide la gama, por lo que de verdad predice cada cosa: los NUCLEOS
//  -el hilo de audio quiere uno para el solo, el de interfaz otro, y el
//  cargador de muestras y el escritor de sesion un tercero, y cuatro es el
//  suelo en el que dejan de pelearse-; la MEMORIA, que es toda la historia de
//  esta app - un minuto de estereo en float son 23 MB, y un break troceado se
//  guarda una vez y se apunta dieciseis - y en 3 GB el presupuesto tiene que
//  ser lo bastante pequeno como para que el sistema no tenga motivo para
//  matarnos; y la FRECUENCIA solo para desempatar, porque Android la informa
//  mal y no dice nada del reparto entre nucleos grandes y pequenos.
//
//  Lo que NO se usa, a proposito: el nombre del modelo. Una tabla de telefonos
//  es una mentira que envejece mal y que hace falta publicar para arreglar.
// ============================================================================
namespace DeviceTier
{
    enum class Tier { low = 0, mid, high, ultra };

    struct Profile
    {
        Tier tier = Tier::mid;

        //  El deposito de voces. Cada una es una interpolacion de Hermite y, en
        //  modo TONO, dos granos solapados: lo mas caro que hace el hilo de audio.
        int voices = 32;

        //  Cuantas voces puede acaparar un pad antes de robarse a si mismo. Sube
        //  con el deposito, para que un pad mantenido no deje secos a los demas.
        int voicesPerPad = 8;

        //  EL LATIDO DEL RELOJ, Y EL SUELO DEL REPINTADO — que son dos cosas
        //  y este numero era las dos a la vez.
        //
        //  Se llamaba `uiIntervalMs` y era «cada cuanto se repinta la cara»:
        //  100, 60, 40 o 33 ms segun la gama, o sea **10, 16.7, 25 y 30
        //  fotogramas por segundo**. Y la gama sale del mismo `classify()` que
        //  el deposito de voces y el techo de muestra — nucleos y RAM — que
        //  para esos dos es el eje correcto y para un REPINTADO no lo es: un
        //  telefono de ocho nucleos con 6 GB caia a `mid` por un giga y movia
        //  la interfaz entera a 16.7 fps. La RAM no dice nada de lo rapido que
        //  refresca un panel.
        //
        //  Desde hoy el repintado cuelga del VBLANK -Choreographer en Android,
        //  o sea 60, 90 o 120 segun el panel- y este numero es dos cosas mas
        //  humildes: cada cuanto late el reloj de MANTENIMIENTO -los seis
        //  vigilantes, que tienen que correr con la pantalla apagada, donde no
        //  hay vblank- y **el suelo**: lo mas lento a lo que se permite caer el
        //  dibujo cuando el aparato no llega y hay que saltar vblanks.
        int relojMs = 60;

        //  Cuantas muestras del master entran en el osciloscopio. Menos puntos son
        //  menos columnas que recorrer y que rellenar.
        int scopePoints = 1024;

        //  La toma de microfono: segundos, y si se intenta estereo siquiera.
        double recordSeconds = 60.0;
        bool   recordStereo = true;

        //  Techo de una muestra decodificada, en megas. A un telefono que no la
        //  aguanta le sale mejor rechazar el fichero con un mensaje que morir a
        //  manos del sistema a mitad de la decodificacion.
        int sampleBudgetMB = 192;

        //  Multiplicador sobre el burst mas pequeno del driver. Uno es el camino
        //  rapido; un aparato que no renderiza un bloque a tiempo produce un
        //  under-run, y un under-run es un chasquido, que es peor que los
        //  milisegundos de mas. Punto de partida, no respuesta: ver checkXRuns.
        int bufferBursts = 1;

        //  Si las tapas de los pads dibujan su onda. Dieciseis envolventes
        //  repintadas en cada destello es trabajo de verdad sin GPU.
        bool padWaveformArt = true;
    };

    //  Se clasifica una vez; las siguientes llamadas devuelven lo mismo.
    const Profile& profile();

    //  Para la ficha de AJUSTES: "4 nucleos - 3.7 GB - media".
    juce::String describe();
    juce::String tierName (Tier t);
}
