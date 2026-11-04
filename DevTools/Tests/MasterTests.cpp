// Easy Master, stage 1: the chain, the meters and the window (not shipped).
#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "MasterConsole.h"
#include "Parameters.h"
#include <juce_audio_formats/juce_audio_formats.h>

using namespace th;
using P = EasyMasterAudioProcessor;

namespace
{
    constexpr double sr = 44100.0;

    juce::AudioBuffer<float> loadMix (const juce::String& name, double fromSeconds, double seconds)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (juce::File ("/home/claude/presetcheck/mixes/" + name + ".wav").createInputStream().release(), true));
        if (r == nullptr) { std::cout << "  (couldn't read " << name << ")\n"; return juce::AudioBuffer<float> (2, (int) (sr * seconds)); }
        const int n = (int) juce::jmin ((juce::int64) (sr * seconds), r->lengthInSamples - (juce::int64) (sr * fromSeconds));
        juce::AudioBuffer<float> b (2, n);
        r->read (&b, 0, n, (juce::int64) (sr * fromSeconds), true, true);
        return b;
    }

    void set (P& p, const char* id, float v)   { setParam (p, id, v); }

    float worstDelayedDifference (const juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& in, int delay)
    {
        float worst = 0.0f;
        for (int c = 0; c < 2; ++c)
            for (int i = delay; i < out.getNumSamples(); ++i)
                worst = std::max (worst, std::abs (out.getSample (c, i) - in.getSample (c, i - delay)));
        return worst;
    }

    float biggestJump (const juce::AudioBuffer<float>& b, int from, int to)
    {
        float worst = 0.0f;
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = std::max (1, from); i < to; ++i)
                worst = std::max (worst, std::abs (b.getSample (c, i) - b.getSample (c, i - 1)));
        return worst;
    }

    //==========================================================================
    void testChain()
    {
        std::cout << "\n[the chain]\n";
        const auto band = loadMix ("band", 0.0, 20.0);
        {
            P p; prepare (p, sr, 512);
            check (p.getLatencySamples() > (int) (sr * 0.04) && p.getLatencySamples() < (int) (sr * 0.06), "tells your DAW its delay (the tamer's slice + the limiter's look-ahead): "
                   + juce::String (p.getLatencySamples()) + " samples, " + juce::String (p.getLatencySamples() / sr * 1000.0, 1) + " ms");
            set (p, CommonIDs::bypass, 1.0f);
            P q; set (q, CommonIDs::bypass, 1.0f); prepare (q, sr, 512);
            const auto out = run (q, band, 512);
            check (worstDelayedDifference (out, band, q.getLatencySamples()) == 0.0f, "BYPASS: exactly the original mix (delayed like the processed sound)");
        }
        {
            P p; prepare (p, sr, 512);
            juce::AudioBuffer<float> silence (2, (int) sr * 2); silence.clear();
            const auto out = run (p, silence, 512);
            check (allFinite (out) && peak (out, 0, 0, out.getNumSamples()) < 1.0e-6f, "silence in, silence out");
        }
        {
            P p; prepare (p, sr, 512);
            loadPresetNamed (p, "Master - Heavy & Loud (+7 dB)");
            set (p, MasterIDs::limitGain, 12.0f);
            const auto out = run (p, band, 512);
            const float tp = TruePeakLimiter::truePeakDb (out);
            check (allFinite (out) && tp <= -0.29f, "pushed 12 dB into the ceiling: the true peak stays at " + juce::String (tp, 2) + " dBTP (ceiling -0.3)");
            const auto r = p.getReadings();
            check (r.highestPeak <= -0.29f && r.highestPeak > -1.5f, "the HIGHEST TRUE PEAK meter agrees: " + juce::String (r.highestPeak, 2) + " dBTP");
        }
        {
            // Default: the compressor gently at work on a real mix
            P p; prepare (p, sr, 512);
            float most = 0.0f, total = 0.0f; int counted = 0;
            run (p, band, 512, [&] (int start) { if (start > (int) sr * 3) { const auto r = p.getReadings(); most = std::max (most, r.midCut); total += r.midCut; ++counted; } });
            check (most > 0.5f && most < 8.0f, "default settings on your band's mix: the compressor turns down up to " + juce::String (most, 1) + " dB (average "
                   + juce::String (total / std::max (1, counted), 1) + " dB) - glue, not squash");
        }
    }

    void testMidSide()
    {
        std::cout << "\n[mid / side]\n";
        auto band = loadMix ("band", 0.0, 10.0);
        // A mono mix (no sides): heavy SIDE settings must change nothing between left and right
        juce::AudioBuffer<float> mono (band);
        mono.copyFrom (1, 0, mono, 0, 0, mono.getNumSamples());
        {
            P p; set (p, MasterIDs::compMode, 1.0f); set (p, MasterIDs::sideThreshold, -40.0f); set (p, MasterIDs::sideRatio, 10.0f); set (p, MasterIDs::sideMakeup, 12.0f);
            prepare (p, sr, 512);
            const auto out = run (p, mono, 512);
            float worst = 0.0f;
            for (int i = 0; i < out.getNumSamples(); ++i) worst = std::max (worst, std::abs (out.getSample (0, i) - out.getSample (1, i)));
            check (worst < 1.0e-5f, "MID + SIDE on a mono mix: the side settings can't touch it (left = right within " + juce::String (toDb (worst), 0) + " dB)");
        }
        {
            // Squeezing the sides only: the middle's level hardly changes, the sides' does
            P a; set (a, MasterIDs::tamerOn, 0.0f); set (a, MasterIDs::compMode, 1.0f); set (a, MasterIDs::midRatio, 1.0f); set (a, MasterIDs::midMakeup, 0.0f); set (a, MasterIDs::limitGain, 0.0f);
            set (a, MasterIDs::sideThreshold, -40.0f); set (a, MasterIDs::sideRatio, 6.0f); set (a, MasterIDs::sideMakeup, 0.0f);
            prepare (a, sr, 512);
            const auto out = run (a, band, 512);
            const int d = a.getLatencySamples();
            double midIn = 0, midOut = 0, sideIn = 0, sideOut = 0;
            for (int i = (int) sr; i + d < out.getNumSamples(); ++i)
            {
                const double mi = 0.5 * (band.getSample (0, i) + band.getSample (1, i)), si = 0.5 * (band.getSample (0, i) - band.getSample (1, i));
                const double mo = 0.5 * (out.getSample (0, i + d) + out.getSample (1, i + d)), so = 0.5 * (out.getSample (0, i + d) - out.getSample (1, i + d));
                midIn += mi * mi; midOut += mo * mo; sideIn += si * si; sideOut += so * so;
            }
            const double midChange = 10.0 * std::log10 (midOut / midIn), sideChange = 10.0 * std::log10 (sideOut / sideIn);
            check (std::abs (midChange) < 0.5 && sideChange < -3.0, "squeezing only the SIDES: the middle " + juce::String (midChange, 2) + " dB, the sides "
                   + juce::String (sideChange, 1) + " dB");
        }
        {
            // Switching STEREO <-> MID + SIDE while playing: no click
            P p; prepare (p, sr, 512);
            const auto loud = sine (sr, 220.0, 0.4f, 2.0);
            const auto out = run (p, loud, 512, [&] (int start) { if (start == 512 * 80) set (p, MasterIDs::compMode, 1.0f); });
            const float normal = biggestJump (out, (int) (sr * 0.3), 512 * 70), during = biggestJump (out, 512 * 78, 512 * 100);
            check (during < normal * 1.6f, "switching to MID + SIDE while playing: no click (largest step " + juce::String (during, 3) + " vs " + juce::String (normal, 3) + " normally)");
        }
    }

    void testCompare()
    {
        std::cout << "\n[compare and listen]\n";
        const auto band = loadMix ("band", 0.0, 20.0);
        {
            P p; loadPresetNamed (p, "Master - Rock & Punk (+5 dB)"); set (p, MasterIDs::match, 1.0f); prepare (p, sr, 512);
            const auto out = run (p, band, 512);
            const int from = (int) (sr * 10), to = out.getNumSamples();
            const double before = loudnessDb (band, from, to), after = loudnessDb (out, from, to);
            check (std::abs (after - before) < 1.0, "MATCH: a +5 dB master plays as loud as the original (" + juce::String (after - before, 2) + " dB; MATCH turning down "
                   + juce::String (-p.getReadings().matchDb, 1) + " dB)");
        }
        {
            P p; set (p, MasterIDs::listen, (float) MasterListen::mono); prepare (p, sr, 512);
            const auto out = run (p, band, 512);
            float worst = 0.0f;
            for (int i = (int) sr; i < out.getNumSamples(); ++i) worst = std::max (worst, std::abs (out.getSample (0, i) - out.getSample (1, i)));
            check (worst < 1.0e-6f, "LISTEN MONO: the same in both speakers");
            P q; set (q, MasterIDs::listen, (float) MasterListen::side); prepare (q, sr, 512);
            juce::AudioBuffer<float> mono (band); mono.copyFrom (1, 0, mono, 0, 0, mono.getNumSamples());
            const auto sideOut = run (q, mono, 512);
            check (rms (sideOut, 0, (int) sr, sideOut.getNumSamples()) < 1.0e-5f, "LISTEN SIDE on a mono mix: silence (no width to hear)");
        }
        {
            P p; prepare (p, sr, 512);
            const auto out = run (p, band, 512);
            const auto r = p.getReadings();
            const double ref = loudnessDb (out, out.getNumSamples() - (int) (sr * 3), out.getNumSamples()) - 0.691;   // (LUFS = K-weighted level - 0.691)
            check (r.shortTerm > -40.0f && std::abs (r.shortTerm - (float) ref) < 0.5f, "LUFS SHORT-TERM reads " + juce::String (r.shortTerm, 1)
                   + " (the test's own measurement: " + juce::String (ref, 1) + ")");
            check (r.integrated > -40.0f && r.momentary > -60.0f, "MOMENTARY " + juce::String (r.momentary, 1) + ", INTEGRATED " + juce::String (r.integrated, 1) + " LUFS");
            check (r.correlation > 0.2f && r.correlation <= 1.0f, "stereo correlation of your mix: " + juce::String (r.correlation, 2));
        }
    }

    void testTamer()
    {
        std::cout << "\n[resonance tamer]\n";
        const auto band = loadMix ("band", 20.0, 12.0);
        const auto runP = [&] (std::function<void (P&)> setUp)
        {
            P p; set (p, MasterIDs::compOn, 0.0f); set (p, MasterIDs::limitGain, 0.0f); set (p, MasterIDs::ceiling, -0.1f); setUp (p);
            prepare (p, sr, 512);
            float most = 0.0f;
            auto out = run (p, band, 512, [&] (int start) { if (start > (int) sr * 2) most = std::max (most, p.getReadings().tamerMidCut); });
            return std::make_pair (out, most);
        };
        const auto off = runP ([] (P& p) { set (p, MasterIDs::tamerOn, 0.0f); });
        const auto on = runP ([] (P& p) { set (p, MasterIDs::tamerOn, 1.0f); set (p, MasterIDs::tMidAmount, 6.0f); });
        P q; prepare (q, sr, 512);
        const int d = q.getLatencySamples();
        // (off: exactly the mix, delayed - apart from the limiter catching the odd over at -0.1)
        double diff = 0, ref = 0;
        for (int c = 0; c < 2; ++c) for (int i = d; i < band.getNumSamples(); ++i) { const double x = off.first.getSample (c, i) - band.getSample (c, i - d); diff += x * x; ref += band.getSample (c, i - d) * band.getSample (c, i - d); }
        check (10.0 * std::log10 (diff / ref + 1e-30) < -40.0, "TAMER off: the mix passes untouched (the same delay either way, so nothing jumps when you switch it)");
        const double change = loudnessDb (on.first, (int) sr * 2, on.first.getNumSamples()) - loudnessDb (off.first, (int) sr * 2, off.first.getNumSamples());
        check (on.second > 1.0f && on.second <= 6.05f && change < 0.0 && change > -2.0,
               "TAMER on your band's mix: the deepest cut " + juce::String (on.second, 1) + " dB (AMOUNT 6), overall " + juce::String (change, 2) + " dB - spots, not the whole mix");
        // MID + SIDE: a mono mix has no sides - the SIDE settings can't touch it
        juce::AudioBuffer<float> mono (band); mono.copyFrom (1, 0, mono, 0, 0, mono.getNumSamples());
        P m; set (m, MasterIDs::tamerMode, 1.0f); set (m, MasterIDs::tSideAmount, 12.0f); set (m, MasterIDs::tSideSens, 100.0f); set (m, MasterIDs::compOn, 0.0f);
        prepare (m, sr, 512);
        const auto mo = run (m, mono, 512);
        float worst = 0.0f;
        for (int i = 0; i < mo.getNumSamples(); ++i) worst = std::max (worst, std::abs (mo.getSample (0, i) - mo.getSample (1, i)));
        check (worst < 1.0e-4f, "TAMER MID + SIDE on a mono mix: left = right (the sides' settings have nothing to work on)");
        // LISTEN: only what it takes out
        P l; set (l, MasterIDs::tListen, 1.0f); set (l, MasterIDs::tMidAmount, 6.0f); set (l, MasterIDs::compOn, 0.0f); set (l, MasterIDs::limitGain, 0.0f);
        prepare (l, sr, 512);
        const auto lo = run (l, band, 512);
        const double removed = loudnessDb (lo, (int) sr * 2, lo.getNumSamples()), full = loudnessDb (band, (int) sr * 2, band.getNumSamples());
        check (removed < full - 12.0 && removed > full - 60.0, "TAMER LISTEN: just what it takes out (" + juce::String (removed - full, 1) + " dB below the mix)");
    }

    juce::MouseEvent mouseAt (juce::Component* c, juce::Point<float> at, bool down)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (source, at, down ? juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier) : juce::ModifierKeys(),
                                 juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                 juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY, c, c, now, at, now, 1, false);
    }

    void testEQ()
    {
        std::cout << "\n[EQ]\n";
        const auto band = loadMix ("band", 20.0, 6.0);
        const auto quiet = [] (P& p) { set (p, MasterIDs::tamerOn, 0.0f); set (p, MasterIDs::compOn, 0.0f); set (p, MasterIDs::limitGain, 0.0f); set (p, MasterIDs::ceiling, -0.1f); };
        {
            // A +6 dB MID bell at 1 kHz on a quiet 1 kHz tone: +6 dB; an untouched band elsewhere changes nothing
            P p; quiet (p); set (p, MasterEQIDs::gain[0][2], 6.0f); set (p, MasterEQIDs::freq[0][2], 1000.0f); prepare (p, sr, 512);
            const auto tone = sine (sr, 1000.0, 0.05f, 2.0);
            const auto out = run (p, tone, 512);
            const float change = toDb (rms (out, 0, (int) sr, out.getNumSamples()) / rms (tone, 0, (int) sr, tone.getNumSamples()));
            check (std::abs (change - 6.0f) < 0.2f, "a +6 dB MID band at 1 kHz lifts a 1 kHz tone by " + juce::String (change, 2) + " dB");
        }
        {
            // Gentle, passive-style curves: a bell keeps the same broad width however far it goes (no narrowing)
            const auto widthAt = [] (float gainDb)
            {
                MasteringEQ::Part part; part.gainDb = { 0.0f, 0.0f, gainDb, 0.0f, 0.0f }; part.freqHz[2] = 1000.0f;
                const double peak = juce::Decibels::gainToDecibels (MasteringEQ::responseAt (part, 1000.0, sr));
                return juce::Decibels::gainToDecibels (MasteringEQ::responseAt (part, 500.0, sr)) / peak;   // (how much of it is left an octave away)
            };
            const double small = widthAt (2.0f), big = widthAt (12.0f);
            check (small > 0.55 && big > small - 0.12, "the bells stay broad like a passive EQ: an octave away, " + juce::String (juce::roundToInt (small * 100.0)) + "% of the boost at +2 dB, "
                   + juce::String (juce::roundToInt (big * 100.0)) + "% at +12 dB (no narrowing as you push)");
        }
        {
            // MID + SIDE: a low cut on the sides makes the low end mono; on a mono mix it can't do anything
            juce::AudioBuffer<float> mono (band); mono.copyFrom (1, 0, mono, 0, 0, mono.getNumSamples());
            P p; quiet (p); set (p, MasterEQIDs::mode, 1.0f); set (p, MasterEQIDs::lowCut[1], 200.0f); set (p, MasterEQIDs::gain[1][4], 6.0f); prepare (p, sr, 512);
            const auto out = run (p, mono, 512);
            float worst = 0.0f;
            for (int i = 0; i < out.getNumSamples(); ++i) worst = std::max (worst, std::abs (out.getSample (0, i) - out.getSample (1, i)));
            check (worst < 1.0e-5f, "EQ MID + SIDE: the sides' EQ leaves a mono mix alone (left = right)");
            // ... and in STEREO the side row does nothing at all
            P q; quiet (q); set (q, MasterEQIDs::gain[1][2], 12.0f); prepare (q, sr, 512);
            P r; quiet (r); prepare (r, sr, 512);
            check (maxDifference (run (q, band, 512), run (r, band, 512)) < 1.0e-6f, "EQ in STEREO: the SIDE row is resting (no effect)");
        }
        {
            // Flat EQ on: exactly the same as EQ off
            P a; quiet (a); prepare (a, sr, 512);
            P b; quiet (b); set (b, MasterEQIDs::on, 0.0f); prepare (b, sr, 512);
            check (maxDifference (run (a, band, 512), run (b, band, 512)) < 1.0e-6f, "a flat EQ changes nothing (exactly the same as switched off)");
            // Moving a gain while playing: no clicks
            P c; quiet (c); prepare (c, sr, 256);
            const auto tone = sine (sr, 300.0, 0.3f, 2.0);
            const auto out = run (c, tone, 256, [&] (int start) { if (start == 256 * 100) set (c, MasterEQIDs::gain[0][1], 12.0f); });
            const float normal = biggestJump (out, 256 * 20, 256 * 90), during = biggestJump (out, 256 * 99, 256 * 130);
            check (during < normal * 4.5f, "turning a band up 12 dB while playing glides, no click (" + juce::String (during, 3) + " vs " + juce::String (normal, 3) + ")");
        }
    }

    void testMultiband()
    {
        std::cout << "\n[multiband]\n";
        const auto quiet = [] (P& p) { set (p, MasterIDs::tamerOn, 0.0f); set (p, MasterIDs::compOn, 0.0f); set (p, MasterIDs::limitGain, 0.0f); set (p, MasterIDs::ceiling, -0.1f);
                                       set (p, MasterMBIDs::on, 1.0f); };
        const auto levelChange = [&] (std::function<void (P&)> setUp, double hz)
        {
            P p; quiet (p); setUp (p); prepare (p, sr, 512);
            const auto tone = sine (sr, hz, 0.1f, 1.5);
            const auto out = run (p, tone, 512);
            return toDb (rms (out, 0, (int) (sr * 0.7), out.getNumSamples()) / rms (tone, 0, (int) (sr * 0.7), tone.getNumSamples()));
        };
        float worst = 0.0f;
        for (double hz : { 50.0, 120.0, 400.0, 1000.0, 3000.0, 6000.0, 12000.0 })
            worst = std::max (worst, std::abs (levelChange ([] (P&) {}, hz)));
        check (worst < 0.05f, "the four bands add back up to the original: within " + juce::String (worst, 3) + " dB from 50 Hz to 12 kHz (nothing compressing)");
        const float lowUp = levelChange ([] (P& p) { set (p, MasterMBIDs::gain[0][0], 6.0f); }, 50.0), topSame = levelChange ([] (P& p) { set (p, MasterMBIDs::gain[0][0], 6.0f); }, 5000.0);
        check (std::abs (lowUp - 6.0f) < 0.3f && std::abs (topSame) < 0.1f, "LOW band GAIN +6: a 50 Hz tone " + juce::String (lowUp, 2) + " dB, a 5 kHz tone " + juce::String (topSame, 2) + " dB");
        const float squeezed = levelChange ([] (P& p) { set (p, MasterMBIDs::threshold[0][3], -40.0f); set (p, MasterMBIDs::ratio[0][3], 10.0f); }, 10000.0);
        const float untouched = levelChange ([] (P& p) { set (p, MasterMBIDs::threshold[0][3], -40.0f); set (p, MasterMBIDs::ratio[0][3], 10.0f); }, 200.0);
        check (squeezed < -6.0f && std::abs (untouched) < 0.1f, "the HIGH band compressed hard: 10 kHz " + juce::String (squeezed, 1) + " dB, while 200 Hz is untouched (" + juce::String (untouched, 2) + " dB)");
        const float soloed = levelChange ([] (P& p) { set (p, MasterMBIDs::solo[0], 1.0f); }, 5000.0);
        check (soloed < -40.0f, "SOLO on the LOW band: a 5 kHz tone disappears (" + juce::String (soloed, 0) + " dB)");
        {
            juce::AudioBuffer<float> mono (loadMix ("band", 20.0, 4.0)); mono.copyFrom (1, 0, mono, 0, 0, mono.getNumSamples());
            P p; quiet (p); set (p, MasterMBIDs::mode, 1.0f); set (p, MasterMBIDs::threshold[1][0], -40.0f); set (p, MasterMBIDs::ratio[1][0], 10.0f); set (p, MasterMBIDs::gain[1][3], 6.0f);
            prepare (p, sr, 512);
            const auto out = run (p, mono, 512);
            float diff = 0.0f;
            for (int i = 0; i < out.getNumSamples(); ++i) diff = std::max (diff, std::abs (out.getSample (0, i) - out.getSample (1, i)));
            check (diff < 1.0e-5f, "MULTIBAND MID + SIDE on a mono mix: the sides' settings leave it alone (left = right)");
        }
        {
            P a; quiet (a); set (a, MasterMBIDs::on, 0.0f); prepare (a, sr, 512);
            P b; quiet (b); set (b, MasterMBIDs::on, 0.0f); set (b, MasterIDs::tamerOn, 0.0f); prepare (b, sr, 512);
            const auto mix = loadMix ("band", 20.0, 3.0);
            check (maxDifference (run (a, mix, 512), run (b, mix, 512)) < 1.0e-6f, "MULTIBAND off (the default): the mix passes untouched");
        }
    }

    void testPunchWidth()
    {
        std::cout << "\n[punch and width]\n";
        const auto quiet = [] (P& p) { set (p, MasterIDs::tamerOn, 0.0f); set (p, MasterIDs::compOn, 0.0f); set (p, MasterIDs::limitGain, 0.0f); set (p, MasterIDs::ceiling, 0.0f);
                                       set (p, MasterIDs::trim, -12.0f); };   // (well under the ceiling: the limiter never acts)
        const auto band = loadMix ("band", 20.0, 8.0);
        const auto runWith = [&] (std::function<void (P&)> setUp, const juce::AudioBuffer<float>& in)
        {
            P p; quiet (p); setUp (p); prepare (p, sr, 512);
            return run (p, in, 512);
        };
        const auto plain = runWith ([] (P&) {}, band);
        {
            const auto zero = runWith ([] (P& p) { set (p, MasterPunchIDs::on, 1.0f); set (p, MasterWidthIDs::on, 1.0f); }, band);
            check (maxDifference (plain, zero) < 1.0e-6f, "PUNCH and WIDTH on at their starting settings (0%, 100%, no mono bass): the mix passes untouched");
        }
        const auto crest = [&] (const juce::AudioBuffer<float>& b)
        {
            // (how far the hits stand above the music: the loudest 2% of 5 ms moments against the typical moment)
            const int w = (int) (sr * 0.005);
            std::vector<float> levels;
            for (int i = (int) sr; i + w < b.getNumSamples(); i += w)
                levels.push_back (toDb (juce::jmax (rms (b, 0, i, i + w), rms (b, 1, i, i + w)) + 1.0e-9f));
            std::sort (levels.begin(), levels.end());
            return (double) (levels[(size_t) (levels.size() * 98 / 100)] - levels[levels.size() / 2]);
        };
        {
            const auto more = runWith ([] (P& p) { set (p, MasterPunchIDs::on, 1.0f); set (p, MasterPunchIDs::punch[0], 50.0f); }, band);
            const auto less = runWith ([] (P& p) { set (p, MasterPunchIDs::on, 1.0f); set (p, MasterPunchIDs::punch[0], -50.0f); }, band);
            const double c0 = crest (plain), cUp = crest (more);
            // (at the hits - the moments +50% lifts most - -50% should soften by about as much)
            const int w = (int) (sr * 0.005);
            std::vector<std::pair<float, float>> moments;
            for (int i = (int) sr; i + w < plain.getNumSamples(); i += w)
            {
                const float base = rms (plain, 0, i, i + w) + 1.0e-9f;
                moments.push_back ({ toDb (rms (more, 0, i, i + w) / base), toDb (rms (less, 0, i, i + w) / base) });
            }
            std::sort (moments.begin(), moments.end(), [] (auto& x, auto& y) { return x.first > y.first; });
            double up = 0.0, down = 0.0;
            const size_t top = moments.size() / 50;
            for (size_t i = 0; i < top; ++i) { up += moments[i].first; down += moments[i].second; }
            up /= (double) top; down /= (double) top;
            check (cUp > c0 + 0.3 && up > 1.0 && down < -1.0, "PUNCH on your band's mix: +50% lifts the hits " + juce::String (up, 1) + " dB (they stand "
                   + juce::String (cUp - c0, 2) + " dB further above the music), -50% softens them " + juce::String (down, 1) + " dB");
        }
        {
            juce::AudioBuffer<float> mono (band); mono.copyFrom (1, 0, mono, 0, 0, mono.getNumSamples());
            const auto out = runWith ([] (P& p) { set (p, MasterPunchIDs::on, 1.0f); set (p, MasterPunchIDs::mode, 1.0f);
                                                  set (p, MasterPunchIDs::punch[1], 100.0f); set (p, MasterPunchIDs::sustain[1], 100.0f); }, mono);
            float diff = 0.0f;
            for (int i = 0; i < out.getNumSamples(); ++i) diff = std::max (diff, std::abs (out.getSample (0, i) - out.getSample (1, i)));
            check (diff < 1.0e-5f, "PUNCH MID + SIDE on a mono mix: the sides' settings leave it alone (left = right)");
        }
        {
            P p; quiet (p); set (p, MasterPunchIDs::on, 1.0f); set (p, MasterPunchIDs::punch[0], 30.0f); prepare (p, sr, 512);
            const auto tone = sine (sr, 220.0, 0.1f, 2.0);
            const float normal = biggestJump (run (p, tone, 512), (int) sr / 2, (int) sr);
            const auto out = run (p, tone, 512, [&] (int start) { if (start >= (int) sr / 2 && start < (int) sr / 2 + 512) set (p, MasterPunchIDs::mode, 1.0f); });
            const float during = biggestJump (out, (int) sr / 2, (int) sr);
            check (during < normal * 1.6f, "switching PUNCH to MID + SIDE while playing: no click (" + juce::String (during, 3) + " vs " + juce::String (normal, 3) + ")");
        }
        const auto sideToMid = [] (const juce::AudioBuffer<float>& b)
        {
            double mm = 0.0, ss = 0.0;
            for (int i = (int) sr; i < b.getNumSamples(); ++i)
            {
                const double l = b.getSample (0, i), r = b.getSample (1, i);
                mm += 0.25 * (l + r) * (l + r); ss += 0.25 * (l - r) * (l - r);
            }
            return 10.0 * std::log10 (ss / mm + 1.0e-30);
        };
        {
            const auto wide = runWith ([] (P& p) { set (p, MasterWidthIDs::on, 1.0f); set (p, MasterWidthIDs::width, 150.0f); }, band);
            const double change = sideToMid (wide) - sideToMid (plain);
            double midDiff = 0.0;
            for (int i = 0; i < wide.getNumSamples(); ++i)
                midDiff = std::max (midDiff, (double) std::abs ((wide.getSample (0, i) + wide.getSample (1, i)) - (plain.getSample (0, i) + plain.getSample (1, i))));
            check (std::abs (change - 3.52) < 0.1 && midDiff < 1.0e-5, "WIDTH 150%: the sides " + juce::String (change, 2) + " dB louder (should be +3.52), the middle untouched");
            const auto mono = runWith ([] (P& p) { set (p, MasterWidthIDs::on, 1.0f); set (p, MasterWidthIDs::width, 0.0f); }, band);
            float diff = 0.0f;
            for (int i = (int) sr; i < mono.getNumSamples(); ++i) diff = std::max (diff, std::abs (mono.getSample (0, i) - mono.getSample (1, i)));
            check (diff < 1.0e-5f, "WIDTH 0%: mono (left = right)");
        }
        {
            // MONO BASS 120 Hz on pure width (left = -right): the lows gone from the sides, the highs kept
            const auto sideTone = [&] (double hz) { auto b = sine (sr, hz, 0.1f, 2.0); b.applyGain (1, 0, b.getNumSamples(), -1.0f); return b; };
            const auto change = [&] (double hz)
            {
                const auto in = sideTone (hz);
                const auto out = runWith ([] (P& p) { set (p, MasterWidthIDs::on, 1.0f); set (p, MasterWidthIDs::monoBass, 120.0f); }, in);
                return toDb (rms (out, 0, (int) sr, out.getNumSamples()) / rms (in, 0, (int) sr, in.getNumSamples())) + 12.0f;   // (less the -12 dB TRIM)
            };
            const float low = change (40.0), high = change (2000.0);
            check (low < -30.0f && std::abs (high) < 0.1f, "MONO BASS 120 Hz: a 40 Hz sound at the sides " + juce::String (low, 0) + " dB, at 2 kHz "
                   + juce::String (high, 2) + " dB (the kick and bass dead centre)");
        }
        {
            P p; set (p, MasterPunchIDs::on, 1.0f); set (p, MasterWidthIDs::on, 1.0f); set (p, MasterWidthIDs::width, 140.0f);
            loadPresetNamed (p, "Master - Heavy Low End");
            check (getParam (p, MasterPunchIDs::on) < 0.5f && getParam (p, MasterWidthIDs::on) < 0.5f && std::abs (getParam (p, MasterWidthIDs::width) - 100.0f) < 0.01f,
                   "a main preset puts PUNCH and WIDTH back off (the main presets sound exactly as before)");
            juce::MemoryBlock state;
            set (p, MasterPunchIDs::sustain[1], -25.0f); set (p, MasterWidthIDs::monoBass, 110.0f);
            p.getStateInformation (state);
            P r; r.setStateInformation (state.getData(), (int) state.getSize());
            check (std::abs (getParam (r, MasterPunchIDs::sustain[1]) + 25.0f) < 0.01f && std::abs (getParam (r, MasterWidthIDs::monoBass) - 110.0f) < 0.5f,
                   "PUNCH and WIDTH are saved with the project");
        }
    }

    void testParts (const juce::File& outDir)
    {
        std::cout << "\n[presets, saving, A / B, window]\n";
        {
            P p; juce::MemoryBlock state;
            set (p, MasterIDs::sideRatio, 4.0f); set (p, MasterIDs::compMode, 1.0f);
            p.getStateInformation (state);
            P r; r.setStateInformation (state.getData(), (int) state.getSize());
            check (getParam (r, MasterIDs::sideRatio) == 4.0f && getParam (r, MasterIDs::compMode) == 1.0f, "saved with the project and loaded back");
            const auto report = presetTextsReport (p.getBuiltInPresets());
            check (report.first, report.second);
            P ab; set (ab, MasterIDs::limitGain, 3.0f); ab.switchToSlot (1); set (ab, MasterIDs::limitGain, 7.0f); ab.switchToSlot (0);
            const float a = getParam (ab, MasterIDs::limitGain); ab.switchToSlot (1);
            check (a == 3.0f && getParam (ab, MasterIDs::limitGain) == 7.0f, "A / B keep two sets of settings");
        }

        P p; prepare (p, sr, 512);
        loadPresetNamed (p, "Master - Tighten & Brighten (M/S)");
        run (p, loadMix ("mureka", 40.0, 6.0), 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* window = dynamic_cast<EasyMasterAudioProcessorEditor*> (editor.get());
        check (window != nullptr && editor->getWidth() == 1620 && editor->getHeight() == 927, "the window opens at 1620 x 927 (fits a 1080p screen; drag the corner for any size)");
        if (window == nullptr) return;
        auto& console = window->getConsole();
        using C = MasterHolo::Console;
        check (console.getWidth() == C::width && console.getHeight() == C::height, "the console is laid out at 1800 x 1030 (like Easy Strip) and scaled to the window");

        // Nothing overlapping or spilling out: the console's parts, and each module's parts inside it
        const auto tidy = [&] (juce::String& problem)
        {
            const auto checkInside = [&] (juce::Component& parent, std::function<bool (juce::Component*, juce::Component*)> allowed)
            {
                std::vector<juce::Component*> seen;
                for (auto* child : parent.getChildren())
                {
                    if (! child->isVisible() || child->getWidth() == 0) continue;
                    if (! parent.getLocalBounds().contains (child->getBounds())) { problem = child->getName() + " spills out of " + parent.getName(); return false; }
                    for (auto* other : seen)
                        if (other->getBounds().intersects (child->getBounds()) && ! allowed (other, child))
                        { problem = "overlap in " + juce::String (typeid (parent).name()) + " at " + child->getBounds().toString() + " / " + other->getBounds().toString(); return false; }
                    seen.push_back (child);
                }
                return true;
            };
            const auto isTab = [&] (juce::Component* c) { return c == console.bayTabs[0].get() || c == console.bayTabs[1].get(); };
            const auto isBay = [&] (juce::Component* c) { return c == console.getModule (MasterModules::eq) || c == console.getModule (MasterModules::multiband); };
            if (! checkInside (console, [&] (juce::Component* a, juce::Component* b2) { return (isTab (a) && isBay (b2)) || (isTab (b2) && isBay (a)); })) return false;
            for (auto& m : console.getModules())
                if (m->isVisible() && ! checkInside (*m, [] (juce::Component*, juce::Component*) { return false; })) return false;
            return true;
        };
        const char* names[C::numFocus] { "tamer", "eq", "multiband", "comp", "punch", "width" };
        for (int f = 0; f < C::numFocus; ++f)
        {
            console.showFocus (f);
            if (f == C::eqFocus) console.getModuleBrowser (MasterModules::eq).onPresetChosen (MasterModules::presets (MasterModules::eq)[2]);
            if (f == C::mbFocus) console.getModuleBrowser (MasterModules::multiband).onPresetChosen (MasterModules::presets (MasterModules::multiband)[4]);
            if (f == C::punchFocus) console.getModuleBrowser (MasterModules::punch).onPresetChosen (MasterModules::presets (MasterModules::punch)[1]);
            if (f == C::widthFocus) console.getModuleBrowser (MasterModules::width).onPresetChosen (MasterModules::presets (MasterModules::width)[3]);
            for (int i = 0; i < 12; ++i) { run (p, loadMix ("band", 30.0 + i * 0.2, 0.2), 512); console.updateNow(); }
            juce::String problem;
            const bool ok = tidy (problem);
            check (ok && console.getScreenComponent (f)->isVisible(), juce::String ("the console with the screen on ") + names[f] + ": nothing overlapping" + (ok ? "" : " (" + problem + ")"));
            savePng (console, outDir.getChildFile (juce::String ("master_") + names[f] + ".png"));
        }
        check (console.getBay() == 1 && console.getModule (MasterModules::multiband)->isVisible() && ! console.getModule (MasterModules::eq)->isVisible(),
               "the screen on MULTIBAND brings the MULTIBAND into the big bay");
        console.bayTabs[0]->onClick();
        check (console.getFocus() == C::eqFocus && console.getBay() == 0, "... and the bay's EQ tab brings back the EQ, on the screen too");
        {
            // Dragging a dot on the screen: the band follows; double-click = 0 dB
            auto& eqView = console.eqDisplay;
            console.showFocus (C::eqFocus);
            console.updateNow();
            const auto start = eqView.nodePosition ({ 0, 2 });
            const auto target = start + juce::Point<float> (60.0f, -40.0f);
            eqView.mouseDown (mouseAt (&eqView, start, true));
            eqView.mouseDrag (mouseAt (&eqView, target, true));
            eqView.mouseUp (mouseAt (&eqView, target, false));
            const float freq = getParam (p, MasterEQIDs::freq[0][2]), gain = getParam (p, MasterEQIDs::gain[0][2]);
            check (freq > 1100.0f && gain > 0.5f, "dragging the MID dot on the screen moves the band: now " + juce::String (freq, 0) + " Hz, " + Format::signedDecibels (gain));
            console.updateNow();
            eqView.mouseDoubleClick (mouseAt (&eqView, eqView.nodePosition ({ 0, 2 }), false));
            check (std::abs (getParam (p, MasterEQIDs::gain[0][2])) < 0.01f, "double-clicking the dot puts it back to 0 dB");
            auto* ring = console.findControl (MasterEQIDs::freq[0][2]);
            check (ring != nullptr && std::abs (ring->getValue() - freq) < 1.0f, "... and the band's FREQ ring below follows it");
        }
        {
            console.showFocus (C::mbFocus);
            console.updateNow();
            auto& view = console.mbDisplay;
            const float x = view.splitX (1);
            view.mouseDown (mouseAt (&view, { x, 60.0f }, true));
            view.mouseDrag (mouseAt (&view, { x + 50.0f, 60.0f }, true));
            view.mouseUp (mouseAt (&view, { x + 50.0f, 60.0f }, false));
            check (getParam (p, MasterMBIDs::split[1]) > 1200.0f, "dragging the line between LOW MID and HIGH MID moves the split: " + Format::hertz (getParam (p, MasterMBIDs::split[1])));
        }
        {
            std::unique_ptr<juce::AudioProcessorEditor> again (p.createEditor());
            auto& c2 = dynamic_cast<EasyMasterAudioProcessorEditor*> (again.get())->getConsole();
            check (c2.getFocus() == C::mbFocus && c2.getBay() == 1, "reopening the window comes back to the same screen and bay");
        }
        {
            // Each module's own Pr and INIT: only that module changes
            loadPresetNamed (p, "Master - Tighten & Brighten (M/S)");
            const float eqBefore = getParam (p, MasterEQIDs::gain[1][4]), tamerBefore = getParam (p, MasterIDs::tMidAmount), gainBefore = getParam (p, MasterIDs::limitGain);
            console.getModuleBrowser (MasterModules::comp).onPresetChosen (MasterModules::presets (MasterModules::comp)[2]);   // "Rock & Punk"
            const bool compChanged = std::abs (getParam (p, MasterIDs::midRatio) - 3.0f) < 0.01f && getParam (p, MasterIDs::compMode) < 0.5f;
            check (compChanged && getParam (p, MasterEQIDs::gain[1][4]) == eqBefore && getParam (p, MasterIDs::tMidAmount) == tamerBefore
                   && getParam (p, MasterIDs::limitGain) == gainBefore, "the COMPRESSOR's own Pr: 'Rock & Punk' loads into the compressor only");
            check (p.isPresetEdited() && console.getPresetText().endsWith ("*"), "... and the preset window shows it's been edited (*)");
            console.getModule (MasterModules::eq)->init.onClick();
            check (std::abs (getParam (p, MasterEQIDs::gain[1][4])) < 0.01f && getParam (p, MasterEQIDs::lowCut[1]) < 20.5f
                   && std::abs (getParam (p, MasterIDs::midRatio) - 3.0f) < 0.01f, "the EQ's own INIT: the EQ flat again, the compressor untouched");
            bool allFit = true;
            for (int m = 0; m < MasterModules::count; ++m)
            {
                const auto list = MasterModules::presets (m);
                for (auto& pr : list) for (auto& [id, v] : pr.values) if (! MasterModules::ids (m).contains (id)) allFit = false;
                if (list.size() < 5) allFit = false;
                auto* mod = console.getModule (m);
                if (mod == nullptr || ! mod->init.isVisible() || ! mod->pr.isVisible()) allFit = false;
            }
            check (allFit, "every module has its own Pr and INIT and its own presets (5+ each), each touching only that module's settings");
        }
        // Every setting has a control on the console, and each explains itself on the ticker
        {
            bool all = true, hints = true;
            juce::String missing;
            for (auto& id : masterPresetIDs())
            {
                auto* c = console.findControl (id);
                const bool isSwitch = id == MasterIDs::compOn || id == MasterIDs::compMode || id == MasterIDs::tamerOn || id == MasterIDs::tamerMode
                                      || id == MasterIDs::reactsTo || id == MasterIDs::character || id == MasterIDs::releaseMode
                                      || id == MasterEQIDs::on || id == MasterEQIDs::mode || id == MasterMBIDs::on || id == MasterMBIDs::mode
                                      || id == MasterPunchIDs::on || id == MasterPunchIDs::mode || id == MasterWidthIDs::on || id.startsWith ("mb") && id.endsWith ("on");
                if (isSwitch) continue;
                if (c == nullptr) { all = false; missing = id; continue; }
                console.showBay (juce::String (id).startsWith ("mb") ? 1 : 0);
                console.mouseEnter (hoverEvent (c));
                if (console.getTickerText().length() < 30) { hints = false; missing = id; }
            }
            check (all, "every knob and fader of the chain is on the console" + (all ? juce::String() : " (missing " + missing + ")"));
            check (hints, "each one explains itself on the ticker" + (hints ? juce::String() : " (" + missing + ")"));
        }
        console.initAll.onClick();
        for (auto& m : console.getModules()) { m->init.flash = false; m->init.repaint(); }   // (INIT only flashes for a moment when pressed)
        console.updateNow();
        check (p.getPresetName() == "Default settings", "INIT: back to 'Default settings'");
        check (! console.findControl (MasterIDs::sideThreshold)->isEnabled() && console.findControl (MasterIDs::midThreshold)->isEnabled(),
               "STEREO: the MID controls do the whole mix, the SIDE ones rest (dimmed)");
        console.showFocus (C::compFocus);
        for (int i = 0; i < 8; ++i) { run (p, loadMix ("band", 34.0 + i * 0.2, 0.2), 512); console.updateNow(); }
        savePng (console, outDir.getChildFile ("master_stereo.png"));
    }

    void testSpeed()
    {
       #if ! JUCE_DEBUG
        std::cout << "\n[computer power]\n";
        P p; prepare (p, 48000.0, 256);
        loadPresetNamed (p, "Master - Darksynth Dense");
        const auto song = loadMix ("mureka", 40.0, 20.0);
        check (true, "tamer + EQ + compressor (MID + SIDE) + limiter + meters: " + juce::String (coreShare (p, song), 2) + "% of one core");
        P q; prepare (q, 48000.0, 256);
        loadPresetNamed (q, "Master - Darksynth Dense");
        set (q, MasterMBIDs::on, 1.0f); set (q, MasterMBIDs::mode, 1.0f);
        set (q, MasterPunchIDs::on, 1.0f); set (q, MasterPunchIDs::mode, 1.0f); set (q, MasterPunchIDs::punch[0], 20.0f); set (q, MasterPunchIDs::sustain[1], 10.0f);
        set (q, MasterWidthIDs::on, 1.0f); set (q, MasterWidthIDs::width, 120.0f); set (q, MasterWidthIDs::monoBass, 120.0f);
        const double all = coreShare (q, song);
        check (all < 15.0, "everything on (multiband, PUNCH in MID + SIDE, WIDTH): " + juce::String (all, 2) + "% of one core");
       #endif
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    testChain();
    testMidSide();
    testCompare();
    testTamer();
    testEQ();
    testMultiband();
    testPunchWidth();
    testParts (outDir);
    testSpeed();
    return finish();
}
