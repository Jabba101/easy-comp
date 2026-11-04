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
            check (p.getLatencySamples() > 0 && p.getLatencySamples() < (int) (sr * 0.01), "tells your DAW its delay (the limiter's look-ahead): "
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
            P a; set (a, MasterIDs::compMode, 1.0f); set (a, MasterIDs::midRatio, 1.0f); set (a, MasterIDs::midMakeup, 0.0f); set (a, MasterIDs::limitGain, 0.0f);
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
        for (int i = 0; i < 4; ++i) window->updateDisplaysNow();

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
        check (fitsAndNoOverlaps(), "MID + SIDE: everything fits without overlapping");
        check (window->isKnobShowing (EasyMasterAudioProcessorEditor::midThresholdKnob) && window->isKnobShowing (EasyMasterAudioProcessorEditor::sideThresholdKnob)
               && window->isKnobUsable (EasyMasterAudioProcessorEditor::sideThresholdKnob), "MID and SIDE each have their own row of knobs, both showing and working");
        savePng (*editor, outDir.getChildFile ("master.png"));
        window->getInitAllButton().onClick();
        check (p.getPresetName() == "Default settings", "INIT: back to 'Default settings'");
        window->updateDisplaysNow();
        check (! window->isKnobUsable (EasyMasterAudioProcessorEditor::sideThresholdKnob) && window->isKnobUsable (EasyMasterAudioProcessorEditor::midThresholdKnob)
               && fitsAndNoOverlaps(), "STEREO: the top row does the whole mix, the SIDE row rests (dimmed)");
        savePng (*editor, outDir.getChildFile ("master_stereo.png"));
        bool hints = true;
        for (int k = 0; k < EasyMasterAudioProcessorEditor::numKnobs; ++k)
        {
            window->mouseEnter (hoverEvent (&window->getKnob (k)));
            if (window->hintBar.getText().length() < 40) hints = false;
        }
        check (hints, "every knob explains itself in the help bar");
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
    testParts (outDir);
    testSpeed();
    return finish();
}
