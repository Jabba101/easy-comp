// Easy Amp: the valve amp and the plugin around it (not shipped).
#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_dsp/juce_dsp.h>

using namespace th;
using P = EasyAmpAudioProcessor;

namespace
{
    constexpr double sr = 48000.0;

    void set (P& p, const char* id, float v)   { setParam (p, id, v); }

    float maxAbs (const juce::AudioBuffer<float>& b, int start = 0)
    {
        float m = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = start; i < b.getNumSamples(); ++i)
                m = std::max (m, std::abs (b.getSample (ch, i)));
        return m;
    }

    float delayedDifference (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out, int delay, int from)
    {
        float m = 0.0f;
        for (int ch = 0; ch < in.getNumChannels(); ++ch)
            for (int i = from; i < out.getNumSamples(); ++i)
                m = std::max (m, std::abs (out.getSample (ch, i) - in.getSample (ch, i - delay)));
        return m;
    }

    double amplitudeAt (const juce::AudioBuffer<float>& b, int ch, int start, int length, double freq)
    {
        double re = 0, im = 0, wsum = 0;
        for (int i = 0; i < length; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (length - 1));
            const double ph = juce::MathConstants<double>::twoPi * freq * i / sr;
            re += w * b.getSample (ch, start + i) * std::cos (ph);
            im -= w * b.getSample (ch, start + i) * std::sin (ph);
            wsum += w;
        }
        return 2.0 * std::sqrt (re * re + im * im) / wsum;
    }

    double aliasingDb (const juce::AudioBuffer<float>& b, int start, double f0)
    {
        constexpr int order = 15, n = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> data ((size_t) n * 2, 0.0f);
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.35875 - 0.48829 * std::cos (juce::MathConstants<double>::twoPi * i / n)
                           + 0.14128 * std::cos (2 * juce::MathConstants<double>::twoPi * i / n)
                           - 0.01168 * std::cos (3 * juce::MathConstants<double>::twoPi * i / n);
            data[(size_t) i] = (float) (w * b.getSample (0, start + i));
        }
        fft.performFrequencyOnlyForwardTransform (data.data());

        const double binHz = sr / n;
        double unwanted = 0, fundamental = 0;
        for (int k = 3; k < n / 2; ++k)
        {
            const double f = k * binHz, power = (double) data[(size_t) k] * data[(size_t) k];
            bool harmonic = false;
            for (int h = 1; h * f0 < sr / 2; ++h)
                if (std::abs (f - h * f0) < 6 * binHz) harmonic = true;
            if (std::abs (f - f0) < 6 * binHz) fundamental += power;
            if (! harmonic) unwanted += power;
        }
        return 10.0 * std::log10 (unwanted / fundamental + 1e-30);
    }

    // K-weighting-ish loudness (a 150 Hz high-pass, then RMS), in dB
    double heardDb (const juce::AudioBuffer<float>& b, int start, int end)
    {
        double sum = 0; int count = 0;
        const double a = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 150.0 / sr);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            double low = 0;
            for (int i = 0; i < end; ++i)
            {
                low += a * (b.getSample (ch, i) - low);
                const double h = b.getSample (ch, i) - low;
                if (i >= start) { sum += h * h; ++count; }
            }
        }
        return 10.0 * std::log10 (sum / count + 1e-30);
    }

    // A bass-like test note: 82 Hz with some harmonics, plucked every half second
    juce::AudioBuffer<float> bassLine (double seconds, float peakDb)
    {
        const int n = (int) (sr * seconds);
        juce::AudioBuffer<float> b (2, n);
        const double amp = fromDb (peakDb);
        for (int i = 0; i < n; ++i)
        {
            const double t = std::fmod ((double) i / sr, 0.5);
            const double env = std::exp (-t / 0.4);
            const double ph = juce::MathConstants<double>::twoPi * 82.4 * i / sr;
            const double x = amp * env * (0.7 * std::sin (ph) + 0.2 * std::sin (2 * ph) + 0.1 * std::sin (3 * ph));
            b.setSample (0, i, (float) x);
            b.setSample (1, i, (float) x);
        }
        return b;
    }

    //==========================================================================
    void testCleanAndDelay()
    {
        std::cout << "\n[clean at 0, the delay]\n";
        P p; prepare (p, sr, 512);
        const int latency = p.getLatencySamples();
        check (latency > 0 && latency < 150, "reports a fixed delay to the DAW: " + juce::String (latency) + " samples (" + juce::String (latency / 48.0, 2) + " ms)");
        check (getParam (p, CommonIDs::autoGain) > 0.5f && getParam (p, AmpIDs::drive) == 0.0f, "starts clean (DRIVE 0) with AUTO-GAIN on");

        auto loop = drumLoop (sr, 2.0, -3.0f, -6.0f, -18.0f);
        for (int quality = 0; quality < 4; ++quality)
        {
            P q; set (q, AmpIDs::quality, (float) quality); prepare (q, sr, 512);
            auto out = run (q, loop, 512);
            check (delayedDifference (loop, out, latency, latency) == 0.0f,
                   "DRIVE 0, OVERSAMPLING " + juce::StringArray { "AUTO", "OFF", "4x", "8x" }[quality] + ": the original exactly, just delayed");
        }

        // The amp's sound lines up with the original (so MIX doesn't comb)
        for (int quality : { 1, 2, 3 })
        {
            P a; set (a, AmpIDs::quality, (float) quality); set (a, AmpIDs::drive, 5.0f); set (a, CommonIDs::autoGain, 0.0f); prepare (a, sr, 512);
            auto sine = th::sine (sr, 1000.0, 0.1f, 1.0);
            auto full = run (a, sine, 512);
            P b; set (b, AmpIDs::quality, (float) quality); set (b, AmpIDs::drive, 5.0f); set (b, CommonIDs::autoGain, 0.0f); set (b, AmpIDs::mix, 50.0f); prepare (b, sr, 512);
            auto half = run (b, sine, 512);
            const double ratio = amplitudeAt (half, 0, 24000, 16384, 1000.0) / amplitudeAt (full, 0, 24000, 16384, 1000.0);
            const double expected = 0.5 + 0.5 / amplitudeAt (full, 0, 24000, 16384, 1000.0) * 0.1;
            check (std::abs (ratio - expected) < 0.03, juce::String ("OVERSAMPLING ") + juce::StringArray { "AUTO", "OFF", "4x", "8x" }[quality]
                                                       + ": MIX 50% adds up without cancelling (the amp lines up with the original)");
        }
    }

    //==========================================================================
    void testCharacter()
    {
        std::cout << "\n[the valve character]\n";
        auto sine = th::sine (sr, 200.0, fromDb (-9.0f), 2.0);   // a typical track's peak level
        double previousThd = -200.0;
        bool rising = true;
        juce::String report;
        double h2At50 = 0, oddAt100 = 0;

        for (float drive : { 10.0f, 25.0f, 50.0f, 75.0f, 100.0f })
        {
            P p; set (p, AmpIDs::drive, drive); set (p, AmpIDs::quality, 3.0f); prepare (p, sr, 512);
            auto out = run (p, sine, 512);
            const double h1 = amplitudeAt (out, 0, 48000, 32768, 200.0);
            double harmonics = 0, even = 0, odd = 0;
            for (int h = 2; h <= 15; ++h)
            {
                const double a = amplitudeAt (out, 0, 48000, 32768, 200.0 * h);
                harmonics += a * a;
                (h % 2 == 0 ? even : odd) += a * a;
            }
            const double thd = 10.0 * std::log10 (harmonics / (h1 * h1) + 1e-30);
            if (thd <= previousThd) rising = false;
            previousThd = thd;
            report << juce::String ((int) drive) << "%: " << juce::String (thd, 1) << " dB (even " << juce::String (10 * std::log10 (even / (h1 * h1) + 1e-30), 1)
                   << ", odd " << juce::String (10 * std::log10 (odd / (h1 * h1) + 1e-30), 1) << ")  ";
            if (drive == 50.0f) h2At50 = 20 * std::log10 (amplitudeAt (out, 0, 48000, 32768, 400.0) / h1);
            if (drive == 100.0f) oddAt100 = 10 * std::log10 (odd / (h1 * h1) + 1e-30);
        }

        std::cout << "  harmonics of a -9 dBFS 200 Hz tone: " << report << "\n";
        check (rising, "more DRIVE, more harmonics, every step of the way");
        check (h2At50 > -30.0, "DRIVE 50%: a strong 2nd harmonic (the valve's lopsided warmth): " + juce::String (h2At50, 1) + " dB");
        check (oddAt100 > -12.0, "DRIVE 100%: heavy distortion (odd harmonics " + juce::String (oddAt100, 1) + " dB): a real growl");

        P q; set (q, AmpIDs::drive, 10.0f); prepare (q, sr, 512);
        auto out = run (q, sine, 512);
        const double gentle = 20 * std::log10 (amplitudeAt (out, 0, 48000, 32768, 400.0) / amplitudeAt (out, 0, 48000, 32768, 200.0));
        check (gentle < -30.0 && gentle > -70.0, "DRIVE 10%: just a trace of warmth (2nd harmonic " + juce::String (gentle, 1) + " dB)");

        // It breathes: a loud hit pushes it into compression, which lets go afterwards
        juce::AudioBuffer<float> burst (2, (int) sr);
        for (int i = 0; i < burst.getNumSamples(); ++i)
        {
            const float level = i < 12000 ? 0.05f : (i < 24000 ? 0.6f : 0.05f);
            const float x = level * (float) std::sin (juce::MathConstants<double>::twoPi * 300.0 * i / sr);
            burst.setSample (0, i, x); burst.setSample (1, i, x);
        }
        P b; set (b, AmpIDs::drive, 80.0f); set (b, CommonIDs::autoGain, 0.0f); prepare (b, sr, 512);
        auto bo = run (b, burst, 512);
        const int L = b.getLatencySamples();
        const float early = th::peak (bo, 0, 12000 + L + 200, 12000 + L + 1000), late = th::peak (bo, 0, 24000 + L - 2000, 24000 + L - 200);
        check (late < early * 0.97f, "under a long loud note the amp sags a little (" + juce::String (toDb (late / early), 1) + " dB after half a second), as valves do");
    }

    //==========================================================================
    void testLevelMatching()
    {
        std::cout << "\n[level matching]\n";
        auto loop = drumLoop (sr, 8.0, -6.0f, -9.0f, -20.0f);
        auto bass = bassLine (8.0, -8.0f);
        const double loopIn = heardDb (loop, (int) sr * 4, loop.getNumSamples());
        const double bassIn = heardDb (bass, (int) sr * 4, bass.getNumSamples());
        bool allClose = true, fixedClose = true;
        juce::String report;

        for (float drive : { 20.0f, 50.0f, 80.0f, 100.0f })
        {
            for (int material = 0; material < 2; ++material)
            {
                auto& in = material == 0 ? loop : bass;
                P p; set (p, AmpIDs::drive, drive); prepare (p, sr, 512);
                auto out = run (p, in, 512);
                const double change = heardDb (out, (int) sr * 4, out.getNumSamples()) - (material == 0 ? loopIn : bassIn);
                if (std::abs (change) > 1.5) allClose = false;

                P f; set (f, AmpIDs::drive, drive); set (f, CommonIDs::autoGain, 0.0f); prepare (f, sr, 512);
                auto fo = run (f, in, 512);
                const double fixedChange = heardDb (fo, (int) sr * 4, fo.getNumSamples()) - (material == 0 ? loopIn : bassIn);
                if (std::abs (fixedChange) > 6.0) fixedClose = false;
                report << (material == 0 ? "drums " : "bass ") << juce::String ((int) drive) << "%: " << juce::String (change, 1) << " / " << juce::String (fixedChange, 1) << "  ";
            }
        }

        std::cout << "  loudness change, AUTO-GAIN on / off: " << report << "\n";
        check (allClose, "AUTO-GAIN on: drums and bass come out within 1.5 dB as loud as they went in, at every DRIVE");
        check (fixedClose, "AUTO-GAIN off: the fixed correction alone keeps it within 6 dB (it never blasts)");

        // Turning DRIVE up while playing: no big jump in volume
        P p; prepare (p, sr, 512);
        auto out = run (p, loop, 512, [&] (int start) { set (p, AmpIDs::drive, start > (int) sr * 2 ? 90.0f : 10.0f); });
        float worst = 0.0f;
        for (int s = (int) sr * 2; s < (int) sr * 3; s += 2400)
            worst = juce::jmax (worst, th::peak (out, 0, s, s + 2400));
        const float before = th::peak (out, 0, (int) sr, (int) sr * 2);
        check (toDb (worst / before) < 4.0f, "DRIVE from 10% to 90% in one go: the loudest moment just after is " + juce::String (toDb (worst / before), 1) + " dB louder than before (no blast)");
    }

    //==========================================================================
    void testTightAndAliasing()
    {
        std::cout << "\n[TIGHT, OVERSAMPLING]\n";
        // A 50 Hz bass note at full DRIVE: TIGHT 120 Hz keeps it clean
        auto low = th::sine (sr, 50.0, fromDb (-9.0f), 2.0);
        const auto distortionOf50 = [&] (float tight)
        {
            P p; set (p, AmpIDs::drive, 100.0f); set (p, AmpIDs::tight, tight); prepare (p, sr, 512);
            auto out = run (p, low, 512);
            const double h1 = amplitudeAt (out, 0, 48000, 32768, 50.0);
            double h = 0;
            for (int k = 2; k <= 6; ++k) { const double a = amplitudeAt (out, 0, 48000, 32768, 50.0 * k); h += a * a; }
            return 10.0 * std::log10 (h / (h1 * h1) + 1e-30);
        };
        const double off = distortionOf50 (20.0f), on = distortionOf50 (120.0f);
        check (on < off - 15.0, "TIGHT 120 Hz at full DRIVE: a 50 Hz note stays clean (" + juce::String (on, 1) + " dB of distortion, vs " + juce::String (off, 1) + " dB with TIGHT off)");

        const double f0 = 4567.0;
        double results[3];
        for (int quality = 1; quality <= 3; ++quality)
        {
            P p; set (p, AmpIDs::quality, (float) quality); set (p, AmpIDs::drive, 100.0f); prepare (p, sr, 512);
            auto out = run (p, th::sine (sr, f0, fromDb (-9.0f), 1.5), 512);
            results[quality - 1] = aliasingDb (out, 8000, f0);
        }
        check (results[1] < results[0] - 15.0 && results[2] <= results[1] + 1.0,
               "a 4.6 kHz tone at full DRIVE: out-of-tune foldback " + juce::String (results[0], 1) + " dB (OFF), " + juce::String (results[1], 1)
               + " dB (4x), " + juce::String (results[2], 1) + " dB (8x)");

        double automatic[2];
        for (int exporting = 0; exporting < 2; ++exporting)
        {
            P p; set (p, AmpIDs::drive, 100.0f); p.setNonRealtime (exporting == 1); prepare (p, sr, 512);
            auto out = run (p, th::sine (sr, f0, fromDb (-9.0f), 1.5), 512);
            automatic[exporting] = aliasingDb (out, 8000, f0);
        }
        check (std::abs (automatic[0] - results[1]) < 0.01 && std::abs (automatic[1] - results[2]) < 0.01, "AUTO: 4x while you mix, 8x when the DAW exports");

        // Switching OVERSAMPLING (and every knob) while it plays: nothing nasty
        P p; prepare (p, sr, 256);
        auto loop = drumLoop (sr, 4.0, -3.0f, -6.0f, -18.0f);
        auto out = run (p, loop, 256, [&] (int start)
        {
            const float t = (float) start / (float) loop.getNumSamples();
            set (p, AmpIDs::drive, 100.0f * std::abs (std::sin (t * 9.0f)));
            set (p, AmpIDs::tone, 100.0f * std::sin (t * 13.0f));
            set (p, AmpIDs::tight, t > 0.5f ? 150.0f : 20.0f);
            set (p, AmpIDs::mix, 50.0f + 50.0f * std::sin (t * 7.0f));
            set (p, AmpIDs::quality, (float) ((start / 40000) % 4));
        });
        check (allFinite (out) && maxAbs (out) < 2.0f, "turning every knob and switching OVERSAMPLING while it plays: stays sane (loudest " + juce::String (toDb (maxAbs (out)), 1) + " dBFS)");
    }

    //==========================================================================
    void testPluginParts()
    {
        std::cout << "\n[settings, presets, BYPASS]\n";
        P p; prepare (p, sr, 512);
        check (p.getParameters().size() == 7, "7 settings: DRIVE, TONE, TIGHT, MIX, OVERSAMPLING, AUTO-GAIN, BYPASS");

        const auto [ok, report] = presetTextsReport (p.getBuiltInPresets());
        check (ok, report);

        auto loop = drumLoop (sr, 4.0, -6.0f, -9.0f, -20.0f);
        const double in = heardDb (loop, (int) sr * 2, loop.getNumSamples());
        bool allSane = true;
        juce::String loudest;
        for (auto& preset : p.getBuiltInPresets())
        {
            P q; q.loadPreset (preset); prepare (q, sr, 512);
            auto out = run (q, loop, 512);
            const double change = heardDb (out, (int) sr * 2, out.getNumSamples()) - in;
            if (! allFinite (out) || std::abs (change) > 2.0) { allSane = false; loudest << preset.name << " " << juce::String (change, 1) << " dB; "; }
        }
        check (allSane, "every preset on a drum loop: within 2 dB of the original loudness (AUTO-GAIN)" + (loudest.isEmpty() ? juce::String() : ": " + loudest));

        P y; set (y, AmpIDs::drive, 80.0f); set (y, CommonIDs::bypass, 1.0f); prepare (y, sr, 512);
        auto yo = run (y, loop, 512);
        check (delayedDifference (loop, yo, y.getLatencySamples(), y.getLatencySamples()) == 0.0f, "BYPASS: exactly the original, delayed the same as the amp's sound");

        P m; set (m, AmpIDs::drive, 80.0f); set (m, AmpIDs::mix, 0.0f); prepare (m, sr, 512);
        auto mo = run (m, loop, 512);
        check (delayedDifference (loop, mo, m.getLatencySamples(), m.getLatencySamples()) < 1e-6f, "MIX 0%: just the original");

        const auto blockRun = [&] (int block)
        {
            P q; set (q, AmpIDs::drive, 70.0f); set (q, AmpIDs::tight, 90.0f); set (q, AmpIDs::tone, 20.0f); set (q, CommonIDs::autoGain, 0.0f);
            prepare (q, sr, block);
            return run (q, loop, block);
        };
        const auto a = blockRun (512), c = blockRun (37), d = blockRun (2048);
        check (maxDifference (a, c) < 1e-5f && maxDifference (a, d) < 1e-5f, "the same sound in blocks of 37, 512 and 2048 samples");

        P mono; mono.setPlayConfigDetails (1, 1, sr, 512); set (mono, AmpIDs::drive, 80.0f); mono.prepareToPlay (sr, 512);
        auto mOut = run (mono, th::sine (sr, 110.0, 0.3f, 1.0, 1), 512);
        check (allFinite (mOut), "works on a mono track");

        juce::MemoryBlock state;
        set (p, AmpIDs::drive, 63.0f); set (p, AmpIDs::quality, 3.0f); set (p, CommonIDs::autoGain, 0.0f);
        p.getStateInformation (state);
        P r; r.setStateInformation (state.getData(), (int) state.getSize());
        check (getParam (r, AmpIDs::drive) == 63.0f && getParam (r, AmpIDs::quality) == 3.0f && getParam (r, CommonIDs::autoGain) < 0.5f,
               "saved with the project and loaded back");
    }

    //==========================================================================
    void testWindow (const juce::File& outDir)
    {
        std::cout << "\n[plugin window]\n";
        P p; prepare (p, sr, 512);
        loadPresetNamed (p, "Guitar - Classic Rock Crunch");
        auto loop = bassLine (2.0, -8.0f);
        run (p, loop, 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* window = dynamic_cast<EasyAmpAudioProcessorEditor*> (editor.get());
        check (window != nullptr && editor->getWidth() == 1100 && editor->getHeight() == 640, "window is 1100 x 640, with the shared header");
        if (window == nullptr) return;

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

        bool doubleClickToType = true;
        for (int k = 0; k < EasyAmpAudioProcessorEditor::numKnobs; ++k)
            for (auto* child : window->getKnob (k).getChildren())
                if (auto* box = dynamic_cast<juce::Label*> (child))
                    if (box->isEditableOnSingleClick() || ! box->isEditableOnDoubleClick()) doubleClickToType = false;
        check (doubleClickToType, "every number: double-click to type a value");

        auto& init = window->getInitAllButton();
        check (init.getWidth() <= 60 && init.getX() - window->bypassButton.getRight() >= 40 && init.getRight() < window->presetBrowser.getX(),
               "a small INIT beside the preset box, well clear of BYPASS");

        for (int i = 0; i < 10; ++i) { run (p, loop, 512); window->updateDisplaysNow(); }
        savePng (*editor, outDir.getChildFile ("amp.png"));

        init.onClick();
        check (getParam (p, AmpIDs::drive) == 0.0f && p.getPresetName() == "Default settings", "INIT: back to clean ('Default settings')");

        bool hintsOk = true;
        for (int k = 0; k < EasyAmpAudioProcessorEditor::numKnobs; ++k)
        {
            window->mouseEnter (hoverEvent (&window->getKnob (k)));
            if (window->hintBar.getText().length() < 40) hintsOk = false;
        }
        check (hintsOk, "every knob explains itself in the help bar");
    }

    //==========================================================================
    void testSpeed()
    {
       #if ! JUCE_DEBUG
        std::cout << "\n[speed]\n";
        auto loop = drumLoop (sr, 10.0, -6.0f, -9.0f, -20.0f);
        {
            P p; prepare (p, sr, 256);
            const double share = coreShare (p, loop);
            check (share < 0.3, "DRIVE 0 (clean): " + juce::String (share, 3) + "% of one core - next to nothing");
        }
        for (int quality = 1; quality <= 3; ++quality)
        {
            P p; set (p, AmpIDs::quality, (float) quality); loadPresetNamed (p, "Bass - Growl Under Clean Lows"); prepare (p, sr, 256);
            const double share = coreShare (p, loop);
            const double budget = quality == 1 ? 1.5 : (quality == 2 ? 5.0 : 9.0);   // (8x: AUTO only uses it when exporting)
            check (share < budget, juce::StringArray { "", "OFF", "4x", "8x" }[quality] + ", stereo, 48 kHz: " + juce::String (share, 3)
                                   + "% of one core (budget " + juce::String (budget, 1) + "%)");
        }
       #endif
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());

    testCleanAndDelay();
    testCharacter();
    testLevelMatching();
    testTightAndAliasing();
    testPluginParts();
    testWindow (outDir);
    testSpeed();
    return finish();
}
