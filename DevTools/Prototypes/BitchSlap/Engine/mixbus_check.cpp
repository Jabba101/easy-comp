// How loud Bitch-Slap's reverb comes out on full mixes (wet only, MIX 100%),
// relative to the mix itself, for a range of LENGTHs: K-weighted (BS.1770 at
// 48 kHz, ungated, both channels). Used to set the "Mix Bus" presets' MIX.
#include <juce_audio_formats/juce_audio_formats.h>
#include "GatedReverb.h"
#include <cstdio>

static double loudness (const juce::AudioBuffer<float>& b)
{
    struct Biquad { double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
                    double process (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
    double sum = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        Biquad shelf { 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585 };
        Biquad highPass { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 };
        for (int i = 0; i < b.getNumSamples(); ++i) { const double y = highPass.process (shelf.process (b.getSample (ch, i))); sum += y * y; }
    }
    return 10.0 * std::log10 (sum / b.getNumSamples() + 1e-30);
}

int main()
{
    const char* names[] = { "mix_rock", "mix_song", "mix_edm", "mix_jazz", "mix_acoustic", "mix_hiphop", "master_rock", "master_song" };
    const float lengths[] = { 80, 100, 125, 150, 200, 250, 300, 400 };
    juce::WavAudioFormat wav;
    std::printf ("%-14s", "wet vs dry dB");
    for (float l : lengths) std::printf ("%7.0f", l);
    std::printf ("\n");
    std::vector<double> mean (std::size (lengths), 0.0);

    for (auto* name : names)
    {
        const juce::File file (juce::String ("/home/claude/presetcheck/material/") + name + ".wav");
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
        juce::AudioBuffer<float> dry ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&dry, 0, dry.getNumSamples(), 0, true, true);
        const double dryLevel = loudness (dry);
        std::printf ("%-14s", name);

        for (size_t k = 0; k < std::size (lengths); ++k)
        {
            GatedReverb reverb;
            GatedReverb::Settings s; s.lengthMs = lengths[k]; s.mix = 1.0f;
            reverb.prepare (48000.0, 2, s);
            juce::AudioBuffer<float> wet (dry);
            for (int start = 0; start < wet.getNumSamples(); start += 512)
            {
                juce::AudioBuffer<float> view (wet.getArrayOfWritePointers(), 2, start, std::min (512, wet.getNumSamples() - start));
                reverb.process (view, 2);
            }
            const double delta = loudness (wet) - dryLevel;
            mean[k] += delta / (double) std::size (names);
            std::printf ("%7.1f", delta);
        }
        std::printf ("\n");
    }
    std::printf ("%-14s", "average");
    for (double m : mean) std::printf ("%7.1f", m);
    std::printf ("\n");
}
