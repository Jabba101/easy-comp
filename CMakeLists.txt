# =============================================================================
#  Aardvark Easy Suite - beginner-friendly mixing plugins (VST3 / AU), built on JUCE
#
#  You don't need to edit this file to build them. JUCE (the free audio
#  framework they're built on) is downloaded automatically the first time
#  the project is opened, so there's nothing else to install by hand.
#  See "HOW TO BUILD.txt" for the step-by-step guide.
#
#  When the build finishes, every plugin is in one folder:
#      (build folder)/Finished Plugins/
#
#  How the project is laid out:
#      Effects/   the sound-shaping code (no windows or knobs in there)
#      Shared/    the parts every plugin shares: the look, the help bar,
#                 BYPASS / AUTO-GAIN, the preset menu, saving with projects
#      Plugins/   one folder per plugin: its knobs, window and presets
# =============================================================================
cmake_minimum_required(VERSION 3.22)

project(EasyPlugins VERSION 1.10.0)

# Build the Visual C++ runtime into the plugins themselves, so they also work
# on a PC that doesn't have Visual Studio installed. (No effect on macOS / Linux.)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---- Download JUCE ----------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/9.0.4.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(JUCE)

# ---- The parts every plugin shares ------------------------------------------
set(SHARED_SOURCES
    Shared/EasyProcessor.h  Shared/EasyProcessor.cpp
    Shared/EasyWindow.h     Shared/EasyWindow.cpp
    Shared/Look.h           Shared/Look.cpp
    Shared/PresetBrowser.h  Shared/PresetBrowser.cpp
    Shared/Widgets.h        Shared/Widgets.cpp
    Shared/Parameters.h
    Effects/FastMath.h
    Effects/LoudnessMatcher.h)

set(FINISHED_PLUGINS_FOLDER "${CMAKE_BINARY_DIR}/Finished Plugins")

# ---- PFFFT: the fast FFT behind Bitch-Slap's reverb (ThirdParty/pffft/README.txt)
add_library(pffft STATIC ThirdParty/pffft/pffft.c ThirdParty/pffft/pffft_common.c)
target_include_directories(pffft PUBLIC ThirdParty/pffft)
target_compile_definitions(pffft PUBLIC PFFFT_STATIC_DEFINE)
target_compile_options(pffft PRIVATE $<$<C_COMPILER_ID:MSVC>:/FImalloc.h>)   # (pffft.c uses _alloca but forgets this header on Windows)
set_target_properties(pffft PROPERTIES POSITION_INDEPENDENT_CODE ON)

# add_easy_plugin(<target>
#     NAME        "<the name your DAW shows>"
#     CODE        <4 characters, exactly one capital, unique per plugin - never change it>
#     VERSION     <x.y.z>
#     BUNDLE_ID   <com.something.unique>
#     CATEGORIES  <VST3 categories, e.g. Fx Dynamics>
#     SOURCES     <the plugin's own files>)
function(add_easy_plugin target)
    cmake_parse_arguments(PLUGIN "" "NAME;CODE;VERSION;BUNDLE_ID" "CATEGORIES;SOURCES" ${ARGN})

    juce_add_plugin(${target}
        VERSION                     ${PLUGIN_VERSION}
        PRODUCT_NAME                "${PLUGIN_NAME}"
        COMPANY_NAME                "Aardvark Easy Suite"   # the maker: DAWs list the plugins in a folder with this name
        BUNDLE_ID                   ${PLUGIN_BUNDLE_ID}
        PLUGIN_MANUFACTURER_CODE    Hmst              # 4 characters, the same for every plugin - never change it
        PLUGIN_CODE                 ${PLUGIN_CODE}
        FORMATS                     VST3 AU           # AU is only built on a Mac
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD     TRUE              # into "Finished Plugins" (not your system folders)
        VST3_COPY_DIR               "${FINISHED_PLUGINS_FOLDER}"
        AU_COPY_DIR                 "${FINISHED_PLUGINS_FOLDER}"
        NEEDS_WEB_BROWSER           FALSE
        NEEDS_CURL                  FALSE
        VST3_CATEGORIES             ${PLUGIN_CATEGORIES}
        AU_MAIN_TYPE                kAudioUnitType_Effect)

    target_sources(${target} PRIVATE ${PLUGIN_SOURCES} ${SHARED_SOURCES})
    target_include_directories(${target} PRIVATE Shared Effects)

    target_compile_definitions(${target}
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

# ---- The plugins ------------------------------------------------------------
add_easy_plugin(EasyComp
    NAME        "Easy Comp"
    CODE        Ezcp
    VERSION     1.6.0
    BUNDLE_ID   com.homestudio.easycomp
    CATEGORIES  Fx Dynamics
    SOURCES     Effects/Compressor.h
                Plugins/EasyComp/Presets.h
                Plugins/EasyComp/PluginProcessor.h
                Plugins/EasyComp/PluginProcessor.cpp
                Plugins/EasyComp/PluginEditor.h
                Plugins/EasyComp/PluginEditor.cpp)

add_easy_plugin(EasyTransient          # (called Easy Transient up to 1.2.0)
    NAME        "Easy Trans"
    CODE        Eztr
    VERSION     1.2.1
    BUNDLE_ID   com.homestudio.easytransient
    CATEGORIES  Fx Dynamics
    SOURCES     Effects/TransientShaper.h
                Plugins/EasyTransient/Presets.h
                Plugins/EasyTransient/PluginProcessor.h
                Plugins/EasyTransient/PluginProcessor.cpp
                Plugins/EasyTransient/PluginEditor.h
                Plugins/EasyTransient/PluginEditor.cpp)

add_easy_plugin(EasyGate
    NAME        "Easy Gate"
    CODE        Ezgt
    VERSION     1.0.0
    BUNDLE_ID   com.homestudio.easygate
    CATEGORIES  Fx Dynamics
    SOURCES     Effects/Gate.h
                Plugins/EasyGate/Presets.h
                Plugins/EasyGate/PluginProcessor.h
                Plugins/EasyGate/PluginProcessor.cpp
                Plugins/EasyGate/PluginEditor.h
                Plugins/EasyGate/PluginEditor.cpp)

add_easy_plugin(EasyDS
    NAME        "Easy DS"
    CODE        Ezds
    VERSION     1.0.0
    BUNDLE_ID   com.homestudio.easyds
    CATEGORIES  Fx Dynamics
    SOURCES     Effects/DeEsser.h
                Effects/HarshnessFinder.h
                Plugins/EasyDS/Presets.h
                Plugins/EasyDS/PluginProcessor.h
                Plugins/EasyDS/PluginProcessor.cpp
                Plugins/EasyDS/PluginEditor.h
                Plugins/EasyDS/PluginEditor.cpp)

add_easy_plugin(BitchSlap
    NAME        "Bitch-Slap"
    CODE        Ezbs
    VERSION     1.0.0
    BUNDLE_ID   com.homestudio.bitchslap
    CATEGORIES  Fx Reverb
    SOURCES     Effects/GatedReverb.h
                Plugins/BitchSlap/Presets.h
                Plugins/BitchSlap/PluginProcessor.h
                Plugins/BitchSlap/PluginProcessor.cpp
                Plugins/BitchSlap/PluginEditor.h
                Plugins/BitchSlap/PluginEditor.cpp)
target_link_libraries(BitchSlap PRIVATE pffft)
