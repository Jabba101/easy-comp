#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>
#include <array>

//==============================================================================
/**
    INFLATE: more loudness and fullness without squashing - in the spirit of
    the Oxford Inflator.

    It's a gentle curve, not a compressor: quiet and middling parts of the
    waveform are lifted (by up to about 3.5 dB at 100%), the loudest are
    rounded off smoothly towards full scale, and the transients keep their
    shape. The result sounds denser and louder at the same peak level -
    fuller, richer - and gives the limiter after it less to do.

    EFFECT: how much of the curve is blended in (0 = none). CURVE (-50 to
    +50): below zero the curve works more on the loud parts (rounder,
    softer peaks); above zero it lifts the quiet parts more (bigger, more
    "inflated").

    Worked at four times the sample rate (so the curve adds no harsh
    aliasing), with a small, fixed delay. Off passes the sound through
    untouched, just delayed by the same amount (switching crossfades).
*/
class Inflator
{
public:
    struct Settings
    {
        bool on = false;
        float effect = 0.0f;   // 0..1
        float curve = 0.0f;    // -50..+50
    };

    void prepare (double sampleRate, int maxBlock)
    {
        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        oversampler->initProcessing ((size_t) maxBlock);
        latency = (int) std::lround (oversampler->getLatencyInSamples());
        for (auto& line : delay) line.assign ((size_t) juce::jmax (1, latency), 0.0f);
        delayPos = 0;
        blend.reset (sampleRate, 0.05);
        blend.setCurrentAndTargetValue (settings.on ? settings.effect : 0.0f);
        onFade.reset (sampleRate, 0.02);
        onFade.setCurrentAndTargetValue (settings.on ? 1.0f : 0.0f);
        plain.setSize (2, maxBlock);
        oversampler->reset();
    }

    int getLatencySamples() const noexcept   { return latency; }

    void setSettings (const Settings& s) noexcept
    {
        settings = s;
        blend.setTargetValue (juce::jlimit (0.0f, 1.0f, s.effect));
        onFade.setTargetValue (s.on ? 1.0f : 0.0f);
        const float cp = juce::jlimit (-50.0f, 50.0f, s.curve) / 100.0f;
        a = 1.5f + cp; b = -2.0f * cp; c = cp - 0.5f; d = 0.0625f - 0.25f * cp + 0.25f * cp * cp;
    }

    /** The shape on its own (for the window's picture and the tests): x in, x out (full blend). */
    float shape (float x) const noexcept
    {
        const float s = std::abs (x);
        if (s >= 1.0f) return x;
        const float s2 = s * s, s3 = s2 * s, s4 = s2 * s2;
        const float y = a * s + b * s2 + c * s3 - d * (s2 - 2.0f * s3 + s4);
        return x < 0.0f ? -y : y;
    }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int n = buffer.getNumSamples();
        // The plain path: just delayed by the same amount as the working one
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* x = buffer.getReadPointer (ch);
            float* out = plain.getWritePointer (ch);
            auto& line = delay[(size_t) ch];
            int pos = delayPos;
            for (int i = 0; i < n; ++i)
            {
                out[i] = line[(size_t) pos];
                line[(size_t) pos] = x[i];
                if (++pos >= (int) line.size()) pos = 0;
            }
        }
        delayPos = (delayPos + n) % (int) delay[0].size();

        // Fully off: the plain path only (the working one kept warm, so switching on is seamless)
        const bool working = onFade.getCurrentValue() > 0.0f || onFade.isSmoothing();
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), 2, (size_t) n);
        auto up = oversampler->processSamplesUp (block);
        if (working)
        {
            const int m = (int) up.getNumSamples();
            for (int i = 0; i < m; ++i)
            {
                const float e = (i % 4 == 0) ? blend.getNextValue() : blend.getCurrentValue();
                for (int ch = 0; ch < 2; ++ch)
                {
                    float* x = up.getChannelPointer ((size_t) ch);
                    x[i] = x[i] * (1.0f - e) + shape (x[i]) * e;
                }
            }
        }
        else blend.skip (n);
        oversampler->processSamplesDown (block);

        // Switching on and off: a 20 ms crossfade between the two
        for (int i = 0; i < n; ++i)
        {
            const float w = onFade.getNextValue();
            for (int ch = 0; ch < 2; ++ch)
            {
                float* x = buffer.getWritePointer (ch);
                x[i] = w >= 1.0f ? x[i] : (w <= 0.0f ? plain.getSample (ch, i) : x[i] * w + plain.getSample (ch, i) * (1.0f - w));
            }
        }
    }

    void reset() noexcept
    {
        if (oversampler != nullptr) oversampler->reset();
        for (auto& line : delay) std::fill (line.begin(), line.end(), 0.0f);
    }

private:
    Settings settings;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    std::array<std::vector<float>, 2> delay;
    int delayPos = 0, latency = 0;
    juce::SmoothedValue<float> blend { 0.0f }, onFade { 0.0f };
    juce::AudioBuffer<float> plain;
    float a = 1.5f, b = 0.0f, c = -0.5f, d = 0.0625f;
};
