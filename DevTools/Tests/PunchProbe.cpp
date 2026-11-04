// Where do the hits stand out in a whole mix? (not shipped)
#include "TransientShaper.h"
#include "Equalizer.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

static juce::AudioBuffer<float> load (const juce::String& path, double& rate, double from, double secs)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (juce::File (path).createInputStream().release(), true));
    rate = r->sampleRate;
    const int n = (int) (secs * rate);
    juce::AudioBuffer<float> b (2, n);
    r->read (&b, 0, n, (juce::int64) (from * rate), true, true);
    return b;
}
struct BQ { Equalizer::Coefficients c; double z1 = 0, z2 = 0; float run (float x) { double y = c.b0 * x + z1; z1 = c.b1 * x - c.a1 * y + z2; z2 = c.b2 * x - c.a2 * y; return (float) y; } };

int main()
{

    for (auto* name : { "band", "mureka" })
    {
        double rate;
        auto mix = load (juce::String ("/home/claude/presetcheck/mixes/") + name + ".wav", rate, 20.0, 12.0);
        struct Variant { const char* what; double hp, lp; };
        for (auto v : { Variant { "full", 0, 0 }, Variant { "HP 1k", 1000, 0 }, Variant { "HP 3k", 3000, 0 }, Variant { "HP 6k", 6000, 0 },
                        Variant { "LP 150", 0, 150 }, Variant { "BP 1-5k", 1000, 5000 } })
        {
            juce::AudioBuffer<float> b (mix);
            for (int c = 0; c < 2; ++c)
            {
                BQ h1, h2, l1, l2;
                if (v.hp > 0) { h1.c = Equalizer::highPass (v.hp, Equalizer::q24a, rate); h2.c = Equalizer::highPass (v.hp, Equalizer::q24b, rate); }
                if (v.lp > 0) { l1.c = Equalizer::lowPass (v.lp, Equalizer::q24a, rate); l2.c = Equalizer::lowPass (v.lp, Equalizer::q24b, rate); }
                auto* d = b.getWritePointer (c);
                for (int i = 0; i < b.getNumSamples(); ++i) { float x = d[i]; if (v.hp > 0) x = h2.run (h1.run (x)); if (v.lp > 0) x = l2.run (l1.run (x)); d[i] = x; }
            }
            TransientShaper t; TransientShaper::Settings s; s.punch = 0.5f; t.prepare (rate, s);
            double sum = 0, most = 0; int count = 0;
            for (int i = 0; i + 64 <= b.getNumSamples(); i += 64)
            {
                juce::AudioBuffer<float> part (b.getArrayOfWritePointers(), 2, i, 64);
                const auto r = t.process (part);
                if (i > rate) { sum += r.maxBoostDb; most = std::max (most, (double) r.maxBoostDb); ++count; }
            }
            // how often it's boosting by more than 1 dB
            std::printf ("%-7s %-8s average boost %.2f dB, most %.2f dB\n", name, v.what, sum / count, most);
        }
    }
}
