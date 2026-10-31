#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <vector>

//==============================================================================
/**
    Easy Flange's sound: flanger, chorus and vibrato - all three made the
    same way, by mixing your sound with a copy of itself that's delayed by
    a few milliseconds, the delay slowly swinging back and forth.

      FLANGER   a very short delay (0.3 - 8 ms), swept: the copy and your
                sound cancel at a row of frequencies that slide up and down -
                the jet-plane "whoosh". FEEDBACK makes it sharper and more
                metallic (above 0) or hollow (below 0).
      CHORUS    a longer delay (around 15 ms) with two copies drifting in
                pitch very slightly: like several players at once - wider,
                thicker, shimmering.
      VIBRATO   only the swinging copy (with MIX at 100%): the pitch itself
                wobbles, like a singer's vibrato or a Leslie-style warble.

      RATE      how fast it swings (Hz), or with SYNC a note length (one
                full swing) locked to the song's beat.
      DEPTH     how far the delay swings.
      WIDTH     the left and right sides swinging apart (up to opposite
                each other): 0 = the same on both sides, 100% = very wide.
      MIX       0% = dry, 50% = an even blend (the strongest flanging),
                100% = the delayed copy only.

    No delay is added to your track (the copy is mixed on top of it).
*/
class Flanger
{
public:
    enum class Type { flanger = 0, chorus = 1, vibrato = 2 };

    static constexpr float minRateHz = 0.02f, maxRateHz = 10.0f;

    struct Settings
    {
        Type   type       = Type::flanger;
        bool   synced     = false;
        double cycleBeats = 8.0;
        float  rateHz     = 0.3f;
        float  depth      = 0.7f;     // 0 .. 1
        float  feedback   = 0.5f;     // -0.9 .. +0.9 (FLANGER only)
        float  width      = 0.5f;     // 0 .. 1
        float  mix        = 0.5f;     // 0 .. 1
        bool   bypassed   = false;
    };

    void prepare (double newSampleRate, const Settings& initial)
    {
        sampleRate = newSampleRate;
        int size = 1;
        while (size < (int) (sampleRate * 0.05) + 8)   // 50 ms
            size <<= 1;
        mask = size - 1;
        for (auto& line : lines)
            line.assign ((size_t) size, 0.0f);
        writePos = 0;
        phase = 0.0;

        for (auto* v : { &depth, &feedback, &width, &dryGain, &wetGain, &bypassFade })
            v->reset (sampleRate, 0.05);
        settings = initial;
        currentType = initial.type;
        typeFade = 1.0f;
        setSettings (initial, true);
    }

    void setSettings (const Settings& s, bool jump = false)
    {
        settings = s;
        const auto set = [jump] (juce::SmoothedValue<float>& v, float target)
        {
            if (jump) v.setCurrentAndTargetValue (target);
            else      v.setTargetValue (target);
        };

        // MIX: an even blend at 50%, kept about as loud as the dry sound
        const float m = juce::jlimit (0.0f, 1.0f, s.mix);
        const float level = 1.0f / std::sqrt ((1.0f - m) * (1.0f - m) + m * m);
        set (dryGain, (1.0f - m) * level);
        set (wetGain, m * level);
        set (depth, juce::jlimit (0.0f, 1.0f, s.depth));
        if (s.type == currentType || jump)   // (on a TYPE change, FEEDBACK switches while the sound is faded out)
            set (feedback, s.type == Type::flanger ? juce::jlimit (-0.9f, 0.9f, s.feedback) : 0.0f);
        set (width, juce::jlimit (0.0f, 1.0f, s.width));
        set (bypassFade, s.bypassed ? 1.0f : 0.0f);
    }

    /** songBeats: where the song is (quarter notes) at the block's start while it plays, else < 0. */
    void process (juce::AudioBuffer<float>& buffer, int numInputs, double bpm, double songBeats) noexcept
    {
        const int n = buffer.getNumSamples();
        const int outputs = juce::jmin (2, buffer.getNumChannels());
        if (outputs == 0)
            return;

        double cyclesPerSample;
        if (settings.synced)
        {
            const double beats = juce::jmax (0.125, settings.cycleBeats);
            cyclesPerSample = juce::jlimit (20.0, 999.0, bpm) / 60.0 / beats / sampleRate;
            if (songBeats >= 0.0)
                phase = std::fmod (songBeats / beats, 1.0);
        }
        else
        {
            cyclesPerSample = juce::jlimit ((double) minRateHz, (double) maxRateHz, (double) settings.rateHz) / sampleRate;
        }

        // A TYPE change: a quick fade out, switch, and back (so it never clicks)
        const bool changing = settings.type != currentType;
        const float fadeStep = changing ? -typeFade / (float) n : (typeFade < 1.0f ? 1.0f / 480.0f : 0.0f);

        float* out[2] { buffer.getWritePointer (0), outputs > 1 ? buffer.getWritePointer (1) : nullptr };
        const bool stereoIn = numInputs > 1 && outputs > 1;
        const float ms = (float) sampleRate / 1000.0f;
        const auto type = currentType;

        for (int i = 0; i < n; ++i)
        {
            const float inL = out[0][i];
            const float inR = outputs > 1 ? (stereoIn ? out[1][i] : inL) : inL;
            const float d = depth.getNextValue(), fb = feedback.getNextValue(), w = width.getNextValue();
            const float dry = dryGain.getNextValue(), wet = wetGain.getNextValue(), bypass = bypassFade.getNextValue();
            typeFade = juce::jlimit (0.0f, 1.0f, typeFade + fadeStep);

            phase += cyclesPerSample;
            if (phase >= 1.0) phase -= 1.0;

            float in[2] { inL, inR }, y[2] {};
            for (int ch = 0; ch < outputs; ++ch)
            {
                const double p = phase + (ch == 1 ? 0.5 * w : 0.0);
                const float swing = 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * p);   // 0 .. 1

                float copy;
                if (type == Type::chorus)
                {
                    // Two voices, a third of a cycle apart, drifting around 15 ms
                    const float swing2 = 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * (p + 0.33));
                    copy = 0.5f * (read (ch, (12.0f + 7.0f * d * swing) * ms) + read (ch, (14.0f + 7.0f * d * swing2) * ms));
                }
                else if (type == Type::vibrato)
                {
                    copy = read (ch, (1.0f + 6.0f * d * swing) * ms);
                }
                else
                {
                    copy = read (ch, (0.3f + 7.7f * d * swing) * ms);
                }

                lines[(size_t) ch][(size_t) writePos] = in[ch] + fb * copy;
                const float wetPart = copy * (1.0f - 0.5f * std::abs (fb)) * typeFade;
                const float mixed = in[ch] * dry + wetPart * wet + in[ch] * wet * (1.0f - typeFade);
                y[ch] = mixed * (1.0f - bypass) + in[ch] * bypass;
            }
            writePos = (writePos + 1) & mask;

            out[0][i] = y[0];
            if (out[1] != nullptr)
                out[1][i] = y[1];
        }

        if (changing && typeFade <= 0.0f)
        {
            currentType = settings.type;   // (the delay line keeps its sound, so the new type fades in smoothly)
            feedback.setCurrentAndTargetValue (settings.type == Type::flanger ? juce::jlimit (-0.9f, 0.9f, settings.feedback) : 0.0f);
        }

        position = (float) phase;
    }

    /** Where the swing is (0..1), for the window. */
    float getPosition() const noexcept   { return position; }

    /** The delay (ms) the swing gives at a position, for the window's drawing. */
    static float delayMsAt (Type type, float depthAmount, float swing) noexcept
    {
        switch (type)
        {
            case Type::chorus:  return 12.0f + 7.0f * depthAmount * swing;
            case Type::vibrato: return 1.0f + 6.0f * depthAmount * swing;
            case Type::flanger:
            default:            return 0.3f + 7.7f * depthAmount * swing;
        }
    }

private:
    float read (int ch, float delaySamples) const noexcept
    {
        const float at = (float) writePos - juce::jlimit (1.0f, (float) mask - 4.0f, delaySamples);
        const float whole = std::floor (at);
        const float t = at - whole;
        const int p = (int) whole;
        const auto& line = lines[(size_t) ch];
        const float y0 = line[(size_t) ((p - 1) & mask)], y1 = line[(size_t) (p & mask)],
                    y2 = line[(size_t) ((p + 1) & mask)], y3 = line[(size_t) ((p + 2) & mask)];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    double sampleRate = 48000.0, phase = 0.0;
    Settings settings;
    Type currentType = Type::flanger;
    float typeFade = 1.0f, position = 0.0f;
    std::array<std::vector<float>, 2> lines;
    int mask = 0, writePos = 0;
    juce::SmoothedValue<float> depth, feedback, width, dryGain, wetGain, bypassFade;
};
