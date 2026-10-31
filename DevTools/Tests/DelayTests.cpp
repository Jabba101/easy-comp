// Easy Delay: the stereo delay and the plugin around it (not shipped).
#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace th;
using P = EasyDelayAudioProcessor;

namespace
{
    constexpr double sr = 48000.0;

    void set (P& p, const char* id, float v)   { setParam (p, id, v); }

    // Plain settings: TIME in ms, every echo exact, filters off, dry and echoes both full
    void plain (P& p, float timeMs, float feedback, int mode = 0)
    {
        set (p, DelayIDs::sync, 0.0f);
        set (p, DelayIDs::time, timeMs);
        set (p, DelayIDs::feedback, feedback);
        set (p, DelayIDs::mode, (float) mode);
        set (p, DelayIDs::lowCut, 20.0f);
        set (p, DelayIDs::highCut, 20000.0f);
        set (p, DelayIDs::character, 0.0f);
        set (p, DelayIDs::duck, 0.0f);
        set (p, DelayIDs::mix, 50.0f);
    }

    juce::AudioBuffer<float> click (double seconds, int at = 1000)
    {
        juce::AudioBuffer<float> b (2, (int) (sr * seconds));
        b.clear();
        b.setSample (0, at, 1.0f);
        b.setSample (1, at, 1.0f);
        return b;
    }

    float at (const juce::AudioBuffer<float>& b, int ch, int i)   { return b.getSample (ch, i); }

    //==========================================================================
    void testEchoes()
    {
        std::cout << "\n[the echoes]\n";
        {
            P p; prepare (p, sr, 512);
            check (p.getLatencySamples() == 0, "no delay added to the track itself");
            check (getParam (p, DelayIDs::sync) == 1.0f && getParam (p, DelayIDs::mix) == 20.0f, "starts on 1/8 notes (SYNC TEMPO), MIX 20%");
        }
        {
            P p; plain (p, 100.0f, 50.0f); prepare (p, sr, 512);
            auto out = run (p, click (1.0), 512);
            const int t = 4800;
            check (near (at (out, 0, 1000), 1.0f, 1.0e-4f) && near (at (out, 0, 1000 + t), 1.0f, 1.0e-3f) && near (at (out, 0, 1000 + 2 * t), 0.5f, 1.0e-3f)
                       && near (at (out, 0, 1000 + 3 * t), 0.25f, 1.0e-3f) && std::abs (at (out, 0, 1000 + t + 7)) < 1.0e-3f,
                   "TIME 100 ms, FEEDBACK 50%: echoes exactly every 100 ms, each half as loud (" + juce::String (at (out, 0, 1000 + t), 3) + ", "
                       + juce::String (at (out, 0, 1000 + 2 * t), 3) + ", " + juce::String (at (out, 0, 1000 + 3 * t), 3) + ")");
        }
        {
            P p; plain (p, 100.0f, 50.0f, 1); prepare (p, sr, 512);
            auto out = run (p, click (1.0), 512);
            const int t = 4800;
            check (std::abs (at (out, 0, 1000 + t)) > 0.4f && std::abs (at (out, 1, 1000 + t)) < 1.0e-3f
                       && std::abs (at (out, 1, 1000 + 2 * t)) > 0.2f && std::abs (at (out, 0, 1000 + 2 * t)) < 1.0e-3f,
                   "PING-PONG: the first echo on the left, the next on the right, and so on");
        }
        {
            P p; plain (p, 100.0f, 0.0f, 2); prepare (p, sr, 512);
            auto out = run (p, click (1.0), 512);
            check (near (at (out, 0, 1000 + 4800), 1.0f, 1.0e-3f) && near (at (out, 1, 1000 + 7200), 1.0f, 1.0e-3f) && std::abs (at (out, 1, 1000 + 4800)) < 1.0e-3f,
                   "WIDE: the right side's echo comes half as late again (100 ms left, 150 ms right)");
        }
        {
            P p; plain (p, 100.0f, 0.0f); set (p, DelayIDs::sync, 1.0f); set (p, DelayIDs::note, (float) DelayNotes::n8D); prepare (p, sr, 512);
            auto out = run (p, click (1.0), 512);
            check (near (at (out, 0, 1000 + 18000), 1.0f, 1.0e-3f), "SYNC TEMPO: dotted 1/8 at 120 BPM = 375 ms, exactly");
        }
        {
            P p; plain (p, 100.0f, 0.0f); set (p, DelayIDs::mix, 0.0f); prepare (p, sr, 512);
            auto in = drumLoop (sr, 1.0);
            auto out = run (p, in, 512);
            check (maxDifference (out, in) == 0.0f, "MIX 0%: your sound exactly");
            P q; set (q, CommonIDs::bypass, 1.0f); prepare (q, sr, 512);
            auto bo = run (q, in, 512);
            check (maxDifference (bo, in) == 0.0f, "BYPASS: your sound exactly");
            P w; plain (w, 100.0f, 0.0f); set (w, DelayIDs::mix, 100.0f); prepare (w, sr, 512);
            auto wo = run (w, click (0.5), 512);
            check (std::abs (at (wo, 0, 1000)) < 1.0e-6f && near (at (wo, 0, 5800), 1.0f, 1.0e-3f), "MIX 100%: echoes only (for an FX send)");
        }
    }

    //==========================================================================
    void testToneAndSafety()
    {
        std::cout << "\n[tone, ducking, safety]\n";
        // LOW CUT / HIGH CUT shape the echoes, more with every repeat
        const auto echoLevel = [] (double freq, float lowCut, float highCut, int echo)
        {
            P p; plain (p, 200.0f, 60.0f); set (p, DelayIDs::mix, 100.0f);
            set (p, DelayIDs::lowCut, lowCut); set (p, DelayIDs::highCut, highCut);
            prepare (p, sr, 512);
            const int burst = 4800;   // a 100 ms tone burst
            juce::AudioBuffer<float> in (2, (int) sr);
            in.clear();
            for (int i = 0; i < burst; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    in.setSample (ch, i, 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / sr));
            auto out = run (p, in, 512);
            const int start = 9600 * echo + 1200;
            return rms (out, 0, start, start + 2400);
        };
        const float plainLow = echoLevel (60.0, 20.0f, 20000.0f, 1), cutLow1 = echoLevel (60.0, 300.0f, 20000.0f, 1), cutLow3 = echoLevel (60.0, 300.0f, 20000.0f, 3);
        const float plainLow3 = echoLevel (60.0, 20.0f, 20000.0f, 3);
        check (toDb (cutLow1 / plainLow) < -12.0f && toDb (cutLow3 / plainLow3) < toDb (cutLow1 / plainLow) - 10.0f,
               "LOW CUT 300 Hz: 60 Hz in the 1st echo " + juce::String (toDb (cutLow1 / plainLow), 1) + " dB, the 3rd " + juce::String (toDb (cutLow3 / plainLow3), 1)
                   + " dB (thinner with every repeat)");
        const float plainHigh = echoLevel (8000.0, 20.0f, 20000.0f, 1), cutHigh = echoLevel (8000.0, 20.0f, 3000.0f, 1);
        const float keepMid = echoLevel (500.0, 300.0f, 3000.0f, 1) / echoLevel (500.0, 20.0f, 20000.0f, 1);
        check (toDb (cutHigh / plainHigh) < -12.0f && toDb (keepMid) > -1.0f, "HIGH CUT 3 kHz: 8 kHz down " + juce::String (toDb (cutHigh / plainHigh), 1)
                   + " dB; 500 Hz between the two cuts kept (" + juce::String (toDb (keepMid), 1) + " dB)");

        // DUCK: the echoes step back while you play, and come up in the gaps
        {
            const auto measure = [] (float duck)
            {
                P p; plain (p, 150.0f, 40.0f); set (p, DelayIDs::duck, duck); set (p, DelayIDs::mix, 100.0f);
                prepare (p, sr, 512);
                juce::AudioBuffer<float> in (2, (int) (sr * 2.0));
                in.clear();
                for (int i = 0; i < (int) sr; ++i)   // playing for a second, then quiet
                    for (int ch = 0; ch < 2; ++ch)
                        in.setSample (ch, i, 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 330.0 * i / sr));
                auto out = run (p, in, 512);
                return std::pair<float, float> { rms (out, 0, 24000, 46000), rms (out, 0, 48000 + 24000, 48000 + 30000) };
            };
            const auto off = measure (0.0f), on = measure (100.0f);
            const float duringDb = toDb (on.first / off.first), afterDb = toDb (on.second / off.second);
            check (duringDb < -15.0f && afterDb > -3.0f, "DUCK 100%: the echoes " + juce::String (duringDb, 1) + " dB while you play, back within "
                       + juce::String (-afterDb, 1) + " dB once you stop");
        }

        // FEEDBACK at the top, TAPE on, loud noise: it never runs away
        {
            P p; plain (p, 30.0f, 95.0f); set (p, DelayIDs::character, 1.0f); set (p, DelayIDs::mix, 100.0f);
            prepare (p, sr, 512);
            juce::Random r (5);
            juce::AudioBuffer<float> in (2, (int) (sr * 10.0));
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < in.getNumSamples(); ++i)
                    in.setSample (ch, i, 0.9f * (r.nextFloat() * 2.0f - 1.0f));
            auto out = run (p, in, 512);
            const float most = out.getMagnitude (0, out.getNumSamples());
            P q; plain (q, 30.0f, 95.0f); set (q, DelayIDs::mix, 100.0f); prepare (q, sr, 512);
            auto clean = run (q, in, 512);
            check (allFinite (out) && allFinite (clean) && most < 4.0f, "FEEDBACK 95% on loud noise for 10 s: never runs away (TAPE peak "
                       + juce::String (most, 2) + ", CLEAN peak " + juce::String (clean.getMagnitude (0, clean.getNumSamples()), 1) + ")");
        }
    }

    //==========================================================================
    void testMovingTime()
    {
        std::cout << "\n[moving TIME while it plays]\n";
        for (int tape = 0; tape < 2; ++tape)
        {
            P p; plain (p, 200.0f, 30.0f); set (p, DelayIDs::character, (float) tape); set (p, DelayIDs::mix, 100.0f);
            prepare (p, sr, 256);
            auto in = th::sine (sr, 220.0, 0.25f, 3.0);
            juce::AudioBuffer<float> out (in);
            juce::MidiBuffer midi;
            float worst = 0.0f, last = 0.0f;
            for (int start = 0; start < out.getNumSamples(); start += 256)
            {
                if (start == 48000) set (p, DelayIDs::time, 450.0f);
                if (start == 96000) set (p, DelayIDs::time, 120.0f);
                const int n = std::min (256, out.getNumSamples() - start);
                juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, start, n);
                p.processBlock (view, midi);
                for (int i = 0; i < n; ++i)
                {
                    const float s = out.getSample (0, start + i);
                    if (start > 20000) worst = std::max (worst, std::abs (s - last));
                    last = s;
                }
            }
            // a 220 Hz sine at 0.25 steps at most ~0.007 per sample; echoes overlapping a little more
            check (allFinite (out) && worst < 0.03f, juce::String (tape ? "TAPE (the pitch bends)" : "CLEAN (a quick crossfade)")
                       + ": no clicks moving TIME (biggest step " + juce::String (worst, 4) + ")");
        }

        P mono; prepare (mono, sr, 512, 1);
        auto mOut = run (mono, th::sine (sr, 220.0, 0.3f, 1.0, 1), 512);
        check (allFinite (mOut) && mOut.getMagnitude (0, mOut.getNumSamples()) > 0.1f, "works on a mono track");

        P p;
        juce::MemoryBlock state;
        set (p, DelayIDs::mode, 2.0f); set (p, DelayIDs::note, 3.0f); set (p, DelayIDs::duck, 40.0f);
        p.getStateInformation (state);
        P r; r.setStateInformation (state.getData(), (int) state.getSize());
        check (getParam (r, DelayIDs::mode) == 2.0f && getParam (r, DelayIDs::note) == 3.0f && getParam (r, DelayIDs::duck) == 40.0f, "saved with the project and loaded back");

        P t; plain (t, 500.0f, 50.0f);
        check (t.getTailLengthSeconds() > 4.0 && t.getTailLengthSeconds() < 6.0, "tells the DAW how long the echoes ring on ("
                   + juce::String (t.getTailLengthSeconds(), 1) + " s at 500 ms, 50%)");

        const auto report = presetTextsReport (t.getBuiltInPresets());
        check (report.first, report.second);
    }

    //==========================================================================
    void testWindow (const juce::File& outDir)
    {
        std::cout << "\n[plugin window]\n";
        P p; prepare (p, sr, 512);
        loadPresetNamed (p, "Synth - Synthwave Ping-Pong");
        run (p, drumLoop (sr, 1.0), 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* window = dynamic_cast<EasyDelayAudioProcessorEditor*> (editor.get());
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

        window->updateDisplaysNow();
        check (window->getKnob (EasyDelayAudioProcessorEditor::noteKnob).isVisible() && ! window->getKnob (EasyDelayAudioProcessorEditor::timeKnob).isVisible(),
               "SYNC on TEMPO: TIME shows as a note");
        set (p, DelayIDs::sync, 0.0f);
        window->updateDisplaysNow();
        check (! window->getKnob (EasyDelayAudioProcessorEditor::noteKnob).isVisible() && window->getKnob (EasyDelayAudioProcessorEditor::timeKnob).isVisible(),
               "...and in ms with SYNC OFF");
        check (EasyDelayAudioProcessorEditor::describeTime (true, DelayNotes::n8D, 375.0f, 120.0, true) == "dotted 1/8 at 120 BPM = 375 ms",
               "says what a note comes to: \"" + EasyDelayAudioProcessorEditor::describeTime (true, DelayNotes::n8D, 375.0f, 120.0, true) + "\"");
        set (p, DelayIDs::sync, 1.0f);
        for (int i = 0; i < 5; ++i) { run (p, drumLoop (sr, 0.05), 512); window->updateDisplaysNow(); }
        savePng (*editor, outDir.getChildFile ("delay.png"));

        window->getInitAllButton().onClick();
        check (p.getPresetName() == "Default settings" && getParam (p, DelayIDs::mix) == 20.0f, "INIT: back to 'Default settings'");

        bool hintsOk = true;
        for (int k = 0; k < EasyDelayAudioProcessorEditor::numKnobs; ++k)
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
        std::cout << "\n[computer power]\n";
        auto loop = drumLoop (sr, 10.0, -6.0f, -9.0f, -20.0f);
        P p; loadPresetNamed (p, "Synth - Darksynth Tape"); prepare (p, sr, 256);
        const double share = coreShare (p, loop);
        check (share < 1.5, "TAPE, WIDE, DUCK, stereo, 48 kHz: " + juce::String (share, 3) + "% of one core");
       #endif
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());

    testEchoes();
    testToneAndSafety();
    testMovingTime();
    testWindow (outDir);
    testSpeed();
    return finish();
}
