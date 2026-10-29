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
    Easy Amp's sound: an overdriven valve (tube) amplifier, from clean to a
    growling rock distortion - the ballpark of a 70s/80s hard-rock guitar
    or a gritty, overdriven rock bass. (Not high-gain metal.)

    Why valves sound nicer pushed than transistors, and what this copies:

      - They bend gradually. A valve starts rounding the wave early and
        gently (a tanh-shaped curve here), so turning up first thickens
        the sound, then dirties it. No sudden wall.
      - They're lopsided. A triode runs out of room sooner on one half of
        the wave than the other (here the grid-conduction side squashes at
        1, the cut-off side only at 2), which adds even harmonics - the
        octave, the octave and a fifth - that blend with the note.
      - They "breathe". Driven hard, the grid draws current and shifts the
        valve's operating point (bias shift), and the power supply sags.
        Both follow the playing: the amp compresses and recovers with each
        hit or note.
      - Several stages in a row. One preamp stage gives the warmth; from
        about a third of the way up DRIVE a second stage fades in (leaning
        the same way as the first, so the warmth builds rather than
        cancels), and a push-pull power stage (even-sided, so it rounds
        without adding more lopsidedness) sits at the end.
      - Filtering around the stages, as in an amp: coupling capacitors
        between stages (they block the slow offsets the lopsided stages
        make), a little top-end loss after the second stage, and a "fizz"
        filter at the output that closes in as DRIVE goes up, so the
        distortion stays smooth instead of buzzy.

    The knobs:
      DRIVE   0 = clean (switched off: costs nothing), 100% = full growl.
      TONE    darker / brighter after the amp (a tilt around 800 Hz,
              up to 6 dB each way; the middle changes nothing).
      TIGHT   below this frequency the sound goes round the amp, clean
              (a Linkwitz-Riley split), so deep bass doesn't turn to mud
              and kick and bass keep their weight. OFF at its lowest.
      MIX     blends the original back in (parallel distortion).
      Level matching (AUTO-GAIN): turning DRIVE up changes the tone, not
      the volume. A fixed correction for each DRIVE setting (measured on
      noise at -18 dBFS when the plugin starts) jumps in straight away;
      a slow listener then trims it to your actual track. With AUTO-GAIN
      off, only the fixed correction is used, so it still never blasts.

    OVERSAMPLING as Easy Clip: "4x / 8x" (4x while mixing, 8x when exporting),
    OFF, 4x, 8x, and always the 8x delay, so switching never shifts the
    timing.
*/
class ValveAmp
{
public:
    enum class Quality { off = 0, times4 = 1, times8 = 2 };

    static constexpr float tightOffHz = 20.0f;   // TIGHT at (or below) this is off

    struct Settings
    {
        float drive     = 0.0f;          // 0 .. 1
        float tone      = 0.0f;          // -1 (darker) .. +1 (brighter)
        float tightHz   = tightOffHz;
        float mix       = 1.0f;          // 0 .. 1
        bool  autoGain  = true;
        Quality quality = Quality::times4;
        bool  bypassed  = false;
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

        latency = oversamplerLatency[1];

        int ringSize = 1;
        while (ringSize < latency + chunkSize + 8)
            ringSize <<= 1;

        ringMask = ringSize - 1;
        ringStride = ringSize;
        for (auto* ring : { &originalRing, &lowsRing, &highsRing, &ampRing })
            ring->assign ((size_t) (numChannels * ringSize), 0.0f);
        writePos = 0;

        work.setSize (numChannels, chunkSize);

        for (auto* s : { &drive, &tone, &mix, &active, &tightAmount })
            s->reset (sampleRate, 0.03);
        bypassFade.reset (sampleRate, 0.02);

        measureFixedCorrections();

        currentQuality = initial.quality;
        modeFade = 1.0f;
        fadingOut = false;
        setSettings (initial, true);
        resetState();
    }

    /** The delay your DAW is told about (the same whatever OVERSAMPLING is set to). */
    int getLatencySamples() const noexcept   { return latency; }

    void setSettings (const Settings& s, bool jump = false)
    {
        const bool knobsMoved = std::abs (s.drive - settings.drive) > 0.001f || std::abs (s.tone - settings.tone) > 0.001f;
        settings = s;

        const auto set = [jump] (juce::SmoothedValue<float>& v, float target)
        {
            if (jump) v.setCurrentAndTargetValue (target);
            else      v.setTargetValue (target);
        };

        const bool tight = s.tightHz > tightOffHz + 0.5f;

        set (drive,       juce::jlimit (0.0f, 1.0f, s.drive));
        set (tone,        juce::jlimit (-1.0f, 1.0f, s.tone));
        set (mix,         juce::jlimit (0.0f, 1.0f, s.mix));
        set (active,      s.drive > 0.001f ? 1.0f : 0.0f);
        set (tightAmount, tight ? 1.0f : 0.0f);
        set (bypassFade,  s.bypassed ? 1.0f : 0.0f);

        if (tight)
            tightHz = s.tightHz;

        if (knobsMoved && ! jump)
            listener.quickenFor = (int) (sampleRate * 0.5);   // re-learn quicker for half a second
    }

    //==========================================================================
    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int total = buffer.getNumSamples();
        const int channels = juce::jmin (numChannels, buffer.getNumChannels());

        for (int start = 0; start < total; start += chunkSize)
            processChunk (buffer, channels, start, juce::jmin (chunkSize, total - start));
    }

    void resetState() noexcept
    {
        for (auto& o : oversamplers)
            if (o != nullptr)
                o->reset();

        for (auto& c : stages)    c = {};
        for (auto& c : baseState) c = {};
        for (auto* ring : { &originalRing, &lowsRing, &highsRing, &ampRing })
            std::fill (ring->begin(), ring->end(), 0.0f);

        listener = {};
        lastTightHz = -1.0f;
    }

    /** How many dB the level matching is adding (or taking away) right now. */
    float getMatchGainDb() const noexcept       { return matchGainDb; }

    /** How hard the amp is being driven right now: 0 (clean) .. 1 (well into the growl), for the window. */
    float getWorkingAmount() const noexcept     { return working; }

    //==========================================================================
    /** The amp's curve on its own (no filters, no breathing), for the
        window's drawing: what a sample x (around +-0.5 for a typical
        track's peaks) comes out as at this DRIVE. */
    static float transfer (float x, float driveAmount) noexcept
    {
        const auto g = gainsFor (driveAmount);
        const float y1 = triode (x * g.pre, stage1Bias);
        const float y = y1 + (triode (y1 * g.second, stage2Bias) * 0.5f - y1) * g.secondMix;
        return tanhOf (y * g.power);
    }

private:
    static constexpr int maxChannels = 2;
    static constexpr int chunkSize   = 512;
    static constexpr float stage1Bias = 0.25f, stage2Bias = 0.35f;

    //==========================================================================
    // tanh with the suite's quick exp2: 1 - 2 / (e^2x + 1), odd-symmetric
    static float tanhOf (float x) noexcept
    {
        const float a = juce::jmin (std::abs (x), 20.0f);
        const float t = 1.0f - 2.0f / (FastMath::exp2 (a * 2.88539008f) + 1.0f);
        return x < 0.0f ? -t : t;
    }

    // One triode stage: squashes at +1 on the grid-conduction side, only at
    // -2 on the cut-off side; biased so it's lopsided even when barely
    // driven. The resting offset is taken back off.
    static float triodeCurve (float v) noexcept   { return v >= 0.0f ? tanhOf (v) : 2.0f * tanhOf (0.5f * v); }
    static float triode (float u, float bias, float rest) noexcept   { return triodeCurve (u + bias) - rest; }
    static float triode (float u, float bias) noexcept               { return triode (u, bias, triodeCurve (bias)); }

    struct Gains
    {
        float pre, second, secondMix, power, fizzHz;
        float rest1 = triodeCurve (stage1Bias), rest2 = triodeCurve (stage2Bias);   // each stage's resting offset
    };

    static float smoothStep (float a, float b, float x) noexcept
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - a) / (b - a));
        return t * t * (3.0f - 2.0f * t);
    }

    // DRIVE -> how hard each stage is pushed. Into the first stage: -14 dB
    // at 0 (a typical track's peaks barely bend it) rising 40 dB to +26 dB;
    // the second stage fades in from a third of the way and pushes up to
    // +18 dB more; the power stage adds a little; the fizz filter closes
    // from 18 kHz to about 5.8 kHz.
    static Gains gainsFor (float d) noexcept
    {
        Gains g;
        g.pre       = juce::Decibels::decibelsToGain (-14.0f + 40.0f * d);
        g.secondMix = smoothStep (0.33f, 0.66f, d);
        g.second    = juce::Decibels::decibelsToGain (18.0f * smoothStep (0.4f, 1.0f, d));
        g.power     = 1.0f + 2.0f * d;
        g.fizzHz    = 18000.0f * std::pow (0.32f, std::pow (d, 1.5f));
        return g;
    }

    struct OnePole   // low-pass; high-pass = input - low-pass
    {
        float z = 0.0f;
        float lowPass (float x, float coefficient) noexcept   { z += coefficient * (x - z); return z; }
    };

    static float onePoleCoefficient (double hz, double rate) noexcept
    {
        return (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * juce::jmin (hz, rate * 0.45) / rate));
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

    // The amp's state, per channel, at the working rate
    struct StageState
    {
        OnePole inputCoupling, coupling1, coupling2, treble2, fizz1, fizz2;
        float gridCharge = 0.0f;   // bias shift: builds up while the grid conducts
        float supply = 0.0f;       // power-supply sag
    };

    // Per channel at the normal rate: the TIGHT split, TONE, the listener's filters
    struct BaseState
    {
        Biquad low1, low2, high1, high2;
        OnePole toneSplit, hearIn, hearOut;
    };

    struct Coefficients
    {
        float inputCoupling, coupling, treble, fizz, chargeUp, chargeDown, sagUp, sagDown;
    };

    Coefficients coefficientsFor (double rate, float fizzHz) const noexcept
    {
        Coefficients c;
        c.inputCoupling = onePoleCoefficient (30.0, rate);
        c.coupling      = onePoleCoefficient (12.0, rate);
        c.treble        = onePoleCoefficient (9000.0, rate);
        c.fizz          = onePoleCoefficient (fizzHz, rate);
        c.chargeUp      = onePoleCoefficient (1.0 / (2.0 * juce::MathConstants<double>::pi * 0.002), rate);   // ~2 ms
        c.chargeDown    = onePoleCoefficient (1.0 / (2.0 * juce::MathConstants<double>::pi * 0.08), rate);    // ~80 ms
        c.sagUp         = onePoleCoefficient (1.0 / (2.0 * juce::MathConstants<double>::pi * 0.01), rate);    // ~10 ms
        c.sagDown       = onePoleCoefficient (1.0 / (2.0 * juce::MathConstants<double>::pi * 0.2), rate);     // ~200 ms
        return c;
    }

    // One sample through the amp
    static forcedinline float amp (float x, const Gains& g, const Coefficients& c, StageState& s) noexcept
    {
        x -= s.inputCoupling.lowPass (x, c.inputCoupling);

        // Stage 1, with bias shift: while the grid conducts it charges up and
        // pulls the operating point down; it lets go over ~80 ms.
        const float u = x * g.pre;
        const float conducting = juce::jmax (0.0f, u + stage1Bias - s.gridCharge);
        s.gridCharge += (conducting > s.gridCharge ? c.chargeUp : c.chargeDown) * (conducting * 0.3f - s.gridCharge);
        float y = triode (u - s.gridCharge, stage1Bias, g.rest1);
        y -= s.coupling1.lowPass (y, c.coupling);

        // Stage 2, fading in. (A real stage also turns the wave upside down;
        // here both stages lean the same way, so their even harmonics - the
        // warmth - add up instead of cancelling while the second fades in.)
        if (g.secondMix > 0.0f)
        {
            float y2 = triode (y * g.second, stage2Bias, g.rest2) * 0.5f;
            y2 = s.treble2.lowPass (y2, c.treble);
            y2 -= s.coupling2.lowPass (y2, c.coupling);
            y += (y2 - y) * g.secondMix;
        }

        // Push-pull power stage, sagging with the load
        const float load = std::abs (y);
        s.supply += (load > s.supply ? c.sagUp : c.sagDown) * (load - s.supply);
        const float powerGain = g.power / (1.0f + 0.35f * s.supply * (g.power - 1.0f));
        y = tanhOf (y * powerGain);

        // The fizz filter
        return s.fizz2.lowPass (s.fizz1.lowPass (y, c.fizz), c.fizz);
    }

    //==========================================================================
    // The fixed loudness correction for each DRIVE setting, measured once on
    // noise at -18 dBFS (RMS), the way the listener hears it.
    void measureFixedCorrections()
    {
        juce::Random random (11);
        const int length = juce::jmax (4096, (int) (sampleRate * 0.25));
        std::vector<float> noise ((size_t) length);
        OnePole shape;
        const float shapeCoefficient = onePoleCoefficient (1500.0, sampleRate);   // a darker, music-like noise
        double power = 0.0;

        for (auto& n : noise)
        {
            n = shape.lowPass (random.nextFloat() * 2.0f - 1.0f, shapeCoefficient);
            power += (double) n * n;
        }

        const float scale = juce::Decibels::decibelsToGain (-18.0f) / (float) std::sqrt (power / length);
        for (auto& n : noise)
            n *= scale;

        const float hear = onePoleCoefficient (150.0, sampleRate);

        for (size_t k = 0; k < fixedCorrection.size(); ++k)
        {
            const float d = (float) k / (float) (fixedCorrection.size() - 1);
            const auto g = gainsFor (d);
            const auto c = coefficientsFor (sampleRate, g.fizzHz);
            StageState s;
            OnePole hearIn, hearOut;
            double in = 0.0, out = 0.0;

            for (int i = 0; i < length; ++i)
            {
                const float y = amp (noise[(size_t) i], g, c, s);
                const float a = noise[(size_t) i] - hearIn.lowPass (noise[(size_t) i], hear);
                const float b = y - hearOut.lowPass (y, hear);
                if (i > length / 4) { in += (double) a * a; out += (double) b * b; }
            }

            fixedCorrection[k] = (float) std::sqrt (in / juce::jmax (out, 1.0e-20));
        }
    }

    float fixedCorrectionFor (float d) const noexcept
    {
        const float position = juce::jlimit (0.0f, 1.0f, d) * (float) (fixedCorrection.size() - 1);
        const auto k = (size_t) juce::jmin ((int) position, (int) fixedCorrection.size() - 2);
        const float t = position - (float) k;
        return fixedCorrection[k] + (fixedCorrection[k + 1] - fixedCorrection[k]) * t;
    }

    //==========================================================================
    void setTightSplit (float hz) noexcept
    {
        const double w = juce::MathConstants<double>::twoPi * juce::jmin ((double) hz, sampleRate * 0.45) / sampleRate;
        const double cosw = std::cos (w), alpha = std::sin (w) / juce::MathConstants<double>::sqrt2;
        const double a0 = 1.0 + alpha;
        const float a1 = (float) (-2.0 * cosw / a0), a2 = (float) ((1.0 - alpha) / a0);
        const float lb0 = (float) ((1.0 - cosw) * 0.5 / a0), lb1 = (float) ((1.0 - cosw) / a0);
        const float hb0 = (float) ((1.0 + cosw) * 0.5 / a0), hb1 = (float) (-(1.0 + cosw) / a0);

        for (auto& b : baseState)
        {
            for (auto* f : { &b.low1, &b.low2 })   { f->b0 = lb0; f->b1 = lb1; f->b2 = lb0; f->a1 = a1; f->a2 = a2; }
            for (auto* f : { &b.high1, &b.high2 }) { f->b0 = hb0; f->b1 = hb1; f->b2 = hb0; f->a1 = a1; f->a2 = a2; }
        }
    }

    int latencyOf (Quality q) const noexcept   { return q == Quality::off ? 0 : oversamplerLatency[q == Quality::times4 ? 0 : 1]; }

    float& ring (std::vector<float>& r, int ch, int pos) noexcept   { return r[(size_t) (ch * ringStride + (pos & ringMask))]; }

    void processChunk (juce::AudioBuffer<float>& buffer, int channels, int start, int n) noexcept
    {
        if (settings.quality != currentQuality && ! fadingOut)
            fadingOut = true;

        if (std::abs (tightHz - lastTightHz) > 0.01f)
        {
            setTightSplit (tightHz);
            lastTightHz = tightHz;
        }

        for (int i = 0; i < n; ++i)
        {
            const auto k = (size_t) i;
            driveNow[k]  = drive.getNextValue();
            toneNow[k]   = tone.getNextValue();
            mixNow[k]    = mix.getNextValue();
            activeNow[k] = active.getNextValue();
            tightNow[k]  = tightAmount.getNextValue();
        }

        // Split off the lows (TIGHT), and keep everything for later
        const float toneSplit = onePoleCoefficient (800.0, sampleRate);
        const float hear = onePoleCoefficient (150.0, sampleRate);
        double heardIn = 0.0;

        for (int ch = 0; ch < channels; ++ch)
        {
            const float* in = buffer.getReadPointer (ch, start);
            float* w = work.getWritePointer (ch);
            auto& b = baseState[(size_t) ch];

            for (int i = 0; i < n; ++i)
            {
                const float t = tightNow[(size_t) i];
                float lows = 0.0f, highs = in[i];

                if (t > 0.0f)
                {
                    const float l = b.low2.process (b.low1.process (in[i]));
                    const float h = b.high2.process (b.high1.process (in[i]));
                    lows = l * t;
                    highs = h * t + in[i] * (1.0f - t);
                }

                ring (originalRing, ch, writePos + i) = in[i];
                ring (lowsRing, ch, writePos + i) = lows;
                ring (highsRing, ch, writePos + i) = highs;
                w[i] = highs;

                const float heard = highs - b.hearIn.lowPass (highs, hear);
                heardIn += (double) heard * heard;
            }
        }

        // Is the amp needed at all? (DRIVE at 0 and settled: no.)
        bool needed = false;
        for (int i = 0; i < n; ++i)
            needed = needed || activeNow[(size_t) i] > 0.0f;

        const int factor = currentQuality == Quality::off ? 1 : (currentQuality == Quality::times4 ? 4 : 8);
        const int pad = latency - latencyOf (currentQuality);
        double heardOut = 0.0;

        if (needed)
        {
            const double rate = sampleRate * factor;

            const auto runAmp = [&] (float* x, int count, StageState& s)
            {
                // Knob values change slowly: work out the gains every 32 samples
                for (int j0 = 0; j0 < count; j0 += 32 * factor)
                {
                    const int i = j0 / factor;
                    const auto g = gainsFor (driveNow[(size_t) i]);
                    const auto c = coefficientsFor (rate, g.fizzHz);
                    const int j1 = juce::jmin (count, j0 + 32 * factor);

                    for (int j = j0; j < j1; ++j)
                        x[j] = amp (x[j], g, c, s);
                }
            };

            if (factor == 1)
            {
                for (int ch = 0; ch < channels; ++ch)
                    runAmp (work.getWritePointer (ch), n, stages[(size_t) ch]);
            }
            else
            {
                auto& os = *oversamplers[currentQuality == Quality::times4 ? 0 : 1];
                juce::dsp::AudioBlock<float> block (work.getArrayOfWritePointers(), (size_t) channels, (size_t) n);
                auto up = os.processSamplesUp (block);

                for (int ch = 0; ch < channels; ++ch)
                    runAmp (up.getChannelPointer ((size_t) ch), n * factor, stages[(size_t) ch]);

                os.processSamplesDown (block);
            }

            // TONE (a tilt: the lows one way, the highs the other), then the
            // loudness correction, then into the padding ring
            for (int ch = 0; ch < channels; ++ch)
            {
                float* w = work.getWritePointer (ch);
                auto& b = baseState[(size_t) ch];

                for (int i = 0; i < n; ++i)
                {
                    const float lowPart = b.toneSplit.lowPass (w[i], toneSplit);
                    const float tilt = toneNow[(size_t) i] * 0.5f;   // +-6 dB at the ends: a gain of 2 or 1/2
                    const float y = lowPart * FastMath::exp2 (-tilt) + (w[i] - lowPart) * FastMath::exp2 (tilt);
                    w[i] = y * fixedCorrectionFor (driveNow[(size_t) i]);

                    const float heard = w[i] - b.hearOut.lowPass (w[i], hear);
                    heardOut += (double) heard * heard;
                }
            }
        }
        else
        {
            for (int ch = 0; ch < channels; ++ch)
                juce::FloatVectorOperations::copy (work.getWritePointer (ch), buffer.getReadPointer (ch, start), n);   // (unused while silent)
        }

        // The listener: slowly trims the fixed correction to this track
        listen (heardIn, heardOut, n, needed);
        const float trim = settings.autoGain ? listener.trim : 1.0f;
        matchGainDb = juce::Decibels::gainToDecibels (trim * fixedCorrectionFor (settings.drive));

        for (int ch = 0; ch < channels; ++ch)
        {
            const float* w = work.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
                ring (ampRing, ch, writePos + i + pad) = needed ? w[i] * trim : 0.0f;
        }

        // Fades for an OVERSAMPLING change
        float fadeStep = 0.0f;
        if (fadingOut)
            fadeStep = -modeFade / (float) n;
        else if (modeFade < 1.0f)
            fadeStep = 1.0f / (float) fadeLength;

        // Out: lows + amp, MIXed with the original as the split leaves it,
        // faded with DRIVE's on/off; or (BYPASS) the original
        float workingNow = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float bypass = bypassFade.getNextValue();
            modeFade = juce::jlimit (0.0f, 1.0f, modeFade + fadeStep);
            const float on = activeNow[(size_t) i] * modeFade;
            const float m = mixNow[(size_t) i];

            for (int ch = 0; ch < channels; ++ch)
            {
                const int at = writePos + i - latency;
                const float original = ring (originalRing, ch, at);
                const float lows = ring (lowsRing, ch, at);
                const float split = lows + ring (highsRing, ch, at);   // the original as the split leaves it
                const float wet = lows + ring (ampRing, ch, writePos + i);
                const float mixed = split + (wet - split) * m;
                const float played = original + (mixed - original) * on;
                buffer.setSample (ch, start + i, played * (1.0f - bypass) + original * bypass);
            }

            workingNow = juce::jmax (workingNow, driveNow[(size_t) i] * on);
        }

        working = workingNow;
        writePos = (writePos + n) & ringMask;

        if (fadingOut)
        {
            fadingOut = false;
            modeFade = 0.0f;
            currentQuality = settings.quality;

            for (auto& o : oversamplers)
                o->reset();
            for (auto& s : stages)
                s = {};
            std::fill (ampRing.begin(), ampRing.end(), 0.0f);
        }

        if (! needed)
            for (auto& s : stages)
                s = {};   // (starts fresh when DRIVE comes back up)
    }

    // Compares how loud the amp's part sounds going in and coming out (the
    // last second or so), and trims the fixed correction to match: quick to
    // turn down, gentle to turn up, never more than 12 dB either way. In
    // silence it holds still.
    void listen (double heardIn, double heardOut, int n, bool ampOn) noexcept
    {
        auto& l = listener;
        const double rate = 1.0 - std::exp (-(double) n / (sampleRate * (l.quickenFor > 0 ? 0.15 : 0.8)));
        l.quickenFor = juce::jmax (0, l.quickenFor - n);

        const double in = heardIn / (double) n, out = heardOut / (double) n;

        if (! ampOn || in < 1.0e-8)   // below about -80 dB: hold
            return;

        l.in += rate * (in - l.in);
        l.out += rate * (out - l.out);

        const float wanted = juce::jlimit (0.25f, 4.0f, (float) std::sqrt (l.in / juce::jmax (l.out, 1.0e-20)));
        const float speed = (float) (wanted < l.trim ? rate * 2.0 : rate * 0.5);
        l.trim += speed * (wanted - l.trim);
    }

    //==========================================================================
    double sampleRate = 48000.0;
    int numChannels = 2;
    Settings settings;

    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 2> oversamplers;
    std::array<int, 2> oversamplerLatency { 0, 0 };
    int latency = 0;

    std::vector<float> originalRing, lowsRing, highsRing, ampRing;
    int ringMask = 0, ringStride = 0, writePos = 0;
    juce::AudioBuffer<float> work;

    juce::SmoothedValue<float> drive, tone, mix, active, tightAmount, bypassFade;
    std::array<float, chunkSize> driveNow {}, toneNow {}, mixNow {}, activeNow {}, tightNow {};
    float tightHz = 100.0f, lastTightHz = -1.0f;

    std::array<StageState, maxChannels> stages;
    std::array<BaseState, maxChannels> baseState;
    std::array<float, 21> fixedCorrection {};

    struct Listener
    {
        double in = 0.0, out = 0.0;
        float trim = 1.0f;
        int quickenFor = 0;
    } listener;

    float matchGainDb = 0.0f, working = 0.0f;

    Quality currentQuality = Quality::times4;
    float modeFade = 1.0f;
    bool fadingOut = false;
    static constexpr int fadeLength = 240;
};
