#pragma once

#include "PresetBrowser.h"

//==============================================================================
/*
    Parameter IDs: the names the plugin uses internally, and that your DAW
    stores in saved projects. Don't rename these once you've used the plugin
    in a project, or that project won't find its settings again.
*/
namespace CompIDs
{
    inline constexpr const char* threshold = "threshold";
    inline constexpr const char* ratio     = "ratio";
    inline constexpr const char* attack    = "attack";
    inline constexpr const char* release   = "release";
    inline constexpr const char* makeup    = "makeup";
    inline constexpr const char* mix       = "mix";
    inline constexpr const char* lowCut    = "lowcut";   // added in version 1.1
    inline constexpr const char* reactsTo  = "reactsto"; // added in version 1.3
}

/** REACTS TO: the two choices (in this order). */
enum ReactsTo { peaks = 0, average = 1 };

//==============================================================================
/*
    The presets.

    They're starting points, not magic numbers: every recording hits the
    plugin at a different level. After choosing one, adjust THRESHOLD until
    the meter shows the amount the tip suggests.

    To add your own: copy a line, then change the category (which sub-menu
    it's in), the name, the numbers and the tip. (You can also save presets
    from the plugin's menu without touching any code.)

    "ignore below" is the low-cut for what the compressor listens to:
    20 means off. "reacts to" is peaks or average. Presets don't touch the
    BYPASS and AUTO-GAIN buttons.
*/
struct CompPreset
{
    const char* category;
    const char* name;
    float thresholdDb;  // -60 .. 0
    float ratio;        //   1 .. 20
    float attackMs;     // 0.1 .. 100
    float releaseMs;    //  10 .. 1000
    float makeupDb;     //   0 .. 24
    float mixPercent;   //   0 .. 100
    float lowCutHz;     //  20 (off) .. 500
    ReactsTo reactsTo;  //  peaks or average
    const char* tip;
};

inline const CompPreset compressorPresets[] =
{
    //  category  name                          thresh  ratio  attack  release  make-up    mix  ignore  reacts to
    { "", "Default settings",                   -20.0f,  4.0f,  10.0f,  100.0f,    0.0f, 100.0f,  20.0f, peaks,
      "An all-round starting point. Turn THRESHOLD down until the meter moves about 3-6 dB on the loud parts." },

    { "Drums", "Kick - Punch",                  -18.0f,  4.0f,  25.0f,   80.0f,    3.0f, 100.0f,  20.0f, peaks,
      "The slow attack lets the beater's click through. Aim for 3-6 dB on each kick." },
    { "Drums", "Snare - Crack",                 -20.0f,  4.0f,  15.0f,  100.0f,    3.0f, 100.0f, 100.0f, peaks,
      "Keeps the stick's crack while evening out the hits. Aim for 3-6 dB on each hit." },
    { "Drums", "Toms - Fuller",                 -24.0f,  3.0f,   5.0f,  200.0f,    4.0f, 100.0f,  20.0f, peaks,
      "Brings up the ring of the toms. Aim for 4-6 dB on each hit." },
    { "Drums", "Overheads - Smooth",            -18.0f,  2.0f,  20.0f,  250.0f,    2.0f, 100.0f, 120.0f, peaks,
      "Gentle control of the whole kit and cymbals. Aim for just 1-3 dB." },
    { "Drums", "Room Mics - Crushed",           -35.0f, 12.0f,   1.0f,  300.0f,   10.0f, 100.0f,  20.0f, peaks,
      "Squashes the room sound for size and excitement. 10 dB or more is fine here - blend it in with the fader." },
    { "Drums", "Drum Bus - Glue",               -16.0f,  2.0f,  30.0f,  150.0f,    2.0f, 100.0f, 100.0f, peaks,
      "Makes the kit sound like one instrument. Aim for 1-3 dB: if you can clearly hear it working, back off." },
    { "Drums", "Drum Bus - Parallel Smash",     -40.0f, 10.0f,   1.0f,  120.0f,   12.0f,  35.0f,  80.0f, peaks,
      "Heavy squash blended under the original (MIX 35%). Turn MIX up for more thickness, down for more natural." },
};

//==============================================================================
inline std::vector<Preset> makeCompressorPresets()
{
    std::vector<Preset> presets;

    for (auto& p : compressorPresets)
        presets.push_back ({ p.category, p.name, p.tip,
                             { { CompIDs::threshold, p.thresholdDb }, { CompIDs::ratio,    p.ratio },
                               { CompIDs::attack,    p.attackMs },    { CompIDs::release,  p.releaseMs },
                               { CompIDs::makeup,    p.makeupDb },    { CompIDs::mix,      p.mixPercent },
                               { CompIDs::lowCut,    p.lowCutHz },    { CompIDs::reactsTo, (float) p.reactsTo } } });

    return presets;
}

/** The settings presets store (everything except BYPASS and AUTO-GAIN). */
inline juce::StringArray compressorPresetIDs()
{
    return { CompIDs::threshold, CompIDs::ratio, CompIDs::attack, CompIDs::release,
             CompIDs::makeup, CompIDs::mix, CompIDs::lowCut, CompIDs::reactsTo };
}
