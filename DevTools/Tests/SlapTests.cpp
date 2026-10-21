// Bitch-Slap: the sound (against a plain, slow reference), LENGTH and SYNC,
// the DAW plumbing (settings, saving, presets, buses), the window (and PNG
// renders of it), resting in silence, and speed. (Not shipped.)
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TestHelpers.h"
#include <juce_audio_formats/juce_audio_formats.h>

JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal")   // (exact answers are the point here)

using namespace th;
using P = BitchSlapAudioProcessor;

namespace
{
    constexpr double sr = 48000.0;

    /** A DAW's transport, as far as SYNC cares: just the tempo. */
    struct TempoPlayHead : juce::AudioPlayHead
    {
        double bpm = 120.0;
        bool sendsTempo = true;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            if (sendsTempo) info.setBpm (bpm);
            return info;
        }
    };

    bool setLayout (P& p, int ins, int outs)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::canonicalChannelSet (ins));
        layout.outputBuses.add (juce::AudioChannelSet::canonicalChannelSet (outs));
        return p.setBusesLayout (layout);
    }

    void prepareSlap (P& p, double rate = sr, int block = 512, int ins = 2, int outs = 2)
    {
        setLayout (p, ins, outs);
        p.setRateAndBufferSizeDetails (rate, block);
        p.prepareToPlay (rate, block);
    }

    juce::AudioBuffer<float> loadWav (const juce::String& path)
    {
        juce::WavAudioFormat wav;
        const juce::File file (path.startsWith ("/") ? path : "/home/claude/presetcheck/material/" + path + ".wav");
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
        juce::AudioBuffer<float> b;
        if (reader == nullptr) { std::cout << "  (couldn't read " << path << ")\n"; return b; }
        b.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&b, 0, (int) reader->lengthInSamples, 0, true, true);
        return b;
    }

    juce::AudioBuffer<float> stereoOf (const juce::AudioBuffer<float>& source, double from, double seconds, double rate = sr)
    {
        juce::AudioBuffer<float> b (2, (int) (seconds * rate));
        for (int ch = 0; ch < 2; ++ch)
            b.copyFrom (ch, 0, source, std::min (ch, source.getNumChannels() - 1), (int) (from * rate),
                        std::min (b.getNumSamples(), source.getNumSamples() - (int) (from * rate)));
        return b;
    }

    /** Some clicks (a handful of samples each) at given times: sparse, so the
        slow reference stays quick. */
    juce::AudioBuffer<float> clicks (int numSamples, const std::vector<std::pair<int, float>>& hits, int channels = 2, int width = 6)
    {
        juce::AudioBuffer<float> b (channels, numSamples);
        b.clear();
        for (auto [at, level] : hits)
            for (int k = 0; k < width && at + k < numSamples; ++k)
                for (int ch = 0; ch < channels; ++ch)
                    b.setSample (ch, at + k, level * (k % 2 == 0 ? 1.0f : -0.6f) * (ch == 0 ? 1.0f : 0.8f));
        return b;
    }

    /** The reverb that should come out, worked out the slow, obvious way: every
        input sample times the burst (a GatedReverb's own burst - the same noise),
        added up, then the two tone filters. lengthAt: the LENGTH each input sample plays with. */
    std::vector<float> slowReference (const juce::AudioBuffer<float>& input, int numInputs, int side, double rate,
                                      std::function<float (int)> lengthAt)
    {
        GatedReverb source;
        source.prepare (rate, 2, {});
        const int n = input.getNumSamples();
        const int longest = (int) std::ceil (GatedReverb::maxLengthMs * 1e-3 * rate) + 2;
        std::vector<float> out ((size_t) n, 0.0f), burst ((size_t) longest);
        float burstLength = -1.0f;

        for (int i = 0; i < n; ++i)
        {
            float x = 0.0f;
            for (int ch = 0; ch < numInputs; ++ch) x += input.getSample (ch, i);
            x /= (float) numInputs;

            if (x == 0.0f)
                continue;

            const float lengthMs = lengthAt (i);
            if (lengthMs != burstLength)
            {
                source.renderBurst (side, lengthMs, burst.data(), longest);
                burstLength = lengthMs;
            }

            for (int k = 0; k < longest && i + k < n; ++k)
                out[(size_t) (i + k)] += x * burst[(size_t) k];
        }

        auto filter = source.makeToneFilter();
        filter.process (out.data(), n);
        return out;
    }

    /** How far a result is from the reference, in dB below the reference's level. */
    double errorDb (const juce::AudioBuffer<float>& result, int ch, const std::vector<float>& expected, const juce::AudioBuffer<float>* subtract = nullptr)
    {
        double e2 = 0.0, s2 = 0.0;
        for (int i = 0; i < result.getNumSamples(); ++i)
        {
            const double got = result.getSample (ch, i) - (subtract != nullptr ? subtract->getSample (std::min (ch, subtract->getNumChannels() - 1), i) : 0.0f);
            const double e = got - expected[(size_t) i];
            e2 += e * e;
            s2 += (double) expected[(size_t) i] * expected[(size_t) i];
        }
        return 10.0 * std::log10 (e2 / std::max (s2, 1e-30) + 1e-30);
    }

    juce::AudioBuffer<float> runSlap (P& p, const juce::AudioBuffer<float>& input, int block = 512, std::function<void (int)> beforeBlock = {})
    {
        return run (p, input, block, std::move (beforeBlock));
    }

    /** Where the burst ends: the last moment anything above 1/100 of its level is left. */
    int burstEnd (const juce::AudioBuffer<float>& wet, int ch, int from)
    {
        const float level = rms (wet, ch, from + (int) (0.004 * sr), from + (int) (0.040 * sr));
        for (int i = wet.getNumSamples() - 1; i > from; --i)
            if (std::abs (wet.getSample (ch, i)) > 0.01f * level) return i;
        return from;
    }

    juce::String ms (double samples, double rate = sr)   { return juce::String (samples * 1000.0 / rate, 1) + " ms"; }

    //==========================================================================
    void testParameters()
    {
        std::cout << "\n[settings your DAW sees]\n";
        P p;
        check (p.getParameters().size() == 5, "5 settings: MIX, LENGTH, SYNC, NOTE, BYPASS (" + juce::String (p.getParameters().size()) + ")");
        check (! p.hasAutoGain(), "no AUTO-GAIN (a reverb adds sound on purpose)");
        check (getParam (p, SlapIDs::mix) == 35.0f && getParam (p, SlapIDs::length) == 300.0f && getParam (p, SlapIDs::sync) == 0.0f
                 && getParam (p, SlapIDs::note) == (float) n8,
               "defaults: MIX 35%, LENGTH 300 ms, SYNC OFF, NOTE 1/8");

        auto* length = p.parameters.getParameter (SlapIDs::length);
        check (length->getText (length->getValue(), 0) == "300 ms" && length->convertFrom0to1 (0.5f) > 280.0f && length->convertFrom0to1 (0.5f) < 320.0f,
               "LENGTH reads \"300 ms\", and 300 ms is halfway round the knob (" + juce::String (length->convertFrom0to1 (0.5f), 0) + ")");
        check (length->convertFrom0to1 (0.0f) == 50.0f && length->convertFrom0to1 (1.0f) == 1000.0f, "LENGTH goes from 50 to 1000 ms");

        auto* note = p.parameters.getParameter (SlapIDs::note);
        check (note->getText (note->convertTo0to1 ((float) n8), 0) == "1/8 note" && note->getText (note->convertTo0to1 ((float) n4D), 0) == "dotted 1/4",
               "NOTE reads \"1/8 note\", \"dotted 1/4\"...");
        bool sorted = true;
        for (int i = 1; i < numNoteLengths; ++i) sorted = sorted && noteLengths[i].beats > noteLengths[i - 1].beats;
        check (sorted && numNoteLengths == 12, "12 note lengths, shortest first");

        check (p.getLatencySamples() == 0, "adds no delay to the track (the engine's wait hides in the 2 ms gap)");

        check (setLayout (p, 2, 2) && setLayout (p, 1, 1) && setLayout (p, 1, 2), "works on stereo and mono tracks, and mono in / stereo out");
        check (! setLayout (p, 2, 1) && ! setLayout (p, 6, 6), "refuses stereo in / mono out, and surround");

        check (near ((float) p.getTailLengthSeconds(), 0.35f, 0.001f), "tells the DAW the reverb rings on for LENGTH + 50 ms after the sound stops");

        check (near (P::lengthForNote (n8, 120.0), 250.0f, 0.01f) && near (P::lengthForNote (n4, 90.0), 666.67f, 0.01f)
                 && near (P::lengthForNote (n8T, 120.0), 166.67f, 0.01f) && near (P::lengthForNote (n8D, 120.0), 375.0f, 0.01f),
               "note lengths: 1/8 at 120 BPM = 250 ms, 1/4 at 90 = 667 ms, 1/8 triplet = 167 ms, dotted 1/8 = 375 ms");
        check (P::lengthForNote (n2, 60.0) == 1000.0f && P::lengthForNote (n32, 200.0) == 50.0f,
               "...kept to what it can do (1/2 at 60 BPM -> 1000 ms, 1/32 at 200 BPM -> 50 ms)");
    }

    //==========================================================================
    void testBurst()
    {
        std::cout << "\n[the burst: against the slow, obvious reference]\n";
        const int n = (int) (1.6 * sr);
        const auto input = clicks (n, { { 2000, 0.8f }, { 30000, 0.5f }, { 31000, 0.3f } });

        for (float lengthMs : { 50.0f, 300.0f, 1000.0f })
        {
            P p; prepareSlap (p);
            setParam (p, SlapIDs::mix, 100.0f);
            setParam (p, SlapIDs::length, lengthMs);
            const auto out = runSlap (p, input, 333);
            double worst = -1000.0;
            for (int ch = 0; ch < 2; ++ch)
                worst = std::max (worst, errorDb (out, ch, slowReference (input, 2, ch, sr, [=] (int) { return lengthMs; })));
            check (worst < -100.0, "MIX 100%, LENGTH " + juce::String (lengthMs, 0) + " ms: exactly the burst (error " + juce::String (worst, 1) + " dB)");
        }

        // MIX: up to 50% it only adds reverb; above, the original goes down
        {
            const auto wetAt = [&] (float mixPercent)
            {
                P p; prepareSlap (p);
                setParam (p, SlapIDs::mix, mixPercent);
                return runSlap (p, input, 512);
            };
            const auto reference = slowReference (input, 2, 0, sr, [] (int) { return 300.0f; });

            const auto at0 = wetAt (0.0f), at25 = wetAt (25.0f), at50 = wetAt (50.0f), at75 = wetAt (75.0f);
            check (maxDifference (at0, input) == 0.0f, "MIX 0%: exactly your sound, untouched");

            std::vector<float> half (reference), full (reference);
            for (auto& v : half) v *= 0.5f;
            check (errorDb (at50, 0, full, &input) < -100.0, "MIX 50%: your sound at full level plus the burst at full level");
            check (errorDb (at25, 0, half, &input) < -100.0, "MIX 25%: the burst at half level (-6 dB), your sound untouched");

            juce::AudioBuffer<float> halfDry (input);
            halfDry.applyGain (0.5f);
            check (errorDb (at75, 0, full, &halfDry) < -100.0, "MIX 75%: the burst at full level, your sound at half");
        }

        // Its shape
        {
            P p; prepareSlap (p);
            setParam (p, SlapIDs::mix, 100.0f);
            setParam (p, SlapIDs::length, 300.0f);
            const auto out = runSlap (p, clicks ((int) (0.6 * sr), { { 0, 1.0f } }, 2, 1));

            int first = 0;
            while (first < out.getNumSamples() && std::abs (out.getSample (0, first)) < 1e-6f) ++first;
            check (first >= 96 && first <= 100, "the burst starts 2 ms after the hit (" + ms (first) + ")");

            const float start = rms (out, 0, (int) (0.004 * sr), (int) (0.040 * sr));
            const float late  = rms (out, 0, (int) (0.250 * sr), (int) (0.282 * sr));
            const float after = rms (out, 0, (int) (0.306 * sr), (int) (0.400 * sr));
            const float sagDb = toDb (start) - toDb (late);
            check (sagDb > 1.5f && sagDb < 5.0f, "it sags gently while it rings (" + juce::String (sagDb, 1) + " dB by the end)");
            check (toDb (after) - toDb (start) < -60.0f, "...and then stops dead: 6 ms after LENGTH it's " + juce::String (toDb (after) - toDb (start), 0) + " dB down");

            const int end = burstEnd (out, 0, 0);
            check (std::abs (end - (int) (0.300 * sr)) < (int) (0.004 * sr), "LENGTH 300 ms: the burst is over 300 ms after the hit (" + ms (end) + ")");

            double lr = 0.0, ll = 0.0, rr = 0.0;
            for (int i = 0; i < (int) (0.3 * sr); ++i)
            {
                const double l = out.getSample (0, i), r = out.getSample (1, i);
                lr += l * r; ll += l * l; rr += r * r;
            }
            const double correlation = lr / std::sqrt (ll * rr);
            check (std::abs (correlation) < 0.1 && std::abs (10.0 * std::log10 (ll / rr)) < 1.0,
                   "wide: left and right are different noise (correlation " + juce::String (correlation, 3) + "), at the same level ("
                     + juce::String (10.0 * std::log10 (ll / rr), 2) + " dB)");
        }

        // The same sound at every sample rate. (A sweep from 100 Hz to 10 kHz:
        // the same sound at any rate, and wide enough to hear the whole burst.)
        {
            float level48 = 0.0f;
            double end48 = 0.0;

            for (double rate : { 48000.0, 44100.0, 88200.0, 96000.0, 192000.0 })
            {
                P p; prepareSlap (p, rate, 512);
                setParam (p, SlapIDs::mix, 100.0f);
                juce::AudioBuffer<float> sweep (2, (int) (0.6 * rate));
                sweep.clear();
                const int sweepLength = (int) (0.05 * rate);
                for (int i = 0; i < sweepLength; ++i)
                {
                    const double t = i / rate;
                    const double phase = juce::MathConstants<double>::twoPi * (100.0 * t + 0.5 * (9900.0 / 0.05) * t * t);
                    for (int ch = 0; ch < 2; ++ch)
                        sweep.setSample (ch, i, 0.3f * (float) std::sin (phase));
                }
                const auto out = runSlap (p, sweep, 512);
                const float level = rms (out, 0, (int) (0.06 * rate), (int) (0.28 * rate));
                const double end = burstEnd (out, 0, 0) * 1000.0 / rate;

                if (rate == 48000.0) { level48 = level; end48 = end; }
                check (std::abs (toDb (level) - toDb (level48)) < 0.3f && std::abs (end - end48) < 1.0,
                       "at " + juce::String (rate / 1000.0, 1) + " kHz: the same level (" + juce::String (toDb (level) - toDb (level48), 2)
                         + " dB) and length (" + juce::String (end - end48, 2) + " ms)");
            }
        }
    }

    //==========================================================================
    void testTracks()
    {
        std::cout << "\n[mono and stereo tracks]\n";
        const int n = (int) (0.8 * sr);

        {
            P p; prepareSlap (p, sr, 512, 1, 1);
            setParam (p, SlapIDs::mix, 50.0f);
            const auto input = clicks (n, { { 1000, 0.7f } }, 1);
            const auto out = runSlap (p, input, 512);
            const auto expected = slowReference (input, 1, 0, sr, [] (int) { return 300.0f; });
            check (errorDb (out, 0, expected, &input) < -100.0, "a mono track: your sound plus a mono burst");
        }
        {
            P p; prepareSlap (p, sr, 512, 1, 2);
            setParam (p, SlapIDs::mix, 50.0f);
            const auto mono = clicks (n, { { 1000, 0.7f } }, 1);
            juce::AudioBuffer<float> io (2, n);
            io.copyFrom (0, 0, mono, 0, 0, n);
            for (int i = 0; i < n; ++i) io.setSample (1, i, 99.0f);   // (what's in an unused channel can be anything)
            const auto out = runSlap (p, io, 512);
            const auto left = slowReference (mono, 1, 0, sr, [] (int) { return 300.0f; });
            const auto right = slowReference (mono, 1, 1, sr, [] (int) { return 300.0f; });
            check (errorDb (out, 0, left, &mono) < -100.0 && errorDb (out, 1, right, &mono) < -100.0,
                   "a mono track with a stereo output: your sound on both sides, and the full wide burst");
        }
        {
            P p; prepareSlap (p);
            setParam (p, SlapIDs::mix, 100.0f);
            auto input = clicks (n, { { 1000, 0.7f } });
            input.clear (1, 0, n);   // only the left side
            const auto out = runSlap (p, input, 512);
            const auto expected = slowReference (input, 2, 0, sr, [] (int) { return 300.0f; });
            check (errorDb (out, 0, expected) < -100.0, "stereo: the burst hears left and right added together (a sound panned hard left still gets the wide burst)");
        }
    }

    //==========================================================================
    void testLengthAndSync()
    {
        std::cout << "\n[LENGTH and SYNC]\n";

        // The level stays the same whatever the LENGTH
        {
            float shortest = 0.0f;
            bool allRight = true;
            juce::String ends;

            for (float lengthMs : { 50.0f, 150.0f, 300.0f, 600.0f, 1000.0f })
            {
                P p; prepareSlap (p);
                setParam (p, SlapIDs::mix, 100.0f);
                setParam (p, SlapIDs::length, lengthMs);
                const auto out = runSlap (p, clicks ((int) (1.2 * sr), { { 0, 1.0f } }, 2, 1), 512);
                const float level = rms (out, 0, (int) (0.003 * sr), (int) (0.010 * sr));   // (the same noise every time: only the shape differs)
                if (lengthMs == 50.0f) shortest = level;
                const int end = burstEnd (out, 0, 0);
                allRight = allRight && std::abs (end - lengthMs * 1e-3 * sr) < 0.004 * sr && std::abs (toDb (level) - toDb (shortest)) < 1.0f;
                ends << ms (end) << " ";
            }

            check (allRight, "LENGTH 50 / 150 / 300 / 600 / 1000 ms: bursts end on time (" + ends.trim() + "), all at the same level");
        }

        // SYNC
        {
            const auto syncedEnd = [] (double bpm, int note, bool sendsTempo = true)
            {
                TempoPlayHead playHead;
                playHead.bpm = bpm;
                playHead.sendsTempo = sendsTempo;
                P p; prepareSlap (p);
                p.setPlayHead (&playHead);
                setParam (p, SlapIDs::mix, 100.0f);
                setParam (p, SlapIDs::sync, (float) syncTempo);
                setParam (p, SlapIDs::note, (float) note);
                juce::AudioBuffer<float> warmUp (2, 512); warmUp.clear();
                runSlap (p, warmUp, 512);   // (the tempo arrives with the first block)
                const auto out = runSlap (p, clicks ((int) (1.2 * sr), { { 0, 1.0f } }, 2, 1), 512);
                p.setPlayHead (nullptr);
                return burstEnd (out, 0, 0) * 1000.0 / sr;
            };

            const double e8 = syncedEnd (120.0, n8), e4 = syncedEnd (90.0, n4), e2 = syncedEnd (60.0, n2), eNone = syncedEnd (100.0, n8, false);
            check (std::abs (e8 - 250.0) < 4.0, "SYNC TEMPO, 1/8 note at 120 BPM: the burst lasts 250 ms (" + juce::String (e8, 1) + ")");
            check (std::abs (e4 - 666.7) < 4.0, "1/4 note at 90 BPM: 667 ms (" + juce::String (e4, 1) + ")");
            check (std::abs (e2 - 1000.0) < 4.0, "1/2 note at 60 BPM: kept to 1000 ms, the longest (" + juce::String (e2, 1) + ")");
            check (std::abs (eNone - 250.0) < 4.0, "a DAW that doesn't send its tempo: 120 BPM (" + juce::String (eNone, 1) + ")");
        }

        // Following a tempo change: the next hits get the new length
        {
            TempoPlayHead playHead;
            P p; prepareSlap (p);
            p.setPlayHead (&playHead);
            setParam (p, SlapIDs::mix, 100.0f);
            setParam (p, SlapIDs::sync, (float) syncTempo);
            setParam (p, SlapIDs::note, (float) n8);
            const int second = (int) sr;
            auto out = runSlap (p, clicks (3 * second, { { 0, 1.0f }, { 2 * second, 1.0f } }, 2, 1), 512,
                                      [&] (int start) { if (start >= second) playHead.bpm = 100.0; });
            p.setPlayHead (nullptr);
            juce::AudioBuffer<float> firstHalf (out.getArrayOfWritePointers(), 2, 0, second);
            const double before = burstEnd (firstHalf, 0, 0) * 1000.0 / sr;
            const double after = (burstEnd (out, 0, 2 * second) - 2 * second) * 1000.0 / sr;
            check (std::abs (before - 250.0) < 4.0 && std::abs (after - 300.0) < 4.0,
                   "the song speeds down from 120 to 100 BPM: the next 1/8 notes last 300 ms (" + juce::String (before, 0) + " -> " + juce::String (after, 0) + ")");
        }

        // Moving LENGTH while bursts ring: each hit keeps the burst it started with
        {
            const int n = (int) (2.0 * sr), t0 = 2000, change = t0 + (int) (0.15 * sr), t2 = t0 + (int) (0.6 * sr);
            const auto input = clicks (n, { { t0, 0.8f }, { t2, 0.6f } });
            P p; prepareSlap (p);
            setParam (p, SlapIDs::mix, 100.0f);
            setParam (p, SlapIDs::length, 400.0f);
            const auto out = runSlap (p, input, 256, [&] (int start) { if (start >= change && start < change + 256) setParam (p, SlapIDs::length, 150.0f); });
            const auto expected = slowReference (input, 2, 0, sr, [&] (int i) { return i < change ? 400.0f : 150.0f; });
            const double error = errorDb (out, 0, expected);
            check (error < -100.0, "LENGTH 400 -> 150 ms while a burst rings: that burst still lasts 400 ms, the next hit gets 150 ms (error "
                                     + juce::String (error, 1) + " dB)");
        }

        // However soon after a hit LENGTH moves, that hit gets one whole burst:
        // all of the old one, or all of the new one - never a mix
        {
            const int n = (int) (1.2 * sr), t0 = 3000;
            const auto input = clicks (n, { { t0, 0.8f } });
            const auto before = slowReference (input, 2, 0, sr, [] (int) { return 400.0f; });
            const auto after = slowReference (input, 2, 0, sr, [] (int) { return 150.0f; });
            double worst = -1000.0;
            juce::String which;

            for (int delayMs : { 0, 3, 10, 25, 40, 60, 80, 100, 120, 140, 160, 180, 200 })
            {
                const int change = t0 + delayMs * (int) sr / 1000;
                P p; prepareSlap (p);
                setParam (p, SlapIDs::mix, 100.0f);
                setParam (p, SlapIDs::length, 400.0f);
                const auto out = runSlap (p, input, 32, [&] (int start) { if (start <= change && change < start + 32) setParam (p, SlapIDs::length, 150.0f); });
                const double old = errorDb (out, 0, before), changed = errorDb (out, 0, after);
                worst = std::max (worst, std::min (old, changed));
                which << (old < changed ? "4" : "1");
            }

            check (worst < -100.0, "LENGTH moved 0-200 ms after a hit: the hit keeps one whole burst, never a mix (worst " + juce::String (worst, 1)
                                     + " dB; old/new: " + which + ")");
        }

        // Sweeping LENGTH on a drum loop: no clicks
        {
            const auto loop = drumLoop (sr, 6.0);
            const auto jumpRatio = [&] (bool sweep)
            {
                P p; prepareSlap (p);
                setParam (p, SlapIDs::mix, 100.0f);
                juce::Random random (5);
                const auto out = runSlap (p, loop, 480, [&] (int start) { if (sweep && (start / 480) % 2 == 0) setParam (p, SlapIDs::length, 50.0f + 950.0f * random.nextFloat()); });
                float biggest = 0.0f;
                for (int i = 1; i < out.getNumSamples(); ++i) biggest = std::max (biggest, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
                return biggest / rms (out, 0, 0, out.getNumSamples());
            };
            const float steady = jumpRatio (false), swept = jumpRatio (true);
            check (swept < steady * 1.5f, "sweeping LENGTH all over every 20 ms on a drum loop: no clicks (biggest jump " + juce::String (swept / steady, 2) + "x a steady LENGTH's)");
        }
    }

    //==========================================================================
    void testBypassAndSilence()
    {
        std::cout << "\n[BYPASS, silence and resting]\n";
        const auto loop = drumLoop (sr, 3.0);

        {
            P p; prepareSlap (p);
            setParam (p, SlapIDs::mix, 60.0f);
            const int at = (int) (1.0 * sr);
            const auto out = runSlap (p, loop, 480, [&] (int start) { if (start == at) setParam (p, CommonIDs::bypass, 1.0f); });
            juce::AudioBuffer<float> after (2, out.getNumSamples() - at - 1200), original (2, after.getNumSamples());
            for (int ch = 0; ch < 2; ++ch)
            {
                after.copyFrom (ch, 0, out, ch, at + 1200, after.getNumSamples());
                original.copyFrom (ch, 0, loop, ch, at + 1200, after.getNumSamples());
            }
            check (maxDifference (after, original) == 0.0f, "BYPASS: 25 ms later you hear exactly your original sound");

            float biggest = 0.0f, steady = 0.0f;
            for (int i = at - 2000; i < at + 2000; ++i) biggest = std::max (biggest, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
            for (int i = 1; i < at - 2000; ++i) steady = std::max (steady, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
            check (biggest <= steady * 1.2f, "...and it fades over, no click");
        }

        {
            P p; prepareSlap (p);
            setParam (p, SlapIDs::mix, 100.0f);
            juce::AudioBuffer<float> silence (2, (int) (2.0 * sr));
            silence.clear();
            const auto out = runSlap (p, silence, 512);
            check (peak (out, 0, 0, out.getNumSamples()) == 0.0f && peak (out, 1, 0, out.getNumSamples()) == 0.0f, "silence in, silence out");
        }

        // Hits far apart: it rests in between (and wakes up exactly on time)
        {
            const int n = (int) (6.0 * sr);
            const auto input = clicks (n, { { 1000, 0.7f }, { (int) (2.5 * sr) + 77, 0.5f }, { (int) (4.9 * sr) + 13, 0.9f } });
            P p; prepareSlap (p);
            setParam (p, SlapIDs::mix, 100.0f);
            setParam (p, SlapIDs::length, 1000.0f);
            const auto out = runSlap (p, input, 300);
            const double error = errorDb (out, 0, slowReference (input, 2, 0, sr, [] (int) { return 1000.0f; }));
            check (error < -100.0, "hits 2.5 s apart: resting in between changes nothing (error " + juce::String (error, 1) + " dB)");
        }

        // Odd block sizes
        {
            const int n = (int) (1.5 * sr);
            const auto input = clicks (n, { { 500, 0.7f }, { 9000, 0.4f }, { 20011, 0.6f } });
            const auto expected = slowReference (input, 2, 1, sr, [] (int) { return 300.0f; });
            double worst = -1000.0;

            for (int block : { 1, 7, 64, 100, 2048 })
            {
                P p; prepareSlap (p, sr, block);
                setParam (p, SlapIDs::mix, 100.0f);
                worst = std::max (worst, errorDb (runSlap (p, input, block), 1, expected));
            }

            check (worst < -100.0, "blocks of 1, 7, 64, 100 and 2048 samples all give the same sound (worst error " + juce::String (worst, 1) + " dB)");
        }
    }

    //==========================================================================
    void testLevel()
    {
        std::cout << "\n[level: MIX 50% against the listening examples you approved]\n";
        const auto dry = loadWav ("/home/claude/presetcheck/slap_material/snare_example_dry.wav");
        const auto burst = loadWav ("/home/claude/presetcheck/slap_material/snare_example_burst.wav");
        if (dry.getNumSamples() == 0 || burst.getNumSamples() == 0) return;

        P p; prepareSlap (p);
        setParam (p, SlapIDs::mix, 100.0f);   // (the example file is the reverb on its own, at "MIX 50%" level)
        setParam (p, SlapIDs::length, 302.0f);   // (the examples' bursts were 300 ms long, starting 2 ms after the hit)
        const auto out = runSlap (p, dry, 512);
        const float ours = rms (out, 0, 0, out.getNumSamples()), theirs = rms (burst, 0, 0, burst.getNumSamples());
        check (std::abs (toDb (ours) - toDb (theirs)) < 1.0f,
               "the snare example's burst: the same level as the one you approved (" + juce::String (toDb (ours) - toDb (theirs), 2) + " dB)");
    }

    //==========================================================================
    void testEfficiency()
    {
        std::cout << "\n[speed]\n";
       #if JUCE_DEBUG
        std::cout << "  (speed checks only count in the optimised build)\n";
       #else
        const auto loop = drumLoop (sr, 20.0);
        const auto percentOfRealTime = [&] (std::function<void (P&)> setUp, const juce::AudioBuffer<float>& material, std::function<void (P&, int)> perBlock = {})
        {
            P p; prepareSlap (p);
            setUp (p);
            const auto start = std::chrono::steady_clock::now();
            run (p, material, 512, [&] (int at) { if (perBlock) perBlock (p, at); });
            return 100.0 * std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count() * sr / material.getNumSamples();
        };

        juce::AudioBuffer<float> silence (2, loop.getNumSamples());
        silence.clear();

        const double full = percentOfRealTime ([] (P& p) { setParam (p, SlapIDs::length, 1000.0f); }, loop);
        const double normal = percentOfRealTime ([] (P& p) { setParam (p, SlapIDs::length, 300.0f); }, loop);
        const double quiet = percentOfRealTime ([] (P&) {}, silence);
        const double mixZero = percentOfRealTime ([] (P& p) { setParam (p, SlapIDs::mix, 0.0f); }, loop);
        const double bypassed = percentOfRealTime ([] (P& p) { setParam (p, CommonIDs::bypass, 1.0f); }, loop);
        juce::Random random (9);
        const double sweeping = percentOfRealTime ([] (P&) {}, loop, [&] (P& p, int) { setParam (p, SlapIDs::length, 50.0f + 950.0f * random.nextFloat()); });

        check (full < 1.5, "a drum loop, 1 second bursts: " + juce::String (full, 3) + "% of one core (budget 1.5%)");
        check (normal < 1.2, "...300 ms bursts: " + juce::String (normal, 3) + "% (budget 1.2%)");
        check (sweeping < 3.0, "...with LENGTH moving every block: " + juce::String (sweeping, 3) + "% (budget 3%)");
        check (quiet < 0.04, "silence: " + juce::String (quiet, 4) + "% (budget 0.04%)");
        check (mixZero < 0.05 && bypassed < 0.05, "MIX 0% (" + juce::String (mixZero, 4) + "%) and BYPASS (" + juce::String (bypassed, 4) + "%): next to nothing (budget 0.05%)");
       #endif
    }

    //==========================================================================
    void testStateAndPresets()
    {
        std::cout << "\n[saving, presets]\n";
        {
            P a; prepareSlap (a);
            setParam (a, SlapIDs::mix, 62.0f);
            setParam (a, SlapIDs::length, 444.0f);
            setParam (a, SlapIDs::sync, 1.0f);
            setParam (a, SlapIDs::note, (float) n4T);
            juce::MemoryBlock state;
            a.getStateInformation (state);
            P b; prepareSlap (b);
            b.setStateInformation (state.getData(), (int) state.getSize());
            check (getParam (b, SlapIDs::mix) == 62.0f && getParam (b, SlapIDs::length) == 444.0f && getParam (b, SlapIDs::sync) == 1.0f
                     && getParam (b, SlapIDs::note) == (float) n4T,
                   "a project saves and re-opens every setting");
        }

        P p; prepareSlap (p);
        const auto& presets = p.getBuiltInPresets();
        const auto [textsOk, textsReport] = presetTextsReport (presets);
        check (textsOk, textsReport);

        bool allLoad = true, allSound = true;
        juce::String levels;
        const auto snare = loadWav ("snare_rock");
        const auto material = snare.getNumSamples() > 0 ? stereoOf (snare, 1.0, 4.0) : drumLoop (sr, 4.0);

        for (auto& preset : presets)
        {
            P q; prepareSlap (q);
            q.loadPreset (preset);
            for (auto& [id, value] : preset.values)
                allLoad = allLoad && near (getParam (q, id.toRawUTF8()), value, 0.01f);
            const auto out = runSlap (q, material, 512);
            allSound = allSound && allFinite (out) && peak (out, 0, 0, out.getNumSamples()) < 4.0f;
        }

        check (allLoad, "every preset sets MIX, LENGTH, SYNC and NOTE (" + juce::String ((int) presets.size()) + " presets)");
        check (allSound, "every preset plays cleanly on a rock snare");

        int categories = 0;
        juce::StringArray seen;
        for (auto& preset : presets)
            if (preset.category.isNotEmpty() && ! seen.contains (preset.category)) { seen.add (preset.category); ++categories; }
        check (categories >= 6, "presets in " + juce::String (categories) + " sub-menus: " + seen.joinIntoString (", "));
    }

    //==========================================================================
    juce::String hintTitleOver (juce::AudioProcessorEditor& ed, juce::Component& c)
    {
        ed.mouseEnter (hoverEvent (&c));
        auto* window = dynamic_cast<EasyWindow*> (&ed);
        const auto title = window->hintBar.getTitle();
        ed.mouseExit (hoverEvent (&c));
        return title;
    }

    void testWindow()
    {
        std::cout << "\n[the window]\n";
        P p; prepareSlap (p);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        check (ed->getWidth() == 1100 && ed->getHeight() == 600, "opens at 1100 x 600");

        std::vector<juce::Slider*> sliders;
        for (auto* child : ed->getChildren())
            if (auto* s = dynamic_cast<juce::Slider*> (child)) sliders.push_back (s);
        check (sliders.size() == 3, "three knobs: MIX, LENGTH (ms) and LENGTH (as a note)");

        if (sliders.size() == 3)
        {
            check (hintTitleOver (*ed, *sliders[0]) == "MIX" && hintTitleOver (*ed, *sliders[1]) == "LENGTH", "the help bar explains MIX and LENGTH");
            check (sliders[1]->isVisible() && ! sliders[2]->isVisible(), "SYNC OFF: the LENGTH knob in ms");
            check (sliders[1]->getTextFromValue (sliders[1]->getValue()) == "300 ms", "...reading \"300 ms\"");

            setParam (p, SlapIDs::sync, (float) syncTempo);
            ed.reset (p.createEditor());   // (its timer isn't running here: re-open it)
            sliders.clear();
            for (auto* child : ed->getChildren())
                if (auto* s = dynamic_cast<juce::Slider*> (child)) sliders.push_back (s);
            check (! sliders[1]->isVisible() && sliders[2]->isVisible() && sliders[2]->getTextFromValue (sliders[2]->getValue()) == "1/8 note",
                   "SYNC TEMPO: the note knob takes its place, reading \"1/8 note\"");
        }

        using E = BitchSlapAudioProcessorEditor;
        check (E::describeLength (true, n8, 250.0f, 120.0, true) == "1/8 note at 120 BPM\n= 250 ms", "under SYNC: \"1/8 note at 120 BPM = 250 ms\"");
        check (E::describeLength (true, n2, 1000.0f, 60.0, true).contains ("the longest it goes"), "...and says when a note is longer than it can go");
        check (E::describeLength (false, n8, 300.0f, 120.0, true) == "At 120 BPM that's\nbetween a 1/8 and a 1/4 note"
                 && E::describeLength (false, n8, 250.0f, 120.0, true) == "At 120 BPM that's\nabout a 1/8 note",
               "SYNC OFF: how LENGTH fits the song (\"about a 1/8 note\", \"between a 1/8 and a 1/4 note\")");
        check (E::describeLength (false, n8, 300.0f, 120.0, false).contains ("until your DAW sends its tempo"), "...and says when it's guessing the tempo");
    }

    //==========================================================================
    void renders (const juce::File& outDir)
    {
        std::cout << "\n[renders]\n";
        const auto snare = loadWav ("snare_rock");
        const auto toms = loadWav ("toms");
        const auto kit = loadWav ("/home/claude/presetcheck/slap_material/kit_example_dry.wav");
        if (snare.getNumSamples() == 0 || toms.getNumSamples() == 0 || kit.getNumSamples() == 0) return;

        const auto render = [&] (const char* fileName, const juce::AudioBuffer<float>& material, std::function<void (P&)> setUp,
                                 std::function<void (juce::AudioProcessorEditor&)> afterOpening = {})
        {
            TempoPlayHead playHead;
            P q; prepareSlap (q);
            q.setPlayHead (&playHead);
            if (setUp) setUp (q);
            runSlap (q, material, 480);
            std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
            if (afterOpening) afterOpening (*ed);
            savePng (*ed, outDir.getChildFile (fileName));
            ed.reset();
            q.setPlayHead (nullptr);
        };

        const auto hoverSlider = [] (int index)
        {
            return [index] (juce::AudioProcessorEditor& ed)
            {
                int n = 0;
                for (auto* child : ed.getChildren())
                    if (dynamic_cast<juce::Slider*> (child) != nullptr && n++ == index)
                        ed.mouseEnter (hoverEvent (child));
            };
        };

        render ("slap_default.png", stereoOf (snare, 1.0, 4.0), {});
        render ("slap_mix_hint.png", stereoOf (snare, 1.0, 4.0), [] (P& q) { setParam (q, SlapIDs::mix, 50.0f); }, hoverSlider (0));
        render ("slap_toms_long.png", stereoOf (toms, 2.0, 4.0), [] (P& q) { loadPresetNamed (q, "Toms - Big 80s Fills"); setParam (q, SlapIDs::length, 700.0f); }, hoverSlider (1));
        render ("slap_synced.png", stereoOf (snare, 1.0, 4.0), [] (P& q) { loadPresetNamed (q, "Synced - 1/8 Note"); }, hoverSlider (2));
        render ("slap_send_kit.png", stereoOf (kit, 0.0, 4.5), [] (P& q) { loadPresetNamed (q, "Send - 80s Snare & Toms"); });
        render ("slap_bypassed.png", stereoOf (snare, 1.0, 4.0), [] (P& q) { setParam (q, CommonIDs::bypass, 1.0f); });
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[1])).getFullPathName()
                                      : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    testParameters();
    testBurst();
    testTracks();
    testLengthAndSync();
    testBypassAndSilence();
    testLevel();
    testEfficiency();
    testStateAndPresets();
    testWindow();
    renders (outDir);
    return finish();
}

JUCE_END_IGNORE_WARNINGS_GCC_LIKE
