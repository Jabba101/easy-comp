// Verification harness for Easy Gate (not shipped).

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TestHelpers.h"
#include <juce_audio_formats/juce_audio_formats.h>

using namespace th;
using P = EasyGateAudioProcessor;

namespace
{
    constexpr double sr = 48000.0;
    const double twoPi = juce::MathConstants<double>::twoPi;

    //==========================================================================
    /** Sets the gate up for a track (and a sidechain: 0 channels = switched off). */
    bool prepareGate (P& p, double rate, int block, int trackChannels = 2, int sidechainChannels = 2)
    {
        juce::AudioProcessor::BusesLayout layout;
        const auto set = [] (int n) { return n == 0 ? juce::AudioChannelSet::disabled() : juce::AudioChannelSet::canonicalChannelSet (n); };
        layout.inputBuses.add (set (trackChannels));
        layout.inputBuses.add (set (sidechainChannels));
        layout.outputBuses.add (set (trackChannels));
        const bool ok = p.setBusesLayout (layout);
        p.setRateAndBufferSizeDetails (rate, block);
        p.prepareToPlay (rate, block);
        return ok;
    }

    /** Runs a track (plus what goes into the sidechain, if anything) through
        the gate, the way a DAW does, and returns what came out. */
    juce::AudioBuffer<float> runGate (P& p, const juce::AudioBuffer<float>& track, const juce::AudioBuffer<float>* sidechain, int block,
                                      std::function<void (int)> beforeBlock = {})
    {
        const int trackChannels = track.getNumChannels();
        const int sidechainChannels = p.getChannelCountOfBus (true, 1);
        const int n = track.getNumSamples();
        juce::AudioBuffer<float> work (trackChannels + sidechainChannels, n);
        work.clear();

        for (int ch = 0; ch < trackChannels; ++ch)
            work.copyFrom (ch, 0, track, ch, 0, n);

        if (sidechain != nullptr && sidechain->getNumChannels() > 0)
            for (int ch = 0; ch < sidechainChannels; ++ch)
                work.copyFrom (trackChannels + ch, 0, *sidechain, std::min (ch, sidechain->getNumChannels() - 1), 0, n);

        juce::MidiBuffer midi;

        for (int start = 0; start < n; start += block)
        {
            if (beforeBlock) beforeBlock (start);
            juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), work.getNumChannels(), start, std::min (block, n - start));
            p.processBlock (view, midi);
        }

        juce::AudioBuffer<float> out (trackChannels, n);
        for (int ch = 0; ch < trackChannels; ++ch)
            out.copyFrom (ch, 0, work, ch, 0, n);
        return out;
    }

    juce::AudioBuffer<float> runGate (P& p, const juce::AudioBuffer<float>& track, int block = 512)
    {
        return runGate (p, track, nullptr, block);
    }

    JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal")   // (exact answers are the point here)

    /** How many samples differ from the input played 'latency' samples late, from 'from' on. */
    int samplesNotDelayedCopy (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out, int latency, int from = 0)
    {
        int count = 0;
        for (int ch = 0; ch < out.getNumChannels(); ++ch)
            for (int i = from; i < out.getNumSamples(); ++i)
                if (out.getSample (ch, i) != (i >= latency ? in.getSample (ch, i - latency) : 0.0f))
                    ++count;
        return count;
    }

    JUCE_END_IGNORE_WARNINGS_GCC_LIKE

    /** White noise at a level (dB peak). */
    juce::AudioBuffer<float> noise (double rate, double seconds, float db, int channels = 2, int seed = 11)
    {
        juce::AudioBuffer<float> b (channels, (int) (rate * seconds));
        juce::Random rng (seed);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (ch, i, fromDb (db) * (rng.nextFloat() * 2.0f - 1.0f));
        return b;
    }

    /** Adds a tone burst: frequency, level (dB peak), start and length in seconds, with 2 ms fades. */
    void addBurst (juce::AudioBuffer<float>& b, double rate, double freq, float db, double start, double length)
    {
        const int from = (int) (start * rate), to = std::min (b.getNumSamples(), (int) ((start + length) * rate));
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = from; i < to; ++i)
            {
                const double t = (i - from) / rate;
                const double fade = std::min ({ 1.0, t / 0.002, ((to - i) / rate) / 0.002 });
                b.addSample (ch, i, (float) (fromDb (db) * fade * std::sin (twoPi * freq * t)));
            }
    }

    /** Adds a pure decaying tone, like a tom hit: level (dB peak) at the start, decay time constant. */
    void addDecayingTone (juce::AudioBuffer<float>& b, double rate, double freq, float db, double start, double decaySeconds)
    {
        const int from = (int) (start * rate);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = from; i < b.getNumSamples(); ++i)
            {
                const double t = (i - from) / rate;
                b.addSample (ch, i, (float) (fromDb (db) * std::exp (-t / decaySeconds) * std::sin (twoPi * freq * t)));
            }
    }

    float levelDb (const juce::AudioBuffer<float>& b, int ch, double start, double end, double rate = sr)
    {
        return toDb (peak (b, ch, (int) (start * rate), (int) (end * rate)));
    }

    juce::AudioBuffer<float> loadWav (const juce::String& name)
    {
        juce::WavAudioFormat wav;
        const juce::File file ("/home/claude/presetcheck/material/" + name + ".wav");
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
        juce::AudioBuffer<float> b;
        if (reader == nullptr) { std::cout << "  (couldn't read " << name << ")\n"; return b; }
        b.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&b, 0, (int) reader->lengthInSamples, 0, true, true);
        return b;
    }

    /** Does this help-bar text fit its two lines? (Like presetTextsReport, for the window's hints.) */
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

    //==========================================================================
    void testParameters()
    {
        std::cout << "\n[parameters, latency and inputs]\n";
        P p;
        check (p.getParameters().size() == 9, "9 settings visible to the DAW (7 knobs, OPENED BY, BYPASS) - got " + juce::String (p.getParameters().size()));
        check (! p.hasAutoGain() && p.parameters.getParameter (CommonIDs::autoGain) == nullptr, "no AUTO-GAIN (a gate doesn't change the loudness of what it lets through)");
        check (p.getBypassParameter() == p.parameters.getParameter (CommonIDs::bypass), "BYPASS is the DAW's own bypass switch");

        const auto& d = gatePresets[0];
        check (near (getParam (p, GateIDs::threshold), d.thresholdDb, 1e-4f) && near (getParam (p, GateIDs::range), d.rangeDb, 1e-4f)
                 && near (getParam (p, GateIDs::attack), d.attackMs, 1e-4f) && near (getParam (p, GateIDs::hold), d.holdMs, 1e-4f)
                 && near (getParam (p, GateIDs::release), d.releaseMs, 1e-4f) && near (getParam (p, GateIDs::lowCut), Gate::lowCutOffHz, 1e-4f)
                 && near (getParam (p, GateIDs::highCut), Gate::highCutOffHz, 1e-4f) && getParam (p, GateIDs::openedBy) < 0.5f,
               "defaults match 'Default settings', with both IGNOREs off and OPENED BY: THIS TRACK");

        auto text = [&] (const char* id, float v) { auto* prm = p.parameters.getParameter (id); return prm->getText (prm->convertTo0to1 (v), 0); };
        auto typed = [&] (const char* id, const char* t) { auto* prm = p.parameters.getParameter (id); return prm->convertFrom0to1 (prm->getValueForText (t)); };

        check (text (GateIDs::threshold, -35.0f) == "-35.0 dB" && text (GateIDs::range, 20.0f) == "20.0 dB" && text (GateIDs::range, 80.0f) == "Full",
               "THRESHOLD reads '-35.0 dB'; RANGE '20.0 dB', and 'Full' at the top");
        check (text (GateIDs::attack, 0.5f) == "0.50 ms" && text (GateIDs::hold, 1500.0f) == "1500 ms" && text (GateIDs::release, 150.0f) == "150 ms",
               "ATTACK reads '0.50 ms', HOLD '1500 ms', RELEASE '150 ms'");
        check (text (GateIDs::lowCut, 20.0f) == "Off" && text (GateIDs::lowCut, 180.0f) == "180 Hz" && text (GateIDs::lowCut, 2500.0f) == "2.5 kHz"
                 && text (GateIDs::highCut, 20000.0f) == "Off" && text (GateIDs::highCut, 4000.0f) == "4.0 kHz",
               "IGNORE BELOW reads 'Off', '180 Hz', '2.5 kHz'; IGNORE ABOVE 'Off', '4.0 kHz'");
        check (near (typed (GateIDs::range, "-30"), 30.0f, 0.05f) && near (typed (GateIDs::range, "full"), 80.0f, 0.05f)
                 && near (typed (GateIDs::lowCut, "2.5k"), 2500.0f, 1.0f) && near (typed (GateIDs::lowCut, "off"), 20.0f, 0.5f)
                 && near (typed (GateIDs::highCut, "3 kHz"), 3000.0f, 1.0f) && near (typed (GateIDs::highCut, "OFF"), 20000.0f, 1.0f),
               "typing works: '-30' or '30' for RANGE, 'full', '2.5k', '3 kHz', 'off'");

        bool latencyOk = true;
        juce::String latencies;
        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            P q;
            prepareGate (q, rate, 512);
            latencyOk = latencyOk && q.getLatencySamples() == (int) std::lround (rate * 0.002);
            latencies << q.getLatencySamples() << " ";
        }
        check (latencyOk, "it tells the DAW about the 2 ms look-ahead: " + latencies.trim() + " samples at 44.1, 48, 96 and 192 kHz");

        check (p.getBusCount (true) == 2 && p.getBus (true, 1)->getName() == "Sidechain" && p.getBusCount (false) == 1,
               "two inputs (your track and the sidechain), one output");

        bool layoutsOk = true;
        for (int track : { 1, 2 })
            for (int side : { 0, 1, 2 })
            {
                P q;
                layoutsOk = layoutsOk && prepareGate (q, sr, 512, track, side);
            }
        juce::AudioProcessor::BusesLayout bad;
        bad.inputBuses.add (juce::AudioChannelSet::stereo()); bad.inputBuses.add (juce::AudioChannelSet::stereo());
        bad.outputBuses.add (juce::AudioChannelSet::mono());
        juce::AudioProcessor::BusesLayout surround;
        surround.inputBuses.add (juce::AudioChannelSet::create5point1()); surround.inputBuses.add (juce::AudioChannelSet::stereo());
        surround.outputBuses.add (juce::AudioChannelSet::create5point1());
        check (layoutsOk && ! p.checkBusesLayoutSupported (bad) && ! p.checkBusesLayoutSupported (surround),
               "mono or stereo tracks, with the sidechain mono, stereo or switched off; in and out must match");
    }

    //==========================================================================
    void testGating()
    {
        std::cout << "\n[opening and closing]\n";
        const int D = 96;   // the look-ahead at 48 kHz

        {
            P p; setParam (p, GateIDs::threshold, -40.0f); prepareGate (p, sr, 512);
            const auto in = sine (sr, 1000.0, fromDb (-12.0f), 2.0);
            const auto out = runGate (p, in);
            check (samplesNotDelayedCopy (in, out, D) == 0, "a sound over the threshold comes out bit for bit untouched, just 2 ms later");
        }
        {
            P p; setParam (p, GateIDs::threshold, -40.0f); setParam (p, GateIDs::range, 40.0f); prepareGate (p, sr, 512);
            const auto in = noise (sr, 2.0, -60.0f);
            const auto out = runGate (p, in);
            float worst = 0.0f;
            for (int i = D; i < in.getNumSamples(); ++i)
                worst = std::max (worst, std::abs (out.getSample (0, i) - in.getSample (0, i - D) * 0.01f));
            check (worst < 1.0e-9f, "noise under the threshold is turned down by exactly RANGE (40 dB)");

            P q; setParam (q, GateIDs::threshold, -40.0f); setParam (q, GateIDs::range, 80.0f); prepareGate (q, sr, 512);
            const auto silent = runGate (q, in);
            check (peak (silent, 0, 0, silent.getNumSamples()) == 0.0f && peak (silent, 1, 0, silent.getNumSamples()) == 0.0f,
                   "RANGE at FULL: true digital silence (so effects after it can rest too)");
        }

        // The very start of each hit comes through untouched, thanks to the look-ahead
        {
            juce::String values;
            bool allClean = true;
            for (float attack : { 0.1f, 0.5f, 1.0f, 2.0f })
            {
                P p; setParam (p, GateIDs::threshold, -30.0f); setParam (p, GateIDs::range, 80.0f); setParam (p, GateIDs::attack, attack);
                prepareGate (p, sr, 480);
                auto in = noise (sr, 2.0, -70.0f);
                const auto hit = singleHit (sr, 1.0, -6.0f, 0.1);
                for (int ch = 0; ch < 2; ++ch) in.addFrom (ch, second, hit, ch, 0, second);
                const auto out = runGate (p, in, 480);
                float worst = 0.0f;
                for (int i = second; i < second + 960; ++i)   // the first 20 ms
                    if (std::abs (in.getSample (0, i)) > 1.0e-3f)
                        worst = std::max (worst, std::abs (toDb (std::abs (out.getSample (0, i + D) / in.getSample (0, i)))));
                allClean = allClean && worst < 0.1f;
                values << juce::String (worst, 3) << " ";
            }
            check (allClean, "ATTACK up to 2 ms: the first 20 ms of a hit come through untouched (worst change, dB: " + values.trim() + ")");

            P p; setParam (p, GateIDs::threshold, -30.0f); setParam (p, GateIDs::range, 80.0f); setParam (p, GateIDs::attack, 20.0f);
            prepareGate (p, sr, 480);
            juce::AudioBuffer<float> in (2, 2 * second);
            in.clear();
            const auto hit = singleHit (sr, 1.0, -6.0f, 0.1);
            for (int ch = 0; ch < 2; ++ch) in.addFrom (ch, second, hit, ch, 0, second);
            const auto out = runGate (p, in, 480);
            const float startChange = toDb (peak (out, 0, second + D, second + D + 96)) - toDb (peak (in, 0, second, second + 96));
            const float laterChange = toDb (peak (out, 0, second + D + 2400, second + D + 2880)) - toDb (peak (in, 0, second + 2400, second + 2880));
            check (startChange < -6.0f && std::abs (laterChange) < 0.1f, "ATTACK 20 ms softens the start of the hit instead (" + juce::String (startChange, 1)
                                                                            + " dB in the first 2 ms, " + juce::String (laterChange, 2) + " dB after 50 ms)");
        }

        // HOLD and RELEASE: it stays fully open for HOLD after the sound drops
        // under the close level, then closes over RELEASE
        {
            P p;
            setParam (p, GateIDs::threshold, -30.0f); setParam (p, GateIDs::range, 40.0f);
            setParam (p, GateIDs::hold, 100.0f); setParam (p, GateIDs::release, 200.0f);
            prepareGate (p, sr, 512);
            juce::AudioBuffer<float> in (2, 3 * second);
            in.clear();
            addDecayingTone (in, sr, 200.0, -6.0f, 0.5, 0.1);
            const auto out = runGate (p, in);

            int lastLoud = 0;   // the last sample at or over the close level (4 dB under the threshold)
            for (int i = 0; i < in.getNumSamples(); ++i)
                if (std::abs (in.getSample (0, i)) >= fromDb (-34.0f)) lastLoud = i;

            const auto change = [&] (int from, int to)   // the least and most it was turned down (dB) over [from, to) of the input's timeline
            {
                float least = -1000.0f, most = 1000.0f;
                for (int i = from; i < to; ++i)
                    if (std::abs (in.getSample (0, i)) > 1.0e-5f)
                    {
                        const float db = toDb (std::abs (out.getSample (0, i + D) / in.getSample (0, i)));
                        least = std::max (least, db);
                        most = std::min (most, db);
                    }
                return std::make_pair (least, most);
            };

            const int ms = 48;
            const auto duringHold = change (lastLoud, lastLoud + 99 * ms);
            const auto afterRelease = change (lastLoud + 301 * ms, lastLoud + 500 * ms);
            const auto halfway = change (lastLoud + 200 * ms, lastLoud + 200 * ms + 48);
            check (std::abs (duringHold.second) < 0.001f, "HOLD 100 ms: fully open for 100 ms after the sound drops under the close level ("
                                                            + juce::String (duringHold.second, 4) + " dB)");
            check (afterRelease.first < -33.5f && afterRelease.second > -40.01f,
                   "RELEASE 200 ms: closed once HOLD and RELEASE are over (" + juce::String (afterRelease.first, 1) + " to " + juce::String (afterRelease.second, 1)
                     + " dB, RANGE is 40)");
            check (halfway.second < -3.0f && halfway.first > -30.0f, "...and on its way there halfway through RELEASE (" + juce::String (halfway.second, 1) + " dB)");
        }

        // Built-in hysteresis: it closes 4 dB under the threshold, so a sound
        // hovering just under the line doesn't make it flutter
        {
            P p; setParam (p, GateIDs::threshold, -30.0f); setParam (p, GateIDs::range, 40.0f); setParam (p, GateIDs::hold, 0.0f);
            setParam (p, GateIDs::release, 20.0f); prepareGate (p, sr, 512);
            juce::AudioBuffer<float> in (2, 3 * second);
            in.clear();
            addBurst (in, sr, 500.0, -27.0f, 0.0, 1.0);     // over the threshold: opens
            addBurst (in, sr, 500.0, -32.0f, 1.0, 1.0);     // 2 dB under it: stays open
            addBurst (in, sr, 500.0, -36.0f, 2.0, 1.0);     // under the close level: closes
            const auto out = runGate (p, in);
            const float hovering = levelDb (out, 0, 1.2, 1.9) - levelDb (in, 0, 1.2 - 0.002, 1.9 - 0.002);
            const float under = levelDb (out, 0, 2.3, 2.9) - levelDb (in, 0, 2.3 - 0.002, 2.9 - 0.002);
            check (std::abs (hovering) < 0.01f && under < -39.0f, "it closes 4 dB under THRESHOLD: still open 2 dB under (" + juce::String (hovering, 2)
                                                                    + " dB), closed 6 dB under (" + juce::String (under, 1) + " dB)");

            P q; setParam (q, GateIDs::threshold, -30.0f); setParam (q, GateIDs::range, 40.0f); prepareGate (q, sr, 512);
            juce::AudioBuffer<float> quiet (2, 2 * second);
            quiet.clear();
            addBurst (quiet, sr, 500.0, -32.0f, 0.0, 2.0);  // never over the threshold
            const auto quietOut = runGate (q, quiet);
            check (levelDb (quietOut, 0, 0.5, 1.9) - levelDb (quiet, 0, 0.5, 1.9) < -39.9f, "...but a sound that never goes over THRESHOLD never opens it");
        }

        // Low notes: the gaps between the waves don't make it flutter
        for (double freq : { 30.0, 40.0, 60.0 })
        {
            P p; setParam (p, GateIDs::threshold, -30.0f); setParam (p, GateIDs::hold, 0.0f); setParam (p, GateIDs::release, 5.0f);
            prepareGate (p, sr, 512);
            const auto in = sine (sr, freq, fromDb (-27.0f), 2.0);
            const auto out = runGate (p, in);
            check (samplesNotDelayedCopy (in, out, D, D + 4800) == 0, "a " + juce::String ((int) freq) + " Hz note just 3 dB over THRESHOLD, with HOLD 0 and "
                                                                       "RELEASE 5 ms: no flutter, it comes through bit for bit");
        }

        // Stereo-linked, mono works, buffer size and sample rate don't matter
        {
            P p; setParam (p, GateIDs::threshold, -30.0f); setParam (p, GateIDs::range, 30.0f); setParam (p, GateIDs::release, 50.0f);
            prepareGate (p, sr, 512);
            auto in = drumLoop (sr, 3.0);
            in.applyGain (1, 0, in.getNumSamples(), 0.05f);   // the right side much quieter
            const auto out = runGate (p, in);
            float worst = 0.0f;
            for (int i = D; i < in.getNumSamples(); ++i)
                if (std::abs (in.getSample (1, i - D)) > 1.0e-4f)
                    worst = std::max (worst, std::abs (out.getSample (0, i) / in.getSample (0, i - D) - out.getSample (1, i) / in.getSample (1, i - D)));
            check (worst < 1.0e-4f, "both channels always get exactly the same change (worst mismatch " + juce::String (worst) + ")");
        }
        {
            P p; setParam (p, GateIDs::threshold, -30.0f); prepareGate (p, sr, 512, 1, 0);
            juce::AudioBuffer<float> in = noise (sr, 1.0, -60.0f, 1);
            addBurst (in, sr, 300.0, -10.0f, 0.2, 0.3);
            const auto out = runGate (p, in);
            check (std::abs (levelDb (out, 0, 0.3, 0.45) - levelDb (in, 0, 0.3 - 0.002, 0.45 - 0.002)) < 0.01f
                     && levelDb (out, 0, 0.8, 1.0) - levelDb (in, 0, 0.8, 1.0) < -39.0f,
                   "works on a mono track");
        }
        {
            P a; setParam (a, GateIDs::threshold, -35.0f); setParam (a, GateIDs::lowCut, 150.0f); setParam (a, GateIDs::release, 80.0f);
            P b; setParam (b, GateIDs::threshold, -35.0f); setParam (b, GateIDs::lowCut, 150.0f); setParam (b, GateIDs::release, 80.0f);
            prepareGate (a, sr, 1024); prepareGate (b, sr, 1024);
            const auto loop = drumLoop (sr, 2.0);
            const auto outA = runGate (a, loop, 1024);
            juce::AudioBuffer<float> work (4, loop.getNumSamples());
            work.clear();
            for (int ch = 0; ch < 2; ++ch) work.copyFrom (ch, 0, loop, ch, 0, loop.getNumSamples());
            juce::MidiBuffer midi;
            for (int start = 0, size = 1; start < work.getNumSamples(); start += size, size = (size * 7) % 997 + 1)
            {
                juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), 4, start, std::min (size, work.getNumSamples() - start));
                b.processBlock (view, midi);
            }
            float diff = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < loop.getNumSamples(); ++i)
                    diff = std::max (diff, std::abs (outA.getSample (ch, i) - work.getSample (ch, i)));
            check (diff == 0.0f, "the result doesn't depend on the DAW's buffer size: bit for bit the same with blocks of 1 to 997 samples");
        }
        {
            bool same = true;
            float reference = -1.0f;
            juce::String values;
            for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            {
                P p; setParam (p, GateIDs::threshold, -30.0f); setParam (p, GateIDs::range, 40.0f);
                setParam (p, GateIDs::hold, 100.0f); setParam (p, GateIDs::release, 200.0f);
                prepareGate (p, rate, 512);
                juce::AudioBuffer<float> in (2, (int) (rate * 2.0));
                in.clear();
                addDecayingTone (in, rate, 200.0, -6.0f, 0.2, 0.1);
                const auto out = runGate (p, in);
                const int latency = p.getLatencySamples();
                int at = -1;   // when is it 20 dB down?
                for (int i = (int) (rate * 0.3); i + latency < in.getNumSamples() && at < 0; ++i)
                {
                    const float x = in.getSample (0, i);
                    if (std::abs (x) > 1.0e-4f && std::abs (out.getSample (0, i + latency) / x) < 0.1f) at = i;
                }
                const float ms = (float) (1000.0 * at / rate);
                if (reference < 0.0f) reference = ms;
                same = same && at > 0 && near (ms, reference, 1.5f);
                values << juce::String (ms, 1) << " ";
            }
            check (same, "the timing is the same at 44.1, 48, 96 and 192 kHz (20 dB down at, ms: " + values.trim() + ")");
        }
        {
            P p; setParam (p, GateIDs::threshold, -80.0f); setParam (p, GateIDs::range, 80.0f); setParam (p, GateIDs::attack, 0.1f);
            setParam (p, GateIDs::hold, 0.0f); setParam (p, GateIDs::release, 5.0f); setParam (p, GateIDs::lowCut, 5000.0f); setParam (p, GateIDs::highCut, 50.0f);
            prepareGate (p, 44100.0, 64);
            juce::AudioBuffer<float> nasty (2, 44100);
            juce::Random rng (42);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < nasty.getNumSamples(); ++i)
                    nasty.setSample (ch, i, i < 11025 ? (rng.nextFloat() * 2.0f - 1.0f) * 4.0f : (i < 22050 ? 0.0f : (i % 4410 == 0 ? 1.0f : 1.0e-38f)));
            const auto out = runGate (p, nasty, 64);
            check (allFinite (out) && peak (out, 0, 11025 + 4410, 22050) == 0.0f, "extreme settings and nasty input: no NaN/inf, and silence stays silent");
        }
    }

    //==========================================================================
    void testKeyFilters()
    {
        std::cout << "\n[IGNORE BELOW and IGNORE ABOVE]\n";

        const auto opened = [] (float lowCut, float highCut, double freq)   // does a -20 dB burst at freq open it? (dB change)
        {
            P p; setParam (p, GateIDs::threshold, -30.0f); setParam (p, GateIDs::range, 40.0f); setParam (p, GateIDs::hold, 0.0f);
            setParam (p, GateIDs::release, 20.0f); setParam (p, GateIDs::lowCut, lowCut); setParam (p, GateIDs::highCut, highCut);
            prepareGate (p, sr, 512);
            juce::AudioBuffer<float> in (2, second);
            in.clear();
            addBurst (in, sr, freq, -20.0f, 0.3, 0.3);
            const auto out = runGate (p, in);
            return levelDb (out, 0, 0.35, 0.55) - levelDb (in, 0, 0.35 - 0.002, 0.55 - 0.002);
        };

        const float kickOff = opened (20.0f, 20000.0f, 55.0f), kickIgnored = opened (250.0f, 20000.0f, 55.0f), snare = opened (250.0f, 20000.0f, 2000.0f);
        check (kickOff > -0.1f && kickIgnored < -39.0f && snare > -0.1f,
               "IGNORE BELOW 250 Hz: kick bleed (55 Hz) no longer opens it (" + juce::String (kickIgnored, 1) + " dB), a snare's crack (2 kHz) still does");

        const float hatOff = opened (20.0f, 20000.0f, 8000.0f), hatIgnored = opened (20.0f, 2000.0f, 8000.0f), tom = opened (20.0f, 2000.0f, 120.0f);
        check (hatOff > -0.1f && hatIgnored < -39.0f && tom > -0.1f,
               "IGNORE ABOVE 2 kHz: cymbal and hi-hat bleed (8 kHz) no longer opens it (" + juce::String (hatIgnored, 1) + " dB), a tom (120 Hz) still does");

        {
            P p; setParam (p, GateIDs::threshold, -60.0f); setParam (p, GateIDs::lowCut, 300.0f); setParam (p, GateIDs::highCut, 3000.0f);
            prepareGate (p, sr, 512);
            auto in = sine (sr, 1000.0, fromDb (-12.0f), 2.0);
            const auto low = sine (sr, 50.0, fromDb (-10.0f), 2.0), high = sine (sr, 9000.0, fromDb (-14.0f), 2.0);
            for (int ch = 0; ch < 2; ++ch) { in.addFrom (ch, 0, low, ch, 0, in.getNumSamples()); in.addFrom (ch, 0, high, ch, 0, in.getNumSamples()); }
            const auto out = runGate (p, in);
            check (samplesNotDelayedCopy (in, out, 96, 4800) == 0, "they only change what the gate hears, never your sound: open, it's still bit for bit untouched");
        }
    }

    //==========================================================================
    void testSidechain()
    {
        std::cout << "\n[OPENED BY: SIDECHAIN]\n";
        const int D = 96;

        // A loud track, opened only by what comes into the sidechain
        const auto track = sine (sr, 220.0, fromDb (-10.0f), 3.0);
        juce::AudioBuffer<float> key (2, 3 * second);
        key.clear();
        addBurst (key, sr, 1000.0, -12.0f, 1.0, 1.0);

        {
            P p; setParam (p, GateIDs::threshold, -30.0f); setParam (p, GateIDs::range, 40.0f); setParam (p, GateIDs::hold, 0.0f);
            setParam (p, GateIDs::release, 20.0f); setParam (p, GateIDs::openedBy, 1.0f);
            prepareGate (p, sr, 512);
            const auto out = runGate (p, track, &key, 512);
            const float before = levelDb (out, 0, 0.2, 0.9) - levelDb (track, 0, 0.2, 0.9);
            const float during = levelDb (out, 0, 1.1, 1.9) - levelDb (track, 0, 1.1, 1.9);
            const float after  = levelDb (out, 0, 2.3, 2.9) - levelDb (track, 0, 2.3, 2.9);
            check (before < -39.9f && std::abs (during) < 0.01f && after < -39.9f,
                   "a loud track stays closed until the sidechain opens it, then closes with it (" + juce::String (before, 1) + " / "
                     + juce::String (during, 2) + " / " + juce::String (after, 1) + " dB)");
            check (! p.isSidechainMissing(), "no warning while something's coming into the sidechain");
        }
        {
            P p; setParam (p, GateIDs::threshold, -30.0f); setParam (p, GateIDs::range, 40.0f); setParam (p, GateIDs::openedBy, 1.0f);
            prepareGate (p, sr, 512, 2, 1);
            juce::AudioBuffer<float> monoKey (1, 3 * second);
            monoKey.clear();
            addBurst (monoKey, sr, 1000.0, -12.0f, 1.0, 1.0);
            const auto out = runGate (p, track, &monoKey, 512);
            check (std::abs (levelDb (out, 1, 1.1, 1.9) - levelDb (track, 1, 1.1, 1.9)) < 0.01f && levelDb (out, 1, 0.2, 0.9) - levelDb (track, 1, 0.2, 0.9) < -39.9f,
                   "a mono sidechain on a stereo track works too");
        }
        {
            P p; setParam (p, GateIDs::threshold, -30.0f); setParam (p, GateIDs::range, 40.0f); setParam (p, GateIDs::openedBy, 1.0f);
            prepareGate (p, sr, 512, 2, 0);
            const auto out = runGate (p, track, nullptr, 512);
            check (levelDb (out, 0, 0.5, 2.9) - levelDb (track, 0, 0.5, 2.9) < -39.9f && p.isSidechainMissing(),
                   "with the sidechain switched off in the DAW it stays closed - and the window says why");

            setParam (p, GateIDs::openedBy, 0.0f);
            juce::AudioBuffer<float> more (track);
            const auto own = runGate (p, more, nullptr, 512);
            check (! p.isSidechainMissing() && samplesNotDelayedCopy (more, own, D, 4800) == 0,
                   "back on OPENED BY: THIS TRACK, it listens to the track again and the warning goes");
        }
        {
            P p; setParam (p, GateIDs::openedBy, 1.0f); prepareGate (p, sr, 512);
            juce::AudioBuffer<float> silence (2, 3 * second);
            silence.clear();
            runGate (p, silence, nullptr, 512);
            check (! p.isSidechainMissing(), "no warning while the track itself is silent (e.g. the DAW is stopped)");
        }
    }

    //==========================================================================
    void testListenAndBypass()
    {
        std::cout << "\n[LISTEN and BYPASS]\n";
        const int D = 96;

        {
            P p; setParam (p, GateIDs::threshold, -20.0f); setParam (p, GateIDs::lowCut, 500.0f); prepareGate (p, sr, 512);
            check (! p.isListening(), "LISTEN starts off");
            p.setListening (true);
            juce::AudioBuffer<float> in = sine (sr, 60.0, fromDb (-10.0f), 2.0);
            const auto high = sine (sr, 3000.0, fromDb (-20.0f), 2.0);
            for (int ch = 0; ch < 2; ++ch) in.addFrom (ch, 0, high, ch, 0, in.getNumSamples());
            const auto out = runGate (p, in);
            const float level = levelDb (out, 0, 0.5, 1.9);
            check (near (level, -20.0f, 1.0f), "LISTEN plays what the gate hears: here, the track with its low end ignored (" + juce::String (level, 1) + " dB: the 3 kHz part)");
        }
        {
            P p; setParam (p, GateIDs::openedBy, 1.0f); prepareGate (p, sr, 512);
            p.setListening (true);
            const auto track = sine (sr, 220.0, fromDb (-10.0f), 1.0);
            const auto key = sine (sr, 1000.0, fromDb (-16.0f), 1.0);
            const auto out = runGate (p, track, &key, 512);
            check (near (levelDb (out, 0, 0.5, 0.9), -16.0f, 0.1f), "with OPENED BY: SIDECHAIN, LISTEN plays the sidechain");
        }
        {
            P a; a.setListening (true);
            juce::MemoryBlock state;
            a.getStateInformation (state);
            P b; b.setStateInformation (state.getData(), (int) state.getSize());
            check (! b.isListening(), "LISTEN isn't saved with the project (so it can't be left on by accident)");

            P c; c.setListening (true); prepareGate (c, sr, 512);
            {
                std::unique_ptr<juce::AudioProcessorEditor> editor (c.createEditor());
                auto* window = dynamic_cast<EasyGateAudioProcessorEditor*> (editor.get());
                check (window != nullptr && window->listenButton.getToggleState(), "the window's LISTEN button shows it's on");
            }
            check (! c.isListening(), "closing the window switches LISTEN off");
        }
        {
            P p; setParam (p, GateIDs::threshold, -30.0f); setParam (p, GateIDs::range, 80.0f); setParam (p, CommonIDs::bypass, 1.0f);
            prepareGate (p, sr, 512);
            const auto loop = drumLoop (sr, 4.0);
            check (samplesNotDelayedCopy (loop, runGate (p, loop), D) == 0, "bypassed: the original, bit for bit (2 ms later, so it stays in time with the gated sound)");
        }

        // Flicking BYPASS and LISTEN while playing: smooth fades, no clicks
        {
            const auto in = sine (sr, 100.0, fromDb (-20.0f), 4.0);   // under the threshold: closed, 40 dB down
            P p; setParam (p, GateIDs::threshold, -10.0f); setParam (p, GateIDs::range, 40.0f); prepareGate (p, sr, 256);
            const auto out = runGate (p, in, nullptr, 256, [&] (int start)
            {
                setParam (p, CommonIDs::bypass, (start / 12288) % 2 == 0 ? 0.0f : 1.0f);
                p.setListening ((start / 20480) % 2 == 1);
            });
            float worstStep = 0.0f, ownStep = 0.0f;
            for (int i = 4800; i < in.getNumSamples(); ++i)
            {
                worstStep = std::max (worstStep, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
                ownStep = std::max (ownStep, std::abs (in.getSample (0, i) - in.getSample (0, i - 1)));
            }
            check (worstStep < ownStep * 1.5f, "flicking BYPASS and LISTEN while playing: smooth fades, no clicks (largest step " + juce::String (worstStep, 5)
                                                 + ", the tone's own " + juce::String (ownStep, 5) + ")");
        }

        // Opening and closing follow smooth curves, taking ATTACK and RELEASE to do it
        {
            P p; setParam (p, GateIDs::threshold, -30.0f); setParam (p, GateIDs::range, 80.0f); setParam (p, GateIDs::hold, 10.0f);
            setParam (p, GateIDs::release, 50.0f); setParam (p, GateIDs::attack, 1.0f);
            prepareGate (p, sr, 512);
            juce::AudioBuffer<float> in = sine (sr, 100.0, fromDb (-40.0f), 2.0);   // a quiet hum, always there...
            addBurst (in, sr, 3000.0, -12.0f, 0.5, 0.3);                            // ...and something loud for a moment
            const auto hum = sine (sr, 100.0, fromDb (-40.0f), 2.0);
            const auto out = runGate (p, in);

            float worstStep = 0.0f, previousGain = -1.0f;
            int opening = 0, closing = 0;   // samples spent between 1% and 99% open
            for (int i = 2400; i < in.getNumSamples() - D; ++i)
            {
                const double t = (double) i / sr;
                const float x = hum.getSample (0, i);
                if (std::abs (x) < fromDb (-46.0f) || (t > 0.4975 && t < 0.8)) { previousGain = -1.0f; continue; }
                const float gain = out.getSample (0, i + D) / x;
                if (previousGain >= 0.0f) worstStep = std::max (worstStep, std::abs (gain - previousGain));
                previousGain = gain;
            }
            // (the gain itself, straight from the gate, for how long each takes)
            Gate gate;
            Gate::Settings s; s.thresholdDb = -30.0f; s.rangeDb = 80.0f; s.holdMs = 10.0f; s.releaseMs = 50.0f; s.attackMs = 1.0f;
            gate.prepare (sr, 240, s);
            juce::AudioBuffer<float> probe (in);
            float gainBefore = 0.0f;
            for (int start = 0; start < probe.getNumSamples(); start += 1)
            {
                juce::AudioBuffer<float> one (probe.getArrayOfWritePointers(), 2, start, 1);
                const float gainNow = gate.process (one).gain;
                if (gainNow > 0.01f && gainNow < 0.99f) (gainNow > gainBefore ? opening : closing) += 1;
                gainBefore = gainNow;
            }
            const float openingMs = (float) opening / 48.0f, closingMs = (float) closing / 48.0f;
            check (worstStep < 0.06f && near (openingMs, 1.0f, 0.1f) && near (closingMs, 50.0f, 2.0f),
                   "opening and closing follow smooth S-shaped curves: ATTACK 1 ms opens in " + juce::String (openingMs, 2) + " ms, RELEASE 50 ms closes in "
                     + juce::String (closingMs, 1) + " ms (largest step from one sample to the next " + juce::String (worstStep, 3) + ")");
        }
    }

    //==========================================================================
    /** Like th::restReport, for the gate (which takes a key and gives moments). */
    RestReport gateRestReport (const juce::AudioBuffer<float>& input, int block, Gate::Settings settings,
                               std::function<void (int, Gate::Settings&)> beforeBlock = {})
    {
        juce::ScopedNoDenormals noDenormals;
        auto skipping = std::make_unique<Gate>(), working = std::make_unique<Gate>();
        working->allowResting (false);
        skipping->prepare (sr, 240, settings);
        working->prepare (sr, 240, settings);

        juce::AudioBuffer<float> a (input), b (input);
        RestReport r { -999.0, 0.0, 0, 0 };
        std::vector<Gate::Moment> momentsA, momentsB;

        for (int start = 0; start < input.getNumSamples(); start += block)
        {
            const int n = std::min (block, input.getNumSamples() - start);
            if (beforeBlock) beforeBlock (start, settings);
            skipping->setSettings (settings);
            working->setSettings (settings);
            juce::AudioBuffer<float> va (a.getArrayOfWritePointers(), a.getNumChannels(), start, n);
            juce::AudioBuffer<float> vb (b.getArrayOfWritePointers(), b.getNumChannels(), start, n);
            skipping->process (va, nullptr, [&] (const Gate::Moment& m) { momentsA.push_back (m); });
            working->process (vb, nullptr, [&] (const Gate::Moment& m) { momentsB.push_back (m); });
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
                if (std::abs (y) > 1.0e-4)
                    r.worstLevelDb = std::max (r.worstLevelDb, std::abs (x) < 1.0e-30 ? 999.0 : std::abs (20.0 * std::log10 (x / y)));
            }

        if (worst > 0.0)
            r.differenceDb = 20.0 * std::log10 (worst / std::max (peakLevel, 1.0e-30));

        bool sameMoments = momentsA.size() == momentsB.size();
        for (size_t i = 0; sameMoments && i < momentsA.size(); ++i)
            sameMoments = std::abs (momentsA[i].keyLevel - momentsB[i].keyLevel) < 1.0e-6f && std::abs (momentsA[i].outLevel - momentsB[i].outLevel) < 1.0e-6f
                       && std::abs (momentsA[i].gain - momentsB[i].gain) < 1.0e-6f && momentsA[i].open == momentsB[i].open;
        if (! sameMoments)
            r.worstLevelDb = 999.0;   // (the display would have shown something different)

        return r;
    }

    void testEfficiency()
    {
        std::cout << "\n[efficiency: skipping through silence, speed]\n";
        const auto gapped = withGaps (drumLoop (sr, 6.0));

        struct Case { const char* name; Gate::Settings settings; };
        std::vector<Case> cases;
        { Gate::Settings s; s.thresholdDb = -30.0f; cases.push_back ({ "the default timings", s }); }
        { Gate::Settings s; s.thresholdDb = -25.0f; s.rangeDb = 80.0f; s.lowCutHz = 200.0f; s.highCutHz = 5000.0f; s.holdMs = 0.0f; s.releaseMs = 20.0f;
          cases.push_back ({ "FULL, both IGNOREs, fast", s }); }
        { Gate::Settings s; s.thresholdDb = -40.0f; s.rangeDb = 15.0f; s.holdMs = 200.0f; s.releaseMs = 300.0f; s.listen = true;
          cases.push_back ({ "slower, gentle, LISTEN on", s }); }

        for (auto& c : cases)
        {
            const auto r = gateRestReport (gapped, 256, c.settings);
            check (r.differenceDb <= -110.0 && r.worstLevelDb <= 0.0005 && r.blocksRested > 0,
                   juce::String ("skipping silence sounds (and looks) the same as working through it (") + c.name + "): "
                   + (r.differenceDb < -900.0 ? juce::String ("bit for bit") : juce::String (r.differenceDb, 1) + " dB apart")
                   + "; skipped " + juce::String (r.blocksRested) + " of " + juce::String (r.blocks) + " blocks");
        }

        {
            const auto r = gateRestReport (gapped, 256, cases[0].settings, [] (int start, Gate::Settings& s)
            {
                if (start >= 180000 && start < 180256) { s.rangeDb = 80.0f; s.thresholdDb = -20.0f; }
                if (start >= 200000 && start < 200256) s.bypassed = true;
                if (start >= 230000 && start < 230256) { s.bypassed = false; s.listen = true; s.lowCutHz = 300.0f; }
                if (start >= 250000 && start < 250256) s.listen = false;
            });
            check (r.differenceDb <= -110.0 && r.worstLevelDb <= 0.0005,
                   "...and with knobs moved, BYPASS and LISTEN flicked during the silence: "
                     + (r.differenceDb < -900.0 ? juce::String ("bit for bit") : juce::String (r.differenceDb, 1) + " dB apart"));
        }

       #if JUCE_DEBUG
        check (true, "(the speed checks only run in an optimised build)");
       #else
        {
            P p; setParam (p, GateIDs::threshold, -30.0f); setParam (p, GateIDs::lowCut, 150.0f); setParam (p, GateIDs::highCut, 5000.0f);
            prepareGate (p, sr, 256, 2, 0);
            const double busy = coreShare (p, drumLoop (sr, 30.0));
            check (busy < 0.15, "drums, both IGNOREs on: " + juce::String (busy, 3) + "% of one core (budget 0.15%)");

            P q; prepareGate (q, sr, 256, 2, 0);
            juce::AudioBuffer<float> silence (2, 48000 * 30);
            silence.clear();
            const double idle = coreShare (q, silence);
            check (idle < 0.03, "digital silence: " + juce::String (idle, 4) + "% of one core (budget 0.03%)");
        }
       #endif
    }

    //==========================================================================
    void testDisplayAndState()
    {
        std::cout << "\n[display data, light, saving and presets]\n";
        {
            P p; setParam (p, GateIDs::threshold, -30.0f); prepareGate (p, sr, 512);
            runGate (p, sine (sr, 1000.0, fromDb (-6.0f), 1.0));
            std::vector<P::Moment> moments (1000);
            const int count = p.takeMoments (moments.data(), (int) moments.size());
            check (count == 200, "one second = 200 moments of 5 ms for the display (got " + juce::String (count) + ")");
            const auto& last = moments[(size_t) std::max (0, count - 1)];
            check (count > 10 && near (last.keyDb, -6.0f, 0.1f) && near (last.outDb, -6.0f, 0.1f) && std::abs (last.gainDb) < 1.0e-6f && last.open,
                   "a steady -6 dB tone over the threshold: heard and let out at -6 dB, fully open");
            check (p.takeMoments (moments.data(), 1000) == 0, "moments are only handed over once");
            check (p.takeOpenForLight() && ! p.takeOpenForLight(), "the light sees it open (and is only told once)");
        }
        {
            P p; setParam (p, GateIDs::threshold, -30.0f); setParam (p, GateIDs::range, 25.0f); prepareGate (p, sr, 512);
            runGate (p, sine (sr, 1000.0, fromDb (-40.0f), 1.0));
            std::vector<P::Moment> moments (1000);
            const int count = p.takeMoments (moments.data(), (int) moments.size());
            const auto& last = moments[(size_t) std::max (0, count - 1)];
            check (near (last.keyDb, -40.0f, 0.1f) && near (last.outDb, -65.0f, 0.1f) && near (last.gainDb, -25.0f, 0.01f) && ! last.open
                     && ! p.takeOpenForLight() && near (p.takeGainForLight(), -25.0f, 0.01f),
                   "a -40 dB tone under the threshold: heard at -40 dB, let out at -65 dB (RANGE 25 dB), and the light stays off");
        }
        {
            P a;
            setParam (a, GateIDs::threshold, -33.0f); setParam (a, GateIDs::range, 22.0f); setParam (a, GateIDs::openedBy, 1.0f);
            if (std::size (gatePresets) > 1) loadPresetNamed (a, gatePresets[1].name);
            a.markPresetEdited();
            setParam (a, GateIDs::hold, 123.0f);
            juce::MemoryBlock state;
            a.getStateInformation (state);
            P b;
            b.setStateInformation (state.getData(), (int) state.getSize());
            check (near (getParam (b, GateIDs::hold), 123.0f, 1e-3f) && getParam (b, GateIDs::openedBy) > 0.5f
                     && b.getPresetName() == a.getPresetName() && b.isPresetEdited() == a.isPresetEdited()
                     && near (getParam (b, GateIDs::threshold), getParam (a, GateIDs::threshold), 1e-3f),
                   "the settings, OPENED BY and the preset name survive saving and reopening the project");
        }
        {
            P p;
            setParam (p, GateIDs::openedBy, 1.0f);
            setParam (p, CommonIDs::bypass, 1.0f);
            bool allOk = std::size (gatePresets) == p.getBuiltInPresets().size();
            for (auto& preset : gatePresets)
            {
                loadPresetNamed (p, preset.name);
                allOk = allOk && near (getParam (p, GateIDs::threshold), preset.thresholdDb, 0.051f) && near (getParam (p, GateIDs::range), preset.rangeDb, 0.051f)
                              && near (getParam (p, GateIDs::attack), preset.attackMs, 0.0051f) && near (getParam (p, GateIDs::hold), preset.holdMs, 0.51f)
                              && near (getParam (p, GateIDs::release), preset.releaseMs, 0.51f) && near (getParam (p, GateIDs::lowCut), preset.lowCutHz, 0.51f)
                              && near (getParam (p, GateIDs::highCut), preset.highCutHz, 0.51f) && p.getPresetName() == preset.name;
            }
            check (allOk, "all " + juce::String ((int) std::size (gatePresets)) + " presets load exactly");
            check (getParam (p, GateIDs::openedBy) > 0.5f && getParam (p, CommonIDs::bypass) > 0.5f, "presets leave OPENED BY and BYPASS as they were");
            const auto [textsOk, textsReport] = presetTextsReport (P().getBuiltInPresets());
            check (textsOk, textsReport);
        }
    }

    //==========================================================================
    void testWindow()
    {
        std::cout << "\n[plugin window]\n";
        P p; setParam (p, GateIDs::threshold, -30.0f); prepareGate (p, sr, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* window = dynamic_cast<EasyGateAudioProcessorEditor*> (editor.get());
        check (window != nullptr && editor->getWidth() == 1100 && editor->getHeight() == 600, "window is 1100 x 600, with the shared header");
        if (window == nullptr) return;

        std::vector<juce::Slider*> sliders;
        GateDisplay* display = nullptr;
        GateLight* light = nullptr;
        ChoiceSwitch* openedBy = nullptr;
        bool autoGainShown = false;
        for (auto* child : editor->getChildren())
        {
            if (auto* s = dynamic_cast<juce::Slider*> (child)) sliders.push_back (s);
            if (auto* d = dynamic_cast<GateDisplay*> (child)) display = d;
            if (auto* l = dynamic_cast<GateLight*> (child)) light = l;
            if (auto* c = dynamic_cast<ChoiceSwitch*> (child)) openedBy = c;
            if (child == &window->autoGainButton) autoGainShown = true;
        }
        check (sliders.size() == 7 && display != nullptr && light != nullptr && openedBy != nullptr, "7 knobs, the display, the light and OPENED BY present");
        if (sliders.size() != 7 || display == nullptr || light == nullptr || openedBy == nullptr) return;

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

        sliders[0]->setValue (-42.0, juce::sendNotificationSync);
        check (near (getParam (p, GateIDs::threshold), -42.0f, 1e-3f), "turning THRESHOLD changes the setting");
        check (sliders[1]->getTextFromValue (80.0) == "Full" && sliders[5]->getTextFromValue (20.0) == "Off" && sliders[6]->getTextFromValue (20000.0) == "Off",
               "the boxes read 'Full' and 'Off' at the ends");

        // Every explanation fits the help bar's two lines
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
            hintFor (display); hintFor (light); hintFor (openedBy); hintFor (&window->listenButton); hintFor (&window->bypassButton);
            editor->mouseExit (hoverEvent (editor.get()));
            int lines = 0;
            if (! fitsHelpBar (window->hintBar.getTitle(), window->hintBar.getText(), lines)) { allFit = false; tooLong << window->hintBar.getTitle(); }
            check (allFit, "every explanation fits the help bar (" + juce::String (mostLines) + " lines at most)" + (tooLong.isEmpty() ? juce::String() : ": too long: " + tooLong));
        }

        editor->mouseEnter (hoverEvent (sliders[1]));
        check (window->hintBar.getTitle() == "RANGE", "hovering RANGE explains it");
        editor->mouseEnter (hoverEvent (&window->listenButton));
        check (window->hintBar.getTitle() == "LISTEN", "hovering LISTEN explains it");
        editor->mouseEnter (hoverEvent (&window->bypassButton));
        check (! window->hintBar.getText().contains ("AUTO-GAIN"), "the BYPASS tip doesn't mention AUTO-GAIN (there isn't one)");

        // (A click on LISTEN: it flips the button, then tells the plugin.)
        window->listenButton.setToggleState (true, juce::dontSendNotification);
        window->listenButton.onClick();
        check (p.isListening(), "clicking LISTEN switches it on");
        window->listenButton.setToggleState (false, juce::dontSendNotification);
        window->listenButton.onClick();
        check (! p.isListening(), "...and off again");

        // The light
        {
            P q; setParam (q, GateIDs::threshold, -30.0f); prepareGate (q, sr, 512);
            runGate (q, sine (sr, 500.0, fromDb (-10.0f), 0.5));
            std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
            GateLight* l = nullptr;
            for (auto* child : ed->getChildren()) if (auto* found = dynamic_cast<GateLight*> (child)) l = found;
            P r; setParam (r, GateIDs::threshold, -30.0f); setParam (r, GateIDs::range, 30.0f); prepareGate (r, sr, 512);
            runGate (r, noise (sr, 0.5, -50.0f));
            std::unique_ptr<juce::AudioProcessorEditor> ed2 (r.createEditor());
            GateLight* l2 = nullptr;
            for (auto* child : ed2->getChildren()) if (auto* found = dynamic_cast<GateLight*> (child)) l2 = found;
            check (l != nullptr && l2 != nullptr && l->getWord() == "OPEN" && l2->getWord() == "CLOSED",
                   "the light reads OPEN while a loud sound plays, CLOSED on quiet noise");
        }

        // Presets
        {
            auto& browser = window->presetBrowser;
            const auto presetFolder = juce::File::createTempFile ("easy-gate-presets");
            browser.setUserPresetFolder (presetFolder);
            const auto menu = browser.buildMenu();
            int count = 0;
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                if (it.getItem().subMenu != nullptr) ++count;
            check (count >= 4, "the preset menu has its instrument sub-menus (" + juce::String (count) + ")");
            presetFolder.deleteRecursively();
        }
    }

    //==========================================================================
    void renders (const juce::File& outDir)
    {
        std::cout << "\n[renders]\n";
        const auto toms = loadWav ("toms");
        const auto snare = loadWav ("snare_rock");
        if (toms.getNumSamples() == 0 || snare.getNumSamples() == 0) return;

        const auto slice = [] (const juce::AudioBuffer<float>& source, double from, double seconds)
        {
            juce::AudioBuffer<float> b (2, (int) (seconds * sr));
            for (int ch = 0; ch < 2; ++ch) b.copyFrom (ch, 0, source, std::min (ch, source.getNumChannels() - 1), (int) (from * sr), b.getNumSamples());
            return b;
        };

        const auto render = [&] (const char* fileName, const juce::AudioBuffer<float>& material, std::function<void (P&)> setUp,
                                 std::function<void (juce::AudioProcessorEditor&)> afterOpening = {})
        {
            P q; prepareGate (q, sr, 480, 2, 0);
            if (setUp) setUp (q);
            runGate (q, material, 480);
            std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
            if (afterOpening) afterOpening (*ed);
            savePng (*ed, outDir.getChildFile (fileName));
        };

        const auto hover = [] (int sliderIndex)
        {
            return [sliderIndex] (juce::AudioProcessorEditor& ed)
            {
                int n = 0;
                for (auto* child : ed.getChildren())
                    if (dynamic_cast<juce::Slider*> (child) != nullptr && n++ == sliderIndex)
                        ed.mouseEnter (hoverEvent (child));
            };
        };

        render ("gate_default.png", slice (toms, 2.0, 4.0), {});
        render ("gate_toms.png", slice (toms, 2.0, 4.0), [] (P& q) { setParam (q, GateIDs::threshold, -30.0f); setParam (q, GateIDs::range, 25.0f);
                                                                    setParam (q, GateIDs::hold, 80.0f); setParam (q, GateIDs::release, 250.0f); }, hover (0));
        render ("gate_snare_ignore_below.png", slice (snare, 1.0, 4.0), [] (P& q) { setParam (q, GateIDs::threshold, -26.0f); setParam (q, GateIDs::range, 30.0f);
                                                                                   setParam (q, GateIDs::lowCut, 250.0f); setParam (q, GateIDs::release, 100.0f); }, hover (5));
        render ("gate_bypassed.png", slice (toms, 2.0, 4.0), [] (P& q) { setParam (q, CommonIDs::bypass, 1.0f); });
        render ("gate_listening.png", slice (snare, 1.0, 4.0), [] (P& q) { q.setListening (true); setParam (q, GateIDs::lowCut, 300.0f); setParam (q, GateIDs::threshold, -26.0f); },
                [] (juce::AudioProcessorEditor& ed) { if (auto* w = dynamic_cast<EasyGateAudioProcessorEditor*> (&ed)) ed.mouseEnter (hoverEvent (&w->listenButton)); });
        render ("gate_sidechain_missing.png", slice (toms, 2.0, 4.0), [] (P& q) { setParam (q, GateIDs::openedBy, 1.0f); });
        render ("gate_preset_toms.png", slice (toms, 2.0, 4.0), [] (P& q) { loadPresetNamed (q, "Toms - Natural"); });
        render ("gate_preset_snare_tight.png", slice (snare, 1.0, 4.0), [] (P& q) { loadPresetNamed (q, "Snare - Tight & Dry"); });
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[1])).getFullPathName()
                                      : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    testParameters();
    testGating();
    testKeyFilters();
    testSidechain();
    testListenAndBypass();
    testEfficiency();
    testDisplayAndState();
    testWindow();
    renders (outDir);
    return finish();
}
