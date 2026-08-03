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
"""

USAGE_ANCHOR = "            builder.setPerformanceMode (oboe::PerformanceMode::LowLatency);"
USAGE_PATCH = USAGE_ANCHOR + """

            // Zati: the usage the phone was willing to grant MMAP for.
            if (zatiOboeUsage != 0 && direction == oboe::Direction::Output)
                builder.setUsage ((oboe::Usage) zatiOboeUsage);"""

FLOAT_ANCHOR = """    // SDK versions 21 and higher should natively support floating point...
    std::unique_ptr<OboeSessionBase> session = std::make_unique<OboeSessionImpl<float>> (owner,"""
FLOAT_PATCH = """    // Zati: skip the float attempt when the probe found that only a 16-bit
    // stream is granted exclusive mode - opening float first would succeed
    // and quietly settle for the shared path.
    std::unique_ptr<OboeSessionBase> session;

    if (zatiOboeForceI16 == 0)
    session = std::make_unique<OboeSessionImpl<float>> (owner,"""


def main() -> int:
    if not SRC.is_file():
        print(f"patch_juce_oboe: {SRC} not found", file=sys.stderr)
        return 1

    text = SRC.read_text(encoding="utf-8")

    if "zatiOboeUsage" in text:
        print("patch_juce_oboe: already patched")
        return 0

    for name, anchor in (("performance mode", USAGE_ANCHOR),
                         ("float session", FLOAT_ANCHOR)):
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

    SRC.write_text(text, encoding="utf-8")
    print(f"patch_juce_oboe: patched {SRC}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
