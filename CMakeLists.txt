# ============================================================================
#  Zati — native sampler (JUCE / C++)  ·  by ARTiFACTS
#  Desktop-first standalone app. Android export is done later by the user in
#  Android Studio (see README). JUCE is fetched & pinned via FetchContent; no
#  Projucer, no vendored JUCE checkout.
# ============================================================================
cmake_minimum_required(VERSION 3.22)

project(Zati VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Fetch JUCE (PINNED tag — never track master/develop) -------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4          # pinned; bump deliberately, not automatically
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# --- Standalone GUI app -----------------------------------------------------
juce_add_gui_app(Zati
    PRODUCT_NAME "Zati"
    COMPANY_NAME "ARTiFACTS"
    BUNDLE_ID    "com.artifacts.zati"
)

juce_generate_juce_header(Zati)

# Bundled fonts (Oswald display + JetBrains Mono) as binary data.
juce_add_binary_data(ZatiData SOURCES
    Source/fonts/OswaldBold.ttf
    Source/fonts/OswaldMedium.ttf
    Source/fonts/JetBrainsMonoRegular.ttf
    Source/fonts/JetBrainsMonoBold.ttf
    # Los treinta y un sonidos de fabrica que vienen de una maquina de verdad.
    # Ver Tools/fabrica.py: de donde sale cada uno y por que en FLAC.
    Source/kits/a01_kick.flac
    Source/kits/a02_snare.flac
    Source/kits/a03_hat.flac
    Source/kits/a04_open.flac
    Source/kits/a05_rim.flac
    Source/kits/a06_tomlo.flac
    Source/kits/a07_tommi.flac
    Source/kits/a08_tomhi.flac
    Source/kits/a09_clap.flac
    Source/kits/a10_ride.flac
    Source/kits/a11_crash.flac
    Source/kits/a12_shake.flac
    Source/kits/a13_conga.flac
    Source/kits/a15_tamb.flac
    Source/kits/a16_splash.flac
    Source/kits/b01_bd.flac
    Source/kits/b02_sd.flac
    Source/kits/b03_ch.flac
    Source/kits/b04_oh.flac
    Source/kits/b05_rs.flac
    Source/kits/b06_cp.flac
    Source/kits/b07_lt.flac
    Source/kits/b08_mt.flac
    Source/kits/b09_ht.flac
    Source/kits/b10_lc.flac
    Source/kits/b11_mc.flac
    Source/kits/b12_hc.flac
    Source/kits/b13_cy.flac
    Source/kits/b14_cb.flac
    Source/kits/b15_cl.flac
    Source/kits/b16_ma.flac
)

target_sources(Zati PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/AudioEngine.cpp
    Source/SampleLoader.cpp
    Source/AudioFocus.cpp
    Source/Lang.cpp
    Source/SystemInsets.cpp
    Source/DeviceTier.cpp
    Source/SessionKeeper.cpp
    Source/AppStorage.cpp
    Source/MediaStore.cpp
)

# --- Compile definitions ----------------------------------------------------
#  GPLv3 / trial mode: splash screen + usage reporting stay ON. Swap to a
#  commercial config (splash off) only after a JUCE licence is purchased.
target_compile_definitions(Zati PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_DISPLAY_SPLASH_SCREEN=1
    JUCE_REPORT_APP_USAGE=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1          # decode MP3 (common on phones)
    JUCE_STRICT_REFCOUNTEDPOINTER=1   # catches ref-count misuse at compile time
)

# --- Modules ----------------------------------------------------------------
#  juce_audio_utils transitively pulls audio_basics/devices/formats/processors,
#  gui_basics, events, core, data_structures, graphics — and AudioAppComponent.
target_link_libraries(Zati PRIVATE
    ZatiData
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
#  The bench.
#
#  Two halves, and neither is a unit test: they are the app under the load a
#  stand puts on it. The interface half needs no target at all — the app
#  measures itself when ZATI_AUDIT is set (see Source/UiAudit.h). This is the
#  audio half: the engine driven the way a demo drives it, with the output
#  checked for the three things that end one — silence, a NaN, and a block
#  that took longer than it had.
#
#      cmake -B build -DZATI_BENCH=ON && cmake --build build --target StressTest
#      ./build/StressTest_artefacts/StressTest
# ---------------------------------------------------------------------------
option(ZATI_BENCH "Build the engine stress bench" OFF)

if(ZATI_BENCH)
    juce_add_console_app(StressTest PRODUCT_NAME "StressTest")
    juce_generate_juce_header(StressTest)
    target_sources(StressTest PRIVATE Tests/StressTest.cpp Source/AudioEngine.cpp)
    target_compile_definitions(StressTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STRICT_REFCOUNTEDPOINTER=1)
    target_link_libraries(StressTest PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    #  Y el de MUCHA GENTE, que es otra cosa que el de mucha carga: un millon
    #  de sesiones distintas, cada una con su semilla, su aparato y su tirada
    #  de acciones. Ver Tests/Soak.cpp.
    juce_add_console_app(Soak PRODUCT_NAME "Soak")
    juce_generate_juce_header(Soak)
    target_sources(Soak PRIVATE Tests/Soak.cpp Source/AudioEngine.cpp)
    target_compile_definitions(Soak PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STRICT_REFCOUNTEDPOINTER=1)
    target_link_libraries(Soak PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    #  Y el de CUANTO CUESTA CADA COSA. Ver Tests/Cpu.cpp: una fila por etapa
    #  aislada contra el mismo silencio, porque "optimizar la CPU" sin la
    #  tabla es elegir a ojo cual de veinte bucles es el caro.
    juce_add_console_app(Cpu PRODUCT_NAME "Cpu")
    juce_generate_juce_header(Cpu)
    target_sources(Cpu PRIVATE Tests/Cpu.cpp Source/AudioEngine.cpp)
    target_compile_definitions(Cpu PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STRICT_REFCOUNTEDPOINTER=1)
    target_link_libraries(Cpu PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()
