#pragma once

#include "PresetBrowser.h"

//==============================================================================
/*
    Parameter IDs: the names the plugin uses internally, and that your DAW
    stores in saved projects. Don't rename these once you've used the plugin
    in a project, or that project won't find its settings again.
*/
namespace MasterIDs
{
    // INPUT
    inline constexpr const char* trim         = "trim";          // dB
    // COMPRESSOR (MID settings are also the STEREO settings)
    inline constexpr const char* compOn       = "compon";        // on / off
    inline constexpr const char* compMode     = "compmode";      // STEREO or MID + SIDE
    inline constexpr const char* midThreshold = "mthreshold";    // dB
    inline constexpr const char* midRatio     = "mratio";
    inline constexpr const char* midAttack    = "mattack";       // ms
    inline constexpr const char* midRelease   = "mrelease";      // ms
    inline constexpr const char* midMakeup    = "mmakeup";       // dB
    inline constexpr const char* sideThreshold = "sthreshold";
    inline constexpr const char* sideRatio    = "sratio";
    inline constexpr const char* sideAttack   = "sattack";
    inline constexpr const char* sideRelease  = "srelease";
    inline constexpr const char* sideMakeup   = "smakeup";
    inline constexpr const char* compMix      = "compmix";       // %
    inline constexpr const char* ignoreBelow  = "ignorebelow";   // Hz (the compressor doesn't react to the deep bass)
    inline constexpr const char* reactsTo     = "reactsto";      // PEAKS or AVERAGE
    inline constexpr const char* character    = "character";     // MODERN or VINTAGE
    // LIMITER
    inline constexpr const char* limitGain    = "lgain";         // dB, into the ceiling
    inline constexpr const char* ceiling      = "ceiling";       // dBTP
    inline constexpr const char* releaseMode  = "lreleasemode";  // AUTO or SET
    inline constexpr const char* release      = "lrelease";      // ms
    // COMPARE (not in presets)
    inline constexpr const char* match        = "match";         // the whole chain as loud as the original
    inline constexpr const char* listen       = "listen";        // STEREO / MID / SIDE / MONO (for checking only)
}

namespace MasterMode   { enum { stereo = 0, midSide }; }
namespace MasterListen { enum { stereo = 0, mid, side, mono }; }

//==============================================================================
/*
    The presets: the whole chain, as a starting point to tweak. LUFS is a
    reading here, never a target - the ceiling stops overs, GAIN pushes only
    as hard as you ask. For heavy bass, the compressor ignores the deep end
    (IGNORE BELOW), so the kick and bass don't make the whole mix pump.

    COMP 0 = off. MODE 0 = STEREO (the MID column is used for both sides),
    1 = MID + SIDE. REACTS 0 = PEAKS, 1 = AVERAGE. CHAR 0 = MODERN,
    1 = VINTAGE. REL 0 = AUTO, 1 = SET (the ms column).
*/
struct MasterPreset
{
    const char* category;
    const char* name;
    float trimDb;
    int   compOn, mode;
    float midThreshold, midRatio, midAttack, midRelease, midMakeup;
    float sideThreshold, sideRatio, sideAttack, sideRelease, sideMakeup;
    float mixPercent, ignoreBelowHz;
    int   reactsTo, character;
    float limitGainDb, ceilingDb;
    int   releaseMode;
    float releaseMs;
    const char* tip;
};

inline const MasterPreset masterPresets[] =
{
    //                                                  trim  comp mode | MID: thr  ratio att  rel  makeup | SIDE: thr ratio att  rel  makeup | mix  ignore react char | gain ceil  rel  ms
    { "", "Default settings",                           0.0f,  1, 0,     -12.0f, 2.0f, 30.0f, 200.0f, 1.0f,   -12.0f, 2.0f, 30.0f, 200.0f, 1.0f,   100.0f, 80.0f, 1, 0,   2.0f, -0.3f, 0, 100.0f,
      "Gentle glue (2:1, slow, ignoring the deep bass) into a clean -0.3 dBTP ceiling, 2 dB pushed. Use MATCH to hear it fairly." },

    // ---- Mastering --------------------------------------------------------------------
    { "Mastering", "Master - Transparent (ceiling only)", 0.0f, 0, 0,    -14.0f, 2.0f, 30.0f, 200.0f, 0.0f,   -14.0f, 2.0f, 30.0f, 200.0f, 0.0f,   100.0f, 80.0f, 1, 0,   0.0f, -0.3f, 0, 100.0f,
      "Nothing but the overs protection: for a mix that's already finished." },
    { "Mastering", "Master - Gentle Glue (+3 dB)",      0.0f,  1, 0,     -16.0f, 1.5f, 30.0f, 250.0f, 1.0f,   -16.0f, 1.5f, 30.0f, 250.0f, 1.0f,   100.0f, 80.0f, 1, 0,   3.0f, -0.3f, 0, 100.0f,
      "1.5:1 holding the mix together, a little louder. Should be hard to hear with MATCH on - just more 'finished'." },
    { "Mastering", "Master - Rock & Punk (+5 dB)",      0.0f,  1, 0,     -14.0f, 3.0f, 30.0f, 120.0f, 2.0f,   -14.0f, 3.0f, 30.0f, 120.0f, 2.0f,   100.0f, 90.0f, 0, 0,   5.0f, -0.3f, 0, 100.0f,
      "Firmer and faster: guitars and drums dense and in your face. 30 ms lets the snare crack through first." },
    { "Mastering", "Master - Heavy & Loud (+7 dB)",     0.0f,  1, 0,     -16.0f, 3.0f, 20.0f, 100.0f, 2.0f,   -16.0f, 3.0f, 20.0f, 100.0f, 2.0f,    85.0f, 100.0f, 0, 1,  7.0f, -0.3f, 0, 100.0f,
      "Loud metal and heavy rock. VINTAGE for weight, 85% MIX keeps some of the hits' snap. Listen to the kick." },
    { "Mastering", "Master - Synthwave Smooth & Wide",  0.0f,  1, 1,     -16.0f, 2.0f, 40.0f, 300.0f, 1.0f,   -20.0f, 1.5f, 40.0f, 300.0f, 1.0f,   100.0f, 80.0f, 1, 0,   4.0f, -0.3f, 1, 300.0f,
      "Mid glued, the wide pads and synths left breathing; a slow, steady limiter release with no pumping." },
    { "Mastering", "Master - Darksynth Dense",          0.0f,  1, 1,     -18.0f, 3.0f, 25.0f, 150.0f, 2.0f,   -18.0f, 2.0f, 30.0f, 200.0f, 1.0f,   100.0f, 90.0f, 1, 1,   6.0f, -0.3f, 0, 100.0f,
      "A thick, heavy middle (bass, kick, lead) with controlled sides. Big, dark and loud." },
    { "Mastering", "Master - Jazz Natural",             0.0f,  1, 0,     -20.0f, 1.5f, 50.0f, 300.0f, 0.5f,   -20.0f, 1.5f, 50.0f, 300.0f, 0.5f,   100.0f, 60.0f, 1, 0,   1.5f, -0.3f, 0, 100.0f,
      "Barely there: the dynamics of real players kept, just the peaks caught. The room and the ride stay open." },

    // ---- Mid / side fixes -------------------------------------------------------------
    { "Mid / Side", "M/S - Tighten the Sides",          0.0f,  1, 1,     -14.0f, 1.5f, 30.0f, 200.0f, 0.5f,   -22.0f, 3.0f, 15.0f, 150.0f, 0.0f,   100.0f, 80.0f, 1, 0,   2.0f, -0.3f, 0, 100.0f,
      "The sides held in (wide guitars, cymbals, reverb that jump out), the middle almost untouched." },
    { "Mid / Side", "M/S - Punchy Middle",              0.0f,  1, 1,     -14.0f, 2.5f, 40.0f, 120.0f, 1.5f,   -18.0f, 1.5f, 30.0f, 250.0f, 0.5f,   100.0f, 90.0f, 0, 0,   3.0f, -0.3f, 0, 100.0f,
      "A slow attack on the middle so kick, snare and vocal punch through; the sides just smoothed." },
    { "Mid / Side", "M/S - Steady Vocal & Bass",        0.0f,  1, 1,     -18.0f, 2.5f, 15.0f, 150.0f, 1.5f,   -20.0f, 1.2f, 30.0f, 250.0f, 0.0f,   100.0f, 40.0f, 1, 0,   3.0f, -0.3f, 0, 100.0f,
      "Evens out what's in the centre (vocal, bass, kick) and leaves the stereo picture alone." },
};

//==============================================================================
inline std::vector<Preset> makeMasterPresets()
{
    std::vector<Preset> presets;

    for (auto& p : masterPresets)
        presets.push_back ({ p.category, p.name, p.tip,
                             { { MasterIDs::trim, p.trimDb }, { MasterIDs::compOn, (float) p.compOn }, { MasterIDs::compMode, (float) p.mode },
                               { MasterIDs::midThreshold, p.midThreshold }, { MasterIDs::midRatio, p.midRatio }, { MasterIDs::midAttack, p.midAttack },
                               { MasterIDs::midRelease, p.midRelease }, { MasterIDs::midMakeup, p.midMakeup },
                               { MasterIDs::sideThreshold, p.sideThreshold }, { MasterIDs::sideRatio, p.sideRatio }, { MasterIDs::sideAttack, p.sideAttack },
                               { MasterIDs::sideRelease, p.sideRelease }, { MasterIDs::sideMakeup, p.sideMakeup },
                               { MasterIDs::compMix, p.mixPercent }, { MasterIDs::ignoreBelow, p.ignoreBelowHz },
                               { MasterIDs::reactsTo, (float) p.reactsTo }, { MasterIDs::character, (float) p.character },
                               { MasterIDs::limitGain, p.limitGainDb }, { MasterIDs::ceiling, p.ceilingDb },
                               { MasterIDs::releaseMode, (float) p.releaseMode }, { MasterIDs::release, p.releaseMs } } });

    return presets;
}

/** The settings presets store (everything except MATCH, LISTEN and BYPASS). */
inline juce::StringArray masterPresetIDs()
{
    using namespace MasterIDs;
    return { trim, compOn, compMode, midThreshold, midRatio, midAttack, midRelease, midMakeup,
             sideThreshold, sideRatio, sideAttack, sideRelease, sideMakeup, compMix, ignoreBelow, reactsTo, character,
             limitGain, ceiling, releaseMode, release };
}
