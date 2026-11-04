// Easy Master's resonance tamer on test sounds and real mixes (not shipped).
#include "ResonanceTamer.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <iostream>

static juce::AudioBuffer<float> loadMix (const juce::String& name, double from, double secs)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (juce::File ("/home/claude/presetcheck/mixes/" + name + ".wav").createInputStream().release(), true));
    const int n = (int) (44100 * secs);
    juce::AudioBuffer<float> b (2, n);
    r->read (&b, 0, n, (juce::int64) (44100 * from), true, true);
    return b;
}

struct Result { float averageCut = 0, mostCut = 0; double changeDb = 0; std::array<float, ResonanceTamer::numBands> bandAverage {}; };

static Result run (juce::AudioBuffer<float> in, double sr, ResonanceTamer::Settings s)
{
    ResonanceTamer t; t.prepare (sr, s);
    juce::AudioBuffer<float> b (in);
    Result r; int frames = 0;
    for (int i = 0; i + 512 <= b.getNumSamples(); i += 512)
    {
        juce::AudioBuffer<float> v (b.getArrayOfWritePointers(), 2, i, 512);
        t.process (v);
        if (i > sr * 1.0)
        {
            float sum = 0;
            for (int k = 0; k < ResonanceTamer::numBands; ++k) { sum += t.getBandCutDb (0, k); r.bandAverage[(size_t) k] += t.getBandCutDb (0, k); }
            r.averageCut += sum / ResonanceTamer::numBands; r.mostCut = std::max (r.mostCut, t.getMostCutDb (0)); ++frames;
        }
    }
    r.averageCut /= std::max (1, frames);
    for (auto& v : r.bandAverage) v /= std::max (1, frames);
    double e0 = 0, e1 = 0; const int L = t.getLatencySamples();
    for (int c = 0; c < 2; ++c) for (int i = (int) sr; i + L < b.getNumSamples(); ++i) { e0 += in.getSample (c, i) * in.getSample (c, i); e1 += b.getSample (c, i + L) * b.getSample (c, i + L); }
    r.changeDb = 10 * std::log10 (e1 / e0);
    return r;
}

static void show (const char* name, const Result& r)
{
    std::printf ("%-34s average cut %.2f dB, most %.1f dB, level change %+.2f dB\n   cuts by band: ", name, r.averageCut, r.mostCut, r.changeDb);
    for (int k = 0; k < ResonanceTamer::numBands; k += 2) if (r.bandAverage[(size_t) k] > 0.3f) std::printf ("%.0fHz:%.1f ", 50.0 * std::pow (2.0, k / 6.0), r.bandAverage[(size_t) k]);
    std::printf ("\n");
}

int main (int argc, char** argv)
{
    const double sr = 48000.0;
    ResonanceTamer::Settings s;
    if (argc > 1) s.mid.amountDb = (float) atof (argv[1]);
    if (argc > 2) s.mid.sensitivity = (float) atof (argv[2]);
    if (argc > 3) s.mid.sharpness = (float) atof (argv[3]);
    juce::Random rng (7);
    const int n = (int) (sr * 6);
    // Pink-ish noise
    juce::AudioBuffer<float> pink (2, n);
    { float b0=0,b1=0,b2=0; for (int i = 0; i < n; ++i) { const float w = rng.nextFloat()*2-1; b0 = 0.99765f*b0 + w*0.099046f; b1 = 0.963f*b1 + w*0.2965164f; b2 = 0.57f*b2 + w*1.0526913f; const float v = 0.05f*(b0+b1+b2+w*0.1848f); pink.setSample (0, i, v); pink.setSample (1, i, v); } }
    show ("pink noise", run (pink, sr, s));
    // ... with a ringing resonance at 3 kHz (+12 dB, narrow)
    juce::AudioBuffer<float> res (pink);
    for (int c = 0; c < 2; ++c) { juce::dsp::IIR::Filter<float> f; f.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, 3000.0, 8.0, juce::Decibels::decibelsToGain (12.0f)); for (int i = 0; i < n; ++i) res.setSample (c, i, f.processSample (res.getSample (c, i))); }
    show ("pink + 3 kHz resonance (+12 dB)", run (res, sr, s));
    // A sawtooth chord (A2 C#3 E3): rich harmonics, no resonance
    juce::AudioBuffer<float> saw (2, n);
    for (int i = 0; i < n; ++i) { float v = 0; for (double f : { 110.0, 138.6, 164.8 }) { const double ph = std::fmod (i * f / sr, 1.0); v += (float) (2 * ph - 1) * 0.15f; } saw.setSample (0, i, v + 0.3f * pink.getSample (0, i)); saw.setSample (1, i, v + 0.3f * pink.getSample (0, i)); }
    show ("saw chord + a little noise", run (saw, sr, s));
    for (auto* m : { "band", "mureka" })
    {
        const auto mix = loadMix (m, 20.0, 20.0);
        show (m, run (mix, 44100.0, s));
    }
}
