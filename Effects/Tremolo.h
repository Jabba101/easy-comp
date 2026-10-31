#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

//==============================================================================
/**
    Easy Tremolo's sound: the volume pulsing (TREMOLO), or the sound moving
    from side to side (AUTO-PAN), at a RATE in Hz or locked to the song's
    tempo.

      - SHAPE: how the volume moves.
          SINE      smooth, like a vintage amp's tremolo
          TRIANGLE  a steadier rise and fall
          SQUARE    chopped on and off (the edges softened just enough not
                    to click) - the trance gate / stutter sound
          PUMP      dips on every beat and swells back: the "pumping"
                    sidechain sound, without a sidechain
      - DEPTH: how far the volume dips (or how far it swings, panning).
        At 100% a SQUARE cuts right out.
      - With SYNC on TEMPO the movement is locked to the song's beat (while
        the song plays, it follows the DAW's position, so it always lands
        on the beat). With SYNC OFF, RATE is free, in Hz.

    At its loudest point the sound is untouched: tremolo only takes level
    away. No delay added.
*/
class Tremolo
{
public:
    enum class Shape { sine = 0, triangle = 1, square = 2, pump = 3 };
    enum class Mode  { tremolo = 0, autoPan = 1 };

    static constexpr float minRateHz = 0.1f, maxRateHz = 20.0f;

    struct Settings
    {
        double cycleBeats = 0.5;     // with sync: one cycle, in quarter notes
        bool   synced     = true;
        float  rateHz     = 4.0f;    // without sync
        float  depth      = 0.5f;    // 0 .. 1
        Shape  shape      = Shape::sine;
        Mode   mode       = Mode::tremolo;
        bool   bypassed   = false;
    };

    void prepare (double newSampleRate, const Settings& initial)
    {
        sampleRate = newSampleRate;
        depth.reset (sampleRate, 0.03);
        bypassFade.reset (sampleRate, 0.02);
        panFade.reset (sampleRate, 0.05);
        phase = 0.0;
        setSettings (initial, true);
    }

    void setSettings (const Settings& s, bool jump = false)
    {
        settings = s;
        const auto set = [jump] (juce::SmoothedValue<float>& v, float target)
        {
            if (jump) v.setCurrentAndTargetValue (target);
            else      v.setTargetValue (target);
        };
        set (depth, juce::jlimit (0.0f, 1.0f, s.depth));
        set (bypassFade, s.bypassed ? 1.0f : 0.0f);
        set (panFade, s.mode == Mode::autoPan ? 1.0f : 0.0f);
    }

    /** songBeats: where the song is (in quarter notes) at the start of this block,
        if the DAW is playing and says; otherwise < 0 (the movement runs freely). */
    void process (juce::AudioBuffer<float>& buffer, double bpm, double songBeats) noexcept
    {
        const int n = buffer.getNumSamples();
        const int channels = juce::jmin (2, buffer.getNumChannels());
        if (channels == 0)
            return;

        // How far through a cycle each sample moves
        double cyclesPerSample;
        if (settings.synced)
        {
            const double beats = juce::jmax (1.0 / 64.0, settings.cycleBeats);
            cyclesPerSample = juce::jlimit (20.0, 999.0, bpm) / 60.0 / beats / sampleRate;
            if (songBeats >= 0.0)
                phase = std::fmod (songBeats / beats, 1.0);   // (locked to the beat)
        }
        else
        {
            cyclesPerSample = juce::jlimit ((double) minRateHz, (double) maxRateHz, (double) settings.rateHz) / sampleRate;
        }

        // The SQUARE / PUMP edges: at least 2 ms, so they never click
        const float edge = (float) juce::jmax (0.02, 0.002 * cyclesPerSample * sampleRate);

        float* left = buffer.getWritePointer (0);
        float* right = channels > 1 ? buffer.getWritePointer (1) : nullptr;
        float lowest = 1.0f;

        for (int i = 0; i < n; ++i)
        {
            const float u = shapeAt ((float) phase, settings.shape, edge);   // 1 = loudest
            phase += cyclesPerSample;
            if (phase >= 1.0) phase -= 1.0;

            const float d = depth.getNextValue();
            const float pan = panFade.getNextValue();
            const float bypass = bypassFade.getNextValue();

            // TREMOLO: both sides dip together. AUTO-PAN: one side dips while the other stays.
            const float trem = 1.0f - d * (1.0f - u);
            const float swing = (2.0f * u - 1.0f) * d;   // -1 (left) .. +1 (right)
            const float panL = juce::jmin (1.0f, 1.0f - swing), panR = juce::jmin (1.0f, 1.0f + swing);
            const float gL = channels > 1 ? trem + (panL - trem) * pan : trem;
            const float gR = trem + (panR - trem) * pan;

            left[i] *= gL + (1.0f - gL) * bypass;
            if (right != nullptr)
                right[i] *= gR + (1.0f - gR) * bypass;

            lowest = juce::jmin (lowest, juce::jmin (gL, gR));
        }

        position = (float) phase;
        dip = 1.0f - lowest;
    }

    /** Where in its cycle the movement is (0..1), and how far it dipped in the last block, for the window. */
    float getPosition() const noexcept   { return position; }
    float getDip() const noexcept        { return dip; }

    /** The shape itself: 1 = full volume, 0 = the deepest dip. phase 0 = on the beat. */
    static float shapeAt (float p, Shape shape, float edge = 0.03f) noexcept
    {
        const auto smooth = [] (float a, float b, float x)
        {
            const float t = juce::jlimit (0.0f, 1.0f, (x - a) / (b - a));
            return t * t * (3.0f - 2.0f * t);
        };

        switch (shape)
        {
            case Shape::triangle: return 1.0f - 2.0f * std::abs (p < 0.5f ? p : p - 1.0f);   // loudest on the beat
            case Shape::square:   return p < 0.5f ? 1.0f - smooth (0.5f - edge, 0.5f, p)   // on for the first half, off for the second
                                                  : smooth (1.0f - edge, 1.0f, p);
            case Shape::pump:     return p > 1.0f - edge ? 1.0f - smooth (1.0f - edge, 1.0f, p) : smooth (0.0f, 0.7f, p);   // dips on the beat
            case Shape::sine:
            default:              return 0.5f + 0.5f * std::cos (juce::MathConstants<float>::twoPi * p);
        }
    }

private:
    double sampleRate = 48000.0, phase = 0.0;
    Settings settings;
    juce::SmoothedValue<float> depth, bypassFade, panFade;
    float position = 0.0f, dip = 0.0f;
};
