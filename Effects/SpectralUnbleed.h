#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <pffft.h>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

//==============================================================================
/**
    Easy Unbleed's sound: cleans the ghost of other instruments out of a
    separated stem (from UVR5 or any separator), using the other stems -
    sent to the sidechain - as its guide.

    What it can and can't do: every stem a separator makes comes from the
    same mix, so the other stems can't reveal anything the separator didn't
    already know. What they CAN do is show when the stem has stopped - a
    vocal pause, a long instrumental - and what's left in it then is ghost.
    That is where the ghost is heard, and where Unbleed turns down. While
    the stem plays, it's left alone. (Tested on real songs: a band's
    multitracks and a Mureka song's own stems, each split with UVR5 - Kim
    Vocal 2 and htdemucs - and compared. A band-by-band rule that also cut
    "quiet" bands during the singing was tried and dropped: it took some
    of the real voice for almost no gain.)

      AMOUNT       how far it turns a ghost down (0 - 40 dB)
      SENSITIVITY  how readily it decides the stem has stopped, and how
                   much louder the others must be in a band to count it
                   as ghost
      SMOOTHNESS   how gently a ghost fades out (short = tight, long =
                   smooth)
      FROM / TO    the range it works in (e.g. only above 3 kHz for
                   hi-hat ghosts in a vocal)
      LISTEN       hear only what it's taking out

    How: the sound is analysed in short, overlapping slices (FFT, 2048
    points at 48 kHz, every 512 samples), grouped into third-octave bands.
    For each band it follows how loud the stem usually is (over ~20 s).
    Across the stem's own range (the bands it's usually within 20 dB of its
    loudest in) it judges whether the stem is playing at all: when it's far
    below its usual level there and the other stems are far louder, it has
    stopped (eased in over 6 dB and ~150 ms, so a breath between words
    doesn't count). Then every band the others are about as loud in or
    louder is turned down by AMOUNT - even a ghost as loud as the real part
    in a few bands (real UVR5 vocal stems carry exactly that: guitar low
    mids through a long instrumental). The gains fade down at SMOOTHNESS,
    open at once when the stem returns, are smoothed across frequency, and
    the slices put back together. Left and right get the same gains (the
    stereo image stays).

    A sidechain that is the same sound as the stem itself (a copy sent by
    mistake) is noticed and ignored, so it can never cancel the stem.
    Delay: one FFT (2048 samples at 48 kHz, ~43 ms) - your DAW lines it up.
*/
class SpectralUnbleed
{
public:
    struct Settings
    {
        float amountDb    = 20.0f;   // 0 .. 40
        float sensitivity = 0.5f;    // 0 .. 1
        float smoothness  = 0.4f;    // 0 .. 1 (how gently it turns down: 5 .. 120 ms)
        float fromHz      = 20.0f;
        float toHz        = 20000.0f;
        bool  listen      = false;   // hear only what's removed
        bool  bypassed    = false;
    };

    static constexpr int numBandsMax = 40;

    //==========================================================================
    void prepare (double newSampleRate, const Settings& initial)
    {
        sampleRate = newSampleRate;
        fftSize = 2048;
        while (fftSize < 2048.0 * sampleRate / 48000.0 * 0.75) fftSize *= 2;   // (about 43 ms at any rate)
        while (fftSize > 512 && fftSize > 2048.0 * sampleRate / 48000.0 * 1.5) fftSize /= 2;
        hop = fftSize / 4;
        bins = fftSize / 2 + 1;

        setup.reset (pffft_new_setup (fftSize, PFFFT_REAL));
        for (auto* b : { &frame, &spectrum, &work, &sideSpectrum })
            b->reset (alloc (fftSize));
        window.assign ((size_t) fftSize, 0.0f);
        for (int i = 0; i < fftSize; ++i)
            window[(size_t) i] = (float) std::sqrt (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / fftSize));

        for (auto& c : chans)
        {
            c.in.assign ((size_t) fftSize, 0.0f);
            c.out.assign ((size_t) fftSize * 2, 0.0f);
            c.dry.assign ((size_t) fftSize * 2, 0.0f);
            c.side.assign ((size_t) fftSize, 0.0f);
        }
        for (auto& k : kept)
            k.assign ((size_t) fftSize, 0.0f);
        inPos = 0;
        outRead = 0;
        sinceFrame = 0;

        makeBands();
        binGain.assign ((size_t) bins, 1.0f);
        bandPowerT.fill (0.0f); bandPowerS.fill (0.0f);
        usualLong.fill (0.0f); stemAbsent = 0.0f; bandGain.fill (1.0f); shownReduction.fill (0.0f); previousPower.fill (0.0f);
        copyEvidence = 0.0f;
        sidechainIsCopy = false;

        bypassFade.reset (sampleRate, 0.02);
        listenFade.reset (sampleRate, 0.02);
        settings = initial;
        setSettings (initial, true);
    }

    void setSettings (const Settings& s, bool jump = false)
    {
        settings = s;
        if (jump)
        {
            bypassFade.setCurrentAndTargetValue (s.bypassed ? 1.0f : 0.0f);
            listenFade.setCurrentAndTargetValue (s.listen ? 1.0f : 0.0f);
        }
        else
        {
            bypassFade.setTargetValue (s.bypassed ? 1.0f : 0.0f);
            listenFade.setTargetValue (s.listen ? 1.0f : 0.0f);
        }
    }

    int getLatencySamples() const noexcept   { return fftSize - 1; }

    /** track: the stem (in place). side: the other stems (any channels), or nullptr / no channels for none. */
    void process (juce::AudioBuffer<float>& track, const juce::AudioBuffer<float>* side) noexcept
    {
        const int n = track.getNumSamples();
        // (up to 4 channels: the first two are the stem and decide the gains; any more just get the same gains - for the tests)
        const int channels = juce::jmin (4, track.getNumChannels());
        const int sideChannels = side != nullptr ? juce::jmin (2, side->getNumChannels()) : 0;
        const int size = fftSize, outSize = fftSize * 2;

        for (int i = 0; i < n; ++i)
        {
            // In: the stem and the sidechain into the analysis buffers
            float sideSample = 0.0f;
            for (int c = 0; c < sideChannels; ++c)
                sideSample += side->getSample (c, i);
            if (sideChannels > 1) sideSample *= 0.5f;

            for (int c = 0; c < channels; ++c)
            {
                auto& ch = chans[(size_t) c];
                ch.in[(size_t) inPos] = track.getSample (c, i);
                ch.side[(size_t) inPos] = sideSample;
                ch.dry[(size_t) ((outRead + size - 1) % outSize)] = track.getSample (c, i);   // (delayed exactly as the processed sound)
            }
            inPos = (inPos + 1) % size;

            if (++sinceFrame == hop)
            {
                sinceFrame = 0;
                analyseAndProcess (channels);
            }

            // Out: the processed sound, one FFT later
            const float bypass = bypassFade.getNextValue(), listen = listenFade.getNextValue();
            for (int c = 0; c < channels; ++c)
            {
                auto& ch = chans[(size_t) c];
                const float wet = ch.out[(size_t) outRead];
                const float dry = ch.dry[(size_t) outRead];
                ch.out[(size_t) outRead] = 0.0f;
                const float heard = wet * (1.0f - listen) + (dry - wet) * listen;   // (LISTEN: only what's removed)
                track.setSample (c, i, bypass >= 1.0f ? dry : heard * (1.0f - bypass) + dry * bypass);
            }
            outRead = (outRead + 1) % outSize;
        }
    }

    //==========================================================================
    /** For the window: the bands' centre frequencies and how much each is being turned down right now (dB). */
    int getNumBands() const noexcept                       { return numBands; }
    float getBandCentreHz (int b) const noexcept           { return bandCentre[(size_t) b]; }
    float getBandReductionDb (int b) const noexcept        { return shownReduction[(size_t) b]; }
    float getBandLevelDb (int b, bool sidechain) const noexcept
    {
        // (in dBFS: a tone peaking at 0 dB reads 0 dB, whatever the slice size)
        const float calibrationDb = 46.8f + 20.0f * std::log10 ((float) fftSize / 2048.0f);
        return 10.0f * std::log10 ((sidechain ? bandPowerS : bandPowerT)[(size_t) b] + 1.0e-20f) - calibrationDb;
    }
    bool isSidechainACopy() const noexcept                 { return sidechainIsCopy; }

private:
    struct AlignedFree { void operator() (float* p) const noexcept { pffft_aligned_free (p); } };
    struct SetupFree   { void operator() (PFFFT_Setup* s) const noexcept { if (s != nullptr) pffft_destroy_setup (s); } };
    using Aligned = std::unique_ptr<float, AlignedFree>;

    static float* alloc (int count)
    {
        auto* p = static_cast<float*> (pffft_aligned_malloc ((size_t) count * sizeof (float)));
        std::fill (p, p + count, 0.0f);
        return p;
    }

    struct Channel
    {
        std::vector<float> in, out, dry, side;
    };

    void makeBands()
    {
        // Third-octave bands from 40 Hz; everything below joins the first
        numBands = 0;
        double low = 0.0, centre = 40.0;
        while (numBands < numBandsMax && low < sampleRate * 0.5)
        {
            const double high = numBands == 0 ? centre * std::pow (2.0, 1.0 / 6.0) : centre * std::pow (2.0, 1.0 / 6.0);
            bandFirst[(size_t) numBands] = juce::jlimit (0, bins - 1, (int) std::floor (low * fftSize / sampleRate));
            bandLast[(size_t) numBands] = juce::jlimit (bandFirst[(size_t) numBands], bins - 1, (int) std::floor (high * fftSize / sampleRate));
            bandCentre[(size_t) numBands] = (float) centre;
            low = high;
            centre *= std::pow (2.0, 1.0 / 3.0);
            ++numBands;
        }
        bandLast[(size_t) (numBands - 1)] = bins - 1;
    }

    /** Power in each band of a spectrum (pffft's ordered layout: re/im pairs, with DC and Nyquist packed first). */
    void bandPowers (const float* spec, std::array<float, numBandsMax>& out) const noexcept
    {
        for (int b = 0; b < numBands; ++b)
        {
            double sum = 0.0;
            for (int k = bandFirst[(size_t) b]; k <= bandLast[(size_t) b]; ++k)
                sum += binPower (spec, k);
            out[(size_t) b] += (float) (sum / (bandLast[(size_t) b] - bandFirst[(size_t) b] + 1));
        }
    }

    float binPower (const float* spec, int k) const noexcept
    {
        if (k == 0)            return spec[0] * spec[0];
        if (k == bins - 1)     return spec[1] * spec[1];
        return spec[2 * k] * spec[2 * k] + spec[2 * k + 1] * spec[2 * k + 1];
    }

    void analyseAndProcess (int channels) noexcept
    {
        const int size = fftSize, outSize = fftSize * 2;
        const float norm = 1.0f / (float) size;
        const int analysed = juce::jmin (2, channels);
        std::array<float, numBandsMax> powerT {}, powerS {};

        // Each channel's spectrum (kept for resynthesis), and the bands' powers
        for (int c = 0; c < channels; ++c)
        {
            auto& ch = chans[(size_t) c];
            float* f = frame.get();
            for (int k = 0; k < size; ++k)
                f[k] = ch.in[(size_t) ((inPos + k) % size)] * window[(size_t) k];
            pffft_transform_ordered (setup.get(), f, spectrum.get(), work.get(), PFFFT_FORWARD);
            std::copy (spectrum.get(), spectrum.get() + size, kept[(size_t) c].begin());
            if (c < analysed)
                bandPowers (spectrum.get(), powerT);
        }
        {
            float* f = frame.get();
            for (int k = 0; k < size; ++k)
                f[k] = chans[0].side[(size_t) ((inPos + k) % size)] * window[(size_t) k];
            pffft_transform_ordered (setup.get(), f, sideSpectrum.get(), work.get(), PFFFT_FORWARD);
            bandPowers (sideSpectrum.get(), powerS);
        }

        // Is the sidechain a copy of this track? (the same shape, the same level, frame after frame)
        {
            double dot = 0.0, a2 = 0.0, b2 = 0.0, levelT = 0.0, levelS = 0.0;
            for (int b = 0; b < numBands; ++b)
            {
                const double t = std::sqrt (powerT[(size_t) b] / (float) analysed), s = std::sqrt (powerS[(size_t) b]);
                dot += t * s; a2 += t * t; b2 += s * s; levelT += t * t; levelS += s * s;
            }
            const bool sound = levelT > 1.0e-10 && levelS > 1.0e-10;
            const double shape = sound ? dot / std::sqrt (a2 * b2) : 0.0;
            const double levelDiff = sound ? std::abs (10.0 * std::log10 (levelS / levelT)) : 99.0;
            const float looksLikeCopy = (shape > 0.995 && levelDiff < 1.5) ? 1.0f : 0.0f;
            if (sound)
                copyEvidence += 0.05f * (looksLikeCopy - copyEvidence);
            sidechainIsCopy = copyEvidence > 0.6f;
        }

        // The gains, band by band
        const float sens = juce::jlimit (0.0f, 1.0f, settings.sensitivity);
        const float frameSeconds = (float) hop / (float) sampleRate;
        const float amount = juce::jlimit (0.0f, 40.0f, settings.amountDb);
        // (turning down eases in - SMOOTHNESS - so a ghost fades out rather than being chopped; when the stem comes
        //  back it opens at once, so the start of every phrase or hit is untouched)
        const float easeDown = 1.0f - std::exp (-frameSeconds / (0.005f + 0.115f * juce::jlimit (0.0f, 1.0f, settings.smoothness)));
        const float openFast = 1.0f - std::exp (-frameSeconds / 0.003f);   // (the stem comes in: a note, a hit)
        const float openSlow = 1.0f - std::exp (-frameSeconds / 0.05f);    // (just a flicker in the gap: don't flutter)

        // Is the stem playing AT ALL right now? (Judged over its whole range, so a ghost that's as loud as the
        // real part in a few bands is still caught when the stem is plainly silent: across its range it has
        // dropped far below its usual level, and the other stems are far louder.)
        {
            const float longFall = std::exp (-frameSeconds / 20.0f);
            double sumBelow = 0.0, sumLouder = 0.0;
            int counted = 0;
            float loudestUsual = 0.0f;
            for (int b = 0; b < numBands; ++b)
            {
                auto& ul = usualLong[(size_t) b];
                const float pt = powerT[(size_t) b] / (float) analysed + 1.0e-20f;
                ul = juce::jmax (pt, longFall * ul + (1.0f - longFall) * pt);
                loudestUsual = juce::jmax (loudestUsual, ul);
            }
            // (the stem's own range: the bands it's usually within 20 dB of its loudest in, above the deep bass
            //  for a vocal, the low end for a bass)
            const float ownFloor = loudestUsual * std::pow (10.0f, -20.0f / 10.0f);
            for (int b = 0; b < numBands; ++b)
            {
                const float pt = powerT[(size_t) b] / (float) analysed + 1.0e-20f, ps = powerS[(size_t) b] + 1.0e-20f;
                const float ul = usualLong[(size_t) b];
                if (ul < ownFloor) continue;
                sumBelow  += juce::jlimit (0.0f, 40.0f, 10.0f * std::log10 ((ul + 1.0e-20f) / pt));
                sumLouder += juce::jlimit (-30.0f, 40.0f, 10.0f * std::log10 (ps / pt));
                ++counted;
            }
            float absent = 0.0f;
            if (counted > 0 && settings.sensitivity > 0.0f)
            {
                const float below = (float) (sumBelow / counted), louder = (float) (sumLouder / counted);
                const float needBelow  = 16.0f - 8.0f * sens;      // (the stem's top end this far under its usual level ...)
                const float needLouder = 14.0f - 8.0f * sens;      // (... and the others this much louder there)
                absent = juce::jlimit (0.0f, 1.0f, (below - needBelow) / 6.0f) * juce::jlimit (0.0f, 1.0f, (louder - needLouder) / 6.0f);
            }
            // (settles in over ~150 ms so a breath between words isn't taken for silence; drops at once when the stem returns)
            const float settle = 1.0f - std::exp (-frameSeconds / 0.15f);
            stemReturning = absent < stemAbsent - 0.2f;
            stemAbsent = absent < stemAbsent ? absent : stemAbsent + settle * (absent - stemAbsent);
        }

        for (int b = 0; b < numBands; ++b)
        {
            const float pt = powerT[(size_t) b] / (float) analysed + 1.0e-20f;
            const float ps = powerS[(size_t) b] + 1.0e-20f;
            bandPowerT[(size_t) b] = pt;
            bandPowerS[(size_t) b] = ps;


            const float othersLouder = 10.0f * std::log10 (ps / pt);
            const bool inRange = bandCentre[(size_t) b] >= settings.fromHz * 0.89f && bandCentre[(size_t) b] <= settings.toHz * 1.12f;
            // (while the stem is silent, a band is ghost if the others are about as loud there or louder - how much
            //  louder they must be follows SENSITIVITY)
            const float notOwn = juce::jlimit (0.0f, 1.0f, (othersLouder + 12.0f * sens - 6.0f) / 6.0f);
            const float ghost = stemAbsent * notOwn;
            const float reduction = (sidechainIsCopy || ! inRange) ? 0.0f : amount * ghost;
            const float target = juce::Decibels::decibelsToGain (-reduction);

            auto& g = bandGain[(size_t) b];
            const bool comingIn = pt > previousPower[(size_t) b] * 3.0f;    // (up more than ~5 dB since the last slice)
            previousPower[(size_t) b] = pt;
            g += (target < g ? easeDown : ((comingIn || stemReturning) ? openFast : openSlow)) * (target - g);
            shownReduction[(size_t) b] = -juce::Decibels::gainToDecibels (g, -60.0f);
        }

        // Band gains spread smoothly over the bins (between band centres, on a log-frequency scale)
        for (int k = 0; k < bins; ++k)
        {
            const float hz = juce::jmax (1.0f, (float) k * (float) sampleRate / (float) size);
            int b = 0;
            while (b < numBands - 1 && bandCentre[(size_t) (b + 1)] < hz) ++b;
            float g;
            if (hz <= bandCentre[0])                         g = bandGain[0];
            else if (b >= numBands - 1)                      g = bandGain[(size_t) (numBands - 1)];
            else
            {
                const float t = std::log (hz / bandCentre[(size_t) b]) / std::log (bandCentre[(size_t) (b + 1)] / bandCentre[(size_t) b]);
                const float da = juce::Decibels::gainToDecibels (bandGain[(size_t) b], -80.0f);
                const float db = juce::Decibels::gainToDecibels (bandGain[(size_t) (b + 1)], -80.0f);
                g = juce::Decibels::decibelsToGain (da + (db - da) * t, -80.0f);
            }
            binGain[(size_t) k] = g;
        }

        // Apply, back to sound, overlap-add
        for (int c = 0; c < channels; ++c)
        {
            float* s = spectrum.get();
            std::copy (kept[(size_t) c].begin(), kept[(size_t) c].end(), s);
            s[0] *= binGain[0];
            s[1] *= binGain[(size_t) (bins - 1)];
            for (int k = 1; k < bins - 1; ++k)
            {
                s[2 * k] *= binGain[(size_t) k];
                s[2 * k + 1] *= binGain[(size_t) k];
            }
            pffft_transform_ordered (setup.get(), s, frame.get(), work.get(), PFFFT_BACKWARD);
            auto& ch = chans[(size_t) c];
            // (sqrt-Hann in and out at 75% overlap adds up to 2: hence the 0.5)
            for (int k = 0; k < size; ++k)
                ch.out[(size_t) ((outRead + k) % outSize)] += frame.get()[k] * window[(size_t) k] * norm * 0.5f;
        }
    }

    double sampleRate = 48000.0;
    int fftSize = 2048, hop = 512, bins = 1025;
    std::unique_ptr<PFFFT_Setup, SetupFree> setup;
    Aligned frame, spectrum, work, sideSpectrum;
    std::vector<float> window, binGain;
    std::array<Channel, 4> chans;
    std::array<std::vector<float>, 4> kept;   // (each channel's spectrum, between analysis and resynthesis)
    int inPos = 0, outRead = 0, sinceFrame = 0;

    int numBands = 0;
    std::array<int, numBandsMax> bandFirst {}, bandLast {};
    float stemAbsent = 0.0f;
    bool stemReturning = false;
    std::array<float, numBandsMax> bandCentre {}, bandPowerT {}, bandPowerS {}, usualLong {}, bandGain {}, shownReduction {}, previousPower {};
    float copyEvidence = 0.0f;
    bool sidechainIsCopy = false;

    Settings settings;
    juce::SmoothedValue<float> bypassFade, listenFade;
};
