// How much computer Easy Master uses, part by part (not shipped).
#include "PluginProcessor.h"
#include "BenchCommon.h"
using namespace bench;

int main (int argc, char**)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto audio = music (20.0);
    if (argc > 1)   // (for a profiler: everything off, a few seconds)
    {
        EasyMasterAudioProcessor p; th::prepare (p, sr, block);
        for (auto* id : { MasterIDs::tamerOn, MasterIDs::compOn, MasterEQIDs::on }) setNamed (p, id, 0.0f);
        juce::MidiBuffer midi; juce::AudioBuffer<float> work (2, block);
        for (int b = 0; b < 400; ++b) { for (int ch = 0; ch < 2; ++ch) work.copyFrom (ch, 0, audio, ch, b * block, block); p.processBlock (work, midi); }
        return 0;
    }
    std::cout << std::unitbuf << "\nEASY MASTER: % of one core (48 kHz, 512 blocks, stereo)\n";
    std::cout << "  " << juce::String ("what's on").paddedRight (' ', 40) << "  playing     silence\n";

    const char* switches[] { MasterIDs::tamerOn, MasterIDs::compOn, MasterEQIDs::on, MasterMBIDs::on, MasterPunchIDs::on,
                             MasterWidthIDs::on, MasterInflateIDs::on, MasterPultecIDs::on, MasterVariMuIDs::on };
    const char* names[]    { "RESONANCE TAMER", "COMPRESSOR", "EQ", "MULTIBAND", "PUNCH", "WIDTH", "INFLATE", "PULTEC", "VARI-MU" };

    const auto fresh = [] { auto p = std::make_unique<EasyMasterAudioProcessor>(); th::prepare (*p, sr, block); return p; };
    const auto only = [&] (EasyMasterAudioProcessor& p, int which)
    {
        for (int i = 0; i < 9; ++i) setNamed (p, switches[i], i == which ? 1.0f : 0.0f);
    };
    const auto both = [&] (const juce::String& name, std::function<void (EasyMasterAudioProcessor&)> setUp)
    {
        auto p = fresh(); setUp (*p);
        auto q = fresh(); setUp (*q);
        const double a = measure (*p, audio, false);
        line (name, a, measure (*q, audio, true));
    };

    {
        auto p = fresh();
        juce::StringArray on;
        for (int i = 0; i < 9; ++i) if (p->parameters.getRawParameterValue (switches[i])->load() > 0.5f) on.add (names[i]);
        std::cout << "  (a new Easy Master has on: " << on.joinIntoString (", ") << ", LIMITER; delay " << p->getLatencySamples() << " samples)\n";
    }

    both ("a new Easy Master (as it opens)", [] (auto&) {});
    both ("everything off (limiter + meters)", [&] (auto& p) { only (p, -1); });
    for (int i = 0; i < 9; ++i)
        both (juce::String ("only ") + names[i], [&, i] (auto& p) { only (p, i); });
    both ("only EQ, MID + SIDE", [&] (auto& p) { only (p, 2); setNamed (p, MasterEQIDs::mode, 1.0f); });
    both ("everything on", [&] (auto& p) { for (auto* s : switches) setNamed (p, s, 1.0f); });
    both ("everything on, DITHER 16 shaped", [&] (auto& p) { for (auto* s : switches) setNamed (p, s, 1.0f); setNamed (p, "dither", 1.0f); });

    auto keeper = fresh();
    const auto presets = keeper->getBuiltInPresets();
    for (size_t i = 1; i < presets.size(); i += juce::jmax ((size_t) 1, presets.size() / 6))
        both ("preset: " + presets[i].name.substring (0, 30), [&, i] (auto& p) { p.loadPreset (presets[i]); });
    return 0;
}
