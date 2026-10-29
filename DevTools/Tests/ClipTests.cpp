// Easy Clip: the clipper and the plugin around it (not shipped).
#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_dsp/juce_dsp.h>

using namespace th;
using P = EasyClipAudioProcessor;

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

    // Largest difference between out and in delayed by `delay`
    float delayedDifference (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out, int delay, int from)
    {
        float m = 0.0f;
        for (int ch = 0; ch < in.getNumChannels(); ++ch)
            for (int i = from; i < out.getNumSamples(); ++i)
                m = std::max (m, std::abs (out.getSample (ch, i) - in.getSample (ch, i - delay)));
        return m;
    }

    // Amplitude of one frequency (Goertzel over a whole number of periods-ish, with a Hann window)
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

    double mean (const juce::AudioBuffer<float>& b, int ch, int start, int end)
    {
        double s = 0;
        for (int i = start; i < end; ++i) s += b.getSample (ch, i);
        return s / (end - start);
    }

    // Aliasing: of a clipped sine at f0, how loud (dB relative to f0) is
    // everything that isn't one of its own harmonics?
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
        double wanted = 0, unwanted = 0, fundamental = 0;
        for (int k = 3; k < n / 2; ++k)
        {
            const double f = k * binHz, power = (double) data[(size_t) k] * data[(size_t) k];
            bool harmonic = false;
            for (int h = 1; h * f0 < sr / 2; ++h)
                if (std::abs (f - h * f0) < 6 * binHz) harmonic = true;
            if (std::abs (f - f0) < 6 * binHz) fundamental += power;
            (harmonic ? wanted : unwanted) += power;
        }
        return 10.0 * std::log10 (unwanted / fundamental + 1e-30);
    }

    //==========================================================================
    void testDefaultsAndLatency()
    {
        std::cout << "\n[defaults, delay]\n";
        P p; prepare (p, sr, 512);
        const int latency = p.getLatencySamples();
        check (latency > 0 && latency < 200, "reports a fixed delay to the DAW: " + juce::String (latency) + " samples (" + juce::String (latency / 48.0, 2) + " ms)");

        // Default settings, OVERSAMPLING OFF: exactly the original, delayed
        set (p, ClipIDs::quality, 1.0f);
        P q; set (q, ClipIDs::quality, 1.0f); prepare (q, sr, 512);
        auto loop = drumLoop (sr, 3.0, -3.0f, -6.0f, -18.0f);
        auto out = run (q, loop, 512);
        check (delayedDifference (loop, out, latency, latency) == 0.0f, "default settings, OVERSAMPLING OFF: the original comes out exactly, just delayed");

        for (int quality : { 1, 2 })
        {
            P r; set (r, ClipIDs::quality, (float) (quality + 1)); prepare (r, sr, 512);
            check (r.getLatencySamples() == latency, juce::String (quality == 1 ? "4x" : "8x") + ": the same delay");
            auto sine = th::sine (sr, 1000.0, 0.5f, 1.0);
            auto o = run (r, sine, 512);
            const float diff = delayedDifference (sine, o, latency, 4000);
            check (toDb (diff / 0.5f) < -60.0f, juce::String (quality == 1 ? "4x" : "8x") + ", default settings: a 1 kHz tone comes out lined up and the same ("
                                                + juce::String (toDb (diff / 0.5f), 1) + " dB difference)");

            // An impulse comes out at the reported delay
            juce::AudioBuffer<float> impulse (2, 4096); impulse.clear();
            impulse.setSample (0, 1000, 0.5f); impulse.setSample (1, 1000, 0.5f);
            P s; set (s, ClipIDs::quality, (float) (quality + 1)); prepare (s, sr, 512);
            auto io = run (s, impulse, 512);
            int peakAt = 0; float peakValue = 0;
            for (int i = 0; i < io.getNumSamples(); ++i) if (std::abs (io.getSample (0, i)) > peakValue) { peakValue = std::abs (io.getSample (0, i)); peakAt = i; }
            check (peakAt == 1000 + latency, juce::String (quality == 1 ? "4x" : "8x") + ": a click comes out exactly at the reported delay");
        }

        P m; m.setPlayConfigDetails (1, 1, sr, 512); m.prepareToPlay (sr, 512);
        set (m, ClipIDs::push, 12.0f);
        auto mono = th::sine (sr, 100.0, 0.5f, 1.0, 1);
        auto mo = run (m, mono, 512);
        check (allFinite (mo) && maxAbs (mo) <= 1.0f, "works on a mono track");

        for (double rate : { 44100.0, 96000.0 })
        {
            P h; prepare (h, rate, 512);
            check (h.getLatencySamples() == latency, "at " + juce::String (rate / 1000.0, 1) + " kHz: the same delay in samples");
        }
    }

    //==========================================================================
    void testCeiling()
    {
        std::cout << "\n[the ceiling holds]\n";
        auto loop = drumLoop (sr, 4.0, 0.0f, -2.0f, -12.0f);
        loop.applyGain (2.0f);   // even a track that already goes over 0 dBFS

        struct Case { const char* name; float push, ceiling, soft, lows, top, bottom, bias, blend; };
        const Case cases[] = {
            { "hard, pushed 24 dB",              24, -1,   0,  20,  0,  0,   0, 100 },
            { "soft 100%",                       24, -6, 100,  20,  0,  0,   0, 100 },
            { "PROTECT LOWS 150 Hz, pushed 18",  18, -1,   0, 150,  0,  0,   0, 100 },
            { "TOP -6, BOTTOM -2, BIAS +80",     18, -3,  30,  80, -6, -2,  80, 100 },
            { "BLEND 30%",                       24, -1,   0,  20,  0,  0,   0,  30 },
            { "BLEND 0% (original only)",        24, -10,  0,  20,  0,  0, -60,   0 },
        };

        for (int quality = 0; quality < 3; ++quality)
            for (auto& c : cases)
            {
                P p; prepare (p, sr, 512);
                set (p, ClipIDs::quality, (float) (quality + 1));
                set (p, ClipIDs::push, c.push); set (p, ClipIDs::ceiling, c.ceiling); set (p, ClipIDs::softness, c.soft);
                set (p, ClipIDs::protectLows, c.lows); set (p, ClipIDs::top, c.top); set (p, ClipIDs::bottom, c.bottom);
                set (p, ClipIDs::bias, c.bias); set (p, ClipIDs::blend, c.blend);
                prepare (p, sr, 512);   // (start from the settings, no glide)
                auto out = run (p, loop, 512);
                const float peakDb = toDb (maxAbs (out));
                check (allFinite (out) && peakDb <= c.ceiling + 0.0001f,
                       juce::String (quality == 0 ? "OFF" : quality == 1 ? "4x" : "8x") + ", " + c.name + ": loudest " + juce::String (peakDb, 3)
                       + " dB, ceiling " + juce::String (c.ceiling, 1));
            }

        // Moving the knobs while playing: still never over
        P p; set (p, ClipIDs::ceiling, -2.0f); prepare (p, sr, 256);
        auto out = run (p, loop, 256, [&] (int start)
        {
            const float t = (float) start / (float) loop.getNumSamples();
            set (p, ClipIDs::push, 24.0f * t);
            set (p, ClipIDs::softness, 100.0f * (1.0f - t));
            set (p, ClipIDs::protectLows, t > 0.5f ? 200.0f : 20.0f);
            set (p, ClipIDs::bias, 100.0f * std::sin (t * 20.0f));
            set (p, ClipIDs::quality, (float) ((start / 40000) % 4));
        });
        check (allFinite (out) && toDb (maxAbs (out)) <= -2.0f + 0.0001f, "turning every knob (and switching OVERSAMPLING) while it plays: never over the ceiling");
    }

    //==========================================================================
    void testShape()
    {
        std::cout << "\n[the clip itself]\n";
        const float c = 0.5f;
        bool flatTop = true, untouched = true, neverOver = true, smooth = true;
        float previous = Clipper::clipInto (-2.0f, -c, c, 0.4f);
        for (float x = -2.0f; x <= 2.0f; x += 0.0005f)
        {
            const float hard = Clipper::clipInto (x, -c, c, 0.0f);
            if (std::abs (x) <= c && hard != x) untouched = false;
            if (x > c && hard != c) flatTop = false;
            for (float s : { 0.0f, 0.3f, 1.0f })
                for (float b : { -1.0f, 0.0f, 0.7f })
                    if (std::abs (Clipper::clipInto (x, -c, c, s, b)) > c) neverOver = false;
            const float soft = Clipper::clipInto (x, -c, c, 0.4f);
            if (soft - previous > 0.0005f * 1.0001f || soft < previous) smooth = false;   // rises, never faster than 1:1
            previous = soft;
        }
        check (untouched && flatTop, "SOFTNESS 0: below the ceiling untouched, above it cut flat");
        check (neverOver, "every SOFTNESS and BIAS: never past the ceiling");
        check (smooth, "SOFTNESS: the curve only ever rises, and never steeper than the original");
        check (Clipper::clipInto (0.6f * c, -c, c, 0.3f) == 0.6f * c && Clipper::clipInto (0.8f * c, -c, c, 0.3f) < 0.8f * c,
               "SOFTNESS 30%: untouched up to 70% of the ceiling, rounded above");
        check (Clipper::clipInto (0.3f * c, -c, c, 0.0f, 0.8f) < 0.3f * c && Clipper::clipInto (-0.3f * c, -c, c, 0.0f, 0.8f) == -0.3f * c,
               "BIAS +80%: the upper half is rounded earlier than the lower half");

        // Hard clip of a loud sine, OFF: flat tops at the ceiling
        P p; set (p, ClipIDs::quality, 1.0f); set (p, ClipIDs::push, 12.0f); set (p, ClipIDs::ceiling, -6.0f); prepare (p, sr, 512);
        auto sine = th::sine (sr, 200.0, 0.5f, 1.0);
        auto out = run (p, sine, 512);
        int atCeiling = 0;
        for (int i = 10000; i < 20000; ++i) if (std::abs (std::abs (out.getSample (0, i)) - fromDb (-6.0f)) < 1e-4f) ++atCeiling;
        check (atCeiling > 5000, "a sine pushed 12 dB into a -6 dB ceiling: flat tops at -6 dB (" + juce::String (atCeiling / 100) + "% of the time)");

        // TOP lower than BOTTOM: lopsided, so even harmonics, and no offset left
        P a; set (a, ClipIDs::quality, 1.0f); set (a, ClipIDs::push, 6.0f); set (a, ClipIDs::top, -6.0f); set (a, ClipIDs::link, 0.0f); prepare (a, sr, 512);
        auto asym = run (a, th::sine (sr, 200.0, 0.5f, 3.0), 512);
        const double h2 = amplitudeAt (asym, 0, 96000, 32768, 400.0), h1 = amplitudeAt (asym, 0, 96000, 32768, 200.0);
        const double dc = mean (asym, 0, 96000, 144000);
        check (20 * std::log10 (h2 / h1) > -40.0, "TOP -6 dB: the clip is lopsided - 2nd harmonic at " + juce::String (20 * std::log10 (h2 / h1), 1) + " dB");
        check (std::abs (dc) < 0.002, "...and the offset that makes is filtered out (" + juce::String (dc, 5) + ")");

        P s; set (s, ClipIDs::quality, 1.0f); set (s, ClipIDs::push, 6.0f); prepare (s, sr, 512);
        auto sym = run (s, th::sine (sr, 200.0, 0.5f, 3.0), 512);
        const double s2 = amplitudeAt (sym, 0, 96000, 32768, 400.0) / amplitudeAt (sym, 0, 96000, 32768, 200.0);
        check (20 * std::log10 (s2) < -80.0, "even clipping: no 2nd harmonic (" + juce::String (20 * std::log10 (s2 + 1e-12), 1) + " dB)");

        P b; set (b, ClipIDs::quality, 1.0f); set (b, ClipIDs::push, 9.0f); set (b, ClipIDs::bias, 60.0f); prepare (b, sr, 512);
        auto biased = run (b, th::sine (sr, 200.0, 0.5f, 3.0), 512);
        const double b2 = amplitudeAt (biased, 0, 96000, 32768, 400.0) / amplitudeAt (biased, 0, 96000, 32768, 200.0);
        check (20 * std::log10 (b2) > -40.0, "BIAS +60% at SOFTNESS 0: even harmonics too (" + juce::String (20 * std::log10 (b2), 1) + " dB)");

        // BIAS on quiet sound: below the knee nothing changes, so no thump when it moves
        P q; set (q, ClipIDs::quality, 1.0f); prepare (q, sr, 256);
        auto quiet = th::sine (sr, 100.0, 0.05f, 2.0);
        auto qo = run (q, quiet, 256, [&] (int start) { set (q, ClipIDs::bias, start > 40000 ? 100.0f : -100.0f); });
        check (delayedDifference (quiet, qo, q.getLatencySamples(), q.getLatencySamples()) < 0.002f,
               "turning BIAS on quiet sound changes almost nothing (no thump)");
    }

    //==========================================================================
    void testProtectLows()
    {
        std::cout << "\n[PROTECT LOWS]\n";
        // A 50 Hz "bass" plus a 3 kHz "click", pushed well over the ceiling
        juce::AudioBuffer<float> mix (2, (int) sr * 3);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < mix.getNumSamples(); ++i)
                mix.setSample (ch, i, (float) (0.3 * std::sin (juce::MathConstants<double>::twoPi * 50.0 * i / sr)
                                               + 0.45 * std::sin (juce::MathConstants<double>::twoPi * 3000.0 * i / sr)));

        const auto lowsAfter = [&] (float protectHz)
        {
            P p; set (p, ClipIDs::push, 6.0f); set (p, ClipIDs::ceiling, -1.0f); set (p, ClipIDs::protectLows, protectHz); prepare (p, sr, 512);
            auto out = run (p, mix, 512);
            return std::make_pair (amplitudeAt (out, 0, 96000, 32768, 50.0), toDb (maxAbs (out)));
        };

        const double wanted = 0.3 * fromDb (6.0f);
        const auto [off, offPeak] = lowsAfter (20.0f);
        const auto [on, onPeak] = lowsAfter (150.0f);
        check (std::abs (20 * std::log10 (on / wanted)) < 1.0,
               "PROTECT LOWS 150 Hz: the 50 Hz bass comes out within 1 dB of its pushed level (" + juce::String (20 * std::log10 (on / wanted), 2) + " dB)");
        check (20 * std::log10 (off / wanted) < -2.0,
               "off: the bass is clipped along with everything else (" + juce::String (20 * std::log10 (off / wanted), 2) + " dB)");
        check (onPeak <= -1.0001f + 0.0002f && offPeak <= -1.0f + 0.0001f, "the ceiling still holds either way");

        // The display and SHAVED OFF compare like with like (the split delays the lows a little)
        auto loop = drumLoop (sr, 3.0, -9.0f, -12.0f, -24.0f);
        P q; set (q, ClipIDs::protectLows, 100.0f); set (q, ClipIDs::ceiling, -1.0f); prepare (q, sr, 512);
        run (q, loop, 512);
        const float quietShaved = q.takeShavedForMeter();
        check (quietShaved < 0.3f, "PROTECT LOWS on, nothing reaching the ceiling: SHAVED OFF reads " + juce::String (quietShaved, 2) + " dB (nothing)");
        P g; loadPresetNamed (g, "Drum Bus - Glue the Peaks"); prepare (g, sr, 512);
        run (g, drumLoop (sr, 3.0, 0.0f, -2.0f, -14.0f), 512);
        const float glueShaved = g.takeShavedForMeter();
        check (glueShaved > 3.0f && glueShaved < 6.5f, "'Drum Bus - Glue the Peaks' on a kick 3 dB over the ceiling after PUSH: SHAVED OFF reads " + juce::String (glueShaved, 1) + " dB");
    }

    //==========================================================================
    void testAliasing()
    {
        std::cout << "\n[OVERSAMPLING: aliasing]\n";
        const double f0 = 4567.0;
        double results[3];
        for (int quality = 0; quality < 3; ++quality)
        {
            P p; set (p, ClipIDs::quality, (float) (quality + 1)); set (p, ClipIDs::push, 12.0f); set (p, ClipIDs::ceiling, -6.0f); prepare (p, sr, 512);
            auto out = run (p, th::sine (sr, f0, 0.5f, 1.5), 512);
            results[quality] = aliasingDb (out, 8000, f0);
        }
        // AUTO: 4x while you mix, 8x when the DAW exports
        double automatic[2];
        for (int exporting = 0; exporting < 2; ++exporting)
        {
            P p; set (p, ClipIDs::push, 12.0f); set (p, ClipIDs::ceiling, -6.0f);
            p.setNonRealtime (exporting == 1);
            prepare (p, sr, 512);
            auto out = run (p, th::sine (sr, f0, 0.5f, 1.5), 512);
            automatic[exporting] = aliasingDb (out, 8000, f0);
        }
        P fresh;
        check (getParam (fresh, ClipIDs::quality) == 0.0f && std::abs (automatic[0] - results[1]) < 0.01 && std::abs (automatic[1] - results[2]) < 0.01,
               "AUTO (the default): 4x while you mix, 8x when the DAW exports");
        check (results[1] < results[0] - 15.0 && results[2] < results[1],
               "a 4.6 kHz tone clipped hard: out-of-tune foldback " + juce::String (results[0], 1) + " dB (OFF), "
               + juce::String (results[1], 1) + " dB (4x), " + juce::String (results[2], 1) + " dB (8x)");
    }

    //==========================================================================
    void testBlendAndBypass()
    {
        std::cout << "\n[BLEND, BYPASS, block sizes]\n";
        auto loop = drumLoop (sr, 2.0, -3.0f, -6.0f, -18.0f);

        P b; set (b, ClipIDs::blend, 0.0f); set (b, ClipIDs::push, 18.0f); set (b, ClipIDs::quality, 1.0f); prepare (b, sr, 512);
        auto bo = run (b, loop, 512);
        check (delayedDifference (loop, bo, b.getLatencySamples(), b.getLatencySamples()) < 1e-6f, "BLEND 0%: just the original (lined up)");

        P y; set (y, ClipIDs::push, 18.0f); set (y, CommonIDs::bypass, 1.0f); prepare (y, sr, 512);
        auto yo = run (y, loop, 512);
        check (delayedDifference (loop, yo, y.getLatencySamples(), y.getLatencySamples()) == 0.0f, "BYPASS: exactly the original, delayed the same as the clipped sound");

        // BYPASS switched on while playing fades (no click), and the timing stays put
        P z; set (z, ClipIDs::push, 18.0f); prepare (z, sr, 512);
        auto zo = run (z, loop, 512, [&] (int start) { set (z, CommonIDs::bypass, start >= 48000 ? 1.0f : 0.0f); });
        check (delayedDifference (loop, zo, z.getLatencySamples(), 48000 + 512 + 1500) == 0.0f, "BYPASS switched on while playing: the original after a short fade");

        // The same sound whatever the block size
        auto ref = [&] (int block)
        {
            P p; set (p, ClipIDs::push, 9.0f); set (p, ClipIDs::softness, 20.0f); set (p, ClipIDs::protectLows, 80.0f);
            set (p, ClipIDs::bias, 30.0f); prepare (p, sr, block);
            return run (p, loop, block);
        };
        const auto a = ref (512), c = ref (37), d = ref (2048);
        check (maxDifference (a, c) < 1e-6f && maxDifference (a, d) < 1e-6f, "the same sound in blocks of 37, 512 and 2048 samples");
    }

    //==========================================================================
    void testPluginParts()
    {
        std::cout << "\n[settings, presets, INIT]\n";
        P p; prepare (p, sr, 512);
        check (p.getParameters().size() == 12, "12 settings: 9 knobs/buttons, OVERSAMPLING, the view and BYPASS (no AUTO-GAIN)");
        check (! p.hasAutoGain(), "no AUTO-GAIN (the ceiling must stay the loudest it can get)");

        const auto [ok, report] = presetTextsReport (p.getBuiltInPresets());
        check (ok, report);

        bool allHold = true;
        auto loop = drumLoop (sr, 2.0, 0.0f, -3.0f, -15.0f);
        for (auto& preset : p.getBuiltInPresets())
        {
            P q; q.loadPreset (preset); prepare (q, sr, 512);
            auto out = run (q, loop, 512);
            if (! allFinite (out) || toDb (maxAbs (out)) > getParam (q, ClipIDs::ceiling) + 0.0001f) allHold = false;
        }
        check (allHold, "every preset, on a loud drum loop: never over its ceiling");

        // The lamp and the ADVANCED INIT
        P a; prepare (a, sr, 512);
        check (! a.isAdvancedInUse(), "the lamp is out with the default settings");
        set (a, ClipIDs::push, 7.0f); set (a, ClipIDs::softness, 40.0f);
        check (! a.isAdvancedInUse(), "...and stays out when only the SIMPLE knobs move");
        set (a, ClipIDs::blend, 80.0f);
        check (a.isAdvancedInUse(), "BLEND 80%: the lamp lights");
        set (a, ClipIDs::top, -4.0f); set (a, ClipIDs::link, 0.0f); set (a, ClipIDs::bias, 20.0f);
        a.resetAdvanced();
        check (! a.isAdvancedInUse() && getParam (a, ClipIDs::link) > 0.5f, "ADVANCED INIT: TOP, BOTTOM, BIAS, BLEND back to neutral, LINK back on");
        check (getParam (a, ClipIDs::push) == 7.0f && getParam (a, ClipIDs::softness) == 40.0f, "...and PUSH and SOFTNESS untouched");

        // Presets don't change OVERSAMPLING or the view
        set (a, ClipIDs::quality, 2.0f); set (a, ClipIDs::view, 1.0f);
        loadPresetNamed (a, "Snare - Crunch");
        check (getParam (a, ClipIDs::quality) == 2.0f && getParam (a, ClipIDs::view) == 1.0f, "loading a preset leaves OVERSAMPLING and SIMPLE / ADVANCED alone");
        check (getParam (a, ClipIDs::top) == -2.0f && getParam (a, ClipIDs::link) < 0.5f, "'Snare - Crunch': TOP -2 dB, LINK off (TOP and BOTTOM differ)");

        // Saving with the project
        juce::MemoryBlock state;
        a.getStateInformation (state);
        P b; b.setStateInformation (state.getData(), (int) state.getSize());
        bool same = true;
        for (auto* id : { ClipIDs::push, ClipIDs::ceiling, ClipIDs::softness, ClipIDs::protectLows, ClipIDs::top, ClipIDs::bottom,
                          ClipIDs::link, ClipIDs::bias, ClipIDs::blend, ClipIDs::quality, ClipIDs::view })
            if (getParam (a, id) != getParam (b, id)) same = false;
        check (same && b.getPresetName() == "Snare - Crunch", "saved with the project and loaded back: every setting, the preset name, the view");

        // Typed values
        auto* lows = p.parameters.getParameter (ClipIDs::protectLows);
        auto* top = p.parameters.getParameter (ClipIDs::top);
        check (lows->getText (lows->getValueForText ("off"), 20) == "Off" && th::near (lows->convertFrom0to1 (lows->getValueForText ("0.1k")), 100.0f, 0.6f)
               && th::near (top->convertFrom0to1 (top->getValueForText ("3")), -3.0f, 0.01f),
               "typing 'off', '0.1k' (100 Hz) and '3' for TOP (3 dB below the ceiling) all work");
    }

    //==========================================================================
    void testWindow (const juce::File& outDir)
    {
        std::cout << "\n[plugin window]\n";
        P p; prepare (p, sr, 512);
        loadPresetNamed (p, "Drum Bus - Glue the Peaks");

        // Some sound through it, so the display has something to show
        auto loop = drumLoop (sr, 3.0, 0.0f, -2.0f, -14.0f);
        run (p, loop, 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* window = dynamic_cast<EasyClipAudioProcessorEditor*> (editor.get());
        check (window != nullptr && editor->getWidth() == 1100 && editor->getHeight() == 640, "window is 1100 x 640, with the shared header");
        if (window == nullptr) return;

        check (! window->isShowingAdvanced() && ! window->getKnob (EasyClipAudioProcessorEditor::topKnob).isVisible(), "opens on SIMPLE: SOUND DESIGN hidden");

        bool doubleClickToType = true;
        for (int k = 0; k < EasyClipAudioProcessorEditor::numKnobs; ++k)
            for (auto* child : window->getKnob (k).getChildren())
                if (auto* box = dynamic_cast<juce::Label*> (child))
                    if (box->isEditableOnSingleClick() || ! box->isEditableOnDoubleClick()) doubleClickToType = false;
        check (doubleClickToType, "every number: double-click to type a value (a single click doesn't open the box)");

        const auto fitsWithoutOverlap = [&]
        {
            bool ok = true;
            std::vector<juce::Rectangle<int>> boxes;
            for (auto* child : editor->getChildren())
            {
                if (! child->isVisible()) continue;
                if (! editor->getLocalBounds().contains (child->getBounds())) ok = false;
                for (auto& other : boxes) if (other.intersects (child->getBounds())) ok = false;
                boxes.push_back (child->getBounds());
            }
            return ok;
        };
        check (fitsWithoutOverlap(), "SIMPLE: everything fits inside the window without overlapping");

        for (int i = 0; i < 20; ++i) { run (p, loop, 512); window->updateDisplaysNow(); }
        savePng (*editor, outDir.getChildFile ("clip_simple.png"));

        set (p, ClipIDs::view, 1.0f);
        window->updateDisplaysNow();
        check (window->isShowingAdvanced() && window->getKnob (EasyClipAudioProcessorEditor::topKnob).isVisible(), "ADVANCED: SOUND DESIGN appears");
        check (fitsWithoutOverlap(), "ADVANCED: everything still fits without overlapping");

        // LINK keeps the gap
        set (p, ClipIDs::link, 0.0f);
        set (p, ClipIDs::top, -2.0f); set (p, ClipIDs::bottom, -6.0f);
        window->updateDisplaysNow();
        set (p, ClipIDs::link, 1.0f);
        window->linkEverything = true;
        auto& topSlider = window->getKnob (EasyClipAudioProcessorEditor::topKnob);
        topSlider.setValue (-3.5, juce::sendNotificationSync);
        check (th::near (getParam (p, ClipIDs::top), -3.5f, 0.01f) && th::near (getParam (p, ClipIDs::bottom), -7.5f, 0.01f),
               "LINK: TOP -2 -> -3.5 moves BOTTOM -6 -> -7.5 (the 4 dB gap stays)");
        topSlider.setValue (-10.0, juce::sendNotificationSync);
        check (th::near (getParam (p, ClipIDs::top), -8.0f, 0.01f) && th::near (getParam (p, ClipIDs::bottom), -12.0f, 0.01f),
               "LINK: when BOTTOM hits -12, TOP stops too (at -8: the gap still 4 dB)");
        topSlider.setValue (0.0, juce::sendNotificationSync);
        check (th::near (getParam (p, ClipIDs::top), 0.0f, 0.01f) && th::near (getParam (p, ClipIDs::bottom), -4.0f, 0.01f), "LINK: and back up to 0 / -4");
        window->getLinkButton().setToggleState (false, juce::sendNotificationSync);
        topSlider.setValue (-1.0, juce::sendNotificationSync);
        check (th::near (getParam (p, ClipIDs::bottom), -4.0f, 0.01f), "LINK off: TOP moves on its own");
        window->linkEverything = false;
        window->getLinkButton().setToggleState (true, juce::sendNotificationSync);
        set (p, ClipIDs::top, -5.0f);   // a preset or the DAW
        check (th::near (getParam (p, ClipIDs::bottom), -4.0f, 0.01f), "LINK doesn't apply to changes from the DAW or presets");

        set (p, ClipIDs::bias, 25.0f); set (p, ClipIDs::blend, 70.0f);
        for (int i = 0; i < 10; ++i) { run (p, loop, 512); window->updateDisplaysNow(); }
        savePng (*editor, outDir.getChildFile ("clip_advanced.png"));

        window->getAdvancedInitButton().onClick();
        window->updateDisplaysNow();
        check (! p.isAdvancedInUse() && getParam (p, ClipIDs::push) > 0.0f, "the INIT button in SOUND DESIGN resets just the ADVANCED settings");

        set (p, ClipIDs::bias, 40.0f); set (p, ClipIDs::quality, 3.0f);
        check (window->getInitAllButton().isVisible() && window->getInitAllButton().getY() < 80 && window->getInitAllButton().getWidth() <= 60
               && window->getInitAllButton().getX() - window->bypassButton.getRight() >= 40, "a small INIT for the whole plugin in the header, well clear of BYPASS");
        window->getInitAllButton().onClick();
        bool allDefault = true;
        for (auto& id : clipPresetIDs())
        {
            auto* prm = p.parameters.getParameter (id);
            if (std::abs (prm->getValue() - prm->getDefaultValue()) > 1e-6f) allDefault = false;
        }
        check (allDefault && p.getPresetName() == "Default settings" && getParam (p, ClipIDs::quality) == 3.0f && getParam (p, ClipIDs::view) == 1.0f,
               "...which puts every knob back to its default ('Default settings'), leaving OVERSAMPLING and the view alone");

        // Help bar
        bool hintsOk = true;
        for (int k = 0; k < EasyClipAudioProcessorEditor::numKnobs; ++k)
        {
            window->mouseEnter (hoverEvent (&window->getKnob (k)));
            if (window->hintBar.getTitle().isEmpty() || window->hintBar.getText().length() < 40) hintsOk = false;
        }
        check (hintsOk, "every knob explains itself in the help bar");
    }

    //==========================================================================
    void testSpeed()
    {
       #if ! JUCE_DEBUG
        std::cout << "\n[speed]\n";
        auto loop = drumLoop (sr, 10.0, -3.0f, -6.0f, -18.0f);
        for (int quality = 0; quality < 3; ++quality)
        {
            P p; set (p, ClipIDs::quality, (float) (quality + 1)); loadPresetNamed (p, "Master - Synthwave Shine"); set (p, ClipIDs::quality, (float) (quality + 1));
            prepare (p, sr, 256);
            const double share = coreShare (p, loop);
            const double budget = quality == 0 ? 0.75 : (quality == 1 ? 3.0 : 5.0);
            check (share < budget, juce::String (quality == 0 ? "OFF" : quality == 1 ? "4x" : "8x") + ", stereo, 48 kHz: "
                                   + juce::String (share, 3) + "% of one core (budget " + juce::String (budget, 1) + "%)");
        }
       #endif
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());

    testDefaultsAndLatency();
    testCeiling();
    testShape();
    testProtectLows();
    testAliasing();
    testBlendAndBypass();
    testPluginParts();
    testWindow (outDir);
    testSpeed();
    return finish();
}
