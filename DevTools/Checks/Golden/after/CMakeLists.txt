cmake_minimum_required(VERSION 3.22)
project(GoldenCompare VERSION 1.0.0)
add_subdirectory(/home/claude/deps/juce-9.0.4 juce)
set(P /home/claude/EasyPlugins)
juce_add_console_app(GoldenCompare PRODUCT_NAME "GoldenCompare")
target_sources(GoldenCompare PRIVATE ../Compare.cpp
    ${P}/Plugins/EasyComp/PluginProcessor.cpp ${P}/Plugins/EasyComp/PluginEditor.cpp
    ${P}/Shared/EasyProcessor.cpp ${P}/Shared/EasyWindow.cpp ${P}/Shared/Look.cpp ${P}/Shared/PresetBrowser.cpp ${P}/Shared/Widgets.cpp)
target_include_directories(GoldenCompare PRIVATE ${P}/Plugins/EasyComp ${P}/Shared ${P}/Effects ..)
target_compile_definitions(GoldenCompare PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Easy Comp")
target_link_libraries(GoldenCompare PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)
