cmake_minimum_required(VERSION 3.22)

# Must be set before project() to take effect.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(VAESynth VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# JUCE has no native CLAP support yet; this adds a VAESynth_CLAP target.
FetchContent_Declare(clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG        7adee3a1bd4684d4caa5601100e364abccff4b4d
    GIT_SUBMODULES_RECURSE TRUE)
FetchContent_MakeAvailable(clap-juce-extensions)

set(VAESYNTH_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND VAESYNTH_FORMATS AU)
endif()

# NOTE: the manufacturer/plugin codes and bundle id identify the plugin in
# DAW sessions. Settle them before the first public release and never change
# them afterwards, or saved projects will no longer find the plugin.
juce_add_plugin(VAESynth
    PRODUCT_NAME              "VAESynth"
    COMPANY_NAME              "VAESynth"
    BUNDLE_ID                 "com.vaesynth.vaesynth"
    PLUGIN_MANUFACTURER_CODE  Vaes
    PLUGIN_CODE               Vae1
    FORMATS                   ${VAESYNTH_FORMATS}
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    VST3_CATEGORIES           Instrument Synth
    AU_MAIN_TYPE              kAudioUnitType_MusicDevice)

clap_juce_extensions_plugin(TARGET VAESynth
    CLAP_ID       "com.vaesynth.vaesynth"
    CLAP_FEATURES instrument synthesizer granular stereo)

juce_add_binary_data(VAESynthAssets
    SOURCES
        assets/fonts/Jost-Regular.ttf
        assets/fonts/Jost-Light.ttf)

set(VAESYNTH_ENGINE_SOURCES
    Source/engine/GrainMap.cpp
    Source/engine/GrainEngine.cpp)

target_sources(VAESynth PRIVATE
    ${VAESYNTH_ENGINE_SOURCES}
    Source/Parameters.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/Theme.cpp
    Source/ui/SphereView.cpp
    Source/ui/ControlPanel.cpp
    Source/ui/SampleBar.cpp)

target_compile_definitions(VAESynth PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(VAESynth
    PRIVATE
        VAESynthAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Headless engine tests: cmake -DVAESYNTH_BUILD_TESTS=ON, then ctest.
option(VAESYNTH_BUILD_TESTS "Build the engine tests" OFF)

if(VAESYNTH_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(VAESynthTests PRODUCT_NAME "VAESynthTests")

    target_sources(VAESynthTests PRIVATE
        ${VAESYNTH_ENGINE_SOURCES}
        tests/EngineTests.cpp)

    target_compile_definitions(VAESynthTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

    target_link_libraries(VAESynthTests
        PRIVATE
            juce::juce_audio_formats
            juce::juce_data_structures
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME engine COMMAND VAESynthTests)

    # Full processor (loading thread, state save/restore), linked against the
    # plugin's own shared code so it exercises exactly what ships.
    add_executable(VAESynthProcessorTests tests/ProcessorTests.cpp)
    target_include_directories(VAESynthProcessorTests PRIVATE $<TARGET_PROPERTY:VAESynth,INCLUDE_DIRECTORIES>)
    target_compile_definitions(VAESynthProcessorTests PRIVATE $<TARGET_PROPERTY:VAESynth,COMPILE_DEFINITIONS>)
    target_link_libraries(VAESynthProcessorTests PRIVATE VAESynth)

    add_test(NAME processor COMMAND VAESynthProcessorTests)
endif()
