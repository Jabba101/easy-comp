// Easy Tape: the tape machine and the plugin around it (not shipped).
#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_dsp/juce_dsp.h>

using namespace th;
using P = EasyTapeAudioProcessor;

namespace
{
    constexpr double sr = 48000.0;

    void set (P& p, const char* id, float v)   { setParam (p, id, v); }

    const juce::StringArray speedNames { "15 ips", "30 ips", "cassette" };

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

    // Gain (dB) at freq for a sine at levelDb (peak), through a fresh plugin
    double gainAt (int speed, double freq, float levelDb, float driveDb = 0.0f, int quality = 2)
    {
        P p; set (p, TapeIDs::speed, (float) speed); set (p, TapeIDs::drive, driveDb); set (p, TapeIDs::quality, (float) quality);
        set (p, CommonIDs::autoGain, 0.0f);
        prepare (p, sr, 512);
        const double seconds = freq < 60.0 ? 2.0 : 0.6;
        auto in = th::sine (sr, freq, fromDb (levelDb), seconds);
        auto out = run (p, in, 512);
        const int length = (int) (sr * (freq < 60.0 ? 1.0 : 0.25));
        const int start = out.getNumSamples() - length;
        return 20.0 * std::log10 (amplitudeAt (out, 0, start, length, freq) / amplitudeAt (in, 0, start, length, freq));
    }

    // Distortion of a 1 kHz sine: the 2nd..9th harmonics against the note, in %
    double thdPercent (int speed, float levelDb, float driveDb)
    {
        P p; set (p, TapeIDs::speed, (float) speed); set (p, TapeIDs::drive, driveDb); set (p, TapeIDs::quality, 3.0f);
        prepare (p, sr, 512);
        auto in = th::sine (sr, 1000.0, fromDb (levelDb), 0.6);
        auto out = run (p, in, 512);
        const int length = (int) (sr * 0.25), start = out.getNumSamples() - length;
        const double f = amplitudeAt (out, 0, start, length, 1000.0);
        double h = 0.0;
        for (int k = 2; k <= 9; ++k) { const double a = amplitudeAt (out, 0, start, length, 1000.0 * k); h += a * a; }
        return 100.0 * std::sqrt (h) / f;
    }

    //==========================================================================
    void testBasics()
    {
        std::cout << "\n[the delay, bypass, silence]\n";
        P p; prepare (p, sr, 512);
        const int latency = p.getLatencySamples();
        check (latency > 100 && latency < 400, "reports a fixed delay to the DAW: " + juce::String (latency) + " samples (" + juce::String (latency / 48.0, 2) + " ms)");
        check (getParam (p, CommonIDs::autoGain) > 0.5f && getParam (p, TapeIDs::speed) == 0.0f && getParam (p, TapeIDs::drive) == 0.0f
               && getParam (p, TapeIDs::wow) == 0.0f && getParam (p, TapeIDs::hiss) == 0.0f,
               "starts at 15 ips, DRIVE 0 dB, no wow & flutter, no hiss, AUTO-GAIN on");

        bool same = true;
        for (int speed = 0; speed < 3; ++speed)
            for (int quality = 0; quality < 4; ++quality)
            {
                P q; set (q, TapeIDs::speed, (float) speed); set (q, TapeIDs::quality, (float) quality); prepare (q, sr, 512);
                same = same && q.getLatencySamples() == latency;
            }
        check (same, "the same delay at every SPEED and OVERSAMPLING setting");

        auto loop = drumLoop (sr, 1.0, -6.0f, -9.0f, -20.0f);
        {
            P q; set (q, CommonIDs::bypass, 1.0f); prepare (q, sr, 512);
            auto out = run (q, loop, 512);
            float diff = 0.0f;
            for (int i = latency; i < out.getNumSamples(); ++i) diff = std::max (diff, std::abs (out.getSample (0, i) - loop.getSample (0, i - latency)));
            check (diff == 0.0f, "BYPASS: the original exactly, just delayed");
        }
        {
            P q; prepare (q, sr, 512);
            juce::AudioBuffer<float> silence (2, (int) sr);
            silence.clear();
            auto out = run (q, silence, 512);
            check (out.getMagnitude (0, out.getNumSamples()) == 0.0f, "HISS at 0: silence in, silence out");

            P h; set (h, TapeIDs::hiss, 50.0f); prepare (h, sr, 512);
            auto hissOut = run (h, silence, 512);
            const float level = toDb (rms (hissOut, 0, (int) sr / 2, (int) sr));
            check (level > -80.0f && level < -60.0f, "HISS 50%, 15 ips: tape noise at " + juce::String (level, 1) + " dBFS RMS (quiet, but there)");
            check (std::abs (hissOut.getSample (0, 30000) - hissOut.getSample (1, 30000)) > 0.0f, "the hiss is different on each side (stereo)");
        }
    }

    //==========================================================================
    void testResponse()
    {
        std::cout << "\n[frequency response, quiet (-30 dBFS): the head bump, the top]\n";
        const double freqs[] = { 20, 30, 40, 50, 70, 100, 150, 300, 1000, 3000, 6000, 10000, 15000, 20000 };

        for (int speed = 0; speed < 3; ++speed)
        {
            juce::String line = "  " + speedNames[speed].paddedRight (' ', 9) + ":";
            std::map<int, double> g;
            for (double f : freqs)
            {
                g[(int) f] = gainAt (speed, f, -30.0f);
                line << " " << (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f)) << " " << juce::String (g[(int) f], 1);
            }
            std::cout << line << "\n";

            // and the drawing agrees with the sound
            float worst = 0.0f;
            for (double f : { 30.0, 50.0, 100.0, 1000.0, 10000.0 })
                worst = std::max (worst, (float) std::abs (g[(int) f] - TapeMachine::responseDb ((TapeMachine::Speed) speed, f, sr)));
            check (worst < 0.25f, speedNames[speed] + ": the window's curve matches what you hear (within " + juce::String (worst, 2) + " dB)");

            if (speed == 0)
            {
                check (g[50] > 1.5 && g[50] < 2.5, "15 ips: the head bump, +" + juce::String (g[50], 1) + " dB at 50 Hz");
                check (g[20] < -1.0, "15 ips: rolls off under the bump (" + juce::String (g[20], 1) + " dB at 20 Hz)");
                check (std::abs (g[300]) < 0.3 && std::abs (g[1000]) < 0.1 && std::abs (g[3000]) < 0.15 && std::abs (g[10000]) < 0.4,
                       "15 ips: flat from 300 Hz to 10 kHz (within 0.4 dB) - no fake roll-off");
                check (g[20000] < -0.5 && g[20000] > -1.6, "15 ips: just " + juce::String (g[20000], 1) + " dB at 20 kHz");
            }
            else if (speed == 1)
            {
                check (g[100] > 0.7 && g[100] < 1.5, "30 ips: a smaller bump higher up, +" + juce::String (g[100], 1) + " dB at 100 Hz");
                check (g[50] < g[100] - 0.8, "30 ips: the dip under it (" + juce::String (g[50], 1) + " dB at 50 Hz)");
                check (std::abs (g[1000]) < 0.1 && std::abs (g[10000]) < 0.3 && g[20000] > -0.9, "30 ips: flat on top (" + juce::String (g[20000], 1) + " dB at 20 kHz)");
            }
            else
            {
                check (g[15000] < -3.0 && g[10000] > -3.5, "cassette: the top rolls off (" + juce::String (g[10000], 1) + " dB at 10 kHz, "
                                                          + juce::String (g[15000], 1) + " at 15 kHz)");
            }
        }
    }

    //==========================================================================
    void testSaturation()
    {
        std::cout << "\n[saturation: grows with level; the highs compress only when pushed]\n";
        // 0 VU = a 1 kHz sine at -18 dBFS RMS = -15 dBFS peak
        const double atZero = thdPercent (0, -15.0f, 0.0f), at6 = thdPercent (0, -15.0f, 6.0f), at12 = thdPercent (0, -15.0f, 12.0f);
        const double quiet = thdPercent (0, -35.0f, 0.0f);
        std::cout << "  15 ips, 1 kHz: -20 VU " << juce::String (quiet, 3) << "%, 0 VU " << juce::String (atZero, 2) << "%, +6 "
                  << juce::String (at6, 2) << "%, +12 " << juce::String (at12, 2) << "%\n";
        check (quiet < 0.05, "20 dB under the normal level: practically clean (" + juce::String (quiet, 3) + "% distortion)");
        check (atZero > 0.1 && atZero < 0.8, "at 0 VU: " + juce::String (atZero, 2) + "% - felt rather than heard, like a real machine");
        check (at6 > atZero * 2.5 && at12 > at6 * 2.0 && at12 < 8.0, "pushed +6 / +12: " + juce::String (at6, 2) + "% / " + juce::String (at12, 2) + "% - thicker, never fuzz");

        const double c0 = thdPercent (2, -15.0f, 0.0f), t30 = thdPercent (1, -15.0f, 0.0f);
        check (t30 < atZero && c0 > atZero, "30 ips has more headroom (" + juce::String (t30, 2) + "%), cassette less (" + juce::String (c0, 2) + "%)");

        // HF compression: 10 kHz against 1 kHz, quiet vs pushed
        for (int speed = 0; speed < 3; ++speed)
        {
            const double quietTilt = gainAt (speed, 10000.0, -30.0f, 0.0f) - gainAt (speed, 1000.0, -30.0f, 0.0f);
            const double hotTilt = gainAt (speed, 10000.0, -12.0f, 9.0f) - gainAt (speed, 1000.0, -12.0f, 9.0f);
            const double squash = quietTilt - hotTilt;
            std::cout << "  " << speedNames[speed] << ": 10 kHz squashes " << juce::String (squash, 2) << " dB more than 1 kHz when pushed +9\n";
            if (speed == 0) check (squash > 1.0 && squash < 6.0, "15 ips pushed: the loud highs soften (" + juce::String (squash, 1) + " dB), quiet ones don't");
            if (speed == 1) check (squash < 2.0, "30 ips pushed: the highs hardly squash (" + juce::String (squash, 1) + " dB)");
            if (speed == 2) check (squash > 3.0, "cassette pushed: the highs squash a lot (" + juce::String (squash, 1) + " dB)");
        }
    }

    //==========================================================================
    void testLevelAndWow()
    {
        std::cout << "\n[DRIVE keeps the volume; wow & flutter]\n";
        auto loop = drumLoop (sr, 4.0, -6.0f, -9.0f, -20.0f);
        const int from = (int) sr * 2, to = loop.getNumSamples();
        const double inLoud = loudnessDb (loop, from, to);
        for (float drive : { 0.0f, 6.0f, 12.0f })
        {
            P p; set (p, TapeIDs::drive, drive); prepare (p, sr, 512);
            auto out = run (p, loop, 512);
            const double change = loudnessDb (out, from, to) - inLoud;
            check (std::abs (change) < 1.0, "drum loop, DRIVE +" + juce::String ((int) drive) + " dB: loudness changes " + juce::String (change, 2) + " dB");
        }

        // Wow & flutter: how far a 3 kHz tone's pitch swings
        for (int speed : { 0, 2 })
        {
            P p; set (p, TapeIDs::speed, (float) speed); set (p, TapeIDs::wow, 100.0f); prepare (p, sr, 512);
            auto in = th::sine (sr, 3000.0, fromDb (-20.0f), 6.0);
            auto out = run (p, in, 512);

            // zero crossings -> period -> speed deviation
            double lastCross = -1.0, worst = 0.0;
            std::vector<double> periods;
            for (int i = (int) sr; i < out.getNumSamples() - 1; ++i)
            {
                const float a = out.getSample (0, i), b = out.getSample (0, i + 1);
                if (a <= 0.0f && b > 0.0f)
                {
                    const double at = i + a / (a - b);
                    if (lastCross > 0.0) periods.push_back (at - lastCross);
                    lastCross = at;
                }
            }
            // average over 8 cycles to see past the sampling
            for (size_t k = 8; k < periods.size(); k += 8)
            {
                double sum = 0.0;
                for (size_t j = k - 8; j < k; ++j) sum += periods[j];
                worst = std::max (worst, std::abs (sum / 8.0 / (sr / 3000.0) - 1.0));
            }
            const double percent = worst * 100.0;
            if (speed == 0) check (percent > 0.03 && percent < 0.3, "15 ips, WOW & FLUTTER 100%: the speed swings up to " + juce::String (percent, 3) + "% (a worn pro machine)");
            else            check (percent > 0.2 && percent < 1.0,  "cassette, WOW & FLUTTER 100%: up to " + juce::String (percent, 2) + "% (an old Walkman)");
        }
    }

    //==========================================================================
    void testChanges()
    {
        std::cout << "\n[changing settings while playing]\n";
        auto in = th::sine (sr, 220.0, fromDb (-12.0f), 4.0);
        P p; prepare (p, sr, 256);
        float worstJump = 0.0f;
        juce::AudioBuffer<float> out (in);
        juce::MidiBuffer midi;
        float last = 0.0f;
        for (int start = 0; start < out.getNumSamples(); start += 256)
        {
            if (start == (int) sr)        set (p, TapeIDs::speed, 1.0f);
            if (start == (int) sr * 2)    set (p, TapeIDs::speed, 2.0f);
            if (start == (int) sr * 2.5)  set (p, TapeIDs::quality, 1.0f);
            if (start == (int) sr * 3)    set (p, TapeIDs::drive, 12.0f);
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, start, 256);
            p.processBlock (view, midi);
            for (int i = 0; i < 256; ++i)
            {
                const float s = out.getSample (0, start + i);
                if (start > 2048) worstJump = std::max (worstJump, std::abs (s - last));
                last = s;
            }
        }
        // A 220 Hz sine at -12 dBFS moves at most about 0.0073 per sample
        check (allFinite (out) && worstJump < 0.02f, "SPEED, OVERSAMPLING and DRIVE changes: no clicks (biggest step " + juce::String (worstJump, 4) + ")");

        P mono; prepare (mono, sr, 512, 1);
        auto mOut = run (mono, th::sine (sr, 110.0, 0.3f, 1.0, 1), 512);
        check (allFinite (mOut) && mOut.getMagnitude (0, mOut.getNumSamples()) > 0.1f, "works on a mono track");

        juce::MemoryBlock state;
        set (p, TapeIDs::speed, 2.0f); set (p, TapeIDs::wow, 40.0f); set (p, CommonIDs::autoGain, 0.0f);
        p.getStateInformation (state);
        P r; r.setStateInformation (state.getData(), (int) state.getSize());
        check (getParam (r, TapeIDs::speed) == 2.0f && getParam (r, TapeIDs::wow) == 40.0f && getParam (r, CommonIDs::autoGain) < 0.5f,
               "saved with the project and loaded back");

        P q;
        const auto report = presetTextsReport (q.getBuiltInPresets());
        check (report.first, report.second);

        juce::StringArray categories;
        for (auto& preset : q.getBuiltInPresets()) if (preset.category.isNotEmpty()) categories.addIfNotAlreadyThere (preset.category);
        check (categories.contains ("Mix Bus") && categories.contains ("Mastering") && ! categories.contains ("Lo-Fi"),
               "full-mix presets in two lists, Mix Bus and Mastering (lo-fi ones inside Mix Bus): " + categories.joinIntoString (", "));
    }

    //==========================================================================
    void testWindow (const juce::File& outDir)
    {
        std::cout << "\n[plugin window]\n";
        P p; prepare (p, sr, 512);
        loadPresetNamed (p, "Drum Bus - Hit the Tape Hard");
        auto loop = drumLoop (sr, 2.0, -6.0f, -9.0f, -20.0f);

        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* window = dynamic_cast<EasyTapeAudioProcessorEditor*> (editor.get());
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

        const auto loudestShown = [&]
        {
            float most = -100.0f;
            for (int start = 0; start + 1600 <= loop.getNumSamples(); start += 1600)
            {
                juce::AudioBuffer<float> chunk (2, 1600);
                for (int ch = 0; ch < 2; ++ch) chunk.copyFrom (ch, 0, loop, ch, start, 1600);
                run (p, chunk, 512);
                window->updateDisplaysNow();
                if (start >= 16000) most = std::max (most, window->getDisplay().getShownVu());   // (after the needle has settled)
            }
            return most;
        };
        const float hot = loudestShown();
        loadPresetNamed (p, "Default settings");
        const float normal = loudestShown();
        check (hot > 3.0f && normal < 0.5f && normal > -6.0f, "the VU needle follows DRIVE: the drums swing it to " + juce::String (normal, 1)
                                                             + " VU at 0 dB (about right for drums), and pins it in the red at +9 dB");
        loadPresetNamed (p, "Drum Bus - Hit the Tape Hard");
        loudestShown();
        savePng (*editor, outDir.getChildFile ("tape.png"));

        window->getInitAllButton().onClick();
        check (getParam (p, TapeIDs::drive) == 0.0f && p.getPresetName() == "Default settings", "INIT: back to 'Default settings'");

        bool hintsOk = true;
        for (int k = 0; k < EasyTapeAudioProcessorEditor::numKnobs; ++k)
        {
            window->mouseEnter (hoverEvent (&window->getKnob (k)));
            if (window->hintBar.getText().length() < 40) hintsOk = false;
        }
        window->mouseEnter (hoverEvent (&window->getSpeedSwitch()));
        if (! window->hintBar.getText().contains ("15 IPS")) hintsOk = false;
        check (hintsOk, "every knob and SPEED explain themselves in the help bar");
    }

    //==========================================================================
    void testSpeed()
    {
       #if ! JUCE_DEBUG
        std::cout << "\n[computer power]\n";
        auto loop = drumLoop (sr, 10.0, -6.0f, -9.0f, -20.0f);
        for (int quality = 1; quality <= 3; ++quality)
        {
            P p; set (p, TapeIDs::quality, (float) quality); set (p, TapeIDs::wow, 50.0f); set (p, TapeIDs::hiss, 20.0f); prepare (p, sr, 256);
            const double share = coreShare (p, loop);
            const double budget = quality == 1 ? 1.0 : (quality == 2 ? 3.0 : 6.0);
            check (share < budget, juce::StringArray { "", "OFF", "4x", "8x" }[quality] + ", stereo, 48 kHz: " + juce::String (share, 3)
                                   + "% of one core (budget " + juce::String (budget, 1) + "%)");
        }
       #endif
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());

    testBasics();
    testResponse();
    testSaturation();
    testLevelAndWow();
    testChanges();
    testWindow (outDir);
    testSpeed();
    return finish();
}
