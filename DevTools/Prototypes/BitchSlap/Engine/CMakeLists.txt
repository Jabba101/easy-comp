# Bitch-Slap's engine, checked and timed on its own (lives at /home/claude/slapdev):
#   EngineCheck  the engine against a plain, slow direct convolution (exactness,
#                odd block sizes, gaps, LENGTH changes, no clicks) and its speed
#   SlapBench    JUCE's FFT vs PFFFT, and JUCE's own Convolution, for comparison
#   SpikeProbe   how long each 64-sample block takes, by position in the cycle
cmake_minimum_required(VERSION 3.22)
project(SlapDev VERSION 1.0.0 LANGUAGES C CXX)
add_subdirectory(/home/claude/deps/juce-9.0.4 juce)

set(P /home/claude/EasyPlugins)
add_library(pffft STATIC ${P}/ThirdParty/pffft/pffft.c ${P}/ThirdParty/pffft/pffft_common.c)
target_include_directories(pffft PUBLIC ${P}/ThirdParty/pffft)
target_compile_definitions(pffft PUBLIC PFFFT_STATIC_DEFINE)
set_target_properties(pffft PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_console_app(SlapBench PRODUCT_NAME "SlapBench")
target_sources(SlapBench PRIVATE bench.cpp)
target_compile_definitions(SlapBench PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(SlapBench PRIVATE pffft juce::juce_dsp juce::juce_audio_formats PUBLIC juce::juce_recommended_config_flags)

foreach(tool EngineCheck SpikeProbe)
    juce_add_console_app(${tool} PRODUCT_NAME "${tool}")
    target_include_directories(${tool} PRIVATE ${P}/Effects)
    target_compile_definitions(${tool} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(${tool} PRIVATE pffft juce::juce_audio_basics PUBLIC juce::juce_recommended_config_flags)
endforeach()
target_sources(EngineCheck PRIVATE engine_check.cpp)
target_sources(SpikeProbe PRIVATE spike_probe.cpp)

# MixBusCheck: how loud the reverb is on full mixes, per LENGTH (for the Mix Bus presets)
juce_add_console_app(MixBusCheck PRODUCT_NAME "MixBusCheck")
target_sources(MixBusCheck PRIVATE mixbus_check.cpp)
target_include_directories(MixBusCheck PRIVATE ${P}/Effects)
target_compile_definitions(MixBusCheck PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(MixBusCheck PRIVATE pffft juce::juce_audio_formats PUBLIC juce::juce_recommended_config_flags)
