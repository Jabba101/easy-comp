#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include "pffft.h"

// (Exact comparisons are deliberate here: digital silence is exactly zero, and
// a MIX level that has finished gliding is exactly at its target.)
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal")

//==============================================================================
/**
    Bitch-Slap's sound: the 80s digital gated reverb. Every sound you play -
    each hit, ghost note and bit of bleed - gets its own burst of dense
    reverb, which stops dead LENGTH after the hit.

    HOW IT WORKS
    The burst is a stretch of noise with a shape: it opens over 2 ms, sags
    4 dB across its length, and slams shut over the last 12 ms. Running your
    sound (left and right added together) through that noise - a
    convolution - gives every sound its own burst. Different noise on the
    left and the right makes it wide. Two filters on the way out set its
    tone (180 Hz to 9 kHz).

    Done sample by sample, a one-second burst would be 48,000
    multiplications per sample on each side - far too slow. So it's done the
    fast way, with FFTs (PFFFT, in ThirdParty/pffft), in pieces: the start
    of the burst in small pieces, so it starts on time, the rest in bigger
    ones, which cost less per sample. Even the small pieces need a short
    wait (64 samples at 44.1 or 48 kHz) - hidden in the 2 ms gap before
    the burst begins, so the plugin adds no delay to your track.

    CHANGING LENGTH
    Each sound keeps the burst it started with; a new LENGTH applies to the
    sounds that come after the change. So moving the knob never clicks or
    chops a burst off. For that to hold exactly, a change waits for the next
    boundary of the biggest pieces (at most 85 ms) - the one moment when
    every size of piece starts afresh - and each stretch of input is
    worked on by itself ("overlap-add"), never shared with its neighbours.
    The burst's pieces are rebuilt one at a time, just as the first sound
    with the new LENGTH reaches them.

    SILENCE COSTS NOTHING
    Silent stretches skip their FFTs. Once the last burst has died away,
    the whole thing rests until sound comes in again. Bypassed, or with MIX
    at 0%, it rests straight away.

    The level is fixed: the burst plays at the same level whatever its
    LENGTH. At MIX 50% the dry sound and the reverb are both at full level,
    and the reverb comes out about as loud as a snare (it was set on the
    listening examples' snare).
*/
class GatedReverb
{
public:
    static constexpr float minLengthMs = 50.0f, maxLengthMs = 1000.0f;
    static constexpr float predelayMs = 2.0f;    // the gap between a hit and its burst
    static constexpr float attackMs   = 2.0f;    // the burst opens this quickly...
    static constexpr float sagDb      = 4.0f;    // ...sags this much across its length...
    static constexpr float slamMs     = 12.0f;   // ...and slams shut over this long
    static constexpr float lowCutHz   = 180.0f, highCutHz = 9000.0f;   // its tone

    /** The noise level (per sample, at 48 kHz) that puts the reverb "about as
        loud as the snare" at MIX 50%. From the listening examples. */
    static constexpr double noiseLevelAt48k = 0.0100;

    struct Settings
    {
        float lengthMs = 300.0f;   // from the hit until the burst is shut
        float mix = 0.35f;         // 0 = dry ... 0.5 = dry and reverb both full ... 1 = reverb only
        bool bypassed = false;
    };

    struct BlockResult
    {
        float wetPeak = 0.0f;      // the loudest the reverb came out (after MIX)
    };

    /** How loud the dry sound and the reverb are at a MIX setting (0..1):
        MIX only adds reverb up to 50%, then turns the dry sound down. */
    static float dryGainFor (float mix) noexcept   { return juce::jlimit (0.0f, 1.0f, 2.0f * (1.0f - mix)); }
    static float wetGainFor (float mix) noexcept   { return juce::jlimit (0.0f, 1.0f, 2.0f * mix); }

    //==========================================================================
    /** Call before playing. numOutputChannels: 1 (a mono burst) or 2 (wide). */
    void prepare (double sampleRate, int numOutputChannels, const Settings& initialSettings)
    {
        rate = sampleRate;
        numWet = juce::jlimit (1, 2, numOutputChannels);

        // The smallest piece: the biggest power of two that fits in the 2 ms
        // gap before the burst (64 samples at 44.1 / 48 kHz, 128 at 96 kHz).
        predelaySamples = juce::jmax (16, (int) std::lround (predelayMs * 1e-3 * rate));
        blockSize = 16;
        while (blockSize * 2 <= predelaySamples && blockSize < 1024)
            blockSize *= 2;

        attackSamples = attackMs * 1e-3 * rate;
        slamSamples   = slamMs * 1e-3 * rate;
        noiseLevel    = noiseLevelAt48k * std::sqrt (48000.0 / rate);   // the same sound at any sample rate

        // The longest burst, counted from blockSize after the hit (the
        // engine's own wait is taken out of the 2 ms gap).
        maxImpulse = (int) std::ceil (maxLengthMs * 1e-3 * rate) + 1;
        const int longest = maxImpulse - blockSize;

        // The pieces: 7 small ones, 7 eight times bigger, then as many 64 times
        // bigger as the longest burst needs. Each size starts exactly where
        // its first output is due, so nothing waits longer than it must.
        stages.clear();
        stages.reserve (3);

        for (int size : { 1, 8, 64 })
        {
            const int pieceSize = size * blockSize;
            const int offset = stages.empty() ? 0 : stages.back().offset + stages.back().numPieces * stages.back().pieceSize;

            if (offset >= longest)
                break;

            const int nextOffset = size < 64 ? size * 8 * blockSize - blockSize : longest;   // where the next size can start
            const int numPieces = juce::jmax (1, (juce::jmin (nextOffset, longest) - offset + pieceSize - 1) / pieceSize);

            stages.emplace_back();
            auto& stage = stages.back();
            stage.pieceSize = pieceSize;
            stage.fftSize = pieceSize * 2;
            stage.offset = offset;
            stage.numPieces = numPieces;
            stage.stepsPerBlock = size;
            stage.fft.reset (pffft_new_setup (stage.fftSize, PFFFT_REAL));
            jassert (stage.fft != nullptr);
            stage.slots = allocate ((size_t) (numPieces * stage.fftSize));
            stage.slotSilent.assign ((size_t) numPieces, 1);
            stage.slotVersion.assign ((size_t) numPieces, 0);
            stage.pieceVersion.assign ((size_t) numPieces, noVersion);

            for (int c = 0; c < 2; ++c)
            {
                stage.pieces[(size_t) c] = allocate ((size_t) (numPieces * stage.fftSize));
                stage.sum[(size_t) c] = allocate ((size_t) stage.fftSize);
            }

            stage.frame = allocate ((size_t) stage.fftSize);
            stage.work  = allocate ((size_t) stage.fftSize);
        }

        const int biggest = stages.back().pieceSize;
        ringSize = 1;
        while (ringSize < 2 * biggest)   // (the biggest pieces add twice their size of output)
            ringSize *= 2;

        inputRing = allocate ((size_t) ringSize);
        for (auto& ring : outputRing)
            ring = allocate ((size_t) ringSize);

        scratch  = allocate ((size_t) (2 * biggest));
        envelope.assign ((size_t) biggest, 0.0f);
        inputBlock.assign ((size_t) blockSize, 0.0f);
        for (auto& block : wetBlock)
            block.assign ((size_t) blockSize, 0.0f);

        // The noise, left and right (always the same noise, so the burst sounds
        // the same every time you play)
        const auto& last = stages.back();
        const int noiseLength = last.offset + last.numPieces * last.pieceSize + blockSize;

        const uint64_t seeds[2] = { 0x5eed0001ull, 0x5eed0001ull + 0x9e3779b97f4a7c15ull };   // left, right

        for (size_t c = 0; c < 2; ++c)
            makeNoise (noise[c], noiseLength, seeds[c]);

        // How long a piece of input can stay in use (so a LENGTH it used isn't
        // forgotten too early), and how long after the last sound everything
        // has died away (plus 50 ms for the tone filters to fall silent).
        int longestInUse = 0;
        for (auto& stage : stages)
            longestInUse = juce::jmax (longestInUse, (stage.numPieces + 2) * stage.pieceSize);

        minSamplesBetweenVersions = juce::jmax ((int) std::ceil (0.02 * rate), longestInUse / (numVersions - 4) + 1);
        restAfterSteps = (longest + 3 * biggest + (int) std::ceil (0.05 * rate)) / blockSize + 1;

        lowCut.design (lowCutHz, rate, false);
        highCut.design (juce::jmin ((double) highCutHz, 0.45 * rate), rate, true);

        settings = initialSettings;
        dryGain = targetDry();
        wetGain = targetWet();
        firstBlock = true;
        rampPerSample = (float) (1.0 / (0.02 * rate));   // a full MIX change takes 20 ms

        versionCounter = 0;
        versions[0] = makeVersion (lengthInSamples (settings.lengthMs));
        currentVersion = 0;
        samplesSinceNewVersion = minSamplesBetweenVersions;

        for (auto& stage : stages)
            std::fill (stage.pieceVersion.begin(), stage.pieceVersion.end(), noVersion);

        resetEngine();
    }

    void setSettings (const Settings& newSettings) noexcept
    {
        settings = newSettings;
        settings.lengthMs = juce::jlimit (minLengthMs, maxLengthMs, settings.lengthMs);
        settings.mix = juce::jlimit (0.0f, 1.0f, settings.mix);

        const double wanted = lengthInSamples (settings.lengthMs);

        if (resting)
        {
            // Nothing is playing, so the new LENGTH can apply straight away
            if (std::abs (wanted - versions[currentVersion % numVersions].lengthSamples) > 1e-6)
                startVersion (wanted);
        }
    }

    /** Silences everything (e.g. when playback jumps). */
    void reset() noexcept
    {
        resetEngine();
        firstBlock = true;
    }

    /** Works on the buffer in place. Its first numInputChannels channels hold
        your sound; the rest (if any) are filled with it before mixing. */
    BlockResult process (juce::AudioBuffer<float>& buffer, int numInputChannels) noexcept
    {
        BlockResult result;
        const int numSamples = buffer.getNumSamples();
        const int numOut = juce::jmin (buffer.getNumChannels(), 2);

        if (numSamples <= 0 || numOut <= 0)
            return result;

        const int numIn = juce::jlimit (1, numOut, numInputChannels);

        // A mono track feeding a stereo output: the original goes to both sides
        for (int c = numIn; c < buffer.getNumChannels(); ++c)
            buffer.copyFrom (c, 0, buffer, 0, 0, numSamples);

        float* const* channels = buffer.getArrayOfWritePointers();
        const bool feeding = targetWet() > 0.0f;   // (bypassed or MIX at 0%: nothing needs a burst)

        // The first sound after starting: MIX's levels start where they're set
        // (some DAWs load your settings after getting the plugin ready)
        if (firstBlock)
        {
            dryGain = targetDry();
            wetGain = targetWet();
            firstBlock = false;
        }

        for (int i = 0; i < numSamples;)
        {
            if (resting)
            {
                // Nothing is ringing: the original passes straight through
                // (at MIX's dry level) until a sound comes in
                const int firstSound = feeding ? findFirstSound (channels, numIn, i, numSamples) : numSamples;
                mixDryOnly (channels, numOut, i, firstSound - i);
                i = firstSound;

                if (i < numSamples)
                    resting = false;   // (everything is already clear, ready to go)

                continue;
            }

            const int length = juce::jmin (numSamples - i, blockSize - position);
            float* in = inputBlock.data() + position;

            // Into the reverb: left and right added together (nothing while bypassed)
            if (! feeding)
                juce::FloatVectorOperations::clear (in, length);
            else if (numIn == 1)
                juce::FloatVectorOperations::copy (in, channels[0] + i, length);
            else
            {
                juce::FloatVectorOperations::copyWithMultiply (in, channels[0] + i, 0.5f, length);
                juce::FloatVectorOperations::addWithMultiply (in, channels[1] + i, 0.5f, length);
            }

            mixWithReverb (channels, numOut, i, length, result);

            position += length;
            i += length;

            if (position == blockSize)
            {
                position = 0;
                step();
            }

            // Bypassed (or MIX at 0%) and faded out: stop at once, rather than ringing on unheard
            if (! feeding && wetGain == 0.0f && ! resting)
                resetEngine();
        }

        return result;
    }

    //==========================================================================
    // For the tests and the display
    bool isResting() const noexcept                 { return resting; }
    int getBlockSize() const noexcept               { return blockSize; }
    int getPredelaySamples() const noexcept         { return predelaySamples; }
    int getNumStages() const noexcept               { return (int) stages.size(); }
    float getCurrentLengthMs() const noexcept       { return (float) (versions[currentVersion % numVersions].lengthSamples * 1000.0 / rate); }

    /** The burst for a LENGTH (before the tone filters), sample by sample from
        the hit - exactly what the engine uses. */
    void renderBurst (int channel, float lengthMs, float* destination, int numSamples) const
    {
        const auto version = makeVersion (lengthInSamples (lengthMs));
        const auto& source = noise[(size_t) juce::jlimit (0, 1, channel)];
        std::vector<float> shape ((size_t) numSamples);
        fillEnvelope (shape.data(), 0, numSamples, version, 1.0);

        for (int i = 0; i < numSamples; ++i)
            destination[i] = i < (int) source.size() ? (float) (shape[(size_t) i] * source[(size_t) i]) : 0.0f;
    }

    /** The tone filters on their own (for checking against the real thing). */
    struct ToneFilter;
    ToneFilter makeToneFilter() const;

private:
    //==========================================================================
    struct AlignedFree { void operator() (float* p) const noexcept { pffft_aligned_free (p); } };
    struct SetupFree   { void operator() (PFFFT_Setup* s) const noexcept { if (s != nullptr) pffft_destroy_setup (s); } };
    using Floats = std::unique_ptr<float[], AlignedFree>;

    static Floats allocate (size_t count)
    {
        auto* memory = static_cast<float*> (pffft_aligned_malloc (juce::jmax ((size_t) 1, count) * sizeof (float)));
        std::fill (memory, memory + count, 0.0f);
        return Floats (memory);
    }

    /** One size of piece. The burst's pieces of this size, as spectra, and the
        spectra of the latest stretches of input (a ring: "slots"). */
    struct Stage
    {
        int pieceSize = 0, fftSize = 0, offset = 0, numPieces = 0, stepsPerBlock = 1;
        std::unique_ptr<PFFFT_Setup, SetupFree> fft;
        Floats slots;
        std::vector<uint8_t> slotSilent;
        std::vector<uint32_t> slotVersion;     // the LENGTH each stretch of input plays with
        int newestSlot = 0;
        std::array<Floats, 2> pieces;          // left and right
        std::vector<uint32_t> pieceVersion;    // the LENGTH each piece was last built for
        std::array<Floats, 2> sum;
        Floats frame, work;
    };

    /** A LENGTH, as the engine uses it. */
    struct Version
    {
        double lengthSamples = 0.0;   // from the hit until the burst is shut
        int activeLength = 0;         // how far its pieces reach (counted from blockSize after the hit)
    };

    static constexpr uint32_t noVersion = 0xffffffffu;
    static constexpr int numVersions = 64;

    //==========================================================================
    // Tone filters: 2nd-order Butterworth high- and low-pass (the same curves
    // at every sample rate)
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;   // (in double: a 180 Hz filter in float adds noise at about -95 dB)
        std::array<double, 2> s1 {}, s2 {};

        void design (double hz, double sampleRate, bool lowPass)
        {
            const double w0 = juce::MathConstants<double>::twoPi * hz / sampleRate;
            const double cosine = std::cos (w0), alpha = std::sin (w0) / (2.0 * std::sqrt (0.5));
            const double a0 = 1.0 + alpha;
            const double b = lowPass ? (1.0 - cosine) * 0.5 : (1.0 + cosine) * 0.5;
            b0 = b / a0;
            b1 = (lowPass ? 2.0 * b : -2.0 * b) / a0;
            b2 = b / a0;
            a1 = -2.0 * cosine / a0;
            a2 = (1.0 - alpha) / a0;
        }

        void process (float* x, int n, int channel) noexcept
        {
            double z1 = s1[(size_t) channel], z2 = s2[(size_t) channel];

            for (int i = 0; i < n; ++i)
            {
                const double in = x[i];
                const double out = b0 * in + z1;
                z1 = b1 * in - a1 * out + z2;
                z2 = b2 * in - a2 * out;
                x[i] = (float) out;
            }

            s1[(size_t) channel] = z1;
            s2[(size_t) channel] = z2;
        }

        void clear() noexcept   { s1 = {}; s2 = {}; }
    };

public:
    /** The tone filters on their own (for the tests). */
    struct ToneFilter
    {
        Biquad low, high;
        void process (float* x, int n) noexcept   { low.process (x, n, 0); high.process (x, n, 0); }
    };

private:
    //==========================================================================
    float targetDry() const noexcept   { return settings.bypassed ? 1.0f : dryGainFor (settings.mix); }
    float targetWet() const noexcept   { return settings.bypassed ? 0.0f : wetGainFor (settings.mix); }

    double lengthInSamples (float lengthMs) const noexcept
    {
        return juce::jlimit ((double) minLengthMs, (double) maxLengthMs, (double) lengthMs) * 1e-3 * rate;
    }

    Version makeVersion (double lengthSamples) const noexcept
    {
        Version version;
        version.lengthSamples = lengthSamples;
        version.activeLength = (int) std::ceil (lengthSamples) - blockSize;
        return version;
    }

    void startVersion (double lengthSamples) noexcept
    {
        currentVersion = ++versionCounter;
        versions[currentVersion % numVersions] = makeVersion (lengthSamples);
        samplesSinceNewVersion = 0;
    }

    /** The burst's shape (times its noise level and scale) for samples
        [first, first + count) after the hit. */
    void fillEnvelope (float* destination, int first, int count, const Version& version, double scale) const noexcept
    {
        const double body = version.lengthSamples - predelaySamples;               // the burst itself, after the gap
        const double sagPerSample = sagDb / 20.0 * std::log (10.0) / body;          // (in nepers)
        const double level = noiseLevel * scale;
        double tau = (double) (first - predelaySamples);
        double sag = std::exp (-sagPerSample * tau);
        const double sagStep = std::exp (-sagPerSample);

        for (int i = 0; i < count; ++i, tau += 1.0, sag *= sagStep)
        {
            if (tau <= 0.0 || tau >= body)
            {
                destination[i] = 0.0f;
                continue;
            }

            const double opening = juce::jmin (1.0, tau / attackSamples);
            const double untilShut = (body - tau) / slamSamples;
            const double slam = untilShut >= 1.0 ? 1.0 : 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * untilShut);
            destination[i] = (float) (level * opening * sag * slam);
        }
    }

    static void makeNoise (std::vector<float>& destination, int length, uint64_t seed)
    {
        // splitmix64, and Box-Muller for a bell-curve (Gaussian) spread - the
        // same on every computer
        uint64_t state = seed;
        const auto next = [&state]
        {
            uint64_t z = (state += 0x9e3779b97f4a7c15ull);
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            return z ^ (z >> 31);
        };
        const auto uniform = [&next] { return ((double) (next() >> 11) + 0.5) * (1.0 / 9007199254740992.0); };

        destination.assign ((size_t) length, 0.0f);

        for (int i = 0; i < length; i += 2)
        {
            const double radius = std::sqrt (-2.0 * std::log (uniform()));
            const double angle = juce::MathConstants<double>::twoPi * uniform();
            destination[(size_t) i] = (float) (radius * std::cos (angle));

            if (i + 1 < length)
                destination[(size_t) i + 1] = (float) (radius * std::sin (angle));
        }
    }

    //==========================================================================
    void resetEngine() noexcept
    {
        for (auto& stage : stages)
        {
            std::fill (stage.slotSilent.begin(), stage.slotSilent.end(), (uint8_t) 1);
            stage.newestSlot = 0;
        }

        if (inputRing != nullptr)
        {
            std::fill (inputRing.get(), inputRing.get() + ringSize, 0.0f);
            for (auto& ring : outputRing)
                std::fill (ring.get(), ring.get() + ringSize, 0.0f);
        }

        std::fill (inputBlock.begin(), inputBlock.end(), 0.0f);
        for (auto& block : wetBlock)
            std::fill (block.begin(), block.end(), 0.0f);

        lowCut.clear();
        highCut.clear();
        writePos = readPos = 0;
        position = 0;
        stepCount = 0;
        quietSteps = 1 << 30;
        resting = true;

        // Pick up a LENGTH change that was waiting
        const double wanted = lengthInSamples (settings.lengthMs);
        if (std::abs (wanted - versions[currentVersion % numVersions].lengthSamples) > 1e-6)
            startVersion (wanted);
    }

    /** One little block of input is in: run the pieces that are due, and get
        the next little block of reverb ready. */
    void step() noexcept
    {
        const bool silent = isSilent (inputBlock.data(), blockSize);
        writeRing (inputRing.get(), writePos, inputBlock.data(), blockSize);
        writePos += (uint32_t) blockSize;
        quietSteps = silent ? juce::jmin (quietSteps + 1, 1 << 30) : 0;
        ++stepCount;

        for (auto& stage : stages)
            if (stepCount % (uint32_t) stage.stepsPerBlock == 0)
                runStage (stage, quietSteps >= stage.stepsPerBlock);

        // A new LENGTH starts only here, just after the biggest pieces' boundary
        // (when every size of piece has just finished a stretch), so each sound
        // plays with exactly one LENGTH from start to finish.
        samplesSinceNewVersion = juce::jmin (samplesSinceNewVersion + blockSize, 1 << 30);

        if (stepCount % (uint32_t) stages.back().stepsPerBlock == 0)
        {
            const double wanted = lengthInSamples (settings.lengthMs);

            if (std::abs (wanted - versions[currentVersion % numVersions].lengthSamples) >= 0.0005 * rate
                 && samplesSinceNewVersion >= minSamplesBetweenVersions)
                startVersion (wanted);
        }

        // The next little block of reverb, through the tone filters
        for (int c = 0; c < numWet; ++c)
        {
            auto& block = wetBlock[(size_t) c];
            takeFromRing (outputRing[(size_t) c].get(), readPos, block.data(), blockSize);
            lowCut.process (block.data(), blockSize, c);
            highCut.process (block.data(), blockSize, c);
        }

        readPos += (uint32_t) blockSize;

        if (quietSteps >= restAfterSteps)
            resetEngine();   // everything has died away: rest
    }

    void runStage (Stage& stage, bool frameSilent) noexcept
    {
        const int numPieces = stage.numPieces;
        stage.newestSlot = (stage.newestSlot + 1) % numPieces;
        const int newest = stage.newestSlot;
        stage.slotSilent[(size_t) newest] = frameSilent ? 1 : 0;
        stage.slotVersion[(size_t) newest] = currentVersion;

        if (! frameSilent)
        {
            // The latest stretch of input (padded with silence), as a spectrum
            readRing (inputRing.get(), writePos - (uint32_t) stage.pieceSize, stage.frame.get(), stage.pieceSize);
            std::fill (stage.frame.get() + stage.pieceSize, stage.frame.get() + stage.fftSize, 0.0f);
            pffft_transform (stage.fft.get(), stage.frame.get(), slot (stage, newest), stage.work.get(), PFFFT_FORWARD);
        }

        // Each piece of the burst, times the stretch of input it applies to now
        bool anything = false;

        for (int age = 0; age < numPieces; ++age)
        {
            const int s = (newest - age + numPieces) % numPieces;

            if (stage.slotSilent[(size_t) s] != 0)
                continue;

            const uint32_t version = stage.slotVersion[(size_t) s];

            if (stage.offset + age * stage.pieceSize >= versions[version % numVersions].activeLength)
                continue;   // past the end of this stretch's burst

            if (stage.pieceVersion[(size_t) age] != version)
                buildPiece (stage, age, version);

            if (! anything)
            {
                for (int c = 0; c < numWet; ++c)
                    std::fill (stage.sum[(size_t) c].get(), stage.sum[(size_t) c].get() + stage.fftSize, 0.0f);

                anything = true;
            }

            for (int c = 0; c < numWet; ++c)
                pffft_zconvolve_accumulate (stage.fft.get(), slot (stage, s), piece (stage, c, age), stage.sum[(size_t) c].get(), 1.0f);
        }

        if (! anything)
            return;

        // Back to sound, and into the output from now on (twice the piece's
        // length: the second half overlaps the next stretch's)
        for (int c = 0; c < numWet; ++c)
        {
            pffft_transform (stage.fft.get(), stage.sum[(size_t) c].get(), stage.frame.get(), stage.work.get(), PFFFT_BACKWARD);
            addToRing (outputRing[(size_t) c].get(), readPos, stage.frame.get(), stage.fftSize);
        }
    }

    /** Makes one piece of the burst (both sides) for a LENGTH. */
    void buildPiece (Stage& stage, int index, uint32_t version) noexcept
    {
        const int first = stage.offset + index * stage.pieceSize + blockSize;   // samples after the hit
        const int n = stage.pieceSize;

        // (1 / FFT size: PFFFT leaves the scaling to us)
        fillEnvelope (envelope.data(), first, n, versions[version % numVersions], 1.0 / stage.fftSize);

        for (int c = 0; c < 2; ++c)
        {
            const float* source = noise[(size_t) c].data() + first;
            float* buffer = scratch.get();

            for (int i = 0; i < n; ++i)
                buffer[i] = source[i] * envelope[(size_t) i];

            std::fill (buffer + n, buffer + 2 * n, 0.0f);
            pffft_transform (stage.fft.get(), buffer, piece (stage, c, index), stage.work.get(), PFFFT_FORWARD);
        }

        stage.pieceVersion[(size_t) index] = version;
    }

    float* slot (Stage& stage, int index) const noexcept               { return stage.slots.get() + (size_t) index * (size_t) stage.fftSize; }
    float* piece (Stage& stage, int channel, int index) const noexcept { return stage.pieces[(size_t) channel].get() + (size_t) index * (size_t) stage.fftSize; }

    //==========================================================================
    // The rings (sizes are powers of two, positions just keep counting)
    void writeRing (float* ring, uint32_t start, const float* source, int n) const noexcept
    {
        const uint32_t mask = (uint32_t) ringSize - 1;
        for (int i = 0; i < n; ++i)
            ring[(start + (uint32_t) i) & mask] = source[i];
    }

    void readRing (const float* ring, uint32_t start, float* destination, int n) const noexcept
    {
        const uint32_t mask = (uint32_t) ringSize - 1;
        const int first = juce::jmin (n, (int) ((uint32_t) ringSize - (start & mask)));
        std::copy (ring + (start & mask), ring + (start & mask) + first, destination);
        std::copy (ring, ring + (n - first), destination + first);
    }

    void addToRing (float* ring, uint32_t start, const float* source, int n) const noexcept
    {
        const uint32_t mask = (uint32_t) ringSize - 1;
        const int first = juce::jmin (n, (int) ((uint32_t) ringSize - (start & mask)));
        juce::FloatVectorOperations::add (ring + (start & mask), source, first);
        juce::FloatVectorOperations::add (ring, source + first, n - first);
    }

    void takeFromRing (float* ring, uint32_t start, float* destination, int n) const noexcept
    {
        readRing (ring, start, destination, n);
        const uint32_t mask = (uint32_t) ringSize - 1;
        for (int i = 0; i < n; ++i)
            ring[(start + (uint32_t) i) & mask] = 0.0f;
    }

    static bool isSilent (const float* x, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
            if (x[i] != 0.0f)
                return false;

        return true;
    }

    static int findFirstSound (float* const* channels, int numIn, int start, int end) noexcept
    {
        for (int i = start; i < end; ++i)
            for (int c = 0; c < numIn; ++c)
                if (channels[c][i] != 0.0f)
                    return i;

        return end;
    }

    //==========================================================================
    // Mixing: MIX's dry and reverb levels glide to new settings over 20 ms
    void mixDryOnly (float* const* channels, int numOut, int start, int n) noexcept
    {
        const float dryTarget = targetDry(), wetTarget = targetWet();

        if (n <= 0)
            return;

        if (dryGain == dryTarget)
        {
            if (dryGain != 1.0f)
                for (int c = 0; c < numOut; ++c)
                    juce::FloatVectorOperations::multiply (channels[c] + start, dryGain, n);
        }
        else
        {
            float gain = dryGain;

            for (int i = 0; i < n; ++i)
            {
                gain = glide (gain, dryTarget);
                for (int c = 0; c < numOut; ++c)
                    channels[c][start + i] *= gain;
            }

            dryGain = gain;
        }

        // (the reverb is silent, but its level keeps gliding so it's right when sound comes in)
        wetGain = wetGain < wetTarget ? juce::jmin (wetTarget, wetGain + rampPerSample * (float) n)
                                      : juce::jmax (wetTarget, wetGain - rampPerSample * (float) n);
    }

    void mixWithReverb (float* const* channels, int numOut, int start, int n, BlockResult& result) noexcept
    {
        const float dryTarget = targetDry(), wetTarget = targetWet();

        for (int c = 0; c < numOut; ++c)
        {
            const float* wet = wetBlock[(size_t) juce::jmin (c, numWet - 1)].data() + position;
            float* out = channels[c] + start;

            if (dryGain == dryTarget && wetGain == wetTarget)
            {
                if (dryGain != 1.0f)
                    juce::FloatVectorOperations::multiply (out, dryGain, n);

                if (wetGain != 0.0f)
                {
                    juce::FloatVectorOperations::addWithMultiply (out, wet, wetGain, n);
                    const auto range = juce::FloatVectorOperations::findMinAndMax (wet, n);
                    result.wetPeak = juce::jmax (result.wetPeak, wetGain * juce::jmax (-range.getStart(), range.getEnd()));
                }
            }
            else
            {
                float d = dryGain, w = wetGain;

                for (int i = 0; i < n; ++i)
                {
                    d = glide (d, dryTarget);
                    w = glide (w, wetTarget);
                    out[i] = out[i] * d + wet[i] * w;
                    result.wetPeak = juce::jmax (result.wetPeak, std::abs (wet[i] * w));
                }

                if (c == numOut - 1)
                {
                    dryGain = d;
                    wetGain = w;
                }
            }
        }
    }

    float glide (float value, float target) const noexcept
    {
        return value < target ? juce::jmin (target, value + rampPerSample)
                              : juce::jmax (target, value - rampPerSample);
    }

    //==========================================================================
    double rate = 48000.0;
    int numWet = 2;
    int predelaySamples = 96, blockSize = 64, maxImpulse = 48001;
    double attackSamples = 96.0, slamSamples = 576.0, noiseLevel = noiseLevelAt48k;

    std::vector<Stage> stages;
    int ringSize = 0;
    Floats inputRing, scratch;
    std::array<Floats, 2> outputRing;
    std::vector<float> envelope, inputBlock;
    std::array<std::vector<float>, 2> wetBlock, noise;

    Biquad lowCut, highCut;

    std::array<Version, numVersions> versions {};
    uint32_t versionCounter = 0, currentVersion = 0;
    int samplesSinceNewVersion = 0, minSamplesBetweenVersions = 960;

    uint32_t writePos = 0, readPos = 0, stepCount = 0;
    int position = 0, quietSteps = 1 << 30, restAfterSteps = 1000;
    bool resting = true;

    Settings settings;
    float dryGain = 1.0f, wetGain = 0.0f, rampPerSample = 0.001f;
    bool firstBlock = true;
};

inline GatedReverb::ToneFilter GatedReverb::makeToneFilter() const
{
    ToneFilter filter;
    filter.low.design (lowCutHz, rate, false);
    filter.high.design (juce::jmin ((double) highCutHz, 0.45 * rate), rate, true);
    return filter;
}

JUCE_END_IGNORE_WARNINGS_GCC_LIKE
