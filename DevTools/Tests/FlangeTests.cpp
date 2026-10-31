// Easy Flange: flanger / chorus / vibrato and the plugin around it (not shipped).
#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace th;
using P = EasyFlangeAudioProcessor;

namespace
{
    constexpr double sr = 48000.0;
    void set (P& p, const char* id, float v)   { setParam (p, id, v); }

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

    void testFlanger()
    {
        std::cout << "\n[FLANGER]\n";
        {
            P p; prepare (p, sr, 512);
            check (p.getLatencySamples() == 0 && getParam (p, FlangeIDs::type) == 0.0f, "no delay added; starts as a FLANGER");
        }
        // DEPTH 0: a fixed 0.3 ms copy - it cancels at 1/(2 x 0.3 ms) = 1667 Hz
        const auto gainAt = [] (double hz)
        {
            P p; set (p, FlangeIDs::depth, 0.0f); set (p, FlangeIDs::feedback, 0.0f); set (p, FlangeIDs::mix, 50.0f); prepare (p, sr, 512);
            auto in = th::sine (sr, hz, 0.3f, 0.5);
            auto out = run (p, in, 512);
            return 20.0 * std::log10 (amplitudeAt (out, 0, 12000, 9600, hz) / amplitudeAt (in, 0, 12000, 9600, hz));
        };
        const double notch = gainAt (1666.7), passed = gainAt (300.0);
        check (notch < passed - 20.0, "the copy cancels your sound at a notch (1667 Hz: " + juce::String (notch, 1) + " dB, 300 Hz: " + juce::String (passed, 1) + " dB)");

        // Swept: the notches slide, so a steady 1 kHz tone rises and falls
        {
            P p; set (p, FlangeIDs::depth, 80.0f); set (p, FlangeIDs::rate, 1.0f); set (p, FlangeIDs::feedback, 0.0f); prepare (p, sr, 512);
            auto out = run (p, th::sine (sr, 1000.0, 0.3f, 2.0), 512);
            float lo = 1.0f, hi = 0.0f;
            for (int start = 12000; start + 480 < out.getNumSamples(); start += 480)
            {
                const float r = rms (out, 0, start, start + 480) / (0.3f / std::sqrt (2.0f));
                lo = std::min (lo, r); hi = std::max (hi, r);
            }
            check (lo < 0.25f && hi > 0.9f, "swept: a steady tone swells and dips as the notches slide past (" + juce::String (toDb (lo), 0) + " to " + juce::String (toDb (hi), 1) + " dB)");
        }
        // FEEDBACK at the end of its range, loud noise: stays put
        {
            P p; set (p, FlangeIDs::feedback, 90.0f); set (p, FlangeIDs::depth, 100.0f); prepare (p, sr, 512);
            juce::Random r (3);
            juce::AudioBuffer<float> in (2, (int) (sr * 5.0));
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < in.getNumSamples(); ++i)
                    in.setSample (ch, i, 0.8f * (r.nextFloat() * 2.0f - 1.0f));
            auto out = run (p, in, 512);
            P q; set (q, FlangeIDs::feedback, -90.0f); prepare (q, sr, 512);
            auto out2 = run (q, in, 512);
            check (allFinite (out) && allFinite (out2) && out.getMagnitude (0, out.getNumSamples()) < 4.0f && out2.getMagnitude (0, out2.getNumSamples()) < 4.0f,
                   "FEEDBACK +90% / -90% on loud noise: stays under control (peaks " + juce::String (out.getMagnitude (0, out.getNumSamples()), 2) + " / "
                       + juce::String (out2.getMagnitude (0, out2.getNumSamples()), 2) + ")");
        }
        {
            P p; set (p, FlangeIDs::mix, 0.0f); prepare (p, sr, 512);
            auto in = drumLoop (sr, 1.0);
            check (maxDifference (run (p, in, 512), in) == 0.0f, "MIX 0%: your sound exactly");
            P q; set (q, CommonIDs::bypass, 1.0f); prepare (q, sr, 512);
            check (maxDifference (run (q, in, 512), in) == 0.0f, "BYPASS: your sound exactly");
        }
    }

    void testChorusAndVibrato()
    {
        std::cout << "\n[CHORUS, VIBRATO, WIDTH]\n";
        {
            auto loop = drumLoop (sr, 4.0, -6.0f, -9.0f, -20.0f);
            auto tone = th::sine (sr, 220.0, 0.2f, 4.0);
            for (int ch = 0; ch < 2; ++ch) loop.addFrom (ch, 0, tone, ch, 0, loop.getNumSamples());
            loop.copyFrom (1, 0, loop, 0, 0, loop.getNumSamples());   // (the same on both sides, so any difference is WIDTH's)
            P p; set (p, FlangeIDs::type, 1.0f); set (p, FlangeIDs::width, 100.0f); prepare (p, sr, 512);
            auto out = run (p, loop, 512);
            const double change = loudnessDb (out, (int) sr, out.getNumSamples()) - loudnessDb (loop, (int) sr, loop.getNumSamples());
            float sideDiff = 0.0f;
            for (int i = 0; i < out.getNumSamples(); ++i) sideDiff = std::max (sideDiff, std::abs (out.getSample (0, i) - out.getSample (1, i)));
            check (std::abs (change) < 1.5 && sideDiff > 0.02f, "CHORUS, WIDTH 100%: about as loud as it went in (" + juce::String (change, 2)
                       + " dB), and the two sides now differ (wider)");

            P q; set (q, FlangeIDs::type, 1.0f); set (q, FlangeIDs::width, 0.0f); prepare (q, sr, 512);
            auto o2 = run (q, loop, 512);
            float same = 0.0f;
            for (int i = 0; i < o2.getNumSamples(); ++i) same = std::max (same, std::abs (o2.getSample (0, i) - o2.getSample (1, i)));
            check (same < 1.0e-6f, "WIDTH 0: both sides move together");
        }
        {
            // VIBRATO: how far a 1 kHz tone's pitch swings
            P p; set (p, FlangeIDs::type, 2.0f); set (p, FlangeIDs::rate, 5.0f); set (p, FlangeIDs::depth, 25.0f); set (p, FlangeIDs::mix, 100.0f);
            prepare (p, sr, 512);
            auto out = run (p, th::sine (sr, 1000.0, 0.3f, 3.0), 512);
            std::vector<double> crossings;
            for (int i = (int) sr; i < out.getNumSamples() - 1; ++i)
            {
                const float a = out.getSample (0, i), b = out.getSample (0, i + 1);
                if (a <= 0.0f && b > 0.0f) crossings.push_back (i + a / (a - b));
            }
            double worst = 0.0;
            for (size_t k = 4; k < crossings.size(); k += 4)
                worst = std::max (worst, std::abs ((crossings[k] - crossings[k - 4]) / 4.0 / 48.0 - 1.0));
            const double semitones = 12.0 * std::log2 (1.0 + worst);
            check (semitones > 0.15 && semitones < 1.5, "VIBRATO 25% at 5 Hz: the pitch wobbles about " + juce::String (semitones, 2) + " semitones - a singer's vibrato");
        }
        {
            // Changing TYPE while it plays: no click
            P p; prepare (p, sr, 256);
            auto in = th::sine (sr, 220.0, 0.25f, 2.0);
            juce::AudioBuffer<float> out (in);
            juce::MidiBuffer midi;
            float worst = 0.0f, last = 0.0f;
            int worstAt = 0;
            for (int start = 0; start < out.getNumSamples(); start += 256)
            {
                if (start == 32000) set (p, FlangeIDs::type, 1.0f);
                if (start == 64000) set (p, FlangeIDs::type, 2.0f);
                juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, start, std::min (256, out.getNumSamples() - start));
                p.processBlock (view, midi);
                for (int i = 0; i < view.getNumSamples(); ++i)
                {
                    const float s = out.getSample (0, start + i);
                    if (start > 4000 && std::abs (s - last) > worst) { worst = std::abs (s - last); worstAt = start + i; }
                    last = s;
                }
            }
            check (worst < 0.03f, "changing TYPE while it plays: no clicks (largest step " + juce::String (worst, 4) + " at " + juce::String (worstAt) + ")");
        }
        P mono; prepare (mono, sr, 512, 1);
        check (allFinite (run (mono, th::sine (sr, 220.0, 0.3f, 1.0, 1), 512)), "works on a mono track");
    }

    void testParts (const juce::File& outDir)
    {
        std::cout << "\n[saving, presets, window]\n";
        {
            P p; juce::MemoryBlock state;
            set (p, FlangeIDs::type, 2.0f); set (p, FlangeIDs::feedback, -40.0f); set (p, FlangeIDs::note, 6.0f);
            p.getStateInformation (state);
            P r; r.setStateInformation (state.getData(), (int) state.getSize());
            check (getParam (r, FlangeIDs::type) == 2.0f && getParam (r, FlangeIDs::feedback) == -40.0f && getParam (r, FlangeIDs::note) == 6.0f,
                   "saved with the project and loaded back");
            const auto report = presetTextsReport (p.getBuiltInPresets());
            check (report.first, report.second);
        }

        P p; prepare (p, sr, 512);
        loadPresetNamed (p, "Guitar - 80s Clean Chorus");
        run (p, drumLoop (sr, 0.5), 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* window = dynamic_cast<EasyFlangeAudioProcessorEditor*> (editor.get());
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
        check (! window->getKnob (EasyFlangeAudioProcessorEditor::feedbackKnob).isEnabled(), "FEEDBACK steps aside (dimmed) when it isn't a FLANGER");
        savePng (*editor, outDir.getChildFile ("flange.png"));

        window->getInitAllButton().onClick();
        window->updateDisplaysNow();
        check (p.getPresetName() == "Default settings" && window->getKnob (EasyFlangeAudioProcessorEditor::feedbackKnob).isEnabled(),
               "INIT: back to 'Default settings' (a FLANGER, FEEDBACK back)");

        bool hintsOk = true;
        for (int k = 0; k < EasyFlangeAudioProcessorEditor::numKnobs; ++k)
        {
            window->mouseEnter (hoverEvent (&window->getKnob (k)));
            if (window->hintBar.getText().length() < 40) hintsOk = false;
        }
        check (hintsOk, "every knob explains itself in the help bar");
    }

    void testSpeed()
    {
       #if ! JUCE_DEBUG
        std::cout << "\n[computer power]\n";
        P p; loadPresetNamed (p, "Synth Pad - Wide Chorus"); prepare (p, sr, 256);
        const double share = coreShare (p, drumLoop (sr, 10.0));
        check (share < 1.0, "CHORUS (two voices), stereo, 48 kHz: " + juce::String (share, 3) + "% of one core");
       #endif
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    testFlanger();
    testChorusAndVibrato();
    testParts (outDir);
    testSpeed();
    return finish();
}
