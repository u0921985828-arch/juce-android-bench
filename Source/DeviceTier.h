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

        //  El latido de la interfaz: medidores, osciloscopio, destellos de los
        //  pads y cabezal de tiempo, todo repintando. Lo segundo mas caro de la
        //  app despues de las voces.
        int uiIntervalMs = 60;

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
