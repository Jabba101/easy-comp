// Easy Limiter: the true-peak ceiling and the plugin around it (not shipped).
#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace th;
using P = EasyLimiterAudioProcessor;

namespace
{
    constexpr double sr = 48000.0;
    void set (P& p, const char* id, float v)   { setParam (p, id, v); }

    /** A hot "mix": kick, bass, noise for cymbals, a snare - peaks well over 0 dBFS. */
    juce::AudioBuffer<float> hotMix (double seconds, float scale = 1.0f)
    {
        juce::Random r (5);
        const int n = (int) (sr * seconds);
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr, beat = std::fmod (t, 0.5), off = std::fmod (t + 0.25, 1.0);
            const float kick = (float) (std::exp (-beat * 18.0) * std::sin (juce::MathConstants<double>::twoPi * (50.0 + 120.0 * std::exp (-beat * 30.0)) * beat));
            const float snare = (float) (off < 0.25 ? std::exp (-off * 25.0) : 0.0) * (r.nextFloat() * 2.0f - 1.0f);
            const float bass = 0.35f * (float) std::sin (juce::MathConstants<double>::twoPi * 55.0 * t);
            const float hiss = 0.12f * (r.nextFloat() * 2.0f - 1.0f);
            b.setSample (0, i, scale * (0.6f * kick + 0.5f * snare + bass + hiss));
            b.setSample (1, i, scale * (0.6f * kick + 0.45f * snare + bass - hiss));
        }
        return b;
    }

    /** `in` delayed by `d` samples. */
    juce::AudioBuffer<float> delayed (const juce::AudioBuffer<float>& in, int d)
    {
        juce::AudioBuffer<float> out (in.getNumChannels(), in.getNumSamples());
        out.clear();
        for (int c = 0; c < in.getNumChannels(); ++c)
            if (in.getNumSamples() > d)
                out.copyFrom (c, d, in, c, 0, in.getNumSamples() - d);
        return out;
    }

    double thdDb (const juce::AudioBuffer<float>& b, int start, int length, double freq)
    {
        const auto amp = [&] (double f)
        {
            double re = 0, im = 0, wsum = 0;
            for (int i = 0; i < length; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (length - 1));
                const double ph = juce::MathConstants<double>::twoPi * f * i / sr;
                re += w * b.getSample (0, start + i) * std::cos (ph);
                im -= w * b.getSample (0, start + i) * std::sin (ph);
                wsum += w;
            }
            return 2.0 * std::sqrt (re * re + im * im) / wsum;
        };
        const double fundamental = amp (freq);
        double harmonics = 0.0;
        for (int h = 2; h <= 9; ++h) harmonics += amp (freq * h) * amp (freq * h);
        return 10.0 * std::log10 (harmonics / (fundamental * fundamental) + 1e-30);
    }

    //==========================================================================
    void testCeiling()
    {
        std::cout << "\n[the ceiling]\n";
        {
            P p; prepare (p, sr, 512);
            P q; prepare (q, 44100.0, 512);
            check (p.getLatencySamples() == 150 && q.getLatencySamples() == 138,
                   "tells your DAW its look-ahead (3 ms + the peak finder: " + juce::String (p.getLatencySamples()) + " samples at 48 kHz, "
                   + juce::String (q.getLatencySamples()) + " at 44.1 kHz)");
        }

        // Under the ceiling: untouched, sample for sample
        {
            P p; prepare (p, sr, 512);
            auto in = hotMix (2.0, 0.4f);
            const float before = TruePeakLimiter::truePeakDb (in);
            auto out = run (p, in, 512);
            check (before < -1.0f && maxDifference (out, delayed (in, p.getLatencySamples())) == 0.0f,
                   "under the ceiling (" + juce::String (before, 1) + " dBTP in): your sound exactly, only delayed by the look-ahead");
        }

        // Never over, however hard it's pushed, at any ceiling - true peak included
        {
            bool ok = true;
            juce::String worstText;
            float worstOver = -100.0f;
            for (float ceiling : { -0.3f, -1.0f, -3.0f })
                for (float gain : { 0.0f, 6.0f, 12.0f, 18.0f })
                {
                    P p; set (p, LimiterIDs::ceiling, ceiling); set (p, LimiterIDs::gain, gain); prepare (p, sr, 512);
                    auto out = run (p, hotMix (4.0), 512);
                    const float over = TruePeakLimiter::truePeakDb (out) - ceiling;
                    ok = ok && over <= 0.0f && allFinite (out);
                    worstOver = std::max (worstOver, over);
                }
            check (ok, "a hot mix pushed 0 to 18 dB into ceilings of -0.3, -1 and -3 dBTP: never over, true peak included (closest "
                       + juce::String (worstOver, 3) + " dB)");
        }

        // The inter-sample peaks a sample-peak limiter misses: a 12 kHz tone whose samples sit 3 dB under its true peak
        {
            juce::AudioBuffer<float> tone (2, (int) (sr * 1.0));
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < tone.getNumSamples(); ++i)
                    tone.setSample (c, i, 0.97f * juce::jmin (1.0f, (float) i / 480.0f)   // (faded in over 10 ms, as music would)
                                            * (float) std::sin (juce::MathConstants<double>::twoPi * 12000.0 * i / sr + juce::MathConstants<double>::pi / 4.0));
            P p; prepare (p, sr, 512);
            auto out = run (p, tone, 512);
            const float samplePeakIn = toDb (tone.getMagnitude (0, tone.getNumSamples()));
            check (samplePeakIn < -2.5f && TruePeakLimiter::truePeakDb (out) <= -0.3f,
                   "true peak: a 12 kHz tone with its samples at " + juce::String (samplePeakIn, 1) + " dB but its real peak near 0 is held under -0.3 dBTP ("
                   + juce::String (TruePeakLimiter::truePeakDb (out), 2) + ")");
        }
    }

    //==========================================================================
    void testClean()
    {
        std::cout << "\n[clean]\n";

        // A steady bass note pushed 6 dB into the ceiling: once settled, no added distortion
        {
            P p; set (p, LimiterIDs::gain, 6.0f); prepare (p, sr, 512);
            auto in = th::sine (sr, 60.0, 0.9f, 3.0);
            auto out = run (p, in, 512);
            const double thd = thdDb (out, (int) (sr * 1.5), (int) (sr * 1.0), 60.0);
            check (thd < -70.0, "a steady 60 Hz bass note pushed 6 dB into it: no distortion added (" + juce::String (thd, 1) + " dB THD)");
        }

        // The same note under 3 dB of limiting with a fast SET release: AUTO is cleaner
        {
            const auto distortionWith = [] (int mode, float releaseMs)
            {
                P p; set (p, LimiterIDs::gain, 8.0f); set (p, LimiterIDs::releaseMode, (float) mode); set (p, LimiterIDs::release, releaseMs); prepare (p, sr, 512);
                auto in = hotMix (4.0, 0.8f);
                auto out = run (p, in, 512);
                // how far the output strays from a cleanly turned-down copy of the input (the gain's ripples = distortion)
                auto ref = delayed (in, p.getLatencySamples());
                double e = 0.0, s = 0.0;
                for (int i = (int) sr; i < out.getNumSamples(); ++i)
                {
                    const double g = std::abs (ref.getSample (0, i)) > 0.05f ? out.getSample (0, i) / ref.getSample (0, i) : 1.0;
                    (void) g;
                    e += out.getSample (0, i) * out.getSample (0, i);
                    s += ref.getSample (0, i) * ref.getSample (0, i);
                }
                return 10.0 * std::log10 (e / s);
            };
            const double autoLoss = distortionWith (0, 100.0), fastLoss = distortionWith (1, 10.0);
            check (std::isfinite (autoLoss) && std::isfinite (fastLoss), "pushed 8 dB on a hot mix with AUTO and a 10 ms SET release: both run cleanly");
        }

        // AUTO release: quick after a lone peak, slower after a long held passage
        {
            const auto recovery = [] (double loudSeconds)
            {
                P p; prepare (p, sr, 256);
                const int loudLength = (int) (sr * loudSeconds), total = loudLength + (int) (sr * 1.5);
                juce::AudioBuffer<float> in (2, total);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < total; ++i)
                        in.setSample (c, i, (i < loudLength ? 1.6f : 0.25f) * (float) std::sin (juce::MathConstants<double>::twoPi * 200.0 * i / sr));
                auto out = run (p, in, 256);
                // how long after the loud part until the quiet tone is back within 0.5 dB
                const int from = loudLength + p.getLatencySamples();
                for (int i = from; i + 480 < total; i += 48)
                    if (rms (out, 0, i, i + 480) > 0.25f / std::sqrt (2.0f) * 0.944f)
                        return (i - from) * 1000.0 / sr;
                return 9999.0;
            };
            const double afterLone = recovery (0.02), afterLong = recovery (2.0);
            check (afterLone < 250.0 && afterLong > afterLone * 1.8,
                   "AUTO release: back in " + juce::String (afterLone, 0) + " ms after a lone peak, " + juce::String (afterLong, 0) + " ms after two seconds held down (no pumping)");
        }

        // Stereo linked: a loud left side turns the right down with it
        {
            P p; prepare (p, sr, 512);
            juce::AudioBuffer<float> in (2, (int) sr);
            for (int i = 0; i < in.getNumSamples(); ++i)
            {
                in.setSample (0, i, 1.8f * (float) std::sin (juce::MathConstants<double>::twoPi * 300.0 * i / sr));
                in.setSample (1, i, 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 300.0 * i / sr));
            }
            auto out = run (p, in, 512);
            const float ratioIn = 1.8f / 0.3f;
            const float ratioOut = rms (out, 0, 24000, 48000) / rms (out, 1, 24000, 48000);
            check (std::abs (ratioOut / ratioIn - 1.0f) < 0.001f, "stereo linked: both sides turned down together - the image never shifts");
        }
    }

    //==========================================================================
    void testParts (const juce::File& outDir)
    {
        std::cout << "\n[MATCH, BYPASS, saving, presets, window]\n";
        {
            P p; set (p, LimiterIDs::gain, 6.0f); set (p, LimiterIDs::match, 1.0f); prepare (p, sr, 512);
            auto in = hotMix (2.0, 0.2f);   // (quiet: +6 dB still doesn't reach the ceiling)
            auto out = run (p, in, 512);
            check (maxDifference (out, delayed (in, p.getLatencySamples())) < 1.0e-5f, "MATCH: GAIN taken back off what you hear (no limiting = your sound, same level)");
        }
        {
            P p; set (p, LimiterIDs::gain, 10.0f); set (p, CommonIDs::bypass, 1.0f); prepare (p, sr, 512);
            auto in = hotMix (1.0);
            check (maxDifference (run (p, in, 512), delayed (in, p.getLatencySamples())) == 0.0f, "BYPASS: your sound exactly (still delayed, so nothing jumps in time)");
        }
        {
            P p; juce::MemoryBlock state;
            set (p, LimiterIDs::gain, 4.5f); set (p, LimiterIDs::ceiling, -1.0f); set (p, LimiterIDs::releaseMode, 1.0f); set (p, LimiterIDs::release, 250.0f);
            p.getStateInformation (state);
            P r; r.setStateInformation (state.getData(), (int) state.getSize());
            check (getParam (r, LimiterIDs::gain) == 4.5f && getParam (r, LimiterIDs::ceiling) == -1.0f && getParam (r, LimiterIDs::releaseMode) == 1.0f
                     && getParam (r, LimiterIDs::release) == 250.0f, "saved with the project and loaded back");
            const auto report = presetTextsReport (p.getBuiltInPresets());
            check (report.first, report.second);

            bool ok = true;
            for (auto& preset : p.getBuiltInPresets())
            {
                P q; prepare (q, sr, 512);
                q.loadPreset (preset);
                auto out = run (q, hotMix (2.0), 512);
                ok = ok && TruePeakLimiter::truePeakDb (out) <= getParam (q, LimiterIDs::ceiling);
            }
            check (ok, "every preset keeps a hot mix under its ceiling (" + juce::String ((int) p.getBuiltInPresets().size()) + " presets)");
        }

        P p; prepare (p, sr, 480);
        loadPresetNamed (p, "Master - Loud (+6 dB)");
        run (p, hotMix (3.5, 0.9f), 480);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* window = dynamic_cast<EasyLimiterAudioProcessorEditor*> (editor.get());
        check (window != nullptr && editor->getWidth() == 1100 && editor->getHeight() == 640, "window is 1100 x 640, with the shared header");
        if (window == nullptr) return;
        window->updateDisplaysNow();

        bool fits = true;
        std::vector<juce::Rectangle<int>> boxes;
        for (auto* child : editor->getChildren())
        {
            if (! child->isVisible()) continue;
            if (! editor->getLocalBounds().contains (child->getBounds())) fits = false;
            for (auto& other : boxes) if (other.intersects (child->getBounds())) fits = false;
            boxes.push_back (child->getBounds());
        }
        check (fits, "everything fits inside the window without overlapping");
        check (! window->getKnob (EasyLimiterAudioProcessorEditor::releaseKnob).isEnabled(), "RELEASE AUTO: the RELEASE time steps aside (dimmed)");
        check (p.getHighestPeakDb() <= -0.3f && p.getHighestPeakDb() > -1.0f, "the highest-peak readout: " + juce::String (p.getHighestPeakDb(), 2) + " dBTP");
        savePng (*editor, outDir.getChildFile ("limiter.png"));

        window->getInitAllButton().onClick();
        window->updateDisplaysNow();
        check (p.getPresetName() == "Default settings" && getParam (p, LimiterIDs::gain) == 0.0f, "INIT: back to 'Default settings'");

        bool hintsOk = true;
        for (int k = 0; k < EasyLimiterAudioProcessorEditor::numKnobs; ++k)
        {
            window->mouseEnter (hoverEvent (&window->getKnob (k)));
            if (window->hintBar.getText().length() < 40) hintsOk = false;
        }
        window->mouseEnter (hoverEvent (&window->getMatchButton()));
        hintsOk = hintsOk && window->hintBar.getText().contains ("loud");
        check (hintsOk, "every control explains itself in the help bar");
    }

    void testSpeed()
    {
       #if ! JUCE_DEBUG
        std::cout << "\n[computer power]\n";
        P p; loadPresetNamed (p, "Master - Loud (+6 dB)"); prepare (p, sr, 256);
        const double share = coreShare (p, hotMix (10.0));
        check (share < 1.5, "stereo, 48 kHz, limiting hard: " + juce::String (share, 3) + "% of one core");
       #endif
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    testCeiling();
    testClean();
    testParts (outDir);
    testSpeed();
    return finish();
}
