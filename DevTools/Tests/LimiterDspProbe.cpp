// Quick probe of TruePeakLimiter (not shipped).
#include "TruePeakLimiter.h"
#include <iostream>
int main()
{
    const double sr = 48000.0;
    juce::Random r (5);
    // "music": kick-like bursts + bass sine + noise, mixed hot
    const int n = (int) (sr * 6);
    juce::AudioBuffer<float> in (2, n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double beat = std::fmod (t, 0.5);
        const float kick = (float) (std::exp (-beat * 18.0) * std::sin (2 * M_PI * (50 + 120 * std::exp (-beat * 30)) * beat));
        const float bass = 0.35f * (float) std::sin (2 * M_PI * 55.0 * t);
        const float hiss = 0.15f * (r.nextFloat() * 2 - 1);
        in.setSample (0, i, 0.6f * kick + bass + hiss);
        in.setSample (1, i, 0.6f * kick + bass - hiss);
    }
    std::printf ("input true peak %.2f dBTP\n", TruePeakLimiter::truePeakDb (in));
    for (float gainDb : { 0.0f, 6.0f, 12.0f, 18.0f })
    {
        TruePeakLimiter lim;
        TruePeakLimiter::Settings s; s.gainDb = gainDb; s.ceilingDb = -0.3f;
        lim.prepare (sr, 2, s);
        auto out = in;
        for (int i = 0; i < n; i += 512) { juce::AudioBuffer<float> b (out.getArrayOfWritePointers(), 2, i, std::min (512, n - i)); lim.process (b, 2); }
        std::printf ("gain %+4.1f: output true peak %.3f dBTP, sample peak %.3f dB, latency %d\n", gainDb, TruePeakLimiter::truePeakDb (out),
                     juce::Decibels::gainToDecibels (out.getMagnitude (0, n)), lim.getLatencySamples());
    }
    // pure sine 1 kHz at 0 dBFS with inter-sample peaks: fs/4 phase 45deg sine gives +3 dB TP
    {
        juce::AudioBuffer<float> s (1, (int) sr);
        for (int i = 0; i < s.getNumSamples(); ++i) s.setSample (0, i, (float) std::sin (2 * M_PI * 12000.0 * i / sr + M_PI / 4));
        std::printf ("12 kHz sine, samples at 0.707: true peak %.2f dBTP (should be ~0)\n", TruePeakLimiter::truePeakDb (s));
        juce::AudioBuffer<float> st (2, (int) sr);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < st.getNumSamples(); ++i) st.setSample (c, i, 0.97f * std::min (1.0f, i / 480.0f) * (float) std::sin (2 * M_PI * 12000.0 * i / sr + M_PI / 4));
        std::printf ("in TP %.3f\n", TruePeakLimiter::truePeakDb (st));
        TruePeakLimiter lim; TruePeakLimiter::Settings se; lim.prepare (sr, 2, se);
        for (int i = 0; i < st.getNumSamples(); i += 512) { juce::AudioBuffer<float> b (st.getArrayOfWritePointers(), 2, i, std::min (512, st.getNumSamples() - i)); lim.process (b, 2); }
        std::printf ("reduction %.3f dB, out peak (meter) %.3f, out TP %.3f, sample peak %.3f\n", lim.getReductionDb(), lim.getOutputPeakDb(), TruePeakLimiter::truePeakDb (st), juce::Decibels::gainToDecibels (st.getMagnitude (0, st.getNumSamples())));
    }
}
