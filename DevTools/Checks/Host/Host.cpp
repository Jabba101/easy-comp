// Loads the built VST3s the way a DAW does, and checks the latency report, the
// sidechain bus and the names. (Not shipped.)
#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::VST3PluginFormat format;
    for (int a = 1; a < argc; ++a)
    {
        juce::OwnedArray<juce::PluginDescription> found;
        format.findAllTypesForFile (found, argv[a]);
        if (found.isEmpty()) { std::cout << "nothing found in " << argv[a] << "\n"; continue; }
        auto& d = *found[0];
        std::cout << d.name << " | maker: " << d.manufacturerName << " | version " << d.version << " | uid " << juce::String::toHexString (d.uniqueId)
                  << " | ins " << d.numInputChannels << " outs " << d.numOutputChannels << "\n";

        juce::String error;
        auto instance = format.createInstanceFromDescription (d, 48000.0, 512, error);
        if (instance == nullptr) { std::cout << "  couldn't open: " << error << "\n"; continue; }

        std::cout << "  input buses: " << instance->getBusCount (true);
        for (int b = 0; b < instance->getBusCount (true); ++b)
            std::cout << " [" << instance->getBus (true, b)->getName() << ", " << instance->getBus (true, b)->getNumberOfChannels() << " ch, "
                      << (instance->getBus (true, b)->isEnabled() ? "on" : "off") << "]";
        std::cout << "\n";

        instance->enableAllBuses();
        instance->prepareToPlay (48000.0, 512);
        const int latency = instance->getLatencySamples();
        std::cout << "  latency after prepare at 48 kHz: " << latency << " samples\n";

        // A click on the track: where does it come out?
        const int channels = juce::jmax (instance->getTotalNumInputChannels(), instance->getTotalNumOutputChannels());
        juce::AudioBuffer<float> buffer (channels, 512);
        juce::MidiBuffer midi;
        int clickAt = -1;
        for (int blockNumber = 0; blockNumber < 8 && clickAt < 0; ++blockNumber)
        {
            buffer.clear();
            if (blockNumber == 2)
                for (int ch = 0; ch < 2; ++ch) for (int i = 100; i < 140; ++i) buffer.setSample (ch, i, 0.5f);   // a loud burst on the track
            instance->processBlock (buffer, midi);
            for (int i = 0; i < 512 && clickAt < 0; ++i)
                if (std::abs (buffer.getSample (0, i)) > 0.25f) clickAt = blockNumber * 512 + i;
        }
        std::cout << "  a burst at sample " << 2 * 512 + 100 << " comes out at " << clickAt << " (" << (clickAt - (2 * 512 + 100)) << " later)\n";
        instance->releaseResources();
    }
    return 0;
}
