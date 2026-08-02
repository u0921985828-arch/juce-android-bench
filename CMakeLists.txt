# ============================================================================
#  Shard — P0 native sampler skeleton (JUCE / C++)  ·  by ARTiFACTS
#  Desktop-first standalone app. Android export is done later by the user in
#  Android Studio (see README). JUCE is fetched & pinned via FetchContent; no
#  Projucer, no vendored JUCE checkout.
# ============================================================================
cmake_minimum_required(VERSION 3.22)

project(Shard VERSION 0.0.1 LANGUAGES C CXX)

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
juce_add_gui_app(Shard
    PRODUCT_NAME "Colors"
    COMPANY_NAME "ARTiFACTS"
    BUNDLE_ID    "com.artifacts.shard"
)

juce_generate_juce_header(Shard)

# Bundled fonts (Oswald display + JetBrains Mono) as binary data.
juce_add_binary_data(ShardData SOURCES
    Source/fonts/OswaldBold.ttf
    Source/fonts/OswaldMedium.ttf
    Source/fonts/JetBrainsMonoRegular.ttf
    Source/fonts/JetBrainsMonoBold.ttf
)

target_sources(Shard PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/AudioEngine.cpp
    Source/SampleLoader.cpp
)

# --- Compile definitions ----------------------------------------------------
#  GPLv3 / trial mode: splash screen + usage reporting stay ON. Swap to a
#  commercial config (splash off) only after a JUCE licence is purchased.
target_compile_definitions(Shard PRIVATE
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
target_link_libraries(Shard PRIVATE
    ShardData
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
