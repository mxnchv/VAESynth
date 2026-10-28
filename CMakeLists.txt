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

target_sources(VAESynth PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(VAESynth PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(VAESynth
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
