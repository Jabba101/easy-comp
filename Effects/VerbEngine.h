#pragma once

#include "GatedReverb.h"
#include "NaturalReverb.h"

//==============================================================================
/**
    Easy Verb's sound: the TYPE switch between the gated burst (BITCH-SLAP,
    Effects/GatedReverb.h) and the natural spaces (ROOM, CLUB, HALL, PLATE,
    Effects/NaturalReverb.h), with MIX and BYPASS working the same way for
    all of them.

      MIX   0% = dry ... 50% = your sound and the reverb both full ...
            100% = reverb only (the same as Bitch-Slap always was).

    Changing TYPE crossfades from the old reverb to the new over 30 ms, so it
    never clicks (the old tail fades away with it). No delay is added to your
    sound.
*/
class VerbEngine
{
public:
    enum class Type { bitchSlap = 0, room = 1, club = 2, hall = 3, plate = 4 };
    static constexpr int numTypes = 5;

    struct Settings
    {
        Type  type       = Type::bitchSlap;
        float lengthMs   = 300.0f;   // BITCH-SLAP
        float decay      = 1.2f;     // the others (seconds)...
        float preDelayMs = 15.0f;
        float tone       = 0.0f;     // -1 .. +1
        float mix        = 0.35f;
        bool  bypassed   = false;
    };

    using BlockResult = GatedReverb::BlockResult;

    static bool isNatural (Type t) noexcept   { return t != Type::bitchSlap; }

    //==========================================================================
    void prepare (double sampleRate, int numOutputChannels, const Settings& initial)
    {
        settings = initial;
        current = initial.type;
        fadeLength = juce::jmax (1, (int) (0.03 * sampleRate));
        fadeLeft = 0;
        started = false;

        gated.prepare (sampleRate, numOutputChannels, gatedSettings (initial));
        for (auto& n : natural)
            n.prepare (sampleRate);
        active = 0;
        natural[0].setSettings (naturalSettings (initial, initial.type), true);

        for (auto& b : { &scratch, &wet })
            b->setSize (2, chunk);
        mono.assign ((size_t) chunk, 0.0f);

        dryGain.reset (sampleRate, 0.05);
        wetGain.reset (sampleRate, 0.05);
        dryGain.setCurrentAndTargetValue (targetDry());
        wetGain.setCurrentAndTargetValue (targetWet());
    }

    void setSettings (const Settings& s) noexcept
    {
        settings = s;
        settings.mix = juce::jlimit (0.0f, 1.0f, settings.mix);

        if (s.type != current && ! started)
        {
            // Nothing played yet (e.g. a project's settings arriving after the DAW got ready): just switch
            current = s.type;
            if (isNatural (current))
            {
                natural[active].reset();
                natural[active].setSettings (naturalSettings (s, current), true);
            }
            else
            {
                gated.reset();
            }
        }
        else if (s.type != current)
        {
            // Crossfade from what's playing now to the new TYPE
            from = current;
            fromNatural = active;
            current = s.type;

            if (isNatural (current))
            {
                active = 1 - active;   // (a fresh one, so the old can ring out under the fade)
                natural[active].reset();
                natural[active].setSettings (naturalSettings (s, current), true);
            }
            else
            {
                gated.reset();
            }
            fadeLeft = fadeLength;
        }

        gated.setSettings (gatedSettings (settings));
        if (isNatural (current))
            natural[active].setSettings (naturalSettings (settings, current));
        dryGain.setTargetValue (targetDry());
        wetGain.setTargetValue (targetWet());
    }

    void reset() noexcept
    {
        gated.reset();
        for (auto& n : natural) n.reset();
        fadeLeft = 0;
    }

    /** Works on the buffer in place (like GatedReverb::process). */
    BlockResult process (juce::AudioBuffer<float>& buffer, int numInputChannels) noexcept
    {
        BlockResult result;
        started = true;
        const int numSamples = buffer.getNumSamples();
        const int channels = buffer.getNumChannels();
        const int numOut = juce::jmin (channels, 2);
        if (numSamples <= 0 || numOut <= 0)
            return result;

        const int numIn = juce::jlimit (1, numOut, numInputChannels);
        for (int c = numIn; c < channels; ++c)   // (a mono track feeding a stereo output: the original on both sides)
            buffer.copyFrom (c, 0, buffer, 0, 0, numSamples);

        for (int start = 0; start < numSamples; start += chunk)
        {
            const int n = juce::jmin (chunk, numSamples - start);
            juce::AudioBuffer<float> piece (buffer.getArrayOfWritePointers(), numOut, start, n);

            if (fadeLeft > 0)
            {
                // The old TYPE into the scratch copy, the new one in place, then crossfade
                juce::AudioBuffer<float> old (scratch.getArrayOfWritePointers(), numOut, 0, n);
                for (int c = 0; c < numOut; ++c)
                    old.copyFrom (c, 0, piece, c, 0, n);

                const auto smoothedDry = dryGain, smoothedWet = wetGain;   // (both paths ramp MIX alike)
                render (from, fromNatural, old, numOut, result);
                dryGain = smoothedDry; wetGain = smoothedWet;
                render (current, active, piece, numOut, result);

                for (int i = 0; i < n; ++i)
                {
                    const float t = 1.0f - (float) juce::jmax (0, fadeLeft - i) / (float) fadeLength;
                    for (int c = 0; c < numOut; ++c)
                    {
                        auto* y = piece.getWritePointer (c);
                        y[i] = old.getSample (c, i) * (1.0f - t) + y[i] * t;
                    }
                }
                fadeLeft = juce::jmax (0, fadeLeft - n);
            }
            else
            {
                render (current, active, piece, numOut, result);
            }
        }
        return result;
    }

    /** How long a reverb with these settings rings on after the sound stops (s). */
    static float tailSeconds (const Settings& s) noexcept
    {
        return isNatural (s.type) ? juce::jlimit (NaturalReverb::minDecay, NaturalReverb::maxDecay, s.decay) + s.preDelayMs / 1000.0f + 0.1f
                                  : (s.lengthMs + 50.0f) / 1000.0f;
    }

private:
    static constexpr int chunk = 256;

    static GatedReverb::Settings gatedSettings (const Settings& s) noexcept
    {
        GatedReverb::Settings g;
        g.lengthMs = s.lengthMs;
        g.mix = s.mix;
        g.bypassed = s.bypassed;
        return g;
    }

    static NaturalReverb::Settings naturalSettings (const Settings& s, Type t) noexcept
    {
        NaturalReverb::Settings n;
        n.type = (NaturalReverb::Type) ((int) t - 1);
        n.decay = s.decay;
        n.preDelayMs = s.preDelayMs;
        n.tone = s.tone;
        return n;
    }

    float targetDry() const noexcept   { return settings.bypassed ? 1.0f : GatedReverb::dryGainFor (settings.mix); }
    float targetWet() const noexcept   { return settings.bypassed ? 0.0f : GatedReverb::wetGainFor (settings.mix); }

    void render (Type type, size_t which, juce::AudioBuffer<float>& piece, int numOut, BlockResult& result) noexcept
    {
        if (! isNatural (type))
        {
            const auto r = gated.process (piece, numOut);
            result.wetPeak = juce::jmax (result.wetPeak, r.wetPeak);
            return;
        }

        const int n = piece.getNumSamples();

        // In: left and right together (nothing while bypassed or at MIX 0%)
        const bool feeding = targetWet() > 0.0f;
        if (! feeding)
            std::fill (mono.begin(), mono.begin() + n, 0.0f);
        else if (numOut == 1)
            juce::FloatVectorOperations::copy (mono.data(), piece.getReadPointer (0), n);
        else
        {
            juce::FloatVectorOperations::copyWithMultiply (mono.data(), piece.getReadPointer (0), 0.5f, n);
            juce::FloatVectorOperations::addWithMultiply (mono.data(), piece.getReadPointer (1), 0.5f, n);
        }

        wet.clear (0, 0, n);
        wet.clear (1, 0, n);
        const bool ringing = natural[which].process (mono.data(), wet.getWritePointer (0), wet.getWritePointer (1), n, 1.0f);

        auto* l = piece.getWritePointer (0);
        auto* r = numOut > 1 ? piece.getWritePointer (1) : nullptr;
        const auto* wl = wet.getReadPointer (0);
        const auto* wr = wet.getReadPointer (1);
        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float d = dryGain.getNextValue(), w = wetGain.getNextValue();
            if (! ringing) { l[i] *= d; if (r != nullptr) r[i] *= d; continue; }

            const float a = wl[i] * w, b = wr[i] * w;
            if (r != nullptr)
            {
                l[i] = l[i] * d + a;
                r[i] = r[i] * d + b;
                peak = juce::jmax (peak, std::abs (a), std::abs (b));
            }
            else
            {
                const float m = 0.5f * (a + b);
                l[i] = l[i] * d + m;
                peak = juce::jmax (peak, std::abs (m));
            }
        }
        result.wetPeak = juce::jmax (result.wetPeak, peak);
    }

    Settings settings;
    Type current = Type::bitchSlap, from = Type::bitchSlap;
    size_t active = 0, fromNatural = 0;
    int fadeLength = 1440, fadeLeft = 0;
    bool started = false;

    GatedReverb gated;
    std::array<NaturalReverb, 2> natural;
    juce::AudioBuffer<float> scratch, wet;
    std::vector<float> mono;
    juce::SmoothedValue<float> dryGain, wetGain;
};
