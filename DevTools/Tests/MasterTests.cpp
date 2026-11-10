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

    void testOutput()
    {
        std::cout << "\n[OUTPUT]\n";
        const auto band = loadMix ("band", 30.0, 8.0);
        P a; prepare (a, sr, 512); const auto full = run (a, band, 512);
        P b; set (b, MasterIDs::outLevel, -6.0f); prepare (b, sr, 512); const auto down = run (b, band, 512);
        const double d = loudnessDb (down, (int) sr, down.getNumSamples()) - loudnessDb (full, (int) sr, full.getNumSamples());
        check (std::abs (d + 6.0) < 0.05, "OUTPUT LEVEL -6: the master 6 dB down (" + juce::String (d, 2) + " dB)");
        check (std::abs ((b.getReadings().highestPeak - a.getReadings().highestPeak) + 6.0f) < 0.3f, "... and the peak readings follow it");
        check (std::abs (b.takeOutputPeak (0) - 0.0f) >= 0.0f && b.parameters.getParameter (MasterIDs::outLevel)->getNormalisableRange().end <= 0.0f,
               "OUTPUT LEVEL only turns down (so the CEILING always holds)");
        bool inPresets = false;
        for (auto& id : masterPresetIDs()) if (id == MasterIDs::outLevel) inPresets = true;
        check (! inPresets, "OUTPUT LEVEL isn't part of presets");
    }

    void testDither()
    {
        std::cout << "\n[DITHER]\n";
        const double lsb = 1.0 / 32768.0;
        const auto band = [] (const std::vector<float>& x, double lo, double hi)
        {
            // (power in a band, from a plain DFT over a few hundred bins)
            const int n = 8192; double total = 0.0; int count = 0;
            for (int k = (int) (lo * n / sr); k <= (int) (hi * n / sr); k += 4)
            {
                double re = 0, im = 0;
                for (int i = 0; i < n; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * juce::MathConstants<double>::pi * i / n); re += x[(size_t) i] * w * std::cos (2 * juce::MathConstants<double>::pi * k * i / n); im += x[(size_t) i] * w * std::sin (2 * juce::MathConstants<double>::pi * k * i / n); }
                total += re * re + im * im; ++count;
            }
            return 10.0 * std::log10 (total / count + 1e-30);
        };
        const auto run1 = [] (int depth, int shape, std::function<float (int)> input, int n)
        {
            Dither d; d.prepare (sr); d.set (depth, shape);
            juce::AudioBuffer<float> b (2, n);
            for (int i = 0; i < n; ++i) { b.setSample (0, i, input (i)); b.setSample (1, i, input (i)); }
            d.process (b.getArrayOfWritePointers(), 2, n);
            return b;
        };
        {
            // A sine far below the last 16-bit step (-110 dBFS): plain rounding loses it entirely, dither keeps it
            const auto quiet = [&] (int i) { return (float) (std::pow (10.0, -110.0 / 20.0) * std::sin (2 * juce::MathConstants<double>::pi * 1000.0 * i / sr)); };
            const int n = 1 << 17;
            const auto out = run1 (Dither::sixteen, Dither::flat, quiet, n);
            bool steps = true;
            for (int i = 0; i < out.getNumSamples(); ++i) { const double q = out.getSample (0, i) / lsb; if (std::abs (q - std::round (q)) > 1.0e-6) steps = false; }
            check (steps, "16-BIT: every sample lands exactly on a 16-bit step (your DAW writes it untouched)");
            double rms = 0.0; for (int i = 0; i < out.getNumSamples(); ++i) rms += (double) out.getSample (0, i) * out.getSample (0, i);
            rms = 10.0 * std::log10 (rms / out.getNumSamples());
            check (rms > -99.0 && rms < -93.0, "its noise: " + juce::String (rms, 1) + " dBFS (half a 16-bit step: a whisper)");
            const auto at = [&] (double f)
            {
                double re = 0, im = 0;
                for (int i = 0; i < n; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * juce::MathConstants<double>::pi * i / n);
                                              re += out.getSample (0, i) * w * std::cos (2 * juce::MathConstants<double>::pi * f * i / sr);
                                              im += out.getSample (0, i) * w * std::sin (2 * juce::MathConstants<double>::pi * f * i / sr); }
                return 10.0 * std::log10 (re * re + im * im + 1e-30);
            };
            double noise = 0.0; for (int k = 0; k < 24; ++k) noise += std::pow (10.0, at (2100.0 + k * 237.0) / 10.0);
            const double above = at (1000.0) - 10.0 * std::log10 (noise / 24.0);
            check (above > 12.0, "a -110 dBFS tone (below the last 16-bit step, which plain rounding would wipe out) survives, "
                   + juce::String (above, 1) + " dB clear of the noise: fades stay smooth, not gritty");
        }
        {
            const auto zero = [] (int) { return 0.0f; };
            const auto noiseOnly = [&] (int shape)
            {
                // (a little signal so it isn't silence, then the noise it adds)
                const auto tiny = [] (int i) { return (float) (1.0e-7 * ((i % 7) - 3)); };
                const auto out = run1 (Dither::sixteen, shape, tiny, 8192);
                std::vector<float> x (8192); for (int i = 0; i < 8192; ++i) x[(size_t) i] = out.getSample (0, i);
                return std::make_pair (band (x, 2000.0, 5000.0), band (x, 16000.0, 20000.0));
            };
            const auto [flatMid, flatTop] = noiseOnly (Dither::flat);
            const auto [shapedMid, shapedTop] = noiseOnly (Dither::shaped);
            check (std::abs (flatMid - flatTop) < 3.0, "FLAT: the noise is even across the range");
            check (shapedMid < flatMid - 8.0 && shapedTop > shapedMid + 15.0, "SHAPED: " + juce::String (flatMid - shapedMid, 1)
                   + " dB quieter at 2-5 kHz (where the ear is keenest), pushed up to the very top");
            const auto silent = run1 (Dither::sixteen, Dither::shaped, zero, (int) sr);
            bool quietEnd = true;
            for (int i = (int) (sr * 0.5) + 1; i < silent.getNumSamples(); ++i) if (silent.getSample (0, i) != 0.0f) quietEnd = false;
            check (quietEnd, "in digital silence it goes silent too (true silence at a song's start and end)");
        }
        {
            // In Easy Master: OFF changes nothing; 16-BIT is the last step
            const auto mix = loadMix ("band", 30.0, 3.0);
            P a; prepare (a, sr, 512); const auto plain = run (a, mix, 512);
            P b; set (b, MasterIDs::dither, 1.0f); prepare (b, sr, 512); const auto dithered = run (b, mix, 512);
            bool steps = true; double diff = 0.0;
            for (int i = 0; i < dithered.getNumSamples(); ++i)
            {
                const double q = dithered.getSample (0, i) / lsb; if (std::abs (q - std::round (q)) > 1.0e-6) steps = false;
                diff = std::max (diff, (double) std::abs (dithered.getSample (0, i) - plain.getSample (0, i)));
            }
            check (steps && diff < 12.0 * lsb, "DITHER 16-BIT on Easy Master: the master on 16-bit steps, within " + juce::String (diff / lsb, 1) + " steps of the undithered one");
            bool inPresets = false;
            for (auto& id : masterPresetIDs()) if (id == MasterIDs::dither || id == MasterIDs::ditherShape) inPresets = true;
            check (! inPresets, "DITHER isn't part of presets (it's about the file you export)");
        }
    }

    void testInflate()
    {
        std::cout << "\n[INFLATE]\n";
        {
            Inflator f; Inflator::Settings st; st.on = true; st.effect = 1.0f; st.curve = 0.0f; f.setSettings (st);
            bool smooth = true; float last = 0.0f;
            for (int i = 1; i <= 1000; ++i) { const float y = f.shape (i / 1000.0f); if (y < last || y > 1.0001f) smooth = false; last = y; }
            const float lift = 20.0f * std::log10 (f.shape (0.05f) / 0.05f);
            check (smooth && std::abs (f.shape (1.0f) - 1.0f) < 1.0e-4f, "the curve rises smoothly to exactly full scale (never past it)");
            check (lift > 3.0f && lift < 3.8f, "at 100%, quiet parts lifted by " + juce::String (lift, 1) + " dB");
            st.curve = -50.0f; f.setSettings (st); const float soft = 20.0f * std::log10 (f.shape (0.05f) / 0.05f);
            st.curve = 50.0f; f.setSettings (st); const float big = 20.0f * std::log10 (f.shape (0.05f) / 0.05f);
            check (soft < lift - 1.0f && big > lift + 1.0f, "CURVE: -50 lifts the quiet parts less (" + juce::String (soft, 1) + " dB), +50 more (" + juce::String (big, 1) + " dB)");
        }
        {
            // Clean: a loud 9 kHz tone at 100%: its harmonics fold back far below it (worked at four times the rate)
            Inflator f; Inflator::Settings st; st.on = true; st.effect = 1.0f; f.setSettings (st); f.prepare (sr, 512);
            const int n = 1 << 15;
            juce::AudioBuffer<float> b (2, n);
            for (int i = 0; i < n; ++i) { const float x = 0.9f * (float) std::sin (2 * juce::MathConstants<double>::pi * 9000.0 * i / sr); b.setSample (0, i, x); b.setSample (1, i, x); }
            for (int s0 = 0; s0 < n; s0 += 512) { juce::AudioBuffer<float> part (b.getArrayOfWritePointers(), 2, s0, 512); f.process (part); }
            const auto at = [&] (double hz)
            {
                double re = 0, im = 0; const int from = n / 2, len = n - from;
                for (int i = from; i < n; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * juce::MathConstants<double>::pi * (i - from) / len);
                                                 re += b.getSample (0, i) * w * std::cos (2 * juce::MathConstants<double>::pi * hz * i / sr);
                                                 im += b.getSample (0, i) * w * std::sin (2 * juce::MathConstants<double>::pi * hz * i / sr); }
                return 20.0 * std::log10 (std::sqrt (re * re + im * im) + 1e-20);
            };
            const double tone = at (9000.0), folded = std::max (at (21000.0), at (3000.0));   // (27 and 45 kHz would fold to 21 and 3 kHz)
            check (tone - folded > 70.0, "a loud 9 kHz tone at 100%: what folds back is " + juce::String (tone - folded, 1) + " dB below it (clean)");
        }
        const auto band = loadMix ("band", 30.0, 8.0);
        {
            P off; prepare (off, sr, 512); const auto a = run (off, band, 512);
            P on; set (on, MasterInflateIDs::on, 1.0f); set (on, MasterInflateIDs::effect, 100.0f); prepare (on, sr, 512); const auto b = run (on, band, 512);
            const double lufs = loudnessDb (b, (int) sr, b.getNumSamples()) - loudnessDb (a, (int) sr, a.getNumSamples());
            check (lufs > 1.0 && on.getReadings().highestPeak <= off.getReadings().highestPeak + 0.2f,
                   "on a mix at 100%: " + juce::String (lufs, 1) + " dB louder, the peaks no higher (the limiter after it holds them)");
            check (on.getLatencySamples() == off.getLatencySamples(), "the same delay on or off (no jump when it's switched)");
        }
        {
            // Switching it on while it plays: a 20 ms crossfade, no click
            P p; set (p, MasterInflateIDs::effect, 60.0f); prepare (p, sr, 512);
            const auto tone = [&] { juce::AudioBuffer<float> t (2, (int) sr * 2); for (int i = 0; i < t.getNumSamples(); ++i) { const float x = 0.2f * (float) std::sin (2 * juce::MathConstants<double>::pi * 110.0 * i / sr); t.setSample (0, i, x); t.setSample (1, i, x); } return t; }();
            juce::AudioBuffer<float> out (tone);
            juce::MidiBuffer midi;
            for (int s0 = 0; s0 < out.getNumSamples(); s0 += 512)
            {
                if (s0 == (int) sr) set (p, MasterInflateIDs::on, 1.0f);
                juce::AudioBuffer<float> part (out.getArrayOfWritePointers(), 2, s0, std::min (512, out.getNumSamples() - s0));
                p.processBlock (part, midi);
            }
            float switching = 0.0f, settled = 0.0f;
            for (int i = (int) sr / 2; i < out.getNumSamples(); ++i)
            {
                const float step = std::abs (out.getSample (0, i) - out.getSample (0, i - 1));
                if (i < (int) (sr * 1.2)) switching = std::max (switching, step); else settled = std::max (settled, step);
            }
            check (switching <= std::max (settled, 0.003f) * 1.05f, "switching INFLATE on while it plays: no click");
        }
        {
            // (a picture: INFLATE on, DITHER 16-BIT)
            P p; set (p, MasterInflateIDs::on, 1.0f); set (p, MasterInflateIDs::effect, 35.0f); set (p, MasterInflateIDs::curve, -10.0f);
            set (p, MasterIDs::dither, 1.0f); set (p, MasterWidthIDs::on, 1.0f); set (p, MasterWidthIDs::width, 115.0f);
            prepare (p, sr, 512);
            run (p, band, 512);
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            auto& console = dynamic_cast<EasyMasterAudioProcessorEditor*> (editor.get())->getConsole();
            for (int i = 0; i < 20; ++i) { run (p, loadMix ("band", 31.0 + i * 0.2, 0.2), 512); console.updateNow(); }
            savePng (console, juce::File ("/tmp/claude-0/-home-claude/b1f7da22-616f-5bd8-9beb-5177ec950132/scratchpad/shots/master_inflate_dither.png"));
        }
    }

    void testVintage()
    {
        std::cout << "\n[VINTAGE: PULTEC EQ and VARI-MU]\n";
        {
            PultecEQ eq; PultecEQ::Settings st; st.on = true; st.lowFreq = 2; st.lowBoost = 5; st.lowAtten = 4; eq.setSettings (st); eq.prepare (sr);
            const float at60 = eq.responseDb (60.0), at40 = eq.responseDb (40.0), at200 = eq.responseDb (200.0), at400 = eq.responseDb (400.0);
            check (at40 > 2.0f && at200 < -0.5f && std::abs (eq.responseDb (5000.0)) < 0.3f,
                   "the low-end trick (60 Hz, BOOST 5 + ATTEN 4): +" + juce::String (at40, 1) + " dB at 40 Hz, " + juce::String (at60, 1) + " at 60, "
                   + juce::String (at200, 1) + " at 200, " + juce::String (at400, 1) + " at 400 - weight with the mud scooped");
            st = {}; st.on = true; st.highFreq = 5; st.highBoost = 3; st.bandwidth = 7; st.attenFreq = 2; st.highAtten = 2; eq.setSettings (st);
            check (eq.responseDb (12000.0) > 3.0f && std::abs (eq.responseDb (500.0)) < 0.5f, "Air & Silk: +" + juce::String (eq.responseDb (12000.0), 1)
                   + " dB at 12k, the mids untouched");
            // OFF: untouched
            PultecEQ offEq; offEq.prepare (sr);
            auto tone = loadMix ("band", 30.0, 1.0); auto copy = tone;
            offEq.process (copy);
            check (worstDelayedDifference (copy, tone, 0) == 0.0f, "PULTEC off: the sound untouched, bit for bit");
        }
        {
            check (std::abs (VariMu::reductionFor (-10.0f)) < 1.0e-6f, "VARI-MU: nothing at all 10 dB under the threshold");
            const float r10 = 10.0f / (10.0f - VariMu::reductionFor (10.0f)), r20 = 20.0f / (20.0f - VariMu::reductionFor (20.0f));
            check (r10 > 3.0f && r10 < 5.0f && r20 > 5.5f && r20 < 7.5f, "the ratio grows as it's pushed: " + juce::String (r10, 1) + ":1 at 10 dB over, "
                   + juce::String (r20, 1) + ":1 at 20 (very soft, smooth)");
            const auto band = loadMix ("band", 30.0, 8.0);
            P p; set (p, MasterVariMuIDs::on, 1.0f); set (p, MasterVariMuIDs::threshold, -20.0f); prepare (p, sr, 512);
            run (p, band, 512);
            const float gr = p.getVariMuReductionDb();
            check (gr > 0.5f && gr < 12.0f, "on a mix (threshold -20): turning down " + juce::String (gr, 1) + " dB");
            for (auto& mixName : { "band", "mureka" })
            {
                std::cout << "  (" << mixName << ", input loudness " << juce::String (loudnessDb (loadMix (mixName, 30.0, 8.0), 0, (int) (sr * 8)) - 0.691, 1) << " LUFS):";
                for (auto& pr : MasterModules::presets (MasterModules::varimu))
                {
                    P t; t.loadPreset (pr); prepare (t, sr, 512);
                    const auto m = loadMix (mixName, 30.0, 8.0);
                    float most = 0.0f, sum = 0.0f; int count = 0;
                    for (int s0 = 0; s0 + 4800 <= m.getNumSamples(); s0 += 4800)
                    {
                        juce::AudioBuffer<float> part (2, 4800); for (int c = 0; c < 2; ++c) part.copyFrom (c, 0, m, c, s0, 4800);
                        juce::MidiBuffer midi; t.processBlock (part, midi);
                        if (s0 > (int) sr * 2) { most = std::max (most, t.getVariMuReductionDb()); sum += t.getVariMuReductionDb(); ++count; }
                    }
                    std::cout << " [" << pr.name << ": " << juce::String (sum / count, 1) << " avg / " << juce::String (most, 1) << " max]";
                }
                std::cout << "\n";
            }
            P q; set (q, MasterVariMuIDs::on, 1.0f); set (q, MasterVariMuIDs::threshold, -20.0f); set (q, MasterVariMuIDs::mix, 0.0f); prepare (q, sr, 512);
            P plain; prepare (plain, sr, 512);
            check (worstDelayedDifference (run (q, band, 512), run (plain, band, 512), 0) < 1.0e-5f, "MIX at 0%: the mix untouched");
        }
        {
            // The VINTAGE page
            P p; set (p, MasterPultecIDs::on, 1.0f); set (p, MasterPultecIDs::lowBoost, 4.0f); set (p, MasterPultecIDs::lowAtten, 3.0f);
            set (p, MasterPultecIDs::highBoost, 2.5f); set (p, MasterPultecIDs::highAtten, 1.5f);
            set (p, MasterVariMuIDs::on, 1.0f); set (p, MasterVariMuIDs::threshold, -20.0f); set (p, MasterVariMuIDs::makeup, 1.5f);
            prepare (p, sr, 512);
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            auto& console = dynamic_cast<EasyMasterAudioProcessorEditor*> (editor.get())->getConsole();
            console.showBay (2);
            for (int i = 0; i < 20; ++i) { run (p, loadMix ("band", 31.0 + i * 0.2, 0.2), 512); console.updateNow(); }
            check (console.getModule (MasterModules::pultec)->isVisible() && console.getModule (MasterModules::varimu)->isVisible()
                   && ! console.getModule (MasterModules::eq)->isVisible(), "VINTAGE shows the PULTEC EQ and the VARI-MU in the big bay");
            savePng (console, juce::File ("/tmp/claude-0/-home-claude/b1f7da22-616f-5bd8-9beb-5177ec950132/scratchpad/shots/master_vintage.png"));
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
        check (window != nullptr && editor->getWidth() == 1699 && editor->getHeight() == 927, "the window opens at 1699 x 927 (fits a 1080p screen; drag the corner for any size)");
        if (window == nullptr) return;
        auto& console = window->getConsole();
        using C = MasterHolo::Console;
        check (console.getWidth() == C::width && console.getHeight() == C::height, "the console is laid out at 1888 x 1030 (INPUT on the left, OUTPUT on the right, like Easy Strip) and scaled to the window");

        // SETUP: the size and the brightness
        {
            const auto settingsFile = juce::File::createTempFile (".settings");
            SoloSettings::useFileForTesting (settingsFile);
            check (console.setup.isVisible() && console.setup.getRight() <= C::width - 12 && console.setup.getX() >= console.bypass.getRight() + 8,
                   "SETUP sits at the end of the header, after BYPASS");
            console.chooseSize (0.75f);
            check (editor->getWidth() == juce::roundToInt (C::width * 0.75f) && std::abs (p.windowScale - 0.75f) < 0.01f, "SETUP > SIZE 75% resizes the window (and is remembered with the project)");
            console.chooseSize (0.9f);
            check (editor->getWidth() == 1699, "... and back to 90%");
            console.setBrightness (0);
            check (console.getBrightness() == 0 && SoloSettings::load().dim == 0, "DISPLAY BRIGHTNESS is remembered for every Easy plugin");
            console.setBrightness (2);
            SoloSettings::useFileForTesting ({});
            settingsFile.deleteFile();
        }

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
            const auto isTab = [&] (juce::Component* c) { return c == console.bayTabs[0].get() || c == console.bayTabs[1].get() || c == console.bayTabs[2].get(); };
            const auto isBay = [&] (juce::Component* c) { return c == console.getModule (MasterModules::eq) || c == console.getModule (MasterModules::multiband); };
            if (! checkInside (console, [&] (juce::Component* a, juce::Component* b2) { return (isTab (a) && isBay (b2)) || (isTab (b2) && isBay (a)); })) return false;
            for (auto& m : console.getModules())
                if (m->isVisible() && ! checkInside (*m, [] (juce::Component*, juce::Component*) { return false; })) return false;
            return true;
        };
        const char* names[C::numFocus] { "tamer", "eq", "multiband", "comp", "punch", "width", "reference" };
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
                                      || id == MasterPunchIDs::on || id == MasterPunchIDs::mode || id == MasterWidthIDs::on || id == MasterInflateIDs::on || id == MasterPultecIDs::on || id == MasterVariMuIDs::on || id == MasterVariMuIDs::time || id == MasterPultecIDs::lowFreq
                                      || id == MasterPultecIDs::highFreq || id == MasterPultecIDs::attenFreq || (id.startsWith ("mb") && id.endsWith ("on"));
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

    //==========================================================================
    // REFERENCE
    struct StoppedHead : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override { PositionInfo i; i.setIsPlaying (playing); return i; }
        bool playing = false;
    };

    bool waitForReference (P& p)
    {
        for (int i = 0; i < 400 && p.getReference().getStatus() == ReferenceTrack::loading; ++i) juce::Thread::sleep (25);
        return p.getReference().getStatus() == ReferenceTrack::ready;
    }

    juce::AudioBuffer<float> runBlocks (P& p, const juce::AudioBuffer<float>& in, int block, std::function<void (int)> beforeBlock = {})
    {
        juce::AudioBuffer<float> out (in);
        juce::MidiBuffer midi;
        for (int s = 0, k = 0; s < out.getNumSamples(); s += block, ++k)
        {
            if (beforeBlock) beforeBlock (k);
            const int n = std::min (block, out.getNumSamples() - s);
            juce::AudioBuffer<float> part (out.getArrayOfWritePointers(), 2, s, n);
            p.processBlock (part, midi);
        }
        return out;
    }

    void testReference (const juce::File& outDir)
    {
        std::cout << "\n[reference]\n";
        const auto band = loadMix ("band", 30.0, 20.0);
        // The reference: another mix, written as a 44.1 kHz WAV, 3 dB hotter than it was (a "louder commercial master")
        auto song = loadMix ("mureka", 20.0, 30.0);
        song.applyGain (juce::Decibels::decibelsToGain (3.0f));
        for (int c = 0; c < 2; ++c) for (int i = 0; i < song.getNumSamples(); ++i) song.setSample (c, i, juce::jlimit (-1.0f, 1.0f, song.getSample (c, i)));
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("easy_master_reference_test.wav");
        {
            file.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (file.createOutputStream().release(), sr, 2, 24, {}, 0));
            w->writeFromAudioSampleBuffer (song, 0, song.getNumSamples());
        }
        const double songLufs = loudnessDb (song, 0, song.getNumSamples()) - 0.691;

        {
            P p; prepare (p, 48000.0, 512);
            p.loadReference (file);
            const bool ok = waitForReference (p);
            auto& ref = p.getReference();
            check (ok && std::abs (ref.getLengthSeconds() - 30.0) < 0.01, "a 44.1 kHz WAV loads and is converted for a 48 kHz session (" + juce::String (ref.getLengthSeconds(), 3) + " s)");
            check (std::abs (ref.getSectionLufs() - (float) songLufs) < 1.0f, "its loudness is measured: " + juce::String (ref.getSectionLufs(), 1) + " LUFS (the test's own: "
                   + juce::String (songLufs, 1) + ")");
        }

        P p; prepare (p, sr, 512);
        p.loadReference (file);
        waitForReference (p);
        auto& ref = p.getReference();
        {
            // Nothing changes until you switch to it
            P plain; prepare (plain, sr, 512);
            const auto a = runBlocks (plain, band, 512), b = runBlocks (p, band, 512);
            check (worstDelayedDifference (b, a, 0) < 1.0e-6f, "loaded but not switched on: your master is untouched");
        }
        {
            // REF on: the reference, as loud as your master
            const auto ours = runBlocks (p, band, 512);   // (the last 20 s measured)
            const double ourLevel = loudnessDb (ours, (int) (sr * 10), ours.getNumSamples());
            ref.setListening (true);
            const auto heard = runBlocks (p, band, 512);
            const double refLevel = loudnessDb (heard, (int) (sr * 10), heard.getNumSamples());
            check (std::abs (refLevel - ourLevel) < 1.0, "REF on, MATCH LOUDNESS: the reference plays as loud as your master (" + juce::String (refLevel - ourLevel, 2)
                   + " dB apart; turned " + juce::String (ref.getGainDb(), 1) + " dB)");
            // ... and it really is the reference, not your mix
            juce::AudioBuffer<float> silence (2, band.getNumSamples()); silence.clear();
            ref.setListening (true);
            const auto onlyRef = runBlocks (p, silence, 512);
            check (rms (onlyRef, 0, (int) sr, onlyRef.getNumSamples()) > 0.01f, "with your mix silent, REF still plays the reference");
            ref.setMatching (false);
            ref.setListening (true);
            const auto asIs = runBlocks (p, silence, 512);
            const double asIsLevel = loudnessDb (asIs, (int) (sr * 2), asIs.getNumSamples()) - 0.691;
            const double songPart = loudnessDb (song, (int) (sr * (2.0 + 20.0)), song.getNumSamples()) - 0.691;   // (where playback has got to: 20-40 s... wrapped)
            juce::ignoreUnused (songPart);
            check (std::abs (asIsLevel - songLufs) < 1.5, "MATCH LOUDNESS off: the reference at its own level (" + juce::String (asIsLevel, 1) + " vs " + juce::String (songLufs, 1) + " LUFS)");
            ref.setMatching (true);
        }
        {
            // Switching back and forth while it plays: no clicks
            ref.setListening (false);
            const auto out = runBlocks (p, band, 512, [&] (int k) { if (k > 0 && k % 40 == 0) ref.setListening (! ref.isListening()); });
            float worstSwitch = 0.0f, worstNormal = 0.0f;
            for (int k = 40; k * 512 < out.getNumSamples() - 4096; k += 40) worstSwitch = std::max (worstSwitch, biggestJump (out, k * 512, k * 512 + 2048));
            worstNormal = biggestJump (out, 0, out.getNumSamples());
            check (worstSwitch <= worstNormal * 1.0001f && worstSwitch < 0.5f, "switching REF on and off while it plays: a 25 ms fade, no click (biggest step at a switch "
                   + juce::String (worstSwitch, 3) + ", in the music itself " + juce::String (worstNormal, 3) + ")");
        }
        {
            // A part of the song, looped: the seam fades - no click
            juce::AudioBuffer<float> silence (2, (int) (sr * 9)); silence.clear();
            p.setReferenceSection (5.0, 7.0);
            ref.setMatching (false);
            ref.setListening (true);
            const auto out = runBlocks (p, silence, 512);
            // (the reference's own biggest step in that part, for comparison)
            const float own = biggestJump (song, (int) (sr * 5.0), (int) (sr * 7.0));
            float seam = 0.0f;
            for (double t = 2.0; t < 8.9; t += 2.0) { const int i = (int) (sr * t); seam = std::max (seam, biggestJump (out, i - 600, i + 600)); }
            check (seam <= own * 1.05f + 1.0e-4f, "looping 0:05 to 0:07: no click where it goes round (" + juce::String (seam, 3) + ", the song's own " + juce::String (own, 3) + ")");
            const double partLufs = loudnessDb (song, (int) (sr * 5.0), (int) (sr * 7.0)) - 0.691;
            check (std::abs (ref.getSectionLufs() - (float) partLufs) < 1.5f, "the part's own loudness is what's matched: " + juce::String (ref.getSectionLufs(), 1)
                   + " LUFS (whole song " + juce::String (songLufs, 1) + ")");
            ref.setMatching (true);
        }
        {
            // The DAW stopped: silence (and every press of play starts at START)
            StoppedHead head; p.setPlayHead (&head);
            ref.setListening (true);
            const auto out = runBlocks (p, band, 512);
            check (rms (out, 0, (int) (sr * 0.1), out.getNumSamples()) < 1.0e-6f && rms (out, 1, (int) (sr * 0.1), out.getNumSamples()) < 1.0e-6f, "DAW stopped: the reference is silent");
            head.playing = true;
            runBlocks (p, loadMix ("band", 30.0, 0.5), 512);
            check (std::abs (ref.getPositionSeconds() - 5.5) < 0.05, "pressing play starts it at START (0:05): " + juce::String (ref.getPositionSeconds(), 2) + " s after half a second");
            p.setPlayHead (nullptr);
        }
        {
            // Saved with the project (but never in presets or A / B, and it never starts switched on)
            juce::MemoryBlock state; p.getStateInformation (state);
            P r; prepare (r, sr, 512); r.setStateInformation (state.getData(), (int) state.getSize());
            const bool ok = waitForReference (r);
            check (ok && r.getReference().getFile() == file && std::abs (r.getReference().getStart() - 5.0) < 0.01 && std::abs (r.getReference().getEnd() - 7.0) < 0.01
                   && ! r.getReference().isListening(), "saved with the project: the song and the part come back (switched to your master)");
            bool inPresets = false;
            for (auto& id : masterPresetIDs()) if (id.startsWith ("ref")) inPresets = true;
            check (! inPresets, "... and it isn't part of any preset");
        }
        {
            // A file that isn't audio: a plain message, nothing breaks
            P bad; prepare (bad, sr, 512);
            const auto junk = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("not_a_song.wav");
            junk.replaceWithText ("hello");
            bad.loadReference (junk);
            waitForReference (bad);
            check (bad.getReference().getStatus() == ReferenceTrack::failed && bad.getReference().getError().isNotEmpty(),
                   "a file that isn't audio: '" + bad.getReference().getError() + "'");
            junk.deleteFile();
        }

        // The window: the REFERENCE page and COMPARE's REF switch
        p.setReferenceSection (0.0, 0.0);
        ref.setListening (false);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto& console = dynamic_cast<EasyMasterAudioProcessorEditor*> (editor.get())->getConsole();
        using C = MasterHolo::Console;
        console.showFocus (C::refFocus);
        for (int i = 0; i < 60; ++i) { run (p, loadMix ("band", 30.0 + i * 0.2, 0.2), 512); console.updateNow(); }
        auto& view = console.referenceDisplay;
        check (view.getReferencePicture().size() == (size_t) BandSpectrum::numBands && view.getMasterPicture().size() == (size_t) BandSpectrum::numBands,
               "the REFERENCE page draws both pictures");
        std::cout << "  (it says: " << view.describeDifference() << ")\n";
        check (view.describeDifference().isNotEmpty(), "... and names the biggest differences in words");
        savePng (console, outDir.getChildFile ("master_reference.png"));
        // Dragging across the overview chooses a part
        {
            const auto o = view.getLocalBounds().toFloat().reduced (14.0f, 8.0f);
            const float y = o.getBottom() - 40.0f, x0 = o.getX() + o.getWidth() * 0.4f, x1 = o.getX() + o.getWidth() * 0.6f;
            view.mouseDown (mouseAt (&view, { x0, y }, true));
            view.mouseDrag (mouseAt (&view, { x1, y }, true));
            view.mouseUp (mouseAt (&view, { x1, y }, false));
            check (std::abs (ref.getStart() - 12.0) < 0.5 && std::abs (ref.getEnd() - 18.0) < 0.5, "dragging across the song chooses a part: "
                   + juce::String (ref.getStart(), 1) + " to " + juce::String (ref.getEnd(), 1) + " s");
        }
        // A song dropped anywhere on the window loads as the reference, and the page opens
        {
            console.showFocus (C::compFocus);
            const auto other = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("easy_master_dropped.wav");
            file.copyFileTo (other);
            check (console.isInterestedInFileDrag ({ other.getFullPathName() }) && ! console.isInterestedInFileDrag ({ "notes.txt" }), "the window takes a dragged song (and ignores other files)");
            console.filesDropped ({ other.getFullPathName() }, 300, 700);
            const bool ok = waitForReference (p);
            check (ok && p.getReference().getFile() == other && console.getFocus() == C::refFocus, "dropping it on the EQ: loaded as the reference, the REFERENCE page shows");
            p.loadReference (file); waitForReference (p);
            other.deleteFile();
        }
        // COMPARE's REF: switches to the reference, lit
        auto* compare = console.getModules().back().get();
        Holo::Button* refButton = nullptr;
        for (auto* c : compare->getChildren()) if (auto* b = dynamic_cast<Holo::Button*> (c)) if (b->getButtonText() == "REF") refButton = b;
        check (refButton != nullptr, "COMPARE has a REF button");
        if (refButton != nullptr)
        {
            refButton->onClick();
            for (int i = 0; i < 4; ++i) { run (p, loadMix ("band", 40.0 + i * 0.2, 0.2), 512); console.updateNow(); }
            check (ref.isListening() && refButton->getToggleState(), "clicking REF: you hear the reference, and REF is lit");
            savePng (console, outDir.getChildFile ("master_reference_listening.png"));
            refButton->onClick();
            console.updateNow();
            check (! ref.isListening() && ! refButton->getToggleState(), "clicking it again: back to your master");
        }
        file.deleteFile();
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
    testOutput();
    testDither();
    testInflate();
    testVintage();
    testCompare();
    testTamer();
    testEQ();
    testMultiband();
    testPunchWidth();
    testParts (outDir);
    testReference (outDir);
    testSpeed();
    return finish();
}
