#include "PluginProcessor.h"
#include "Bench.h"
int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto drums = loadMaterial ("/home/claude/presetcheck/material/drumbus_rock.wav");
    const juce::AudioBuffer<float> silence;
    for (const char* preset : { "Drum Bus - Glue", "Vocal - Pop & R&B" })
        for (bool autoGain : { false, true })
            std::printf ("Easy Comp       %-24s AUTO-GAIN %-3s       : %.3f%% of one core\n", preset, autoGain ? "on" : "off",
                         measure<EasyCompAudioProcessor> (preset, autoGain, drums));
    std::printf ("Easy Comp       %-24s AUTO-GAIN on, silence: %.3f%% of one core\n", "Drum Bus - Glue",
                 measure<EasyCompAudioProcessor> ("Drum Bus - Glue", true, silence));
}
