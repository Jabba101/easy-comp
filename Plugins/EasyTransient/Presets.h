#pragma once

#include "PresetBrowser.h"

//==============================================================================
/*
    Parameter IDs: the names the plugin uses internally, and that your DAW
    stores in saved projects. Don't rename these once you've used the plugin
    in a project, or that project won't find its settings again.
*/
namespace TransientIDs
{
    inline constexpr const char* punch   = "punch";
    inline constexpr const char* sustain = "sustain";
    inline constexpr const char* output  = "output";
    inline constexpr const char* mix     = "mix";
}

//==============================================================================
/*
    The presets.

    PUNCH and SUSTAIN go from -100% to +100%; 0% changes nothing. They're
    starting points: every recording is different, so after choosing one,
    turn the knobs either way and listen.

    To add your own: copy a line, then change the category (which sub-menu
    it's in), the name, the numbers and the tip. (You can also save presets
    from the plugin's menu without touching any code.)
*/
struct TransientPreset
{
    const char* category;
    const char* name;
    float punchPercent;     // -100 .. +100
    float sustainPercent;   // -100 .. +100
    float outputDb;         //  -12 .. +12
    float mixPercent;       //    0 .. 100
    const char* tip;
};

inline const TransientPreset transientPresets[] =
{
    //  category, name                                         punch  sustain  output     mix

    { "", "Default settings",                                   0.0f,   0.0f,  0.0f, 100.0f,
      "Changes nothing yet. Turn PUNCH and SUSTAIN either way from the middle and listen." },

    // ---- Drums -----------------------------------------------------------------
    { "Drums", "Kick - More Click",                            35.0f,   0.0f,  0.0f, 100.0f,
      "Brings out the beater's attack so the kick cuts through the mix. More PUNCH = more click." },
    { "Drums", "Kick - Tighter",                               10.0f, -40.0f,  0.0f, 100.0f,
      "Shortens a boomy kick so it doesn't smear into the bass. Less SUSTAIN = shorter." },
    { "Drums", "Kick - Bigger Boom",                            0.0f,  30.0f,  0.0f, 100.0f,
      "Lets the kick ring out longer, for a deep, booming low end in hip-hop and R&B. Check it doesn't cover up the bass." },
    { "Drums", "Snare - More Crack",                           35.0f,   0.0f,  0.0f, 100.0f,
      "Brings out the stick hitting the head - great on a snare that sounds a bit flat." },
    { "Drums", "Snare - Less Ring",                            10.0f, -35.0f,  0.0f, 100.0f,
      "Tames ring and room on the snare for a drier, tighter hit." },
    { "Drums", "Snare - Fatter",                                0.0f,  35.0f,  0.0f, 100.0f,
      "More body and ring after each hit, for a big rock or ballad snare." },
    { "Drums", "Snare - Softer",                              -30.0f,   0.0f,  0.0f, 100.0f,
      "Rounds off a harsh, spiky snare - nice in jazz, ballads and acoustic songs." },
    { "Drums", "Toms - Less Ring",                             15.0f, -40.0f,  0.0f, 100.0f,
      "Stops toms ringing on between fills - often quicker than gating them." },
    { "Drums", "Toms - Bigger",                                10.0f,  30.0f,  0.0f, 100.0f,
      "Longer, bigger toms for huge fills." },
    { "Drums", "Hi-Hat - Crisper",                             30.0f, -15.0f,  0.0f, 100.0f,
      "Sharper, more defined hi-hat hits with less wash between them." },
    { "Drums", "Overheads - Less Room",                         0.0f, -30.0f,  0.0f, 100.0f,
      "Dries up a roomy kit: the hits stay, the room behind them drops back." },
    { "Drums", "Overheads - More Snap",                        25.0f,   0.0f,  0.0f, 100.0f,
      "Brings the hits forward across the whole kit." },
    { "Drums", "Room Mics - Bigger Room",                     -10.0f,  40.0f,  0.0f, 100.0f,
      "Pushes the room's echo up after each hit for a bigger space." },
    { "Drums", "Drum Bus - Punchier",                          25.0f,   0.0f,  0.0f, 100.0f,
      "Adds snap to the whole kit. Switch AUTO-GAIN on so you're comparing punch, not volume." },
    { "Drums", "Drum Bus - Tighter",                           15.0f, -25.0f,  0.0f, 100.0f,
      "Shorter tails and sharper hits, for tight, dry drums in rock, metal and modern pop." },
    { "Drums", "Drum Bus - Softer",                           -20.0f,  10.0f,  0.0f, 100.0f,
      "Takes the edge off spiky drums for a softer, more relaxed feel - jazz, ballads, acoustic." },
    { "Drums", "Percussion - Snappier",                        25.0f, -15.0f,  0.0f, 100.0f,
      "Crisper, more defined percussion that cuts through without being turned up." },

    // ---- Bass ------------------------------------------------------------------
    { "Bass", "Bass - More Definition",                        30.0f,   0.0f,  0.0f, 100.0f,
      "Brings out the start of each note so the bass line is clearer in a busy mix." },
    { "Bass", "Bass - Tighter",                                10.0f, -25.0f,  0.0f, 100.0f,
      "Shortens each note for a tighter groove that locks in with the kick." },
    { "Bass", "Bass - Softer Slaps",                          -30.0f,   0.0f,  0.0f, 100.0f,
      "Tames spiky slaps, pops and harsh picking." },
    { "Bass", "808 & Synth Bass - More Knock",                 40.0f,   0.0f,  0.0f, 100.0f,
      "Adds a click to the start of each note, so an 808 or synth bass is heard even on small speakers." },

    // ---- Guitars ---------------------------------------------------------------
    { "Guitars", "Acoustic Guitar - More Attack",              50.0f,   0.0f,  0.0f, 100.0f,
      "Clearer strums and picked notes that cut through the mix." },
    { "Guitars", "Acoustic Guitar - Softer Strums",           -50.0f,   0.0f,  0.0f, 100.0f,
      "Tames harsh, spiky strumming for a smoother sound." },
    { "Guitars", "Acoustic Guitar - Less Room",                 0.0f, -40.0f,  0.0f, 100.0f,
      "Dries up a guitar recorded in a roomy space." },
    { "Guitars", "Electric Guitar - Tighter Chugs",            15.0f, -30.0f,  0.0f, 100.0f,
      "Tighter, more defined palm-muted chugs for rock and metal." },
    { "Guitars", "Electric Guitar - More Ring",                 0.0f,  35.0f,  0.0f, 100.0f,
      "Lets clean chords and notes ring out longer." },
    { "Guitars", "Electric Guitar - Funk Snap",                30.0f, -15.0f,  0.0f, 100.0f,
      "Snappier, choppier funk and R&B rhythm parts." },

    // ---- Keys ------------------------------------------------------------------
    { "Keys", "Piano - More Definition",                       40.0f,   0.0f,  0.0f, 100.0f,
      "Clearer note starts, so the piano cuts through a busy mix." },
    { "Keys", "Piano - Softer Attack",                        -40.0f,   0.0f,  0.0f, 100.0f,
      "A gentler, rounder attack for ballads and quiet songs." },
    { "Keys", "Piano - Less Room",                              0.0f, -40.0f,  0.0f, 100.0f,
      "Dries up a piano recorded in a roomy space, or a pedal tail that's too long." },
    { "Keys", "Synth Pluck - Snappier",                        40.0f, -20.0f,  0.0f, 100.0f,
      "Sharper, tighter plucks and arpeggios." },

    // ---- Vocals ----------------------------------------------------------------
    { "Vocals", "Vocal - Less Room",                            0.0f, -20.0f,  0.0f, 100.0f,
      "Dries up a vocal recorded in an echoey room. Use small amounts - too much makes the words sound choppy." },
    { "Vocals", "Vocal - More Bite",                           30.0f,   0.0f,  0.0f, 100.0f,
      "A little more edge at the start of each word, for rap and rock vocals." },

    // ---- Strings & Brass -------------------------------------------------------
    { "Strings & Brass", "Brass - More Punch",                 40.0f,   0.0f,  0.0f, 100.0f,
      "Stabbier, punchier horn hits for funk, soul and pop." },
    { "Strings & Brass", "Strings - Softer Attack",           -40.0f,   0.0f,  0.0f, 100.0f,
      "Smooths scratchy bow attacks for a softer string sound." },

    // ---- Mix Bus ---------------------------------------------------------------
    { "Mix Bus", "Mix Bus - A Little More Punch",              15.0f,   0.0f,  0.0f, 100.0f,
      "A subtle lift to the hits across the whole mix. Keep it small - a little goes a long way here." },
    { "Mix Bus", "Mix Bus - A Little Tighter",                  5.0f, -15.0f,  0.0f, 100.0f,
      "Slightly shorter tails across the whole mix, for a tighter, more modern sound." },
};

//==============================================================================
inline std::vector<Preset> makeTransientPresets()
{
    std::vector<Preset> presets;

    for (auto& p : transientPresets)
        presets.push_back ({ p.category, p.name, p.tip,
                             { { TransientIDs::punch,  p.punchPercent }, { TransientIDs::sustain, p.sustainPercent },
                               { TransientIDs::output, p.outputDb },     { TransientIDs::mix,     p.mixPercent } } });

    return presets;
}

/** The settings presets store (everything except BYPASS and AUTO-GAIN). */
inline juce::StringArray transientPresetIDs()
{
    return { TransientIDs::punch, TransientIDs::sustain, TransientIDs::output, TransientIDs::mix };
}
