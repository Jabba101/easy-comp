#pragma once

#include "PresetBrowser.h"

//==============================================================================
/*
    Parameter IDs: the names the plugin uses internally, and that your DAW
    stores in saved projects. Don't rename these once you've used the plugin
    in a project, or that project won't find its settings again.
*/
namespace AmpIDs
{
    inline constexpr const char* drive   = "drive";
    inline constexpr const char* tone    = "tone";
    inline constexpr const char* tight   = "tight";
    inline constexpr const char* mix     = "mix";
    inline constexpr const char* quality = "quality";   // OVERSAMPLING: Auto / Off / 4x / 8x
}

//==============================================================================
/*
    The presets.

    How hard an amp is driven depends on how loud the track going into it
    is. These assume a track that peaks somewhere around -12 to -6 dBFS
    (a normal recording level). If yours is much quieter or louder, move
    DRIVE until it sounds right - AUTO-GAIN keeps the volume steady while
    you do.

    TONE goes from -100% (darker) to +100% (brighter); 0 = unchanged.
    TIGHT at 20 Hz means off.
*/
struct AmpPreset
{
    const char* category;
    const char* name;
    float drivePercent;   //    0 .. 100
    float tonePercent;    // -100 .. +100
    float tightHz;        //   20 (off) .. 400
    float mixPercent;     //    0 .. 100
    const char* tip;
};

inline const AmpPreset ampPresets[] =
{
    //  category, name                                   drive   tone  tight    mix

    { "", "Default settings",                              0.0f,   0.0f, 20.0f, 100.0f,
      "Clean: changes nothing yet. Turn DRIVE up - a little for valve warmth, halfway for a rough edge, all the way for a growl." },

    // ---- Bass ------------------------------------------------------------------
    { "Bass", "Bass - Valve Warmth",                       25.0f,   0.0f, 20.0f, 100.0f,
      "Rounds and thickens a DI bass the way a valve DI or amp does. You feel it more than hear it." },
    { "Bass", "Bass - Rock Grit",                          55.0f,   5.0f, 80.0f, 100.0f,
      "A rough edge that helps the bass cut through guitars on small speakers, while TIGHT keeps the lows clean and solid." },
    { "Bass", "Bass - Overdriven Growl",                   85.0f,  15.0f, 20.0f, 100.0f,
      "A cranked valve bass amp: the whole bass growls and snarls, the way 80s speed-metal and rock & roll bass does." },
    { "Bass", "Bass - Growl Under Clean Lows",             80.0f,  10.0f,110.0f, 100.0f,
      "Big growl in the mids, but the low end stays clean and tight underneath. Great for punk and modern rock." },

    // ---- Guitars ---------------------------------------------------------------
    { "Guitars", "Guitar - Edge of Breakup",               45.0f,   0.0f, 60.0f, 100.0f,
      "On a clean DI or amp guitar: just breaking up when you dig in, clean when you play softly. Blues and classic rock." },
    { "Guitars", "Guitar - Classic Rock Crunch",           70.0f,   5.0f, 90.0f, 100.0f,
      "The big crunchy rhythm sound of 70s and 80s hard rock: chords stay clear, power chords roar." },
    { "Guitars", "Guitar - Hot Lead",                      90.0f,  10.0f,120.0f, 100.0f,
      "More gain for solos and leads: notes sustain and sing. Not metal-heavy - a cranked old valve amp." },
    { "Guitars", "Guitar - Fatten a Distorted Track",      35.0f,  -5.0f, 90.0f, 100.0f,
      "On a guitar that's already distorted: a little extra thickness and warmth without more fizz." },

    // ---- Drums -----------------------------------------------------------------
    { "Drums", "Drum Bus - Valve Warmth",                  20.0f,   0.0f, 20.0f, 100.0f,
      "On the drum bus: the kit gets a little fatter and rounder, like it went through a valve desk." },
    { "Drums", "Drum Bus - Parallel Dirt",                 75.0f,   5.0f, 70.0f,  30.0f,
      "Distorted drums blended under the clean kit (MIX 30%): more energy and aggression, the hits stay clean on top." },
    { "Drums", "Snare - Dirty Crack",                      60.0f,  10.0f, 20.0f,  60.0f,
      "A gritty, aggressive edge on the snare. Lower MIX for less, raise it for an all-out trashy snare." },
    { "Drums", "Room Mics - Crushed Valve",                85.0f,   0.0f, 20.0f, 100.0f,
      "Drives the room mics into a big, roaring wash. Blend them in under the close mics." },

    // ---- Synths ----------------------------------------------------------------
    { "Synths", "Synth Bass - Grit",                       60.0f,   5.0f, 90.0f, 100.0f,
      "Adds bite and harmonics to a synth bass so it's heard on small speakers, with clean, solid lows. Synthwave and darksynth." },
    { "Synths", "Synth Lead - Overdrive",                  80.0f,   0.0f, 20.0f, 100.0f,
      "Pushes a synth lead into valve overdrive for a thick, aggressive tone. Cyberpunk and darksynth." },
    { "Synths", "Synth Pad - Warmth",                      25.0f,  -5.0f, 20.0f, 100.0f,
      "Takes the digital edge off pads and keys: warmer and rounder, like an old analog synth through a valve preamp." },

    // ---- Vocals ----------------------------------------------------------------
    { "Vocals", "Vocal - Valve Warmth",                    20.0f,   0.0f, 20.0f, 100.0f,
      "A touch of valve-preamp warmth that helps a vocal sit forward. You should miss it when you bypass it, not hear it." },
    { "Vocals", "Vocal - Distorted Shout",                 75.0f,  10.0f,150.0f,  50.0f,
      "Gritty, distorted shouts and backing chants, blended with the clean vocal. Punk and rock." },

    // ---- The whole mix ---------------------------------------------------------
    { "Mix Bus", "Mix Bus - Gentle Valve Glue",            12.0f,   0.0f, 20.0f, 100.0f,
      "On the whole mix: a little valve warmth that ties everything together. Keep DRIVE low - a little goes a long way here." },
};

//==============================================================================
inline std::vector<Preset> makeAmpPresets()
{
    std::vector<Preset> presets;

    for (auto& p : ampPresets)
        presets.push_back ({ p.category, p.name, p.tip,
                             { { AmpIDs::drive, p.drivePercent }, { AmpIDs::tone, p.tonePercent },
                               { AmpIDs::tight, p.tightHz },      { AmpIDs::mix,  p.mixPercent } } });

    return presets;
}

/** The settings presets store (everything except BYPASS, AUTO-GAIN and OVERSAMPLING). */
inline juce::StringArray ampPresetIDs()
{
    return { AmpIDs::drive, AmpIDs::tone, AmpIDs::tight, AmpIDs::mix };
}
