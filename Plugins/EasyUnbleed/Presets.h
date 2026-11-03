#pragma once

#include "PresetBrowser.h"

//==============================================================================
/*
    Parameter IDs: the names the plugin uses internally, and that your DAW
    stores in saved projects. Don't rename these once you've used the plugin
    in a project, or that project won't find its settings again.
*/
namespace UnbleedIDs
{
    inline constexpr const char* amount      = "amount";       // dB
    inline constexpr const char* sensitivity = "sensitivity";  // %
    inline constexpr const char* smoothness  = "smoothness";   // %
    inline constexpr const char* from        = "from";         // Hz (20 = off)
    inline constexpr const char* to          = "to";           // Hz (20000 = off)
    inline constexpr const char* listen      = "listen";       // hear only what's removed
}

inline constexpr float unbleedFromOff = 20.0f, unbleedToOff = 20000.0f;

//==============================================================================
/*
    The presets - starting points for UVR5 (or any separator's) stems. Put
    Easy Unbleed on the stem, and send the OTHER stems to its sidechain.
    FROM / TO limit where it works (20 Hz / 20 kHz = everywhere). Presets
    don't touch LISTEN or BYPASS.
*/
struct UnbleedPreset
{
    const char* category;
    const char* name;
    float amountDb;
    float sensitivityPercent;
    float smoothnessPercent;
    float fromHz;
    float toHz;
    const char* tip;
};

inline const UnbleedPreset unbleedPresets[] =
{
    //  category, name                                    amount  sens  smooth    from      to
    { "", "Default settings",                              24.0f, 60.0f, 30.0f,    20.0f, 20000.0f,
      "Send the other stems to the sidechain. It turns down their ghost where this stem goes quiet. Raise SENSITIVITY for more." },

    // ---- Vocals ------------------------------------------------------------------
    { "Vocals", "Vocal - Clean the Pauses (Gentle)",       18.0f, 45.0f, 40.0f,    20.0f, 20000.0f,
      "Only the clearest ghosts in the gaps between phrases. Safe on any vocal stem." },
    { "Vocals", "Vocal - Hi-Hat & Cymbal Ghost",           30.0f, 70.0f, 20.0f,  3000.0f, 20000.0f,
      "Just the top end: the hi-hat and cymbal hiss UVR leaves in a vocal. The voice's body is never touched." },
    { "Vocals", "Vocal - Strong Cleanup",                  36.0f, 85.0f, 25.0f,    20.0f, 20000.0f,
      "A thorough clean of every ghost. Check with LISTEN that no breaths or word endings go too." },

    // ---- Drums -------------------------------------------------------------------
    { "Drums", "Drums - Vocal Ghost",                      24.0f, 65.0f, 25.0f,   200.0f,  8000.0f,
      "The shadow of the vocal between the hits, in the range a voice sits in. Cymbal tails are left alone above 8 kHz." },
    { "Drums", "Drums - Clean the Gaps",                   30.0f, 75.0f, 15.0f,    20.0f, 20000.0f,
      "Everything else's ghost between the hits: a tight, quick clean for punchy drum stems." },

    // ---- Bass --------------------------------------------------------------------
    { "Bass", "Bass - Low End Only",                       24.0f, 60.0f, 35.0f,    20.0f,   400.0f,
      "Only below 400 Hz: the kick's and the guitars' ghost in the bass stem, leaving the bass's own top alone." },
    { "Bass", "Bass - Clean the Gaps",                     24.0f, 60.0f, 30.0f,    20.0f, 20000.0f,
      "Everything else's ghost when the bass stops." },

    // ---- Other / guitars / keys --------------------------------------------------
    { "Guitars & Keys", "Other - Vocal Ghost",             22.0f, 60.0f, 35.0f,   300.0f,  6000.0f,
      "The vocal's shadow in the 'other' stem (guitars, keys, pads), in the range a voice sits in." },
    { "Guitars & Keys", "Other - Clean the Gaps",          20.0f, 55.0f, 40.0f,    20.0f, 20000.0f,
      "A gentle clean of everything else's ghost when the guitars or keys stop." },
};

//==============================================================================
inline std::vector<Preset> makeUnbleedPresets()
{
    std::vector<Preset> presets;
    for (auto& p : unbleedPresets)
        presets.push_back ({ p.category, p.name, p.tip,
                             { { UnbleedIDs::amount, p.amountDb }, { UnbleedIDs::sensitivity, p.sensitivityPercent },
                               { UnbleedIDs::smoothness, p.smoothnessPercent }, { UnbleedIDs::from, p.fromHz }, { UnbleedIDs::to, p.toHz } } });
    return presets;
}

/** The settings presets store (everything except LISTEN and BYPASS). */
inline juce::StringArray unbleedPresetIDs()
{
    return { UnbleedIDs::amount, UnbleedIDs::sensitivity, UnbleedIDs::smoothness, UnbleedIDs::from, UnbleedIDs::to };
}
