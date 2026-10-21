#include "PluginProcessor.h"
#include "Scenarios.h"
#include <iostream>
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto results = golden::runAll<EasyCompAudioProcessor> ([] (EasyCompAudioProcessor& p, int index) { p.loadPreset (index); });
    golden::save (results, juce::File (argv[1]));
    std::cout << "saved " << results.size() << " reference recordings\n";
    return 0;
}
