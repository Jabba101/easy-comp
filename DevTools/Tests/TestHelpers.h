// Shared helpers for the plugin test programs (not shipped).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PresetBrowser.h"
#include "FastMath.h"
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <random>
#include <vector>
#include <map>

namespace th
{
    inline int checks = 0, failures = 0;

    inline void check (bool ok, const juce::String& what)
    {
        ++checks;
        if (! ok) ++failures;
        std::cout << (ok ? "  ok    " : "  FAIL  ") << what << "\n";
    }

    inline bool near (float a, float b, float tol)    { return std::abs (a - b) <= tol; }
    inline float toDb (float g)                       { return juce::Decibels::gainToDecibels (g, -200.0f); }
    inline float fromDb (float d)                     { return juce::Decibels::decibelsToGain (d, -200.0f); }
    constexpr int second = 48000;

    template <typename P> void setParam (P& p, const char* id, float value)
    {
        auto* prm = p.parameters.getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    }

    template <typename P> float getParam (P& p, const char* id)   { return p.parameters.getRawParameterValue (id)->load(); }

    template <typename P> void prepare (P& p, double sr, int block, int channels = 2)
    {
        p.setPlayConfigDetails (channels, channels, sr, block);
        p.prepareToPlay (sr, block);
    }

    template <typename P> void loadPresetNamed (P& p, const juce::String& name)
    {
        for (auto& preset : p.getBuiltInPresets())
            if (preset.name == name) { p.loadPreset (preset); return; }
        std::cout << "  (no preset named " << name << ")\n";
    }

    template <typename P>
    juce::AudioBuffer<float> run (P& p, const juce::AudioBuffer<float>& input, int block, std::function<void (int)> beforeBlock = {})
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

    inline juce::AudioBuffer<float> sine (double sr, double freq, float amp, double seconds, int channels = 2)
    {
        const int n = (int) (sr * seconds);
        juce::AudioBuffer<float> b (channels, n);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < n; ++i)
                b.setSample (ch, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / sr));
        return b;
    }

    inline float peak (const juce::AudioBuffer<float>& b, int ch, int start, int end)
    {
        float m = 0.0f;
        for (int i = start; i < end; ++i) m = std::max (m, std::abs (b.getSample (ch, i)));
        return m;
    }

    inline float rms (const juce::AudioBuffer<float>& b, int ch, int start, int end)
    {
        double sum = 0.0;
        for (int i = start; i < end; ++i) sum += (double) b.getSample (ch, i) * (double) b.getSample (ch, i);
        return (float) std::sqrt (sum / (double) juce::jmax (1, end - start));
    }

    inline float maxDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        float d = 0.0f;
        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
                d = std::max (d, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
        return d;
    }

    inline bool allFinite (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (ch, i))) return false;
        return true;
    }

    /** Kick on beats 1 & 3, snare on 2 & 4, hi-hat on 8ths, 120 BPM. Every whole second holds one kick and one snare. */
    inline juce::AudioBuffer<float> drumLoop (double sr, double seconds, float kickDb = -6.0f, float snareDb = -10.0f, float hatDb = -24.0f)
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
            if (beatIndex % 2 == 0) x += kick * std::exp (-sinceBeat / 0.12) * std::sin (twoPi * 55.0 * sinceBeat);
            else                    x += snare * std::exp (-sinceBeat / 0.08) * (0.6 * std::sin (twoPi * 190.0 * sinceBeat) + 0.4 * noise);
            x += hat * std::exp (-sinceEighth / 0.02) * noise;
            b.setSample (0, i, (float) x);
            b.setSample (1, i, (float) (x * 0.9));
        }
        return b;
    }

    /** One drum-like hit: an instant start and an exponential decay (tail time constant in seconds). */
    inline juce::AudioBuffer<float> singleHit (double sr, double seconds, float levelDb, double decaySeconds, int seed = 3)
    {
        const int n = (int) (sr * seconds);
        juce::AudioBuffer<float> b (2, n);
        juce::Random rng (seed);
        const double amp = (double) fromDb (levelDb);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            const double x = amp * std::exp (-t / decaySeconds) * (0.5 * std::sin (juce::MathConstants<double>::twoPi * 180.0 * t) + 0.5 * (rng.nextDouble() * 2.0 - 1.0));
            b.setSample (0, i, (float) x);
            b.setSample (1, i, (float) x);
        }
        return b;
    }

    /** Independent loudness measurement using the published 48 kHz BS.1770 K-weighting coefficients. */
    inline double loudnessDb (const juce::AudioBuffer<float>& b, int start, int end)
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

    inline juce::MouseEvent hoverEvent (juce::Component* c)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (source, {}, {}, juce::MouseInputSource::defaultPressure,
                                 juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                 juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                 c, c, now, {}, now, 0, false);
    }

    inline void savePng (juce::Component& c, const juce::File& file)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::cout << "  wrote " << file.getFullPathName() << " (" << image.getWidth() << "x" << image.getHeight() << ")\n";
    }

    /** Every preset has a unique name and a tip; the name fits the preset box even with
        " (edited)" after it; and name + tip fit the help bar's two lines. (The fonts on this
        test machine are a little wider than Windows' Segoe UI, so this errs on the safe side.) */
    inline std::pair<bool, juce::String> presetTextsReport (const std::vector<Preset>& presets)
    {
        constexpr float boxTextWidth = 370.0f - 24.0f - 22.0f;          // the preset box, minus padding and the arrow
        constexpr float hintWidth    = 1100.0f - 40.0f - 66.0f - 18.0f; // the help bar's text area
        const juce::Font boxFont (juce::FontOptions (15.0f));
        const juce::Font hintTitle (juce::FontOptions (14.5f, juce::Font::bold)), hintText (juce::FontOptions (14.5f));

        juce::StringArray names;
        juce::String problems;
        float widest = 0.0f;
        int mostLines = 0;

        for (auto& preset : presets)
        {
            if (names.contains (preset.name))  problems << "'" << preset.name << "' is there twice; ";
            if (preset.tip.isEmpty())          problems << "'" << preset.name << "' has no tip; ";
            names.add (preset.name);

            const float width = juce::GlyphArrangement::getStringWidth (boxFont, preset.name + " (edited)");
            widest = std::max (widest, width);
            if (width > boxTextWidth)
                problems << "'" << preset.name << "' is too wide for the preset box (" << juce::String (width, 0) << " px); ";

            juce::AttributedString message;
            message.append (preset.name + "    ", hintTitle);
            message.append (preset.tip, hintText);
            message.setWordWrap (juce::AttributedString::byWord);
            message.setLineSpacing (2.0f);
            juce::TextLayout layout;
            layout.createLayout (message, hintWidth);
            mostLines = std::max (mostLines, layout.getNumLines());
            if (layout.getNumLines() > 2)
                problems << "'" << preset.name << "' needs " << layout.getNumLines() << " lines in the help bar; ";
        }

        return { problems.isEmpty(), "all " + juce::String ((int) presets.size()) + " presets: unique names, a tip that fits the help bar ("
                                       + juce::String (mostLines) + " lines at most), a name that fits the preset box (widest "
                                       + juce::String (widest, 0) + " of " + juce::String (boxTextWidth, 0) + " px)"
                                       + (problems.isEmpty() ? juce::String() : ": " + problems) };
    }

    //==========================================================================
    // The optimisation pass: quick maths, skipping through silence, speed.
    //==========================================================================
    using Report = std::vector<std::pair<bool, juce::String>>;

    JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal")   // (exact answers are the point here)

    /** The quick dB maths against the standard library's: exact where it has
        to be, and close everywhere else. */
    inline Report fastMathReport()
    {
        Report report;

        bool exact = FastMath::gainToDecibels (1.0f, -120.0f) == 0.0f && FastMath::decibelsToGain (0.0f) == 1.0f
                  && FastMath::log2 (1.0f) == 0.0f && FastMath::exp2 (0.0f) == 1.0f;

        for (int k = -60; k <= 60; ++k)
            exact = exact && FastMath::log2 (std::ldexp (1.0f, k)) == (float) k && FastMath::exp2 ((float) k) == std::ldexp (1.0f, k);

        report.push_back ({ exact, "quick maths is exact at gain 1 = 0 dB, and at every power of two" });

        double worstToDb = 0.0, worstToGain = 0.0;
        bool rising = true;
        float previous = -1000.0f;

        for (int i = 0; i <= 2000000; ++i)
        {
            const float gain = (float) std::pow (10.0, -7.0 + 9.0 * i / 2000000.0);   // -140 dB to +40 dB
            const float db = FastMath::gainToDecibels (gain, -200.0f);
            worstToDb = std::max (worstToDb, std::abs ((double) db - 20.0 * std::log10 ((double) gain)));
            rising = rising && db >= previous;
            previous = db;

            const float level = -100.0f + 130.0f * (float) i / 2000000.0f;              // -100 dB to +30 dB
            const double exactGain = std::pow (10.0, (double) level / 20.0);
            worstToGain = std::max (worstToGain, std::abs (FastMath::decibelsToGain (level, -101.0f) / exactGain - 1.0));
        }

        report.push_back ({ worstToDb < 2.0e-5, "gain to dB within " + juce::String (worstToDb, 7) + " dB of the exact answer, -140 to +40 dB" });
        report.push_back ({ worstToGain < 1.0e-6, "dB to gain within " + juce::String (worstToGain * 100.0, 6) + "% (" + juce::String (8.6859 * worstToGain, 7)
                                                    + " dB) of the exact answer, -100 to +30 dB" });
        report.push_back ({ rising, "louder always reads as more dB (no steps backwards)" });

        report.push_back ({ FastMath::gainToDecibels (0.0f, -120.0f) == -120.0f && FastMath::gainToDecibels (1.0e-9f, -120.0f) == -120.0f
                              && FastMath::gainToDecibels (-0.5f, -120.0f) == -120.0f
                              && FastMath::decibelsToGain (-100.0f) == 0.0f && FastMath::decibelsToGain (-99.0f) > 0.0f,
                            "silence reads as minus infinity, and minus infinity as silence (like JUCE's own)" });

        // The four-at-a-time versions give exactly the same answers as one at a time
        std::mt19937 random (1);
        std::uniform_real_distribution<float> anyGain (0.0f, 4.0f), anyDb (-120.0f, 40.0f);
        bool same = true;

        for (int i = 0; i < 100000; ++i)
        {
            alignas (16) float gains[4], dbs[4], toDb[4], toGain[4];
            for (int j = 0; j < 4; ++j) { gains[j] = anyGain (random) * (j == 3 && i % 7 == 0 ? 0.0f : 1.0f); dbs[j] = anyDb (random); }
            FastMath::gainToDecibels (FastMath::Vec4::load (gains), -120.0f).store (toDb);
            FastMath::decibelsToGain (FastMath::Vec4::load (dbs), -100.0f).store (toGain);
            for (int j = 0; j < 4; ++j)
                same = same && toDb[j] == FastMath::gainToDecibels (gains[j], -120.0f) && toGain[j] == FastMath::decibelsToGain (dbs[j], -100.0f);
        }

        report.push_back ({ same, "four at a time gives exactly the same as one at a time" });
        return report;
    }

    /** A drum loop with stretches of digital silence cut into it (from 1 sample to 2 seconds long). */
    inline juce::AudioBuffer<float> withGaps (const juce::AudioBuffer<float>& source)
    {
        juce::AudioBuffer<float> b (source);
        const int gaps[][2] = { { 30000, 1 }, { 50000, 300 }, { 70000, 4800 }, { 100000, 24000 }, { 160000, 96000 } };   // (start, length)

        for (auto& gap : gaps)
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                if (gap[0] + gap[1] <= b.getNumSamples())
                    b.clear (ch, gap[0], gap[1]);

        return b;
    }

    /** Runs material through two of the same effect, one allowed to skip
        through silence and one not. Reports how far apart the two come out
        (dB below the peak, and the most any sample's level differs), and
        how many blocks were skipped through. */
    struct RestReport { double differenceDb, worstLevelDb; int blocksRested, blocks; };

    template <typename Effect>
    RestReport restReport (const juce::AudioBuffer<float>& input, double sampleRate, int block, typename Effect::Settings settings,
                           std::function<void (int, typename Effect::Settings&, bool&)> beforeBlock = {})
    {
        juce::ScopedNoDenormals noDenormals;
        auto skipping = std::make_unique<Effect>(), working = std::make_unique<Effect>();
        working->allowResting (false);
        skipping->prepare (sampleRate, settings);
        working->prepare (sampleRate, settings);

        juce::AudioBuffer<float> a (input), b (input);
        RestReport r { -999.0, 0.0, 0, 0 };
        double worst = 0.0, peakLevel = 0.0;

        for (int start = 0; start < input.getNumSamples(); start += block)
        {
            const int n = std::min (block, input.getNumSamples() - start);
            bool restart = false;
            if (beforeBlock) beforeBlock (start, settings, restart);
            skipping->setSettings (settings);
            working->setSettings (settings);
            if (restart) { skipping->restartAutoGain(); working->restartAutoGain(); }

            juce::AudioBuffer<float> va (a.getArrayOfWritePointers(), a.getNumChannels(), start, n);
            juce::AudioBuffer<float> vb (b.getArrayOfWritePointers(), b.getNumChannels(), start, n);
            skipping->process (va);
            working->process (vb);
            ++r.blocks;
            if (skipping->isResting()) ++r.blocksRested;
        }

        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
            {
                const double x = a.getSample (ch, i), y = b.getSample (ch, i);
                worst = std::max (worst, std::abs (x - y));
                peakLevel = std::max (peakLevel, std::abs (y));
                if (std::abs (y) > 1.0e-4)
                    r.worstLevelDb = std::max (r.worstLevelDb, x == 0.0 ? 999.0 : std::abs (20.0 * std::log10 (x / y)));
            }

        if (worst > 0.0)
            r.differenceDb = 20.0 * std::log10 (worst / std::max (peakLevel, 1.0e-30));

        return r;
    }

    JUCE_END_IGNORE_WARNINGS_GCC_LIKE

    /** The share of one processor core (in %) a plugin uses on some material,
        processed in blocks of 256 at 48 kHz (best of three goes, to be fair
        to a busy computer). */
    template <typename P>
    double coreShare (P& p, const juce::AudioBuffer<float>& material)
    {
        constexpr int block = 256;
        juce::AudioBuffer<float> work (material);
        juce::MidiBuffer midi;
        double best = 1.0e9;

        for (int go = 0; go < 3; ++go)
        {
            work.makeCopyOf (material);
            const auto started = std::chrono::steady_clock::now();

            for (int start = 0; start + block <= work.getNumSamples(); start += block)
            {
                juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), work.getNumChannels(), start, block);
                p.processBlock (view, midi);
            }

            const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count();
            best = std::min (best, 100.0 * seconds / ((double) work.getNumSamples() / 48000.0));
        }

        return best;
    }

    inline int finish()
    {
        std::cout << "\n" << (checks - failures) << " / " << checks << " checks passed\n";
        return failures == 0 ? 0 : 1;
    }
}
