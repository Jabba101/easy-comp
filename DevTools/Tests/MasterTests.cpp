// Easy Master, stage 1: the chain, the meters and the window (not shipped).
#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
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
        loadPresetNamed (p, "Master - Synthwave Smooth & Wide");
        const auto song = loadMix ("mureka", 40.0, 14.0);
        run (p, song, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* window = dynamic_cast<EasyMasterAudioProcessorEditor*> (editor.get());
        check (window != nullptr && editor->getWidth() == 1680 && editor->getHeight() == 900, "window is 1680 x 900 (fits a 1080p screen)");
        if (window == nullptr) return;

        const auto fitsAndNoOverlaps = [&]
        {
            std::vector<juce::Rectangle<int>> boxes;
            for (auto* child : editor->getChildren())
            {
                if (! child->isVisible()) continue;
                if (! editor->getLocalBounds().contains (child->getBounds())) return false;
                for (auto& other : boxes) if (other.intersects (child->getBounds())) return false;
                boxes.push_back (child->getBounds());
            }
            return true;
        };
        using E = EasyMasterAudioProcessorEditor;
        window->showPage (E::tamerPage);
        for (int i = 0; i < 4; ++i) { run (p, loadMix ("mureka", 54.0, 0.2), 512); window->updateDisplaysNow(); }
        check (window->isKnobShowing (E::tMidAmountKnob) && window->isKnobShowing (E::tSideAmountKnob) && ! window->isKnobShowing (E::midThresholdKnob)
               && window->isKnobShowing (E::gainKnob) && fitsAndNoOverlaps(), "the RESONANCE TAMER page: its MID and SIDE rows, the LIMITER still there, nothing overlapping");
        savePng (*editor, outDir.getChildFile ("master_tamer.png"));
        window->showPage (E::eqPage);
        loadPresetNamed (p, "Master - Tighten & Brighten (M/S)");
        for (int i = 0; i < 4; ++i) { run (p, loadMix ("mureka", 54.0, 0.2), 512); window->updateDisplaysNow(); }
        check (window->getEQKnob (0, 0).isVisible() && window->getEQKnob (1, 10).isVisible() && window->getEQKnob (1, 10).isEnabled()
               && ! window->isKnobShowing (E::tMidAmountKnob) && fitsAndNoOverlaps(), "the EQ page: a full EQ for MID and for SIDE, nothing overlapping");
        savePng (*editor, outDir.getChildFile ("master_eq.png"));
        loadPresetNamed (p, "Master - Synthwave Smooth & Wide");
        window->showPage (E::compPage);
        window->updateDisplaysNow();
        check (window->isKnobShowing (E::midThresholdKnob) && window->isKnobShowing (E::sideThresholdKnob) && ! window->isKnobShowing (E::tMidAmountKnob)
               && window->isKnobUsable (E::sideThresholdKnob) && fitsAndNoOverlaps(), "the COMPRESSOR page: MID and SIDE rows, nothing overlapping");
        savePng (*editor, outDir.getChildFile ("master.png"));
        {
            std::unique_ptr<juce::AudioProcessorEditor> again (p.createEditor());
            check (dynamic_cast<E*> (again.get())->getShownPage() == E::compPage, "reopening the window comes back to the same page");
        }

        window->getInitAllButton().onClick();
        check (p.getPresetName() == "Default settings", "INIT: back to 'Default settings'");
        window->updateDisplaysNow();
        check (! window->isKnobUsable (E::sideThresholdKnob) && window->isKnobUsable (E::midThresholdKnob) && fitsAndNoOverlaps(),
               "STEREO: the top row does the whole mix, the SIDE row rests (dimmed)");
        savePng (*editor, outDir.getChildFile ("master_stereo.png"));
        bool hints = true;
        for (int page = 0; page < E::numPages; ++page)
        {
            window->showPage (page);
            for (int k = 0; k < E::numKnobs; ++k)
            {
                if (! window->isKnobShowing (k)) continue;
                window->mouseEnter (hoverEvent (&window->getKnob (k)));
                if (window->hintBar.getText().length() < 40) hints = false;
            }
        }
        check (hints, "every knob on both pages explains itself in the help bar");
    }

    void testSpeed()
    {
       #if ! JUCE_DEBUG
        std::cout << "\n[computer power]\n";
        P p; prepare (p, 48000.0, 256);
        loadPresetNamed (p, "Master - Darksynth Dense");
        const auto song = loadMix ("mureka", 40.0, 20.0);
        check (true, "MID + SIDE compressor + limiter + meters: " + juce::String (coreShare (p, song), 2) + "% of one core");
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
    testParts (outDir);
    testSpeed();
    return finish();
}
