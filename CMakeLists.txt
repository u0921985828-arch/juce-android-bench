# ============================================================================
#  Zati — native sampler (JUCE / C++)  ·  by ARTiFACTS
#  Desktop-first standalone app. Android export is done later by the user in
#  Android Studio (see README). JUCE is fetched & pinned via FetchContent; no
#  Projucer, no vendored JUCE checkout.
# ============================================================================
cmake_minimum_required(VERSION 3.22)

#  La version la manda Zati.jucer, que es lo que compila la APK: aqui se
#  repite y el dia que una cambie sin la otra el escritorio y el telefono dicen
#  cosas distintas. Ver GOOGLE-PLAY.md.
project(Zati VERSION 1.0.0 LANGUAGES C CXX)

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
# Y NADA MAS: la fabrica se sintetiza entera, asi que la app no
# distribuye audio de nadie. Ver THIRD-PARTY.md.
juce_add_binary_data(ZatiData SOURCES
    Source/fonts/OswaldBold.ttf
    Source/fonts/OswaldMedium.ttf
    Source/fonts/JetBrainsMonoRegular.ttf
    Source/fonts/JetBrainsMonoBold.ttf
)

target_sources(Zati PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/MainComponent_Layout.cpp
    Source/MainComponent_Paint.cpp
    Source/MainComponent_Audit.cpp
    Source/AudioEngine.cpp
    Source/SampleLoader.cpp
    Source/AudioFocus.cpp
    Source/Lang.cpp
    Source/SystemInsets.cpp
    Source/DeviceTier.cpp
    Source/SessionKeeper.cpp
    Source/AppStorage.cpp
    Source/MediaStore.cpp
    Source/Sintes.cpp
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
    target_sources(StressTest PRIVATE Tests/StressTest.cpp Source/AudioEngine.cpp Source/Sintes.cpp)
    target_compile_definitions(StressTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STRICT_REFCOUNTEDPOINTER=1)
    target_link_libraries(StressTest PRIVATE
        #  LOS SONIDOS DE FABRICA YA NO SALEN TODOS DE UNA FORMULA, asi que el
        #  banco del motor necesita los mismos bytes que la app. Desde que la
        #  mitad de la fabrica son grabaciones, Kits.h llama a
        #  BinaryData::getNamedResource y este objetivo no enlazaba ZatiData:
        #  no es que midiera de menos, es que no COMPILABA -"'BinaryData' has
        #  not been declared"- y nadie se entero porque la tanda anterior se
        #  midio antes de meter los FLAC.
        ZatiData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    #  Y el de MUCHA GENTE, que es otra cosa que el de mucha carga: un millon
    #  de sesiones distintas, cada una con su semilla, su aparato y su tirada
    #  de acciones. Ver Tests/Soak.cpp.
    juce_add_console_app(Soak PRODUCT_NAME "Soak")
    juce_generate_juce_header(Soak)
    target_sources(Soak PRIVATE Tests/Soak.cpp Source/AudioEngine.cpp Source/Sintes.cpp)
    target_compile_definitions(Soak PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STRICT_REFCOUNTEDPOINTER=1)
    target_link_libraries(Soak PRIVATE
        #  Y AQUI TAMBIEN, por lo mismo y con el mismo sintoma: el arreglo de
        #  ZatiData se hizo en StressTest -que era el que se corria esa tanda- y
        #  Soak y Cpu se quedaron sin el. "'BinaryData' has not been declared":
        #  no median de menos, es que no compilaban. Un banco que no compila no
        #  falla, no esta - y este es el que mide un millon de sesiones.
        ZatiData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    #  Y el de CUANTO CUESTA CADA COSA. Ver Tests/Cpu.cpp: una fila por etapa
    #  aislada contra el mismo silencio, porque "optimizar la CPU" sin la
    #  tabla es elegir a ojo cual de veinte bucles es el caro.
    juce_add_console_app(Cpu PRODUCT_NAME "Cpu")
    juce_generate_juce_header(Cpu)
    target_sources(Cpu PRIVATE Tests/Cpu.cpp Source/AudioEngine.cpp Source/Sintes.cpp)
    target_compile_definitions(Cpu PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STRICT_REFCOUNTEDPOINTER=1)
    target_link_libraries(Cpu PRIVATE
        ZatiData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()
