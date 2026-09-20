#pragma once

#include "AudioEngine.h"

//  ============================================================================
//  LOS PRESETS DE EFECTO — la tabla de fabrica y la unica puerta para leerla.
//  ============================================================================
//
//  Llego del telefono: «1 tipo de cada, si que podriamos preparar presets para
//  cada efecto, estudialo». Antes de esto un efecto arrancaba en su fila de
//  `AudioEngine::kFxDef` y a partir de ahi se movia a mano, tipo por tipo y
//  canal por canal: veintitres tipos por cuatro parametros por treinta y dos
//  canales.
//
//  ESTO ES EL MOLDE DE LOS INSTRUMENTOS, no uno nuevo. `Sintes` lleva tandas
//  haciendo exactamente esto para 256 recetas y cada pieza tiene su razon
//  escrita; lo que se copia, pieza a pieza:
//
//    · la tabla de datos vive en un `.inc` incluido DENTRO del namespace desde
//      un solo sitio (`SintesTabla.inc` / `Sintes.cpp`);
//    · el acceso por indice se escribe UNA vez (`Sintes::valor`), y aqui ya
//      existe: es `engine.get/setFxParam (canal, fx, par)`;
//    · y se ACOTA EN LA PUERTA con un `isfinite` explicito (`Sintes::acota`),
//      porque un NaN no lo tapa `jlimit` — comparar con NaN siempre es falso.
//
//  Y una pieza que aqui NO hace falta: `Sintes` tiene que DERIVAR los rangos de
//  su propia tabla porque no los declara nadie. Los efectos si los declaran, en
//  `MainComponent::fxDefs[f].spec[pi]`. *Los rangos no se declaran otra vez*, y
//  por eso el acotado vive en la cara y no aqui: `spec` es suyo, y el `def` de
//  `fxDefs` es donde el numero esta razonado — eso ya estaba decidido y escrito
//  encima de `kFxDef`.
namespace FxPresets
{
    //  CUANTOS HAY POR TIPO, contando el cero.
    //
    //  Rectangular y no irregular, igual que las veinticuatro familias por dieciseis
    //  presets de los instrumentos: una tabla rectangular la comprueba el
    //  compilador con un `static_assert`; una irregular hay que creersela.
    static constexpr int kEscritos = 5;          // las filas que hay en el `.inc`
    static constexpr int kPresets  = kEscritos + 1;   // + DEFECTO, que se deriva

    struct Preset
    {
        const char* nombre;
        float p[AudioEngine::kNumParFx];
    };

    #include "FxPresets.inc"

    static_assert (sizeof (kTabla) / sizeof (kTabla[0]) == AudioEngine::kNumFx,
                   "una fila de mas o de menos pone los presets de un efecto en el de al lado");
    static_assert (sizeof (kBandasEq) / sizeof (kBandasEq[0]) == kEscritos,
                   "cada preset de EQ tiene sus cinco bandas o ninguna");

    inline int cuantos() noexcept { return kPresets; }

    //  EL CERO ES DEFECTO Y NO ESTA EN LA TABLA.
    //
    //  Sus numeros salen de `AudioEngine::kFxDef`, que es la fila de fabrica que
    //  el motor ya publica. Escribirlos otra vez aqui seria la TERCERA tabla de
    //  defectos de esta casa -ya hay dos, y una comprobacion las cuadra a mano
    //  sobre 69 casillas con tolerancia 0.001- y la tercera es la que se queda
    //  vieja sin que nadie se entere.
    inline bool esDefecto (int k) noexcept { return k == 0; }

    inline const char* nombre (int f, int k) noexcept
    {
        if (esDefecto (k)) return "DEFECTO";
        if (! juce::isPositiveAndBelow (f, AudioEngine::kNumFx)
            || ! juce::isPositiveAndBelow (k - 1, kEscritos)) return "DEFECTO";
        return kTabla[f][k - 1].nombre;
    }

    inline float valor (int f, int k, int pi) noexcept
    {
        if (! juce::isPositiveAndBelow (f, AudioEngine::kNumFx)
            || ! juce::isPositiveAndBelow (pi, AudioEngine::kNumParFx)) return 0.0f;

        //  `kFxDef` solo tiene TRES columnas: el cuarto parametro -el enganche
        //  del modulador- no tiene fila alli, y su defecto es cero, que es
        //  «libre en Hz». No se inventa una cuarta columna en el motor para
        //  esto: el cero significa algo por si mismo.
        if (esDefecto (k))
            return pi < 3 ? AudioEngine::kFxDef[f][pi] : 0.0f;

        if (! juce::isPositiveAndBelow (k - 1, kEscritos))
            return pi < 3 ? AudioEngine::kFxDef[f][pi] : 0.0f;

        return kTabla[f][k - 1].p[pi];
    }

    //  LAS CINCO BANDAS, y solo del EQ. Devuelve `nullptr` en los otros
    //  veintidos y en DEFECTO, que es «deja las bandas donde estan»: el cero
    //  vuelve a la fila de `kFxDef`, y la fila de `kFxDef` no habla de bandas.
    inline const char* bandas (int f, int k) noexcept
    {
        if (f != AudioEngine::kFxEq || esDefecto (k)) return nullptr;
        if (! juce::isPositiveAndBelow (k - 1, kEscritos)) return nullptr;
        return kBandasEq[k - 1];
    }
}
