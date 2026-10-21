#include "PluginProcessor.h"
#include "Scenarios.h"
#include <iostream>
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto results = golden::runAll<EasyCompAudioProcessor> ([] (EasyCompAudioProcessor& p, int index)
    {
        // v1.2 preset index 1 = "Kick - Punch", with its v1.2 settings: the presets have since
        // been retuned, but this checks that the sound-making code is unchanged.
        if (index == 1)
            p.loadPreset ({ "Drums", "Kick - Punch", "", { { "threshold", -18.0f }, { "ratio", 4.0f }, { "attack", 25.0f },
                                                          { "release", 80.0f }, { "makeup", 3.0f }, { "mix", 100.0f },
                                                          { "lowcut", 20.0f }, { "reactsto", 0.0f } } });
    });
    const int differing = golden::compare (results, juce::File (argv[1]));
    std::cout << (differing == 0 ? "ALL WITHIN TOLERANCE" : "SOME DIFFER") << " (" << results.size() - (size_t) differing << " / " << results.size() << ")\n";
    return differing == 0 ? 0 : 1;
}
