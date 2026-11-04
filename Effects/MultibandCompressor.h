#pragma once

#include "Compressor.h"
#include <array>
#include <cmath>
#include <vector>

//==============================================================================
/**
    Easy Master's MULTIBAND: the mix split into four bands - LOW, LOW MID,
    HIGH MID, HIGH - each with its own compressor, so a booming bass note
    only turns the bass down, a harsh cymbal only the top.

      The splits (three, draggable) are Linkwitz-Riley 24 dB/oct crossovers,
      arranged so the four bands add back up to the original exactly in
      level (each band gets the others' phase turns too): with every band at
      0 dB and nothing compressing, the sound comes out flat - just as it
      went in, apart from a gentle all-pass phase turn at the splits.

      Each band: THRESHOLD, RATIO, SPEED (attack 3-60 ms and release
      40-600 ms together, fast to slow) and GAIN (that band up or down
      afterwards). It reacts to the band's average level (smooth, for
      mastering). Per band ON (off = that band passes untouched) and SOLO
      (hear only the soloed bands).

      STEREO: one set of settings, left and right compressed together.
      MID + SIDE: the middle and the sides each split and compressed with
      their own settings (e.g. the side lows held right down - tight mono
      bass - while the middle's lows breathe).

    No delay. Changing STEREO <-> MID + SIDE while playing dips for 10 ms.
*/
class MultibandCompressor
{
public:
    static constexpr int numBands = 4, numSplits = 3;

    struct Band
    {
        float thresholdDb = 0.0f;   // -40 .. 0
        float ratio       = 2.0f;   // 1 .. 10
        float speed       = 0.5f;   // 0 (fast) .. 1 (slow)
        float gainDb      = 0.0f;   // -12 .. +12
    };

    struct Settings
    {
        std::array<Band, numBands> mid, side;
        std::array<float, numSplits> splitHz { 120.0f, 1000.0f, 6000.0f };
        std::array<bool, numBands> bandOn { true, true, true, true };
        std::array<bool, numBands> solo {};
        bool midSide = false;
        bool bypassed = false;
    };

    static float attackMsFor (float speed) noexcept    { return 3.0f * std::pow (20.0f, juce::jlimit (0.0f, 1.0f, speed)); }    // 3 .. 60 ms
    static float releaseMsFor (float speed) noexcept   { return 40.0f * std::pow (15.0f, juce::jlimit (0.0f, 1.0f, speed)); }   // 40 .. 600 ms

    //==========================================================================
    void prepare (double newSampleRate, int maximumBlock, const Settings& initial)
    {
        sampleRate = newSampleRate;
        block = juce::jmax (1, maximumBlock);
        input.setSize (2, block);
        for (auto& b : bands) b.setSize (2, block);
        for (auto& c : comps) c.prepare (sampleRate, compressorSettings (initial.mid[0], true));
        for (auto& p : splitState) p = {};
        for (auto& v : split) v.reset (sampleRate, 0.05);
        for (int s = 0; s < numSplits; ++s) split[(size_t) s].setCurrentAndTargetValue (initial.splitHz[(size_t) s]);
        bypassFade.reset (sampleRate, 0.02);
        for (auto& v : bandLevel) v.reset (sampleRate, 0.02);
        for (auto& pair : bandGain) for (auto& v : pair) v.reset (sampleRate, 0.03);
        settings = initial;
        playingMidSide = initial.midSide;
        modeFade = 1.0f; modeChanging = false;
        setSettings (initial, true);
        recalculateSplits();
    }

    void setSettings (const Settings& s, bool jump = false)
    {
        settings = s;
        for (int sp = 0; sp < numSplits; ++sp)
        {
            // (the splits kept in order, at least a third of an octave apart)
            float hz = s.splitHz[(size_t) sp];
            if (sp > 0) hz = juce::jmax (hz, s.splitHz[(size_t) sp - 1] * 1.26f);
            if (jump) split[(size_t) sp].setCurrentAndTargetValue (hz); else split[(size_t) sp].setTargetValue (hz);
        }
        bool anySolo = false;
        for (bool so : s.solo) anySolo = anySolo || so;
        for (int b = 0; b < numBands; ++b)
        {
            const float heard = (! anySolo || s.solo[(size_t) b]) ? 1.0f : 0.0f;
            if (jump) bandLevel[(size_t) b].setCurrentAndTargetValue (heard); else bandLevel[(size_t) b].setTargetValue (heard);
        }
        if (jump) bypassFade.setCurrentAndTargetValue (s.bypassed ? 1.0f : 0.0f); else bypassFade.setTargetValue (s.bypassed ? 1.0f : 0.0f);
        applyCompressorSettings();
    }

    /** Works on left and right in place. */
    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int total = buffer.getNumSamples();
        const bool stereo = buffer.getNumChannels() >= 2;
        if (settings.midSide != playingMidSide && ! modeChanging)
            modeChanging = true;

        for (int start = 0; start < total; start += block)
        {
            const int n = juce::jmin (block, total - start);
            float* left = buffer.getWritePointer (0) + start;
            float* right = stereo ? buffer.getWritePointer (1) + start : nullptr;

            // In: left / right, or middle / sides
            float* a = input.getWritePointer (0);
            float* s = input.getWritePointer (1);
            for (int i = 0; i < n; ++i)
            {
                const float l = left[i], r = stereo ? right[i] : left[i];
                if (playingMidSide) { a[i] = 0.5f * (l + r); s[i] = 0.5f * (l - r); }
                else                { a[i] = l; s[i] = r; }
            }

            // Split (re-working the filters every 32 samples while a split glides)
            for (int i0 = 0; i0 < n; i0 += 32)
            {
                const int len = juce::jmin (32, n - i0);
                bool gliding = false;
                for (auto& v : split) if (v.isSmoothing()) { v.skip (len); gliding = true; }
                if (gliding) recalculateSplits();
                for (int c = 0; c < 2; ++c)
                    for (int i = i0; i < i0 + len; ++i)
                    {
                        const double x = input.getSample (c, i);
                        auto& st = splitState[(size_t) c];
                        // split 1: LOW | rest; split 2: LOW MID | rest; split 3: HIGH MID | HIGH -
                        // with all-passes so every band gets the same phase turns
                        double low = st.lp[0][1].run (st.lp[0][0].run (x, coef.lp[0]), coef.lp[0]);
                        double rest = st.hp[0][1].run (st.hp[0][0].run (x, coef.hp[0]), coef.hp[0]);
                        low = st.ap[0].run (low, coef.ap[1]);
                        low = st.ap[1].run (low, coef.ap[2]);
                        double lowMid = st.lp[1][1].run (st.lp[1][0].run (rest, coef.lp[1]), coef.lp[1]);
                        double rest2 = st.hp[1][1].run (st.hp[1][0].run (rest, coef.hp[1]), coef.hp[1]);
                        lowMid = st.ap[2].run (lowMid, coef.ap[2]);
                        const double highMid = st.lp[2][1].run (st.lp[2][0].run (rest2, coef.lp[2]), coef.lp[2]);
                        const double high = st.hp[2][1].run (st.hp[2][0].run (rest2, coef.hp[2]), coef.hp[2]);
                        bands[0].setSample (c, i, (float) low);
                        bands[1].setSample (c, i, (float) lowMid);
                        bands[2].setSample (c, i, (float) highMid);
                        bands[3].setSample (c, i, (float) high);
                    }
            }

            // Compress each band: STEREO = one linked compressor on both channels; MID + SIDE = the middle and the sides each their own
            for (int b = 0; b < numBands; ++b)
            {
                if (playingMidSide)
                {
                    juce::AudioBuffer<float> m (bands[(size_t) b].getArrayOfWritePointers(), 1, 0, n);
                    juce::AudioBuffer<float> sd (bands[(size_t) b].getArrayOfWritePointers() + 1, 1, 0, n);
                    reduction[(size_t) b][0] = comps[(size_t) b].process (m).maxReductionDb;
                    reduction[(size_t) b][1] = comps[(size_t) b + numBands].process (sd).maxReductionDb;
                }
                else
                {
                    juce::AudioBuffer<float> both (bands[(size_t) b].getArrayOfWritePointers(), 2, 0, n);
                    reduction[(size_t) b][0] = reduction[(size_t) b][1] = comps[(size_t) b].process (both).maxReductionDb;
                }
                bandPeak[(size_t) b][0] = bands[(size_t) b].getMagnitude (0, 0, n);
                bandPeak[(size_t) b][1] = bands[(size_t) b].getMagnitude (1, 0, n);
            }

            // Out: the bands added back up (SOLO mutes the others), back to left / right, BYPASS
            const float step = 1.0f / (0.01f * (float) sampleRate);
            for (int i = 0; i < n; ++i)
            {
                double outA = 0.0, outS = 0.0;
                for (int b = 0; b < numBands; ++b)
                {
                    const float heard = bandLevel[(size_t) b].getNextValue();
                    const float ga = bandGain[(size_t) b][0].getNextValue(), gs = bandGain[(size_t) b][1].getNextValue();
                    outA += bands[(size_t) b].getSample (0, i) * heard * ga;
                    outS += bands[(size_t) b].getSample (1, i) * heard * (playingMidSide ? gs : ga);
                }
                float l = (float) outA, r = (float) outS;
                if (playingMidSide) { l = (float) (outA + outS); r = (float) (outA - outS); }

                if (modeChanging || modeFade < 1.0f)
                {
                    modeFade = juce::jlimit (0.0f, 1.0f, modeFade + (modeChanging ? -step : step));
                    const float level = modeFade * modeFade * (3.0f - 2.0f * modeFade);
                    l *= level; r *= level;
                    if (modeChanging && modeFade <= 0.0f)
                    {
                        playingMidSide = settings.midSide;
                        modeChanging = false;
                        for (auto& p : splitState) p = {};
                        for (auto& c : comps) c.prepare (sampleRate, compressorSettings (settings.mid[0], true));
                        applyCompressorSettings (true);
                    }
                }

                const float bp = bypassFade.getNextValue();
                const float dryL = left[i], dryR = stereo ? right[i] : left[i];
                left[i] = bp >= 1.0f ? dryL : l * (1.0f - bp) + dryL * bp;
                if (stereo) right[i] = bp >= 1.0f ? dryR : r * (1.0f - bp) + dryR * bp;
            }
        }
    }

    /** For the window: how much band b's middle (part 0, or both in STEREO) or sides (1) is turned down (dB), and its peak level. */
    float getReductionDb (int band, int part) const noexcept   { return reduction[(size_t) band][(size_t) part]; }
    float getBandPeak (int band, int part) const noexcept      { return bandPeak[(size_t) band][(size_t) part]; }

private:
    static Compressor::Settings compressorSettings (const Band& b, bool on) noexcept
    {
        Compressor::Settings s;
        s.thresholdDb = b.thresholdDb;
        s.ratio = b.ratio;
        s.attackMs = attackMsFor (b.speed);
        s.releaseMs = releaseMsFor (b.speed);
        s.makeupDb = 0.0f;
        s.mix = 1.0f;
        s.lowCutHz = Compressor::lowCutOffHz;
        s.reactToAverage = true;
        s.bypassed = ! on;
        return s;
    }

    void applyCompressorSettings (bool jump = false) noexcept
    {
        for (int b = 0; b < numBands; ++b)
        {
            const bool on = settings.bandOn[(size_t) b];
            const auto& midBand = settings.mid[(size_t) b];
            const auto& sideBand = playingMidSide ? settings.side[(size_t) b] : settings.mid[(size_t) b];
            auto ms = compressorSettings (midBand, on), ss = compressorSettings (sideBand, on);
            // (the band's GAIN, as the compressor's make-up - it can go down as well as up)
            comps[(size_t) b].setSettings (ms, jump);
            comps[(size_t) b + numBands].setSettings (ss, jump);
            gainTarget[(size_t) b][0] = on ? midBand.gainDb : 0.0f;
            gainTarget[(size_t) b][1] = on ? sideBand.gainDb : 0.0f;
        }
        applyBandGains (jump);
    }

    void applyBandGains (bool jump) noexcept
    {
        for (int b = 0; b < numBands; ++b)
            for (int p = 0; p < 2; ++p)
            {
                const float g = juce::Decibels::decibelsToGain (juce::jlimit (-12.0f, 12.0f, gainTarget[(size_t) b][(size_t) p]));
                if (jump) bandGain[(size_t) b][(size_t) p].setCurrentAndTargetValue (g); else bandGain[(size_t) b][(size_t) p].setTargetValue (g);
            }
    }

    struct Coef { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct Section
    {
        double z1 = 0, z2 = 0;
        double run (double x, const Coef& c) noexcept
        {
            const double y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;
            return y;
        }
    };
    struct SplitState { std::array<std::array<Section, 2>, numSplits> lp, hp; std::array<Section, 3> ap; };
    struct SplitCoefs { std::array<Coef, numSplits> lp, hp, ap; } coef;

    static Coef design (int kind, double hz, double sr) noexcept   // 0 = low-pass, 1 = high-pass, 2 = all-pass; Q = 1/sqrt2 (Butterworth)
    {
        const double w = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, sr * 0.45, hz) / sr;
        const double alpha = std::sin (w) / (2.0 * 0.70710678118654752), cw = std::cos (w), a0 = 1.0 + alpha;
        Coef c;
        if (kind == 0)      { c.b0 = (1 - cw) / 2; c.b1 = 1 - cw; c.b2 = (1 - cw) / 2; }
        else if (kind == 1) { c.b0 = (1 + cw) / 2; c.b1 = -(1 + cw); c.b2 = (1 + cw) / 2; }
        else                { c.b0 = 1 - alpha; c.b1 = -2 * cw; c.b2 = 1 + alpha; }
        c.a1 = -2 * cw; c.a2 = 1 - alpha;
        c.b0 /= a0; c.b1 /= a0; c.b2 /= a0; c.a1 /= a0; c.a2 /= a0;
        return c;
    }

    void recalculateSplits() noexcept
    {
        for (int s = 0; s < numSplits; ++s)
        {
            const double hz = split[(size_t) s].getCurrentValue();
            coef.lp[(size_t) s] = design (0, hz, sampleRate);
            coef.hp[(size_t) s] = design (1, hz, sampleRate);
            coef.ap[(size_t) s] = design (2, hz, sampleRate);
        }
    }

    Settings settings;
    double sampleRate = 48000.0;
    int block = 512;
    juce::AudioBuffer<float> input;
    std::array<juce::AudioBuffer<float>, numBands> bands;
    std::array<Compressor, numBands * 2> comps;                // 0-3: the middle (or both), 4-7: the sides
    std::array<SplitState, 2> splitState;
    std::array<juce::SmoothedValue<float>, numSplits> split;
    std::array<juce::SmoothedValue<float>, numBands> bandLevel;   // (SOLO)
    std::array<std::array<juce::SmoothedValue<float>, 2>, numBands> bandGain;   // (each band's GAIN: the middle / sides)
    std::array<std::array<float, 2>, numBands> reduction {}, bandPeak {}, gainTarget {};
    juce::SmoothedValue<float> bypassFade;
    bool playingMidSide = false, modeChanging = false;
    float modeFade = 1.0f;
};
