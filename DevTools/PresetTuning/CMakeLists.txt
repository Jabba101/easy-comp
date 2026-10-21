cmake_minimum_required(VERSION 3.22)
project(PresetCheck VERSION 1.0.0)
add_subdirectory(/home/claude/deps/juce-9.0.4 juce)

set(P /home/claude/EasyPlugins)
set(SHARED ${P}/Shared/EasyProcessor.cpp ${P}/Shared/EasyWindow.cpp ${P}/Shared/Look.cpp ${P}/Shared/PresetBrowser.cpp ${P}/Shared/Widgets.cpp)

foreach(tool CompCheck TransientCheck)
    juce_add_console_app(${tool} PRODUCT_NAME "${tool}")
    target_compile_definitions(${tool} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_include_directories(${tool} PRIVATE ${P}/Shared ${P}/Effects /home/claude/verify2 ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${tool} PRIVATE juce::juce_audio_utils juce::juce_audio_formats PUBLIC juce::juce_recommended_config_flags)
endforeach()

target_sources(CompCheck PRIVATE CompCheck.cpp ${P}/Plugins/EasyComp/PluginProcessor.cpp ${P}/Plugins/EasyComp/PluginEditor.cpp ${SHARED})
target_include_directories(CompCheck PRIVATE ${P}/Plugins/EasyComp)
target_compile_definitions(CompCheck PRIVATE JucePlugin_Name="Easy Comp")

target_sources(TransientCheck PRIVATE TransientCheck.cpp ${P}/Plugins/EasyTransient/PluginProcessor.cpp ${P}/Plugins/EasyTransient/PluginEditor.cpp ${SHARED})
target_include_directories(TransientCheck PRIVATE ${P}/Plugins/EasyTransient)
target_compile_definitions(TransientCheck PRIVATE JucePlugin_Name="Easy Transient")

juce_add_console_app(GateCheck PRODUCT_NAME "GateCheck")
target_compile_definitions(GateCheck PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_include_directories(GateCheck PRIVATE ${P}/Shared ${P}/Effects /home/claude/verify2 ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(GateCheck PRIVATE GateCheck.cpp ${P}/Shared/PresetBrowser.cpp)
target_link_libraries(GateCheck PRIVATE juce::juce_audio_utils juce::juce_audio_formats PUBLIC juce::juce_recommended_config_flags)

juce_add_console_app(DSCheck PRODUCT_NAME "DSCheck")
target_compile_definitions(DSCheck PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_include_directories(DSCheck PRIVATE ${P}/Shared ${P}/Effects /home/claude/verify2 ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(DSCheck PRIVATE DSCheck.cpp ${P}/Shared/PresetBrowser.cpp)
target_link_libraries(DSCheck PRIVATE juce::juce_audio_utils juce::juce_audio_formats PUBLIC juce::juce_recommended_config_flags)
