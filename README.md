# Shard — P0 native sampler prototype (JUCE / C++)  ·  by ARTiFACTS

**P0 is a latency-validation gate**, not a product. It is a minimal but
*architecturally correct* JUCE skeleton whose only job is to prove that
tap → sound feels **"Koala-tight" (~10–25 ms by cable)** on the target Android
device. If it convinces, the project proceeds to P1 (64-voice matrix, choke
groups, the 6 FX, time-stretch, sequencer, streaming). If it doesn't, the
approach is revisited before committing months of work.

This repo is the **native rewrite**, separate from the FX-404 WebView. It shares
no code with the WebView — FX-404's UI/UX only serves as *specification* for P1+.

## What P0 does

- Standalone desktop app (JUCE 8, fetched & pinned via CMake FetchContent).
- Loads **one** audio sample (WAV/AIFF/FLAC/Ogg/MP3) into a `juce::AudioBuffer<float>`.
- Two pads:
  - **PAD A** — plays the sample at original pitch.
  - **PAD B** — plays it **+5 semitones** (audibly proves the pitch-accumulator math).
- Playback engine: fractional **phase accumulator** + **Hermite 4-point** interpolation.
- Triggers cross UI → audio via a **lock-free FIFO** (`juce::AbstractFifo`).
- `getNextAudioBlock` does **zero** allocation / lock / I/O / `std::string` / free.

Deliberately **out of scope for P0**: envelopes, choke, more than 2 voices,
FX, sequencer, recording, streaming.

## Thread model (the core discipline)

| Thread | Does | Never does |
| --- | --- | --- |
| **Message** | UI, async file decode, **all memory frees** | audio DSP |
| **Audio** (`getNextAudioBlock`) | render only | alloc, lock, I/O, `std::string`, **free** |

Transport:
- **Triggers**: `CommandFifo` (single-producer message thread → single-consumer audio thread).
- **Sample data**: atomic raw-pointer swap. The audio thread only *reads* buffer
  data and does atomic ref-count *increments* — it never decrements, so it can
  never trigger a `delete`. Old buffers are retired to a mailbox and deleted by a
  message-thread timer (`AudioEngine::collectRetiredSamples`).

## Layout

```
CMakeLists.txt            FetchContent JUCE 8.0.4 (pinned), standalone GUI app, GPL/trial defs
Source/
  Main.cpp                JUCEApplication + DocumentWindow
  MainComponent.*         AudioAppComponent: pads + Load button + GC timer
  AudioEngine.*           RT engine: voices, command FIFO, atomic sample swap, render loop
  Voice.h                 phase accumulator + Hermite 4-pt interpolation
  SampleBuffer.h          ref-counted AudioBuffer<float> + sourceSampleRate (F_src)
  CommandFifo.h           AbstractFifo-backed SPSC trigger queue
  SampleLoader.*          background decode → publish ref-counted buffer
```

## Build & run (desktop)

**Prerequisites**
- CMake ≥ 3.22 and a C++17 compiler.
- **macOS**: Xcode command-line tools.
- **Windows**: Visual Studio 2022 (MSVC).
- **Linux**: `build-essential`, plus JUCE's audio/GUI dev headers, e.g.:
  `sudo apt install libasound2-dev libjack-jackd2-dev libx11-dev libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev libfreetype6-dev libcurl4-openssl-dev libwebkit2gtk-4.1-dev`
- Network access on first configure (FetchContent clones JUCE `8.0.4`).

**Configure, build, run**
```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```
The binary lands under `build/Shard_artefacts/Release/` (exact path varies by
OS/generator). Launch it, click **Load sample**, pick a file, then tap the pads.

> The desktop build only validates the DSP and threading plumbing. Desktop audio
> latency (especially Windows WASAPI shared mode) is **not** representative of the
> Android low-latency path — the gate verdict is the Android number below.

## Android export (done by you, later, in Android Studio)

P0 is written to be Android-exportable in principle (no desktop-only assumptions),
but is validated desktop-first. When ready:

1. Install the Android SDK + NDK.
2. Configure with the NDK CMake toolchain, e.g.
   `-DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake`
   plus `-DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26`.
3. Enable Oboe for the low-latency audio path:
   add `JUCE_USE_ANDROID_OBOE=1` to the target's compile definitions.
4. Build/run the generated Android project from Android Studio on the device.

The `applicationId`/package and the JUCE licence (GPLv3 vs commercial) are
deferred business decisions — P0 ships in **GPLv3 / trial** mode (splash screen on).

## The GATE — latency measurement (acceptance)

Measure and record, on the **Redmi by cable**:

- **Round-trip audio latency** — via **OboeTester** (loopback / round-trip test)
  or a physical loopback cable, to isolate the audio stack from touch latency.
- **Tap → sound** — capture the finger-tap transient and the resulting audio with
  an external mic/scope and measure the delta.

Record in a gate note: device + OS, audio backend (desktop default vs Android
Oboe/AAudio), sample rate (F_sys) and buffer size, and the two measured numbers
(ms). Prior Stage-0 data (do **not** re-measure): internal speaker ~47–62 ms
(HyperOS smart-amp wall), **cable/USB ~12–25 ms**.

**Pass** → proceed to P1. **Fail** → revisit the approach before scaling.

## Known P0 limitations (by design)

- Single retiree mailbox slot: reloads are serialized (Load button disabled until
  the GC timer drains). P1 replaces it with a small SPSC free-list.
- No envelope: `NoteOff`/one-shot end is a hard stop (may click). P1 adds AHDSR.
- A sample swap mid-note may glitch the note in flight — acceptable for a
  latency prototype.

## UI design constraint for P1 (original look — avoid trade dress)

The real UI is built in P1 (P0 is just two test pads + a Load button, no skin).
To keep the product commercially clear of Roland SP-404 trade dress, P1 must use
an **original visual identity**, not a reproduction of the hardware:

- **Own layout & aesthetic** — do not copy the SP-404's specific colours,
  typography, chassis textures, or exact control placement. A 4×4 pad grid is a
  functional industry standard (MPC, Maschine, Push) and fine to use; the risk is
  cloning the specific look-and-feel, so design a distinct high-contrast/digital
  theme instead of a skeuomorphic chassis.
- **Generic effect names** — avoid Roland's product terms. E.g. "DJFX Looper" →
  "Beat Repeat", "Isolator" → "Multiband EQ". Describe the DSP, don't borrow the
  brand's labels.
- **Naming** — the app is "Shard" (studio: ARTiFACTS), no "404"; the package id is
  `com.artifacts.shard`. Keep "404" and Roland terminology out of user-facing
  strings, package ids, and store metadata.

The C++/JUCE DSP engine is original IP and is not the concern here — this is
purely a presentation-layer guideline. (Not legal advice; confirm with an IP
lawyer before shipping.)
