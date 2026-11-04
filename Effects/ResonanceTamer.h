#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <pffft.h>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

//==============================================================================
/**
    Easy Master's RESONANCE TAMER: turns down harsh, ringing, boxy or boomy
    spots in a mix only while they stick out - the way Unmask or soothe do -
    and leaves everything else alone.

    How: the sound is analysed in short, overlapping slices (FFT, 2048 points
    at 48 kHz, every 512 samples). For each slice:
      1. The spectrum is smoothed a little across frequency (SHARPNESS: about
         1/12 of an octave when sharp, 1/4 when soft), so the individual
         harmonics of a note blur into one line - a resonance is a bump in
         that line, not a single harmonic.
      2. It's compared with its own wider outline (about an octave either
         side). Where the line sticks out above the outline by more than
         SENSITIVITY allows, that spot is turned down by the excess - never
         more than AMOUNT.
      (Lesson from Easy Unbleed: "too loud" is judged against the sound
      around it right now, never against a single loudest moment.)
      3. The cuts move with SPEED - fast to catch a note that rings, slower
         to let go - and are smoothed across frequency, so it never warbles.

    It always works in MID and SIDE inside. STEREO: one set of cuts from both
    together (the MID settings), the same on left and right - the stereo
    picture can't move. MID + SIDE: the middle and the sides each have their
    own settings and their own cuts. LISTEN plays only what's being taken
    out. FROM / TO limit the range (soft edges). Delay: one FFT (2048
    samples at 48 kHz, ~43 ms) - your DAW lines it up.
*/
class ResonanceTamer
{
public:
    struct Part
    {
        float amountDb    = 6.0f;    // 0 .. 12: the most any spot is turned down
        float sensitivity = 0.5f;    // 0 .. 1: how little sticking out counts (6 dB .. 1.5 dB)
        float sharpness   = 0.5f;    // 0 .. 1: soft, broad spots .. sharp, narrow ones
    };

    struct Settings
    {
        Part mid, side;
        bool  midSide  = false;      // false: STEREO (the MID settings for both)
        float speed    = 0.5f;       // 0 .. 1: fast .. slow
        float fromHz   = 200.0f;   // (the bass is left alone by default: low notes aren't resonances)
        float toHz     = 16000.0f;
        bool  listen   = false;
        bool  bypassed = false;
    };

    static constexpr int numBands = 48;   // the display: sixths of an octave from 50 Hz

    //==========================================================================
    void prepare (double newSampleRate, const Settings& initial)
    {
        sampleRate = newSampleRate;
        fftSize = 2048;
        while (fftSize < 2048.0 * sampleRate / 48000.0 * 0.75) fftSize *= 2;
        while (fftSize > 512 && fftSize > 2048.0 * sampleRate / 48000.0 * 1.5) fftSize /= 2;
        hop = fftSize / 4;
        bins = fftSize / 2 + 1;

        setup.reset (pffft_new_setup (fftSize, PFFFT_REAL));
        for (auto* b : { &frame, &spectrum, &work })
            b->reset (alloc (fftSize));
        window.assign ((size_t) fftSize, 0.0f);
        for (int i = 0; i < fftSize; ++i)
            window[(size_t) i] = (float) std::sqrt (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / fftSize));

        for (auto& p : parts)
        {
            p.in.assign ((size_t) fftSize, 0.0f);
            p.out.assign ((size_t) fftSize * 2, 0.0f);
            p.dry.assign ((size_t) fftSize * 2, 0.0f);
            p.kept.assign ((size_t) fftSize, 0.0f);
            p.power.assign ((size_t) bins, 0.0f);
            p.cut.assign ((size_t) bins, 0.0f);
            p.shownLevel.fill (-120.0f);
            p.shownCut.fill (0.0f);
        }
        for (auto* v : { &lineDb, &outlineDb, &targetDb, &sums, &smoothCut })
            v->assign ((size_t) bins + 1, 0.0f);
        inPos = outRead = sinceFrame = 0;

        for (int b = 0; b < numBands; ++b)
        {
            const double centre = 50.0 * std::pow (2.0, b / 6.0);
            bandCentre[(size_t) b] = (float) centre;
            bandFirst[(size_t) b] = juce::jlimit (1, bins - 1, (int) std::floor (centre * std::pow (2.0, -1.0 / 12.0) * fftSize / sampleRate));
            bandLast[(size_t) b]  = juce::jlimit (bandFirst[(size_t) b], bins - 1, (int) std::floor (centre * std::pow (2.0, 1.0 / 12.0) * fftSize / sampleRate));
        }

        bypassFade.reset (sampleRate, 0.02);
        listenFade.reset (sampleRate, 0.02);
        setSettings (initial, true);
    }

    void setSettings (const Settings& s, bool jump = false)
    {
        settings = s;
        const auto set = [jump] (juce::SmoothedValue<float>& v, float t) { if (jump) v.setCurrentAndTargetValue (t); else v.setTargetValue (t); };
        set (bypassFade, s.bypassed ? 1.0f : 0.0f);
        set (listenFade, s.listen ? 1.0f : 0.0f);
    }

    int getLatencySamples() const noexcept   { return fftSize - 1; }

    /** Works on left and right in place (mono: just the one channel, as "mid"). */
    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int n = buffer.getNumSamples();
        const bool stereo = buffer.getNumChannels() >= 2;
        const int size = fftSize, outSize = fftSize * 2;
        float* left = buffer.getWritePointer (0);
        float* right = stereo ? buffer.getWritePointer (1) : nullptr;

        for (int i = 0; i < n; ++i)
        {
            const float l = left[i], r = stereo ? right[i] : left[i];
            const float m = stereo ? 0.5f * (l + r) : l, s = stereo ? 0.5f * (l - r) : 0.0f;
            parts[0].in[(size_t) inPos] = m;
            parts[1].in[(size_t) inPos] = s;
            const int dryAt = (outRead + size - 1) % outSize;
            parts[0].dry[(size_t) dryAt] = m;
            parts[1].dry[(size_t) dryAt] = s;
            inPos = (inPos + 1) % size;

            if (++sinceFrame == hop)
            {
                sinceFrame = 0;
                analyseAndProcess (stereo);
            }

            const float bypass = bypassFade.getNextValue(), listen = listenFade.getNextValue();
            float heard[2], dry[2];
            for (int p = 0; p < 2; ++p)
            {
                auto& part = parts[(size_t) p];
                const float wet = part.out[(size_t) outRead];
                dry[p] = part.dry[(size_t) outRead];
                part.out[(size_t) outRead] = 0.0f;
                const float h = wet * (1.0f - listen) + (dry[p] - wet) * listen;
                heard[p] = bypass >= 1.0f ? dry[p] : h * (1.0f - bypass) + dry[p] * bypass;
            }
            outRead = (outRead + 1) % outSize;

            if (stereo)
            {
                left[i] = heard[0] + heard[1];
                right[i] = heard[0] - heard[1];
            }
            else
                left[i] = heard[0];
        }
    }

    //==========================================================================
    /** For the window: part 0 = MID (or both, in STEREO), 1 = SIDE. Level in dBFS, cut in dB (positive). */
    float getBandCentreHz (int b) const noexcept             { return bandCentre[(size_t) b]; }
    float getBandLevelDb (int part, int b) const noexcept    { return parts[(size_t) part].shownLevel[(size_t) b]; }
    float getBandCutDb (int part, int b) const noexcept      { return parts[(size_t) part].shownCut[(size_t) b]; }
    float getMostCutDb (int part) const noexcept             { return parts[(size_t) part].mostCut; }

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

    struct PartState
    {
        std::vector<float> in, out, dry, kept, power, cut;
        std::array<float, numBands> shownLevel {}, shownCut {};
        float mostCut = 0.0f;
    };

    float binPower (const float* spec, int k) const noexcept
    {
        if (k == 0)        return spec[0] * spec[0];
        if (k == bins - 1) return spec[1] * spec[1];
        return spec[2 * k] * spec[2 * k] + spec[2 * k + 1] * spec[2 * k + 1];
    }

    /** dB values averaged over a window that's a fixed fraction of an octave wide (from running sums). */
    void smoothAcross (const std::vector<float>& source, std::vector<float>& destination, double octaves, int fewestBins = 1) noexcept
    {
        sums[0] = 0.0f;
        for (int k = 0; k < bins; ++k)
            sums[(size_t) k + 1] = sums[(size_t) k] + source[(size_t) k];
        const double spread = std::pow (2.0, octaves * 0.5) - 1.0;
        for (int k = 0; k < bins; ++k)
        {
            const int half = juce::jmax (fewestBins, (int) std::lround (k * spread));
            const int lo = juce::jmax (0, k - half), hi = juce::jmin (bins - 1, k + half);
            destination[(size_t) k] = (sums[(size_t) hi + 1] - sums[(size_t) lo]) / (float) (hi - lo + 1);
        }
    }

    void analyseAndProcess (bool stereo) noexcept
    {
        const int size = fftSize, outSize = fftSize * 2;
        const float norm = 1.0f / (float) size;
        const float frameSeconds = (float) hop / (float) sampleRate;
        const float speed = juce::jlimit (0.0f, 1.0f, settings.speed);
        const float attack = 1.0f - std::exp (-frameSeconds / (0.004f + 0.046f * speed));    // 4 .. 50 ms
        const float release = 1.0f - std::exp (-frameSeconds / (0.04f + 0.36f * speed));    // 40 .. 400 ms
        const float powerFollow = 1.0f - std::exp (-frameSeconds / 0.02f);
        const float calibrationDb = 46.8f + 20.0f * std::log10 ((float) fftSize / 2048.0f);

        // Both spectra (kept for resynthesis), and their powers
        for (int p = 0; p < 2; ++p)
        {
            auto& part = parts[(size_t) p];
            float* f = frame.get();
            for (int k = 0; k < size; ++k)
                f[k] = part.in[(size_t) ((inPos + k) % size)] * window[(size_t) k];
            pffft_transform_ordered (setup.get(), f, spectrum.get(), work.get(), PFFFT_FORWARD);
            std::copy (spectrum.get(), spectrum.get() + size, part.kept.begin());
            for (int k = 0; k < bins; ++k)
                part.power[(size_t) k] += powerFollow * (binPower (spectrum.get(), k) - part.power[(size_t) k]);
        }

        // The cuts: one set from both together (STEREO), or each part its own (MID + SIDE)
        const int analyses = (settings.midSide && stereo) ? 2 : 1;
        for (int a = 0; a < analyses; ++a)
        {
            const auto& set = a == 0 ? settings.mid : settings.side;
            auto& cut = parts[(size_t) a].cut;
            for (int k = 0; k < bins; ++k)
            {
                const float pw = analyses == 2 ? parts[(size_t) a].power[(size_t) k] : parts[0].power[(size_t) k] + parts[1].power[(size_t) k];
                targetDb[(size_t) k] = 10.0f * std::log10 (pw + 1.0e-12f);
            }
            const float sharp = juce::jlimit (0.0f, 1.0f, set.sharpness);
            // the line: harmonics blurred together (1/4 .. 1/12 octave - and never narrower than ~100 Hz, so the low notes'
            // own fundamentals and harmonics aren't mistaken for resonances)
            const int fewest = juce::jmax (1, (int) std::lround (50.0 * size / sampleRate));
            smoothAcross (targetDb, lineDb, 0.25 - 0.1667 * sharp, fewest);
            // its outline: an octave either side - worked out twice, the second time with the bumps clipped off, so a big
            // resonance doesn't lift its own outline and hide itself
            smoothAcross (lineDb, outlineDb, 2.0);
            for (int k = 0; k < bins; ++k)
                smoothCut[(size_t) k] = juce::jmin (lineDb[(size_t) k], outlineDb[(size_t) k] + 3.0f);
            smoothAcross (smoothCut, outlineDb, 2.0);
            const float allowed = 6.0f - 4.5f * juce::jlimit (0.0f, 1.0f, set.sensitivity);   // (6 .. 1.5 dB above its surroundings)
            const float most = juce::jlimit (0.0f, 12.0f, set.amountDb);
            const float quietest = -90.0f + calibrationDb;                 // (nothing to tame in near-silence)
            for (int k = 0; k < bins; ++k)
            {
                const float hz = (float) k * (float) sampleRate / (float) size;
                // soft edges at FROM and TO (a third of an octave)
                const float inFrom = juce::jlimit (0.0f, 1.0f, 3.0f * std::log2 (juce::jmax (1.0f, hz) / settings.fromHz) + 1.0f);
                const float inTo = juce::jlimit (0.0f, 1.0f, 3.0f * std::log2 (settings.toHz / juce::jmax (1.0f, hz)) + 1.0f);
                const float excess = lineDb[(size_t) k] - outlineDb[(size_t) k] - allowed;
                float want = juce::jlimit (0.0f, most, excess * 0.8f) * inFrom * inTo;
                if (lineDb[(size_t) k] < quietest) want = 0.0f;
                auto& c = cut[(size_t) k];
                c += (want > c ? attack : release) * (want - c);
            }
            // (smoothed across frequency, so the cuts are gentle shapes, never single bins)
            smoothAcross (cut, smoothCut, 1.0 / 12.0);
            std::copy (smoothCut.begin(), smoothCut.begin() + bins, cut.begin());
        }
        if (analyses == 1)
            std::copy (parts[0].cut.begin(), parts[0].cut.end(), parts[1].cut.begin());

        // The display: level and cut, a sixth of an octave at a time
        for (int p = 0; p < 2; ++p)
        {
            auto& part = parts[(size_t) p];
            float most = 0.0f;
            for (int b = 0; b < numBands; ++b)
            {
                double pw = 0.0; float c = 0.0f;
                for (int k = bandFirst[(size_t) b]; k <= bandLast[(size_t) b]; ++k)
                {
                    pw += part.power[(size_t) k];
                    c = juce::jmax (c, part.cut[(size_t) k]);
                }
                pw /= (bandLast[(size_t) b] - bandFirst[(size_t) b] + 1);
                part.shownLevel[(size_t) b] = 10.0f * std::log10 ((float) pw + 1.0e-20f) - calibrationDb;
                part.shownCut[(size_t) b] = c;
                most = juce::jmax (most, c);
            }
            part.mostCut = most;
        }

        // Apply, back to sound, overlap-add
        for (int p = 0; p < 2; ++p)
        {
            auto& part = parts[(size_t) p];
            float* s = spectrum.get();
            std::copy (part.kept.begin(), part.kept.end(), s);
            const auto gainAt = [&] (int k) { return juce::Decibels::decibelsToGain (-part.cut[(size_t) k]); };
            s[0] *= gainAt (0);
            s[1] *= gainAt (bins - 1);
            for (int k = 1; k < bins - 1; ++k)
            {
                const float g = gainAt (k);
                s[2 * k] *= g;
                s[2 * k + 1] *= g;
            }
            pffft_transform_ordered (setup.get(), s, frame.get(), work.get(), PFFFT_BACKWARD);
            for (int k = 0; k < size; ++k)
                part.out[(size_t) ((outRead + k) % outSize)] += frame.get()[k] * window[(size_t) k] * norm * 0.5f;
        }
    }

    Settings settings;
    double sampleRate = 48000.0;
    int fftSize = 2048, hop = 512, bins = 1025;
    std::unique_ptr<PFFFT_Setup, SetupFree> setup;
    Aligned frame, spectrum, work;
    std::vector<float> window, lineDb, outlineDb, targetDb, sums, smoothCut;
    std::array<PartState, 2> parts;
    int inPos = 0, outRead = 0, sinceFrame = 0;
    std::array<float, numBands> bandCentre {};
    std::array<int, numBands> bandFirst {}, bandLast {};
    juce::SmoothedValue<float> bypassFade, listenFade;
};
