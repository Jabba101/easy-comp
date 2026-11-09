#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>

//==============================================================================
/**
    DESK: the sound of a mixing console's channel - the colour each track
    picks up going through a classic desk, in the spirit of the originals
    (built from how each is known to behave, not a measured clone).

    Real desks are subtle, so this is: at the normal level (0 VU, which on
    the strip is -18 dBFS - where LEARN puts every track) the added
    harmonics are a fraction of a percent to about one percent; DRIVE
    pushes the desk harder (HOT +6, DRIVEN +12) for more, without making
    the track louder. What each one does:

      - Harmonics. Valves add mostly the 2nd (sweet, thick); transistors
        and transformers mostly the 3rd (edge, density). Both grow the
        harder the desk is hit, gradually.
      - The iron. Transformers saturate on the deep lows first: the bass
        thickens and rounds off before anything else does.
      - Sag. Valve desks (and the TG12345) give a little under loud
        passages - a soft, slow evening-out.
      - The tone of the circuit: small shelves and bumps, a softer or
        sweeter top, a tighter or rounder bottom - each desk's own.

    OFF passes the sound through untouched (bit for bit). Switching desks
    dips for 20 ms (no click). No added delay.
*/
class ConsoleColour
{
public:
    struct Desk
    {
        const char* name;        // for the menus
        const char* shortName;   // for the strip's INPUT column
        float even, odd;         // 2nd / 3rd harmonic strength (see process)
        float iron, ironHz;      // the transformer's low-end saturation, and below where
        float sag;               // soft evening-out under loud passages
        float lowShelfHz, lowShelfDb, bumpHz, bumpDb, bumpQ, presenceHz, presenceDb, presenceQ, highShelfHz, highShelfDb;
        float lowPassHz, highPassHz;   // gentle (6 dB / octave) top roll-off / low tightening; 0 = none
    };

    enum { off = 0, british, american, cleanBritish, ua610, redd, broadcastValve, helios, trident, tg12345, harrison, numDesks };

    static const Desk& desk (int d)
    {
        static const Desk desks[numDesks] {
            { "None (clean)",                            "",         0.0f,  0.0f,  0.0f, 100.0f, 0.0f,   0, 0,      0, 0, 1,      0, 0, 1,       0, 0,       0, 0 },
            { "British (Neve style)",                    "BRITISH",  0.04f, 0.25f, 0.5f, 100.0f, 0.0f,   100, 0.6f, 0, 0, 1,      0, 0, 1,       16000, -0.4f, 0, 0 },
            { "American (API style)",                    "AMERICAN", 0.02f, 0.35f, 0.3f, 100.0f, 0.0f,   0, 0,      200, 0.4f, 0.8f, 2500, 0.6f, 0.7f, 12000, 0.2f, 0, 0 },
            { "Clean British (SSL style)",               "SSL",      0.01f, 0.12f, 0.1f, 80.0f,  0.0f,   0, 0,      0, 0, 1,      0, 0, 1,       12000, 0.3f, 0, 0 },
            { "Valve: UA 610 style",                     "610",      0.08f, 0.08f, 0.4f, 90.0f,  0.6f,   0, 0,      180, 0.8f, 0.7f, 0, 0, 1,       10000, -0.6f, 22000, 0 },
            { "Valve: EMI REDD style",                   "REDD",     0.07f, 0.12f, 0.6f, 100.0f, 0.4f,   0, 0,      220, 1.0f, 0.6f, 0, 0, 1,       7000, -1.2f, 18000, 0 },
            { "Valve: German broadcast (V72 / V76 style)", "V72",    0.06f, 0.20f, 0.5f, 100.0f, 0.3f,   0, 0,      0, 0, 1,      1500, 1.0f, 0.6f, 10000, -0.5f, 0, 30 },
            { "Helios style",                            "HELIOS",   0.05f, 0.40f, 0.7f, 110.0f, 0.0f,   60, -0.3f, 0, 0, 1,      1600, 1.0f, 0.8f, 10000, 0.5f, 0, 40 },
            { "Trident A-Range style",                   "TRIDENT",  0.06f, 0.18f, 0.4f, 100.0f, 0.0f,   80, 0.8f,  0, 0, 1,      0, 0, 1,       12000, 0.7f, 0, 0 },
            { "EMI TG12345 style",                       "TG12345",  0.04f, 0.15f, 0.3f, 100.0f, 0.9f,   0, 0,      300, 0.3f, 0.7f, 0, 0, 1,       10000, -0.5f, 0, 0 },
            { "Harrison 32 style",                       "HARRISON", 0.015f, 0.08f, 0.1f, 80.0f, 0.0f,   0, 0,      400, -0.3f, 0.7f, 0, 0, 1,      16000, 0.5f, 0, 0 },
        };
        return desks[juce::jlimit (0, numDesks - 1, d)];
    }

    static constexpr int numDrives = 4;
    static const char* driveName (int i)   { static const char* n[numDrives] { "GENTLE", "NORMAL", "HOT", "DRIVEN" }; return n[juce::jlimit (0, numDrives - 1, i)]; }
    static float driveDb (int i)           { static const float d[numDrives] { -6.0f, 0.0f, 6.0f, 12.0f }; return d[juce::jlimit (0, numDrives - 1, i)]; }

    void prepare (double sampleRate)
    {
        rate = sampleRate;
        fadeStep = (float) (1.0 / (0.01 * sampleRate));
        shown = wanted.load();
        drive = wantedDrive.load();
        configure();
        reset();
    }

    void reset() noexcept
    {
        for (auto& c : channels) c = {};
        fade = 1.0f;
    }

    /** From any thread: the desk and how hard it's driven (the change happens with a 20 ms dip). */
    void set (int deskIndex, int driveIndex) noexcept   { wanted.store (juce::jlimit (0, numDesks - 1, deskIndex)); wantedDrive.store (juce::jlimit (0, numDrives - 1, driveIndex)); }
    int getDesk() const noexcept   { return shown; }

    void process (float* const* audio, int numChannels, int numSamples) noexcept
    {
        const bool changing = wanted.load() != shown || wantedDrive.load() != drive;
        if (shown == off && ! changing && fade >= 1.0f) return;   // (OFF: untouched)

        for (int i = 0; i < numSamples; ++i)
        {
            // A change: fade out, swap, fade back in
            if ((wanted.load() != shown || wantedDrive.load() != drive) && fade > 0.0f)
                fade = juce::jmax (0.0f, fade - fadeStep);
            else if (fade <= 0.0f && (wanted.load() != shown || wantedDrive.load() != drive))
            {
                shown = wanted.load(); drive = wantedDrive.load();
                configure();
                for (auto& c : channels) c = {};
            }
            else if (fade < 1.0f)
                fade = juce::jmin (1.0f, fade + fadeStep);

            for (int ch = 0; ch < juce::jmin (numChannels, 2); ++ch)
            {
                float& x = audio[ch][i];
                const float coloured = shown == off ? x : colour (channels[(size_t) ch], x);
                x = shown == off ? x * fade : coloured * fade;
            }
        }
    }

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float run (double x, double& z1, double& z2) const noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
    };
    struct Channel
    {
        std::array<double, 2> low {}, bump {}, presence {}, high {};
        double ironState = 0.0, lowPassState = 0.0, highPassIn = 0.0, highPassOut = 0.0, dcIn = 0.0, dcOut = 0.0;
        float envelope = 0.0f;
    };

    static Biquad shelf (double rate, double hz, double db, bool highShelf)
    {
        Biquad b;
        if (hz <= 0.0 || std::abs (db) < 1.0e-4 || hz >= rate * 0.49) return b;
        const double A = std::pow (10.0, db / 40.0), w = juce::MathConstants<double>::twoPi * hz / rate;
        const double cs = std::cos (w), sn = std::sin (w), alpha = sn / 2.0 * std::sqrt (2.0);   // (S = 1)
        const double sq = 2.0 * std::sqrt (A) * alpha;
        double b0, b1, b2, a0, a1, a2;
        if (highShelf)
        {
            b0 = A * ((A + 1) + (A - 1) * cs + sq); b1 = -2 * A * ((A - 1) + (A + 1) * cs); b2 = A * ((A + 1) + (A - 1) * cs - sq);
            a0 = (A + 1) - (A - 1) * cs + sq;       a1 = 2 * ((A - 1) - (A + 1) * cs);      a2 = (A + 1) - (A - 1) * cs - sq;
        }
        else
        {
            b0 = A * ((A + 1) - (A - 1) * cs + sq); b1 = 2 * A * ((A - 1) - (A + 1) * cs);  b2 = A * ((A + 1) - (A - 1) * cs - sq);
            a0 = (A + 1) + (A - 1) * cs + sq;       a1 = -2 * ((A - 1) + (A + 1) * cs);     a2 = (A + 1) + (A - 1) * cs - sq;
        }
        b.b0 = b0 / a0; b.b1 = b1 / a0; b.b2 = b2 / a0; b.a1 = a1 / a0; b.a2 = a2 / a0;
        return b;
    }

    static Biquad peak (double rate, double hz, double db, double q)
    {
        Biquad b;
        if (hz <= 0.0 || std::abs (db) < 1.0e-4 || hz >= rate * 0.49) return b;
        const double A = std::pow (10.0, db / 40.0), w = juce::MathConstants<double>::twoPi * hz / rate;
        const double alpha = std::sin (w) / (2.0 * q), cs = std::cos (w);
        const double a0 = 1 + alpha / A;
        b.b0 = (1 + alpha * A) / a0; b.b1 = -2 * cs / a0; b.b2 = (1 - alpha * A) / a0; b.a1 = -2 * cs / a0; b.a2 = (1 - alpha / A) / a0;
        return b;
    }

    void configure()
    {
        const auto& d = desk (shown);
        lowShelf = shelf (rate, d.lowShelfHz, d.lowShelfDb, false);
        bump = peak (rate, d.bumpHz, d.bumpDb, d.bumpQ);
        presence = peak (rate, d.presenceHz, d.presenceDb, d.presenceQ);
        highShelf = shelf (rate, d.highShelfHz, d.highShelfDb, true);
        lowPassCoeff = d.lowPassHz > 0.0f && d.lowPassHz < rate * 0.45 ? (float) std::exp (-juce::MathConstants<double>::twoPi * d.lowPassHz / rate) : 0.0f;
        highPassCoeff = d.highPassHz > 0.0f ? (float) std::exp (-juce::MathConstants<double>::twoPi * d.highPassHz / rate) : 0.0f;
        ironCoeff = (float) std::exp (-juce::MathConstants<double>::twoPi * d.ironHz / rate);
        dcCoeff = (float) std::exp (-juce::MathConstants<double>::twoPi * 6.0 / rate);
        oddK = std::sqrt (3.0f * juce::jmax (1.0e-6f, d.odd));
        driveGain = juce::Decibels::decibelsToGain (driveDb (drive));
        envAttack = (float) (1.0 - std::exp (-1.0 / (0.01 * rate)));
        envRelease = (float) (1.0 - std::exp (-1.0 / (0.15 * rate)));
    }

    float colour (Channel& c, float in) noexcept
    {
        const auto& d = desk (shown);
        // The desk's own scale: 0 VU (-18 dBFS) is about a fifth of the way to where it gives up
        float u = in * driveGain * unitsPerFull;

        // The iron: the deep lows saturate first
        c.ironState = (1.0 - ironCoeff) * u + ironCoeff * c.ironState;
        const float lows = (float) c.ironState;
        const float ironK = 1.6f;
        u += d.iron * (std::tanh (ironK * lows) / ironK - lows);

        // Sag: a little give under loud passages
        if (d.sag > 0.0f)
        {
            const float level = std::abs (u);
            c.envelope += (level > c.envelope ? envAttack : envRelease) * (level - c.envelope);
            u /= (1.0f + d.sag * c.envelope * c.envelope);
        }

        // Harmonics: the 3rd (a smooth, symmetric curve) and the 2nd (a lean to one side)
        const float t = std::tanh (oddK * u) / oddK;
        float y = t + d.even * (t * t) / (1.0f + t * t);

        // (the 2nd's lean adds a little DC: taken out)
        const double dcFree = y - c.dcIn + dcCoeff * c.dcOut;
        c.dcIn = y; c.dcOut = dcFree;
        y = (float) dcFree;

        // Back to the track's own level, then the circuit's tone
        double x = y / (driveGain * unitsPerFull);
        x = lowShelf.run (x, c.low[0], c.low[1]);
        x = bump.run (x, c.bump[0], c.bump[1]);
        x = presence.run (x, c.presence[0], c.presence[1]);
        x = highShelf.run (x, c.high[0], c.high[1]);
        if (lowPassCoeff > 0.0f) { c.lowPassState = (1.0 - lowPassCoeff) * x + lowPassCoeff * c.lowPassState; x = c.lowPassState; }
        if (highPassCoeff > 0.0f)
        {
            const double hp = highPassCoeff * (c.highPassOut + x - c.highPassIn);
            c.highPassIn = x; c.highPassOut = hp; x = hp;
        }
        return (float) x;
    }

    static constexpr float unitsPerFull = 1.4f;   // (0 VU = -18 dBFS: a sine's peak there is ~0.18 of the way to the desk's limit)

    double rate = 48000.0;
    std::atomic<int> wanted { off }, wantedDrive { 1 };
    int shown = off, drive = 1;
    float fade = 1.0f, fadeStep = 0.002f;
    Biquad lowShelf, bump, presence, highShelf;
    float lowPassCoeff = 0.0f, highPassCoeff = 0.0f, ironCoeff = 0.99f, dcCoeff = 0.999f, oddK = 1.0f, driveGain = 1.0f, envAttack = 0.01f, envRelease = 0.001f;
    std::array<Channel, 2> channels {};
};
