#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#ifndef EASY_PLUGINS_SSE2   // (can be set to 0 to try the plain version on an Intel/AMD computer)
 #if defined (__SSE2__) || defined (_M_X64) || defined (_M_AMD64) || (defined (_M_IX86_FP) && _M_IX86_FP >= 2)
  #define EASY_PLUGINS_SSE2 1
 #else
  #define EASY_PLUGINS_SSE2 0
 #endif
#endif

#if EASY_PLUGINS_SSE2
 #include <emmintrin.h>
#endif

//==============================================================================
/**
    Quick versions of the maths that turns volumes into decibels and back,
    plus a few helpers for working on several samples at once.

    The effects here convert to and from decibels for every single sample:
    tens of thousands of times a second, on every track they're used on. The
    standard versions (std::log10, std::pow) work to 15 or more digits, which
    no one can hear. These are accurate to about 0.00002 dB - thousands of
    times finer than the smallest change anyone can hear - and several times
    quicker, so the plugins leave more of your computer for everything else.

    How they work, roughly: a computer stores a number as "this much, times
    two to the power of that". The power of two can be read straight out of
    how the number is stored, so only a small leftover part needs working
    out, with a short formula fitted in advance for the best accuracy.

    They're exact where it matters: a gain of 1 is exactly 0 dB, and 0 dB is
    exactly a gain of 1, so a setting that's meant to change nothing still
    changes nothing, bit for bit.

    Vec4 holds four samples' worth of numbers and works on all four at once.
    On Intel and AMD processors that uses SSE2, which every 64-bit one has
    (nothing newer, so the plugins run on older computers too). Elsewhere
    it's four plain numbers, which the compiler combines by itself. Either
    way the answers are exactly the same as doing one number at a time.
*/
namespace FastMath
{
    namespace Detail
    {
        forcedinline std::int32_t bitsOf (float x) noexcept    { std::int32_t i; std::memcpy (&i, &x, sizeof (i)); return i; }
        forcedinline float floatFrom (std::int32_t i) noexcept { float x; std::memcpy (&x, &i, sizeof (x)); return x; }

        /** n * 2^23: n moved up to where a float keeps its power of two. */
        forcedinline std::int32_t toExponentBits (std::int32_t n) noexcept   { return (std::int32_t) ((std::uint32_t) n << 23); }

        // The fitted formulas (see log2 and exp2 below for what they're for)
        constexpr float log2Fit[] = { 1.44269991f, -0.721375227f, 0.480459064f, -0.358958989f, 0.297286838f, -0.272842526f, 0.170925021f };
        constexpr float exp2Fit[] = { 0.693146765f, 0.240222007f, 0.0555090681f, 0.00967340078f, 0.00132302393f };
        constexpr std::int32_t sqrtHalfBits = 0x3f3504f3;    // how 0.7071 (the square root of 1/2) is stored
        constexpr float roundingShift = 12582912.0f;          // 1.5 * 2^23: adding it rounds a number to a whole one
    }

    /** log2 (x), for any normal positive x (to within about 0.0000004 for x
        near 1, and about the size of a float's own rounding further out). */
    forcedinline float log2 (float x) noexcept
    {
        using namespace Detail;

        // Split x into 2^e * m, with m between 0.707 and 1.414 (so the formula
        // below only has to cover the part of the curve nearest to log2 (1) = 0).
        const std::int32_t bits = bitsOf (x);
        const std::int32_t e    = (bits - sqrtHalfBits) >> 23;
        const float t = floatFrom (bits - toExponentBits (e)) - 1.0f;

        // log2 (1 + t): a 7th-order fit, best possible over the whole range of t.
        float p = log2Fit[6];
        p = p * t + log2Fit[5];
        p = p * t + log2Fit[4];
        p = p * t + log2Fit[3];
        p = p * t + log2Fit[2];
        p = p * t + log2Fit[1];
        p = p * t + log2Fit[0];

        return (float) e + p * t;
    }

    /** 2 to the power x, for x between -126 and +126 (to within about 0.00002%). */
    forcedinline float exp2 (float x) noexcept
    {
        using namespace Detail;

        // Split x into a whole number k and a leftover f between -0.5 and +0.5.
        const float shifted = x + roundingShift;
        const std::int32_t k = bitsOf (shifted) - bitsOf (roundingShift);
        const float f = x - (shifted - roundingShift);

        // 2^f: a 5th-order fit, exactly 1 when f is 0.
        float q = exp2Fit[4];
        q = q * f + exp2Fit[3];
        q = q * f + exp2Fit[2];
        q = q * f + exp2Fit[1];
        q = q * f + exp2Fit[0];
        q = q * f + 1.0f;

        // ...times 2^k, by adding k straight onto the power of two q is stored with.
        return floatFrom (bitsOf (q) + toExponentBits (k));
    }

    constexpr float decibelsPerOctave = 6.02059984f;    // 20 * log10 (2): dB = 6.02 * log2 (gain)
    constexpr float octavesPerDecibel = 0.166096404f;   // 1 / 6.02
    constexpr float maxDecibels       = 750.0f;         // (keeps exp2 in range, whatever it's given)

    /** A gain (1 = unchanged) in dB, like juce::Decibels::gainToDecibels:
        anything at or below minusInfinityDb (including 0) reads as minusInfinityDb. */
    forcedinline float gainToDecibels (float gain, float minusInfinityDb = -100.0f) noexcept
    {
        return gain > 0.0f ? std::max (minusInfinityDb, decibelsPerOctave * log2 (gain)) : minusInfinityDb;
    }

    /** dB to a gain (1 = unchanged), like juce::Decibels::decibelsToGain:
        at or below minusInfinityDb the gain is 0. */
    forcedinline float decibelsToGain (float decibels, float minusInfinityDb = -100.0f) noexcept
    {
        return decibels > minusInfinityDb ? exp2 (std::min (decibels, maxDecibels) * octavesPerDecibel) : 0.0f;
    }

    //==========================================================================
    /** Four numbers, worked on together. Every operation gives exactly what the
        same operation on one number at a time would (including min and max,
        which match std::min and std::max). */
    struct Vec4
    {
       #if EASY_PLUGINS_SSE2
        __m128 v;

        static forcedinline Vec4 load (const float* source) noexcept { return { _mm_loadu_ps (source) }; }
        static forcedinline Vec4 broadcast (float x) noexcept         { return { _mm_set1_ps (x) }; }
        forcedinline void store (float* destination) const noexcept   { _mm_storeu_ps (destination, v); }

        friend forcedinline Vec4 operator+ (Vec4 a, Vec4 b) noexcept { return { _mm_add_ps (a.v, b.v) }; }
        friend forcedinline Vec4 operator- (Vec4 a, Vec4 b) noexcept { return { _mm_sub_ps (a.v, b.v) }; }
        friend forcedinline Vec4 operator* (Vec4 a, Vec4 b) noexcept { return { _mm_mul_ps (a.v, b.v) }; }
        friend forcedinline Vec4 operator/ (Vec4 a, Vec4 b) noexcept { return { _mm_div_ps (a.v, b.v) }; }
        friend forcedinline Vec4 operator- (Vec4 a) noexcept         { return { _mm_xor_ps (a.v, _mm_set1_ps (-0.0f)) }; }

        /** Like std::min (a, b) and std::max (a, b), lane by lane. */
        friend forcedinline Vec4 min (Vec4 a, Vec4 b) noexcept { return { _mm_min_ps (b.v, a.v) }; }
        friend forcedinline Vec4 max (Vec4 a, Vec4 b) noexcept { return { _mm_max_ps (b.v, a.v) }; }
        friend forcedinline Vec4 sqrt (Vec4 a) noexcept         { return { _mm_sqrt_ps (a.v) }; }
        friend forcedinline Vec4 abs (Vec4 a) noexcept          { return { _mm_andnot_ps (_mm_set1_ps (-0.0f), a.v) }; }

        /** Comparisons give a mask: all bits set where true. */
        friend forcedinline Vec4 operator<  (Vec4 a, Vec4 b) noexcept { return { _mm_cmplt_ps (a.v, b.v) }; }
        friend forcedinline Vec4 operator<= (Vec4 a, Vec4 b) noexcept { return { _mm_cmple_ps (a.v, b.v) }; }
        friend forcedinline Vec4 operator>  (Vec4 a, Vec4 b) noexcept { return { _mm_cmpgt_ps (a.v, b.v) }; }
        friend forcedinline Vec4 operator>= (Vec4 a, Vec4 b) noexcept { return { _mm_cmpge_ps (a.v, b.v) }; }

        /** Where mask is true, ifTrue; elsewhere ifFalse. */
        static forcedinline Vec4 select (Vec4 mask, Vec4 ifTrue, Vec4 ifFalse) noexcept
        {
            return { _mm_or_ps (_mm_and_ps (mask.v, ifTrue.v), _mm_andnot_ps (mask.v, ifFalse.v)) };
        }
       #else
        float v[4];

        static forcedinline Vec4 load (const float* source) noexcept { return { { source[0], source[1], source[2], source[3] } }; }
        static forcedinline Vec4 broadcast (float x) noexcept         { return { { x, x, x, x } }; }
        forcedinline void store (float* destination) const noexcept   { for (int i = 0; i < 4; ++i) destination[i] = v[i]; }

        template <typename Function>
        static forcedinline Vec4 each (Vec4 a, Vec4 b, Function f) noexcept { return { { f (a.v[0], b.v[0]), f (a.v[1], b.v[1]), f (a.v[2], b.v[2]), f (a.v[3], b.v[3]) } }; }

        static forcedinline float maskFor (bool condition) noexcept { return Detail::floatFrom (condition ? -1 : 0); }

        friend forcedinline Vec4 operator+ (Vec4 a, Vec4 b) noexcept { return each (a, b, [] (float x, float y) { return x + y; }); }
        friend forcedinline Vec4 operator- (Vec4 a, Vec4 b) noexcept { return each (a, b, [] (float x, float y) { return x - y; }); }
        friend forcedinline Vec4 operator* (Vec4 a, Vec4 b) noexcept { return each (a, b, [] (float x, float y) { return x * y; }); }
        friend forcedinline Vec4 operator/ (Vec4 a, Vec4 b) noexcept { return each (a, b, [] (float x, float y) { return x / y; }); }
        friend forcedinline Vec4 operator- (Vec4 a) noexcept         { return each (a, a, [] (float x, float) { return -x; }); }

        friend forcedinline Vec4 min (Vec4 a, Vec4 b) noexcept { return each (a, b, [] (float x, float y) { return std::min (x, y); }); }
        friend forcedinline Vec4 max (Vec4 a, Vec4 b) noexcept { return each (a, b, [] (float x, float y) { return std::max (x, y); }); }
        friend forcedinline Vec4 sqrt (Vec4 a) noexcept         { return each (a, a, [] (float x, float) { return std::sqrt (x); }); }
        friend forcedinline Vec4 abs (Vec4 a) noexcept          { return each (a, a, [] (float x, float) { return std::abs (x); }); }

        friend forcedinline Vec4 operator<  (Vec4 a, Vec4 b) noexcept { return each (a, b, [] (float x, float y) { return maskFor (x <  y); }); }
        friend forcedinline Vec4 operator<= (Vec4 a, Vec4 b) noexcept { return each (a, b, [] (float x, float y) { return maskFor (x <= y); }); }
        friend forcedinline Vec4 operator>  (Vec4 a, Vec4 b) noexcept { return each (a, b, [] (float x, float y) { return maskFor (x >  y); }); }
        friend forcedinline Vec4 operator>= (Vec4 a, Vec4 b) noexcept { return each (a, b, [] (float x, float y) { return maskFor (x >= y); }); }

        static forcedinline Vec4 select (Vec4 mask, Vec4 ifTrue, Vec4 ifFalse) noexcept
        {
            Vec4 r;
            for (int i = 0; i < 4; ++i)
                r.v[i] = Detail::bitsOf (mask.v[i]) != 0 ? ifTrue.v[i] : ifFalse.v[i];
            return r;
        }
       #endif

        /** Like juce::jlimit (lowest, highest, x), lane by lane. */
        static forcedinline Vec4 limit (Vec4 lowest, Vec4 highest, Vec4 x) noexcept
        {
            return select (x < lowest, lowest, select (highest < x, highest, x));
        }
    };

    /** log2, exp2, gainToDecibels and decibelsToGain for four numbers at once
        (exactly the same answers as the versions above). */
    forcedinline Vec4 log2 (Vec4 x) noexcept
    {
       #if EASY_PLUGINS_SSE2
        using namespace Detail;
        const __m128i bits = _mm_castps_si128 (x.v);
        const __m128i e    = _mm_srai_epi32 (_mm_sub_epi32 (bits, _mm_set1_epi32 (sqrtHalfBits)), 23);
        const Vec4 t = Vec4 { _mm_castsi128_ps (_mm_sub_epi32 (bits, _mm_slli_epi32 (e, 23))) } - Vec4::broadcast (1.0f);

        Vec4 p = Vec4::broadcast (log2Fit[6]);
        p = p * t + Vec4::broadcast (log2Fit[5]);
        p = p * t + Vec4::broadcast (log2Fit[4]);
        p = p * t + Vec4::broadcast (log2Fit[3]);
        p = p * t + Vec4::broadcast (log2Fit[2]);
        p = p * t + Vec4::broadcast (log2Fit[1]);
        p = p * t + Vec4::broadcast (log2Fit[0]);

        return Vec4 { _mm_cvtepi32_ps (e) } + p * t;
       #else
        return { { log2 (x.v[0]), log2 (x.v[1]), log2 (x.v[2]), log2 (x.v[3]) } };
       #endif
    }

    forcedinline Vec4 exp2 (Vec4 x) noexcept
    {
       #if EASY_PLUGINS_SSE2
        using namespace Detail;
        const Vec4 shift   = Vec4::broadcast (roundingShift);
        const Vec4 shifted = x + shift;
        const __m128i k    = _mm_sub_epi32 (_mm_castps_si128 (shifted.v), _mm_castps_si128 (shift.v));
        const Vec4 f       = x - (shifted - shift);

        Vec4 q = Vec4::broadcast (exp2Fit[4]);
        q = q * f + Vec4::broadcast (exp2Fit[3]);
        q = q * f + Vec4::broadcast (exp2Fit[2]);
        q = q * f + Vec4::broadcast (exp2Fit[1]);
        q = q * f + Vec4::broadcast (exp2Fit[0]);
        q = q * f + Vec4::broadcast (1.0f);

        return { _mm_castsi128_ps (_mm_add_epi32 (_mm_castps_si128 (q.v), _mm_slli_epi32 (k, 23))) };
       #else
        return { { exp2 (x.v[0]), exp2 (x.v[1]), exp2 (x.v[2]), exp2 (x.v[3]) } };
       #endif
    }

    forcedinline Vec4 gainToDecibels (Vec4 gain, float minusInfinityDb) noexcept
    {
        const auto floor = Vec4::broadcast (minusInfinityDb);
        return Vec4::select (gain > Vec4::broadcast (0.0f),
                             max (floor, Vec4::broadcast (decibelsPerOctave) * log2 (gain)),
                             floor);
    }

    forcedinline Vec4 decibelsToGain (Vec4 decibels, float minusInfinityDb) noexcept
    {
        return Vec4::select (decibels > Vec4::broadcast (minusInfinityDb),
                             exp2 (min (decibels, Vec4::broadcast (maxDecibels)) * Vec4::broadcast (octavesPerDecibel)),
                             Vec4::broadcast (0.0f));
    }

    /** The largest of the four (like std::max of all four). */
    forcedinline float largestOf (Vec4 x) noexcept
    {
        alignas (16) float lanes[4];
        x.store (lanes);
        return std::max (std::max (lanes[0], lanes[1]), std::max (lanes[2], lanes[3]));
    }

    /** A knob's value at each sample of a run of samples. While it's gliding
        to a new setting, that's one value per sample (kept in the space it's
        given); the rest of the time it's just the one, steady value, which is
        quicker (nothing needs storing at all). */
    class KnobRun
    {
    public:
        template <typename SmoothedValue>
        KnobRun (SmoothedValue& smoothed, float* space, int numSamples) noexcept
            : values (space), steady (smoothed.getTargetValue()), gliding (smoothed.isSmoothing())
        {
            if (gliding)
                for (int i = 0; i < numSamples; ++i)
                    space[i] = smoothed.getNextValue();
        }

        /** The values at samples i to i + 3. */
        forcedinline Vec4 at (int i) const noexcept   { return gliding ? Vec4::load (values + i) : Vec4::broadcast (steady); }

    private:
        const float* values;
        float steady;
        bool gliding;
    };

    /** How many slots to work through so that numItems are covered four at a time. */
    constexpr int roundUpToFour (int numItems) noexcept   { return (numItems + 3) & ~3; }

    //==========================================================================
    /** base to the power n for a fixed base, remembered for the last n asked
        for (blocks of audio usually all have the same length, so it's rarely
        worked out more than once). */
    class RepeatedPower
    {
    public:
        void setBase (double newBase) noexcept
        {
            if (newBase < base || newBase > base)
            {
                base = newBase;
                lastExponent = -1;
            }
        }

        double operator() (int exponent) noexcept
        {
            if (exponent != lastExponent)
            {
                lastExponent = exponent;
                lastResult = std::pow (base, (double) exponent);
            }

            return lastResult;
        }

    private:
        double base = 1.0, lastResult = 1.0;
        int lastExponent = 0;
    };

    //==========================================================================
    /** True if every sample in the buffer is exactly zero: digital silence, as
        DAWs send for an empty stretch of track or while stopped. */
    inline bool isSilent (const juce::AudioBuffer<float>& buffer) noexcept
    {
        const int numSamples = buffer.getNumSamples();

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const float* samples = buffer.getReadPointer (ch);
            int i = 0;

           #if EASY_PLUGINS_SSE2
            const __m128 zero = _mm_setzero_ps();

            for (; i + 8 <= numSamples; i += 8)
            {
                const __m128 notZero = _mm_or_ps (_mm_cmpneq_ps (_mm_loadu_ps (samples + i), zero),
                                                  _mm_cmpneq_ps (_mm_loadu_ps (samples + i + 4), zero));
                if (_mm_movemask_ps (notZero) != 0)
                    return false;
            }
           #endif

            JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal")   // (exactly zero is the point)
            for (; i < numSamples; ++i)
                if (samples[i] != 0.0f)
                    return false;
            JUCE_END_IGNORE_WARNINGS_GCC_LIKE
        }

        return true;
    }
}
