// How much computer the Easy Strip uses, module by module (not shipped).
// Times real processing of a drum loop: 48 kHz, 512-sample blocks, stereo.
// "% of one core" = processing time / the time the sound lasts.

#include "PluginProcessor.h"
#include "TestHelpers.h"
#include <chrono>

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    juce::AudioBuffer<float> drums (double seconds)
    {
        const int n = (int) (sr * seconds);
        juce::AudioBuffer<float> b (2, n);
        juce::Random random (7);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr, beat = std::fmod (t, 0.5);
            const float kick = (float) (0.9 * std::exp (-beat * 18.0) * std::sin (2.0 * juce::MathConstants<double>::pi * (55.0 + 90.0 * std::exp (-beat * 40.0)) * beat));
            const double off = std::fmod (t + 0.25, 0.5);
            const float snare = (float) (0.5 * std::exp (-off * 25.0)) * (random.nextFloat() * 2.0f - 1.0f);
            const float bed = 0.08f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t) + 0.02f * (random.nextFloat() * 2.0f - 1.0f);
            b.setSample (0, i, kick + snare + bed);
            b.setSample (1, i, kick + 0.9f * snare + bed);
        }
        return b;
    }

    void setNamed (EasyStripAudioProcessor& p, const juce::String& id, float value)
    {
        if (auto* prm = p.parameters.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    }

    void onlyOn (EasyStripAudioProcessor& p, std::initializer_list<int> on)
    {
        for (int m = 0; m < StripModules::numModules; ++m)
            setNamed (p, StripIDs::moduleID (m, CommonIDs::bypass), 1.0f);
        for (int m : on)
            setNamed (p, StripIDs::moduleID (m, CommonIDs::bypass), 0.0f);
    }

    /** % of one core. */
    double measure (EasyStripAudioProcessor& p, const juce::AudioBuffer<float>& source, bool silent, double seconds = 12.0)
    {
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> work (2, block);
        const int blocks = (int) (seconds * sr / block), warm = (int) (2.0 * sr / block);
        int pos = 0;
        const auto run = [&] (int count, bool quiet)
        {
            for (int b = 0; b < count; ++b)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    if (quiet) work.clear (ch, 0, block);
                    else       work.copyFrom (ch, 0, source, ch, pos, block);
                }
                pos = (pos + block) % (source.getNumSamples() - block);
                p.processBlock (work, midi);
            }
        };
        run (warm, false);                 // (always warmed up with sound, so tails are ringing)
        if (silent) run (warm * 4, true);  // (then 8 s of silence before timing the silence)
        double best = 1.0e9;
        for (int pass = 0; pass < 3; ++pass)
        {
            const auto t0 = std::chrono::steady_clock::now();
            run (blocks, silent);
            const double took = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            best = juce::jmin (best, took);
        }
        return 100.0 * best / (blocks * block / sr);
    }

    void line (const juce::String& name, double playing, double silent)
    {
        std::cout << "  " << name.paddedRight (' ', 34) << juce::String (playing, 2).paddedLeft (' ', 7) << " %"
                  << juce::String (silent, 2).paddedLeft (' ', 10) << " %\n";
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto audio = drums (20.0);

    std::cout << std::unitbuf << "\nEASY STRIP: % of one core (48 kHz, 512 blocks, stereo)\n";
    std::cout << "  " << juce::String ("what's on").paddedRight (' ', 34) << "  playing     silence\n";

    const auto fresh = []
    {
        auto p = std::make_unique<EasyStripAudioProcessor>();
        auto layout = p->getBusesLayout();
        if (layout.inputBuses.size() > 1)
            layout.inputBuses.getReference (1) = juce::AudioChannelSet::disabled();   // (no sidechain)
        p->setBusesLayout (layout);
        p->setRateAndBufferSizeDetails (sr, block);
        p->prepareToPlay (sr, block);
        return p;
    };

    {
        auto p = fresh();
        juce::StringArray on;
        for (int m = 0; m < StripModules::numModules; ++m)
            if (p->isModuleOn (m)) on.add (StripModules::tabs[m]);
        std::cout << "  (a new strip has on: " << on.joinIntoString (", ") << ")\n";
        if (auto* os = p->parameters.getParameter (StripIDs::oversampling))
            std::cout << "  (OVERSAMPLING: " << os->getCurrentValueAsText() << " of " << os->getAllValueStrings().joinIntoString (" / ") << ")\n";
    }

    { auto p = fresh(); const double a = measure (*p, audio, false); auto q = fresh(); line ("a new strip (as it opens)", a, measure (*q, audio, true)); }
    { auto p = fresh(); onlyOn (*p, {}); const double a = measure (*p, audio, false); auto q = fresh(); onlyOn (*q, {}); line ("every module off", a, measure (*q, audio, true)); }

    for (int m = 0; m < StripModules::numModules; ++m)
    {
        auto p = fresh(); onlyOn (*p, { m });
        auto q = fresh(); onlyOn (*q, { m });
        const double a = measure (*p, audio, false);
        line (juce::String ("only ") + StripModules::tabs[m], a, measure (*q, audio, true));
    }

    // OVERSAMPLING, for the three it applies to
    auto keeper = fresh();
    if (auto* probe = keeper->parameters.getParameter (StripIDs::oversampling))
    {
        const auto names = probe->getAllValueStrings();
        for (int choice = 0; choice < names.size(); ++choice)
            for (int m : { (int) StripModules::ampIndex, (int) StripModules::tapeIndex, (int) StripModules::clipIndex })
            {
                auto p = fresh(); onlyOn (*p, { m }); setNamed (*p, StripIDs::oversampling, (float) choice);
                auto q = fresh(); onlyOn (*q, { m }); setNamed (*q, StripIDs::oversampling, (float) choice);
                const double a = measure (*p, audio, false);
                line (juce::String ("only ") + StripModules::tabs[m] + ", oversampling " + names[choice], a, measure (*q, audio, true));
            }
    }

    {
        auto p = fresh(); auto q = fresh();
        for (auto* s : { p.get(), q.get() })
            for (int m = 0; m < StripModules::numModules; ++m)
                setNamed (*s, StripIDs::moduleID (m, CommonIDs::bypass), 0.0f);
        const double a = measure (*p, audio, false);
        line ("every module on", a, measure (*q, audio, true));
    }

    // A few presets
    {
        const auto presets = keeper->getBuiltInPresets();
        for (size_t i = 1; i < presets.size(); i += juce::jmax ((size_t) 1, presets.size() / 8))
        {
            auto p = fresh(); p->loadPreset (presets[i]);
            auto q = fresh(); q->loadPreset (presets[i]);
            const double a = measure (*p, audio, false);
            line ("preset: " + presets[i].name.substring (0, 24), a, measure (*q, audio, true));
            if (a > 4.0)   // (a heavy one: which modules is it?)
                for (int m = 0; m < StripModules::numModules; ++m)
                    if (p->isModuleOn (m))
                    {
                        auto without = fresh(); without->loadPreset (presets[i]);
                        setNamed (*without, StripIDs::moduleID (m, CommonIDs::bypass), 1.0f);
                        std::cout << "      " << juce::String (StripModules::tabs[m]).paddedRight (' ', 10)
                                  << juce::String (a - measure (*without, audio, false, 6.0), 2).paddedLeft (' ', 7) << " %\n";
                    }
        }
    }
    return 0;
}
