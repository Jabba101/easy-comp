#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <vector>

//==============================================================================
/**
    Easy Verb's natural reverbs - the spaces jazz, soul and most records
    before the 80s were made in. (The gated burst is Effects/GatedReverb.h.)

    The live spaces - the sound of a band playing in a real place, rather
    than the "studio reverb" sound. Each has strong early reflections off
    the walls (they're what tell your ear the size of the place):

      ROOM     a rehearsal room or drum room: close walls, a short, lively
               tail.
      CLUB     a small gig venue: a bigger space, the reflections a little
               further apart, a fuller tail.
      HALL     a theatre or concert hall: the walls far away, the
               reflections spread out, a long, smooth tail.

    And one studio classic, for the odd vocal or snare:

      PLATE    the 50s/60s steel plate (EMT 140): no "room" at all, just a
               bright, silky, very dense wash.

      DECAY      how long the tail takes to die away (to -60 dB), 0.2 - 6 s
      PRE-DELAY  a gap before the reverb starts (0 - 120 ms), so the sound
                 stays clear and upfront and the space sits behind it
      TONE       darker (the tail loses its highs quickly, like a real room
                 with curtains and people) to brighter (a glassy plate)

    How:
      ROOM, CLUB and HALL: a feedback delay network - eight delay lines feeding
      each other through a mixing matrix (Hadamard), each with its own
      gentle high cut and a gain set from DECAY, so every frequency dies
      away in the time asked; four allpass "diffusers" in front smear each
      sound into a dense cloud first; two lines move very slightly so the
      tail never rings metallically. In front, a set of early reflections,
      spread further apart the bigger the space.
      PLATE: Jon Dattorro's plate (1997): diffusers, then a "figure of eight"
      tank of delays and allpasses, with the outputs taken from seven
      points inside it on each side.

    The reverb hears your sound in mono and spreads it across both sides
    (as real plates and chambers were fed). A gentle low cut (100 Hz) keeps
    the lows of the reverb from turning to mud. No delay added to the dry
    sound. In silence, once the tail has died away, it rests (no cost).
*/
class NaturalReverb
{
public:
    enum class Type { room = 0, club = 1, hall = 2, plate = 3 };

    static constexpr float minDecay = 0.2f, maxDecay = 6.0f, maxPreDelayMs = 120.0f;

    struct Settings
    {
        Type  type       = Type::room;
        float decay      = 1.2f;     // seconds
        float preDelayMs = 15.0f;
        float tone       = 0.0f;     // -1 (dark) .. +1 (bright)
    };

    //==========================================================================
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        const auto samples = [this] (double ms) { return (int) std::ceil (ms * sampleRate / 1000.0); };

        preDelay.setSize (samples (maxPreDelayMs) + 4);
        early.setSize (samples (140.0) + 4);

        for (auto& line : lines)
            line.setSize (samples (90.0) + 64);
        for (auto& side : diffusers)
            for (auto& d : side)
                d.setSize (samples (20.0) + 4);

        // The plate, at its own design rate (29761 Hz), scaled to ours
        const double k = sampleRate / 29761.0;
        const auto scaled = [k] (int n) { return (int) std::ceil (n * k); };
        for (int i = 0; i < 4; ++i)
            plateIn[(size_t) i].setSize (scaled (plateInLengths[i]) + 4);
        for (int i = 0; i < 2; ++i)
        {
            plateModAp[(size_t) i].setSize (scaled (plateModLengths[i] + 32) + 4);
            plateDelayA[(size_t) i].setSize (scaled (plateDelayALengths[i]) + 4);
            plateAp[(size_t) i].setSize (scaled (plateApLengths[i]) + 4);
            plateDelayB[(size_t) i].setSize (scaled (plateDelayBLengths[i]) + 4);
        }

        reset();
        setSettings (settings, true);
    }

    void reset() noexcept
    {
        preDelay.clear(); early.clear();
        for (auto& l : lines) l.clear();
        for (auto& side : diffusers) for (auto& d : side) d.clear();
        for (auto& d : plateIn) d.clear();
        for (auto& d : plateModAp) d.clear();
        for (auto& d : plateAp) d.clear();
        for (auto& d : plateDelayA) d.clear();
        for (auto& d : plateDelayB) d.clear();
        lineDamp = {};
        plateDamp = { 0.0f, 0.0f };
        plateLink = { 0.0f, 0.0f };
        bandwidthState = 0.0f;
        lowCut = { 0.0f, 0.0f };
        modPhase = { 0.0, 0.25 };
        quietFor = 0;
        resting = false;
    }

    void setSettings (const Settings& s, bool jump = false)
    {
        const bool typeChanged = s.type != settings.type;
        settings = s;
        const float decay = juce::jlimit (minDecay, maxDecay, s.decay);
        const float tone = juce::jlimit (-1.0f, 1.0f, s.tone);

        // TONE: how fast the highs die away in the tail, and how bright the sound going in is
        // (the very top's decay time as a share of DECAY: 8% dark .. 18% .. 50% bright)
        const double highsShare = 0.18 * std::pow (tone < 0.0f ? 0.18 / 0.08 : 0.5 / 0.18, (double) tone);
        const auto dampFor = [decay] (double seconds, double share)
        {
            // A one-pole high cut, set so the very top dies away in DECAY x share while the lows take DECAY
            const double r = std::pow (10.0, -3.0 * seconds / decay * (1.0 / share - 1.0));
            return (float) (1.0 - (1.0 - r) / (1.0 + r));
        };
        bandwidthCoefficient = onePole (3500.0 * std::pow (tone < 0.0f ? 3.5 / 1.8 : 9.0 / 3.5, (double) tone));   // 1.8 - 3.5 - 9 kHz
        lowCutCoefficient = onePole (100.0);

        // Each delay line's feedback gain: -60 dB after DECAY seconds
        const auto& space = spaceFor (s.type);
        for (size_t i = 0; i < lineCount; ++i)
        {
            const double length = roomMs[i] * space.size;
            lineSamples[i] = (float) (length * sampleRate / 1000.0);
            lineGain[i] = (float) std::pow (10.0, -3.0 * length / 1000.0 / decay);
            lineDampCoefficient[i] = dampFor (length / 1000.0, highsShare);
        }

        const double halfLoop = 10822.0 / 29761.0;   // (the plate's average trip round half its tank)
        plateDecay = (float) juce::jmin (0.9995, std::pow (10.0, -3.0 * halfLoop * 0.5 / decay));   // (applied twice each half)
        // (a steel plate is brighter than any room: its highs ring on far longer)
        plateDampCoefficient = dampFor (halfLoop, juce::jmin (0.9, highsShare * 2.5));

        // Loudness: about the same whatever DECAY, so turning it doesn't jump in level
        level = (s.type == Type::plate ? 0.7f : space.level) * std::pow (1.2f / decay, 0.2f);

        preDelaySamples = juce::jlimit (0.0f, maxPreDelayMs, s.preDelayMs) * (float) sampleRate / 1000.0f;

        if (typeChanged && ! jump)
            reset();
    }

    /** Adds the reverb of `input` (mono) to wetL / wetR (as loud as asked by `gain`).
        Returns false if it's resting (silent in, tail died away) and did nothing. */
    bool process (const float* input, float* wetL, float* wetR, int n, float gain) noexcept
    {
        // Resting: silence in, and the tail long gone
        float loudest = 0.0f;
        for (int i = 0; i < n; ++i)
            loudest = juce::jmax (loudest, std::abs (input[i]));

        if (loudest > 0.0f)
        {
            if (resting) reset();
            quietFor = 0;
        }
        else
        {
            quietFor += n;
            const int tail = (int) ((juce::jlimit (minDecay, maxDecay, settings.decay) * 1.3f + maxPreDelayMs / 1000.0f + 0.2f) * (float) sampleRate);
            if (quietFor > tail)
                resting = true;
        }

        if (resting)
            return false;

        const float g = gain * level;
        for (int i = 0; i < n; ++i)
        {
            // Pre-delay, and the brightness going in
            preDelay.write (input[i]);
            float x = preDelay.read ((int) std::lround (preDelaySamples));
            bandwidthState += bandwidthCoefficient * (x - bandwidthState);
            x = bandwidthState;

            float l, r;
            if (settings.type == Type::plate) plate (x, l, r);
            else                              network (x, l, r);

            // A gentle low cut on the reverb only
            lowCut[0] += lowCutCoefficient * (l - lowCut[0]);
            lowCut[1] += lowCutCoefficient * (r - lowCut[1]);
            wetL[i] += (l - lowCut[0]) * g;
            wetR[i] += (r - lowCut[1]) * g;
        }
        return true;
    }

    bool isResting() const noexcept   { return resting; }

private:
    //==========================================================================
    struct Delay
    {
        std::vector<float> buffer;
        int mask = 0, pos = 0;

        void setSize (int length)
        {
            int size = 1;
            while (size < length + 2) size <<= 1;
            buffer.assign ((size_t) size, 0.0f);
            mask = size - 1;
            pos = 0;
        }
        void clear()                                   { std::fill (buffer.begin(), buffer.end(), 0.0f); pos = 0; }
        void write (float x) noexcept                  { pos = (pos + 1) & mask; buffer[(size_t) pos] = x; }
        float read (int delay) const noexcept          { return buffer[(size_t) ((pos - delay) & mask)]; }
        /** Between samples (for the lines that move): cubic, so the highs aren't dulled. */
        float readFractional (float delay) const noexcept
        {
            const int whole = (int) delay;
            const float t = delay - (float) whole;
            const float y0 = read (whole - 1), y1 = read (whole), y2 = read (whole + 1), y3 = read (whole + 2);
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * t + c2) * t + c1) * t + y1;
        }
    };

    // An allpass "diffuser": smears a sound in time without colouring it
    struct Allpass
    {
        Delay d;
        void setSize (int length)   { d.setSize (length); }
        void clear()                { d.clear(); }
        float process (float x, float delay, float g, bool moving = false) noexcept
        {
            // (fixed ones at a whole number of samples: reading between samples would dull the highs, every trip round)
            const float delayed = moving ? d.readFractional (delay - 1.0f) : d.read (juce::jmax (0, (int) std::lround (delay) - 1));
            const float v = x + g * delayed;
            d.write (v);
            return delayed - g * v;
        }
        float tap (int at) const noexcept   { return d.read (at); }
    };

    float onePole (double hz) const noexcept
    {
        return (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * juce::jmin (hz, sampleRate * 0.45) / sampleRate));
    }

    float ms (double m) const noexcept   { return (float) (m * sampleRate / 1000.0); }

    //==========================================================================
    // ROOM / CHAMBER: early reflections, diffusion, then the eight-line network
    void network (float x, float& outL, float& outR) noexcept
    {
        const auto& space = spaceFor (settings.type);

        // Early reflections off the walls, further apart the bigger the space
        early.write (x);
        float erL = 0.0f, erR = 0.0f;
        for (auto& tap : earlyTaps)
        {
            const float v = early.read ((int) ms (tap.ms * space.earlySpread));
            erL += v * tap.left;
            erR += v * tap.right;
        }

        // Diffusion: four allpasses, a little different on each side
        float dl = x, dr = x;
        for (size_t k = 0; k < 4; ++k)
        {
            dl = diffusers[0][k].process (dl, ms (diffuserMs[k] * space.diffuserScale), space.diffusion);
            dr = diffusers[1][k].process (dr, ms (diffuserMs[k] * space.diffuserScale * 1.13), space.diffusion);
        }

        // The network: read each line (two of them moving very slightly), damp, mix, feed back
        std::array<float, lineCount> y {};
        for (size_t i = 0; i < lineCount; ++i)
        {
            float delay = lineSamples[i];
            if (i == 0 || i == 5)
            {
                auto& p = modPhase[i == 0 ? 0 : 1];
                p += (i == 0 ? 0.47 : 0.71) / sampleRate;
                if (p >= 1.0) p -= 1.0;
                delay += ms (0.35) * (float) std::sin (juce::MathConstants<double>::twoPi * p);
            }
            float v = (i == 0 || i == 5) ? lines[i].readFractional (delay) : lines[i].read ((int) std::lround (delay));
            lineDamp[i] += lineDampCoefficient[i] * (v - lineDamp[i]);   // (the highs die away faster)
            y[i] = lineDamp[i] * lineGain[i];
        }

        hadamard (y);

        // In: the left diffusion into the even lines, the right into the odd. Out: two
        // different (orthogonal) blends of all eight lines, so the sides stay unalike - wide
        for (size_t i = 0; i < lineCount; ++i)
            lines[i].write (y[i] + ((i & 1) == 0 ? dl : dr) * 0.5f);

        float tailL = 0.0f, tailR = 0.0f;
        for (size_t i = 0; i < lineCount; ++i)
        {
            tailL += lineDamp[i] * ((i & 1) == 0 ? 1.0f : -1.0f);
            tailR += lineDamp[i] * ((i & 2) == 0 ? 1.0f : -1.0f);
        }

        outL = tailL * 0.35f + erL * space.earlyLevel;
        outR = tailR * 0.35f + erR * space.earlyLevel;
    }

    // An 8 x 8 orthogonal mix (fast Hadamard): every line feeds every other, energy kept
    static void hadamard (std::array<float, 8>& v) noexcept
    {
        for (int span = 1; span < 8; span <<= 1)
            for (int i = 0; i < 8; i += span * 2)
                for (int j = i; j < i + span; ++j)
                {
                    const float a = v[(size_t) j], b = v[(size_t) (j + span)];
                    v[(size_t) j] = a + b;
                    v[(size_t) (j + span)] = a - b;
                }
        for (auto& x : v) x *= 0.35355339f;   // 1 / sqrt (8)
    }

    //==========================================================================
    // PLATE: Dattorro's figure-of-eight tank
    void plate (float x, float& outL, float& outR) noexcept
    {
        const float k = (float) (sampleRate / 29761.0);
        const float ins[] { 0.75f, 0.75f, 0.625f, 0.625f };
        for (int i = 0; i < 4; ++i)
            x = plateIn[(size_t) i].process (x, plateInLengths[i] * k, ins[i]);

        for (int half = 0; half < 2; ++half)
        {
            auto& p = modPhase[(size_t) half];
            p += (half == 0 ? 0.6 : 0.8) / sampleRate;
            if (p >= 1.0) p -= 1.0;
            const float excursion = 12.0f * k * (float) std::sin (juce::MathConstants<double>::twoPi * p);

            float v = x + plateLink[(size_t) (1 - half)] * plateDecay;
            v = plateModAp[(size_t) half].process (v, plateModLengths[half] * k + excursion, -0.7f, true);
            plateDelayA[(size_t) half].write (v);
            v = plateDelayA[(size_t) half].read ((int) (plateDelayALengths[half] * k));
            plateDamp[(size_t) half] += plateDampCoefficient * (v - plateDamp[(size_t) half]);
            v = plateDamp[(size_t) half] * plateDecay;
            v = plateAp[(size_t) half].process (v, plateApLengths[half] * k, 0.5f);
            plateDelayB[(size_t) half].write (v);
            plateLink[(size_t) half] = plateDelayB[(size_t) half].read ((int) (plateDelayBLengths[half] * k));
        }

        // The output taps (Dattorro's table), seven a side
        const auto at = [k] (int n) { return (int) (n * k); };
        outL =  plateDelayA[1].read (at (266)) + plateDelayA[1].read (at (2974)) - plateAp[1].tap (at (1913))
              + plateDelayB[1].read (at (1996)) - plateDelayA[0].read (at (1990)) - plateAp[0].tap (at (187))
              - plateDelayB[0].read (at (1066));
        outR =  plateDelayA[0].read (at (353)) + plateDelayA[0].read (at (3627)) - plateAp[0].tap (at (1228))
              + plateDelayB[0].read (at (2673)) - plateDelayA[1].read (at (2111)) - plateAp[1].tap (at (335))
              - plateDelayB[1].read (at (121));
        outL *= 0.6f;
        outR *= 0.6f;
    }

    //==========================================================================
    static constexpr size_t lineCount = 8;
    static constexpr std::array<double, lineCount> roomMs    { 9.3, 11.7, 13.1, 15.9, 17.3, 19.9, 22.1, 24.7 };
    static constexpr std::array<double, 4> diffuserMs { 4.1, 3.1, 7.3, 5.3 };

    // Each live space: its size (the delay lines and reflections scaled up), how loud
    // its reflections are, how dense its diffusion, and an overall level
    struct Space { double size, earlySpread, diffuserScale; float earlyLevel, diffusion, level; };
    static const Space& spaceFor (Type t) noexcept
    {
        static constexpr Space room  { 1.0, 1.0, 1.0, 0.55f, 0.60f, 0.60f },
                               club  { 1.8, 1.7, 1.4, 0.45f, 0.65f, 0.75f },
                               hall  { 3.2, 2.9, 2.2, 0.32f, 0.70f, 0.95f };
        return t == Type::hall ? hall : (t == Type::club ? club : room);
    }

    struct EarlyTap { float ms, left, right; };
    static constexpr std::array<EarlyTap, 10> earlyTaps {{
        { 4.3f, 0.75f, 0.30f }, { 6.1f, 0.25f, 0.70f }, { 9.7f, 0.55f, 0.20f }, { 11.9f, 0.20f, 0.60f }, { 15.4f, 0.45f, 0.35f },
        { 19.8f, 0.30f, 0.45f }, { 24.6f, 0.38f, 0.15f }, { 29.3f, 0.12f, 0.36f }, { 36.2f, 0.25f, 0.20f }, { 44.7f, 0.15f, 0.22f } }};

    static constexpr int plateInLengths[4]     { 142, 107, 379, 277 };
    static constexpr int plateModLengths[2]    { 672, 908 };
    static constexpr int plateDelayALengths[2] { 4453, 4217 };
    static constexpr int plateApLengths[2]     { 1800, 2656 };
    static constexpr int plateDelayBLengths[2] { 3720, 3163 };

    double sampleRate = 48000.0;
    Settings settings;

    Delay preDelay, early;
    std::array<Delay, lineCount> lines;
    std::array<std::array<Allpass, 4>, 2> diffusers;
    std::array<float, lineCount> lineSamples {}, lineGain {}, lineDamp {}, lineDampCoefficient {};

    std::array<Allpass, 4> plateIn;
    std::array<Allpass, 2> plateModAp, plateAp;
    std::array<Delay, 2> plateDelayA, plateDelayB;
    std::array<float, 2> plateDamp {}, plateLink {};
    float plateDecay = 0.5f;

    std::array<double, 2> modPhase { 0.0, 0.25 };
    std::array<float, 2> lowCut {};
    float plateDampCoefficient = 0.5f, bandwidthCoefficient = 0.5f, lowCutCoefficient = 0.01f, bandwidthState = 0.0f;
    float level = 0.5f, preDelaySamples = 0.0f;
    int quietFor = 0;
    bool resting = false;
};
