#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cstdint>

//==============================================================================
/**
    DITHER: the very last step before the master is written to a 16-bit (or
    24-bit) file.

    Cutting a master down to 16 bits throws away the finest detail: on
    fades and quiet tails that shows up as a gritty, buzzing distortion.
    Dither adds a tiny amount of noise first (about -96 dBFS at 16 bits),
    which turns that distortion into a smooth, steady hiss far below the
    music - the fades stay clean all the way down. Then it rounds every
    sample to the exact 16-bit value, so your DAW writes it without
    touching it again.

    FLAT: plain triangular (TPDF) noise - the textbook, completely neutral
    choice. SHAPED: the same noise moved up into the very top of the range,
    where the ear is least sensitive (about 15 dB quieter where it matters
    most); best for a final 16-bit CD / streaming master, as long as nothing
    changes the level after it.

    It goes quiet in silence: after half a second of digital silence the
    noise fades out, so songs start and end on true silence. OFF passes the
    sound through untouched.
*/
class Dither
{
public:
    enum Depth { off = 0, sixteen, twentyFour };
    enum Shape { flat = 0, shaped };

    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate;
        silentLimit = (int) (sampleRate * 0.5);
        channels[0] = Channel { {}, 0x9e3779b9u, 0 };
        channels[1] = Channel { {}, 0x7f4a7c15u, 0 };
    }

    /** depth and shape from any thread; they're read once per block. */
    void set (int depthIndex, int shapeIndex) noexcept   { depth.store (depthIndex); shape.store (shapeIndex); }

    void process (float* const* audio, int numChannels, int numSamples) noexcept
    {
        const int d = depth.load();
        if (d == off) return;
        const double step = d == sixteen ? 1.0 / 32768.0 : 1.0 / 8388608.0;
        const bool noiseShaped = shape.load() == shaped;

        for (int ch = 0; ch < juce::jmin (numChannels, 2); ++ch)
        {
            auto& c = channels[(size_t) ch];
            float* x = audio[ch];
            for (int i = 0; i < numSamples; ++i)
            {
                // Silence in: after half a second, silence out (no hiss on a song's start and end)
                if (std::abs (x[i]) < 1.0e-30f) { if (c.silentFor < silentLimit) ++c.silentFor; }
                else c.silentFor = 0;
                if (c.silentFor >= silentLimit) { c.error = {}; continue; }

                // SHAPED: the last rounding errors fed back, pushing the noise up to the top of the range
                double target = x[i];
                if (noiseShaped)
                    target -= step * (shaping[0] * c.error[0] + shaping[1] * c.error[1] + shaping[2] * c.error[2]);

                // TPDF noise of +-1 step, then round to the nearest step
                const double noise = (random (c) - random (c)) * step;
                double q = std::round ((target + noise) / step) * step;
                q = juce::jlimit (-1.0, 1.0 - step, q);

                c.error[2] = c.error[1]; c.error[1] = c.error[0];
                c.error[0] = (q - target) / step;   // (in steps)
                x[i] = (float) q;
            }
        }
    }

private:
    struct Channel
    {
        std::array<double, 3> error {};
        uint32_t seed = 0x9e3779b9u;
        int silentFor = 0;
    };

    /** 0..1 (a fast, good-enough generator; each channel its own, so left and right aren't the same noise). */
    static double random (Channel& c) noexcept
    {
        c.seed ^= c.seed << 13; c.seed ^= c.seed >> 17; c.seed ^= c.seed << 5;
        return (double) c.seed / 4294967296.0;
    }

    // (a gentle, well-known 3-tap shaping curve: the noise falls away where the ear is most sensitive, 2-5 kHz)
    static constexpr double shaping[3] { 1.623, -0.982, 0.109 };

    double rate = 48000.0;
    int silentLimit = 24000;
    std::atomic<int> depth { off }, shape { shaped };
    std::array<Channel, 2> channels { Channel { {}, 0x9e3779b9u, 0 }, Channel { {}, 0x7f4a7c15u, 0 } };
};
