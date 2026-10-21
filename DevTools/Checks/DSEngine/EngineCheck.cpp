#include <juce_audio_basics/juce_audio_basics.h>
#include "DeEsser.h"
#include "DeEsserReference.h"
#include <iostream>
#include <iomanip>

static float toDb (float g) { return juce::Decibels::gainToDecibels (g, -200.0f); }
static float fromDb (float d) { return juce::Decibels::decibelsToGain (d, -200.0f); }
static const double sr = 48000.0;

// a tone that fades in over 20 ms (so it has no click at the start)
static juce::AudioBuffer<float> tone (double hz, float db, double seconds, int channels = 2)
{
    juce::AudioBuffer<float> b (channels, (int) (seconds * sr));
    const int fade = (int) (0.02 * sr);
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double env = i < fade ? 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * i / fade) : 1.0;
        for (int ch = 0; ch < channels; ++ch)
            b.setSample (ch, i, (float) (env * fromDb (db) * std::sin (juce::MathConstants<double>::twoPi * hz * i / sr)));
    }
    return b;
}

template <typename Effect>
static juce::AudioBuffer<float> run (Effect& d, juce::AudioBuffer<float> in, float* most = nullptr, int block = 512)
{
    float m = 0;
    for (int start = 0; start < in.getNumSamples(); start += block)
    {
        juce::AudioBuffer<float> v (in.getArrayOfWritePointers(), in.getNumChannels(), start, std::min (block, in.getNumSamples() - start));
        m = std::max (m, d.process (v));
    }
    if (most) *most = m;
    return in;
}

static double energy (const juce::AudioBuffer<float>& b, int from, int to, int ch = 0) { double e = 0; for (int i = from; i < to; ++i) e += (double) b.getSample (ch, i) * b.getSample (ch, i); return e; }
static int changed (const juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& in, int D, int from = 0)
{
    int n = 0;
    for (int ch = 0; ch < in.getNumChannels(); ++ch)
        for (int i = std::max (from, D); i < in.getNumSamples(); ++i)
            if (out.getSample (ch, i) != in.getSample (ch, i - D)) ++n;
    return n;
}

int main()
{
    juce::ScopedNoDenormals nd;
    DeEsser::Settings s;  // FROM 5k, TO off, threshold -6, range 8
    const int D = 96;
    std::cout << std::fixed << std::setprecision (1);

    // 1. Low tones: nothing to de-ess, bit for bit - in every way of turning down
    for (int mode = 0; mode < 3; ++mode)
    {
        DeEsser::Settings t = s;
        if (mode == 1) t.toHz = 9000.0f;
        if (mode == 2) t.wholeSound = true;
        DeEsser d; d.prepare (sr, 240, t);
        auto in = tone (1000, -6, 1.0); auto lo = tone (150, -10, 1.0);
        for (int ch = 0; ch < 2; ++ch) in.addFrom (ch, 0, lo, ch, 0, in.getNumSamples());
        float most = 0;
        auto out = run (d, in, &most);
        std::cout << "mode " << mode << ": 1 kHz + 150 Hz: samples changed = " << changed (out, in, D) << ", reduction " << most << " dB\n";
    }
    // 2. High tones alone (all 'harsh'): how far each is turned down
    for (double hz : { 3000.0, 4000.0, 5000.0, 7000.0, 10000.0, 14000.0 })
    {
        DeEsser d; d.prepare (sr, 240, s);
        float most = 0;
        auto in = tone (hz, -10, 1.0);
        auto out = run (d, in, &most);
        std::cout << (int) hz << " Hz alone: out " << 10 * std::log10 (energy (out, 24000, 48000) / energy (in, 24000 - D, 48000 - D)) << " dB, reduction reported " << most << " dB\n";
    }
    // 3. A vowel-ish tone with a little 7 kHz: no reduction
    for (float hiDb : { -40.0f, -30.0f, -20.0f, -14.0f, -10.0f })
    {
        DeEsser d; d.prepare (sr, 240, s);
        auto in = tone (300, -8, 1.0); auto hi = tone (7000, hiDb, 1.0);
        for (int ch = 0; ch < 2; ++ch) in.addFrom (ch, 0, hi, ch, 0, in.getNumSamples());
        float most = 0; run (d, in, &most);
        std::cout << "vowel (-8 dB) + 7k at " << hiDb << " dB: reduction " << most << " dB\n";
    }
    // 4. Shape of the cut: forced reduction by a loud tone in the band, probe tones at -60 dB
    for (int mode = 0; mode < 3; ++mode)
    {
        const char* names[] = { "above 5k (shelf)", "band 5-9k (dip)", "whole sound" };
        std::cout << names[mode] << " cut shape (probe change in dB):";
        for (double probe : { 1000.0, 2500.0, 3500.0, 5000.0, 6708.0, 9000.0, 12000.0, 16000.0 })
        {
            DeEsser::Settings t = s; t.rangeDb = 12.0f;
            if (mode == 1) t.toHz = 9000.0f;
            if (mode == 2) t.wholeSound = true;
            DeEsser d; d.prepare (sr, 240, t);
            DeEsser d2; d2.prepare (sr, 240, t);
            auto trigger = tone (mode == 1 ? 7000.0 : 12000.0, -10, 1.0);
            auto with = trigger; auto p = tone (probe, -60, 1.0);
            for (int ch = 0; ch < 2; ++ch) with.addFrom (ch, 0, p, ch, 0, with.getNumSamples());
            float most = 0;
            auto outWith = run (d, with, &most), outWithout = run (d2, trigger);
            // the probe's output = difference of the two runs (the system is linear for a fixed gain)
            double e = 0, ein = 0;
            for (int i = 24000; i < 48000; ++i) { const double x = outWith.getSample (0, i) - outWithout.getSample (0, i); e += x * x; ein += (double) p.getSample (0, i - D) * p.getSample (0, i - D); }
            std::cout << " " << (int) probe << ":" << 10 * std::log10 (e / ein);
            if (probe == 16000.0) std::cout << "  (reduction " << most << ")";
        }
        std::cout << "\n";
    }
    // 5. An 'S' burst between vowels: reduction during the S, none after
    for (int mode = 0; mode < 3; ++mode)
    {
        DeEsser::Settings t = s;
        if (mode == 1) t.toHz = 10000.0f;
        if (mode == 2) t.wholeSound = true;
        DeEsser d; d.prepare (sr, 240, t);
        juce::AudioBuffer<float> in (2, 48000); in.clear();
        juce::Random r (3);
        auto vowel = tone (220, -10, 1.0);
        juce::IIRFilter hp; hp.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 4000.0));
        std::vector<float> noise (48000);
        for (auto& x : noise) x = r.nextFloat() * 2 - 1;
        hp.processSamples (noise.data(), 48000);
        for (int i = 0; i < 48000; ++i)
        {
            const double t2 = i / sr;
            const double sEnv = (t2 >= 0.3 && t2 < 0.45) ? std::sin (juce::MathConstants<double>::pi * (t2 - 0.3) / 0.15) : 0.0;
            const double vEnv = t2 < 0.29 ? 1.0 : (t2 < 0.3 ? (0.3 - t2) / 0.01 : (t2 > 0.46 ? std::min (1.0, (t2 - 0.46) / 0.01) : 0.0));
            float x = (float) (vEnv * vowel.getSample (0, i) + sEnv * fromDb (-12) * noise[(size_t) i]);
            for (int ch = 0; ch < 2; ++ch) in.setSample (ch, i, x);
        }
        float most = 0;
        auto out = run (d, in, &most);
        const int a = (int) (0.33 * sr), b = (int) (0.42 * sr);
        std::cout << "mode " << mode << " S burst: during " << 10 * std::log10 (energy (out, a + D, b + D) / energy (in, a, b)) << " dB (energy), most " << most
                  << " dB; samples changed 150 ms after the S: " << changed (out, in, D, (int) (0.6 * sr)) << "\n";
    }
    // 6. LISTEN: what it takes out + what's left = the original (2 ms later)
    {
        DeEsser::Settings t = s; t.rangeDb = 12.0f;
        DeEsser a; a.prepare (sr, 240, t);
        t.listen = true;
        DeEsser b; b.prepare (sr, 240, t);
        auto in = tone (8000, -10, 0.5); auto lo = tone (200, -10, 0.5);
        for (int ch = 0; ch < 2; ++ch) in.addFrom (ch, 0, lo, ch, 0, in.getNumSamples());
        auto left = run (a, in), taken = run (b, in);
        double worst = 0;
        for (int i = D; i < in.getNumSamples(); ++i) worst = std::max (worst, (double) std::abs (left.getSample (0, i) + taken.getSample (0, i) - in.getSample (0, i - D)));
        std::cout << "LISTEN + the rest - original: worst " << toDb ((float) worst) << " dB\n";
    }
    // 7. Switching TURNS DOWN in the middle of an S: no click (the largest jump from one sample to the next)
    {
        DeEsser::Settings t = s; t.rangeDb = 12.0f;
        DeEsser d; d.prepare (sr, 240, t);
        auto in = tone (9000, -10, 1.0);
        juce::AudioBuffer<float> out (2, in.getNumSamples());
        for (int start = 0, n = 0; start < in.getNumSamples(); start += 480, ++n)
        {
            t.wholeSound = (n / 20) % 2 == 1;    // switches every 200 ms
            d.setSettings (t);
            juce::AudioBuffer<float> v (in.getArrayOfWritePointers(), 2, start, 480);
            d.process (v);
        }
        float biggest = 0, steadyBiggest = 0;
        for (int i = 1; i < in.getNumSamples(); ++i)
        {
            const float jump = std::abs (in.getSample (0, i) - in.getSample (0, i - 1));
            biggest = std::max (biggest, jump);
        }
        auto ref = tone (9000, -10, 1.0);
        for (int i = 1; i < ref.getNumSamples(); ++i) steadyBiggest = std::max (steadyBiggest, std::abs (ref.getSample (0, i) - ref.getSample (0, i - 1)));
        std::cout << "switching modes on a 9 kHz tone: biggest sample-to-sample step " << toDb (biggest) << " dB (the tone's own: " << toDb (steadyBiggest) << " dB)\n";
    }
    // 8. Speed: stereo, 10 s of vowel + S, 512-sample blocks
    {
        DeEsser::Settings t = s; t.toHz = 10000.0f;
        for (int mode = 0; mode < 3; ++mode)
        {
            DeEsser::Settings u = s;
            if (mode == 1) u.toHz = 10000.0f;
            if (mode == 2) u.wholeSound = true;
            DeEsser d; d.prepare (sr, 240, u);
            auto in = tone (220, -10, 10.0); auto hi = tone (7000, -14, 10.0);
            for (int ch = 0; ch < 2; ++ch) in.addFrom (ch, 0, hi, ch, 0, in.getNumSamples());
            const auto t0 = juce::Time::getHighResolutionTicks();
            run (d, in);
            const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
            std::cout << "mode " << mode << ": " << std::setprecision (3) << 100.0 * secs / 10.0 << "% of one core" << std::setprecision (1) << "\n";
        }
    }

    // 9. The four-at-once stereo version against one at a time: stereo with
    //    the same sound in both channels must match mono exactly; and the
    //    above-FROM mode must match the simple version exactly.
    {
        juce::Random r (11);
        juce::AudioBuffer<float> in (2, 96000);
        auto vowel = tone (180, -12, 2.0), hiss = tone (7300, -18, 2.0), sh = tone (3100, -20, 2.0);
        std::vector<float> noise (96000); for (auto& x : noise) x = r.nextFloat() * 2 - 1;
        for (int i = 0; i < 96000; ++i)
        {
            const double t = i / sr;
            const double sEnv = std::pow (std::max (0.0, std::sin (juce::MathConstants<double>::twoPi * 2.3 * t)), 4.0);
            const float x = vowel.getSample (0, i) * (float) (1.0 - sEnv) + (float) sEnv * (hiss.getSample (0, i) + 0.2f * noise[(size_t) i] + sh.getSample (0, i));
            in.setSample (0, i, x); in.setSample (1, i, x);
        }
        for (int mode = 0; mode < 3; ++mode)
        {
            DeEsser::Settings t = s; t.rangeDb = 12.0f; t.thresholdDb = -10.0f;
            if (mode == 1) t.toHz = 9000.0f;
            if (mode == 2) t.wholeSound = true;
            DeEsser st; st.prepare (sr, 240, t);
            DeEsser mo; mo.prepare (sr, 240, t);
            DeEsserReference ref; ref.prepare (sr, 240, *reinterpret_cast<DeEsserReference::Settings*> (&t));
            juce::AudioBuffer<float> mono (1, 96000); mono.copyFrom (0, 0, in, 0, 0, 96000);
            float most = 0;
            auto a = run (st, in, &most), b = run (mo, mono), c = run (ref, in);
            int stereoVsMono = 0, vsReference = 0; double worst = 0;
            for (int i = 0; i < 96000; ++i)
            {
                if (a.getSample (0, i) != b.getSample (0, i) || a.getSample (1, i) != b.getSample (0, i)) ++stereoVsMono;
                if (a.getSample (0, i) != c.getSample (0, i)) ++vsReference;
                worst = std::max (worst, (double) std::abs (a.getSample (0, i) - c.getSample (0, i)));
            }
            std::cout << "mode " << mode << " (most " << most << " dB): stereo vs mono differ at " << stereoVsMono << " samples; vs the simple version at "
                      << vsReference << " (worst " << toDb ((float) worst) << " dB)\n";
        }
    }
    return 0;
}
