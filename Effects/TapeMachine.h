#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "FastMath.h"
#include <array>
#include <cmath>
#include <complex>
#include <memory>
#include <vector>

//==============================================================================
/**
    Easy Tape's sound: a professional reel-to-reel machine, recording and
    playing back - the subtle thing real tape does, not the lo-fi
    caricature. (The worn-out cassette is there too, for lo-fi, but it's
    its own setting.)

    What real tape does, and what this copies:

      - Near flat. A good machine at 15 or 30 inches per second, lined up
        properly, measures within a dB or so from 30 Hz to 18 kHz. Nothing
        here pulls the top end down at a normal level: at 15 ips the top
        is down 1 dB at 20 kHz, at 30 ips about half that.
      - The head bump. The playback head picks up long wavelengths
        (deep bass) better than its gap should, then can't at all: a gentle
        rise in the lows with a dip and a roll-off below it. Where it sits
        depends on the speed - at 15 ips about +2 dB around 50 Hz (the deep,
        round low end people mean by "tape"), at 30 ips about +1 dB nearer
        100 Hz, with a dip under it.
      - Saturation that grows with the level. Tape bends the loud parts
        gradually and symmetrically (mostly the 3rd harmonic, a little 2nd
        from the bias): at the normal operating level (0 VU) it's a few
        tenths of a percent - you feel it rather than hear it; pushed 6 to
        10 dB over, the peaks round off and the sound thickens.
      - The highs compress first, and only when pushed. A tape machine
        boosts the highs on the way onto the tape (record emphasis) and
        cuts them by exactly as much on the way off (playback
        de-emphasis). Quietly, the two cancel: flat. Loud, the boosted
        highs hit the tape's limit sooner, so cymbals, S's and edges get
        softened - but only the loud ones, and only as hard as you push.
        Here the saturation sits between the two, so that's what happens.
        The speed sets how much: 15 ips (50 us) some, 30 ips (17.5 us)
        little, cassette (120 us) a lot.
      - Wow & flutter (a slow drift and a fast shimmer in the speed) and
        hiss: off unless you want them. On a good machine they're tiny.

    The speeds:
      15 IPS     the multitrack setting, and the default: the deep head bump,
                 gentle HF softening when pushed.
      30 IPS     the mastering machine: almost perfect - a smaller bump
                 higher up, more headroom, the top barely touched. A subtle
                 touch.
      CASSETTE   1 7/8 ips: lo-fi. Less headroom, the highs compress early
                 and roll off (-3 dB around 12 kHz), more wow & flutter and
                 hiss when you turn those up.

    DRIVE is the record level: 0 dB = a track at -18 dBFS (RMS) hits the
    tape at the normal operating level (0 VU). The playback level follows
    it the other way, so DRIVE changes how hard the tape works, not the
    volume. AUTO-GAIN then trims what the saturation takes off (a dB or
    so when pushed).

    OVERSAMPLING as Easy Amp: "4x / 8x" (4x while mixing, 8x when
    exporting), OFF, 4x, 8x, always the 8x delay (plus the short fixed
    delay that wow & flutter swings around), so nothing shifts in time.
*/
class TapeMachine
{
public:
    enum class Quality { off = 0, times4 = 1, times8 = 2 };
    enum class Speed   { ips15 = 0, ips30 = 1, cassette = 2 };

    static constexpr float minDriveDb = -12.0f, maxDriveDb = 18.0f;

    struct Settings
    {
        Speed speed       = Speed::ips15;
        float driveDb     = 0.0f;     // record level, -12 .. +18 dB
        float wowFlutter  = 0.0f;     // 0 .. 1
        float hiss        = 0.0f;     // 0 .. 1
        bool  autoGain    = true;
        Quality quality   = Quality::times4;
        bool  bypassed    = false;
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

        // Wow & flutter swing the playback time around a short fixed delay
        flutterCentre = (int) std::ceil (sampleRate * flutterCentreSeconds);
        latency = oversamplerLatency[1] + flutterCentre;

        int ringSize = 1;
        while (ringSize < latency + chunkSize + 8)
            ringSize <<= 1;

        ringMask = ringSize - 1;
        ringStride = ringSize;
        for (auto* r : { &originalRing, &tapeRing, &flutterRing })
            r->assign ((size_t) (numChannels * ringSize), 0.0f);
        writePos = 0;

        work.setSize (numChannels, chunkSize);

        driftCoefficient = onePoleCoefficient (0.3, sampleRate);
        driftScale = 0.5f / std::sqrt (driftCoefficient / 12.0f / 4.0f);   // (so the wander swings about +-0.5, at any sample rate)

        drive.reset (sampleRate, 0.05);
        wowAmount.reset (sampleRate, 0.1);
        hissAmount.reset (sampleRate, 0.05);
        bypassFade.reset (sampleRate, 0.02);

        currentQuality = initial.quality;
        currentSpeed = initial.speed;
        modeFade = 1.0f;
        fadingOut = false;
        setSettings (initial, true);
        resetState();
    }

    /** The delay your DAW is told about (the same whatever OVERSAMPLING, SPEED or WOW & FLUTTER are set to). */
    int getLatencySamples() const noexcept   { return latency; }

    void setSettings (const Settings& s, bool jump = false)
    {
        const bool driveMoved = std::abs (s.driveDb - settings.driveDb) > 0.05f;
        settings = s;

        const auto set = [jump] (juce::SmoothedValue<float>& v, float target)
        {
            if (jump) v.setCurrentAndTargetValue (target);
            else      v.setTargetValue (target);
        };

        set (drive,      juce::Decibels::decibelsToGain (juce::jlimit (minDriveDb, maxDriveDb, s.driveDb)));
        set (wowAmount,  juce::jlimit (0.0f, 1.0f, s.wowFlutter));
        set (hissAmount, juce::jlimit (0.0f, 1.0f, s.hiss));
        set (bypassFade, s.bypassed ? 1.0f : 0.0f);

        if (driveMoved && ! jump)
            listener.quickenFor = (int) (sampleRate * 0.5);
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

        for (auto* r : { &originalRing, &tapeRing, &flutterRing })
            std::fill (r->begin(), r->end(), 0.0f);

        tape = {};
        playback = {};
        listener = {};
        flutter = {};
        vuStage1 = vuStage2 = 0.0;
        designFilters();
    }

    /** How many dB the level matching is adding (or taking away) right now. */
    float getMatchGainDb() const noexcept      { return matchGainDb; }

    /** How loud the sound hitting the tape is right now, in VU: 0 = the normal operating level. */
    float getRecordLevelVu() const noexcept    { return recordVu; }

    //==========================================================================
    /** The machine's frequency response at a quiet level (the head bump and
        the top), in dB, for the window's drawing. */
    static float responseDb (Speed speed, double hz, double rate = 96000.0) noexcept
    {
        const auto shape = shapeFor (speed);
        double db = 0.0;

        for (auto& f : playbackFilters (shape, rate))
            db += f.magnitudeDb (hz, rate);

        return (float) db;
    }

private:
    static constexpr int maxChannels = 2;
    static constexpr int chunkSize   = 512;
    static constexpr double flutterCentreSeconds = 0.003;   // wow & flutter swing up to about +-2 ms around this

    //==========================================================================
    // What each speed is like
    struct Shape
    {
        float headroom;           // where the tape bends (in the units the saturation works in)
        double emphasisHz;        // record / playback emphasis corner (1 / (2 pi tau))
        float emphasisBoostDb;    // how far the record emphasis lifts the very top
        double bumpHz;  float bumpDb;  float bumpQ;
        double dipHz;   float dipDb;   float dipQ;
        double lowCutHz;          // the roll-off under the bump (12 dB / octave)
        double topHz;   float topDb;  // a gentle shelf at the very top (the gap's loss)
        double cassetteTopHz;     // 0 = none; otherwise a 12 dB / octave roll-off
        float wowDepth, flutterDepth;   // speed deviation at 100% (fraction, e.g. 0.001 = 0.1%)
        float hissDb;             // hiss at 100%, dBFS RMS
        double hissToneHz;
    };

    static Shape shapeFor (Speed s) noexcept
    {
        switch (s)
        {
            case Speed::ips30:
                return { 1.3f, 1.0e6 / (juce::MathConstants<double>::twoPi * 17.5), 8.0f,
                         100.0, 1.1f, 1.1f,    45.0, -0.9f, 1.6f,   16.0,
                         19000.0, -0.6f,  0.0,
                         0.0005f, 0.0003f,  -66.0f, 9000.0 };

            case Speed::cassette:
                return { 0.7f, 1.0e6 / (juce::MathConstants<double>::twoPi * 120.0), 14.0f,
                         60.0, 1.5f, 1.0f,     28.0, -1.0f, 1.5f,   30.0,
                         9000.0, -1.5f,  12500.0,
                         0.0030f, 0.0015f,  -50.0f, 5500.0 };

            case Speed::ips15:
            default:
                return { 1.1f, 1.0e6 / (juce::MathConstants<double>::twoPi * 50.0), 10.0f,
                         50.0, 2.0f, 1.0f,     24.0, -1.2f, 1.5f,   13.0,
                         16000.0, -1.0f,  0.0,
                         0.0008f, 0.0004f,  -60.0f, 7500.0 };
        }
    }

    static constexpr float evenBias = 0.015f;  // a touch of lopsidedness from the bias: a little 2nd harmonic

    //==========================================================================
    // tanh with the suite's quick exp2: 1 - 2 / (e^2x + 1), odd-symmetric
    static float tanhOf (float x) noexcept
    {
        const float a = juce::jmin (std::abs (x), 20.0f);
        const float t = 1.0f - 2.0f / (FastMath::exp2 (a * 2.88539008f) + 1.0f);
        return x < 0.0f ? -t : t;
    }

    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;

        float process (float x) noexcept
        {
            const float y = (float) b0 * x + z1;
            z1 = (float) b1 * x - (float) a1 * y + z2;
            z2 = (float) b2 * x - (float) a2 * y;
            return y;
        }

        double magnitudeDb (double hz, double rate) const noexcept
        {
            const std::complex<double> z = std::polar (1.0, juce::MathConstants<double>::twoPi * hz / rate);
            const auto zi = 1.0 / z, zi2 = zi * zi;
            const auto h = (b0 + b1 * zi + b2 * zi2) / (1.0 + a1 * zi + a2 * zi2);
            return 20.0 * std::log10 (std::max (std::abs (h), 1.0e-12));
        }

        // The RBJ cookbook
        static Biquad peak (double hz, double q, double db, double rate)
        {
            const double A = std::pow (10.0, db / 40.0), w = omega (hz, rate), alpha = std::sin (w) / (2.0 * q), c = std::cos (w);
            return make (1 + alpha * A, -2 * c, 1 - alpha * A, 1 + alpha / A, -2 * c, 1 - alpha / A);
        }

        static Biquad highPass (double hz, double q, double rate)
        {
            const double w = omega (hz, rate), alpha = std::sin (w) / (2.0 * q), c = std::cos (w);
            return make ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
        }

        static Biquad lowPass (double hz, double q, double rate)
        {
            const double w = omega (hz, rate), alpha = std::sin (w) / (2.0 * q), c = std::cos (w);
            return make ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
        }

        static Biquad highShelf (double hz, double db, double rate)
        {
            const double A = std::pow (10.0, db / 40.0), w = omega (hz, rate), c = std::cos (w);
            const double alpha = std::sin (w) / 2.0 * std::sqrt (2.0);   // (slope 1)
            const double sA = 2.0 * std::sqrt (A) * alpha;
            return make (A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA),
                         (A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA);
        }

        static double omega (double hz, double rate)   { return juce::MathConstants<double>::twoPi * std::min (hz, rate * 0.45) / rate; }

        static Biquad make (double b0, double b1, double b2, double a0, double a1, double a2)
        {
            Biquad f;
            f.b0 = b0 / a0; f.b1 = b1 / a0; f.b2 = b2 / a0; f.a1 = a1 / a0; f.a2 = a2 / a0;
            return f;
        }
    };

    static constexpr int numPlaybackFilters = 5;

    // The playback side, at the normal rate: head bump, the dip under it,
    // the roll-off under that, the top shelf, and (cassette) the top roll-off.
    static std::array<Biquad, numPlaybackFilters> playbackFilters (const Shape& s, double rate)
    {
        std::array<Biquad, numPlaybackFilters> f;
        f[0] = Biquad::peak (s.bumpHz, s.bumpQ, s.bumpDb, rate);
        f[1] = Biquad::peak (s.dipHz, s.dipQ, s.dipDb, rate);
        f[2] = Biquad::highPass (s.lowCutHz, 0.6, rate);
        f[3] = Biquad::highShelf (s.topHz, s.topDb, rate);
        f[4] = s.cassetteTopHz > 0.0 ? Biquad::lowPass (s.cassetteTopHz, 0.6, rate) : Biquad();
        return f;
    }

    // First-order shelf: rises from 1 to "boost" above hz (record emphasis);
    // its exact opposite is the playback de-emphasis. Both are made the
    // same way, so quietly they cancel to the last bit.
    struct Shelf
    {
        float b0 = 1, b1 = 0, a1 = 0, z = 0;

        float process (float x) noexcept
        {
            const float y = b0 * x + z;
            z = b1 * x - a1 * y;
            return y;
        }

        static void design (Shelf& emphasis, Shelf& deEmphasis, double hz, float boostDb, double rate)
        {
            const double K = 2.0 * rate;
            const auto warp = [rate] (double f)  { return 2.0 * rate * std::tan (juce::MathConstants<double>::pi * std::min (f, rate * 0.42) / rate); };
            const double w1 = warp (hz), w2 = warp (hz * juce::Decibels::decibelsToGain ((double) boostDb));

            // (1 + s/w1) / (1 + s/w2), and its inverse
            const auto make = [K] (Shelf& f, double zero, double pole)
            {
                const double g = pole / zero;
                f.b0 = (float) (g * (zero + K) / (pole + K));
                f.b1 = (float) (g * (zero - K) / (pole + K));
                f.a1 = (float) ((pole - K) / (pole + K));
            };

            make (emphasis, w1, w2);
            make (deEmphasis, w2, w1);
        }
    };

    struct OnePole
    {
        float z = 0.0f;
        float lowPass (float x, float coefficient) noexcept   { z += coefficient * (x - z); return z; }
    };

    static float onePoleCoefficient (double hz, double rate) noexcept
    {
        return (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * juce::jmin (hz, rate * 0.45) / rate));
    }

    // At the working rate: the emphasis, the tape, the de-emphasis
    struct TapeState
    {
        std::array<Shelf, maxChannels> emphasis, deEmphasis;
    } tape;

    // At the normal rate: the playback filters, the bias offset's DC block, the listener's filters, the hiss
    struct PlaybackState
    {
        std::array<std::array<Biquad, numPlaybackFilters>, maxChannels> filters;
        std::array<OnePole, maxChannels> dcBlock, hearIn, hearOut, hissTone;
    } playback;

    //==========================================================================
    void designFilters() noexcept
    {
        const auto shape = shapeFor (currentSpeed);
        const int factor = currentQuality == Quality::off ? 1 : (currentQuality == Quality::times4 ? 4 : 8);
        const double rate = sampleRate * factor;

        const auto fresh = playbackFilters (shape, sampleRate);

        for (int ch = 0; ch < maxChannels; ++ch)
        {
            Shelf::design (tape.emphasis[(size_t) ch], tape.deEmphasis[(size_t) ch], shape.emphasisHz, shape.emphasisBoostDb, rate);

            for (size_t k = 0; k < fresh.size(); ++k)   // (new settings, same state)
            {
                auto& f = playback.filters[(size_t) ch][k];
                f.b0 = fresh[k].b0; f.b1 = fresh[k].b1; f.b2 = fresh[k].b2; f.a1 = fresh[k].a1; f.a2 = fresh[k].a2;
            }
        }

        activeShape = shape;
    }

    float& ring (std::vector<float>& r, int ch, int pos) noexcept   { return r[(size_t) (ch * ringStride + (pos & ringMask))]; }

    int latencyOf (Quality q) const noexcept   { return q == Quality::off ? 0 : oversamplerLatency[q == Quality::times4 ? 0 : 1]; }

    // One sample onto the tape and off again (at the working rate)
    static forcedinline float throughTape (float x, float headroom, float rest, float restSlope, Shelf& emphasis, Shelf& deEmphasis) noexcept
    {
        const float lifted = emphasis.process (x) / headroom;
        const float onTape = (tanhOf (lifted + evenBias) - rest) / restSlope;
        return deEmphasis.process (onTape * headroom);
    }

    //==========================================================================
    void processChunk (juce::AudioBuffer<float>& buffer, int channels, int start, int n) noexcept
    {
        if ((settings.quality != currentQuality || settings.speed != currentSpeed) && ! fadingOut)
            fadingOut = true;

        for (int i = 0; i < n; ++i)
        {
            driveNow[(size_t) i] = drive.getNextValue();
            wowNow[(size_t) i]   = wowAmount.getNextValue();
            hissNow[(size_t) i]  = hissAmount.getNextValue();
        }

        // In: keep the original; the record level
        const float hear = onePoleCoefficient (150.0, sampleRate);
        double heardIn = 0.0, recordAverage = 0.0;

        for (int ch = 0; ch < channels; ++ch)
        {
            const float* in = buffer.getReadPointer (ch, start);
            float* w = work.getWritePointer (ch);
            auto& hearIn = playback.hearIn[(size_t) ch];

            for (int i = 0; i < n; ++i)
            {
                ring (originalRing, ch, writePos + i) = in[i];
                w[i] = in[i] * driveNow[(size_t) i];
                recordAverage += (double) std::abs (w[i]);

                const float heard = in[i] - hearIn.lowPass (in[i], hear);
                heardIn += (double) heard * heard;
            }
        }

        // 0 VU = a sine at -18 dBFS RMS hitting the tape. Read the way a VU
        // meter reads: the average level (not the peaks), swinging over in
        // about 300 ms - so drums read lower than their peaks, as on a real
        // machine.
        const double vuRate = 1.0 - std::exp (-(double) n / (sampleRate * 0.065));
        vuStage1 += vuRate * (recordAverage / (double) (n * channels) - vuStage1);
        vuStage2 += vuRate * (vuStage1 - vuStage2);
        recordVu = (float) (20.0 * std::log10 (vuStage2 * 1.1107 + 1.0e-9)) + 18.0f;   // (1.11: a sine's average -> its RMS)

        // Onto the tape and off again
        const int factor = currentQuality == Quality::off ? 1 : (currentQuality == Quality::times4 ? 4 : 8);
        const float headroom = activeShape.headroom;
        const float rest = tanhOf (evenBias);
        const float restSlope = 1.0f - rest * rest;

        if (factor == 1)
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                float* w = work.getWritePointer (ch);
                for (int i = 0; i < n; ++i)
                    w[i] = throughTape (w[i], headroom, rest, restSlope, tape.emphasis[(size_t) ch], tape.deEmphasis[(size_t) ch]);
            }
        }
        else
        {
            auto& os = *oversamplers[currentQuality == Quality::times4 ? 0 : 1];
            juce::dsp::AudioBlock<float> block (work.getArrayOfWritePointers(), (size_t) channels, (size_t) n);
            auto up = os.processSamplesUp (block);

            for (int ch = 0; ch < channels; ++ch)
            {
                float* x = up.getChannelPointer ((size_t) ch);
                for (int j = 0; j < n * factor; ++j)
                    x[j] = throughTape (x[j], headroom, rest, restSlope, tape.emphasis[(size_t) ch], tape.deEmphasis[(size_t) ch]);
            }

            os.processSamplesDown (block);
        }

        // Playback: the level back down, the bias offset off, the head bump and the top
        const float dc = onePoleCoefficient (5.0, sampleRate);
        double heardOut = 0.0;

        for (int ch = 0; ch < channels; ++ch)
        {
            float* w = work.getWritePointer (ch);
            auto& filters = playback.filters[(size_t) ch];
            auto& dcBlock = playback.dcBlock[(size_t) ch];
            auto& hearOut = playback.hearOut[(size_t) ch];

            for (int i = 0; i < n; ++i)
            {
                float y = w[i] / driveNow[(size_t) i];
                y -= dcBlock.lowPass (y, dc);

                for (auto& f : filters)
                    y = f.process (y);

                w[i] = y;
                const float heard = y - hearOut.lowPass (y, hear);
                heardOut += (double) heard * heard;
            }
        }

        listen (heardIn, heardOut, n);
        const float trim = settings.autoGain ? listener.trim : 1.0f;
        matchGainDb = juce::Decibels::gainToDecibels (trim);

        const int pad = latency - flutterCentre - latencyOf (currentQuality);
        for (int ch = 0; ch < channels; ++ch)
        {
            const float* w = work.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
                ring (tapeRing, ch, writePos + i + pad) = w[i] * trim;
        }

        // Fades for an OVERSAMPLING or SPEED change
        float fadeStep = 0.0f;
        if (fadingOut)
            fadeStep = -modeFade / (float) n;
        else if (modeFade < 1.0f && holdFor <= 0)
            fadeStep = 1.0f / (float) fadeLength;

        holdFor -= n;

        // Out: through wow & flutter, plus hiss; or (BYPASS) the original
        const auto shape = activeShape;
        workOutFlutter (n, shape);
        const float hissFull = juce::Decibels::decibelsToGain (shape.hissDb) * 3.0f;
        const float hissTone = onePoleCoefficient (shape.hissToneHz, sampleRate);

        for (int i = 0; i < n; ++i)
        {
            const float bypass = bypassFade.getNextValue();
            modeFade = juce::jlimit (0.0f, 1.0f, modeFade + fadeStep);

            const float offset = offsetNow[(size_t) i];
            const float hissGain = hissNow[(size_t) i] * hissNow[(size_t) i] * hissFull;

            for (int ch = 0; ch < channels; ++ch)
            {
                const float original = ring (originalRing, ch, writePos + i - latency);
                ring (flutterRing, ch, writePos + i) = ring (tapeRing, ch, writePos + i);

                float played;
                if (std::abs (offset) < 1.0e-6f)
                    played = ring (flutterRing, ch, writePos + i - flutterCentre);
                else
                    played = readBetween (ch, (float) (writePos + i - flutterCentre) - offset);

                if (hissGain > 0.0f)
                    played += playback.hissTone[(size_t) ch].lowPass (random.nextFloat() * 2.0f - 1.0f, hissTone) * hissGain;

                played = original + (played - original) * modeFade;   // (an OVERSAMPLING / SPEED change dips to the original for a moment)
                buffer.setSample (ch, start + i, played * (1.0f - bypass) + original * bypass);
            }
        }

        writePos = (writePos + n) & ringMask;

        if (fadingOut)
        {
            fadingOut = false;
            modeFade = 0.0f;
            holdFor = latency + 256;   // (let the new settings fill the delay line before fading back in)

            if (currentQuality != settings.quality)
            {
                for (auto& o : oversamplers)
                    o->reset();
                tape = {};
                std::fill (tapeRing.begin(), tapeRing.end(), 0.0f);
            }

            currentQuality = settings.quality;
            currentSpeed = settings.speed;
            designFilters();   // (the filters keep their state: only their settings change)
        }
    }

    // Where the tape is, sample by sample: a slow drift (wow) and a fast
    // shimmer (flutter), as a time offset in samples. Worked out every 16
    // samples and joined with straight lines (the wobbles are slow).
    void workOutFlutter (int n, const Shape& shape) noexcept
    {
        constexpr int step = 16;
        const double speedHz[] = { 0.55, 0.83, 7.3, 11.1 };
        const double tp = juce::MathConstants<double>::twoPi;
        auto& f = flutter;

        bool any = false;
        for (int i = 0; i < n; ++i)
            any = any || wowNow[(size_t) i] > 0.0f;

        if (! any)
        {
            std::fill (offsetNow.begin(), offsetNow.begin() + n, 0.0f);
            f.lastOffset = 0.0f;
            return;
        }

        for (int i0 = 0; i0 < n; i0 += step)
        {
            const int count = juce::jmin (step, n - i0);

            for (int k = 0; k < 4; ++k)
            {
                f.phase[(size_t) k] += speedHz[k] * count / sampleRate;
                if (f.phase[(size_t) k] >= 1.0) f.phase[(size_t) k] -= 1.0;
            }

            for (int j = 0; j < count; ++j)   // a slow random wander
            {
                f.wander1 += driftCoefficient * ((random.nextFloat() - 0.5f) - f.wander1);
                f.wander2 += driftCoefficient * (f.wander1 - f.wander2);
            }
            const float drift = juce::jlimit (-1.0f, 1.0f, f.wander2 * driftScale);

            const double wowTime = (std::sin (tp * f.phase[0]) / (tp * 0.55) + 0.4 * std::sin (tp * f.phase[1]) / (tp * 0.83))
                                   * (0.8 + 0.4 * drift) * shape.wowDepth;
            const double flutterTime = (std::sin (tp * f.phase[2]) / (tp * 7.3) + 0.5 * std::sin (tp * f.phase[3]) / (tp * 11.1)) * shape.flutterDepth;
            const float target = juce::jlimit ((float) (3 - flutterCentre), (float) (flutterCentre - 3),
                                               (float) (sampleRate * (wowTime + flutterTime)) * wowNow[(size_t) (i0 + count - 1)]);

            for (int j = 0; j < count; ++j)
                offsetNow[(size_t) (i0 + j)] = f.lastOffset + (target - f.lastOffset) * (float) (j + 1) / (float) count;

            f.lastOffset = target;
        }
    }

    // A point between samples (Hermite), for wow & flutter
    float readBetween (int ch, float position) noexcept
    {
        const float whole = std::floor (position);
        const float t = position - whole;
        const int p = (int) whole;
        const float y0 = ring (flutterRing, ch, p - 1), y1 = ring (flutterRing, ch, p),
                    y2 = ring (flutterRing, ch, p + 1), y3 = ring (flutterRing, ch, p + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    // Compares how loud the sound is going in and coming out (above the
    // lows, so the head bump doesn't count) and trims what the saturation
    // took off. Never more than 6 dB either way; in silence it holds.
    void listen (double heardIn, double heardOut, int n) noexcept
    {
        auto& l = listener;
        const double rate = 1.0 - std::exp (-(double) n / (sampleRate * (l.quickenFor > 0 ? 0.15 : 0.8)));
        l.quickenFor = juce::jmax (0, l.quickenFor - n);

        const double in = heardIn / (double) n, out = heardOut / (double) n;
        if (in < 1.0e-8)
            return;

        l.in += rate * (in - l.in);
        l.out += rate * (out - l.out);

        const float wanted = juce::jlimit (0.5f, 2.0f, (float) std::sqrt (l.in / juce::jmax (l.out, 1.0e-20)));
        const float speed = (float) (wanted < l.trim ? rate * 2.0 : rate * 0.5);
        l.trim += speed * (wanted - l.trim);
    }

    //==========================================================================
    double sampleRate = 48000.0;
    int numChannels = 2;
    Settings settings;
    Shape activeShape = shapeFor (Speed::ips15);

    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 2> oversamplers;
    std::array<int, 2> oversamplerLatency { 0, 0 };
    int latency = 0, flutterCentre = 0;

    std::vector<float> originalRing, tapeRing, flutterRing;
    int ringMask = 0, ringStride = 0, writePos = 0;
    juce::AudioBuffer<float> work;

    juce::SmoothedValue<float> drive, wowAmount, hissAmount, bypassFade;
    std::array<float, chunkSize> driveNow {}, wowNow {}, hissNow {}, offsetNow {};

    struct Flutter
    {
        std::array<double, 4> phase { 0.0, 0.31, 0.57, 0.12 };
        float wander1 = 0.0f, wander2 = 0.0f, lastOffset = 0.0f;
    } flutter;

    struct Listener
    {
        double in = 0.0, out = 0.0;
        float trim = 1.0f;
        int quickenFor = 0;
    } listener;

    juce::Random random { 7 };
    double vuStage1 = 0.0, vuStage2 = 0.0;
    float matchGainDb = 0.0f, recordVu = -100.0f, driftCoefficient = 0.0f, driftScale = 1.0f;

    Quality currentQuality = Quality::times4;
    Speed currentSpeed = Speed::ips15;
    float modeFade = 1.0f;
    int holdFor = 0;
    bool fadingOut = false;
    static constexpr int fadeLength = 480;
};
