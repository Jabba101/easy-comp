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
    //  category  name                          punch  sustain  output    mix
    { "", "Default settings",                    0.0f,    0.0f,   0.0f, 100.0f,
      "Changes nothing yet. Turn PUNCH and SUSTAIN either way from the middle and listen." },

    { "Drums", "Kick - More Click",             40.0f,    0.0f,   0.0f, 100.0f,
      "Brings out the beater's attack so the kick cuts through the mix. More PUNCH = more click." },
    { "Drums", "Kick - Tighter",                10.0f,  -40.0f,   0.0f, 100.0f,
      "Shortens a boomy kick so it doesn't smear into the bass. Less SUSTAIN = shorter." },
    { "Drums", "Snare - More Crack",            40.0f,    0.0f,   0.0f, 100.0f,
      "Brings out the stick hitting the head - great on a snare that sounds a bit flat." },
    { "Drums", "Snare - Less Ring",             10.0f,  -35.0f,   0.0f, 100.0f,
      "Tames ring and room on the snare for a drier, tighter hit." },
    { "Drums", "Toms - Less Ring",              15.0f,  -40.0f,   0.0f, 100.0f,
      "Stops toms ringing on between fills - often quicker than gating them." },
    { "Drums", "Overheads - Less Room",          0.0f,  -30.0f,   0.0f, 100.0f,
      "Dries up a roomy kit: the hits stay, the room behind them drops back." },
    { "Drums", "Room Mics - Bigger Room",      -10.0f,   40.0f,   0.0f, 100.0f,
      "Pushes the room's echo up after each hit for a bigger space." },
    { "Drums", "Drum Bus - Punchier",           25.0f,    0.0f,   0.0f, 100.0f,
      "Adds snap to the whole kit. Switch AUTO-GAIN on so you're comparing punch, not volume." },
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
