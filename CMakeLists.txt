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
endif()
