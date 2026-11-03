// Easy Unbleed's engine on simulated UVR-style stems (not shipped).
#include "SpectralUnbleed.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

static juce::AudioBuffer<float> load (const juce::String& name)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (juce::File ("/home/claude/presetcheck/unbleed/" + name + ".wav").createInputStream().release(), true));
    juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
    return b;
}

int main (int argc, char** argv)
{
    const char* names[] { "vocals", "drums", "bass", "other" };
    SpectralUnbleed::Settings s;
    if (argc > 1) s.amountDb = (float) atof (argv[1]);
    if (argc > 2) s.sensitivity = (float) atof (argv[2]);
    if (argc > 3) s.smoothness = (float) atof (argv[3]);
    std::array<juce::AudioBuffer<float>, 4> est, tru;
    for (int k = 0; k < 4; ++k) { est[(size_t) k] = load (juce::String (names[k]) + "_est"); tru[(size_t) k] = load (juce::String (names[k]) + "_true"); }
    const int n = est[0].getNumSamples();
    for (int k = 0; k < 4; ++k)
    {
        juce::AudioBuffer<float> side (2, n); side.clear();
        for (int j = 0; j < 4; ++j) if (j != k) for (int c = 0; c < 2; ++c) side.addFrom (c, 0, est[(size_t) j], c, 0, n);
        juce::AudioBuffer<float> buf (4, n);
        for (int c = 0; c < 2; ++c) { buf.copyFrom (c, 0, est[(size_t) k], c, 0, n); buf.copyFrom (c + 2, 0, tru[(size_t) k], c, 0, n); }
        SpectralUnbleed u; u.prepare (48000.0, s);
        for (int i = 0; i < n; i += 512)
        {
            const int len = std::min (512, n - i);
            juce::AudioBuffer<float> b (buf.getArrayOfWritePointers(), 4, i, len);
            juce::AudioBuffer<float> sb (side.getArrayOfWritePointers(), 2, i, len);
            u.process (b, &sb);
        }
        const int L = u.getLatencySamples();
        // metrics (out aligned back by the latency)
        double t2 = 0, e0 = 0, e1 = 0, dmg = 0, gapB = 0, gapA = 0, env = 0, peak = 0;
        std::vector<double> envs ((size_t) n);
        for (int i = 0; i < n; ++i) { const double v = 0.5 * (tru[(size_t) k].getSample (0, i) * tru[(size_t) k].getSample (0, i) + tru[(size_t) k].getSample (1, i) * tru[(size_t) k].getSample (1, i)); env = 0.999 * env + 0.001 * v; envs[(size_t) i] = std::sqrt (env); peak = std::max (peak, envs[(size_t) i]); }
        for (int i = 0; i + L < n; ++i)
            for (int c = 0; c < 2; ++c)
            {
                const double t = tru[(size_t) k].getSample (c, i), e = est[(size_t) k].getSample (c, i);
                const double out = buf.getSample (c, i + L), gt = buf.getSample (c + 2, i + L);
                t2 += t * t; e0 += (e - t) * (e - t); e1 += (out - t) * (out - t); dmg += (gt - t) * (gt - t);
                if (envs[(size_t) i] < peak * 0.0178) { gapB += (e - t) * (e - t); gapA += (out - gt) * (out - gt); }
            }
        std::printf ("%-7s SDR %5.2f -> %5.2f dB (%+.2f)  ghost in gaps %+5.1f dB  damage %6.1f dB\n", names[k], 10 * std::log10 (t2 / e0), 10 * std::log10 (t2 / e1),
                     10 * std::log10 (e0 / e1), 10 * std::log10 ((gapA + 1e-20) / (gapB + 1e-20)), 10 * std::log10 (dmg / t2 + 1e-20));
    }
}
