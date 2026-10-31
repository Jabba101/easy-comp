// Easy Wah: the automatic wah and the plugin around it (not shipped).
#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace th;
using P = EasyWahAudioProcessor;

namespace
{
    constexpr double sr = 48000.0;
    void set (P& p, const char* id, float v)   { setParam (p, id, v); }

    double amplitudeAt (const juce::AudioBuffer<float>& b, int start, int length, double freq)
    {
        double re = 0, im = 0, wsum = 0;
        for (int i = 0; i < length; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (length - 1));
            const double ph = juce::MathConstants<double>::twoPi * freq * i / sr;
            re += w * b.getSample (0, start + i) * std::cos (ph);
            im -= w * b.getSample (0, start + i) * std::sin (ph);
            wsum += w;
        }
        return 2.0 * std::sqrt (re * re + im * im) / wsum;
    }

    // The pedal parked (SENSITIVITY 0), AUTO-GAIN off: the filter's gain (dB) at a frequency
    double parkedGain (float pedalPercent, double hz)
    {
        P p; set (p, WahIDs::sensitivity, 0.0f); set (p, WahIDs::pedal, pedalPercent); set (p, CommonIDs::autoGain, 0.0f);
        prepare (p, sr, 512);
        auto in = th::sine (sr, hz, 0.1f, 0.5);
        auto out = run (p, in, 512);
        const int len = 9600, start = out.getNumSamples() - len;
        return 20.0 * std::log10 (amplitudeAt (out, start, len, hz) / amplitudeAt (in, start, len, hz));
    }

    // Plucked notes: 110 Hz with harmonics, hit every half second, decaying
    juce::AudioBuffer<float> plucks (double seconds, float peakDb)
    {
        const int n = (int) (sr * seconds);
        juce::AudioBuffer<float> b (2, n);
        const double amp = fromDb (peakDb);
        for (int i = 0; i < n; ++i)
        {
            const double t = std::fmod ((double) i / sr, 0.5);
            const double ph = juce::MathConstants<double>::twoPi * 110.0 * i / sr;
            const double x = amp * std::exp (-t / 0.15) * (0.6 * std::sin (ph) + 0.3 * std::sin (2 * ph) + 0.2 * std::sin (3 * ph) + 0.1 * std::sin (5 * ph));
            b.setSample (0, i, (float) x);
            b.setSample (1, i, (float) x);
        }
        return b;
    }

    std::pair<float, float> pedalTravel (P& p, const juce::AudioBuffer<float>& in)
    {
        float lo = 2.0f, hi = -1.0f;
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> out (in);
        for (int start = 0; start < out.getNumSamples(); start += 512)
        {
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, start, std::min (512, out.getNumSamples() - start));
            p.processBlock (view, midi);
            if (start > (int) sr)
            {
                const auto [l, h] = p.takePedalRange();
                lo = std::min (lo, l); hi = std::max (hi, h);
            }
        }
        return { lo, hi };
    }

    //==========================================================================
    void testFilter()
    {
        std::cout << "\n[the wah's filter]\n";
        {
            P p; prepare (p, sr, 512);
            check (p.getLatencySamples() == 0 && getParam (p, CommonIDs::autoGain) > 0.5f && getParam (p, WahIDs::mode) == 0.0f,
                   "no delay added; starts on AUTO, AUTO-GAIN on");
        }
        const double heel400 = parkedGain (0.0f, 400.0), heel2200 = parkedGain (0.0f, 2200.0);
        const double toe400 = parkedGain (100.0f, 400.0), toe2200 = parkedGain (100.0f, 2200.0);
        check (heel400 > heel2200 + 12.0 && toe2200 > toe400 + 12.0, "heel down: 400 Hz peaks (" + juce::String (heel400, 1) + " dB vs "
                   + juce::String (heel2200, 1) + " at 2.2 kHz); toe down: 2.2 kHz peaks (" + juce::String (toe2200, 1) + " vs " + juce::String (toe400, 1) + ")");

        AutoWah::Settings s; s.resonance = 0.6f;
        float worst = 0.0f;
        for (double hz : { 150.0, 500.0, 900.0, 1500.0, 4000.0 })
            worst = std::max (worst, (float) std::abs (parkedGain (50.0f, hz) - AutoWah::responseDb (s, 0.5f, hz, sr)));
        check (worst < 0.5f, "the window's curve matches what you hear (within " + juce::String (worst, 2) + " dB)");

        {
            P p; set (p, WahIDs::mix, 0.0f); prepare (p, sr, 512);
            auto in = drumLoop (sr, 1.0);
            check (maxDifference (run (p, in, 512), in) == 0.0f, "MIX 0%: your sound exactly");
            P q; set (q, CommonIDs::bypass, 1.0f); prepare (q, sr, 512);
            check (maxDifference (run (q, in, 512), in) == 0.0f, "BYPASS: your sound exactly");
        }
    }

    //==========================================================================
    void testMovement()
    {
        std::cout << "\n[the pedal moving]\n";
        const auto notes = plucks (4.0, -8.0f);
        {
            P p; set (p, WahIDs::sensitivity, 100.0f); prepare (p, sr, 512);
            const auto [lo, hi] = pedalTravel (p, notes);
            check (lo < 0.15f && hi > 0.85f, "AUTO, SENSITIVITY 100%: each note kicks the pedal right forward and it falls back ("
                       + juce::String (lo, 2) + " to " + juce::String (hi, 2) + ")");
        }
        {
            P p; set (p, WahIDs::sensitivity, 30.0f); prepare (p, sr, 512);
            const auto [lo, hi] = pedalTravel (p, notes);
            check (hi > 0.2f && hi < 0.75f, "SENSITIVITY 30%: it moves less (up to " + juce::String (hi, 2) + ")");
        }
        {
            P p; set (p, WahIDs::sensitivity, 0.0f); set (p, WahIDs::pedal, 45.0f); prepare (p, sr, 512);
            const auto [lo, hi] = pedalTravel (p, notes);
            check (std::abs (lo - 0.45f) < 0.01f && std::abs (hi - 0.45f) < 0.01f, "SENSITIVITY 0: the pedal stays put where PEDAL says (the cocked wah)");
        }
        {
            P p; set (p, WahIDs::sensitivity, 100.0f); set (p, WahIDs::direction, 1.0f); set (p, WahIDs::pedal, 100.0f); prepare (p, sr, 512);
            const auto [lo, hi] = pedalTravel (p, notes);
            check (hi > 0.95f && lo < 0.2f, "DIRECTION DOWN: playing pushes it from the toe towards the heel");
        }
        {
            P p; set (p, WahIDs::mode, 1.0f); set (p, WahIDs::sensitivity, 80.0f); set (p, WahIDs::note, (float) WahNotes::n4); prepare (p, sr, 512);
            juce::AudioBuffer<float> quiet (2, (int) (sr * 3.0)); quiet.clear();
            const auto [lo, hi] = pedalTravel (p, quiet);
            check (lo < 0.02f && hi > 0.78f, "TEMPO: it rocks back and forth by itself, as far as SENSITIVITY says (" + juce::String (hi, 2) + ")");
        }
        {
            P p; set (p, WahIDs::sensitivity, 100.0f); set (p, WahIDs::response, 0.0f); set (p, WahIDs::resonance, 100.0f); prepare (p, sr, 256);
            auto out = run (p, notes, 256);
            float worst = 0.0f;
            for (int i = 1; i < out.getNumSamples(); ++i) worst = std::max (worst, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
            check (allFinite (out) && worst < 0.15f, "SNAPPY at full RESONANCE: sweeps fast with no clicks or blow-ups (largest step " + juce::String (worst, 3) + ")");
        }
        {
            auto loop = drumLoop (sr, 6.0, -6.0f, -9.0f, -20.0f);
            P p; prepare (p, sr, 512);
            auto out = run (p, loop, 512);
            const double change = loudnessDb (out, (int) sr * 3, out.getNumSamples()) - loudnessDb (loop, (int) sr * 3, loop.getNumSamples());
            P q; set (q, CommonIDs::autoGain, 0.0f); prepare (q, sr, 512);
            auto raw = run (q, loop, 512);
            std::cout << "  drum loop: the wah alone changes the loudness by " << loudnessDb (raw, (int) sr * 3, raw.getNumSamples()) - loudnessDb (loop, (int) sr * 3, loop.getNumSamples())
                      << " dB; AUTO-GAIN adds " << p.getMakeUpDb() << " dB\n";
            check (std::abs (change) < 1.5, "AUTO-GAIN: as loud as it went in (" + juce::String (change, 2) + " dB) - the wah alone would be quieter");
        }
    }

    //==========================================================================
    void testParts (const juce::File& outDir)
    {
        std::cout << "\n[saving, presets, window]\n";
        {
            P p; juce::MemoryBlock state;
            set (p, WahIDs::mode, 1.0f); set (p, WahIDs::from, 250.0f); set (p, WahIDs::response, 2.0f);
            p.getStateInformation (state);
            P r; r.setStateInformation (state.getData(), (int) state.getSize());
            check (getParam (r, WahIDs::mode) == 1.0f && std::abs (getParam (r, WahIDs::from) - 250.0f) < 0.5f && getParam (r, WahIDs::response) == 2.0f,
                   "saved with the project and loaded back");
            const auto report = presetTextsReport (p.getBuiltInPresets());
            check (report.first, report.second);

            P mono; prepare (mono, sr, 512, 1);
            check (allFinite (run (mono, plucks (1.0, -8.0f), 512)), "works on a mono track");
        }

        P p; prepare (p, sr, 512);
        loadPresetNamed (p, "Guitar - Funk Envelope");
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* window = dynamic_cast<EasyWahAudioProcessorEditor*> (editor.get());
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

        const auto notes = plucks (0.6, -8.0f);
        for (int start = 0; start + 1600 <= notes.getNumSamples(); start += 1600)
        {
            juce::AudioBuffer<float> chunk (2, 1600);
            for (int ch = 0; ch < 2; ++ch) chunk.copyFrom (ch, 0, notes, ch, start, 1600);
            run (p, chunk, 512);
            window->updateDisplaysNow();
        }
        check (window->getDisplay().getShownPedal() > 0.3f, "the pedal in the picture moves with the notes");
        savePng (*editor, outDir.getChildFile ("wah.png"));

        set (p, WahIDs::mode, 1.0f);
        window->updateDisplaysNow();
        check (window->getKnob (EasyWahAudioProcessorEditor::noteKnob).isVisible(), "TEMPO: the sweep's note length appears (RESPONSE and DIRECTION step aside)");
        savePng (*editor, outDir.getChildFile ("wah-tempo.png"));

        window->getInitAllButton().onClick();
        check (p.getPresetName() == "Default settings", "INIT: back to 'Default settings'");

        bool hintsOk = true;
        for (int k = 0; k < EasyWahAudioProcessorEditor::numKnobs; ++k)
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
        P p; loadPresetNamed (p, "Guitar - Funk Envelope"); prepare (p, sr, 256);
        const double share = coreShare (p, drumLoop (sr, 10.0));
        check (share < 0.8, "stereo, 48 kHz: " + juce::String (share, 3) + "% of one core");
       #endif
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    testFilter();
    testMovement();
    testParts (outDir);
    testSpeed();
    return finish();
}
