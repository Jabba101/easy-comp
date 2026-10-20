#pragma once

#include <array>

//==============================================================================
/*
    Parameter IDs: the names the plugin uses internally, and that your DAW
    stores in saved projects. Don't rename these once you've used the plugin
    in a project, or that project won't find its settings again.
*/
namespace ParamIDs
{
    inline constexpr const char* threshold = "threshold";
    inline constexpr const char* ratio     = "ratio";
    inline constexpr const char* attack    = "attack";
    inline constexpr const char* release   = "release";
    inline constexpr const char* makeup    = "makeup";
    inline constexpr const char* mix       = "mix";
}

//==============================================================================
/*
    Drum presets.

    These are starting points, not magic numbers: every recording hits the
    plugin at a different level. After choosing one, adjust THRESHOLD until
    the meter shows the amount of squeeze you're after.

    The first preset is also the plugin's default setting.

    To add your own: copy a line, change the name and numbers, and change the
    8 in std::array<CompressorPreset, 8> to the new number of presets.
*/
struct CompressorPreset
{
    const char* name;
    float thresholdDb;  // -60 .. 0
    float ratio;        //   1 .. 20
    float attackMs;     // 0.1 .. 100
    float releaseMs;    //  10 .. 1000
    float makeupDb;     //   0 .. 24
    float mixPercent;   //   0 .. 100
};

inline constexpr std::array<CompressorPreset, 8> drumPresets
{{
    //  name                          thresh  ratio  attack  release  make-up    mix
    { "Default (all-rounder)",        -20.0f,  4.0f,  10.0f,  100.0f,    0.0f, 100.0f },
    { "Kick - Punch",                 -18.0f,  4.0f,  25.0f,   80.0f,    3.0f, 100.0f },
    { "Snare - Crack",                -20.0f,  4.0f,  15.0f,  100.0f,    3.0f, 100.0f },
    { "Toms - Fuller",                -24.0f,  3.0f,   5.0f,  200.0f,    4.0f, 100.0f },
    { "Overheads - Smooth",           -18.0f,  2.0f,  20.0f,  250.0f,    2.0f, 100.0f },
    { "Room Mics - Crushed",          -35.0f, 12.0f,   1.0f,  300.0f,   10.0f, 100.0f },
    { "Drum Bus - Glue",              -16.0f,  2.0f,  30.0f,  150.0f,    2.0f, 100.0f },
    { "Drum Bus - Parallel Smash",    -40.0f, 10.0f,   1.0f,  120.0f,   12.0f,  35.0f },
}};
