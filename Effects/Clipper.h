#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "FastMath.h"
#include <array>
#include <cmath>
#include <memory>
#include <vector>

//==============================================================================
/**
    Easy Clip's sound: a clipper. It shaves the tops off peaks that would go
    past the CEILING, so a track (or a whole mix) can be louder without its
    loudest moments poking out.

    The order things happen in, for each sample:

      1. PUSH turns the sound up into the clipper.
      2. PROTECT LOWS (when it's on) splits the sound in two at its
         frequency (a Linkwitz-Riley crossover, the kind whose halves add
         back up without a dip). The lows go round the clipper untouched;
         only the part above is clipped, evenly, into the room the lows
         leave under the ceiling (so the clip never pushes the lows up or
         down). So a kick's or
         bass's body stays clean while the clicks and peaks on top of it get
         shaved. (If the lows alone ever go past the ceiling, the ceiling
         still wins.) Like any crossover, the split delays the lows by a
         few milliseconds relative to the highs (at 100 Hz, about 4 ms at
         the very bottom); with PROTECT LOWS off there's no split at all.
      3. The clip itself. SOFTNESS 0 is a brick wall. Turning it up starts
         rounding peaks off before they reach the ceiling (a tanh curve that
         never goes past it); at 100% the whole wave is gently rounded.
         ADVANCED: TOP and BOTTOM lower the ceiling for each half of the wave
         separately; BIAS makes the rounding lopsided - one half of the wave
         is rounded earlier and harder than the other (it also adds a little
         rounding of its own, so it works with SOFTNESS at 0 too). Both add
         even harmonics, the warmer, valve-like edge - and a slow offset
         (DC), which is filtered out of what the clipper changed (never out
         of your original sound, and only while the clip is lopsided). Below
         the knee nothing is changed at all.
      4. BLEND mixes your original sound (at its own level, not pushed) back
         in: 100% is all clipped.
      5. The safety net: a very fast look-ahead limiter at the CEILING, so
         nothing ever comes out louder than it. It only has the tiny
         overshoots that oversampling's filters and BLEND can leave to deal
         with, and it turns them down smoothly over a millisecond instead of
         clipping them (clipping them at the normal sample rate would bring
         back the harshness oversampling is there to avoid).

    OVERSAMPLING: clipping creates new high harmonics; at the normal sample
    rate the highest of them fold back down as harsh, out-of-tune tones
    (aliasing). Working at 4 or 8 times the rate keeps most of them out of
    earshot. The plugin always reports the 8x delay to your DAW and pads the
    other settings to match, so switching between them never shifts the
    timing. BYPASS and BLEND's original line up with the clipped sound
    exactly.

    With PUSH at 0 dB, the CEILING at 0 dB and everything else neutral,
    sound that never goes past 0 dBFS comes out unchanged (with OVERSAMPLING
    OFF, exactly; with it on, only its filters' tiny effect at the very top
    of the range).
*/
class Clipper
{
public:
    enum class Quality { off = 0, times4 = 1, times8 = 2 };

    static constexpr float protectOffHz = 20.0f;   // PROTECT LOWS at (or below) this is off
    static constexpr float silenceDb    = -100.0f;
    static constexpr int   safetyLength = 48;      // the safety limiter's look-ahead (samples)

    struct Settings
    {
        float pushDb        = 0.0f;     //   0 .. +24 dB
        float ceilingDb     = 0.0f;     // -24 .. 0 dBFS
        float softness      = 0.0f;     //   0 .. 1
        float protectLowsHz = protectOffHz;

        float topDb         = 0.0f;     // -12 .. 0 dB below the ceiling, upper half of the wave
        float bottomDb      = 0.0f;     // -12 .. 0 dB, lower half
        float bias          = 0.0f;     //  -1 .. +1
        float blend         = 1.0f;     //   0 .. 1 (1 = all clipped)

        Quality quality     = Quality::times4;
        bool bypassed       = false;
    };

    //==========================================================================
    void prepare (double newSampleRate, int numChannelsToUse, const Settings& initial)
    {
        sampleRate  = newSampleRate;
        numChannels = juce::jlimit (1, maxChannels, numChannelsToUse);

        for (int q = 0; q < 2; ++q)
        {
            oversamplers[(size_t) q] = std::make_unique<juce::dsp::Oversampling<float>> (
                (size_t) numChannels, (size_t) (q == 0 ? 2 : 3),
                juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
            oversamplers[(size_t) q]->initProcessing ((size_t) chunkSize);
            oversamplerLatency[(size_t) q] = juce::roundToInt (oversamplers[(size_t) q]->getLatencyInSamples());
        }

        clipLatency = oversamplerLatency[1];

        // Ring buffers: the original sound (for BLEND and BYPASS), and the
        // clipped sound (padded to the 8x delay).
        int ringSize = 1;
        while (ringSize < clipLatency + safetyLength + chunkSize + 8)
            ringSize <<= 1;

        ringMask = ringSize - 1;
        originalRing.assign ((size_t) (numChannels * ringSize), 0.0f);
        clippedRing.assign ((size_t) (numChannels * ringSize), 0.0f);
        referenceRing.assign ((size_t) (numChannels * ringSize), 0.0f);
        ringStride = ringSize;
        writePos = 0;

        work.setSize (numChannels, chunkSize);

        constexpr double smoothing = 0.03;   // knobs glide over 30 ms
        for (auto* s : { &push, &ceiling, &soft, &topGain, &bottomGain, &biasAmount, &blendAmount, &protectAmount, &lopsided })
            s->reset (sampleRate, smoothing);
        bypassFade.reset (sampleRate, 0.02);

        currentQuality = initial.quality;
        modeFade = 1.0f;
        fadingOut = false;
        setSettings (initial, true);
        resetState();
    }

    /** The delay your DAW is told about (the same whatever OVERSAMPLING is set to). */
    int getLatencySamples() const noexcept   { return clipLatency + safetyLength - 1; }

    void setSettings (const Settings& s, bool jump = false)
    {
        settings = s;

        const auto set = [jump] (juce::SmoothedValue<float>& v, float target)
        {
            if (jump) v.setCurrentAndTargetValue (target);
            else      v.setTargetValue (target);
        };

        const bool protecting = s.protectLowsHz > protectOffHz + 0.5f;
        const bool uneven = std::abs (s.topDb - s.bottomDb) > 0.001f || std::abs (s.bias) > 0.0001f;

        set (push,          juce::Decibels::decibelsToGain (s.pushDb));
        set (ceiling,       juce::Decibels::decibelsToGain (s.ceilingDb));
        set (soft,          juce::jlimit (0.0f, 1.0f, s.softness));
        set (topGain,       juce::Decibels::decibelsToGain (juce::jmin (0.0f, s.topDb)));
        set (bottomGain,    juce::Decibels::decibelsToGain (juce::jmin (0.0f, s.bottomDb)));
        set (biasAmount,    juce::jlimit (-1.0f, 1.0f, s.bias));
        set (blendAmount,   juce::jlimit (0.0f, 1.0f, s.blend));
        set (protectAmount, protecting ? 1.0f : 0.0f);
        set (lopsided,      uneven ? 1.0f : 0.0f);
        set (bypassFade,    s.bypassed ? 1.0f : 0.0f);

        if (protecting)
            protectHz = s.protectLowsHz;   // (when it switches off, the split keeps its last frequency while it fades out)
    }

    //==========================================================================
    /** Clips the buffer in place. pushedLevel (optional, one value per
        sample): how loud the loudest channel would have come out without
        the clip (pushed, and BLENDed like the real thing), lined up with
        the output - for the "what it's doing" display and SHAVED OFF. */
    void process (juce::AudioBuffer<float>& buffer, float* pushedLevel = nullptr) noexcept
    {
        const int total = buffer.getNumSamples();
        const int channels = juce::jmin (numChannels, buffer.getNumChannels());

        for (int start = 0; start < total; start += chunkSize)
        {
            const int n = juce::jmin (chunkSize, total - start);
            processChunk (buffer, channels, start, n, pushedLevel != nullptr ? pushedLevel + start : nullptr);
        }
    }

    void resetState() noexcept
    {
        for (auto& o : oversamplers)
            if (o != nullptr)
                o->reset();

        for (auto& c : channelState)
            c = {};

        std::fill (originalRing.begin(), originalRing.end(), 0.0f);
        std::fill (clippedRing.begin(), clippedRing.end(), 0.0f);
        std::fill (referenceRing.begin(), referenceRing.end(), 0.0f);
        for (auto& r : referenceState)
            r = {};
        lastProtectHz = -1.0f;

        safety = {};
        safety.required.fill (1.0f);
        safety.smallest.fill (1.0f);
        safety.sum = (double) safetyLength;
    }

    //==========================================================================
    /** The clip curve on its own, for one sample (also used by the tests).
        Keeps x between low (negative) and high (positive), rounding into
        them by `softness` (0..1); `bias` (-1..1) rounds the upper half
        earlier than the lower one (or the other way round). */
    static float clipInto (float x, float low, float high, float softness, float bias = 0.0f) noexcept
    {
        const float s = juce::jmax (softness, std::abs (bias));
        const float roomUp = juce::jmax (high, 0.0f), roomDown = juce::jmax (-low, 0.0f);
        const float shaped = x >= 0.0f ? curve (x, roomUp, juce::jlimit (0.0f, 1.0f, s * (1.0f + bias)))
                                       : -curve (-x, roomDown, juce::jlimit (0.0f, 1.0f, s * (1.0f - bias)));
        return juce::jlimit (low, high, shaped);
    }

private:
    static constexpr int maxChannels = 2;
    static constexpr int chunkSize   = 512;

    // Rises 1:1 up to the knee, then bends (tanh) towards `limit` without ever reaching it.
    static float curve (float x, float limit, float softness) noexcept
    {
        if (limit <= 0.0f)
            return 0.0f;

        const float knee = limit * (1.0f - softness);

        if (x <= knee)
            return x;

        const float room = limit - knee;
        if (room < 1.0e-6f)
            return limit;

        // tanh (u) = 1 - 2 / (e^2u + 1), with the quick exp2 (u is never negative here)
        const float u = juce::jmin ((x - knee) / room, 40.0f);
        const float tanhU = 1.0f - 2.0f / (FastMath::exp2 (u * 2.88539008f) + 1.0f);   // (2 / ln 2)
        return knee + room * tanhU;
    }

    struct Biquad
    {
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    struct ChannelState
    {
        Biquad low1, low2, high1, high2;   // Linkwitz-Riley: two Butterworths each way
        float offset = 0.0f;               // the slow offset in what the clipper changed
    };

    template <typename States>
    static void setCrossover (States& states, double rate, float hz) noexcept
    {
        const double w = juce::MathConstants<double>::twoPi * juce::jmin ((double) hz, rate * 0.45) / rate;
        const double cosw = std::cos (w), alpha = std::sin (w) / juce::MathConstants<double>::sqrt2;   // Q = 1/sqrt2
        const double a0 = 1.0 + alpha;
        const float a1 = (float) (-2.0 * cosw / a0), a2 = (float) ((1.0 - alpha) / a0);
        const float lb0 = (float) ((1.0 - cosw) * 0.5 / a0), lb1 = (float) ((1.0 - cosw) / a0);
        const float hb0 = (float) ((1.0 + cosw) * 0.5 / a0), hb1 = (float) (-(1.0 + cosw) / a0);

        for (auto& c : states)
        {
            for (auto* f : { &c.low1, &c.low2 })   { f->b0 = lb0; f->b1 = lb1; f->b2 = lb0; f->a1 = a1; f->a2 = a2; }
            for (auto* f : { &c.high1, &c.high2 }) { f->b0 = hb0; f->b1 = hb1; f->b2 = hb0; f->a1 = a1; f->a2 = a2; }
        }
    }

    int latencyOf (Quality q) const noexcept
    {
        return q == Quality::off ? 0 : oversamplerLatency[q == Quality::times4 ? 0 : 1];
    }

    float& originalAt (int ch, int pos) noexcept   { return originalRing[(size_t) (ch * ringStride + (pos & ringMask))]; }
    float& clippedAt (int ch, int pos) noexcept    { return clippedRing[(size_t) (ch * ringStride + (pos & ringMask))]; }
    float& referenceAt (int ch, int pos) noexcept  { return referenceRing[(size_t) (ch * ringStride + (pos & ringMask))]; }

    void processChunk (juce::AudioBuffer<float>& buffer, int channels, int start, int n, float* pushedLevel) noexcept
    {
        // OVERSAMPLING changed: fade the clipped sound out over this chunk,
        // switch, then fade back in (a 5 ms dip instead of a click).
        if (settings.quality != currentQuality && ! fadingOut)
            fadingOut = true;

        // Knob values for each sample of the chunk
        for (int i = 0; i < n; ++i)
        {
            const auto k = (size_t) i;
            pushGain[k]    = push.getNextValue();
            ceilingGain[k] = ceiling.getNextValue();
            softNow[k]     = soft.getNextValue();
            topLimit[k]    = ceilingGain[k] * topGain.getNextValue();
            bottomLimit[k] = ceilingGain[k] * bottomGain.getNextValue();
            biasNow[k]     = biasAmount.getNextValue();
            blendNow[k]    = blendAmount.getNextValue();
            protectNow[k]  = protectAmount.getNextValue();
            lopsidedNow[k] = lopsided.getNextValue();
        }

        const int factor = currentQuality == Quality::off ? 1 : (currentQuality == Quality::times4 ? 4 : 8);
        const double rate = sampleRate * factor;

        if (std::abs (protectHz - lastProtectHz) > 0.01f)
        {
            setCrossover (channelState, rate, protectHz);
            setCrossover (referenceState, sampleRate, protectHz);
            lastProtectHz = protectHz;
        }

        // Keep the original, and make the pushed copy to work on. (Plus, for
        // the display, the original as the crossover leaves it: PROTECT
        // LOWS' split delays the lows by a few milliseconds, and the display
        // has to compare like with like.)
        for (int ch = 0; ch < channels; ++ch)
        {
            const float* in = buffer.getReadPointer (ch, start);
            float* w = work.getWritePointer (ch);
            auto& r = referenceState[(size_t) ch];

            for (int i = 0; i < n; ++i)
            {
                originalAt (ch, writePos + i) = in[i];
                w[i] = in[i] * pushGain[(size_t) i];

                const float protect = protectNow[(size_t) i];
                const float split = protect > 0.0f ? r.low2.process (r.low1.process (in[i])) + r.high2.process (r.high1.process (in[i])) : in[i];
                referenceAt (ch, writePos + i) = protect * split + (1.0f - protect) * in[i];
            }
        }

        // The clip, at the chosen rate

        const float offsetCoefficient = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * 3.0 / rate);

        if (currentQuality == Quality::off)
        {
            for (int ch = 0; ch < channels; ++ch)
                clipRun (work.getWritePointer (ch), n, 1, channelState[(size_t) ch], offsetCoefficient);
        }
        else
        {
            auto& os = *oversamplers[currentQuality == Quality::times4 ? 0 : 1];
            juce::dsp::AudioBlock<float> block (work.getArrayOfWritePointers(), (size_t) channels, (size_t) n);
            auto up = os.processSamplesUp (block);

            for (int ch = 0; ch < channels; ++ch)
                clipRun (up.getChannelPointer ((size_t) ch), n * factor, factor, channelState[(size_t) ch], offsetCoefficient);

            os.processSamplesDown (block);
        }

        // BLEND with the original (delayed to match), padded up to the 8x delay
        const int latency = latencyOf (currentQuality);
        const int pad = clipLatency - latency;

        for (int ch = 0; ch < channels; ++ch)
        {
            const float* w = work.getReadPointer (ch);

            for (int i = 0; i < n; ++i)
            {
                const float original = originalAt (ch, writePos + i - latency);
                const float blend = blendNow[(size_t) i];
                clippedAt (ch, writePos + i + pad) = blend * w[i] + (1.0f - blend) * original;
            }
        }

        // Fades for an OVERSAMPLING change
        float fadeStep = 0.0f;
        if (fadingOut)
            fadeStep = -modeFade / (float) n;
        else if (modeFade < 1.0f)
            fadeStep = 1.0f / (float) fadeLength;

        // The safety net, then out: the clipped sound, or (BYPASS) the original
        const int totalLatency = getLatencySamples();

        for (int i = 0; i < n; ++i)
        {
            const float bypass = bypassFade.getNextValue();
            const float limit = ceilingGain[(size_t) i];
            modeFade = juce::jlimit (0.0f, 1.0f, modeFade + fadeStep);

            const float blend = blendNow[(size_t) i], pushed = pushGain[(size_t) i];
            float loudestIn = 0.0f, loudestClipped = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
            {
                loudestClipped = juce::jmax (loudestClipped, std::abs (clippedAt (ch, writePos + i)));
                loudestIn = juce::jmax (loudestIn, std::abs (blend * pushed * referenceAt (ch, writePos + i - totalLatency)
                                                             + (1.0f - blend) * originalAt (ch, writePos + i - totalLatency)));
            }

            const float gain = safetyGain (loudestClipped * modeFade, limit);

            for (int ch = 0; ch < channels; ++ch)
            {
                const float original = originalAt (ch, writePos + i - totalLatency);
                const float delayed = clippedAt (ch, writePos + i - (safetyLength - 1)) * modeFade;
                const float clipped = juce::jlimit (-limit, limit, delayed * gain);   // (the limiter already keeps it inside; this is just certainty)
                buffer.setSample (ch, start + i, clipped * (1.0f - bypass) + original * bypass);
            }

            if (pushedLevel != nullptr)
                pushedLevel[i] = loudestIn;
        }

        writePos = (writePos + n) & ringMask;

        if (fadingOut)
        {
            // Faded out: switch now; the next chunk fades the new one in.
            fadingOut = false;
            modeFade = 0.0f;
            currentQuality = settings.quality;

            for (auto& o : oversamplers)
                o->reset();
            for (auto& c : channelState)
                c = {};
            std::fill (clippedRing.begin(), clippedRing.end(), 0.0f);
            lastProtectHz = -1.0f;
        }
    }

    // The safety limiter: the gain for the sample that's safetyLength - 1
    // samples old. The smallest gain any sample in the look-ahead needs,
    // averaged over the look-ahead, so it glides down in time and never
    // lets a sample through above the ceiling.
    float safetyGain (float loudest, float limit) noexcept
    {
        auto& s = safety;
        s.required[(size_t) s.position] = loudest > limit ? limit / loudest : 1.0f;

        float smallest = 1.0f;
        for (auto r : s.required)
            smallest = juce::jmin (smallest, r);

        s.sum += (double) smallest - (double) s.smallest[(size_t) s.position];
        s.smallest[(size_t) s.position] = smallest;
        s.position = (s.position + 1) % safetyLength;

        if (s.position == 0)   // (now and then, add it up afresh so rounding can't creep in)
        {
            s.sum = 0.0;
            for (auto m : s.smallest)
                s.sum += (double) m;
        }

        return (float) juce::jmin (1.0, s.sum / (double) safetyLength);
    }

    // One channel's run of samples at the working rate. `factor` working
    // samples share one sample's knob values.
    void clipRun (float* x, int count, int factor, ChannelState& state, float offsetCoefficient) noexcept
    {
        for (int j = 0; j < count; ++j)
        {
            const auto i = (size_t) (factor == 1 ? j : j / factor);
            const float v = x[j];

            // PROTECT LOWS: the lows go round the clipper (fading in and out smoothly)
            const float protect = protectNow[i];
            float lows = 0.0f, rest = v;

            if (protect > 0.0f)
            {
                lows = state.low2.process (state.low1.process (v)) * protect;
                rest = state.high2.process (state.high1.process (v)) * protect + v * (1.0f - protect);
            }

            // The part above is clipped evenly into whatever room the lows
            // leave, so the clip never shifts it up or down - which would
            // eat into the lows. (No room left: the lows themselves stop at
            // the ceiling.)
            const float lowsSize = std::abs (lows);
            const float roomUp = juce::jmax (0.0f, topLimit[i] - lowsSize);
            const float roomDown = juce::jmax (0.0f, bottomLimit[i] - lowsSize);
            const float lowsKept = juce::jlimit (-bottomLimit[i], topLimit[i], lows);
            const float change = clipInto (rest, -roomDown, roomUp, softNow[i], biasNow[i]) - rest;

            // Take the slow offset a lopsided clip makes out of what it changed
            state.offset += offsetCoefficient * (change - state.offset);
            x[j] = lowsKept + rest + change - lopsidedNow[i] * state.offset;
        }
    }

    //==========================================================================
    double sampleRate = 48000.0;
    int numChannels = 2;
    Settings settings;

    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 2> oversamplers;
    std::array<int, 2> oversamplerLatency { 0, 0 };
    int clipLatency = 0;

    std::vector<float> originalRing, clippedRing, referenceRing;
    int ringMask = 0, ringStride = 0, writePos = 0;
    juce::AudioBuffer<float> work;

    juce::SmoothedValue<float> push, ceiling, soft, topGain, bottomGain, biasAmount, blendAmount, protectAmount, lopsided, bypassFade;
    float protectHz = 100.0f, lastProtectHz = -1.0f;

    std::array<ChannelState, maxChannels> channelState, referenceState;
    std::array<float, chunkSize> pushGain {}, ceilingGain {}, softNow {}, topLimit {}, bottomLimit {},
                                 biasNow {}, blendNow {}, protectNow {}, lopsidedNow {};

    struct Safety
    {
        std::array<float, safetyLength> required {}, smallest {};
        double sum = 0.0;
        int position = 0;
    } safety;

    Quality currentQuality = Quality::times4;
    float modeFade = 1.0f;
    bool fadingOut = false;
    static constexpr int fadeLength = 240;
};
