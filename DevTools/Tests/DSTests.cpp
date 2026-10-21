// Verification harness for Easy DS (not shipped).

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TestHelpers.h"
#include "Parameters.h"
#include <juce_audio_formats/juce_audio_formats.h>

using namespace th;
using P = EasyDSAudioProcessor;

namespace
{
    constexpr double sr = 48000.0;
    const double twoPi = juce::MathConstants<double>::twoPi;
    constexpr int D = 96;   // the look-ahead at 48 kHz

    //==========================================================================
    bool prepareDS (P& p, double rate, int block, int channels = 2)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::canonicalChannelSet (channels));
        layout.outputBuses.add (juce::AudioChannelSet::canonicalChannelSet (channels));
        const bool ok = p.setBusesLayout (layout);
        p.setRateAndBufferSizeDetails (rate, block);
        p.prepareToPlay (rate, block);
        return ok;
    }

    juce::AudioBuffer<float> runDS (P& p, const juce::AudioBuffer<float>& in, int block = 512, std::function<void (int)> beforeBlock = {})
    {
        juce::AudioBuffer<float> out (in);
        juce::MidiBuffer midi;
        for (int start = 0; start < out.getNumSamples(); start += block)
        {
            if (beforeBlock) beforeBlock (start);
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), out.getNumChannels(), start, std::min (block, out.getNumSamples() - start));
            p.processBlock (view, midi);
        }
        return out;
    }

    /** The engine on its own, in blocks. */
    juce::AudioBuffer<float> runEngine (DeEsser& d, const juce::AudioBuffer<float>& in, int block = 512,
                                        std::function<void (int, DeEsser&)> beforeBlock = {}, std::vector<DeEsser::Moment>* moments = nullptr)
    {
        juce::ScopedNoDenormals noDenormals;   // (as the plugin does)
        juce::AudioBuffer<float> out (in);
        for (int start = 0; start < out.getNumSamples(); start += block)
        {
            if (beforeBlock) beforeBlock (start, d);
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), out.getNumChannels(), start, std::min (block, out.getNumSamples() - start));
            d.process (view, [&] (const DeEsser::Moment& m) { if (moments) moments->push_back (m); });
        }
        return out;
    }

    JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal")   // (exact answers are the point here)

    int samplesNotDelayedCopy (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out, int latency, int from = 0)
    {
        int count = 0;
        for (int ch = 0; ch < out.getNumChannels(); ++ch)
            for (int i = from; i < out.getNumSamples(); ++i)
                if (out.getSample (ch, i) != (i >= latency ? in.getSample (ch, i - latency) : 0.0f))
                    ++count;
        return count;
    }

    bool bitSame (float a, float b)    { return a == b; }

    bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples()) return false;
        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
                if (a.getSample (ch, i) != b.getSample (ch, i)) return false;
        return true;
    }

    JUCE_END_IGNORE_WARNINGS_GCC_LIKE

    //==========================================================================
    /** A vowel: a note's harmonics up to 3 kHz, fading in over 20 ms. */
    juce::AudioBuffer<float> vowel (double seconds, float db, double f0 = 220.0, int channels = 2)
    {
        juce::AudioBuffer<float> b (channels, (int) (seconds * sr));
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / sr;
            double x = 0.0;
            for (int k = 1; k * f0 < 3000.0; ++k)
                x += std::sin (twoPi * k * f0 * t + 0.3 * k) / k;
            x *= 0.6 * std::min (1.0, t / 0.02);
            for (int ch = 0; ch < channels; ++ch)
                b.setSample (ch, i, (float) (fromDb (db) * x));
        }
        return b;
    }

    /** Adds an S: noise from lo to hi Hz, at a level (dB peak, roughly), with 10 ms fades. */
    void addS (juce::AudioBuffer<float>& b, double start, double length, float db, double lo = 5000.0, double hi = 10000.0, int seed = 5, int onlyChannel = -1)
    {
        juce::Random rng (seed);
        juce::IIRFilter h1, h2, l1, l2;
        h1.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, lo)); h2.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, lo));
        l1.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, hi));  l2.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, hi));
        const int from = (int) (start * sr), n = std::min (b.getNumSamples() - from, (int) (length * sr));
        std::vector<float> s ((size_t) n);
        for (auto& x : s) x = rng.nextFloat() * 2.0f - 1.0f;
        h1.processSamples (s.data(), n); h2.processSamples (s.data(), n); l1.processSamples (s.data(), n); l2.processSamples (s.data(), n);
        float peakValue = 0.0f;
        for (auto x : s) peakValue = std::max (peakValue, std::abs (x));
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr, fade = std::min ({ 1.0, t / 0.01, ((n - i) / sr) / 0.01 });
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                if (onlyChannel < 0 || ch == onlyChannel)
                    b.addSample (ch, from + i, (float) (fade * fromDb (db) * s[(size_t) i] / peakValue));
        }
    }

    /** A vowel, a gap with an S in it (0.6 to 0.85 s), then the vowel again - like "yes, yes". */
    juce::AudioBuffer<float> speechLike (int onlyChannel = -1)
    {
        auto b = vowel (2.0, -10.0f);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double t = i / sr;
                const double fade = juce::jlimit (0.0, 1.0, std::max ((0.58 - t) / 0.02, (t - 0.85) / 0.02));
                if (onlyChannel < 0 || ch == onlyChannel) b.setSample (ch, i, b.getSample (ch, i) * (float) fade);
            }
        addS (b, 0.6, 0.25, -14.0f, 5000.0, 10000.0, 5, onlyChannel);
        return b;
    }

    juce::AudioBuffer<float> tone (double freq, float db, double seconds, int channels = 2)
    {
        juce::AudioBuffer<float> b (channels, (int) (seconds * sr));
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / sr;
            for (int ch = 0; ch < channels; ++ch)
                b.setSample (ch, i, (float) (fromDb (db) * std::min (1.0, t / 0.02) * std::sin (twoPi * freq * t)));
        }
        return b;
    }

    juce::AudioBuffer<float> sum (juce::AudioBuffer<float> a, const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            a.addFrom (ch, 0, b, std::min (ch, b.getNumChannels() - 1), 0, std::min (a.getNumSamples(), b.getNumSamples()));
        return a;
    }

    /** Energy (dB) of channel ch between two times, optionally only above a frequency. */
    double energyDb (const juce::AudioBuffer<float>& b, int ch, double from, double to, double aboveHz = 0.0, int delay = 0)
    {
        std::vector<float> x ((size_t) b.getNumSamples());
        for (int i = 0; i < b.getNumSamples(); ++i) x[(size_t) i] = b.getSample (ch, i);
        if (aboveHz > 0.0)
        {
            juce::IIRFilter h1, h2;
            h1.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, aboveHz)); h2.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, aboveHz));
            h1.processSamples (x.data(), (int) x.size()); h2.processSamples (x.data(), (int) x.size());
        }
        double e = 0.0;
        for (int i = (int) (from * sr) + delay; i < std::min ((int) x.size(), (int) (to * sr) + delay); ++i) e += (double) x[(size_t) i] * x[(size_t) i];
        return 10.0 * std::log10 (e + 1.0e-30);
    }

    /** The largest jump from one sample to the next (a click shows up as a big one). */
    float biggestStep (const juce::AudioBuffer<float>& b, int ch = 0, int from = 1)
    {
        float m = 0.0f;
        for (int i = std::max (1, from); i < b.getNumSamples(); ++i)
            m = std::max (m, std::abs (b.getSample (ch, i) - b.getSample (ch, i - 1)));
        return m;
    }

    juce::AudioBuffer<float> loadMaterial (const juce::String& name)
    {
        juce::WavAudioFormat wav;
        const juce::File file ("/home/claude/presetcheck/ds_material/" + name + ".wav");
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
        juce::AudioBuffer<float> b;
        if (reader == nullptr) { std::cout << "  (couldn't read " << name << ")\n"; return b; }
        b.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&b, 0, (int) reader->lengthInSamples, 0, true, true);
        return b;
    }

    juce::AudioBuffer<float> slice (const juce::AudioBuffer<float>& source, double from, double seconds)
    {
        juce::AudioBuffer<float> b (2, (int) (seconds * sr));
        for (int ch = 0; ch < 2; ++ch)
            b.copyFrom (ch, 0, source, std::min (ch, source.getNumChannels() - 1), (int) (from * sr), b.getNumSamples());
        return b;
    }

    bool fitsHelpBar (const juce::String& title, const juce::String& text, int& lines)
    {
        constexpr float hintWidth = 1100.0f - 40.0f - 66.0f - 18.0f;
        juce::AttributedString message;
        message.append (title + "    ", juce::Font (juce::FontOptions (14.5f, juce::Font::bold)));
        message.append (text, juce::Font (juce::FontOptions (14.5f)));
        message.setWordWrap (juce::AttributedString::byWord);
        message.setLineSpacing (2.0f);
        juce::TextLayout layout;
        layout.createLayout (message, hintWidth);
        lines = layout.getNumLines();
        return lines <= 2;
    }

    DeEsser::Settings settingsWith (float fromHz, float toHz, float thresholdDb, float rangeDb, bool whole = false)
    {
        DeEsser::Settings s;
        s.fromHz = fromHz; s.toHz = toHz; s.thresholdDb = thresholdDb; s.rangeDb = rangeDb; s.wholeSound = whole;
        return s;
    }

    //==========================================================================
    void testParameters()
    {
        std::cout << "\n[parameters, latency and inputs]\n";
        P p;
        check (p.getParameters().size() == 6, "6 settings visible to the DAW (FROM, TO, THRESHOLD, RANGE, TURNS DOWN, BYPASS) - got " + juce::String (p.getParameters().size()));
        check (! p.hasAutoGain() && p.parameters.getParameter (CommonIDs::autoGain) == nullptr, "no AUTO-GAIN (a de-esser hardly changes how loud your track is)");
        check (p.getBypassParameter() == p.parameters.getParameter (CommonIDs::bypass), "BYPASS is the DAW's own bypass switch");

        const auto& d = dsPresets[0];
        check (near (getParam (p, DSIDs::from), d.fromHz, 0.5f) && near (getParam (p, DSIDs::to), DeEsser::toOffHz, 0.5f)
                 && near (getParam (p, DSIDs::threshold), d.thresholdDb, 1e-4f) && near (getParam (p, DSIDs::range), d.rangeDb, 1e-4f)
                 && getParam (p, DSIDs::turnsDown) < 0.5f,
               "defaults match 'Default settings': FROM " + Format::hertz (d.fromHz) + ", TO off, TURNS DOWN: HARSH BAND");

        auto text = [&] (const char* id, float v) { auto* prm = p.parameters.getParameter (id); return prm->getText (prm->convertTo0to1 (v), 0); };
        auto typed = [&] (const char* id, const char* t) { auto* prm = p.parameters.getParameter (id); return prm->convertFrom0to1 (prm->getValueForText (t)); };

        check (text (DSIDs::from, 5000.0f) == "5.0 kHz" && text (DSIDs::to, 20000.0f) == "Off" && text (DSIDs::to, 9000.0f) == "9.0 kHz",
               "FROM reads '5.0 kHz'; TO '9.0 kHz', and 'Off' at the top");
        check (text (DSIDs::threshold, -8.0f) == "-8.0 dB" && text (DSIDs::threshold, 6.0f) == "+6.0 dB" && text (DSIDs::range, 6.0f) == "6.0 dB",
               "THRESHOLD reads '-8.0 dB' or '+6.0 dB'; RANGE '6.0 dB'");
        check (near (typed (DSIDs::from, "6.3k"), 6300.0f, 1.0f) && near (typed (DSIDs::to, "off"), 20000.0f, 1.0f) && near (typed (DSIDs::to, "9 kHz"), 9000.0f, 1.0f)
                 && near (typed (DSIDs::range, "-6"), 6.0f, 0.05f),
               "typing works: '6.3k', '9 kHz', 'off', and '-6' or '6' for RANGE");

        bool latencyOk = true;
        juce::String latencies;
        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            P q;
            prepareDS (q, rate, 512);
            latencyOk = latencyOk && q.getLatencySamples() == (int) std::lround (rate * 0.002);
            latencies << q.getLatencySamples() << " ";
        }
        check (latencyOk, "it tells the DAW about the 2 ms look-ahead: " + latencies.trim() + " samples at 44.1, 48, 96 and 192 kHz");

        bool layoutsOk = true;
        for (int channels : { 1, 2 })
        {
            P q;
            layoutsOk = layoutsOk && prepareDS (q, sr, 512, channels);
        }
        juce::AudioProcessor::BusesLayout bad;
        bad.inputBuses.add (juce::AudioChannelSet::stereo()); bad.outputBuses.add (juce::AudioChannelSet::mono());
        check (p.getBusCount (true) == 1 && p.getBusCount (false) == 1 && layoutsOk && ! p.checkBusesLayoutSupported (bad),
               "mono or stereo tracks, in and out matching (no sidechain)");
    }

    //==========================================================================
    void testDeEssing()
    {
        std::cout << "\n[de-essing]\n";

        // Ordinary sound: untouched, bit for bit, whichever way it turns down
        {
            bool allExact = true;
            juce::String detail;
            const auto in = sum (vowel (3.0, -6.0f), vowel (3.0, -12.0f, 330.0));
            for (int mode = 0; mode < 3; ++mode)
            {
                DeEsser d;
                d.prepare (sr, 240, settingsWith (5000.0f, mode == 1 ? 9000.0f : 20000.0f, -8.0f, 6.0f, mode == 2));
                const int changed = samplesNotDelayedCopy (in, runEngine (d, in), D);
                allExact = allExact && changed == 0;
                detail << changed << " ";
            }
            check (allExact, "a vowel with nothing harsh comes out bit for bit untouched (just 2 ms later), with all three ways of turning down");
        }

        // An S between two vowels: turned down while it lasts, untouched again straight after
        {
            auto in = speechLike();
            DeEsser d;
            d.prepare (sr, 240, settingsWith (5000.0f, 20000.0f, -8.0f, 6.0f, true));
            const auto out = runEngine (d, in);
            const double during = energyDb (out, 0, 0.65, 0.8, 0.0, D) - energyDb (in, 0, 0.65, 0.8);
            check (near ((float) during, -6.0f, 0.15f), "WHOLE SOUND: during an S, everything is down by exactly RANGE (6 dB): " + juce::String (during, 2) + " dB");
            check (samplesNotDelayedCopy (in, out, D, (int) ((0.6 + 0.25 + 0.2) * sr)) == 0, "...and 200 ms after the S, the vowel is bit for bit untouched again");
            const double before = energyDb (out, 0, 0.2, 0.55, 0.0, D) - energyDb (in, 0, 0.2, 0.55);
            check (std::abs (before) < 0.001, "...and untouched before it too (" + juce::String (before, 4) + " dB)");
        }
        {
            auto in = speechLike();
            DeEsser d;
            d.prepare (sr, 240, settingsWith (5000.0f, 20000.0f, -8.0f, 6.0f));
            const auto out = runEngine (d, in);
            const double sBand = energyDb (out, 0, 0.65, 0.8, 7000.0, D) - energyDb (in, 0, 0.65, 0.8, 7000.0);
            check (sBand < -5.0 && sBand > -6.5, "HARSH BAND: during an S, the S itself (above 7 kHz) comes down by about RANGE: " + juce::String (sBand, 2) + " dB");
        }

        // The shapes of the three ways of turning down (a forced 12 dB cut, measured with quiet probe tones)
        {
            struct Shape { const char* name; float to; bool whole; float triggerHz; };
            const Shape shapes[] = { { "HARSH BAND, TO off (a shelf above FROM)", 20000.0f, false, 12000.0f },
                                     { "HARSH BAND, 5-9 kHz (a dip between FROM and TO)", 9000.0f, false, 6708.0f },
                                     { "WHOLE SOUND", 20000.0f, true, 12000.0f } };
            const double probes[] = { 1000.0, 2500.0, 5000.0, 6708.0, 9000.0, 16000.0 };

            for (auto& shape : shapes)
            {
                std::map<int, double> change;
                for (double probe : probes)
                {
                    const auto trigger = tone (shape.triggerHz, -10.0f, 1.0);
                    const auto with = sum (trigger, tone (probe, -60.0f, 1.0));
                    DeEsser a, b;
                    a.prepare (sr, 240, settingsWith (5000.0f, shape.to, -8.0f, 12.0f, shape.whole));
                    b.prepare (sr, 240, settingsWith (5000.0f, shape.to, -8.0f, 12.0f, shape.whole));
                    const auto outWith = runEngine (a, with), outWithout = runEngine (b, trigger);
                    double e = 0.0, ein = 0.0;
                    for (int i = 24000; i < 48000; ++i)
                    {
                        const double x = outWith.getSample (0, i) - outWithout.getSample (0, i);
                        const double y = fromDb (-60.0f) * std::sin (twoPi * probe * (i - D) / sr);
                        e += x * x; ein += y * y;
                    }
                    change[(int) probe] = 10.0 * std::log10 (e / ein);
                }

                juce::String values;
                for (double probe : probes) values << juce::String ((int) probe) << ": " << juce::String (change[(int) probe], 1) << "  ";

                bool ok = false;
                if (shape.whole)
                    ok = std::all_of (std::begin (probes), std::end (probes), [&] (double f) { return near ((float) change[(int) f], -12.0f, 0.1f); });
                else if (shape.to > 19000.0f)
                    ok = change[1000] > -0.2 && change[2500] > -1.5 && near ((float) change[5000], -6.0f, 0.6f) && change[9000] < -10.5 && change[16000] < -11.5;
                else
                    ok = near ((float) change[6708], -12.0f, 0.3f) && change[5000] > -7.5 && change[5000] < -3.0 && change[9000] > -7.5 && change[9000] < -3.0
                      && change[1000] > -0.3 && change[16000] > -0.6;

                check (ok, juce::String (shape.name) + " (dB at Hz): " + values.trim());
            }
        }

        // THRESHOLD: the display's numbers and the sound agree - it turns down by however far the harsh band goes over the line
        {
            bool agree = true, rising = true;
            float previous = -1.0f;
            juce::String values;
            for (float bandDb : { -40.0f, -30.0f, -25.0f, -20.0f, -16.0f, -12.0f, -8.0f })
            {
                const auto in = sum (vowel (3.0, -10.0f), tone (7000.0, bandDb, 3.0));
                DeEsser d;
                std::vector<DeEsser::Moment> moments;
                d.prepare (sr, 240, settingsWith (5000.0f, 20000.0f, -8.0f, 24.0f, true));
                runEngine (d, in, 512, {}, &moments);
                const auto& m = moments[moments.size() - 2];
                const float band = 10.0f * std::log10 (m.bandLevel), line = DeEsser::thresholdLineDb (10.0f * std::log10 (m.lineLevel), -8.0f);
                const float over = band - line, reduction = -toDb (m.gain);
                const float expected = over <= -3.0f ? 0.0f : (over >= 3.0f ? over : (over + 3.0f) * (over + 3.0f) / 12.0f);
                agree = agree && std::abs (reduction - expected) < 0.25f;
                rising = rising && reduction >= previous - 0.01f;
                previous = reduction;
                values << juce::String (over, 1) << "->" << juce::String (reduction, 1) << "  ";
            }
            check (agree && rising, "it turns down by however far the harsh band goes over the THRESHOLD line (easing in over 6 dB) - over -> turned down: " + values.trim());
        }

        // RANGE 0: nothing changes, however harsh
        {
            auto in = vowel (2.0, -10.0f);
            addS (in, 0.5, 0.5, -6.0f);
            DeEsser d;
            d.prepare (sr, 240, settingsWith (5000.0f, 20000.0f, -20.0f, 0.0f));
            check (samplesNotDelayedCopy (in, runEngine (d, in), D) == 0, "RANGE 0: nothing changes, bit for bit, however harsh");
        }

        // Stereo: both channels always get the same change; mono works; stereo four-at-once = one at a time
        {
            auto in = speechLike (0);   // an S in the left channel only (the right just holds its vowel)
            P p; setParam (p, DSIDs::turnsDown, 1.0f); prepareDS (p, sr, 512);
            const auto out = runDS (p, in);
            float worst = 0.0f, deepest = 1.0f;
            for (int i = (int) (0.62 * sr); i < (int) (0.83 * sr); ++i)
            {
                if (std::abs (in.getSample (0, i)) < 1.0e-3f || std::abs (in.getSample (1, i)) < 1.0e-3f) continue;
                const float left = out.getSample (0, i + D) / in.getSample (0, i), right = out.getSample (1, i + D) / in.getSample (1, i);
                worst = std::max (worst, std::abs (left - right));
                deepest = std::min (deepest, right);
            }
            check (worst < 1.0e-4f && deepest < 0.9f, "an S in one channel turns both down together, by the same amount (the other channel, holding a vowel, dips to "
                                                        + juce::String (toDb (deepest), 1) + " dB with it)");

            // the same sound in both channels comes out exactly as the same sound in mono would
            auto speech = loadMaterial ("speech_female");
            bool same = speech.getNumSamples() > 0;
            for (int mode = 0; same && mode < 3; ++mode)
            {
                const auto s = settingsWith (4500.0f, mode == 1 ? 9000.0f : 20000.0f, -10.0f, 8.0f, mode == 2);
                juce::AudioBuffer<float> stereo (2, 10 * second), mono (1, 10 * second);
                for (int ch = 0; ch < 2; ++ch) stereo.copyFrom (ch, 0, speech, 0, 0, 10 * second);
                mono.copyFrom (0, 0, speech, 0, 0, 10 * second);
                DeEsser a, b;
                a.prepare (sr, 240, s); b.prepare (sr, 240, s);
                const auto outStereo = runEngine (a, stereo), outMono = runEngine (b, mono);
                for (int i = 0; same && i < outMono.getNumSamples(); ++i)
                    same = bitSame (outStereo.getSample (0, i), outMono.getSample (0, i)) && bitSame (outStereo.getSample (1, i), outMono.getSample (0, i));
            }
            check (same, "stereo (worked out four filters at a time) gives exactly what mono (one at a time) gives, with all three ways of turning down");

            // and with more channels (the general version)
            juce::AudioBuffer<float> four (4, 5 * second), two (2, 5 * second);
            for (int ch = 0; ch < 4; ++ch) four.copyFrom (ch, 0, speech, 0, 0, 5 * second);
            for (int ch = 0; ch < 2; ++ch) two.copyFrom (ch, 0, speech, 0, 0, 5 * second);
            DeEsser a, b;
            a.prepare (sr, 240, settingsWith (4500.0f, 20000.0f, -10.0f, 8.0f)); b.prepare (sr, 240, settingsWith (4500.0f, 20000.0f, -10.0f, 8.0f));
            const auto outFour = runEngine (a, four), outTwo = runEngine (b, two);
            float apart = 0.0f;
            for (int i = 0; i < outTwo.getNumSamples(); ++i) apart = std::max (apart, std::abs (outFour.getSample (3, i) - outTwo.getSample (0, i)));
            check (apart < 1.0e-6f, "four channels work too (the same as stereo, to " + juce::String (toDb (std::max (apart, 1.0e-12f)), 0) + " dB)");
        }

        // Buffer size and sample rate don't matter
        {
            const auto speech = slice (loadMaterial ("speech_male"), 0.0, 6.0);
            DeEsser reference;
            reference.prepare (sr, 240, settingsWith (4000.0f, 20000.0f, -8.0f, 6.0f));
            const auto expected = runEngine (reference, speech, 512);
            bool same = true;
            for (int block : { 1, 7, 64, 100, 997, 4096 })
            {
                DeEsser d; d.prepare (sr, 240, settingsWith (4000.0f, 20000.0f, -8.0f, 6.0f));
                same = same && identical (runEngine (d, speech, block), expected);
            }
            check (same, "the result doesn't depend on the DAW's buffer size: bit for bit the same with blocks of 1 to 4096 samples");
        }
        {
            juce::String values;
            bool same = true;
            float first = 0.0f;
            for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            {
                juce::AudioBuffer<float> in (2, (int) (rate * 1.5));
                juce::Random rng (3);
                for (int i = 0; i < in.getNumSamples(); ++i)
                {
                    const double t = i / rate;
                    double x = 0.0;
                    for (int k = 1; k * 220.0 < 3000.0; ++k) x += std::sin (twoPi * k * 220.0 * t) / k;
                    const float s = (t > 0.8 && t < 1.1) ? 0.3f * (float) std::sin (twoPi * 7000.0 * t) : 0.0f;
                    for (int ch = 0; ch < 2; ++ch) in.setSample (ch, i, (float) (0.15 * x) + s);
                }
                DeEsser d; d.prepare (rate, (int) (rate * 0.005), settingsWith (5000.0f, 20000.0f, -8.0f, 6.0f, true));
                std::vector<DeEsser::Moment> moments;
                runEngine (d, in, 512, {}, &moments);
                const float most = -toDb (moments[(size_t) (1.0 / 0.005)].gain);
                if (rate < 45000.0) first = most;
                same = same && near (most, first, 0.2f);
                values << juce::String (most, 2) << " ";
            }
            check (same, "the same amount at 44.1, 48, 96 and 192 kHz (dB turned down mid-S: " + values.trim() + ")");
        }

        // No clicks: WHOLE SOUND on a deep note with S sounds, and switching TURNS DOWN in the middle of an S
        {
            auto in = tone (110.0, -8.0f, 3.0);
            for (double t : { 0.5, 1.2, 1.9 }) addS (in, t, 0.2, -10.0f, 4500.0, 11000.0, (int) (t * 10));
            DeEsser d; d.prepare (sr, 240, settingsWith (4500.0f, 20000.0f, -10.0f, 10.0f, true));
            const auto out = runEngine (d, in);
            // the low note alone, turned down by 10 dB: its own biggest step, for comparison
            const auto low = tone (110.0, -8.0f, 3.0);
            juce::AudioBuffer<float> outLow (out);
            // (the deep note after a band-split: keep only below 1 kHz in both)
            juce::IIRFilter a1, a2, b1, b2;
            a1.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, 1000.0)); a2.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, 1000.0));
            b1.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, 1000.0)); b2.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, 1000.0));
            a1.processSamples (outLow.getWritePointer (0), outLow.getNumSamples()); a2.processSamples (outLow.getWritePointer (0), outLow.getNumSamples());
            juce::AudioBuffer<float> inLow (low);
            b1.processSamples (inLow.getWritePointer (0), inLow.getNumSamples()); b2.processSamples (inLow.getWritePointer (0), inLow.getNumSamples());
            const float step = biggestStep (outLow, 0, 2400), ownStep = biggestStep (inLow, 0, 2400);
            check (step < ownStep * 1.05f, "WHOLE SOUND dips smoothly: no clicks on a deep note as each S comes and goes (largest step "
                                             + juce::String (step / ownStep, 3) + "x the note's own)");
        }
        {
            const auto in = tone (9000.0, -10.0f, 2.0);
            DeEsser d; auto s = settingsWith (5000.0f, 20000.0f, -8.0f, 12.0f);
            d.prepare (sr, 240, s);
            const auto out = runEngine (d, in, 480, [&] (int start, DeEsser& e)
            {
                const int n = start / 4800;
                s.wholeSound = n % 3 == 1;
                s.toHz = n % 3 == 2 ? 11000.0f : 20000.0f;
                e.setSettings (s);
            });
            const float step = biggestStep (out, 0, 9600), turnedDown = biggestStep (in, 0, 9600) * fromDb (-11.0f);
            check (step < turnedDown * 1.6f, "switching TURNS DOWN (and TO on and off) in the middle of an S fades smoothly - no clicks (largest step "
                                               + juce::String (step / turnedDown, 2) + "x the turned-down tone's own)");
        }

        // Extreme settings and nasty input
        {
            juce::AudioBuffer<float> in (2, 48000);
            juce::Random rng (9);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 24000; ++i)
                    in.setSample (ch, i, i % 1000 < 3 ? 1.0e6f : (rng.nextFloat() * 2.0f - 1.0f) * (i % 7 == 0 ? 1.0e-38f : 4.0f));
            for (int ch = 0; ch < 2; ++ch) in.clear (ch, 24000, 24000);
            bool ok = true;
            for (auto s : { settingsWith (1000.0f, 2000.0f, -30.0f, 24.0f), settingsWith (16000.0f, 20000.0f, 20.0f, 24.0f),
                            settingsWith (16000.0f, 2000.0f, -30.0f, 24.0f, true), settingsWith (8000.0f, 8000.0f, -30.0f, 24.0f) })
            {
                DeEsser d; d.prepare (sr, 240, s);
                const auto out = runEngine (d, in);
                const bool finite = allFinite (out);
                const float after = peak (out, 0, 48000 - 4800, 48000);   // (the last 100 ms, half a second after the noise stops)
                DeEsser e; e.prepare (44100.0, 220, s);
                const bool finite44 = allFinite (runEngine (e, in));
                if (! finite || after > 0.0f || ! finite44)
                    std::cout << "      (FROM " << s.fromHz << " TO " << s.toHz << ": finite " << finite << " " << finite44 << ", after the noise stops: " << after << ")\n";
                ok = ok && finite && finite44 && bitSame (after, 0.0f);
            }
            check (ok, "extreme settings (FROM and TO the same, swapped, at the very top) and nasty input (+120 dB spikes, tiny numbers): no NaN/inf, and silence after it is silent again");
        }
    }

    //==========================================================================
    void testListenAndBypass()
    {
        std::cout << "\n[LISTEN and BYPASS]\n";
        auto in = speechLike();
        {
            P p; prepareDS (p, sr, 512);
            check (! p.isListening(), "LISTEN starts off");
            const auto normal = runDS (p, in);
            P q; prepareDS (q, sr, 512);
            q.setListening (true);
            const auto taken = runDS (q, in);
            float worst = 0.0f;
            for (int i = (int) (0.05 * sr); i < in.getNumSamples(); ++i)   // (after LISTEN's 20 ms fade in)
                worst = std::max (worst, std::abs (normal.getSample (0, i) + taken.getSample (0, i) - in.getSample (0, i - D)));
            check (worst < 1.0e-6f, "LISTEN plays exactly what's taken out: added back to the de-essed sound, it gives the original (to "
                                      + juce::String (toDb (std::max (worst, 1.0e-12f)), 0) + " dB)");
            check (bitSame (peak (taken, 0, (int) (0.2 * sr), (int) (0.55 * sr)), 0.0f) && energyDb (taken, 0, 0.65, 0.85) > energyDb (in, 0, 0.65, 0.85) - 12.0,
                   "...so on an ordinary vowel it's silent, and on an S you hear the part being removed");
            juce::MemoryBlock state;
            q.getStateInformation (state);
            P b; b.setStateInformation (state.getData(), (int) state.getSize());
            check (! b.isListening(), "LISTEN isn't saved with the project (so it can't be left on by accident)");
        }
        {
            P p; prepareDS (p, sr, 512);
            p.setListening (true);
            {
                std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
                auto* window = dynamic_cast<EasyDSAudioProcessorEditor*> (editor.get());
                check (window != nullptr && window->listenButton.getToggleState(), "the window's LISTEN button shows it's on");
            }
            check (! p.isListening(), "closing the window switches LISTEN off");
        }
        {
            P p; setParam (p, CommonIDs::bypass, 1.0f); prepareDS (p, sr, 512);
            check (samplesNotDelayedCopy (in, runDS (p, in), D) == 0, "bypassed: the original, bit for bit (2 ms later, so it stays in time)");
        }
        {
            // Flicking BYPASS and LISTEN while an S plays: smooth fades, no clicks
            const auto steady = sum (tone (300.0, -10.0f, 3.0), tone (8000.0, -14.0f, 3.0));
            P p; prepareDS (p, sr, 480);
            const auto out = runDS (p, steady, 480, [&] (int start)
            {
                const int n = start / 7200;
                setParam (p, CommonIDs::bypass, n % 3 == 1 ? 1.0f : 0.0f);
                p.setListening (n % 3 == 2);
            });
            const float worstStep = biggestStep (out, 0, 9600), ownStep = biggestStep (steady, 0, 9600);
            check (worstStep < ownStep * 1.5f, "flicking BYPASS and LISTEN while playing: smooth fades, no clicks (largest step "
                                                 + juce::String (worstStep / ownStep, 2) + "x the sound's own)");
        }
    }

    //==========================================================================
    void testFind()
    {
        std::cout << "\n[FIND]\n";
        {
            HarshnessFinder::Listener listener;
            listener.prepare (sr);
            std::vector<HarshnessFinder::Frame> frames;
            const auto t = tone (6300.0, -10.0f, 0.5);
            listener.process (t, [&] (const HarshnessFinder::Frame& f) { frames.push_back (f); });
            const auto& f = frames.back();
            const auto loudest = std::max_element (f.bands.begin(), f.bands.end()) - f.bands.begin();
            check (frames.size() == 50 && loudest == 5 && near (10.0f * std::log10 (f.bands[5]), -10.0f, 0.5f) && near (10.0f * std::log10 (f.total), -10.0f, 0.2f)
                     && f.low < f.bands[5] * 0.01f,
                   "FIND listens in thirds of an octave: half a second = 50 frames, and a -10 dB 6.3 kHz tone lands in its own band at "
                   + juce::String (10.0f * std::log10 (f.bands[5]), 1) + " dB");
        }

        const auto findOn = [] (const juce::AudioBuffer<float>& material)
        {
            HarshnessFinder::Listener listener;
            listener.prepare (sr);
            std::vector<HarshnessFinder::Frame> frames;
            listener.process (material, [&] (const HarshnessFinder::Frame& f) { frames.push_back (f); });
            return HarshnessFinder::analyse (frames, [&] (int b) { return listener.isUsable (b); });
        };
        using Outcome = HarshnessFinder::Result::Outcome;
        {
            auto in = vowel (8.0, -10.0f);
            for (double t = 0.5; t < 7.5; t += 0.7) addS (in, t, 0.15, -14.0f, 6000.0, 9000.0, (int) (t * 10));
            const auto r = findOn (in);
            check (r.outcome == Outcome::found && r.fromHz > 4400.0f && r.fromHz < 6200.0f && r.toHz > 8500.0f && r.toHz < 12000.0f,
                   "S sounds between 6 and 9 kHz: FIND sets FROM " + Format::hertz (r.fromHz) + ", TO " + Format::hertz (r.toHz));
        }
        {
            const auto r = findOn (vowel (8.0, -10.0f));
            check (r.outcome == Outcome::nothingStoodOut, "a voice with no S sounds: FIND says nothing harsh stood out (and changes nothing)");
            const auto quiet = findOn (vowel (1.0, -10.0f));
            juce::AudioBuffer<float> silence (2, 10 * second); silence.clear();
            const auto none = findOn (silence);
            check (quiet.outcome == Outcome::notEnoughSound && none.outcome == Outcome::notEnoughSound, "a second of sound, or silence: not enough to go on");
        }
        {
            const auto speech = loadMaterial ("speech_male");
            const auto r = findOn (slice (speech, 0.0, 12.0));
            check (r.outcome == Outcome::found && r.fromHz > 3500.0f && r.fromHz < 6000.0f,
                   "real recorded speech (male): the harshness is from " + Format::hertz (r.fromHz) + (r.toHz > 19000.0f ? juce::String (" up") : " to " + Format::hertz (r.toHz)));
            const auto oh = loadMaterial ("overheads_cymbals");
            const auto c = findOn (slice (oh, 0.0, 12.0));
            check (c.outcome == Outcome::found && c.fromHz < 4000.0f,
                   "a real drum kit's overheads with loud crashes: from " + Format::hertz (c.fromHz) + (c.toHz > 19000.0f ? juce::String (" up") : " to " + Format::hertz (c.toHz)));
        }

        // In the plugin window: click FIND, play, and FROM and TO are set
        {
            P p; prepareDS (p, sr, 512);
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            auto* window = dynamic_cast<EasyDSAudioProcessorEditor*> (editor.get());
            if (window == nullptr) { check (false, "the window opens"); return; }
            loadPresetNamed (p, "Default settings");

            window->findButton.onClick();
            check (p.isFinding() && window->findButton.getToggleState() && window->display.getFindTitle() == "FINDING THE HARSHNESS",
                   "clicking FIND starts it listening: the button lights and the display says so");

            auto in = vowel (10.0, -10.0f);
            for (double t = 0.5; t < 9.5; t += 0.7) addS (in, t, 0.15, -14.0f, 6000.0, 9000.0, (int) (t * 10));
            runDS (p, in, 512, [&] (int start) { if (start % 16384 == 0) window->findStep(); });
            window->findStep();
            const float from = getParam (p, DSIDs::from), to = getParam (p, DSIDs::to);
            check (! p.isFinding() && ! window->findButton.getToggleState() && from > 4400.0f && from < 6200.0f && to > 8500.0f && to < 12000.0f
                     && window->display.getFindTitle().startsWith ("FOUND IT"),
                   "after 8 seconds of sound it stops by itself and sets FROM " + Format::hertz (from) + " and TO " + Format::hertz (to)
                     + " (\"" + window->display.getFindTitle() + "\")");
            check (p.isPresetEdited(), "...which counts as changing the preset (it shows as edited)");

            window->findButton.onClick();
            runDS (p, vowel (1.0, -10.0f));
            window->findButton.onClick();
            check (! p.isFinding() && window->display.getFindTitle() == "NOT ENOUGH SOUND" && near (getParam (p, DSIDs::from), from, 0.5f),
                   "clicking FIND again stops it early; with too little to go on it says so and leaves FROM and TO alone");

            window->findButton.onClick();
            check (p.isFinding(), "(FIND on again...)");
        }
    }

    //==========================================================================
    /** Runs material through two de-essers, one allowed to skip through
        silence and one not, and compares (sound and display). */
    RestReport dsRestReport (const juce::AudioBuffer<float>& input, int block, DeEsser::Settings settings,
                             std::function<void (int, DeEsser::Settings&)> beforeBlock = {})
    {
        juce::ScopedNoDenormals noDenormals;
        auto skipping = std::make_unique<DeEsser>(), working = std::make_unique<DeEsser>();
        working->allowResting (false);
        skipping->prepare (sr, 240, settings);
        working->prepare (sr, 240, settings);

        juce::AudioBuffer<float> a (input), b (input);
        RestReport r { -999.0, 0.0, 0, 0 };
        std::vector<DeEsser::Moment> momentsA, momentsB;

        for (int start = 0; start < input.getNumSamples(); start += block)
        {
            const int n = std::min (block, input.getNumSamples() - start);
            if (beforeBlock) beforeBlock (start, settings);
            skipping->setSettings (settings);
            working->setSettings (settings);
            juce::AudioBuffer<float> va (a.getArrayOfWritePointers(), a.getNumChannels(), start, n);
            juce::AudioBuffer<float> vb (b.getArrayOfWritePointers(), b.getNumChannels(), start, n);
            skipping->process (va, [&] (const DeEsser::Moment& m) { momentsA.push_back (m); });
            working->process (vb, [&] (const DeEsser::Moment& m) { momentsB.push_back (m); });
            ++r.blocks;
            if (skipping->isResting()) ++r.blocksRested;
        }

        double worst = 0.0, peakLevel = 0.0;
        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
            {
                const double x = a.getSample (ch, i), y = b.getSample (ch, i);
                worst = std::max (worst, std::abs (x - y));
                peakLevel = std::max (peakLevel, std::abs (y));
                if (std::abs (y) > 1.0e-3)   // (any sample louder than -60 dB)
                    r.worstLevelDb = std::max (r.worstLevelDb, std::abs (x) < 1.0e-30 ? 999.0 : std::abs (20.0 * std::log10 (x / y)));
            }

        if (worst > 0.0)
            r.differenceDb = 20.0 * std::log10 (worst / std::max (peakLevel, 1.0e-30));

        bool sameMoments = momentsA.size() == momentsB.size();
        const auto close = [] (float x, float y) { return std::abs (x - y) <= 1.0e-6f * std::max (1.0f, std::abs (y)); };
        for (size_t i = 0; sameMoments && i < momentsA.size(); ++i)
            sameMoments = close (momentsA[i].soundLevel, momentsB[i].soundLevel) && close (momentsA[i].bandLevel, momentsB[i].bandLevel)
                       && std::abs (momentsA[i].lineLevel - momentsB[i].lineLevel) < 1.0e-6f && close (momentsA[i].gain, momentsB[i].gain);
        if (! sameMoments)
            r.worstLevelDb = 999.0;   // (the display would have shown something different)

        return r;
    }

    void testEfficiency()
    {
        std::cout << "\n[efficiency: skipping through silence, speed]\n";
        const auto gapped = withGaps (slice (loadMaterial ("speech_female"), 0.0, 6.0));

        struct Case { const char* name; DeEsser::Settings settings; };
        std::vector<Case> cases;
        cases.push_back ({ "default settings", settingsWith (5000.0f, 20000.0f, -8.0f, 6.0f) });
        cases.push_back ({ "a band, firm", settingsWith (4500.0f, 9000.0f, -12.0f, 12.0f) });
        { auto s = settingsWith (5000.0f, 20000.0f, -8.0f, 6.0f, true); s.listen = true; cases.push_back ({ "WHOLE SOUND, LISTEN on", s }); }

        for (auto& c : cases)
        {
            const auto r = dsRestReport (gapped, 256, c.settings);
            check (r.differenceDb <= -110.0 && r.worstLevelDb <= 0.0005 && r.blocksRested > 0,
                   juce::String ("skipping silence sounds (and looks) the same as working through it (") + c.name + "): "
                   + (r.differenceDb < -900.0 ? juce::String ("bit for bit") : juce::String (r.differenceDb, 1) + " dB apart")
                   + "; skipped " + juce::String (r.blocksRested) + " of " + juce::String (r.blocks) + " blocks"
                   + (r.worstLevelDb > 0.0005 ? " (worst level difference " + juce::String (r.worstLevelDb, 4) + " dB)" : juce::String()));
        }
        {
            const auto r = dsRestReport (gapped, 256, cases[0].settings, [] (int start, DeEsser::Settings& s)
            {
                if (start >= 160000 && start < 160256) { s.rangeDb = 12.0f; s.thresholdDb = -12.0f; s.toHz = 9000.0f; }
                if (start >= 180000 && start < 180256) s.bypassed = true;
                if (start >= 230000 && start < 230256) { s.bypassed = false; s.listen = true; s.wholeSound = true; }
                if (start >= 250000 && start < 250256) s.listen = false;
            });
            check (r.differenceDb <= -110.0 && r.worstLevelDb <= 0.0005,
                   "...and with knobs moved, BYPASS, LISTEN and TURNS DOWN flicked during the silence: "
                     + (r.differenceDb < -900.0 ? juce::String ("bit for bit") : juce::String (r.differenceDb, 1) + " dB apart"));
        }

       #if JUCE_DEBUG
        check (true, "(the speed checks only run in an optimised build)");
       #else
        {
            const auto vocal = loadMaterial ("vocal_female");
            juce::AudioBuffer<float> material (2, 30 * second);
            for (int ch = 0; ch < 2; ++ch)
                for (int start = 0; start < material.getNumSamples(); start += vocal.getNumSamples())
                    material.copyFrom (ch, start, vocal, ch, 0, std::min (vocal.getNumSamples(), material.getNumSamples() - start));
            P p; prepareDS (p, sr, 256);
            const double busy = coreShare (p, material);
            check (busy < 0.25, "a sung vocal: " + juce::String (busy, 3) + "% of one core (budget 0.25%)");

            P q; setParam (q, DSIDs::to, 9000.0f); setParam (q, DSIDs::turnsDown, 1.0f); prepareDS (q, sr, 256);
            const double band = coreShare (q, material);
            check (band < 0.25, "...a band, WHOLE SOUND: " + juce::String (band, 3) + "% (budget 0.25%)");

            P r; prepareDS (r, sr, 256);
            juce::AudioBuffer<float> silence (2, 48000 * 30);
            silence.clear();
            const double idle = coreShare (r, silence);
            check (idle < 0.03, "digital silence: " + juce::String (idle, 4) + "% of one core (budget 0.03%)");
        }
       #endif
    }

    //==========================================================================
    void testDisplayAndState()
    {
        std::cout << "\n[display data, meter, saving and presets]\n";
        {
            P p; prepareDS (p, sr, 512);
            runDS (p, sum (vowel (1.0, -10.0f), tone (8000.0, -12.0f, 1.0)));
            std::vector<P::Moment> moments (1000);
            const int count = p.takeMoments (moments.data(), (int) moments.size());
            check (count == 200, "one second = 200 moments of 5 ms for the display (got " + juce::String (count) + ")");
            const auto& last = moments[(size_t) std::max (0, count - 1)];
            const float line = DeEsser::thresholdLineDb (last.lineDb, getParam (p, DSIDs::threshold));
            check (near (last.bandDb, -12.0f, 1.5f) && last.soundDb > last.bandDb && last.bandDb > line && near (-last.gainDb, 6.0f, 0.01f),
                   "a steady 8 kHz tone at -12 dB over a vowel: the harsh band reads " + juce::String (last.bandDb, 1) + " dB, over the line ("
                   + juce::String (line, 1) + " dB), turned down by RANGE (" + juce::String (-last.gainDb, 1) + " dB)");
            check (p.takeMoments (moments.data(), 1000) == 0, "moments are only handed over once");
            check (near (p.takeReductionForMeter(), 6.0f, 0.01f) && bitSame (p.takeReductionForMeter(), 0.0f), "the meter sees the 6 dB (and is only told once)");
        }
        {
            P a;
            setParam (a, DSIDs::from, 6300.0f); setParam (a, DSIDs::turnsDown, 1.0f);
            if (std::size (dsPresets) > 1) loadPresetNamed (a, dsPresets[1].name);
            a.markPresetEdited();
            setParam (a, DSIDs::to, 9500.0f);
            juce::MemoryBlock state;
            a.getStateInformation (state);
            P b;
            b.setStateInformation (state.getData(), (int) state.getSize());
            check (near (getParam (b, DSIDs::to), 9500.0f, 1.0f) && b.getPresetName() == a.getPresetName() && b.isPresetEdited() == a.isPresetEdited()
                     && near (getParam (b, DSIDs::from), getParam (a, DSIDs::from), 1.0f) && near (getParam (b, DSIDs::turnsDown), getParam (a, DSIDs::turnsDown), 0.01f),
                   "the settings and the preset name survive saving and reopening the project");
        }
        {
            P p;
            setParam (p, CommonIDs::bypass, 1.0f);
            bool allOk = std::size (dsPresets) == p.getBuiltInPresets().size();
            for (auto& preset : dsPresets)
            {
                loadPresetNamed (p, preset.name);
                allOk = allOk && near (getParam (p, DSIDs::from), preset.fromHz, 0.51f) && near (getParam (p, DSIDs::to), preset.toHz, 0.51f)
                              && near (getParam (p, DSIDs::threshold), preset.thresholdDb, 0.051f) && near (getParam (p, DSIDs::range), preset.rangeDb, 0.051f)
                              && near (getParam (p, DSIDs::turnsDown), (float) preset.turnsDown, 0.01f) && p.getPresetName() == preset.name;
            }
            check (allOk, "all " + juce::String ((int) std::size (dsPresets)) + " presets load exactly");
            check (getParam (p, CommonIDs::bypass) > 0.5f, "presets leave BYPASS as it was");
            const auto [textsOk, textsReport] = presetTextsReport (P().getBuiltInPresets());
            check (textsOk, textsReport);
        }
    }

    //==========================================================================
    void testWindow()
    {
        std::cout << "\n[plugin window]\n";
        P p; prepareDS (p, sr, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* window = dynamic_cast<EasyDSAudioProcessorEditor*> (editor.get());
        check (window != nullptr && editor->getWidth() == 1100 && editor->getHeight() == 600, "window is 1100 x 600, with the shared header");
        if (window == nullptr) return;

        std::vector<juce::Slider*> sliders;
        DSDisplay* display = nullptr;
        GainReductionMeter* meter = nullptr;
        ChoiceSwitch* turnsDown = nullptr;
        bool autoGainShown = false;
        for (auto* child : editor->getChildren())
        {
            if (auto* s = dynamic_cast<juce::Slider*> (child)) sliders.push_back (s);
            if (auto* d = dynamic_cast<DSDisplay*> (child)) display = d;
            if (auto* m = dynamic_cast<GainReductionMeter*> (child)) meter = m;
            if (auto* c = dynamic_cast<ChoiceSwitch*> (child)) turnsDown = c;
            if (child == &window->autoGainButton) autoGainShown = true;
        }
        check (sliders.size() == 4 && display != nullptr && meter != nullptr && turnsDown != nullptr && window->findButton.isVisible(),
               "4 knobs, the display, the meter, TURNS DOWN and FIND present");
        if (sliders.size() != 4 || display == nullptr || meter == nullptr || turnsDown == nullptr) return;

        check (! autoGainShown && window->listenButton.isVisible() && window->listenButton.getRight() < window->presetBrowser.getX()
                 && window->listenButton.getX() > window->bypassButton.getRight() && window->listenButton.getY() == window->bypassButton.getY(),
               "no AUTO-GAIN button: LISTEN sits in the header instead, between BYPASS and the presets");

        bool allInside = true, noOverlap = true;
        std::vector<juce::Rectangle<int>> boxes;
        for (auto* child : editor->getChildren())
        {
            if (! child->isVisible()) continue;
            if (! editor->getLocalBounds().contains (child->getBounds())) allInside = false;
            for (auto& other : boxes) if (other.intersects (child->getBounds())) noOverlap = false;
            boxes.push_back (child->getBounds());
        }
        check (allInside && noOverlap, "everything fits inside the window without overlapping");

        const float subtitleWidth = juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (13.0f)), window->getSubtitle());
        check (subtitleWidth < (float) (window->bypassButton.getX() - 20), "the line under the title fits beside the buttons (" + juce::String (subtitleWidth, 0)
                                                                              + " of " + juce::String (window->bypassButton.getX() - 20) + " px)");

        sliders[2]->setValue (-12.0, juce::sendNotificationSync);
        check (near (getParam (p, DSIDs::threshold), -12.0f, 1e-3f), "turning THRESHOLD changes the setting");
        check (sliders[1]->getTextFromValue (20000.0) == "Off" && sliders[0]->getTextFromValue (4500.0) == "4.5 kHz", "TO reads 'Off' at the top; FROM '4.5 kHz'");

        {
            int mostLines = 0;
            bool allFit = true;
            juce::String tooLong;
            const auto hintFor = [&] (juce::Component* c)
            {
                editor->mouseEnter (hoverEvent (c));
                int lines = 0;
                if (! fitsHelpBar (window->hintBar.getTitle(), window->hintBar.getText(), lines)) { allFit = false; tooLong << window->hintBar.getTitle() << " "; }
                mostLines = std::max (mostLines, lines);
            };
            for (auto* s : sliders) hintFor (s);
            hintFor (display); hintFor (meter); hintFor (turnsDown); hintFor (&window->findButton); hintFor (&window->listenButton); hintFor (&window->bypassButton);
            editor->mouseExit (hoverEvent (editor.get()));
            int lines = 0;
            if (! fitsHelpBar (window->hintBar.getTitle(), window->hintBar.getText(), lines)) { allFit = false; tooLong << window->hintBar.getTitle(); }
            check (allFit, "every explanation fits the help bar (" + juce::String (mostLines) + " lines at most)" + (tooLong.isEmpty() ? juce::String() : ": too long: " + tooLong));
        }

        editor->mouseEnter (hoverEvent (sliders[3]));
        check (window->hintBar.getTitle() == "RANGE", "hovering RANGE explains it");
        editor->mouseEnter (hoverEvent (&window->findButton));
        check (window->hintBar.getTitle() == "FIND", "hovering FIND explains it");
        editor->mouseEnter (hoverEvent (&window->bypassButton));
        check (! window->hintBar.getText().contains ("AUTO-GAIN"), "the BYPASS tip doesn't mention AUTO-GAIN (there isn't one)");

        window->listenButton.setToggleState (true, juce::dontSendNotification);
        window->listenButton.onClick();
        check (p.isListening(), "clicking LISTEN switches it on");
        window->listenButton.setToggleState (false, juce::dontSendNotification);
        window->listenButton.onClick();
        check (! p.isListening(), "...and off again");

        loadPresetNamed (p, "Female Vocal");
        // (A click on WHOLE SOUND: it lights, then tells the plugin.)
        turnsDown->getOption (1).setToggleState (true, juce::dontSendNotification);
        turnsDown->getOption (1).onClick();
        check (getParam (p, DSIDs::turnsDown) > 0.5f && p.isPresetEdited(), "clicking WHOLE SOUND switches TURNS DOWN (and marks the preset edited)");

        {
            auto& browser = window->presetBrowser;
            const auto presetFolder = juce::File::createTempFile ("easy-ds-presets");
            browser.setUserPresetFolder (presetFolder);
            const auto menu = browser.buildMenu();
            int count = 0;
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                if (it.getItem().subMenu != nullptr) ++count;
            check (count >= 4, "the preset menu has its sub-menus (" + juce::String (count) + ")");
            presetFolder.deleteRecursively();
        }

        {
            P q; prepareDS (q, sr, 512);
            q.startFinding();
            {
                std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
            }
            check (! q.isFinding(), "closing the window switches FIND off too");
        }
    }

    //==========================================================================
    void renders (const juce::File& outDir)
    {
        std::cout << "\n[renders]\n";
        const auto female = loadMaterial ("vocal_female");
        const auto speech = loadMaterial ("speech_male");
        const auto oh = loadMaterial ("overheads_cymbals");
        if (female.getNumSamples() == 0 || oh.getNumSamples() == 0) return;

        const auto render = [&] (const char* fileName, const juce::AudioBuffer<float>& material, std::function<void (P&)> setUp,
                                 std::function<void (EasyDSAudioProcessorEditor&)> afterOpening = {})
        {
            P q; prepareDS (q, sr, 480);
            if (setUp) setUp (q);
            std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
            auto* window = dynamic_cast<EasyDSAudioProcessorEditor*> (ed.get());
            runDS (q, material, 480);
            std::vector<P::Moment> moments (4000);
            const int n = q.takeMoments (moments.data(), (int) moments.size());
            window->display.addMoments (moments.data(), n);
            window->display.setThreshold (getParam (q, DSIDs::threshold));
            window->display.setStatus (getParam (q, CommonIDs::bypass) > 0.5f, q.isListening());
            if (afterOpening) afterOpening (*window);
            savePng (*ed, outDir.getChildFile (fileName));
        };

        const auto hoverSlider = [] (int sliderIndex)
        {
            return [sliderIndex] (EasyDSAudioProcessorEditor& ed)
            {
                int n = 0;
                for (auto* child : ed.getChildren())
                    if (dynamic_cast<juce::Slider*> (child) != nullptr && n++ == sliderIndex)
                        ed.mouseEnter (hoverEvent (child));
            };
        };

        render ("ds_default.png", slice (female, 2.0, 4.0), {});
        render ("ds_threshold.png", slice (female, 6.0, 4.0), [] (P& q) { loadPresetNamed (q, "Female Vocal"); }, hoverSlider (2));
        render ("ds_speech.png", slice (speech, 1.0, 4.0), [] (P& q) { loadPresetNamed (q, "Male Vocal"); });
        render ("ds_listening.png", slice (female, 2.0, 4.0), [] (P& q) { q.setListening (true); },
                [] (EasyDSAudioProcessorEditor& w) { w.mouseEnter (hoverEvent (&w.listenButton)); });
        render ("ds_bypassed.png", slice (female, 2.0, 4.0), [] (P& q) { setParam (q, CommonIDs::bypass, 1.0f); });
        render ("ds_whole_sound.png", slice (female, 2.0, 4.0), [] (P& q) { loadPresetNamed (q, "Classic (Whole Sound)"); });
        render ("ds_overheads.png", slice (oh, 4.0, 4.0), [] (P& q) { loadPresetNamed (q, "Overheads - Harsh Cymbals"); });
        render ("ds_finding.png", slice (female, 2.0, 4.0), {}, [] (EasyDSAudioProcessorEditor& w)
        {
            w.display.setFindMessage ("FINDING THE HARSHNESS", "Keep your track playing... 5 s", 0.4f);
            w.findButton.setToggleState (true, juce::dontSendNotification);
            w.mouseEnter (hoverEvent (&w.findButton));
        });
        render ("ds_found.png", slice (female, 2.0, 4.0), [] (P& q) { setParam (q, DSIDs::from, 5600.0f); }, [] (EasyDSAudioProcessorEditor& w)
        {
            w.display.setFindMessage ("FOUND IT: EVERYTHING ABOVE 5.6 KHZ", "FROM and TO are set. Now set THRESHOLD so only the S sounds go over the line.");
        });
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[1])).getFullPathName()
                                      : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    testParameters();
    testDeEssing();
    testListenAndBypass();
    testFind();
    testEfficiency();
    testDisplayAndState();
    testWindow();
    renders (outDir);
    return finish();
}
