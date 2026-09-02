#pragma once

#include <JuceHeader.h>

// ============================================================================
//  Lang - la app en cuatro idiomas: castellano, ingles, chino y arabe.
//
//  La CLAVE de cada cadena es el texto que ya estaba escrito en el codigo, que
//  es casi todo castellano con algunas palabras inglesas que vienen del
//  vocabulario del estudio. Es a proposito, por dos cosas. Nada del codigo se
//  convierte en un identificador opaco: una linea con la frase entera dentro de
//  la llamada sigue diciendo lo que va a poner en pantalla, y una traduccion que
//  falta degrada a algo que una persona puede leer en vez de a
//  STR_PAD_EMPTY_042. (El ejemplo no se escribe aqui como una llamada de
//  verdad: Tests/lang.py lee el codigo buscandolas, y una en un comentario le
//  sale como una clave que no esta en la tabla.) Y el castellano es una busqueda como
//  cualquier otra en lugar de la identidad, asi que los pocos sitios donde el
//  texto castellano era el mismo una palabra inglesa se arreglan en la tabla sin
//  tocar el codigo.
//
//  A veces dos cadenas necesitan la misma clave para decir cosas distintas -REV
//  es el interruptor de reverso de un pad y tambien la reverberacion- asi que
//  una clave puede llevar un contexto detras de una barra: T ("REV|reverso").
//  La barra y lo que va detras se recortan cuando no hay traduccion, para que el
//  respaldo siga siendo la palabra pelada.
//
//  Los argumentos entran como %1, %2, %3 y no concatenando trozos. "Cortado en "
//  + n + " trozos" solo se lee bien en un idioma cuyo orden de palabras coincida
//  con el castellano, y ni el chino ni el arabe lo hacen.
//
//  Lo que NO se traduce en ningun idioma: las seis abreviaturas de los efectos
//  (FLT, HPF, DRV, DLY, BIT, REV) y los sufijos de unidad (ms, Hz, dB, bpm,
//  st). Se leen igual en una mesa de Shanghai que en una de Madrid, y viven en
//  botones de cuatro caracteres de ancho.
//
//  El arabe se traduce pero el maquetado no se espeja: la cara de la maquina es
//  un panel fijo de pads y mandos cuyas posiciones son memoria muscular, no una
//  columna de texto. El texto se lee de derecha a izquierda dentro de su caja,
//  que es lo que hace el dibujante por su cuenta.
// ============================================================================
class Lang
{
public:
    enum Id { es = 0, en, zh, ar, numLanguages };

    static Id  current() noexcept;
    static void set (Id newLanguage);

    //  El idioma del sistema si lo hablamos, y castellano si no.
    static Id  detect();

    static const char* code (Id id);          // "es" / "en" / "zh" / "ar"
    static const char* nativeName (Id id);    // as that language writes itself
    static bool isRightToLeft (Id id) noexcept { return id == ar; }

    //  Cercar una tirada de NUMEROS o de latino para que el arabe no la
    //  reordene.
    //
    //  "OUT " + "-inf" salia en pantalla como "خرج inf-": el bidi vio un menos
    //  en el borde de una tirada de derecha a izquierda y se lo llevo al otro
    //  extremo, asi que una lectura de menos infinito se convirtio en algo que
    //  no es un numero. Igual con "-12 dB", con "4 nucleos - 15.7 GB", con el
    //  nombre de un fichero, con cualquier cosa latina metida dentro de una
    //  frase traducida.
    //
    //  U+2066 LEFT-TO-RIGHT ISOLATE ... U+2069 POP DIRECTIONAL ISOLATE dice
    //  "este trozo tiene su propia direccion y no participa en el bidi de lo que
    //  lo rodea". Fuera del arabe no cuesta nada: los caracteres son invisibles
    //  y la cadena vuelve intacta.
    static juce::String ltr (const juce::String& latinRun);

    //  Donde EMPIEZA una linea de texto y donde ACABA, que no es lo mismo que
    //  izquierda y derecha en cuanto el arabe es uno de los idiomas.
    //
    //  La cara de la maquina se queda como esta a proposito: pads, mandos y
    //  transporte son memoria muscular y una posicion, no una columna de prosa.
    //  Pero las fichas son filas de rotulo y valor, y un rotulo clavado a la
    //  izquierda de una fila arabe se lee tan mal como uno ingles clavado a la
    //  derecha.
    static juce::Justification start (int extraFlags = juce::Justification::verticallyCentred)
    {
        return juce::Justification ((isRightToLeft (current()) ? juce::Justification::right
                                                               : juce::Justification::left) | extraFlags);
    }
    static juce::Justification end (int extraFlags = juce::Justification::verticallyCentred)
    {
        return juce::Justification ((isRightToLeft (current()) ? juce::Justification::left
                                                               : juce::Justification::right) | extraFlags);
    }

    //  Coge el trozo de cabeza de una fila -el de la izquierda normalmente, el
    //  de la derecha en arabe- para que un par rotulo/valor cambie de lado con
    //  el idioma.
    static juce::Rectangle<int> takeStart (juce::Rectangle<int>& row, int amount)
    {
        return isRightToLeft (current()) ? row.removeFromRight (amount)
                                         : row.removeFromLeft  (amount);
    }
    //  ...y el trozo de cola, que es donde viven la equis de una ficha y sus
    //  teclas de esquina. Espejar el TEXTO de una ficha sin espejar tambien
    //  estas es peor que no espejar nada: en arabe el titulo se va a la derecha
    //  y aterriza justo encima de los botones que ya estaban ahi.
    static juce::Rectangle<int> takeEnd (juce::Rectangle<int>& row, int amount)
    {
        return isRightToLeft (current()) ? row.removeFromLeft  (amount)
                                         : row.removeFromRight (amount);
    }

    //  Se recuerda entre arranques al lado del resto de la carpeta ZATI. Es una
    //  preferencia y no estado del proyecto: no pinta nada dentro de una cancion.
    static void loadPreference();
    static void savePreference();
};

//  Traducir. Las sobrecargas sustituyen %1, %2, %3.
juce::String T (const juce::String& key);
juce::String T (const juce::String& key, const juce::String& a1);
juce::String T (const juce::String& key, const juce::String& a1, const juce::String& a2);
juce::String T (const juce::String& key, const juce::String& a1,
                const juce::String& a2, const juce::String& a3);
