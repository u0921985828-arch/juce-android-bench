#pragma once

#include <JuceHeader.h>
#include "Kits.h"
#include "Sintes.h"
#include "ProjectStore.h"

// ============================================================================
//  Instrumentos — el catalogo de contenido descargable.
//
//  LA MAQUINA VENIA CON SESENTA Y CUATRO SONIDOS Y NO HABIA FORMA DE METER MAS
//  QUE DE UNO EN UNO. Se podia buscar un fichero y ponerlo en un pad, o
//  repartir una carpeta que te hubieras montado tu por el banco de delante
//  (CARGAR KIT), y ya. Nada de eso es contenido: es gestion de ficheros. Lo
//  que faltaba es que la app SEPA que tiene dentro y lo enseñe.
//
//  Un INSTRUMENTO son dieciseis presets, o sea un banco entero. Esa es la
//  unidad y no otra, y sale de como esta hecha la maquina: la rejilla enseña
//  dieciseis pads, cambiar de banco cambia los dieciseis a la vez, y los
//  sesenta y cuatro de fabrica ya vienen en cuatro grupos de dieciseis con
//  nombre propio. Un instrumento es "lo que hay en la rejilla ahora mismo".
//
//  Un PACK son hasta dieciseis instrumentos. Dieciseis y no mas porque el menu
//  es una rejilla de cuatro por cuatro -la misma forma que la cara y que el
//  selector del RACK, asi que el 07 esta donde la mano ya sabe- y porque
//  pasado ese numero deja de ser un menu y es un directorio. Un directorio ya
//  lo tiene esta app: es el navegador, y esta a dos toques.
//
// ----------------------------------------------------------------------------
//  POR QUE UNA CARPETA DE CARPETAS Y NO UN FORMATO PROPIO.
//
//  Es la regla que ya se pago con los kits: "el kit que guardas y el pack que
//  te bajas entran por la MISMA puerta". Un pack es una carpeta con carpetas
//  de audios numerados, exactamente igual que un kit es una carpeta de audios
//  numerados. Asi un instrumento se puede montar a mano, mirar desde el
//  gestor de ficheros del telefono, copiar a otro aparato y arreglar cuando
//  algo salga mal. Un formato propio habria sido una segunda forma de hacer lo
//  mismo y ademas la unica que no sabria abrir nadie mas.
//
//      ZATI/Instrumentos/
//        <PACK>/
//          pack.txt              nombre y si es de pago
//          01 <INSTRUMENTO>/
//            01 <PRESET>.wav
//            ...  hasta 16
//          02 <INSTRUMENTO>/
//
//  CON LAS DOS CIFRAS DELANTE, que es lo mismo que costo una prueba en los
//  kits: sin ellas "10" se ordena antes que "2" y el pack vuelve barajado. El
//  numero ES el orden, y se le quita al nombre que se enseña.
//
// ----------------------------------------------------------------------------
//  LA FABRICA ES EL PRIMER PACK, Y NO SE ESCRIBE EN NINGUN SITIO.
//
//  Los cuatro bancos de fabrica son cuatro instrumentos de dieciseis presets:
//  ya lo eran, solo que no habia donde enseñarlos. Se listan desde
//  Kits::table() y no copiando 64 ficheros a ZATI/Instrumentos, que costaria
//  megas de instalacion para decir algo que la app ya sabe.
//
//  Y hace falta que este: un menu de contenido que abre VACIO -porque nadie ha
//  comprado nada todavia- se lee como una funcion rota, no como una tienda.
//  Con la fabrica dentro, el menu explica lo que es la primera vez que se abre
//  y ademas le da a FABRICA un sitio con sentido, que es de donde viene: la
//  tapa suelta que recargaba el banco era esta misma accion sin el resto.
//
// ----------------------------------------------------------------------------
//  LA LICENCIA NO VIVE DENTRO DEL PACK.
//
//  Lo primero que sale es poner un fichero en la carpeta del pack. Esta mal por
//  una razon que no es la falsificacion: un fichero ahi dentro VIAJA con la
//  carpeta, asi que copiar el pack a otro telefono copia tambien el derecho a
//  usarlo, y desinstalar la app lo pierde. Una licencia es de la PERSONA y del
//  APARATO, igual que el idioma y la carcasa, asi que vive donde ellos: en el
//  directorio interno de la app, un id por linea.
//
//  Un pack sin `pago=1` en su manifiesto esta abierto. Es a proposito: quien se
//  monte un pack a mano o se lo pase un amigo tiene que poder usarlo, y un
//  candado que no se puede abrir es peor que ningun candado. Lo que gatea es lo
//  que llega de la tienda diciendo que es de pago.
//
//  Quien ESCRIBE esa linea es la unica pieza que no esta aqui: la compra pasa
//  por la facturacion de la tienda, que necesita la cuenta de quien publica y
//  no se puede compilar ni medir desde este lado. `concede` es la costura, y
//  ZATI_DLC la usa para que el banco pueda medir las dos caras del candado.
// ============================================================================
namespace Instrumentos
{
    static constexpr int kMaxInstr = 16;   // por pack: la rejilla es de 4x4
    static constexpr int kPresets  = Kits::kPadsPerBank;   // 16 = un banco

    struct Instrumento
    {
        juce::String nombre;
        juce::File   carpeta;             // vacia si es de fabrica
        int          bancoFabrica = -1;   // 0..3 si es de fabrica, -1 si no
        int          presets      = 0;

        //  Y LA TERCERA CLASE, que es la que cambia a donde va esto.
        //
        //  Un instrumento de kit son dieciseis SONIDOS y va a los dieciseis
        //  pads de un banco: es una bateria. Un instrumento de Sintes es UN
        //  sonido tocable con dieciseis variantes y va a UN pad: es un
        //  instrumento. La misma rejilla sirve para los dos porque el gesto es
        //  el mismo -elegir-, pero el destino no puede serlo.
        int          familiaSintes = -1;  // 0..15 si lo sintetiza Sintes
    };

    struct Pack
    {
        juce::String id;                  // el nombre de la carpeta; "ZATI" el de dentro
        juce::String nombre;
        bool         dentro = false;      // el de fabrica, sin carpeta
        bool         dePago = false;
        bool         abierto = true;      // dePago y con licencia, o gratis
        std::vector<Instrumento> instr;
    };

    inline juce::File carpeta() { return ProjectStore::instrumentos(); }

    //  Donde se apunta lo comprado. Ver arriba: aqui y no dentro del pack.
    inline juce::File ficheroLicencias()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("zati-dlc.txt");
    }

    inline juce::StringArray licencias()
    {
        juce::StringArray ids;
        const auto f = ficheroLicencias();
        if (f.existsAsFile()) ids.addLines (f.loadFileAsString());
        ids.removeEmptyStrings();
        ids.trim();
        return ids;
    }

    //  LA COSTURA DE LA COMPRA. La llama quien confirme un pago -la
    //  facturacion de la tienda, que vive del lado de Java- y el banco por
    //  ZATI_DLC. Se anade y no se reescribe: perder una licencia porque otra
    //  se acaba de conceder seria el peor fallo posible de esta funcion.
    inline void concede (const juce::String& id)
    {
        if (id.isEmpty()) return;
        auto ids = licencias();
        if (ids.contains (id)) return;
        ids.add (id);

        //  Y SE ESCRIBE POR LA PUERTA QUE NO PIERDE EL FICHERO ANTERIOR.
        //
        //  El comentario de arriba dice que perder una licencia seria el peor
        //  fallo posible de esta funcion, y la implementacion no lo cubria: se
        //  reescribe la lista ENTERA, asi que un proceso muerto a mitad no
        //  pierde una licencia, las pierde TODAS. Y es el unico fichero de esta
        //  app cuya perdida no se puede rehacer desde la app - hay que volver a
        //  pasar por la facturacion de la tienda.
        //
        //  El validador es el que corresponde aqui: no basta con que la
        //  escritura no fallara, tienen que volver a leerse las mismas lineas.
        const auto texto = ids.joinIntoString ("\n");
        ProjectStore::escribeTexto (ficheroLicencias(), texto,
                                    [&ids] (const juce::File& f)
                                    {
                                        juce::StringArray vuelta;
                                        vuelta.addLines (f.loadFileAsString());
                                        vuelta.removeEmptyStrings();
                                        vuelta.trim();
                                        return vuelta == ids;
                                    });
    }

    namespace detalle
    {
        //  "03 RHODES" -> "RHODES". Las dos cifras son el ORDEN y no el
        //  nombre; enseñarlas seria enseñar el andamio.
        inline juce::String sinNumero (const juce::String& s)
        {
            auto t = s.trim();
            int i = 0;
            while (i < t.length() && juce::CharacterFunctions::isDigit (t[i])) ++i;
            if (i == 0) return t.toUpperCase();          // sin numero delante, tal cual
            const auto resto = t.substring (i).trimCharactersAtStart (" _-.").trim();
            //  "01" A SECAS ES UN NOMBRE, aunque sea malo. Quitarle el numero
            //  dejaria la tapa en blanco, y una tapa sin rotulo no se puede
            //  elegir - que es peor que una que se llama como un numero.
            return resto.isEmpty() ? t.toUpperCase() : resto.toUpperCase();
        }

        inline juce::String valorDe (const juce::String& texto, const juce::String& clave)
        {
            juce::StringArray lineas;
            lineas.addLines (texto);
            for (auto l : lineas)
            {
                l = l.trim();
                if (l.startsWithIgnoreCase (clave + "="))
                    return l.fromFirstOccurrenceOf ("=", false, false).trim();
            }
            return {};
        }
    }

    //  EL PACK DE DENTRO. Cuatro instrumentos, uno por banco, leidos de la
    //  tabla de fabrica. Cero bytes de instalacion y cero ficheros que mirar.
    inline Pack fabrica()
    {
        Pack p;
        p.id = "ZATI";
        p.nombre = "ZATI";
        p.dentro = true;
        p.abierto = true;
        for (int b = 0; b < Kits::kNumBanks; ++b)
            p.instr.push_back ({ juce::String (Kits::bankName (b)), {}, b, kPresets });
        return p;
    }

    //  Y EL PACK DE INSTRUMENTOS, que es el que va A UN PAD.
    //
    //  Dieciseis familias, y cada una con dieciseis presets: el mismo numero
    //  que tiene el banco, para que la rejilla del menu y la rejilla de pads
    //  sean la MISMA forma. El instrumento numero n vive siempre en el pad n
    //  del banco D, asi que el 07 esta donde la mano ya lo busca - la misma
    //  razon por la que el selector del RACK dejo de ser una fila de dieciseis.
    inline Pack sintes()
    {
        Pack p;
        p.id = "SINTES";
        p.nombre = "SINTES";
        p.dentro = true;
        p.abierto = true;
        for (int f = 0; f < Sintes::kFamilias; ++f)
            p.instr.push_back ({ juce::String (Sintes::tabla()[f].nombre), {}, -1,
                                 Sintes::kPresets, f });
        return p;
    }

    inline bool esAudio (const juce::File& f)
    {
        const auto e = f.getFileExtension().toLowerCase();
        return e == ".wav" || e == ".aif" || e == ".aiff" || e == ".flac"
            || e == ".mp3" || e == ".ogg";
    }

    //  Lee el catalogo entero: el de dentro primero y los de disco despues, por
    //  nombre. Cuesta un barrido de directorio, asi que lo llama quien ABRE la
    //  ficha y no quien la pinta - treinta barridos por segundo por una lista
    //  que no cambia es el mismo derroche que costo la CPU de la cara.
    inline std::vector<Pack> lee()
    {
        std::vector<Pack> packs;
        //  SINTES primero: es lo que esta ficha ES desde que un pad puede
        //  llevar un instrumento. La fabrica detras, que sigue siendo lo que
        //  recarga un banco entero.
        packs.push_back (sintes());
        packs.push_back (fabrica());

        const auto raiz = carpeta();
        if (! raiz.isDirectory()) return packs;

        const auto tengo = licencias();
        auto dirs = raiz.findChildFiles (juce::File::findDirectories, false);
        dirs.sort();

        for (const auto& d : dirs)
        {
            Pack p;
            p.id     = d.getFileName();
            p.nombre = detalle::sinNumero (p.id);

            if (const auto man = d.getChildFile ("pack.txt"); man.existsAsFile())
            {
                const auto texto = man.loadFileAsString();
                if (const auto n = detalle::valorDe (texto, "nombre"); n.isNotEmpty())
                    p.nombre = n.toUpperCase();
                p.dePago = detalle::valorDe (texto, "pago") == "1";
            }
            p.abierto = ! p.dePago || tengo.contains (p.id);

            auto sub = d.findChildFiles (juce::File::findDirectories, false);
            sub.sort();
            for (const auto& s : sub)
            {
                if ((int) p.instr.size() >= kMaxInstr) break;
                int n = 0;
                for (const auto& f : s.findChildFiles (juce::File::findFiles, false))
                    if (esAudio (f)) ++n;
                if (n == 0) continue;   // una carpeta sin audio no es un instrumento
                p.instr.push_back ({ detalle::sinNumero (s.getFileName()), s, -1,
                                     juce::jmin (n, kPresets) });
            }

            //  UN PACK SIN INSTRUMENTOS NO SE LISTA. Una carpeta vacia dentro
            //  de ZATI/Instrumentos es basura o un pack a medio copiar, y una
            //  fila que al tocarla no hace nada es la peor clase de control.
            if (! p.instr.empty()) packs.push_back (p);
        }
        return packs;
    }

    //  Los ficheros de un instrumento, en el orden en que van a los pads: por
    //  nombre, que es donde estan las dos cifras.
    inline juce::Array<juce::File> presetsDe (const Instrumento& i)
    {
        juce::Array<juce::File> out;
        if (i.carpeta.isDirectory())
            for (const auto& f : i.carpeta.findChildFiles (juce::File::findFiles, false))
                if (esAudio (f)) out.add (f);
        out.sort();
        if (out.size() > kPresets) out.removeRange (kPresets, out.size() - kPresets);
        return out;
    }
}
