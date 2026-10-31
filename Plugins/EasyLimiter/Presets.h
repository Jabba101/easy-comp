#pragma once

#include "PresetBrowser.h"

//==============================================================================
/*
    Parameter IDs: the names the plugin uses internally, and that your DAW
    stores in saved projects. Don't rename these once you've used the plugin
    in a project, or that project won't find its settings again.
*/
namespace LimiterIDs
{
    inline constexpr const char* gain        = "gain";          // dB, into the ceiling
    inline constexpr const char* ceiling     = "ceiling";       // dBTP
    inline constexpr const char* releaseMode = "releasemode";   // AUTO or SET
    inline constexpr const char* release     = "release";       // ms, when RELEASE is SET
    inline constexpr const char* match       = "match";         // on / off (for comparing by ear)
}

//==============================================================================
/*
    The presets. No loudness targets: the ceiling stops overs, GAIN pushes
    only as hard as you ask. RELEASE 0 = AUTO, 1 = SET (the ms column).
    Presets don't touch MATCH or BYPASS.
*/
struct LimiterPreset
{
    const char* category;
    const char* name;
    float gainDb;
    float ceilingDb;
    int   releaseMode;
    float releaseMs;
    const char* tip;
};

inline const LimiterPreset limiterPresets[] =
{
    //  category, name                                   gain  ceiling  mode  release
    { "", "Default settings",                             0.0f, -0.3f,   0, 100.0f,
      "Overs protection only: nothing changes until a peak would go past -0.3 dB true peak. Turn up GAIN to push into it." },

    // ---- Mastering ---------------------------------------------------------------
    { "Mixing & Mastering", "Master - Safety Ceiling",             0.0f, -0.3f,   0, 100.0f,
      "The last thing on the master: catches the odd over, true peak included, and leaves everything else alone." },
    { "Mixing & Mastering", "Master - Gentle Push (+2 dB)",        2.0f, -0.3f,   0, 100.0f,
      "Just the tallest peaks shaved: a little louder with no audible change. Check with MATCH." },
    { "Mixing & Mastering", "Master - Firm Push (+4 dB)",          4.0f, -0.3f,   0, 100.0f,
      "Louder, still clean on most mixes. Listen to the kick and snare: if they soften, back GAIN off." },
    { "Mixing & Mastering", "Master - Loud (+6 dB)",               6.0f, -0.3f,   0, 100.0f,
      "Loud rock, punk and electronic masters. Compare with MATCH on: you should hear more density, not damage." },
    { "Mixing & Mastering", "Master - Dense & Steady (slow release)", 4.0f, -0.3f, 1, 400.0f,
      "A slow, steady release: smooth and glued for synthwave, pads and ballads - no pumping, a little less punch." },

    // ---- Buses and stems -----------------------------------------------------------
    { "Mixing & Mastering", "Mix Bus - Overs Only",             0.0f, -1.0f,   0, 100.0f,
      "On the mix bus while you work: nothing ever clips, with room left for the master." },
    { "Drums", "Drum Bus - Catch the Peaks",       3.0f, -1.0f,   1,  30.0f,
      "A fast release that grabs the hits and lets straight go: more level from a drum bus without dulling it." },
    { "Mixing & Mastering", "Stems & Exports - -1 dB Ceiling",  0.0f, -1.0f,   0, 100.0f,
      "A little more room under the ceiling for files that will be converted, mixed or mastered elsewhere." },
};

//==============================================================================
inline std::vector<Preset> makeLimiterPresets()
{
    std::vector<Preset> presets;

    for (auto& p : limiterPresets)
        presets.push_back ({ p.category, p.name, p.tip,
                             { { LimiterIDs::gain, p.gainDb }, { LimiterIDs::ceiling, p.ceilingDb },
                               { LimiterIDs::releaseMode, (float) p.releaseMode }, { LimiterIDs::release, p.releaseMs } } });

    return presets;
}

/** The settings presets store (everything except MATCH and BYPASS). */
inline juce::StringArray limiterPresetIDs()
{
    return { LimiterIDs::gain, LimiterIDs::ceiling, LimiterIDs::releaseMode, LimiterIDs::release };
}
