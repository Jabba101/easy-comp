#pragma once

#include "PresetBrowser.h"

//==============================================================================
/*
    Parameter IDs: the names the plugin uses internally, and that your DAW
    stores in saved projects. Don't rename these once you've used the plugin
    in a project, or that project won't find its settings again.
*/
namespace ClipIDs
{
    inline constexpr const char* push        = "push";
    inline constexpr const char* ceiling     = "ceiling";
    inline constexpr const char* softness    = "softness";
    inline constexpr const char* protectLows = "protectlows";
    inline constexpr const char* top         = "top";
    inline constexpr const char* bottom      = "bottom";
    inline constexpr const char* link        = "link";
    inline constexpr const char* bias        = "bias";
    inline constexpr const char* blend       = "blend";
    inline constexpr const char* quality     = "quality";    // OVERSAMPLING: Auto / Off / 4x / 8x
    inline constexpr const char* view        = "view";       // Simple / Advanced
}

//==============================================================================
/*
    The presets.

    How much a clipper does depends on how loud your track already is, so
    the presets set the character (CEILING, SOFTNESS, PROTECT LOWS and the
    ADVANCED settings) and a sensible PUSH. After choosing one, play the
    loudest part of the song and adjust PUSH until the SHAVED OFF meter
    shows about 1-3 dB on the loudest hits.

    PROTECT LOWS at 20 Hz means off. TOP and BOTTOM are dB below the
    ceiling (0 = at the ceiling). BIAS goes from -100% to +100%; 0 = even.
*/
struct ClipPreset
{
    const char* category;
    const char* name;
    float pushDb;           //   0 .. 24
    float ceilingDb;        // -24 .. 0
    float softnessPercent;  //   0 .. 100
    float protectLowsHz;    //  20 (off) .. 500
    float topDb;            // -12 .. 0
    float bottomDb;         // -12 .. 0
    float biasPercent;      // -100 .. +100
    float blendPercent;     //   0 .. 100
    const char* tip;
};

inline const ClipPreset clipPresets[] =
{
    //  category, name                                  push  ceiling soft  lows   top  bottom  bias  blend

    { "", "Default settings",                           0.0f,  0.0f,   0.0f, 20.0f, 0.0f, 0.0f,  0.0f, 100.0f,
      "Changes nothing yet. Play the loudest part of your song and turn PUSH up until SHAVED OFF shows 1-3 dB on the loudest hits." },

    // ---- Drums -----------------------------------------------------------------
    { "Drums", "Kick - Tight & Loud",                   4.0f, -1.0f,  15.0f, 20.0f, 0.0f, 0.0f,  0.0f, 100.0f,
      "Shaves the very top off each kick so it can sit louder in the mix. Adjust PUSH until the meter shows 1-3 dB." },
    { "Drums", "Kick - Keep the Boom",                  4.0f, -1.0f,   0.0f,100.0f, 0.0f, 0.0f,  0.0f, 100.0f,
      "Clips the click and the attack but leaves the low boom alone (PROTECT LOWS), so the kick gets louder without losing weight." },
    { "Drums", "Snare - Shave the Spikes",              4.0f, -1.0f,   0.0f, 20.0f, 0.0f, 0.0f,  0.0f, 100.0f,
      "A snare's first few milliseconds are far louder than the rest. Shaving them lets you push the snare up without it jumping out." },
    { "Drums", "Snare - Crunch",                        9.0f, -1.0f,   0.0f, 20.0f,-2.0f, 0.0f,  0.0f, 100.0f,
      "Hit hard on purpose: an audible crunch on every snare, for punk, garage rock and darksynth. TOP clips one side harder for more grit." },
    { "Drums", "Toms - Louder Fills",                   4.0f, -1.0f,  20.0f, 20.0f, 0.0f, 0.0f,  0.0f, 100.0f,
      "Tames the loudest tom hits so fills can sit up front without one hit poking out." },
    { "Drums", "Room Mics - Trashy",                   12.0f, -1.0f,  30.0f, 20.0f, 0.0f, 0.0f, 40.0f, 100.0f,
      "Smashes the room mics into a dirty, exciting roar. Blend it in under the close mics. BIAS makes it warmer and nastier." },

    // ---- The whole kit ---------------------------------------------------------
    { "Drum Bus", "Drum Bus - Glue the Peaks",          3.0f, -1.0f,  10.0f, 90.0f, 0.0f, 0.0f,  0.0f, 100.0f,
      "On the drum bus: catches the loudest hits so the kit holds together and sits louder, while the kick's low end stays clean." },
    { "Drum Bus", "Drum Bus - Smashed (Punk)",          9.0f, -1.0f,   0.0f, 20.0f, 0.0f, 0.0f,  0.0f, 100.0f,
      "Hard clipping on the whole kit: loud, raw and in your face, for punk and garage rock." },
    { "Drum Bus", "Drum Bus - Parallel Crush",         12.0f, -1.0f,   0.0f, 20.0f, 0.0f, 0.0f,  0.0f,  50.0f,
      "Crushes the kit and blends the original back in (BLEND 50%): the punch of the hits stays, the crushed copy adds size and energy." },

    // ---- Bass ------------------------------------------------------------------
    { "Bass", "Bass - Even Out the Peaks",              3.0f, -1.0f,  40.0f, 20.0f, 0.0f, 0.0f,  0.0f, 100.0f,
      "Rounds off the loudest notes and slaps so the bass sits at a steadier level. Faster than a compressor and adds no pumping." },
    { "Bass", "Bass - Growl",                           8.0f, -1.0f,  60.0f, 20.0f, 0.0f, 0.0f, 30.0f, 100.0f,
      "Warm, gritty saturation that helps a bass cut through on small speakers. For rock, punk and synth bass." },

    // ---- Guitars and synths ----------------------------------------------------
    { "Guitars & Synths", "Distorted Guitar - Tame the Spikes", 2.0f, -1.0f, 30.0f, 20.0f, 0.0f, 0.0f, 0.0f, 100.0f,
      "Catches the odd spiky chord so heavy guitars sit at an even level. Barely changes the tone." },
    { "Guitars & Synths", "Synth Lead - Bite",          6.0f, -1.0f,  50.0f, 20.0f, 0.0f, 0.0f, 25.0f, 100.0f,
      "Adds edge and harmonics to a synth lead so it cuts through. For synthwave and cyberpunk." },

    // ---- Vocals ----------------------------------------------------------------
    { "Vocals", "Vocal - Catch the Loud Words",         2.0f, -1.0f,  60.0f, 20.0f, 0.0f, 0.0f,  0.0f, 100.0f,
      "Gently rounds off the odd loud word or shout. Keep PUSH low on vocals: clipping a voice hard is very easy to hear." },

    // ---- The whole mix ---------------------------------------------------------
    { "Mix Bus & Master", "Master - Gentle Safety Net", 0.0f, -1.0f,  20.0f, 20.0f, 0.0f, 0.0f,  0.0f, 100.0f,
      "On the finished mix: catches the odd stray peak before it goes over -1 dB. You shouldn't hear it working." },
    { "Mix Bus & Master", "Master - Clean Loudness",    3.0f, -1.0f,  10.0f, 20.0f, 0.0f, 0.0f,  0.0f, 100.0f,
      "On the finished mix: a few dB louder with the peaks shaved cleanly. If you can hear it working, lower PUSH." },
    { "Mix Bus & Master", "Master - Loud Rock & Punk",  5.0f, -1.0f,   0.0f, 20.0f, 0.0f, 0.0f,  0.0f, 100.0f,
      "On the finished mix: loud and aggressive, the drums' peaks shaved hard. Compare with BYPASS at the same loudness." },
    { "Mix Bus & Master", "Master - Synthwave Shine",   4.0f, -1.0f,  30.0f, 90.0f, 0.0f, 0.0f,  0.0f, 100.0f,
      "On the finished mix: louder and brighter, with the bass and kick protected so the low end stays smooth and big." },
};

//==============================================================================
inline std::vector<Preset> makeClipPresets()
{
    std::vector<Preset> presets;

    for (auto& p : clipPresets)
        presets.push_back ({ p.category, p.name, p.tip,
                             { { ClipIDs::push,        p.pushDb },        { ClipIDs::ceiling, p.ceilingDb },
                               { ClipIDs::softness,    p.softnessPercent },
                               { ClipIDs::protectLows, p.protectLowsHz },
                               { ClipIDs::top,         p.topDb },         { ClipIDs::bottom,  p.bottomDb },
                               { ClipIDs::link,        std::abs (p.topDb - p.bottomDb) < 0.05f ? 1.0f : 0.0f },
                               { ClipIDs::bias,        p.biasPercent },   { ClipIDs::blend,   p.blendPercent } } });

    return presets;
}

/** The settings presets store (everything except BYPASS, OVERSAMPLING and the SIMPLE / ADVANCED view). */
inline juce::StringArray clipPresetIDs()
{
    return { ClipIDs::push, ClipIDs::ceiling, ClipIDs::softness, ClipIDs::protectLows,
             ClipIDs::top, ClipIDs::bottom, ClipIDs::link, ClipIDs::bias, ClipIDs::blend };
}

/** The ADVANCED settings (their own INIT resets just these). */
inline juce::StringArray clipAdvancedIDs()
{
    return { ClipIDs::top, ClipIDs::bottom, ClipIDs::link, ClipIDs::bias, ClipIDs::blend };
}
