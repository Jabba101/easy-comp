# =============================================================================
#  Easy Comp - a beginner-friendly compressor plugin (VST3 / AU), built on JUCE
#
#  You don't need to edit this file to build the plugin. JUCE (the free audio
#  framework the plugin is built on) is downloaded automatically the first
#  time the project is opened, so there's nothing else to install by hand.
#  See "HOW TO BUILD.txt" for the step-by-step guide.
# =============================================================================
cmake_minimum_required(VERSION 3.22)

project(EasyComp VERSION 1.1.0)

# Build the Visual C++ runtime into the plugin itself, so it also works on a
# PC that doesn't have Visual Studio installed. (No effect on macOS / Linux.)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---- Download JUCE ----------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/9.0.4.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(JUCE)

# ---- The plugin -------------------------------------------------------------
juce_add_plugin(EasyComp
    PRODUCT_NAME                "Easy Comp"       # the name your DAW shows
    COMPANY_NAME                "Home Studio"     # shown as the plugin's maker
    BUNDLE_ID                   com.homestudio.easycomp
    PLUGIN_MANUFACTURER_CODE    Hmst              # 4 characters, at least one capital
    PLUGIN_CODE                 Ezcp              # 4 characters, exactly one capital, unique per plugin
    FORMATS                     VST3 AU           # AU is only built on a Mac
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE             # copied by hand: the VST3 folder needs admin rights
    NEEDS_WEB_BROWSER           FALSE
    NEEDS_CURL                  FALSE
    VST3_CATEGORIES             Fx Dynamics
    AU_MAIN_TYPE                kAudioUnitType_Effect)

target_sources(EasyComp
    PRIVATE
        Source/Compressor.h
        Source/Presets.h
        Source/PluginProcessor.h
        Source/PluginProcessor.cpp
        Source/PluginEditor.h
        Source/PluginEditor.cpp)

target_compile_definitions(EasyComp
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(EasyComp
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
