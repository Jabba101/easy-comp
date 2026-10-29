#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "LoudnessMatcher.h"
#include <array>
#include <cmath>
#include <complex>

//==============================================================================
/**
    Easy EQ's sound: a four-band equaliser in the spirit of a classic 550-style
    console EQ, with high- and low-pass filters, mid-side, and MONO BASS.

      - Four overlapping bands: LOW (30-400 Hz), LOW MID (75 Hz-1.2 kHz),
        HIGH MID (800 Hz-8 kHz), HIGH (2.5-20 kHz). LOW and HIGH can be a
        BELL or a SHELF.
      - Proportional Q: no width knob. The harder you boost or cut, the
        narrower a band gets - about an octave and a half wide at +-2 dB, a
        third of an octave at +-12 dB. Gentle moves stay broad and musical,
        big ones get precise.
      - HPF (20-500 Hz) and LPF (2-20 kHz), OFF at their ends, 12 or 24 dB
        per octave each (Butterworth).
      - Mid-side: each band works on the whole stereo sound (ST), only the
        middle (MID: what's the same in both speakers - vocals, kick, bass,
        snare) or only the sides (SIDE: what's different - room, width,
        stereo guitars). MONO BASS keeps everything below its frequency in
        the middle (a 24 dB/oct high-pass on the sides only).

    Every filter is "matched" (after Vicanek, "Matched Second Order Digital
    Filters", 2016): ordinary digital EQ filters bend out of shape towards
    the top of the range (cramping), so a 12 kHz bell or shelf sounds and
    looks different from the analogue one it's modelled on. Matched filters
    take their poles from the analogue filter directly and choose their
    zeros so the response is exactly right at 0 Hz, at the band's frequency
    and at the very top (half the sample rate), which keeps the whole curve
    correct up to 20 kHz with no oversampling.

    Mid-side works by turning left/right into mid/side, equalising those,
    and turning them back. A band set to ST works on both, so it's exactly
    the same as equalising left and right. On a mono track there's only the
    middle.

    Knobs glide (30 ms), and the filters are recalculated every 32 samples
    while they do. A band at 0 dB, or a filter at OFF, is skipped entirely.
*/
class Equalizer
{
public:
    enum class Target { stereo = 0, mid = 1, side = 2 };

    static constexpr int numBands = 4;
    static constexpr float hpfOffHz = 20.0f, lpfOffHz = 20000.0f, monoBassOffHz = 20.0f;
    static constexpr float maxGainDb = 12.0f;

    struct Band
    {
        float freqHz = 1000.0f;
        float gainDb = 0.0f;
        bool  shelf  = false;        // LOW and HIGH only
        Target target = Target::stereo;
    };

    struct Settings
    {
        std::array<Band, numBands> bands {{ { 100.0f, 0.0f, true },  { 300.0f, 0.0f, false },
                                            { 2500.0f, 0.0f, false }, { 10000.0f, 0.0f, true } }};
        float hpfHz = hpfOffHz, lpfHz = lpfOffHz, monoBassHz = monoBassOffHz;
        bool  hpfSteep = false, lpfSteep = false;   // 24 dB/oct instead of 12
        bool  autoGain = false;
        bool  bypassed = false;
        int   listenBand = -1;                      // LISTEN: the band being listened to, or -1
    };

    //==========================================================================
    // Filter design (also used by the window, to draw the curve)
    //==========================================================================
    struct Coefficients
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

        /** The filter's gain (not in dB) at a frequency. */
        double magnitudeAt (double hz, double sampleRate) const noexcept
        {
            const double w = juce::MathConstants<double>::twoPi * hz / sampleRate;
            const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
            return std::abs ((b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2));
        }
    };

    /** Proportional Q: how narrow a bell gets at this much boost or cut. */
    static double qForGain (double gainDb) noexcept
    {
        return 0.75 * std::pow (5.733, juce::jmin (std::abs (gainDb), 15.0) / 12.0);
    }

    /** A matched biquad from an analogue one, H(s) = (n2 s^2 + n1 s + n0) / (d2 s^2 + d1 s + d0),
        with s normalised so the filter's frequency is 1, at hz. */
    static Coefficients matched (double n2, double n1, double n0, double d2, double d1, double d0, double hz, double sampleRate) noexcept
    {
        const double pi = juce::MathConstants<double>::pi;
        const double w0 = 2.0 * pi * juce::jlimit (1.0, sampleRate * 0.4999, hz) / sampleRate;

        // The poles: straight from the analogue filter's (impulse invariance)
        const double naturalFrequency = std::sqrt (d0 / d2) * w0;
        const double zeta = d1 / (2.0 * std::sqrt (d0 * d2));
        const double w = juce::jmin (naturalFrequency, pi * 0.999);
        Coefficients c;
        c.a2 = std::exp (-2.0 * zeta * w);
        c.a1 = zeta <= 1.0 ? -2.0 * std::exp (-zeta * w) * std::cos (w * std::sqrt (1.0 - zeta * zeta))
                           : -2.0 * std::exp (-zeta * w) * std::cosh (w * std::sqrt (zeta * zeta - 1.0));

        // The analogue filter's gain (squared) at a digital frequency
        const auto analogue = [&] (double digitalW)
        {
            const double x = digitalW / w0;   // normalised frequency
            const std::complex<double> s (0.0, x);
            const auto h = (n2 * s * s + n1 * s + n0) / (d2 * s * s + d1 * s + d0);
            return std::norm (h);
        };

        // |P(e^jw)|^2 = P0 phi0 + P1 phi1 + P2 phi2 for any quadratic P
        const auto phis = [] (double digitalW)
        {
            const double s = std::sin (digitalW * 0.5), phi1 = s * s, phi0 = 1.0 - phi1;
            return std::array<double, 3> { phi0, phi1, 4.0 * phi0 * phi1 };
        };

        const double A0 = (1.0 + c.a1 + c.a2) * (1.0 + c.a1 + c.a2);
        const double A1 = (1.0 - c.a1 + c.a2) * (1.0 - c.a1 + c.a2);
        const double A2 = -4.0 * c.a2;

        // The zeros: right at 0 Hz, at the filter's frequency and at the top
        const double B0 = analogue (0.0) * A0;
        const double B1 = analogue (pi) * A1;
        const auto p = phis (w0);
        const double denominatorAtW0 = A0 * p[0] + A1 * p[1] + A2 * p[2];
        const double B2 = (analogue (w0) * denominatorAtW0 - B0 * p[0] - B1 * p[1]) / p[2];

        const double sumRoot = std::sqrt (juce::jmax (0.0, B0)), differenceRoot = std::sqrt (juce::jmax (0.0, B1));
        const double W = 0.5 * (sumRoot + differenceRoot);
        c.b0 = 0.5 * (W + std::sqrt (juce::jmax (0.0, W * W + B2)));
        c.b1 = 0.5 * (sumRoot - differenceRoot);
        c.b2 = W - c.b0;
        return c;
    }

    static Coefficients bell (double hz, double gainDb, double sampleRate) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0), q = qForGain (gainDb);
        return matched (1.0, A / q, 1.0, 1.0, 1.0 / (A * q), 1.0, hz, sampleRate);
    }

    static Coefficients lowShelf (double hz, double gainDb, double sampleRate) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0), k = std::sqrt (A) / juce::MathConstants<double>::sqrt2 * 2.0;   // (Q = 1/sqrt2)
        return matched (A, A * k, A * A, A, k, 1.0, hz, sampleRate);   // = A (s^2 + sqrtA/Q s + A) / (A s^2 + sqrtA/Q s + 1)
    }

    static Coefficients highShelf (double hz, double gainDb, double sampleRate) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0), k = std::sqrt (A) / juce::MathConstants<double>::sqrt2 * 2.0;
        return matched (A * A, A * k, A, 1.0, k, A, hz, sampleRate);   // = A (A s^2 + sqrtA/Q s + 1) / (s^2 + sqrtA/Q s + A)
    }

    static Coefficients lowPass (double hz, double q, double sampleRate) noexcept
    {
        return matched (0.0, 0.0, 1.0, 1.0, 1.0 / q, 1.0, hz, sampleRate);
    }

    static Coefficients highPass (double hz, double q, double sampleRate) noexcept
    {
        return matched (1.0, 0.0, 0.0, 1.0, 1.0 / q, 1.0, hz, sampleRate);
    }

    /** Butterworth Qs: one section for 12 dB/oct, two for 24 */
    static constexpr double q12 = 0.70710678, q24a = 0.54119610, q24b = 1.30656296;

    /** The whole EQ's gain (not in dB) at a frequency, for the middle (mid = true) or the sides,
        as the window draws it (and as MASTER will build its filter from). */
    static double responseAt (const Settings& s, double hz, double sampleRate, bool mid) noexcept
    {
        double g = 1.0;

        for (int b = 0; b < numBands; ++b)
        {
            const auto& band = s.bands[(size_t) b];
            const bool applies = band.target == Target::stereo || (band.target == Target::mid) == mid;

            if (applies && std::abs (band.gainDb) > 0.001f)
                g *= bandCoefficients (b, band, sampleRate).magnitudeAt (hz, sampleRate);
        }

        if (s.hpfHz > hpfOffHz + 0.5f)
        {
            if (s.hpfSteep) g *= highPass (s.hpfHz, q24a, sampleRate).magnitudeAt (hz, sampleRate) * highPass (s.hpfHz, q24b, sampleRate).magnitudeAt (hz, sampleRate);
            else            g *= highPass (s.hpfHz, q12, sampleRate).magnitudeAt (hz, sampleRate);
        }

        if (s.lpfHz < lpfOffHz - 0.5f)
        {
            if (s.lpfSteep) g *= lowPass (s.lpfHz, q24a, sampleRate).magnitudeAt (hz, sampleRate) * lowPass (s.lpfHz, q24b, sampleRate).magnitudeAt (hz, sampleRate);
            else            g *= lowPass (s.lpfHz, q12, sampleRate).magnitudeAt (hz, sampleRate);
        }

        if (! mid && s.monoBassHz > monoBassOffHz + 0.5f)
            g *= highPass (s.monoBassHz, q24a, sampleRate).magnitudeAt (hz, sampleRate) * highPass (s.monoBassHz, q24b, sampleRate).magnitudeAt (hz, sampleRate);

        return g;
    }

    static Coefficients bandCoefficients (int index, const Band& band, double sampleRate) noexcept
    {
        if (band.shelf && index == 0)               return lowShelf (band.freqHz, band.gainDb, sampleRate);
        if (band.shelf && index == numBands - 1)    return highShelf (band.freqHz, band.gainDb, sampleRate);
        return bell (band.freqHz, band.gainDb, sampleRate);
    }

    //==========================================================================
    // Processing
    //==========================================================================
    void prepare (double newSampleRate, const Settings& initial)
    {
        sampleRate = newSampleRate;
        settings = initial;

        for (auto& b : bandState)
        {
            b.freq.reset (sampleRate, glide);  b.gain.reset (sampleRate, glide);
            b.toMid.reset (sampleRate, glide); b.toSide.reset (sampleRate, glide);
            b.shelfAmount.reset (sampleRate, glide);
        }

        for (auto* f : { &hpf, &lpf, &monoBass })
        {
            f->freq.reset (sampleRate, glide);
            f->amount.reset (sampleRate, glide);
            f->steep.reset (sampleRate, glide);
        }

        bypassFade.reset (sampleRate, 0.02);
        listenFade.reset (sampleRate, 0.02);

        inEar.prepare (sampleRate, 100.0f);
        outEar.prepare (sampleRate, 100.0f);

        setSettings (initial, true);
        reset();
    }

    void reset() noexcept
    {
        for (auto& b : bandState)
            for (auto& f : b.filter) f = {};
        for (auto* f : { &hpf, &lpf, &monoBass })
            for (auto& ch : f->filters) for (auto& s : ch) s = {};
        for (auto& l : listenFilters) for (auto& s : l) s = {};
        listener = {};
        countdown = 0;
    }

    void setSettings (const Settings& s, bool jump = false)
    {
        settings = s;

        const auto set = [jump] (juce::SmoothedValue<float>& v, float target)
        {
            if (jump) v.setCurrentAndTargetValue (target);
            else      v.setTargetValue (target);
        };

        for (int b = 0; b < numBands; ++b)
        {
            const auto& band = s.bands[(size_t) b];
            auto& st = bandState[(size_t) b];
            set (st.freq, std::log (juce::jmax (10.0f, band.freqHz)));
            set (st.gain, band.gainDb);
            set (st.toMid, band.target != Target::side ? 1.0f : 0.0f);
            set (st.toSide, band.target != Target::mid ? 1.0f : 0.0f);
            set (st.shelfAmount, band.shelf ? 1.0f : 0.0f);
        }

        const auto setFilter = [&] (FilterState& f, float hz, bool on, bool steep)
        {
            if (on) set (f.freq, std::log (hz));
            else if (jump) f.freq.setCurrentAndTargetValue (std::log (hz));   // (off: keeps gliding nowhere)
            set (f.amount, on ? 1.0f : 0.0f);
            set (f.steep, steep ? 1.0f : 0.0f);
        };

        setFilter (hpf, s.hpfHz, s.hpfHz > hpfOffHz + 0.5f, s.hpfSteep);
        setFilter (lpf, s.lpfHz, s.lpfHz < lpfOffHz - 0.5f, s.lpfSteep);
        setFilter (monoBass, s.monoBassHz, s.monoBassHz > monoBassOffHz + 0.5f, true);

        set (bypassFade, s.bypassed ? 1.0f : 0.0f);
        set (listenFade, s.listenBand >= 0 ? 1.0f : 0.0f);
        if (s.listenBand >= 0)
            listening = s.listenBand;
    }

    /** Equalises the buffer in place (1 or 2 channels): just the EQ itself.
        Then call finish() for AUTO-GAIN, LISTEN and BYPASS. */
    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int channels = juce::jmin (2, buffer.getNumChannels());
        const int total = buffer.getNumSamples();

        for (int start = 0; start < total;)
        {
            if (countdown <= 0)
            {
                updateCoefficients();
                countdown = updateInterval;
            }

            const int n = juce::jmin (countdown, total - start);
            processRun (buffer, channels, start, n);
            countdown -= n;
            start += n;
        }
    }

    /** How many dB AUTO-GAIN is adding (or taking away) right now. */
    float getAutoGainDb() const noexcept   { return juce::Decibels::gainToDecibels (listener.trim); }

private:
    static constexpr double glide = 0.03;
    static constexpr int updateInterval = 32;

    struct Section   // one biquad, transposed direct form II, in double for low-frequency accuracy
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        void set (const Coefficients& c) noexcept   { b0 = c.b0; b1 = c.b1; b2 = c.b2; a1 = c.a1; a2 = c.a2; }

        double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    struct BandState
    {
        juce::SmoothedValue<float> freq, gain, toMid, toSide, shelfAmount;
        std::array<Section, 2> filter;            // [mid, side]
        std::array<bool, 2> active { false, false };
    };

    struct FilterState
    {
        juce::SmoothedValue<float> freq, amount, steep;
        std::array<std::array<Section, 3>, 2> filters;   // [mid, side][12 dB section, 24 dB section a, b]
        bool active = false;
        float amountNow = 0.0f, steepNow = 0.0f;
    };

    void updateCoefficients() noexcept
    {
        // Advance the glides by a whole run at once (they're read once per run)
        const auto advance = [] (juce::SmoothedValue<float>& v) { return v.skip (updateInterval); };

        for (int b = 0; b < numBands; ++b)
        {
            auto& st = bandState[(size_t) b];
            const float hz = std::exp (advance (st.freq));
            const float gain = advance (st.gain);
            const float toMid = advance (st.toMid), toSide = advance (st.toSide);
            const float shelfAmount = advance (st.shelfAmount);

            // (A band switching between BELL and SHELF glides through 0 dB, so it never clicks.)
            const float shelfFade = std::abs (shelfAmount - 0.5f) * 2.0f;
            const bool shelf = shelfAmount > 0.5f;

            for (int m = 0; m < 2; ++m)
            {
                const float g = gain * (m == 0 ? toMid : toSide) * shelfFade;
                st.active[(size_t) m] = std::abs (g) > 0.0005f;

                if (st.active[(size_t) m])
                {
                    Band band { hz, g, shelf, Target::stereo };
                    st.filter[(size_t) m].set (bandCoefficients (b, band, sampleRate));
                }
            }
        }

        const auto updateFilter = [&] (FilterState& f, bool isHighPass)
        {
            const float hz = std::exp (advance (f.freq));
            f.amountNow = advance (f.amount);
            f.steepNow = advance (f.steep);
            f.active = f.amountNow > 0.0001f;

            if (f.active)
            {
                const auto make = [&] (double q) { return isHighPass ? highPass (hz, q, sampleRate) : lowPass (hz, q, sampleRate); };
                const auto c12 = make (q12), ca = make (q24a), cb = make (q24b);

                for (auto& ch : f.filters)
                {
                    ch[0].set (c12); ch[1].set (ca); ch[2].set (cb);
                }
            }
        };

        updateFilter (hpf, true);
        updateFilter (lpf, false);
        updateFilter (monoBass, true);

        // LISTEN: what the chosen band covers
        if (listening >= 0)
        {
            const auto& band = settings.bands[(size_t) listening];
            const float hz = std::exp (bandState[(size_t) listening].freq.getCurrentValue());
            Coefficients c;

            if (band.shelf && listening == 0)                  c = lowPass (hz * 1.5, q12, sampleRate);
            else if (band.shelf && listening == numBands - 1)  c = highPass (hz / 1.5, q12, sampleRate);
            else
            {
                // A band-pass as wide as the band (at least 2/3 of an octave), peak at 0 dB
                const double q = juce::jmax (2.1, qForGain (band.gainDb));
                c = matched (0.0, 1.0 / q, 0.0, 1.0, 1.0 / q, 1.0, hz, sampleRate);
            }

            for (auto& l : listenFilters)
                for (auto& s : l) s.set (c);
        }
    }

    void processRun (juce::AudioBuffer<float>& buffer, int channels, int start, int n) noexcept
    {
        const bool stereo = channels == 2;
        float* left = buffer.getWritePointer (0, start);
        float* right = stereo ? buffer.getWritePointer (1, start) : nullptr;

        for (int i = 0; i < n; ++i)
        {
            const double l = left[i], r = stereo ? right[i] : l;

            // Into mid and side
            double m = stereo ? 0.5 * (l + r) : l;
            double s = stereo ? 0.5 * (l - r) : 0.0;

            for (auto& b : bandState)
            {
                if (b.active[0])            m = b.filter[0].process (m);
                if (stereo && b.active[1])  s = b.filter[1].process (s);
            }

            const auto runFilter = [] (FilterState& f, int which, double x)
            {
                auto& ch = f.filters[(size_t) which];
                const double gentle = ch[0].process (x);
                const double steep = ch[2].process (ch[1].process (x));
                const double filtered = gentle + (steep - gentle) * f.steepNow;
                return x + (filtered - x) * f.amountNow;
            };

            if (hpf.active)                       { m = runFilter (hpf, 0, m); if (stereo) s = runFilter (hpf, 1, s); }
            if (lpf.active)                       { m = runFilter (lpf, 0, m); if (stereo) s = runFilter (lpf, 1, s); }
            if (stereo && monoBass.active)        s = runFilter (monoBass, 1, s);

            // Back to left and right
            left[i] = (float) (stereo ? m + s : m);
            if (stereo)
                right[i] = (float) (m - s);
        }
    }

public:
    /** After the EQ (or MASTER's linear-phase version of it): AUTO-GAIN,
        LISTEN and BYPASS. `original` is the sound going in, lined up with
        `buffer` (so delayed the same as MASTER, while it's on). */
    void finish (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>& original) noexcept
    {
        const int channels = juce::jmin (2, buffer.getNumChannels(), original.getNumChannels());
        const int n = buffer.getNumSamples();
        const bool stereo = channels == 2;
        float* left = buffer.getWritePointer (0);
        float* right = stereo ? buffer.getWritePointer (1) : nullptr;
        const float* dryLeft = original.getReadPointer (0);
        const float* dryRight = stereo ? original.getReadPointer (1) : nullptr;
        double heardIn = 0.0, heardOut = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const double l = dryLeft[i], r = stereo ? dryRight[i] : l;
            double outL = left[i], outR = stereo ? right[i] : outL;

            if (settings.autoGain)
            {
                heardIn  += inEar.measure (0, (float) l)     + (stereo ? inEar.measure (1, (float) r) : 0.0);
                heardOut += outEar.measure (0, (float) outL) + (stereo ? outEar.measure (1, (float) outR) : 0.0);
                outL *= listener.trim;
                outR *= listener.trim;
            }

            // LISTEN: just what the band covers (of the original), faded in and out
            const float listenAmount = listenFade.getNextValue();
            if (listenAmount > 0.0f)
            {
                const double heardL = listenFilters[0][0].process (listenFilters[0][1].process (l));
                const double heardR = stereo ? listenFilters[1][0].process (listenFilters[1][1].process (r)) : heardL;
                outL += (heardL - outL) * listenAmount;
                outR += (heardR - outR) * listenAmount;
            }

            const float bypass = bypassFade.getNextValue();
            left[i] = (float) (outL + (l - outL) * bypass);
            if (stereo)
                right[i] = (float) (outR + (r - outR) * bypass);
        }

        if (settings.autoGain)
            trimTowards (heardIn, heardOut, n);
    }

private:
    // AUTO-GAIN: compares how loud it sounds going in and coming out (the
    // last couple of seconds, as ears weigh loudness) and trims to match.
    // Quick to turn down, gentle to turn up, at most 12 dB either way.
    void trimTowards (double heardIn, double heardOut, int n) noexcept
    {
        auto& l = listener;
        if (heardIn / n < 1.0e-9)
            return;   // (silence: hold)

        const double rate = 1.0 - std::exp (-(double) n / (sampleRate * 1.5));
        l.in += rate * (heardIn / n - l.in);
        l.out += rate * (heardOut / n - l.out);   // (measured before the trim)

        const double wanted = juce::jlimit (0.25, 4.0, std::sqrt (l.in / juce::jmax (l.out, 1.0e-20)));
        const double speed = wanted < l.trim ? rate * 3.0 : rate;
        l.trim = (float) (l.trim + speed * (wanted - l.trim));
    }

    double sampleRate = 48000.0;
    Settings settings;

    std::array<BandState, numBands> bandState;
    FilterState hpf, lpf, monoBass;
    juce::SmoothedValue<float> bypassFade, listenFade;
    std::array<std::array<Section, 2>, 2> listenFilters;   // [left, right][two in a row: steeper]
    int listening = -1;
    int countdown = 0;

    LoudnessMatcher inEar, outEar;   // (only their ear filters are used)

    struct Listener
    {
        double in = 0.0, out = 0.0;
        float trim = 1.0f;
    } listener;
};
