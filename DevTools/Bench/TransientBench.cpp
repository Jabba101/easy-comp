#include "PluginProcessor.h"
#include "Bench.h"
int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto drums = loadMaterial ("/home/claude/presetcheck/material/drumbus_rock.wav");
    const juce::AudioBuffer<float> silence;
    for (bool autoGain : { false, true })
        std::printf ("Easy Transient  %-24s AUTO-GAIN %-3s       : %.3f%% of one core\n", "Drum Bus - Punchier", autoGain ? "on" : "off",
                     measure<EasyTransientAudioProcessor> ("Drum Bus - Punchier", autoGain, drums));
    std::printf ("Easy Transient  %-24s AUTO-GAIN on, silence: %.3f%% of one core\n", "Drum Bus - Punchier",
                 measure<EasyTransientAudioProcessor> ("Drum Bus - Punchier", true, silence));
}
