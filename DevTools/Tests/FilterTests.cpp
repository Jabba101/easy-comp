// Easy Filter: the moving filter and the plugin around it (not shipped).
#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace th;
using P = EasyFilterAudioProcessor;

namespace
{
    constexpr double sr = 48000.0;
    void set (P& p, const char* id, float v)   { setParam (p, id, v); }

    /** A DAW playing the song at 120 BPM, from `beats` on. */
    struct Song : public juce::AudioPlayHead
    {
        double beats = 0.0;
        bool playing = true;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (120.0);
            info.setIsPlaying (playing);
            info.setPpqPosition (beats);
            return info;
        }
    };

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

    /** The gain (dB) of a still filter (DEPTH 0) at a frequency. */
    double stillGain (int type, float cutoff, float resonance, double hz)
    {
        P p; set (p, FilterIDs::type, (float) type); set (p, FilterIDs::cutoff, cutoff); set (p, FilterIDs::resonance, resonance);
        set (p, FilterIDs::depth, 0.0f); set (p, FilterIDs::sync, 0.0f); prepare (p, sr, 512);
        auto in = th::sine (sr, hz, 0.25f, 0.6);
        auto out = run (p, in, 512);
        return 20.0 * std::log10 (amplitudeAt (out, 0, 14400, 9600, hz) / amplitudeAt (in, 0, 14400, 9600, hz));
    }

    juce::AudioBuffer<float> noise (double seconds, float level, int seed = 3)
    {
        juce::Random r (seed);
        juce::AudioBuffer<float> b (2, (int) (sr * seconds));
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (ch, i, level * (r.nextFloat() * 2.0f - 1.0f));
        return b;
    }

    /** The biggest second difference (a click stands out from the music). */
    float worstJump (const juce::AudioBuffer<float>& b, int from = 2)
    {
        float worst = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = juce::jmax (2, from); i < b.getNumSamples(); ++i)
                worst = std::max (worst, std::abs (b.getSample (ch, i) - 2.0f * b.getSample (ch, i - 1) + b.getSample (ch, i - 2)));
        return worst;
    }

    //==========================================================================
    void testFilter()
    {
        std::cout << "\n[the filter]\n";
        {
            P p; prepare (p, sr, 512);
            check (p.getLatencySamples() == 0 && getParam (p, FilterIDs::type) == 0.0f, "no delay added; starts as a LOW-PASS");
        }

        const double lpLow = stillGain (0, 1000.0f, 0.0f, 100.0), lpHigh = stillGain (0, 1000.0f, 0.0f, 8000.0);
        check (lpLow > -1.5 && lpHigh < -30.0, "LOW-PASS at 1 kHz: 100 Hz through (" + juce::String (lpLow, 1) + " dB), 8 kHz gone (" + juce::String (lpHigh, 1) + " dB)");
        const double hpLow = stillGain (1, 1000.0f, 0.0f, 100.0), hpHigh = stillGain (1, 1000.0f, 0.0f, 8000.0);
        check (hpLow < -30.0 && hpHigh > -1.5, "HIGH-PASS at 1 kHz: 100 Hz gone (" + juce::String (hpLow, 1) + " dB), 8 kHz through (" + juce::String (hpHigh, 1) + " dB)");
        const double bpMid = stillGain (2, 1000.0f, 0.3f * 100.0f, 1000.0), bpLow = stillGain (2, 1000.0f, 30.0f, 100.0), bpHigh = stillGain (2, 1000.0f, 30.0f, 8000.0);
        check (std::abs (bpMid) < 1.5 && bpLow < -12.0 && bpHigh < -12.0, "BAND-PASS at 1 kHz: 1 kHz through at full level (" + juce::String (bpMid, 1) + " dB), 100 Hz and 8 kHz down ("
               + juce::String (bpLow, 1) + " / " + juce::String (bpHigh, 1) + " dB)");

        // The window's curve is the real thing
        {
            double worst = 0.0;
            for (int type : { 0, 1, 2 })
                for (float res : { 0.0f, 70.0f })
                    for (double hz : { 150.0, 700.0, 1000.0, 2500.0 })
                        worst = std::max (worst, std::abs (stillGain (type, 1000.0f, res, hz)
                                                           - ModFilter::responseDb ((ModFilter::Type) type, 1000.0f, res / 100.0f, (float) hz, sr)));
            check (worst < 0.5, "the window's curve matches what you hear (worst " + juce::String (worst, 2) + " dB)");
        }

        const double flat = stillGain (0, 1000.0f, 0.0f, 1000.0), peaked = stillGain (0, 1000.0f, 100.0f, 1000.0);
        check (peaked > flat + 9.0 && peaked < 15.0, "RESONANCE: a peak at the cutoff (" + juce::String (flat, 1) + " dB -> " + juce::String (peaked, 1) + " dB), not a howl");

        // Pushed as far as it goes, on loud noise: stays put
        {
            P p; set (p, FilterIDs::resonance, 100.0f); set (p, FilterIDs::depth, 100.0f); set (p, FilterIDs::sync, 0.0f); set (p, FilterIDs::rate, 20.0f);
            set (p, FilterIDs::shape, (float) FilterShape::square); set (p, FilterIDs::cutoff, 2000.0f); prepare (p, sr, 512);
            auto out = run (p, noise (5.0, 0.9f), 512);
            check (allFinite (out) && out.getMagnitude (0, out.getNumSamples()) < 6.0f,
                   "full RESONANCE and DEPTH, fastest SQUARE, loud noise: stays under control (peak " + juce::String (out.getMagnitude (0, out.getNumSamples()), 2) + ")");
        }
        {
            P p; set (p, FilterIDs::mix, 0.0f); prepare (p, sr, 512);
            auto in = drumLoop (sr, 1.0);
            check (maxDifference (run (p, in, 512), in) == 0.0f, "MIX 0%: your sound exactly");
            P q; set (q, CommonIDs::bypass, 1.0f); prepare (q, sr, 512);
            check (maxDifference (run (q, in, 512), in) == 0.0f, "BYPASS: your sound exactly");
        }
    }

    //==========================================================================
    void testMovement()
    {
        std::cout << "\n[the movement]\n";

        // DEPTH swings a 1 kHz tone through a low-pass set at 1 kHz: loud when it's open, gone when it's shut
        {
            P p; set (p, FilterIDs::depth, 100.0f); set (p, FilterIDs::sync, 0.0f); set (p, FilterIDs::rate, 1.0f); prepare (p, sr, 512);
            auto out = run (p, th::sine (sr, 1000.0, 0.3f, 3.0), 512);
            float lo = 10.0f, hi = 0.0f;
            for (int start = 24000; start + 480 < out.getNumSamples(); start += 480)
            {
                const float r = rms (out, 0, start, start + 480) / (0.3f / std::sqrt (2.0f));
                lo = std::min (lo, r); hi = std::max (hi, r);
            }
            check (hi > 0.85f && lo < 0.05f, "DEPTH 100%: a 1 kHz tone opens fully and shuts (" + juce::String (toDb (lo), 0) + " to " + juce::String (toDb (hi), 1) + " dB)");
        }

        // Every shape keeps to the range, and the window's cycle is the real one
        {
            bool ok = true;
            for (int shape = 0; shape < 6; ++shape)
                for (double p = 0.0; p < 3.0; p += 0.01)
                {
                    const float v = ModFilter::shapeAt ((ModFilter::Shape) shape, p, std::floor (p));
                    ok = ok && v >= -1.0f && v <= 1.0f;
                }
            const bool square = ModFilter::shapeAt (ModFilter::Shape::square, 0.25) == 1.0f && ModFilter::shapeAt (ModFilter::Shape::square, 0.75) == -1.0f;
            const bool ramps = ModFilter::shapeAt (ModFilter::Shape::rampUp, 0.9) > 0.7f && ModFilter::shapeAt (ModFilter::Shape::rampDown, 0.9) < -0.7f;
            const bool random = ModFilter::shapeAt (ModFilter::Shape::random, 0.2, 5.0) == ModFilter::shapeAt (ModFilter::Shape::random, 0.8, 5.0)
                                  && ModFilter::shapeAt (ModFilter::Shape::random, 0.2, 5.0) != ModFilter::shapeAt (ModFilter::Shape::random, 0.2, 6.0);
            check (ok && square && ramps && random, "the six SHAPEs: in range, SQUARE jumps, RAMPs rise and fall, RANDOM holds for a cycle then moves");
        }

        // WIDTH: the sides apart
        {
            const auto sides = [] (float width)
            {
                P p; set (p, FilterIDs::depth, 100.0f); set (p, FilterIDs::width, width); set (p, FilterIDs::sync, 0.0f); set (p, FilterIDs::rate, 0.5f);
                prepare (p, sr, 512);
                run (p, noise (0.5, 0.3f), 512);   // (half a second in: a quarter of the way round)
                return p.getCutoffNow (1) / p.getCutoffNow (0);
            };
            const float together = sides (0.0f), apart = sides (100.0f);
            check (std::abs (together - 1.0f) < 0.001f && (apart > 4.0f || apart < 0.25f),
                   "WIDTH 0: both sides together; 100%: far apart (right/left cutoff x" + juce::String (apart, 2) + ")");
        }

        // A mono track with a stereo output gets the width too
        {
            P p; set (p, FilterIDs::depth, 100.0f); set (p, FilterIDs::width, 100.0f); set (p, FilterIDs::sync, 0.0f); set (p, FilterIDs::rate, 1.0f);
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (juce::AudioChannelSet::mono());
            layout.outputBuses.add (juce::AudioChannelSet::stereo());
            const bool supported = p.setBusesLayout (layout);
            p.setRateAndBufferSizeDetails (sr, 512);
            p.prepareToPlay (sr, 512);
            auto in = noise (1.0, 0.3f);
            in.clear (1, 0, in.getNumSamples());
            auto out = run (p, in, 512);
            check (supported && maxDifference (out, out) == 0.0f && rms (out, 1, 0, out.getNumSamples()) > 0.01f
                     && std::abs (rms (out, 0, 24000, 36000) - rms (out, 1, 24000, 36000)) > 0.01f,
                   "a mono track with a stereo output: both sides filtered, moving apart");
        }

        // SQUARE, RAMP and RANDOM jumps are eased: no clicks. A 220 Hz tone has nothing above 3 kHz, so anything that
        // turns up there is the filter's movement ticking.
        {
            const auto tickWith = [] (int shape)
            {
                P p; set (p, FilterIDs::shape, (float) shape); set (p, FilterIDs::depth, 100.0f); set (p, FilterIDs::resonance, 60.0f);
                set (p, FilterIDs::sync, 1.0f); set (p, FilterIDs::note, (float) FilterNotes::n16); set (p, FilterIDs::cutoff, 600.0f); prepare (p, sr, 512);
                auto out = run (p, th::sine (sr, 220.0, 0.5f, 2.0), 512);
                juce::IIRFilter high1, high2;
                high1.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 3000.0));
                high2.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 3000.0));
                float worst = 0.0f;
                for (int i = 0; i < out.getNumSamples(); ++i)
                {
                    const float h = high2.processSingleSampleRaw (high1.processSingleSampleRaw (out.getSample (0, i)));
                    if (i > 4800) worst = std::max (worst, std::abs (h));
                }
                return toDb (worst / 0.5f);
            };
            float worst = -200.0f;
            juce::String each;
            for (int shape : { FilterShape::square, FilterShape::rampUp, FilterShape::rampDown, FilterShape::random })
            {
                const float t = tickWith (shape);
                each << juce::String (juce::roundToInt (t)) << " ";
                worst = std::max (worst, t);
            }
            check (worst < -40.0f, "SQUARE, RAMPs and RANDOM every 16th, full DEPTH: no ticks (the worst splash above 3 kHz: " + each + "dB)");
        }

        // TYPE switched while playing: no clicks
        {
            const auto loop = drumLoop (sr, 3.0);
            float steady = 0.0f;   // (the biggest jump any of the three types makes on its own)
            for (int type : { 0, 1, 2 })
            {
                P a; set (a, FilterIDs::type, (float) type); prepare (a, sr, 256);
                steady = std::max (steady, worstJump (run (a, loop, 256), 2400));
            }
            P b; prepare (b, sr, 256);
            int step = 0;
            const float switching = worstJump (run (b, loop, 256, [&] (int start) { if (start > 0 && start % 4800 < 256) set (b, FilterIDs::type, (float) (++step % 3)); }), 2400);
            check (switching < steady * 1.6f, "switching TYPE every 100 ms on a drum loop: no clicks (" + juce::String (switching / steady, 2) + "x the steady types' biggest jump)");
        }
    }

    //==========================================================================
    void testTempo()
    {
        std::cout << "\n[locked to the song]\n";

        // The same place in the song sounds the same, every time it's played (RANDOM too)
        {
            const auto from = [] (double startBeat, double warmUpSeconds)
            {
                P p; set (p, FilterIDs::shape, (float) FilterShape::random); set (p, FilterIDs::sync, 1.0f); set (p, FilterIDs::note, (float) FilterNotes::n8);
                set (p, FilterIDs::depth, 90.0f); prepare (p, sr, 512);
                Song song;
                p.setPlayHead (&song);
                if (warmUpSeconds > 0.0)
                {
                    song.beats = 40.0;
                    run (p, noise (warmUpSeconds, 0.3f, 9), 512, [&] (int start) { song.beats = 40.0 + start / sr * 2.0; });
                }
                auto out = run (p, noise (2.0, 0.3f), 512, [&] (int start) { song.beats = startBeat + start / sr * 2.0; });
                p.setPlayHead (nullptr);
                return out;
            };
            const auto first = from (8.0, 0.0), again = from (8.0, 1.3);
            float worst = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 2400; i < first.getNumSamples(); ++i)   // (after the first 50 ms, while it settles from where it was)
                    worst = std::max (worst, std::abs (first.getSample (ch, i) - again.getSample (ch, i)));
            check (worst < 0.003f, "SYNC: bar 3 sounds the same every time it's played, RANDOM included (difference " + juce::String (toDb (worst), 0) + " dB)");
        }

        // One cycle takes exactly the note asked for
        {
            P p; set (p, FilterIDs::sync, 1.0f); set (p, FilterIDs::note, (float) FilterNotes::bar1); prepare (p, sr, 480);
            Song song;
            song.beats = 1.0;   // (a quarter of the way through the bar)
            p.setPlayHead (&song);
            run (p, noise (0.01, 0.1f), 480);
            p.setPlayHead (nullptr);
            check (std::abs (p.getPosition() - (0.25f + 0.01f * 2.0f / 4.0f)) < 0.001f, "SYNC 1 bar at 120 BPM: a quarter-note in, it's a quarter of the way round ("
                   + juce::String (p.getPosition(), 4) + ")");
        }
    }

    //==========================================================================
    void testParts (const juce::File& outDir)
    {
        std::cout << "\n[saving, presets, window]\n";
        {
            P p; juce::MemoryBlock state;
            set (p, FilterIDs::type, 2.0f); set (p, FilterIDs::cutoff, 3300.0f); set (p, FilterIDs::shape, 5.0f); set (p, FilterIDs::note, 12.0f);
            p.getStateInformation (state);
            P r; r.setStateInformation (state.getData(), (int) state.getSize());
            check (getParam (r, FilterIDs::type) == 2.0f && getParam (r, FilterIDs::cutoff) == 3300.0f && getParam (r, FilterIDs::shape) == 5.0f
                     && getParam (r, FilterIDs::note) == 12.0f, "saved with the project and loaded back");
            const auto report = presetTextsReport (p.getBuiltInPresets());
            check (report.first, report.second);

            bool clean = true;
            for (auto& preset : p.getBuiltInPresets())
            {
                P q; prepare (q, sr, 512);
                q.loadPreset (preset);
                auto out = run (q, drumLoop (sr, 2.0), 512);
                clean = clean && allFinite (out) && out.getMagnitude (0, out.getNumSamples()) < 3.0f;
            }
            check (clean, "every preset plays cleanly on a drum loop (" + juce::String ((int) p.getBuiltInPresets().size()) + " presets)");
        }

        P p; prepare (p, sr, 512);
        loadPresetNamed (p, "Synth - Sample & Hold 1/16");
        Song song;
        p.setPlayHead (&song);
        run (p, drumLoop (sr, 0.7), 512, [&] (int start) { song.beats = start / sr * 2.0; });
        p.setPlayHead (nullptr);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* window = dynamic_cast<EasyFilterAudioProcessorEditor*> (editor.get());
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
        check (window->getKnob (EasyFilterAudioProcessorEditor::noteKnob).isVisible() && ! window->getKnob (EasyFilterAudioProcessorEditor::rateKnob).isVisible()
                 && window->getKnob (EasyFilterAudioProcessorEditor::noteKnob).getTextFromValue (window->getKnob (EasyFilterAudioProcessorEditor::noteKnob).getValue()) == "1/16 note",
               "SYNC TEMPO: RATE shows as a note (\"1/16 note\")");
        check (window->getKnob (EasyFilterAudioProcessorEditor::cutoffKnob).getTextFromValue (1200.0) == "1.2 kHz"
                 || window->getKnob (EasyFilterAudioProcessorEditor::cutoffKnob).getTextFromValue (1200.0).contains ("1.2"),
               "CUTOFF reads in Hz / kHz (" + window->getKnob (EasyFilterAudioProcessorEditor::cutoffKnob).getTextFromValue (1200.0) + ")");
        savePng (*editor, outDir.getChildFile ("filter.png"));

        window->getInitAllButton().onClick();
        window->updateDisplaysNow();
        check (p.getPresetName() == "Default settings", "INIT: back to 'Default settings'");

        bool hintsOk = true;
        for (int k = 0; k < EasyFilterAudioProcessorEditor::numKnobs; ++k)
        {
            window->mouseEnter (hoverEvent (&window->getKnob (k)));
            if (window->hintBar.getText().length() < 40) hintsOk = false;
        }
        check (hintsOk, "every knob explains itself in the help bar");

        // Two more pictures: a high-pass build, and a wide band-pass pulse
        for (auto [preset, file] : { std::pair<const char*, const char*> { "Drum Bus - High-Pass Build (4 bars)", "filter-highpass.png" },
                                     std::pair<const char*, const char*> { "Cyberpunk - Stereo Pulse 1/16", "filter-bandpass.png" } })
        {
            P q; prepare (q, sr, 512);
            loadPresetNamed (q, preset);
            Song s2;
            q.setPlayHead (&s2);
            run (q, drumLoop (sr, 0.3), 512, [&] (int start) { s2.beats = 5.1 + start / sr * 2.0; });
            q.setPlayHead (nullptr);
            std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
            if (auto* w = dynamic_cast<EasyFilterAudioProcessorEditor*> (ed.get())) w->updateDisplaysNow();
            savePng (*ed, outDir.getChildFile (file));
        }
    }

    void testSpeed()
    {
       #if ! JUCE_DEBUG
        std::cout << "\n[computer power]\n";
        P p; loadPresetNamed (p, "Synth - Sample & Hold 1/16"); prepare (p, sr, 256);
        const double share = coreShare (p, drumLoop (sr, 10.0));
        check (share < 1.0, "stereo, 48 kHz: " + juce::String (share, 3) + "% of one core");
       #endif
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    testFilter();
    testMovement();
    testTempo();
    testParts (outDir);
    testSpeed();
    return finish();
}
