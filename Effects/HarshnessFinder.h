#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "FastMath.h"

//==============================================================================
/**
    FIND: works out where the harshness in your track is, so FROM and TO can
    be set for you.

    While FIND is on, it listens to your track through a row of filters, a
    third of an octave apart from 2 kHz to 16 kHz (plus one for everything
    below 1.6 kHz), and every 10 ms notes how much came through each one: a
    Frame. That part runs with the audio (see Listener); it's only switched
    on while FIND is listening, so it costs nothing the rest of the time.

    Then analyse() looks through the frames:

      1. It leaves out silence and quiet background noise between phrases
         (anything more than 30 dB below the loudest parts).
      2. It sorts what's left by how much high treble there is compared with
         the low and middle: the top tenth are the harsh moments (the S
         sounds, or the cymbal crashes); the bottom half are the ordinary ones.
      3. It compares their tone, a third of an octave at a time: the harsh
         band is where the harsh moments are strongest, among the parts that
         are at least 6 dB stronger than in the ordinary moments - plus the
         neighbouring thirds that are nearly as strong (within 6 dB).

    FROM is the bottom of that band. TO is its top - or off (everything above
    FROM) if the harshness carries on up past 12 kHz.
*/
namespace HarshnessFinder
{
    constexpr int numBands = 10;   // the thirds of an octave, 2 kHz to 16 kHz
    constexpr std::array<float, numBands> bandCentres { 2000.0f, 2500.0f, 3150.0f, 4000.0f, 5000.0f,
                                                        6300.0f, 8000.0f, 10000.0f, 12500.0f, 16000.0f };
    constexpr float lowCutOffHz = 1600.0f;            // "the low and middle": everything below this
    constexpr float thirdOfAnOctave = 1.12246205f;    // 2 ^ (1/6): from a band's centre to its edges
    constexpr double frameSeconds = 0.01;

    /** 10 ms of your track: the power that came through each filter (1 = 0 dB). */
    struct Frame
    {
        std::array<float, numBands> bands {};
        float low = 0.0f;     // below 1.6 kHz
        float total = 0.0f;   // everything
    };

    //==========================================================================
    /** The part that runs with the audio: the filters, and a Frame every 10 ms. */
    class Listener
    {
    public:
        void prepare (double sampleRate)
        {
            using FastMath::Vec4;
            frameLength = std::max (1, (int) std::lround (frameSeconds * sampleRate));

            // Four filters at a time: { below 1.6k, 2k, 2.5k, 3.15k }, { 4k .. 8k }, { 10k .. 16k, everything }
            const float q = 4.31847f;   // (a third of an octave wide)
            std::array<float, 12> warps {}, ks {}, m0 {}, m1 {}, m2 {};

            for (int lane = 0; lane < 12; ++lane)
            {
                if (lane == 0)
                {
                    warps[0] = warpFor (lowCutOffHz, sampleRate);
                    ks[0] = juce::MathConstants<float>::sqrt2;
                    m2[0] = 1.0f;                     // the low-pass output
                }
                else if (lane <= numBands)
                {
                    warps[(size_t) lane] = warpFor (bandCentres[(size_t) lane - 1], sampleRate);
                    ks[(size_t) lane] = 1.0f / q;
                    m1[(size_t) lane] = 1.0f / q;     // the band-pass output (k x v1: 1 at its centre)
                }
                else
                {
                    warps[(size_t) lane] = warpFor (1000.0f, sampleRate);
                    ks[(size_t) lane] = 1.0f;
                    m0[(size_t) lane] = 1.0f;         // the sound itself
                }
            }

            for (int group = 0; group < 3; ++group)
            {
                float a1[4], a2[4], a3[4];

                for (int i = 0; i < 4; ++i)
                {
                    const auto lane = (size_t) (group * 4 + i);
                    const float g = warps[lane];
                    a1[i] = 1.0f / (1.0f + g * (g + ks[lane]));
                    a2[i] = g * a1[i];
                    a3[i] = g * a2[i];
                }

                auto& c = groups[(size_t) group];
                c.a1 = Vec4::load (a1);
                c.a2 = Vec4::load (a2);
                c.a3 = Vec4::load (a3);
                c.m0 = Vec4::load (m0.data() + group * 4);
                c.m1 = Vec4::load (m1.data() + group * 4);
                c.m2 = Vec4::load (m2.data() + group * 4);
            }

            // (Bands too close to the top of what this sample rate can hold aren't used.)
            for (size_t b = 0; b < (size_t) numBands; ++b)
                usable[b] = bandCentres[b] * thirdOfAnOctave < 0.45f * (float) sampleRate;

            reset();
        }

        void reset() noexcept
        {
            for (auto& c : groups)
                c.s1 = c.s2 = c.sum = FastMath::Vec4::broadcast (0.0f);

            filled = 0;
        }

        /** Which bands this sample rate can measure. */
        bool isUsable (int band) const noexcept    { return usable[(size_t) band]; }

        /** Listens to a block of your track (the channels mixed to one).
            onFrame (const Frame&) is called every 10 ms. */
        template <typename OnFrame>
        void process (const juce::AudioBuffer<float>& audio, OnFrame&& onFrame) noexcept
        {
            using FastMath::Vec4;
            const int numChannels = audio.getNumChannels(), numSamples = audio.getNumSamples();

            if (numChannels == 0)
                return;

            const float scale = 1.0f / (float) numChannels;
            const auto two = Vec4::broadcast (2.0f);

            for (int i = 0; i < numSamples; ++i)
            {
                float mixed = 0.0f;

                for (int ch = 0; ch < numChannels; ++ch)
                    mixed += audio.getReadPointer (ch)[i];

                const auto x = Vec4::broadcast (mixed * scale);

                for (auto& c : groups)
                {
                    const auto v3 = x - c.s2;
                    const auto v1 = c.a1 * c.s1 + c.a2 * v3;
                    const auto v2 = c.s2 + c.a2 * c.s1 + c.a3 * v3;
                    c.s1 = two * v1 - c.s1;
                    c.s2 = two * v2 - c.s2;

                    const auto out = c.m0 * x + c.m1 * v1 + c.m2 * v2;
                    c.sum = c.sum + out * out;
                }

                if (++filled >= frameLength)
                {
                    Frame frame;
                    float sums[12];

                    for (size_t g = 0; g < 3; ++g)
                        groups[g].sum.store (sums + g * 4);

                    const float perSample = 2.0f / (float) filled;   // (x 2: a steady tone reads as its peak level)
                    frame.low = sums[0] * perSample;

                    for (size_t b = 0; b < (size_t) numBands; ++b)
                        frame.bands[b] = sums[b + 1] * perSample;

                    frame.total = sums[11] * perSample;
                    onFrame (static_cast<const Frame&> (frame));

                    for (auto& c : groups)
                        c.sum = Vec4::broadcast (0.0f);

                    filled = 0;
                }
            }
        }

    private:
        static float warpFor (float hz, double sampleRate) noexcept
        {
            return (float) std::tan (juce::MathConstants<double>::pi * std::min ((double) hz, 0.45 * sampleRate) / sampleRate);
        }

        struct Group
        {
            FastMath::Vec4 a1, a2, a3, m0, m1, m2, s1, s2, sum;
        };

        std::array<Group, 3> groups {};
        std::array<bool, numBands> usable {};
        int frameLength = 480, filled = 0;
    };

    //==========================================================================
    struct Result
    {
        enum class Outcome { found, nothingStoodOut, notEnoughSound };

        Outcome outcome = Outcome::notEnoughSound;
        float fromHz = 0.0f;
        float toHz = 0.0f;      // 20000 = off: everything above FROM
        int lowestBand = -1, highestBand = -1;
    };

    constexpr float toOffHz = 20000.0f;
    constexpr int framesNeeded = 200;   // at least 2 seconds of sound to go on

    /** Looks through the frames (see the top of this file). usable (band):
        which bands to consider. */
    template <typename IsUsable>
    Result analyse (const std::vector<Frame>& frames, IsUsable&& usable)
    {
        Result result;
        constexpr float tiny = 1.0e-20f;

        // 1. Leave out silence, and anything more than 30 dB below the loud parts.
        std::vector<float> totals;
        for (auto& f : frames)
            if (f.total > 1.0e-6f)   // (-60 dB)
                totals.push_back (f.total);

        if ((int) totals.size() < framesNeeded)
            return result;

        std::sort (totals.begin(), totals.end());
        const float loud = totals[(size_t) ((double) (totals.size() - 1) * 0.95)];
        const float quietest = std::max (1.0e-6f, loud * 0.001f);

        std::vector<const Frame*> heard;
        for (auto& f : frames)
            if (f.total >= quietest)
                heard.push_back (&f);

        if ((int) heard.size() < framesNeeded)
            return result;

        // 2. Sort by how much high treble (4 kHz up) there is compared with the low and middle.
        const auto trebleOf = [&] (const Frame& f)
        {
            float treble = 0.0f;
            for (int b = 3; b < numBands; ++b)
                if (usable (b))
                    treble += f.bands[(size_t) b];
            return treble / (f.low + tiny);
        };

        std::sort (heard.begin(), heard.end(), [&] (const Frame* a, const Frame* b) { return trebleOf (*a) > trebleOf (*b); });

        const size_t numHarsh = std::max<size_t> (10, heard.size() / 10);
        const size_t firstOrdinary = heard.size() / 2;

        // 3. The tone of each, a third of an octave at a time (in dB, compared with the whole sound).
        std::array<double, numBands> harsh {}, ordinary {};

        for (size_t i = 0; i < heard.size(); ++i)
        {
            const bool isHarsh = i < numHarsh, isOrdinary = i >= firstOrdinary;

            if (! isHarsh && ! isOrdinary)
                continue;

            auto& into = isHarsh ? harsh : ordinary;

            for (size_t b = 0; b < (size_t) numBands; ++b)
                into[b] += 10.0 * std::log10 (((double) heard[i]->bands[b] + tiny) / ((double) heard[i]->total + tiny));
        }

        const double numOrdinary = (double) (heard.size() - firstOrdinary);

        for (size_t b = 0; b < (size_t) numBands; ++b)
        {
            harsh[b] /= (double) numHarsh;
            ordinary[b] /= numOrdinary;
        }

        // The strongest part of the harsh moments, among the parts that stand out from the ordinary ones
        int peak = -1;

        for (int b = 0; b < numBands; ++b)
            if (usable (b) && harsh[(size_t) b] - ordinary[(size_t) b] >= 6.0 && (peak < 0 || harsh[(size_t) b] > harsh[(size_t) peak]))
                peak = b;

        if (peak < 0)
        {
            result.outcome = Result::Outcome::nothingStoodOut;
            return result;
        }

        // ...plus its neighbours that are nearly as strong and still stand out a little
        const auto belongs = [&] (int b)
        {
            return b >= 0 && b < numBands && usable (b)
                && harsh[(size_t) b] >= harsh[(size_t) peak] - 6.0
                && harsh[(size_t) b] - ordinary[(size_t) b] >= 3.0;
        };

        int lowest = peak, highest = peak;
        while (belongs (lowest - 1))   --lowest;
        while (belongs (highest + 1))  ++highest;

        result.outcome = Result::Outcome::found;
        result.lowestBand = lowest;
        result.highestBand = highest;
        result.fromHz = juce::jlimit (1000.0f, 16000.0f, bandCentres[(size_t) lowest] / thirdOfAnOctave);
        result.toHz = bandCentres[(size_t) highest] >= 12500.0f ? toOffHz
                                                                : juce::jlimit (2000.0f, toOffHz, bandCentres[(size_t) highest] * thirdOfAnOctave);
        return result;
    }
}
