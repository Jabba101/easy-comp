// Verification harness for Easy Comp (not shipped). Exercises the DSP, the
// processor plumbing and the editor, and renders the editor to PNG files.

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TestHelpers.h"

#include <cmath>
#include <functional>
#include <iostream>

namespace
{
    int checks = 0, failures = 0;

    void check (bool ok, const juce::String& what)
    {
        ++checks;
        if (! ok) ++failures;
        std::cout << (ok ? "  ok    " : "  FAIL  ") << what << "\n";
    }

    bool near (float a, float b, float tol) { return std::abs (a - b) <= tol; }
    float toDb (float g) { return juce::Decibels::gainToDecibels (g, -200.0f); }
    float fromDb (float d) { return juce::Decibels::decibelsToGain (d, -200.0f); }

    const std::array<const char*, 13> allParams { CompIDs::threshold, CompIDs::ratio, CompIDs::attack, CompIDs::release,
                                                  CompIDs::makeup, CompIDs::mix, CompIDs::lowCut, CommonIDs::autoGain, CommonIDs::bypass,
                                                  CompIDs::reactsTo, CompIDs::character, CompIDs::lowEnd, CompIDs::listensTo };

    void setParam (EasyCompAudioProcessor& p, const char* id, float value)
    {
        auto* prm = p.parameters.getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    }

    void loadPresetNumber (EasyCompAudioProcessor& p, int number)
    {
        p.loadPreset (p.getBuiltInPresets()[(size_t) number]);
    }

    void loadPresetNamed (EasyCompAudioProcessor& p, const juce::String& name)
    {
        for (auto& preset : p.getBuiltInPresets())
            if (preset.name == name) { p.loadPreset (preset); return; }

        std::cout << "  (no preset called " << name << ")\n";
        jassertfalse;
    }

    float getParam (EasyCompAudioProcessor& p, const char* id)
    {
        return p.parameters.getRawParameterValue (id)->load();
    }

    juce::AudioBuffer<float> sine (double sr, double freq, float amp, double seconds, int channels = 2)
    {
        const int n = (int) (sr * seconds);
        juce::AudioBuffer<float> b (channels, n);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < n; ++i)
                b.setSample (ch, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / sr));
        return b;
    }

    float peak (const juce::AudioBuffer<float>& b, int ch, int start, int end)
    {
        float m = 0.0f;
        for (int i = start; i < end; ++i)
            m = std::max (m, std::abs (b.getSample (ch, i)));
        return m;
    }

    float maxDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        float d = 0.0f;
        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
                d = std::max (d, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
        return d;
    }

    // Runs a signal through a processor in blocks, optionally calling a hook before each block.
    juce::AudioBuffer<float> run (EasyCompAudioProcessor& p, const juce::AudioBuffer<float>& input, int block,
                                  std::function<void (int)> beforeBlock = {})
    {
        juce::AudioBuffer<float> out (input);
        juce::MidiBuffer midi;
        for (int start = 0; start < out.getNumSamples(); start += block)
        {
            if (beforeBlock) beforeBlock (start);
            const int n = std::min (block, out.getNumSamples() - start);
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), out.getNumChannels(), start, n);
            p.processBlock (view, midi);
        }
        return out;
    }

    void prepare (EasyCompAudioProcessor& p, double sr, int block, int channels = 2)
    {
        p.setPlayConfigDetails (channels, channels, sr, block);
        p.prepareToPlay (sr, block);
    }

    //==========================================================================
    // A simple, repeatable drum pattern at 120 BPM: kick on beats 1 and 3,
    // snare on 2 and 4, hi-hat on every 8th note. Every whole second holds
    // exactly one kick and one snare.
    juce::AudioBuffer<float> drumLoop (double sr, double seconds, float kickDb = -6.0f, float snareDb = -10.0f, float hatDb = -24.0f)
    {
        const int n = (int) (sr * seconds);
        juce::AudioBuffer<float> b (2, n);
        juce::Random rng (7);
        constexpr double beat = 0.5;
        const double twoPi = juce::MathConstants<double>::twoPi;
        const double kick = (double) fromDb (kickDb), snare = (double) fromDb (snareDb), hat = (double) fromDb (hatDb);

        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            const int beatIndex = (int) (t / beat);
            const double sinceBeat = t - (double) beatIndex * beat;
            const double sinceEighth = std::fmod (t, beat * 0.5);
            const double noise = rng.nextDouble() * 2.0 - 1.0;
            double x = 0.0;

            if (beatIndex % 2 == 0)
                x += kick * std::exp (-sinceBeat / 0.12) * std::sin (twoPi * 55.0 * sinceBeat);
            else
                x += snare * std::exp (-sinceBeat / 0.08) * (0.6 * std::sin (twoPi * 190.0 * sinceBeat) + 0.4 * noise);

            x += hat * std::exp (-sinceEighth / 0.02) * noise;

            b.setSample (0, i, (float) x);
            b.setSample (1, i, (float) (x * 0.9));
        }
        return b;
    }

    /** Independent loudness measurement using the published 48 kHz K-weighting
        coefficients from ITU-R BS.1770 - deliberately not the plugin's own code. */
    double loudnessDb (const juce::AudioBuffer<float>& b, int start, int end)
    {
        struct Biquad
        {
            double b0, b1, b2, a1, a2, z1 = 0.0, z2 = 0.0;
            double process (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
        };

        double sum = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            Biquad shelf    { 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585 };
            Biquad highPass { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 };
            for (int i = 0; i < end; ++i)
            {
                const double y = highPass.process (shelf.process ((double) b.getSample (ch, i)));
                if (i >= start) sum += y * y;
            }
        }
        return 10.0 * std::log10 (sum / (double) (end - start) + 1e-30);
    }

    double rmsDb (const juce::AudioBuffer<float>& b, int start, int end)
    {
        double sum = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = start; i < end; ++i)
                sum += (double) b.getSample (ch, i) * (double) b.getSample (ch, i);
        return 10.0 * std::log10 (sum / (double) (end - start) + 1e-30);
    }

    float worstGainStep (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out, int from)
    {
        float worst = 0.0f, prevGain = -1.0f;
        for (int i = from; i < in.getNumSamples(); ++i)
        {
            const float x = in.getSample (0, i);
            if (std::abs (x) < 0.05f) { prevGain = -1.0f; continue; }
            const float gain = out.getSample (0, i) / x;
            if (prevGain > 0.0f) worst = std::max (worst, std::abs (gain - prevGain));
            prevGain = gain;
        }
        return worst;
    }

    constexpr int second = 48000;

    //==========================================================================
    void testParametersAndText()
    {
        std::cout << "\n[parameters, defaults and value text]\n";
        EasyCompAudioProcessor p;

        check (p.getParameters().size() == 14, "14 settings visible to the DAW (7 knobs, 2 buttons, REACTS TO, STYLE, LOW END, LISTENS TO, WORKS ON)");
        check (p.getBypassParameter() != nullptr && p.getBypassParameter() == p.parameters.getParameter (CommonIDs::bypass),
               "the BYPASS button is also the DAW's own bypass switch");
        check (getParam (p, CommonIDs::autoGain) < 0.5f && getParam (p, CommonIDs::bypass) < 0.5f, "AUTO-GAIN and BYPASS start off");
        check (near (getParam (p, CompIDs::threshold), -20.0f, 1e-4f), "default threshold -20 dB");
        check (near (getParam (p, CompIDs::ratio), 4.0f, 1e-4f),        "default ratio 4:1");
        check (near (getParam (p, CompIDs::attack), 10.0f, 1e-3f),      "default attack 10 ms");
        check (near (getParam (p, CompIDs::release), 100.0f, 1e-3f),    "default release 100 ms");
        check (near (getParam (p, CompIDs::makeup), 0.0f, 1e-4f),       "default make-up 0 dB");
        check (near (getParam (p, CompIDs::mix), 100.0f, 1e-4f),        "default mix 100%");
        check (near (getParam (p, CompIDs::lowCut), 20.0f, 1e-4f),      "default ignore-below = off (20 Hz)");
        check (getParam (p, CompIDs::character) < 0.5f && getParam (p, CompIDs::lowEnd) < 0.5f && getParam (p, CompIDs::listensTo) < 0.5f,
               "STYLE starts on MODERN, LOW END on NORMAL, LISTENS TO on THIS TRACK");

        auto text = [&] (const char* id, float v)
        {
            auto* prm = p.parameters.getParameter (id);
            return prm->getText (prm->convertTo0to1 (v), 0);
        };

        auto expectText = [&] (const char* id, float v, const char* expected)
        {
            const auto got = text (id, v);
            check (got == expected, juce::String (id) + " " + juce::String (v) + " -> '" + got + "' (want '" + expected + "')");
        };

        expectText (CompIDs::threshold, -20.0f, "-20.0 dB");
        expectText (CompIDs::ratio, 4.0f, "4.0 : 1");
        expectText (CompIDs::ratio, 20.0f, "20.0 : 1");
        expectText (CompIDs::attack, 0.5f, "0.50 ms");
        expectText (CompIDs::attack, 5.0f, "5.0 ms");
        expectText (CompIDs::attack, 25.0f, "25 ms");
        expectText (CompIDs::release, 100.0f, "100 ms");
        expectText (CompIDs::release, 1000.0f, "1000 ms");
        expectText (CompIDs::makeup, 0.0f, "0.0 dB");
        expectText (CompIDs::makeup, 6.0f, "+6.0 dB");
        expectText (CompIDs::mix, 100.0f, "100%");
        expectText (CompIDs::mix, 35.0f, "35%");
        expectText (CompIDs::lowCut, 20.0f, "Off");
        expectText (CompIDs::lowCut, 100.0f, "100 Hz");
        expectText (CompIDs::lowCut, 500.0f, "500 Hz");
        expectText (CompIDs::character, 0.0f, "Modern");
        expectText (CompIDs::character, 1.0f, "Vintage");
        expectText (CompIDs::lowEnd, 0.0f, "Normal");
        expectText (CompIDs::lowEnd, 1.0f, "Thrust");
        expectText (CompIDs::listensTo, 0.0f, "This Track");
        expectText (CompIDs::listensTo, 1.0f, "Sidechain");

        auto* thr = p.parameters.getParameter (CompIDs::threshold);
        check (near (thr->convertFrom0to1 (thr->getValueForText ("-15")), -15.0f, 1e-3f), "typing '-15' into threshold gives -15 dB");
        check (near (thr->convertFrom0to1 (thr->getValueForText ("-12.5 dB")), -12.5f, 1e-3f), "typing '-12.5 dB' gives -12.5 dB");

        auto* lc = p.parameters.getParameter (CompIDs::lowCut);
        check (near (lc->convertFrom0to1 (lc->getValueForText ("off")), 20.0f, 1e-3f), "typing 'off' into ignore-below switches it off");
        check (near (lc->convertFrom0to1 (lc->getValueForText ("120 Hz")), 120.0f, 1e-3f), "typing '120 Hz' gives 120 Hz");

        auto* ratio = p.parameters.getParameter (CompIDs::ratio);
        check (near (ratio->convertFrom0to1 (0.5f), 4.0f, 0.11f), "ratio knob halfway = ~4:1 (smooth curve)");
        auto* attack = p.parameters.getParameter (CompIDs::attack);
        check (near (attack->convertFrom0to1 (0.5f), 10.0f, 0.02f), "attack knob halfway = ~10 ms");
        auto* release = p.parameters.getParameter (CompIDs::release);
        check (near (release->convertFrom0to1 (0.5f), 100.0f, 1.0f), "release knob halfway = ~100 ms");
        check (near (lc->convertFrom0to1 (0.5f), 100.0f, 1.0f), "ignore-below knob halfway = ~100 Hz");
        check (near (thr->convertFrom0to1 (0.0f), -60.0f, 1e-4f) && near (thr->convertFrom0to1 (1.0f), 0.0f, 1e-4f), "threshold range -60..0 dB");
        check (near (ratio->convertFrom0to1 (0.0f), 1.0f, 1e-4f) && near (ratio->convertFrom0to1 (1.0f), 20.0f, 1e-4f), "ratio range 1..20");
        check (near (attack->convertFrom0to1 (0.0f), 0.1f, 1e-4f) && near (attack->convertFrom0to1 (1.0f), 100.0f, 1e-3f), "attack range 0.1..100 ms");
        check (near (release->convertFrom0to1 (0.0f), 10.0f, 1e-4f) && near (release->convertFrom0to1 (1.0f), 1000.0f, 1e-2f), "release range 10..1000 ms");
        check (near (lc->convertFrom0to1 (0.0f), 20.0f, 1e-4f) && near (lc->convertFrom0to1 (1.0f), 500.0f, 1e-2f), "ignore-below range off..500 Hz");
        auto* mk = p.parameters.getParameter (CompIDs::makeup);
        check (near (mk->convertFrom0to1 (1.0f), 24.0f, 1e-4f), "make-up range 0..24 dB");
        auto* mx = p.parameters.getParameter (CompIDs::mix);
        check (near (mx->convertFrom0to1 (1.0f), 100.0f, 1e-4f), "mix range 0..100%");
        check (p.getLatencySamples() == 0, "zero latency reported");
    }

    void testGainComputerAndCurve()
    {
        std::cout << "\n[gain computer and the graph's curve maths]\n";
        check (near (Compressor::gainReductionFor (-30.0f, -20.0f, 4.0f), 0.0f, 1e-6f), "well below threshold: no reduction");
        check (near (Compressor::gainReductionFor (-10.0f, -20.0f, 4.0f), 7.5f, 1e-4f), "10 dB over at 4:1 -> 7.5 dB reduction");
        check (near (Compressor::gainReductionFor (-10.0f, -20.0f, 1.0f), 0.0f, 1e-6f), "ratio 1:1 -> no reduction");
        check (near (Compressor::gainReductionFor (0.0f, -20.0f, 20.0f), 19.0f, 1e-4f), "20 dB over at 20:1 -> 19 dB reduction");

        float prev = 0.0f;
        bool monotonic = true, continuous = true;
        for (int i = 0; i <= 4000; ++i)
        {
            const float level = -40.0f + 0.01f * (float) i;
            const float gr = Compressor::gainReductionFor (level, -20.0f, 4.0f);
            if (gr + 1e-6f < prev) monotonic = false;
            if (gr - prev > 0.01f) continuous = false;
            prev = gr;
        }
        check (monotonic, "curve never decreases as level rises");
        check (continuous, "curve has no jumps (soft knee joins smoothly)");
        check (near (Compressor::gainReductionFor (-20.0f, -20.0f, 4.0f), 0.75f * 3.0f * 3.0f / 12.0f, 1e-4f), "at the threshold, knee gives ~0.56 dB");

        Compressor::Settings s;   // -20 dB, 4:1, no make-up, 100% mix
        check (near (Compressor::staticOutputDb (-60.0f, s), -60.0f, 1e-4f), "graph: quiet sound comes out unchanged");
        check (near (Compressor::staticOutputDb (0.0f, s), -15.0f, 1e-3f), "graph: 0 dB in -> -15 dB out at -20 dB / 4:1");
        check (near (Compressor::staticOutputDb (-20.0f, s), -20.5625f, 1e-3f), "graph: knee at the threshold");
        s.makeupDb = 6.0f;
        check (near (Compressor::staticOutputDb (0.0f, s), -9.0f, 1e-3f) && near (Compressor::staticOutputDb (-60.0f, s), -54.0f, 1e-3f),
               "graph: +6 dB make-up lifts the whole curve by 6 dB");
        s.mix = 0.0f;
        check (near (Compressor::staticOutputDb (-7.0f, s), -7.0f, 1e-4f), "graph: mix 0% = straight 1:1 line");
        Compressor::Settings parallel; parallel.thresholdDb = -60.0f; parallel.ratio = 20.0f; parallel.mix = 0.5f;
        check (near (Compressor::staticOutputDb (0.0f, parallel), toDb (0.5f + 0.5f * fromDb (-57.0f)), 1e-3f), "graph: parallel mix blends correctly");
    }

    void testBelowAndAboveThreshold()
    {
        std::cout << "\n[processor: below / above threshold]\n";
        {
            EasyCompAudioProcessor p; prepare (p, 48000.0, 512);
            const auto in = sine (48000.0, 1000.0, fromDb (-30.0f), 1.0);
            const auto out = run (p, in, 512);
            const float maxDiff = maxDifference (in, out);
            check (maxDiff < 1e-6f, "-30 dBFS sine with threshold -20: untouched (max diff " + juce::String (maxDiff) + ")");
            check (p.takeGainReductionForMeter() < 1e-4f, "meter reads 0 below threshold");
        }
        {
            EasyCompAudioProcessor p; prepare (p, 48000.0, 512);
            const auto in = sine (48000.0, 1000.0, fromDb (-6.0f), 1.0);
            const auto out = run (p, in, 512);
            const float outDb = toDb (peak (out, 0, 24000, 48000));
            check (outDb > -16.6f && outDb < -15.0f, "-6 dBFS sine, -20 dB / 4:1 / 10 ms -> output peak " + juce::String (outDb, 2) + " dBFS (static curve -16.5)");
            const float meter = p.takeGainReductionForMeter();
            check (meter > 9.0f && meter < 10.6f, "meter shows " + juce::String (meter, 2) + " dB reduction (static curve 10.5)");
            check (p.takeGainReductionForMeter() < 1e-6f, "meter value resets after being read");
        }
        // The graph shows the static curve: with fast attack / slow release the audio should land on it,
        // including make-up and mix.
        for (auto [makeup, mix] : { std::pair<float, float> { 0.0f, 100.0f }, { 6.0f, 100.0f }, { 4.0f, 50.0f } })
        {
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::attack, 0.1f); setParam (p, CompIDs::release, 1000.0f);
            setParam (p, CompIDs::makeup, makeup); setParam (p, CompIDs::mix, mix);
            prepare (p, 48000.0, 512);
            const auto in = sine (48000.0, 1000.0, fromDb (-6.0f), 1.0);
            const auto out = run (p, in, 512);
            const float outDb = toDb (peak (out, 0, 24000, 48000));
            const float graphDb = Compressor::staticOutputDb (toDb (peak (in, 0, 24000, 48000)), p.getCurrentSettings());
            check (near (outDb, graphDb, 0.3f), "audio matches the graph (make-up " + juce::String (makeup, 0) + " dB, mix " + juce::String (mix, 0)
                                                    + "%): " + juce::String (outDb, 2) + " vs " + juce::String (graphDb, 2) + " dB");
        }
    }

    void testTransparentSettings()
    {
        std::cout << "\n[transparent settings]\n";
        auto maxDiffFor = [] (std::function<void (EasyCompAudioProcessor&)> setup)
        {
            EasyCompAudioProcessor p;
            setup (p);
            prepare (p, 48000.0, 256);
            const auto in = sine (48000.0, 440.0, fromDb (-3.0f), 0.5);
            return maxDifference (in, run (p, in, 256));
        };

        const float ratioOne = maxDiffFor ([] (auto& p) { setParam (p, CompIDs::ratio, 1.0f); setParam (p, CompIDs::threshold, -60.0f); });
        check (ratioOne < 1e-6f, "ratio 1:1 leaves audio untouched (max diff " + juce::String (ratioOne) + ")");

        const float mixZero = maxDiffFor ([] (auto& p) { setParam (p, CompIDs::mix, 0.0f); setParam (p, CompIDs::threshold, -60.0f);
                                                         setParam (p, CompIDs::ratio, 20.0f); setParam (p, CompIDs::makeup, 24.0f); });
        check (mixZero < 1e-6f, "mix 0% leaves audio untouched even with heavy settings (max diff " + juce::String (mixZero) + ")");
    }

    void testMakeupAndMix()
    {
        std::cout << "\n[make-up gain and mix]\n";
        {
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::makeup, 6.0f);
            prepare (p, 48000.0, 512);
            const auto in = sine (48000.0, 1000.0, fromDb (-40.0f), 0.5);
            const auto out = run (p, in, 512);
            const float gainDb = toDb (peak (out, 0, 12000, 24000)) - toDb (peak (in, 0, 12000, 24000));
            check (near (gainDb, 6.0f, 0.02f), "+6 dB make-up below threshold adds 6 dB (got " + juce::String (gainDb, 3) + ")");
        }
        {
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::threshold, -60.0f); setParam (p, CompIDs::ratio, 20.0f);
            setParam (p, CompIDs::attack, 0.1f); setParam (p, CompIDs::release, 1000.0f); setParam (p, CompIDs::mix, 50.0f);
            prepare (p, 48000.0, 512);
            const auto in = sine (48000.0, 1000.0, fromDb (-6.0f), 1.0);
            const auto out = run (p, in, 512);
            const float ratioOut = peak (out, 0, 24000, 48000) / peak (in, 0, 24000, 48000);
            check (ratioOut > 0.5f && ratioOut < 0.53f, "mix 50% with a heavily squashed copy -> ~0.5x of the original (" + juce::String (ratioOut, 4) + ")");
        }
    }

    void testAttackReleaseTiming()
    {
        std::cout << "\n[attack / release timing]\n";
        constexpr double sr = 48000.0;
        Compressor::Settings s;
        Compressor c;
        c.prepare (sr, s);

        const float target = Compressor::gainReductionFor (toDb (0.5f), -20.0f, 4.0f);
        juce::AudioBuffer<float> one (2, 1);
        std::vector<float> gr;
        for (int i = 0; i < (int) (0.4 * sr); ++i)
        {
            one.setSample (0, 0, 0.5f); one.setSample (1, 0, 0.5f);
            gr.push_back (c.process (one).maxReductionDb);
        }
        const float at10ms = gr[(size_t) (0.010 * sr) - 1];
        check (near (at10ms, target * (1.0f - std::exp (-1.0f)), 0.15f), "after 10 ms attack: " + juce::String (at10ms, 2) + " dB (theory " + juce::String (target * (1.0f - std::exp (-1.0f)), 2) + ")");
        check (near (gr.back(), target, 0.01f), "settles at " + juce::String (gr.back(), 3) + " dB (target " + juce::String (target, 3) + ")");

        std::vector<float> rel;
        for (int i = 0; i < (int) (0.3 * sr); ++i)
        {
            one.clear();
            rel.push_back (c.process (one).maxReductionDb);
        }
        const float at100ms = rel[(size_t) (0.100 * sr) - 1];
        check (near (at100ms, target * std::exp (-1.0f), 0.15f), "100 ms into release: " + juce::String (at100ms, 2) + " dB (theory " + juce::String (target * std::exp (-1.0f), 2) + ")");
    }

    //==========================================================================
    /** Max gain reduction over the last 0.5 s of a 1.5 s sine (fast attack, slow release). */
    float settledReduction (double freq, float lowCutHz, float thresholdDb, float ratio, float levelDb = -6.0f)
    {
        EasyCompAudioProcessor p;
        setParam (p, CompIDs::threshold, thresholdDb); setParam (p, CompIDs::ratio, ratio);
        setParam (p, CompIDs::attack, 0.1f); setParam (p, CompIDs::release, 1000.0f);
        setParam (p, CompIDs::lowCut, lowCutHz);
        prepare (p, 48000.0, 480);
        run (p, sine (48000.0, freq, fromDb (levelDb), 1.5), 480, [&] (int start) { if (start == 48000) p.takeGainReductionForMeter(); });
        return p.takeGainReductionForMeter();
    }

    void testLowCut()
    {
        std::cout << "\n[IGNORE BELOW (detector low-cut)]\n";

        const float kickOff = settledReduction (50.0, 20.0f, -20.0f, 4.0f);
        const float kickOn  = settledReduction (50.0, 200.0f, -20.0f, 4.0f);
        check (kickOff > 9.5f, "50 Hz 'kick' tone, ignore-below off: compressed " + juce::String (kickOff, 2) + " dB");
        check (kickOn < 0.1f, "same tone, ignore-below 200 Hz: no longer sets it off (" + juce::String (kickOn, 3) + " dB)");

        const float snareOff = settledReduction (1000.0, 20.0f, -20.0f, 4.0f);
        const float snareOn  = settledReduction (1000.0, 200.0f, -20.0f, 4.0f);
        check (near (snareOff, snareOn, 0.1f), "1 kHz 'snare' tone reacts the same either way (" + juce::String (snareOff, 2) + " vs " + juce::String (snareOn, 2) + " dB)");

        // At the cut-off frequency the filter is 3 dB down: at 20:1 that's 0.95 x 3.01 = 2.86 dB less reduction.
        const float atCutoff = settledReduction (100.0, 20.0f, -40.0f, 20.0f) - settledReduction (100.0, 100.0f, -40.0f, 20.0f);
        check (near (atCutoff, 2.86f, 0.2f), "filter is 3 dB down at its setting (reduction differs by " + juce::String (atCutoff, 2) + " dB, theory 2.86)");

        // An octave below the setting, a 12 dB/octave filter is ~12.3 dB down -> 11.7 dB less reduction at 20:1.
        const float octaveBelow = settledReduction (50.0, 20.0f, -40.0f, 20.0f) - settledReduction (50.0, 100.0f, -40.0f, 20.0f);
        check (near (octaveBelow, 0.95f * 12.30f, 0.4f), "12 dB per octave slope (reduction differs by " + juce::String (octaveBelow, 2) + " dB, theory 11.7)");

        // The audio itself is never filtered: a 30 Hz tone below threshold passes bit-for-bit with ignore-below at 500 Hz.
        {
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::lowCut, 500.0f);
            prepare (p, 48000.0, 512);
            const auto in = sine (48000.0, 30.0, fromDb (-30.0f), 1.0);
            const float diff = maxDifference (in, run (p, in, 512));
            check (diff < 1e-6f, "your sound isn't filtered: 30 Hz passes untouched with ignore-below at 500 Hz (max diff " + juce::String (diff) + ")");
        }

        // Sweeping the knob while audio plays: no clicks, no NaN.
        {
            EasyCompAudioProcessor p;
            prepare (p, 48000.0, 64);
            const auto in = sine (48000.0, 1000.0, fromDb (-6.0f), 2.0);
            int blockCount = 0;
            const auto out = run (p, in, 64, [&] (int)
            {
                const float phase = (float) (blockCount++ % 100) / 100.0f;           // fast up-down sweep
                const float hz = 20.0f + 480.0f * (phase < 0.5f ? phase * 2.0f : 2.0f - phase * 2.0f);
                setParam (p, CompIDs::lowCut, (blockCount / 1000) % 2 == 0 ? hz : 20.0f);   // and on/off toggling
            });
            float worstStep = 0.0f, prevGain = -1.0f;
            bool finite = true;
            for (int i = 4800; i < in.getNumSamples(); ++i)
            {
                if (! std::isfinite (out.getSample (0, i))) finite = false;
                const float x = in.getSample (0, i);
                if (std::abs (x) < 0.05f) { prevGain = -1.0f; continue; }
                const float gain = out.getSample (0, i) / x;
                if (prevGain > 0.0f) worstStep = std::max (worstStep, std::abs (gain - prevGain));
                prevGain = gain;
            }
            check (finite, "sweeping and toggling the knob during playback: no NaN/inf");
            check (worstStep < 0.005f, "sweeping and toggling the knob: no clicks (largest per-sample gain step " + juce::String (worstStep, 5) + ")");
        }

        // Graph level readout
        {
            EasyCompAudioProcessor p; prepare (p, 48000.0, 512);
            run (p, sine (48000.0, 1000.0, fromDb (-6.0f), 0.5), 512);
            const float level = p.takeInputLevelForGraph();
            check (near (level, -6.0f, 0.1f), "graph dot reads the level going in: " + juce::String (level, 2) + " dB");
            check (p.takeInputLevelForGraph() <= Compressor::silenceDb, "graph level resets after being read");

            EasyCompAudioProcessor q;
            setParam (q, CompIDs::lowCut, 200.0f);
            prepare (q, 48000.0, 512);
            run (q, sine (48000.0, 50.0, fromDb (-6.0f), 1.0), 512, [&] (int start) { if (start == 24064) q.takeInputLevelForGraph(); });
            const float heard = q.takeInputLevelForGraph();
            check (near (heard, -6.0f - 24.1f, 0.5f), "with ignore-below 200 Hz the dot shows what it reacts to: 50 Hz at -6 dB reads " + juce::String (heard, 2) + " dB");
        }
    }

    void testStereoLinkAndMono()
    {
        std::cout << "\n[stereo link and mono]\n";
        for (float lowCutHz : { 20.0f, 150.0f })
        {
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::lowCut, lowCutHz);
            prepare (p, 48000.0, 512);
            auto in = sine (48000.0, 200.0, fromDb (-6.0f), 1.0);
            const auto quiet = sine (48000.0, 200.0, fromDb (-40.0f), 1.0, 1);
            in.copyFrom (1, 0, quiet, 0, 0, in.getNumSamples());
            const auto out = run (p, in, 512);
            float worst = 0.0f;
            for (int i = 0; i < in.getNumSamples(); ++i)
            {
                const float l = in.getSample (0, i), r = in.getSample (1, i);
                if (std::abs (r) > 1e-3f)
                    worst = std::max (worst, std::abs (out.getSample (0, i) / l - out.getSample (1, i) / r));
            }
            check (worst < 1e-4f, "both channels get identical gain, ignore-below " + juce::String (lowCutHz, 0) + " Hz (worst mismatch " + juce::String (worst) + ")");
        }

        EasyCompAudioProcessor m; prepare (m, 48000.0, 512, 1);
        EasyCompAudioProcessor st; prepare (st, 48000.0, 512, 2);
        const auto monoIn = sine (48000.0, 300.0, fromDb (-3.0f), 0.5, 1);
        const auto monoOut = run (m, monoIn, 512);
        const auto stereoOut = run (st, sine (48000.0, 300.0, fromDb (-3.0f), 0.5, 2), 512);
        float diff = 0.0f;
        for (int i = 0; i < monoIn.getNumSamples(); ++i)
            diff = std::max (diff, std::abs (monoOut.getSample (0, i) - stereoOut.getSample (0, i)));
        check (diff < 1e-6f, "mono track gives the same result as identical stereo channels");

        EasyCompAudioProcessor p;
        using Set = juce::AudioChannelSet;
        auto layout = [] (Set in1, Set out1) { juce::AudioProcessor::BusesLayout l; l.inputBuses.add (in1); l.outputBuses.add (out1); return l; };
        check (p.isBusesLayoutSupported (layout (Set::stereo(), Set::stereo())), "stereo -> stereo supported");
        check (p.isBusesLayoutSupported (layout (Set::mono(), Set::mono())), "mono -> mono supported");
        check (! p.isBusesLayoutSupported (layout (Set::mono(), Set::stereo())), "mono -> stereo rejected");
        check (! p.isBusesLayoutSupported (layout (Set::create5point1(), Set::create5point1())), "5.1 rejected");
    }

    void testSmoothing()
    {
        std::cout << "\n[parameter smoothing (no zipper noise)]\n";
        EasyCompAudioProcessor p; prepare (p, 48000.0, 480);
        const auto in = sine (48000.0, 100.0, fromDb (-40.0f), 0.5);
        const int switchAt = 9600;
        const auto out = run (p, in, 480, [&] (int start) { if (start == switchAt) setParam (p, CompIDs::makeup, 24.0f); });

        float worstStep = 0.0f, prevGain = -1.0f;
        for (int i = switchAt - 480; i < in.getNumSamples(); ++i)
        {
            const float x = in.getSample (0, i);
            if (std::abs (x) < 2e-3f) { prevGain = -1.0f; continue; }
            const float gain = out.getSample (0, i) / x;
            if (prevGain > 0.0f) worstStep = std::max (worstStep, std::abs (gain - prevGain));
            prevGain = gain;
        }
        const float gainAfter25ms = out.getSample (0, switchAt + 1200 + 120) / in.getSample (0, switchAt + 1200 + 120);
        const float finalGain = peak (out, 0, 24000 - 4800, 24000) / peak (in, 0, 24000 - 4800, 24000);
        check (worstStep < 0.05f, "make-up jump of +24 dB glides (largest per-sample gain step " + juce::String (worstStep, 4) + ", an instant jump would be 14.8)");
        check (gainAfter25ms > 1.5f && gainAfter25ms < 14.0f, "halfway through the 50 ms glide the gain is in between (" + juce::String (gainAfter25ms, 2) + "x)");
        check (near (toDb (finalGain), 24.0f, 0.05f), "glide arrives at +24 dB (" + juce::String (toDb (finalGain), 3) + " dB)");
    }

    void testExtremes()
    {
        std::cout << "\n[extreme settings and nasty input]\n";
        for (float lowCutHz : { 20.0f, 500.0f })
        {
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::threshold, -60.0f); setParam (p, CompIDs::ratio, 20.0f); setParam (p, CompIDs::attack, 0.1f);
            setParam (p, CompIDs::release, 10.0f); setParam (p, CompIDs::makeup, 24.0f); setParam (p, CompIDs::mix, 100.0f);
            setParam (p, CompIDs::lowCut, lowCutHz);
            prepare (p, 44100.0, 64);

            juce::Random rng (42);
            juce::AudioBuffer<float> in (2, 44100);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < in.getNumSamples(); ++i)
                {
                    float v = 0.0f;
                    if (i < 11025)        v = (rng.nextFloat() * 2.0f - 1.0f) * 4.0f;
                    else if (i < 22050)   v = 0.0f;
                    else if (i < 33075)   v = (i % 4410 == 0) ? 1.0f : 1.0e-38f;
                    else                  v = (rng.nextFloat() * 2.0f - 1.0f) * 1.0e-6f;
                    in.setSample (ch, i, v);
                }
            const auto out = run (p, in, 64);
            bool allFinite = true;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < out.getNumSamples(); ++i)
                    if (! std::isfinite (out.getSample (ch, i))) allFinite = false;
            const juce::String label = lowCutHz > 20.0f ? " (ignore-below 500 Hz)" : "";
            check (allFinite, "no NaN/inf with +12 dBFS noise, silence, clicks, tiny values at the most extreme settings" + label);
            check (peak (out, 0, 11025, 22050) == 0.0f, "silence in -> silence out" + label);
        }

        EasyCompAudioProcessor p; prepare (p, 48000.0, 64);
        juce::AudioBuffer<float> empty (2, 0);
        juce::MidiBuffer midi;
        p.processBlock (empty, midi);
        check (true, "zero-length block handled");

        for (float lowCutHz : { 20.0f, 120.0f })
        {
            EasyCompAudioProcessor a; setParam (a, CompIDs::lowCut, lowCutHz); prepare (a, 48000.0, 1024);
            EasyCompAudioProcessor b; setParam (b, CompIDs::lowCut, lowCutHz); prepare (b, 48000.0, 1024);
            const auto sig = sine (48000.0, 150.0, fromDb (-4.0f), 0.25);
            const auto outA = run (a, sig, 1024);
            juce::AudioBuffer<float> outB (sig);
            juce::MidiBuffer mb;
            for (int start = 0, size = 1; start < outB.getNumSamples(); start += size, size = (size * 7) % 997 + 1)
            {
                const int n = std::min (size, outB.getNumSamples() - start);
                juce::AudioBuffer<float> view (outB.getArrayOfWritePointers(), 2, start, n);
                b.processBlock (view, mb);
            }
            const float diff = maxDifference (outA, outB);
            check (diff < 1e-5f, "result doesn't depend on the DAW's buffer size, ignore-below " + juce::String (lowCutHz, 0) + " Hz (max diff " + juce::String (diff) + ")");
        }
    }

    void testSampleRates()
    {
        std::cout << "\n[sample rates]\n";
        for (float lowCutHz : { 20.0f, 100.0f })
        {
            float reference = 0.0f;
            for (double sr : { 48000.0, 44100.0, 88200.0, 96000.0, 192000.0 })
            {
                EasyCompAudioProcessor p;
                setParam (p, CompIDs::lowCut, lowCutHz);
                prepare (p, sr, 512);
                const auto in = sine (sr, lowCutHz > 20.0f ? 100.0 : 1000.0, fromDb (-6.0f), 1.0);
                const auto out = run (p, in, 512);
                const int half = in.getNumSamples() / 2;
                const float outDb = toDb (peak (out, 0, half, in.getNumSamples()));
                if (reference == 0.0f) reference = outDb;
                check (near (outDb, reference, 0.1f), juce::String (sr, 0) + " Hz sounds the same" + (lowCutHz > 20.0f ? juce::String (" (ignore-below on)") : juce::String())
                                                          + ": output peak " + juce::String (outDb, 2) + " dBFS");
            }
        }
    }

    void testStateAndPresets()
    {
        std::cout << "\n[saving/loading and presets]\n";
        EasyCompAudioProcessor a;
        setParam (a, CompIDs::threshold, -33.3f); setParam (a, CompIDs::ratio, 7.5f); setParam (a, CompIDs::attack, 3.3f);
        setParam (a, CompIDs::release, 333.0f); setParam (a, CompIDs::makeup, 4.4f); setParam (a, CompIDs::mix, 55.0f);
        setParam (a, CompIDs::lowCut, 150.0f); setParam (a, CommonIDs::autoGain, 1.0f); setParam (a, CompIDs::reactsTo, 1.0f);
        setParam (a, CompIDs::character, 1.0f); setParam (a, CompIDs::lowEnd, 1.0f); setParam (a, CompIDs::listensTo, 1.0f);
        juce::MemoryBlock state;
        a.getStateInformation (state);

        EasyCompAudioProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());
        bool same = true;
        for (auto id : allParams)
            if (! near (getParam (a, id), getParam (b, id), 1e-4f)) same = false;
        check (same, "all 13 settings survive save -> reopen of a DAW project");

        b.setStateInformation ("garbage", 7);
        check (near (getParam (b, CompIDs::threshold), -33.3f, 1e-3f), "corrupt state data is ignored safely");

        // A project saved with version 1.0 (no IGNORE BELOW setting)
        {
            auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
            for (auto id : { CompIDs::lowCut, CommonIDs::autoGain, CommonIDs::bypass, CompIDs::reactsTo, CompIDs::character, CompIDs::lowEnd, CompIDs::listensTo })
                if (auto* child = xml->getChildByAttribute ("id", id))
                    xml->removeChildElement (child, true);
            juce::MemoryBlock oldState;
            juce::AudioProcessor::copyXmlToBinary (*xml, oldState);

            EasyCompAudioProcessor c;
            setParam (c, CompIDs::lowCut, 300.0f);   // whatever they were set to before
            setParam (c, CommonIDs::autoGain, 1.0f);
            setParam (c, CommonIDs::bypass, 1.0f);
            setParam (c, CompIDs::reactsTo, 1.0f);
            setParam (c, CompIDs::character, 1.0f); setParam (c, CompIDs::lowEnd, 1.0f); setParam (c, CompIDs::listensTo, 1.0f);
            c.setStateInformation (oldState.getData(), (int) oldState.getSize());
            check (near (getParam (c, CompIDs::lowCut), 20.0f, 1e-4f), "a project saved with 1.0 opens with ignore-below off");
            check (getParam (c, CommonIDs::autoGain) < 0.5f && getParam (c, CommonIDs::bypass) < 0.5f,
                   "a project saved before 1.2 opens with AUTO-GAIN and BYPASS off");
            check (getParam (c, CompIDs::reactsTo) < 0.5f, "a project saved before 1.3 opens with REACTS TO on PEAKS");
            check (getParam (c, CompIDs::character) < 0.5f && getParam (c, CompIDs::lowEnd) < 0.5f && getParam (c, CompIDs::listensTo) < 0.5f,
                   "a project saved before 1.6 opens on MODERN, NORMAL and THIS TRACK - sounding exactly as it did");
            check (near (getParam (c, CompIDs::threshold), -33.3f, 1e-3f) && near (getParam (c, CompIDs::mix), 55.0f, 1e-3f),
                   "...and its other settings restored");
        }

        EasyCompAudioProcessor p;
        for (size_t i = 0; i < std::size (compressorPresets); ++i)
        {
            loadPresetNumber (p, (int) i);
            const auto& pr = compressorPresets[i];
            const bool ok = near (getParam (p, CompIDs::threshold), pr.thresholdDb, 0.051f)
                         && near (getParam (p, CompIDs::ratio), pr.ratio, 0.051f)
                         && near (getParam (p, CompIDs::attack), pr.attackMs, 0.006f)
                         && near (getParam (p, CompIDs::release), pr.releaseMs, 0.51f)
                         && near (getParam (p, CompIDs::makeup), pr.makeupDb, 0.051f)
                         && near (getParam (p, CompIDs::mix), pr.mixPercent, 0.51f)
                         && near (getParam (p, CompIDs::lowCut), pr.lowCutHz, 0.51f)
                         && near (getParam (p, CompIDs::reactsTo), (float) pr.reactsTo, 0.01f)
                         && near (getParam (p, CompIDs::character), (float) pr.style, 0.01f)
                         && near (getParam (p, CompIDs::lowEnd), (float) pr.lowEnd, 0.01f);
            check (ok, juce::String ("preset loads exactly: ") + pr.name);
        }
        check (std::size (compressorPresets) + std::size (midSidePresets) == p.getBuiltInPresets().size(), "every preset in the tables reaches the menu");
        {
            const auto [textsOk, textsReport] = th::presetTextsReport (p.getBuiltInPresets());
            check (textsOk, textsReport);
        }

        // Which preset is loaded travels with the project
        {
            EasyCompAudioProcessor q;
            check (q.getPresetName().isEmpty() && ! q.isPresetEdited(), "a fresh plugin has no preset loaded");
            loadPresetNamed (q, "Snare - Crack");
            check (q.getPresetName() == "Snare - Crack" && ! q.isPresetEdited(), "loading a preset remembers its name");
            q.markPresetEdited();
            check (q.isPresetEdited(), "moving a knob marks it as edited");
            juce::MemoryBlock saved;
            q.getStateInformation (saved);

            EasyCompAudioProcessor r;
            r.setStateInformation (saved.getData(), (int) saved.getSize());
            check (r.getPresetName() == "Snare - Crack" && r.isPresetEdited(), "reopening the project shows 'Snare - Crack (edited)' again");

            loadPresetNamed (r, "Snare - Crack");
            check (! r.isPresetEdited(), "choosing the preset again clears '(edited)'");

            // A project from 1.2 (no preset name stored)
            auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
            xml->removeAttribute ("presetName");
            xml->removeAttribute ("presetEdited");
            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary (*xml, old);
            EasyCompAudioProcessor s;
            s.setStateInformation (old.getData(), (int) old.getSize());
            float snareThreshold = 0.0f;
            for (auto& preset : compressorPresets)
                if (juce::String (preset.name) == "Snare - Crack")
                    snareThreshold = preset.thresholdDb;
            check (s.getPresetName().isEmpty() && near (getParam (s, CompIDs::threshold), snareThreshold, 1e-3f),
                   "an older project opens with its settings and no preset name");
        }

        // A preset that doesn't mention a setting puts it back to its default
        {
            EasyCompAudioProcessor q;
            setParam (q, CompIDs::reactsTo, 1.0f);
            setParam (q, CompIDs::release, 777.0f);
            setParam (q, CompIDs::character, 1.0f); setParam (q, CompIDs::lowEnd, 1.0f);
            q.loadPreset ({ "", "Partial", "", { { CompIDs::threshold, -12.0f } } });
            check (near (getParam (q, CompIDs::threshold), -12.0f, 1e-3f) && near (getParam (q, CompIDs::release), 100.0f, 1e-3f)
                     && getParam (q, CompIDs::reactsTo) < 0.5f && getParam (q, CompIDs::character) < 0.5f && getParam (q, CompIDs::lowEnd) < 0.5f,
                   "settings a preset doesn't mention go back to their defaults (so presets saved before 1.6 load as MODERN, NORMAL)");
            setParam (q, CommonIDs::autoGain, 1.0f);
            setParam (q, CompIDs::listensTo, 1.0f);
            loadPresetNamed (q, "Toms - Fuller");
            check (getParam (q, CommonIDs::autoGain) > 0.5f && getParam (q, CommonIDs::bypass) < 0.5f, "presets leave BYPASS and AUTO-GAIN alone");
            check (getParam (q, CompIDs::listensTo) > 0.5f, "...and LISTENS TO (it depends on how your tracks are routed, not the preset)");
            bool listensToStored = false;
            for (auto& [id, value] : q.getCurrentPresetValues())
                listensToStored = listensToStored || id == CompIDs::listensTo;
            check (! listensToStored, "saving a preset doesn't store LISTENS TO");
            loadPresetNamed (q, "Drum Bus - Vintage Glue");
            check (getParam (q, CompIDs::character) > 0.5f && getParam (q, CompIDs::lowEnd) > 0.5f, "a VINTAGE + THRUST preset switches both on");
        }
    }

    void testReactsTo()
    {
        std::cout << "\n[REACTS TO: PEAKS / AVERAGE]\n";
        constexpr double sr = 48000.0;

        // A steady tone reads the same either way
        auto steadyReduction = [&] (bool average)
        {
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::reactsTo, average ? 1.0f : 0.0f);
            setParam (p, CompIDs::attack, 0.1f); setParam (p, CompIDs::release, 1000.0f);
            prepare (p, sr, 480);
            run (p, sine (sr, 1000.0, fromDb (-6.0f), 1.5), 480, [&] (int start) { if (start == 48000) p.takeGainReductionForMeter(); });
            return p.takeGainReductionForMeter();
        };
        const float steadyPeaks = steadyReduction (false), steadyAverage = steadyReduction (true);
        check (near (steadyPeaks, steadyAverage, 0.3f), "a steady tone gets the same reduction either way ("
                                                          + juce::String (steadyPeaks, 2) + " vs " + juce::String (steadyAverage, 2) + " dB)");

        // A short, sharp click: AVERAGE barely reacts, PEAKS clamps down on it
        auto clickReduction = [&] (bool average)
        {
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::reactsTo, average ? 1.0f : 0.0f);
            setParam (p, CompIDs::threshold, -20.0f); setParam (p, CompIDs::ratio, 8.0f);
            setParam (p, CompIDs::attack, 0.1f);
            prepare (p, sr, 480);
            juce::AudioBuffer<float> clicks (2, 48000);
            clicks.clear();
            for (int hit = 0; hit < 4; ++hit)
                for (int i = 0; i < 48; ++i)   // 1 ms clicks at -3 dBFS
                    for (int ch = 0; ch < 2; ++ch)
                        clicks.setSample (ch, hit * 12000 + i, fromDb (-3.0f) * (i % 2 == 0 ? 1.0f : -1.0f));
            run (p, clicks, 480);
            return p.takeGainReductionForMeter();
        };
        const float clickPeaks = clickReduction (false), clickAverage = clickReduction (true);
        // (Theory: a 1 ms click fills a 15 ms average to -11.9 dB, i.e. 8.1 dB over the threshold -> 7.1 dB at 8:1.)
        check (clickPeaks > 12.0f && clickAverage < clickPeaks - 6.0f && near (clickAverage, 7.1f, 0.5f),
               "a 1 ms click: PEAKS clamps " + juce::String (clickPeaks, 1) + " dB, AVERAGE only " + juce::String (clickAverage, 1) + " dB (theory 7.1)");

        // On drums, AVERAGE reacts less to the sharp hits
        {
            float reduction[2] {};
            for (int mode = 0; mode < 2; ++mode)
            {
                EasyCompAudioProcessor p;
                loadPresetNumber (p, 0);
                setParam (p, CompIDs::reactsTo, (float) mode);
                setParam (p, CompIDs::attack, 0.5f);
                prepare (p, sr, 480);
                run (p, drumLoop (sr, 4.0), 480, [&] (int start) { if (start == 2 * second) p.takeGainReductionForMeter(); });
                reduction[mode] = p.takeGainReductionForMeter();
            }
            check (reduction[1] < reduction[0] - 1.0f, "on drums with a fast attack, AVERAGE squeezes the hits less ("
                                                         + juce::String (reduction[1], 1) + " vs " + juce::String (reduction[0], 1) + " dB)");
        }

        // Switching while playing: smooth, no clicks
        {
            EasyCompAudioProcessor p;
            prepare (p, sr, 256);
            const auto in = sine (sr, 300.0, fromDb (-3.0f), 3.0);
            const auto out = run (p, in, 256, [&] (int start) { setParam (p, CompIDs::reactsTo, (start / 12288) % 2 == 0 ? 0.0f : 1.0f); });
            const float step = worstGainStep (in, out, 4800);
            check (step < 0.005f, "switching PEAKS/AVERAGE while playing: no clicks (largest per-sample gain step " + juce::String (step, 5) + ")");
        }

        // AUTO-GAIN still matches in AVERAGE mode
        {
            EasyCompAudioProcessor p;
            loadPresetNamed (p, "Room Mics - Crushed");
            setParam (p, CompIDs::reactsTo, 1.0f);
            setParam (p, CommonIDs::autoGain, 1.0f);
            prepare (p, sr, 480);
            const auto loop = drumLoop (sr, 12.0);
            const auto out = run (p, loop, 480);
            const double diff = loudnessDb (out, 6 * second, 12 * second) - loudnessDb (loop, 6 * second, 12 * second);
            check (std::abs (diff) < 0.5, "AUTO-GAIN matches in AVERAGE mode too (" + juce::String (diff, 2) + " dB)");
        }

        // The graph's dot shows the level it's reacting to
        {
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::reactsTo, 1.0f);
            prepare (p, sr, 512);
            run (p, sine (sr, 1000.0, fromDb (-6.0f), 0.5), 512);
            const float level = p.takeInputLevelForGraph();
            check (near (level, -6.0f, 0.3f), "the graph's dot reads a steady -6 dB tone as " + juce::String (level, 2) + " dB in AVERAGE mode");
        }
    }

    //==========================================================================
    juce::MouseEvent hoverEvent (juce::Component* c)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (source, {}, {}, juce::MouseInputSource::defaultPressure,
                                 juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                 juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                 c, c, now, {}, now, 0, false);
    }

    void savePng (juce::Component& c, const juce::File& file, float scale)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::cout << "  wrote " << file.getFullPathName() << " (" << image.getWidth() << "x" << image.getHeight() << ")\n";
    }


    //==========================================================================
    void testBypass()
    {
        std::cout << "\n[BYPASS]\n";
        constexpr double sr = 48000.0;
        const auto loop = drumLoop (sr, 4.0);

        {
            EasyCompAudioProcessor p;
            loadPresetNamed (p, "Room Mics - Crushed");   // 12:1 and +12 dB make-up
            setParam (p, CommonIDs::autoGain, 1.0f);
            setParam (p, CommonIDs::bypass, 1.0f);
            prepare (p, sr, 512);
            const auto out = run (p, loop, 512);
            check (maxDifference (loop, out) == 0.0f, "bypassed: output is the original, bit for bit (heavy preset, AUTO-GAIN on)");
            check (p.takeGainReductionForMeter() == 0.0f, "bypassed: the meter reads 0");
        }
        {
            EasyCompAudioProcessor p;
            prepare (p, sr, 256);
            const auto in = sine (sr, 1000.0, fromDb (-6.0f), 2.0);
            const auto out = run (p, in, 256, [&] (int start) { setParam (p, CommonIDs::bypass, (start / 12288) % 2 == 0 ? 0.0f : 1.0f); });
            const float step = worstGainStep (in, out, 4800);
            check (step < 0.005f, "flicking BYPASS on and off: smooth fades, no clicks (largest per-sample gain step " + juce::String (step, 5) + ")");
            float diff = 0.0f;
            for (int i = 12288 + 1200; i < 24576; ++i)
                diff = std::max (diff, std::abs (out.getSample (0, i) - in.getSample (0, i)));
            check (diff == 0.0f, "once the 20 ms fade is done, bypassed is exactly the original");
        }
        {
            EasyCompAudioProcessor p;
            loadPresetNamed (p, "Room Mics - Crushed");
            setParam (p, CommonIDs::autoGain, 1.0f);
            setParam (p, CommonIDs::bypass, 1.0f);
            prepare (p, sr, 480);
            const auto loop10 = drumLoop (sr, 10.0);
            const auto out = run (p, loop10, 480, [&] (int start) { if (start == 6 * second) setParam (p, CommonIDs::bypass, 0.0f); });
            const double firstSecond = loudnessDb (out, 6 * second, 7 * second) - loudnessDb (loop10, 6 * second, 7 * second);
            check (std::abs (firstSecond) < 0.75, "switching BYPASS off with AUTO-GAIN on: level-matched from the very first second ("
                                                    + juce::String (firstSecond, 2) + " dB)");
        }
    }

    void testAutoGain()
    {
        std::cout << "\n[AUTO-GAIN]\n";
        constexpr double sr = 48000.0;
        const auto loop = drumLoop (sr, 12.0);

        for (const char* preset : { "Default settings", "Kick - Punch", "Snare - Crack", "Room Mics - Crushed", "Drum Bus - Glue",
                                    "Drum Bus - Parallel Smash", "Drum Bus - Hip-Hop & EDM", "Mix Bus - Glue (Pop & Rock)" })
        {
            EasyCompAudioProcessor p;
            loadPresetNamed (p, preset);
            setParam (p, CommonIDs::autoGain, 1.0f);
            prepare (p, sr, 480);
            std::vector<float> history;
            const auto out = run (p, loop, 480, [&] (int start) { if (start >= 6 * second) history.push_back (p.getAutoGainDb()); });
            const double diff = loudnessDb (out, 6 * second, 12 * second) - loudnessDb (loop, 6 * second, 12 * second);
            const auto [lo, hi] = std::minmax_element (history.begin(), history.end());
            check (std::abs (diff) < 0.5, juce::String (preset) + ": loudness out matches in to "
                                            + juce::String (diff, 2) + " dB (AUTO-GAIN adding " + juce::String (p.getAutoGainDb(), 1) + " dB)");
            check (*hi - *lo < 1.0f, "    ...and holds steady while the drums play (moves " + juce::String (*hi - *lo, 2) + " dB)");
        }

        {
            // Squash only a loud kick: matching raw signal level would get this wrong; matching as you hear it doesn't.
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::threshold, -16.0f); setParam (p, CompIDs::ratio, 20.0f);
            setParam (p, CompIDs::attack, 0.1f);      setParam (p, CompIDs::release, 60.0f);
            setParam (p, CommonIDs::autoGain, 1.0f);
            prepare (p, sr, 480);
            const auto kickHeavy = drumLoop (sr, 12.0, -2.0f, -24.0f, -16.0f);
            const auto out = run (p, kickHeavy, 480);
            const double heardDiff = loudnessDb (out, 6 * second, 12 * second) - loudnessDb (kickHeavy, 6 * second, 12 * second);
            const double rawDiff   = rmsDb (out, 6 * second, 12 * second) - rmsDb (kickHeavy, 6 * second, 12 * second);
            check (std::abs (heardDiff) < 0.5, "kick-heavy mix: matched as you hear it (" + juce::String (heardDiff, 2)
                                                 + " dB), though raw signal level differs by " + juce::String (rawDiff, 2) + " dB");
            check (std::abs (rawDiff) > 1.0, "    ...(so this test really does check the ear-weighting)");
        }

        {
            EasyCompAudioProcessor p;
            loadPresetNamed (p, "Room Mics - Crushed");   // lots of AUTO-GAIN
            setParam (p, CommonIDs::autoGain, 1.0f);
            prepare (p, sr, 480);
            const auto loop14 = drumLoop (sr, 14.0);
            float before = 0.0f;
            const auto out = run (p, loop14, 480, [&] (int start) { if (start == 7 * second) { before = p.getAutoGainDb(); loadPresetNamed (p, "Kick - Punch"); } });
            double worst = -100.0;
            for (int s = 7; s < 14; ++s)
                worst = std::max (worst, loudnessDb (out, s * second, (s + 1) * second) - loudnessDb (loop14, s * second, (s + 1) * second));
            const double settled = loudnessDb (out, 11 * second, 14 * second) - loudnessDb (loop14, 11 * second, 14 * second);
            check (worst < 1.0, "switching preset from 'Room Mics - Crushed' (AUTO-GAIN +" + juce::String (before, 1)
                                  + " dB) to 'Kick - Punch': never louder than the original (loudest second: " + juce::String (worst, 2) + " dB)");
            check (std::abs (settled) < 0.5, "    ...and matched again a few seconds later (" + juce::String (settled, 2) + " dB)");
        }

        // Turning a knob a long way by hand in half a second, from heavy to light compression:
        // the output should never jump much louder than the original.
        auto sweep = [&] (const char* what, const char* paramID, float from, float to,
                          std::function<void (EasyCompAudioProcessor&)> setup, const juce::AudioBuffer<float>& drums, double limitDb)
        {
            EasyCompAudioProcessor p;
            setup (p);
            setParam (p, paramID, from);
            setParam (p, CommonIDs::autoGain, 1.0f);
            prepare (p, sr, 480);
            float before = 0.0f;
            const auto out = run (p, drums, 480, [&] (int start)
            {
                if (start == 7 * second) before = p.getAutoGainDb();
                if (start >= 7 * second)
                    setParam (p, paramID, from + (to - from) * juce::jmin (1.0f, (float) (start - 7 * second) / (float) (second / 2)));
            });
            double worst = -100.0, worstLater = 0.0;
            juce::String perSecond;
            for (int s = 7; s < 14; ++s)
            {
                const double d = loudnessDb (out, s * second, (s + 1) * second) - loudnessDb (drums, s * second, (s + 1) * second);
                worst = std::max (worst, d);
                if (s >= 9) worstLater = std::max (worstLater, std::abs (d));
                perSecond << juce::String (d, 1) << " ";
            }
            check (worst < limitDb, juce::String ("turning ") + what + " fast by hand (AUTO-GAIN was +" + juce::String (before, 1) + " dB): loudest second "
                                      + juce::String (worst, 2) + " dB vs the original [each second: " + perSecond.trim() + "]");
            check (worstLater < 0.5, "    ...and level-matched again within 2 seconds (worst second after that: " + juce::String (worstLater, 2) + " dB)");
        };

        const auto loop14 = drumLoop (sr, 14.0);
        const auto kickHeavy14 = drumLoop (sr, 14.0, -3.0f, -14.0f, -24.0f);
        sweep ("THRESHOLD -40 -> -10 dB", CompIDs::threshold, -40.0f, -10.0f, [] (auto& p) { setParam (p, CompIDs::ratio, 8.0f); }, loop14, 1.5);
        sweep ("RATIO 20:1 -> 1.5:1", CompIDs::ratio, 20.0f, 1.5f, [] (auto& p) { setParam (p, CompIDs::threshold, -30.0f); }, loop14, 1.5);
        sweep ("MIX 100% -> 30%", CompIDs::mix, 100.0f, 30.0f, [] (auto& p) { setParam (p, CompIDs::threshold, -40.0f); setParam (p, CompIDs::ratio, 10.0f); }, loop14, 1.5);
        sweep ("ATTACK 1 -> 60 ms", CompIDs::attack, 1.0f, 60.0f, [] (auto& p) { setParam (p, CompIDs::threshold, -30.0f); setParam (p, CompIDs::ratio, 8.0f); }, loop14, 4.0);
        sweep ("IGNORE BELOW off -> 300 Hz", CompIDs::lowCut, 20.0f, 300.0f, [] (auto& p) { setParam (p, CompIDs::threshold, -30.0f); setParam (p, CompIDs::ratio, 8.0f); }, kickHeavy14, 4.0);

        {
            EasyCompAudioProcessor p;
            prepare (p, sr, 256);
            const auto in = sine (sr, 1000.0, fromDb (-6.0f), 4.0);
            const auto out = run (p, in, 256, [&] (int start) { setParam (p, CommonIDs::autoGain, (start / 24576) % 2 == 0 ? 0.0f : 1.0f); });
            const float step = worstGainStep (in, out, 4800);
            check (step < 0.005f, "switching AUTO-GAIN on and off: smooth, no clicks (largest per-sample gain step " + juce::String (step, 5) + ")");
        }

        {
            EasyCompAudioProcessor a, b;
            setParam (a, CommonIDs::autoGain, 1.0f);
            setParam (b, CommonIDs::autoGain, 1.0f);
            setParam (b, CompIDs::makeup, 24.0f);
            prepare (a, sr, 512); prepare (b, sr, 512);
            const float diff = maxDifference (run (a, loop, 512), run (b, loop, 512));
            check (diff == 0.0f, "with AUTO-GAIN on, the MAKE-UP knob has no effect (max diff " + juce::String (diff) + ")");
        }

        {
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::mix, 0.0f); setParam (p, CommonIDs::autoGain, 1.0f);
            prepare (p, sr, 512);
            check (maxDifference (loop, run (p, loop, 512)) == 0.0f, "MIX 0% with AUTO-GAIN on: still exactly the original");
        }

        {
            EasyCompAudioProcessor p;
            loadPresetNamed (p, "Room Mics - Crushed"); setParam (p, CommonIDs::autoGain, 1.0f);
            prepare (p, sr, 480);
            juce::AudioBuffer<float> withGap (2, 18 * second);
            withGap.clear();
            const auto part = drumLoop (sr, 6.0);
            for (int ch = 0; ch < 2; ++ch)
            {
                withGap.copyFrom (ch, 0, part, ch, 0, 6 * second);
                withGap.copyFrom (ch, 12 * second, part, ch, 0, 6 * second);
            }
            float atGapStart = 0.0f, atGapEnd = 0.0f;
            const auto out = run (p, withGap, 480, [&] (int start)
            {
                if (start == 6 * second)  atGapStart = p.getAutoGainDb();
                if (start == 12 * second) atGapEnd   = p.getAutoGainDb();
            });
            check (std::abs (atGapEnd - atGapStart) < 0.5f, "6 seconds of silence: AUTO-GAIN holds its setting (" + juce::String (atGapStart, 2)
                                                              + " -> " + juce::String (atGapEnd, 2) + " dB)");
            const double back = loudnessDb (out, 12 * second, 13 * second) - loudnessDb (withGap, 12 * second, 13 * second);
            check (std::abs (back) < 0.75, "    ...so when the drums come back in they're level-matched straight away (" + juce::String (back, 2) + " dB)");
        }

        {
            Compressor::Settings s; s.autoGain = true; s.makeupDb = 24.0f;
            check (near (Compressor::staticOutputDb (-60.0f, s, 6.0f), -54.0f, 1e-3f) && near (Compressor::staticOutputDb (0.0f, s, 6.0f), -9.0f, 1e-3f),
                   "graph with AUTO-GAIN: the curve uses the auto amount, not the MAKE-UP knob");
        }

        bool finite = true;
        for (double rate : { 44100.0, 96000.0, 192000.0 })
        {
            EasyCompAudioProcessor p; loadPresetNamed (p, "Drum Bus - Parallel Smash"); setParam (p, CommonIDs::autoGain, 1.0f);
            prepare (p, rate, 333);
            const auto out = run (p, drumLoop (rate, 3.0), 333);
            for (int i = 0; i < out.getNumSamples(); ++i)
                if (! std::isfinite (out.getSample (0, i))) finite = false;
            finite = finite && std::isfinite (p.getAutoGainDb()) && p.getAutoGainDb() > 0.5f;
        }
        check (finite, "AUTO-GAIN works at 44.1, 96 and 192 kHz with odd buffer sizes");
    }

    //==========================================================================
    void testEfficiency()
    {
        std::cout << "\n[efficiency: quick maths, skipping through silence, speed]\n";

        for (auto& [ok, what] : th::fastMathReport())
            check (ok, what);

        // Skipping through digital silence gives the same sound as working through it
        const auto gapped = th::withGaps (drumLoop (48000.0, 6.0));

        struct Case { const char* name; Compressor::Settings settings; };
        std::vector<Case> cases;
        { Compressor::Settings s; s.thresholdDb = -28.0f; s.ratio = 6.0f; s.attackMs = 2.0f; s.releaseMs = 150.0f; cases.push_back ({ "PEAKS", s }); }
        { Compressor::Settings s; s.thresholdDb = -30.0f; s.ratio = 4.0f; s.attackMs = 10.0f; s.releaseMs = 1000.0f; s.reactToAverage = true; s.lowCutHz = 150.0f;
          s.autoGain = true; cases.push_back ({ "AVERAGE, IGNORE BELOW 150 Hz, 1 s release, AUTO-GAIN", s }); }
        { Compressor::Settings s; s.thresholdDb = -40.0f; s.ratio = 20.0f; s.attackMs = 0.1f; s.releaseMs = 300.0f; s.mix = 0.4f; s.makeupDb = 12.0f;
          cases.push_back ({ "parallel smash", s }); }
        { Compressor::Settings s; s.thresholdDb = -28.0f; s.ratio = 6.0f; s.attackMs = 2.0f; s.releaseMs = 150.0f; s.vintage = true;
          cases.push_back ({ "VINTAGE", s }); }
        { Compressor::Settings s; s.thresholdDb = -30.0f; s.ratio = 4.0f; s.attackMs = 10.0f; s.releaseMs = 1000.0f; s.reactToAverage = true; s.lowCutHz = 150.0f;
          s.autoGain = true; s.vintage = true; s.thrust = true; cases.push_back ({ "VINTAGE, THRUST, AVERAGE, IGNORE BELOW 150 Hz, 1 s release, AUTO-GAIN", s }); }
        { Compressor::Settings s; s.thresholdDb = -40.0f; s.ratio = 20.0f; s.attackMs = 0.1f; s.releaseMs = 1000.0f; s.thrust = true;
          cases.push_back ({ "THRUST, 20:1", s }); }

        for (auto& c : cases)
        {
            const auto r = th::restReport<Compressor> (gapped, 48000.0, 256, c.settings);
            check (r.differenceDb <= -110.0 && r.worstLevelDb <= 0.0005 && r.blocksRested > 0,
                   juce::String ("skipping silence sounds the same as working through it (") + c.name + "): "
                   + juce::String (r.differenceDb, 1) + " dB apart, levels within " + juce::String (r.worstLevelDb, 6) + " dB; skipped "
                   + juce::String (r.blocksRested) + " of " + juce::String (r.blocks) + " blocks");
        }

        {
            // Knobs moved and a preset loaded in the middle of the 2-second gap
            auto settings = cases[1].settings;
            const auto r = th::restReport<Compressor> (gapped, 48000.0, 256, settings, [] (int start, Compressor::Settings& s, bool& restart)
            {
                if (start >= 180000 && start < 180256) { s.thresholdDb = -20.0f; s.mix = 0.7f; }
                if (start >= 200000 && start < 200256) restart = true;
                if (start >= 230000 && start < 230256) { s.bypassed = true; s.autoGain = false; }
                if (start >= 250000 && start < 250256) s.bypassed = false;
            });
            check (r.differenceDb <= -110.0 && r.worstLevelDb <= 0.0005,
                   "...and with knobs moved and a preset loaded during the silence: " + juce::String (r.differenceDb, 1) + " dB apart");
        }

        // Speed (only meaningful in an optimised build)
       #if JUCE_DEBUG
        check (true, "(the speed checks only run in an optimised build)");
       #else
        {
            EasyCompAudioProcessor p;
            loadPresetNamed (p, "Drum Bus - Glue");
            setParam (p, CommonIDs::autoGain, 1.0f);
            prepare (p, 48000.0, 256);
            const double busy = th::coreShare (p, drumLoop (48000.0, 30.0));
            check (busy < 0.35, "drum bus, AUTO-GAIN on: " + juce::String (busy, 3) + "% of one core (budget 0.35%)");

            EasyCompAudioProcessor v;
            loadPresetNamed (v, "Drum Bus - Vintage Glue");   // VINTAGE and THRUST
            setParam (v, CommonIDs::autoGain, 1.0f);
            prepare (v, 48000.0, 256);
            const double vintageBusy = th::coreShare (v, drumLoop (48000.0, 30.0));
            check (vintageBusy < 0.45, "drum bus with VINTAGE and THRUST, AUTO-GAIN on: " + juce::String (vintageBusy, 3) + "% of one core (budget 0.45%)");

            EasyCompAudioProcessor q;
            loadPresetNamed (q, "Vocal - Pop & R&B");
            prepare (q, 48000.0, 256);
            juce::AudioBuffer<float> silence (2, 48000 * 30);
            silence.clear();
            const double idle = th::coreShare (q, silence);
            check (idle < 0.03, "digital silence: " + juce::String (idle, 4) + "% of one core (budget 0.03%)");
        }
       #endif
    }

    #include "CompCharacterTests.inc"
    #include "CompEditorTests.inc"
}

namespace
{
    //==========================================================================
    void testMidSide()
    {
        std::cout << "\n[WORKS ON: mid-side]\n";
        constexpr double rate = 48000.0;
        const auto make = [] (float midAmp, float sideAmp, double seconds)
        {
            juce::AudioBuffer<float> b (2, (int) (rate * seconds));
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float m = midAmp * (float) std::sin (juce::MathConstants<double>::twoPi * 200.0 * i / rate);
                const float s = sideAmp * (float) std::sin (juce::MathConstants<double>::twoPi * 3100.0 * i / rate);
                b.setSample (0, i, m + s);
                b.setSample (1, i, m - s);
            }
            return b;
        };
        const auto midOf = [] (const juce::AudioBuffer<float>& b, int from, int to)
        {
            double e = 0.0;
            for (int i = from; i < to; ++i) { const double m = 0.5 * (b.getSample (0, i) + b.getSample (1, i)); e += m * m; }
            return (float) std::sqrt (e / (to - from));
        };
        const auto sideOf = [] (const juce::AudioBuffer<float>& b, int from, int to)
        {
            double e = 0.0;
            for (int i = from; i < to; ++i) { const double s = 0.5 * (b.getSample (0, i) - b.getSample (1, i)); e += s * s; }
            return (float) std::sqrt (e / (to - from));
        };
        const auto runWith = [&] (int worksOn, const juce::AudioBuffer<float>& in, bool bypass = false)
        {
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::threshold, -30.0f); setParam (p, CompIDs::ratio, 8.0f); setParam (p, CompIDs::worksOn, (float) worksOn);
            if (bypass) setParam (p, CommonIDs::bypass, 1.0f);
            prepare (p, rate, 512);
            return th::run (p, in, 512);
        };

        {
            EasyCompAudioProcessor p;
            check (getParam (p, CompIDs::worksOn) == 0.0f, "WORKS ON starts at STEREO (old projects and presets sound the same)");
        }

        const int from = (int) rate, to = (int) (rate * 2.0);
        // MID: a loud middle is compressed, the quiet sides pass untouched
        {
            const auto in = make (0.8f, 0.05f, 2.0);
            const auto mid = runWith (worksOnMid, in), stereo = runWith (worksOnStereo, in);
            const float midCut = th::toDb (midOf (mid, from, to) / midOf (in, from, to));
            const float sideKept = th::toDb (sideOf (mid, from, to) / sideOf (in, from, to));
            const float sideInStereo = th::toDb (sideOf (stereo, from, to) / sideOf (in, from, to));
            check (midCut < -10.0f && std::abs (sideKept) < 0.05f && sideInStereo < -10.0f,
                   "MID: the loud middle squeezed (" + juce::String (midCut, 1) + " dB), the sides untouched (" + juce::String (sideKept, 2)
                   + " dB) - in STEREO they'd have gone down with it (" + juce::String (sideInStereo, 1) + " dB)");
        }
        // SIDE: loud sides compressed, the middle untouched
        {
            const auto in = make (0.05f, 0.8f, 2.0);
            const auto side = runWith (worksOnSide, in);
            const float sideCut = th::toDb (sideOf (side, from, to) / sideOf (in, from, to));
            const float midKept = th::toDb (midOf (side, from, to) / midOf (in, from, to));
            check (sideCut < -10.0f && std::abs (midKept) < 0.05f, "SIDE: the loud sides squeezed (" + juce::String (sideCut, 1) + " dB), the middle untouched ("
                   + juce::String (midKept, 2) + " dB)");
        }
        // MID+SIDE: each on its own - a loud middle doesn't pull quiet sides down, loud sides are still caught
        {
            const auto in = make (0.8f, 0.02f, 2.0);   // (the sides well under THRESHOLD)
            const auto both = runWith (worksOnMidSide, in);
            const float midCut = th::toDb (midOf (both, from, to) / midOf (in, from, to));
            const float sideKept = th::toDb (sideOf (both, from, to) / sideOf (in, from, to));
            const auto in2 = make (0.05f, 0.8f, 2.0);
            const auto both2 = runWith (worksOnMidSide, in2);
            const float sideCut = th::toDb (sideOf (both2, from, to) / sideOf (in2, from, to));
            check (midCut < -10.0f && std::abs (sideKept) < 0.05f && sideCut < -10.0f,
                   "MID+SIDE: each compressed on its own (loud middle " + juce::String (midCut, 1) + " dB, quiet sides " + juce::String (sideKept, 2)
                   + " dB; loud sides " + juce::String (sideCut, 1) + " dB)");
        }
        // A track with nothing in the sides: MID does what STEREO does
        {
            const auto in = make (0.8f, 0.0f, 2.0);
            const auto a = runWith (worksOnMid, in), b = runWith (worksOnStereo, in);
            check (th::maxDifference (a, b) < 1.0e-5f, "a centred track: MID sounds the same as STEREO");
        }
        // BYPASS in MID+SIDE: exactly your track
        {
            const auto in = make (0.8f, 0.3f, 1.0);
            check (th::maxDifference (runWith (worksOnMidSide, in, true), in) == 0.0f, "BYPASS with MID+SIDE: exactly your track");
        }
        // Changing WORKS ON while playing: a quick dip, never a click
        {
            const auto in = make (0.8f, 0.3f, 3.0);
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::threshold, -30.0f); setParam (p, CompIDs::ratio, 8.0f);
            prepare (p, rate, 256);
            int step = 0;
            const auto out = th::run (p, in, 256, [&] (int start) { if (start > 0 && start % 24000 < 256) setParam (p, CompIDs::worksOn, (float) (++step % 4)); });
            const auto jump = [] (const juce::AudioBuffer<float>& b)
            {
                float worst = 0.0f;
                for (int c = 0; c < 2; ++c)
                    for (int i = 4800; i < b.getNumSamples(); ++i)
                        worst = std::max (worst, std::abs (b.getSample (c, i) - 2.0f * b.getSample (c, i - 1) + b.getSample (c, i - 2)));
                return worst;
            };
            float steady = 0.0f;   // (the biggest jump any of the four makes on its own: they sound different)
            for (int w = 0; w < 4; ++w)
                steady = std::max (steady, jump (runWith (w, in)));
            check (jump (out) < steady * 1.3f && th::allFinite (out), "switching WORKS ON every half second while playing: no clicks (" + juce::String (jump (out) / steady, 2) + "x)");
        }
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());

    testParametersAndText();
    testGainComputerAndCurve();
    testBelowAndAboveThreshold();
    testTransparentSettings();
    testMakeupAndMix();
    testAttackReleaseTiming();
    testLowCut();
    testStereoLinkAndMono();
    testSmoothing();
    testExtremes();
    testSampleRates();
    testStateAndPresets();
    testReactsTo();
    testVintage();
    testThrust();
    testSidechain();
    testBypass();
    testAutoGain();
    testMidSide();
    testEfficiency();
    testEditor (outDir);

    std::cout << "\n" << (checks - failures) << " / " << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
