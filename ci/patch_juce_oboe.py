#!/usr/bin/env python3
"""Give JUCE's Oboe backend two dials it does not have.

JUCE opens its Android output stream with a fixed configuration: whatever
usage AAudio defaults to (MEDIA), and float samples with a 16-bit fallback
only if float fails to open outright. Both choices are invisible from
application code, and both decide whether Android grants the stream MMAP.

On a phone whose vendor post-processing (Dolby, the system equaliser) hooks
MEDIA streams, a MEDIA stream cannot be MMAP - the effects have to run
somewhere, so the audio goes through AudioFlinger's mixer and the app pays
~40 ms it never asked for. A GAME stream usually skips that chain. Likewise a
device may only expose an exclusive endpoint in 16 bit.

So the app probes the device at startup (Source/AudioPath.h), finds the
configuration Android is actually willing to grant, and writes it into two
globals. This patch is what makes JUCE read them.

Both default to zero, which reproduces stock JUCE exactly. The patch is
applied to a fresh JUCE clone in CI and fails loudly if the anchors move,
because silently not patching would look identical to patching and not
helping.
"""

import sys
from pathlib import Path

SRC = Path(sys.argv[1] if len(sys.argv) > 1
           else "JUCE/modules/juce_audio_devices/native/juce_Oboe_android.cpp")

DECL = """
// --- Zati: set from the app's AAudio probe before any device is opened. ---
extern "C" int zatiOboeUsage;
extern "C" int zatiOboeForceI16;
extern "C" int zatiOboeInputPreset;
"""

USAGE_ANCHOR = "            builder.setPerformanceMode (oboe::PerformanceMode::LowLatency);"
USAGE_PATCH = USAGE_ANCHOR + """

            // Zati: the usage the phone was willing to grant MMAP for.
            if (zatiOboeUsage != 0 && direction == oboe::Direction::Output)
                builder.setUsage ((oboe::Usage) zatiOboeUsage);

            // Zati: LA ENTRADA, SIN PROCESAR.
            //
            // Android le mete a cualquier grabacion control automatico de
            // ganancia, supresion de ruido y cancelador de eco. Para una
            // llamada esta bien; para samplear un disco es la diferencia entre
            // grabar el disco y grabar una llamada: comprime, se come los
            // graves y bombea. UNPROCESSED apaga los tres. Si el aparato no lo
            // admite, Oboe cae solo al preset que tenga.
            if (zatiOboeInputPreset != 0 && direction == oboe::Direction::Input)
                builder.setInputPreset ((oboe::InputPreset) zatiOboeInputPreset);"""

FLOAT_ANCHOR = """    // SDK versions 21 and higher should natively support floating point...
    std::unique_ptr<OboeSessionBase> session = std::make_unique<OboeSessionImpl<float>> (owner,"""
FLOAT_PATCH = """    // Zati: skip the float attempt when the probe found that only a 16-bit
    // stream is granted exclusive mode - opening float first would succeed
    // and quietly settle for the shared path.
    std::unique_ptr<OboeSessionBase> session;

    if (zatiOboeForceI16 == 0)
    session = std::make_unique<OboeSessionImpl<float>> (owner,"""


#  EL CONTADOR DE CHASQUIDOS, CUANDO EL APARATO NO LO LLEVA.
#
#  `getXRunCount` es el unico sentido que la app tiene para saber si el telefono
#  llega con el bloque que le ha pedido: cuenta los under-runs y, si crecen,
#  sube el bloque. Oboe contesta `ErrorUnimplemented` en los caminos que no lo
#  llevan -el legado de OpenSL, y AAudio fuera del MMAP, que es SIEMPRE el caso
#  por Bluetooth- y JUCE se come ese error y devuelve CERO.
#
#  Cero es tambien lo que devuelve un aparato que cuenta y no tiene ni un
#  chasquido, asi que la ley de la app no podia distinguir «todo limpio» de «no
#  hay quien lo diga», y tomaba lo segundo por lo primero: 45 s de supuesta
#  limpieza y BAJA el bloque, reabriendo el flujo -un corte de sonido- hasta
#  dejarlo en el minimo, que es justo donde cruje. Sin poder volver a subirlo
#  nunca, porque para subir hace falta el contador.
#
#  `MainComponent::checkXRuns` ya tiene la guarda escrita -«if (now < 0) return;
#  el dispositivo no lleva la cuenta»- y era codigo MUERTO, porque JUCE no
#  devuelve negativos. Esto es lo que la hace existir.
#
#  Y el duplex suma entrada y salida: un aparato que cuenta la salida y no la
#  entrada -lo normal- seguiria contestando por la salida. Solo cuando ninguna
#  de las dos sabe, la respuesta es «no se sabe».
XRUN_ANCHOR = """        int getXRunCount() const
        {
            if (stream != nullptr)
            {
                auto count = stream->getXRunCount();

                if (count)
                    return count.value();

                JUCE_OBOE_LOG ("Failed to get Xrun count: " + getOboeString (count.error()));
            }

            return 0;
        }"""
XRUN_PATCH = """        int getXRunCount() const
        {
            if (stream != nullptr)
            {
                auto count = stream->getXRunCount();

                if (count)
                    return count.value();

                JUCE_OBOE_LOG ("Failed to get Xrun count: " + getOboeString (count.error()));
            }

            // Zati: -1 is "this device does not count", which is not the same
            // answer as "no under-runs yet". See Source/MainComponent.cpp,
            // checkXRuns.
            return -1;
        }"""

XRUN_SUM_ANCHOR = """        int getXRunCount() const
        {
            int inputXRunCount  = jmax (0, inputStream  != nullptr ? inputStream->getXRunCount() : 0);
            int outputXRunCount = jmax (0, outputStream != nullptr ? outputStream->getXRunCount() : 0);

            return inputXRunCount + outputXRunCount;
        }"""
XRUN_SUM_PATCH = """        int getXRunCount() const
        {
            // Zati: a stream that cannot count says -1 (see above). It must not
            // be summed as a zero, and it must not hide the other direction's
            // count either - only when NEITHER can answer is the answer -1.
            const int inputXRunCount  = inputStream  != nullptr ? inputStream->getXRunCount()  : -1;
            const int outputXRunCount = outputStream != nullptr ? outputStream->getXRunCount() : -1;

            if (inputXRunCount < 0 && outputXRunCount < 0)
                return -1;

            return jmax (0, inputXRunCount) + jmax (0, outputXRunCount);
        }"""


def main() -> int:
    if not SRC.is_file():
        print(f"patch_juce_oboe: {SRC} not found", file=sys.stderr)
        return 1

    text = SRC.read_text(encoding="utf-8")

    if "zatiOboeUsage" in text and "does not count" in text:
        print("patch_juce_oboe: already patched")
        return 0

    for name, anchor in (("performance mode", USAGE_ANCHOR),
                         ("float session", FLOAT_ANCHOR),
                         ("xrun count", XRUN_ANCHOR),
                         ("xrun sum", XRUN_SUM_ANCHOR)):
        if text.count(anchor) != 1:
            print(f"patch_juce_oboe: {name} anchor matched "
                  f"{text.count(anchor)} times, expected 1 - JUCE moved, "
                  f"update this script", file=sys.stderr)
            return 1

    # The declarations go after the include guard region, i.e. straight after
    # the first namespace opening, so both patch sites can see them.
    marker = "namespace juce\n{\n"
    if marker not in text:
        print("patch_juce_oboe: namespace anchor not found", file=sys.stderr)
        return 1

    text = text.replace(marker, marker + DECL, 1)
    text = text.replace(USAGE_ANCHOR, USAGE_PATCH, 1)
    text = text.replace(FLOAT_ANCHOR, FLOAT_PATCH, 1)
    text = text.replace(XRUN_ANCHOR, XRUN_PATCH, 1)
    text = text.replace(XRUN_SUM_ANCHOR, XRUN_SUM_PATCH, 1)

    SRC.write_text(text, encoding="utf-8")
    print(f"patch_juce_oboe: patched {SRC}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
