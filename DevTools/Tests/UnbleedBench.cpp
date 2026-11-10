// How much computer Easy Unbleed uses (not shipped).
#include "PluginProcessor.h"
#include "BenchCommon.h"
using namespace bench;

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    std::cout << std::unitbuf << "\nEASY UNBLEED: % of one core (48 kHz, 512 blocks, stereo stem + stereo sidechain)\n";
    std::cout << "  " << juce::String ("what").paddedRight (' ', 40) << "  playing     silence\n";

    // The stem on 1-2, the other stems on 3-4
    const auto stem = music (20.0, 7, 220.0), others = music (20.0, 19, 330.0);
    juce::AudioBuffer<float> four (4, stem.getNumSamples()), two (2, stem.getNumSamples());
    for (int ch = 0; ch < 2; ++ch)
    {
        four.copyFrom (ch, 0, stem, ch, 0, stem.getNumSamples());
        four.copyFrom (2 + ch, 0, others, ch, 0, stem.getNumSamples());
        two.copyFrom (ch, 0, stem, ch, 0, stem.getNumSamples());
    }

    const auto fresh = [] (bool sidechain)
    {
        auto p = std::make_unique<EasyUnbleedAudioProcessor>();
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.inputBuses.add (sidechain ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
        layout.outputBuses.add (juce::AudioChannelSet::stereo());
        p->setBusesLayout (layout);
        p->setRateAndBufferSizeDetails (sr, block);
        p->prepareToPlay (sr, block);
        return p;
    };

    { auto p = fresh (true); auto q = fresh (true); const double a = measure (*p, four, false); line ("with the other stems on the sidechain", a, measure (*q, four, true)); }
    { auto p = fresh (false); auto q = fresh (false); const double a = measure (*p, two, false); line ("nothing on the sidechain", a, measure (*q, two, true)); }
    { auto p = fresh (true); auto q = fresh (true); setNamed (*p, CommonIDs::bypass, 1.0f); setNamed (*q, CommonIDs::bypass, 1.0f);
      const double a = measure (*p, four, false); line ("BYPASS on", a, measure (*q, four, true)); }
    return 0;
}
