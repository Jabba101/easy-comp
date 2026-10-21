// Loads the built Easy Comp VST3 the way a DAW does and checks the new 1.6
// parts from the outside: the settings the DAW sees, the sidechain bus, and
// that a sound sent to the sidechain turns the track down. (Not shipped.)
#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>

static float peakOf (const juce::AudioBuffer<float>& b, int ch, int from, int to)
{
    return b.getMagnitude (ch, from, to - from);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::VST3PluginFormat format;
    if (argc < 2) return 1;

    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, argv[1]);
    if (found.isEmpty()) { std::cout << "nothing found\n"; return 1; }
    auto& d = *found[0];
    std::cout << d.name << " | maker: " << d.manufacturerName << " | version " << d.version << " | uid " << juce::String::toHexString (d.uniqueId) << "\n";

    juce::String error;
    auto p = format.createInstanceFromDescription (d, 48000.0, 512, error);
    if (p == nullptr) { std::cout << "couldn't open: " << error << "\n"; return 1; }

    std::cout << "settings the DAW sees (" << p->getParameters().size() << "):";
    juce::AudioProcessorParameter* listensTo = nullptr;
    juce::AudioProcessorParameter* style = nullptr;
    juce::AudioProcessorParameter* lowEnd = nullptr;
    for (auto* prm : p->getParameters())
    {
        std::cout << " [" << prm->getName (40) << " = " << prm->getCurrentValueAsText() << "]";
        if (prm->getName (40) == "Listens To") listensTo = prm;
        if (prm->getName (40) == "Style") style = prm;
        if (prm->getName (40) == "Low End") lowEnd = prm;
    }
    std::cout << "\n";

    std::cout << "input buses: " << p->getBusCount (true);
    for (int b = 0; b < p->getBusCount (true); ++b)
        std::cout << " [" << p->getBus (true, b)->getName() << ", " << p->getBus (true, b)->getNumberOfChannels() << " ch, "
                  << (p->getBus (true, b)->isEnabled() ? "on" : "off") << "]";
    std::cout << "\n";

    if (listensTo == nullptr || style == nullptr || lowEnd == nullptr) { std::cout << "FAIL: new settings missing\n"; return 1; }

    p->enableAllBuses();
    p->prepareToPlay (48000.0, 512);
    std::cout << "latency: " << p->getLatencySamples() << " samples; total ins " << p->getTotalNumInputChannels() << ", outs " << p->getTotalNumOutputChannels() << "\n";

    // 2 s: a quiet 220 Hz track (under the threshold), and a loud 1 kHz tone in the sidechain for the second second
    const auto runOnce = [&] (bool sendToSidechain, float listens, float vintage, float thrust)
    {
        listensTo->setValueNotifyingHost (listens);
        style->setValueNotifyingHost (vintage);
        lowEnd->setValueNotifyingHost (thrust);
        const int channels = juce::jmax (p->getTotalNumInputChannels(), p->getTotalNumOutputChannels());
        const int n = 96000;
        juce::AudioBuffer<float> all (channels, n), in (2, n);
        all.clear();
        for (int i = 0; i < n; ++i)
        {
            const float track = 0.0316f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) i / 48000.0f);   // -30 dBFS
            const float key   = i >= 48000 ? 0.5f * std::sin (juce::MathConstants<float>::twoPi * 1000.0f * (float) i / 48000.0f) : 0.0f;   // -6 dBFS
            for (int ch = 0; ch < 2; ++ch) { all.setSample (ch, i, track); in.setSample (ch, i, track); }
            if (sendToSidechain)
                for (int ch = 2; ch < channels; ++ch) all.setSample (ch, i, key);
        }
        juce::MidiBuffer midi;
        {
            // (3 s of silence first, so whatever the last run left behind has let go)
            juce::AudioBuffer<float> silence (channels, 512);
            for (int k = 0; k < 3 * 48000 / 512; ++k) { silence.clear(); p->processBlock (silence, midi); }
        }
        for (int start = 0; start < n; start += 512)
        {
            juce::AudioBuffer<float> view (all.getArrayOfWritePointers(), channels, start, std::min (512, n - start));
            p->processBlock (view, midi);
        }
        const float before = juce::Decibels::gainToDecibels (peakOf (all, 0, 24000, 47000) / peakOf (in, 0, 24000, 47000));
        const float during = juce::Decibels::gainToDecibels (peakOf (all, 0, 72000, 95000) / peakOf (in, 0, 72000, 95000));
        return std::make_pair (before, during);
    };

    const auto [b1, d1] = runOnce (true, 1.0f, 0.0f, 0.0f);
    std::cout << "LISTENS TO: SIDECHAIN, MODERN:  before the key " << b1 << " dB, during " << d1 << " dB\n";
    const auto [b2, d2] = runOnce (true, 1.0f, 1.0f, 1.0f);
    std::cout << "LISTENS TO: SIDECHAIN, VINTAGE + THRUST: before " << b2 << " dB, during " << d2 << " dB\n";
    const auto [b3, d3] = runOnce (true, 0.0f, 0.0f, 0.0f);
    std::cout << "LISTENS TO: THIS TRACK (key ignored): before " << b3 << " dB, during " << d3 << " dB\n";

    const bool ok = std::abs (b1) < 0.01f && d1 < -8.0f && std::abs (b2) < 0.01f && d2 < -8.0f && std::abs (b3) < 0.01f && std::abs (d3) < 0.01f;
    std::cout << (ok ? "OK" : "FAIL") << "\n";
    p->releaseResources();
    return ok ? 0 : 1;
}
