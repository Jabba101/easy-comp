// Reference scenarios for checking the reorganisation doesn't change the sound.
// Shared between the "before" (v1.2) and "after" builds; GOLDEN_NEW_API selects the API.
#pragma once
#include <cmath>
#include <functional>
#include <map>
#include <vector>

namespace golden
{
    inline float fromDb (float d) { return juce::Decibels::decibelsToGain (d, -200.0f); }

    inline juce::AudioBuffer<float> drumLoop (double sr, double seconds, float kickDb = -6.0f, float snareDb = -10.0f, float hatDb = -24.0f, int channels = 2)
    {
        const int n = (int) (sr * seconds);
        juce::AudioBuffer<float> b (channels, n);
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
            if (beatIndex % 2 == 0) x += kick * std::exp (-sinceBeat / 0.12) * std::sin (twoPi * 55.0 * sinceBeat);
            else                    x += snare * std::exp (-sinceBeat / 0.08) * (0.6 * std::sin (twoPi * 190.0 * sinceBeat) + 0.4 * noise);
            x += hat * std::exp (-sinceEighth / 0.02) * noise;
            for (int ch = 0; ch < channels; ++ch)
                b.setSample (ch, i, (float) (x * (ch == 0 ? 1.0 : 0.9)));
        }
        return b;
    }

    // The 8 presets from version 1.2: threshold, ratio, attack, release, makeup, mix, lowcut
    struct Values { float t, r, a, rel, mk, mix, lc; };
    inline const std::vector<Values> v12Presets {
        { -20, 4, 10, 100, 0, 100, 20 }, { -18, 4, 25, 80, 3, 100, 20 }, { -20, 4, 15, 100, 3, 100, 100 }, { -24, 3, 5, 200, 4, 100, 20 },
        { -18, 2, 20, 250, 2, 100, 120 }, { -35, 12, 1, 300, 10, 100, 20 }, { -16, 2, 30, 150, 2, 100, 100 }, { -40, 10, 1, 120, 12, 35, 80 } };

    template <typename Processor>
    void setParam (Processor& p, const char* id, float value)
    {
        auto* prm = p.parameters.getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    }

    template <typename Processor>
    void applyValues (Processor& p, const Values& v)
    {
        setParam (p, "threshold", v.t); setParam (p, "ratio", v.r); setParam (p, "attack", v.a); setParam (p, "release", v.rel);
        setParam (p, "makeup", v.mk); setParam (p, "mix", v.mix); setParam (p, "lowcut", v.lc);
    }

    template <typename Processor>
    juce::AudioBuffer<float> run (Processor& p, const juce::AudioBuffer<float>& input, int block, std::function<void (int)> beforeBlock = {})
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

    template <typename Processor>
    void prepare (Processor& p, double sr, int block, int channels = 2)
    {
        p.setPlayConfigDetails (channels, channels, sr, block);
        p.prepareToPlay (sr, block);
    }

    // Runs every scenario; "loadPresetIndex" performs a v1.2-style preset load (with the AUTO-GAIN restart).
    template <typename Processor>
    std::map<juce::String, juce::AudioBuffer<float>> runAll (std::function<void (Processor&, int)> loadPresetIndex)
    {
        std::map<juce::String, juce::AudioBuffer<float>> results;
        constexpr double sr = 48000.0;
        const int second = 48000;
        const auto loop = drumLoop (sr, 8.0);

        for (size_t i = 0; i < v12Presets.size(); ++i)
            for (bool autoGain : { false, true })
            {
                Processor p; applyValues (p, v12Presets[i]); setParam (p, "autogain", autoGain ? 1.0f : 0.0f);
                prepare (p, sr, 480);
                results["preset" + juce::String ((int) i) + (autoGain ? "_auto" : "")] = run (p, loop, 480);
            }

        {
            Processor p; applyValues (p, v12Presets[5]); setParam (p, "autogain", 1.0f); prepare (p, sr, 256);
            results["bypass_toggle"] = run (p, loop, 256, [&] (int start) { setParam (p, "bypass", (start / 14336) % 2 == 0 ? 0.0f : 1.0f); });
        }

        const auto loop14 = drumLoop (sr, 14.0);
        auto sweep = [&] (const char* name, const char* id, float from, float to, Values base, const juce::AudioBuffer<float>& drums)
        {
            Processor p; applyValues (p, base); setParam (p, id, from); setParam (p, "autogain", 1.0f); prepare (p, sr, 480);
            results[name] = run (p, drums, 480, [&] (int start)
            {
                if (start >= 7 * second)
                    setParam (p, id, from + (to - from) * juce::jmin (1.0f, (float) (start - 7 * second) / (float) (second / 2)));
            });
        };
        sweep ("sweep_threshold", "threshold", -40.0f, -10.0f, { -40, 8, 10, 100, 0, 100, 20 }, loop14);
        sweep ("sweep_attack", "attack", 1.0f, 60.0f, { -30, 8, 1, 100, 0, 100, 20 }, loop14);
        sweep ("sweep_lowcut", "lowcut", 20.0f, 300.0f, { -30, 8, 10, 100, 0, 100, 20 }, drumLoop (sr, 14.0, -3.0f, -14.0f, -24.0f));
        sweep ("sweep_mix", "mix", 100.0f, 30.0f, { -40, 10, 10, 100, 0, 100, 20 }, loop14);

        {
            Processor p; applyValues (p, v12Presets[5]); setParam (p, "autogain", 1.0f); prepare (p, sr, 480);
            results["preset_switch"] = run (p, loop14, 480, [&] (int start) { if (start == 7 * second) loadPresetIndex (p, 1); });
        }

        {
            Processor p; applyValues (p, v12Presets[7]); setParam (p, "autogain", 1.0f); prepare (p, 44100.0, 333, 1);
            results["mono_44k_odd_blocks"] = run (p, drumLoop (44100.0, 6.0, -6.0f, -10.0f, -24.0f, 1), 333);
        }

        for (bool autoGain : { false, true })
        {
            Processor p; setParam (p, "autogain", autoGain ? 1.0f : 0.0f); prepare (p, sr, 128);
            juce::Random rng (99);
            results[juce::String ("automation") + (autoGain ? "_auto" : "")] = run (p, loop, 128, [&] (int)
            {
                setParam (p, "threshold", -40.0f + 35.0f * rng.nextFloat());
                setParam (p, "ratio", 1.0f + 10.0f * rng.nextFloat());
                setParam (p, "makeup", 12.0f * rng.nextFloat());
                setParam (p, "mix", 100.0f * rng.nextFloat());
                setParam (p, "release", 10.0f + 500.0f * rng.nextFloat());
            });
        }
        return results;
    }

    inline void save (const std::map<juce::String, juce::AudioBuffer<float>>& results, const juce::File& dir)
    {
        dir.createDirectory();
        for (auto& [name, buffer] : results)
        {
            juce::FileOutputStream out (dir.getChildFile (name + ".raw"));
            out.setPosition (0); out.truncate();
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                out.write (buffer.getReadPointer (ch), (size_t) buffer.getNumSamples() * sizeof (float));
        }
    }

    // Returns the number of scenarios that differ by more than the tolerance (prints details).
    // Since the optimisation pass (fast maths) the sound isn't bit-identical any more, so each
    // scenario reports how far below the music the difference is ("null depth"): it must be at
    // least 100 dB down, and no sample's level may differ by more than 0.001 dB.
    inline int compare (const std::map<juce::String, juce::AudioBuffer<float>>& results, const juce::File& dir)
    {
        int differing = 0;
        for (auto& [name, buffer] : results)
        {
            juce::MemoryBlock stored;
            dir.getChildFile (name + ".raw").loadFileAsData (stored);
            const auto* ref = static_cast<const float*> (stored.getData());
            const size_t expected = (size_t) buffer.getNumChannels() * (size_t) buffer.getNumSamples();
            double worst = 0.0, peak = 0.0, worstGainDb = 0.0;
            if (stored.getSize() != expected * sizeof (float)) worst = 1.0e9;
            else
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                    {
                        const double r = ref[(size_t) ch * (size_t) buffer.getNumSamples() + (size_t) i], got = buffer.getSample (ch, i);
                        worst = std::max (worst, std::abs (got - r));
                        peak = std::max (peak, std::abs (r));
                        if (std::abs (r) > 1.0e-4)
                            worstGainDb = std::max (worstGainDb, got == 0.0 ? 999.0 : std::abs (20.0 * std::log10 (got / r)));
                    }
            const double nullDb = worst > 0.0 ? 20.0 * std::log10 (worst / std::max (peak, 1.0e-30)) : -999.0;
            const bool ok = nullDb <= -100.0 && worstGainDb <= 0.001;
            std::cout << (worst == 0.0 ? "  identical  " : ok ? "  within     " : "  DIFFERENT  ") << name
                      << "  (difference " << juce::String (nullDb, 1) << " dB below the peak, level within " << juce::String (worstGainDb, 6) << " dB)\n";
            if (! ok) ++differing;
        }
        return differing;
    }
}
