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
    // RESONANCE TAMER (MID settings are also the STEREO settings)
    inline constexpr const char* tamerOn      = "tameron";
    inline constexpr const char* tamerMode    = "tamermode";     // STEREO or MID + SIDE
    inline constexpr const char* tMidAmount   = "tmamount";      // dB (the most it turns a spot down)
    inline constexpr const char* tMidSens     = "tmsens";        // %
    inline constexpr const char* tMidSharp    = "tmsharp";       // %
    inline constexpr const char* tSideAmount  = "tsamount";
    inline constexpr const char* tSideSens    = "tssens";
    inline constexpr const char* tSideSharp   = "tssharp";
    inline constexpr const char* tSpeed       = "tspeed";        // %: fast .. slow
    inline constexpr const char* tFrom        = "tfrom";         // Hz
    inline constexpr const char* tTo          = "tto";           // Hz
    inline constexpr const char* tListen      = "tlisten";       // hear only what it takes out (not in presets)
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

/** The EQ: per part (0 = MID or STEREO, 1 = SIDE) five bands' FREQ and GAIN and the LOW CUT. */
namespace MasterEQIDs
{
    inline constexpr const char* on   = "eqon";
    inline constexpr const char* mode = "eqmode";   // STEREO or MID + SIDE
    inline constexpr const char* freq[2][5] { { "eqm0f", "eqm1f", "eqm2f", "eqm3f", "eqm4f" }, { "eqs0f", "eqs1f", "eqs2f", "eqs3f", "eqs4f" } };
    inline constexpr const char* gain[2][5] { { "eqm0g", "eqm1g", "eqm2g", "eqm3g", "eqm4g" }, { "eqs0g", "eqs1g", "eqs2g", "eqs3g", "eqs4g" } };
    inline constexpr const char* lowCut[2] { "eqmlc", "eqslc" };

    /** Each band's name, range and starting frequency. */
    struct BandInfo { const char* name; float lowHz, highHz, defaultHz; };
    inline constexpr BandInfo bands[5] { { "LOW SHELF", 20.0f, 300.0f, 80.0f }, { "LOW MID", 80.0f, 800.0f, 250.0f },
                                         { "MID", 300.0f, 4000.0f, 1000.0f }, { "HIGH MID", 1500.0f, 10000.0f, 3500.0f },
                                         { "HIGH SHELF", 3000.0f, 20000.0f, 10000.0f } };
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
    // RESONANCE TAMER: on, MID + SIDE, MID amount / sensitivity / sharpness, SIDE amount / sensitivity / sharpness, speed (%), from, to
    // EQ (when set = true): on, MID + SIDE, then per part the five gains (dB), five frequencies (0 = the band's own default) and LOW CUT
    struct EQ { bool set = false; int on = 1, midSide = 0;
                float midGain[5] {}, midFreq[5] {}, midLowCut = 20.0f, sideGain[5] {}, sideFreq[5] {}, sideLowCut = 20.0f; };
    struct Tamer { int on = 1, midSide = 0; float midAmount = 4.0f, midSens = 50.0f, midSharp = 50.0f,
                   sideAmount = 4.0f, sideSens = 50.0f, sideSharp = 50.0f, speed = 50.0f, fromHz = 200.0f, toHz = 16000.0f; } tamer {};
    EQ eq {};
};

inline const MasterPreset masterPresets[] =
{
    //                                                  trim  comp mode | MID: thr  ratio att  rel  makeup | SIDE: thr ratio att  rel  makeup | mix  ignore react char | gain ceil  rel  ms
    { "", "Default settings",                           0.0f,  1, 0,     -12.0f, 2.0f, 30.0f, 200.0f, 1.0f,   -12.0f, 2.0f, 30.0f, 200.0f, 1.0f,   100.0f, 80.0f, 1, 0,   2.0f, -0.3f, 0, 100.0f,
      "Gentle glue (2:1, slow, ignoring the deep bass) into a clean -0.3 dBTP ceiling, 2 dB pushed. Use MATCH to hear it fairly.", { 1, 0, 3.0f, 50.0f, 50.0f, 3.0f, 50.0f, 50.0f, 50.0f, 200.0f, 16000.0f } },

    // ---- Mastering --------------------------------------------------------------------
    { "Mastering", "Master - Transparent (ceiling only)", 0.0f, 0, 0,    -14.0f, 2.0f, 30.0f, 200.0f, 0.0f,   -14.0f, 2.0f, 30.0f, 200.0f, 0.0f,   100.0f, 80.0f, 1, 0,   0.0f, -0.3f, 0, 100.0f,
      "Nothing but the overs protection: for a mix that's already finished.", { 0, 0, 3.0f, 50.0f, 50.0f, 3.0f, 50.0f, 50.0f, 50.0f, 200.0f, 16000.0f } },
    { "Mastering", "Master - Gentle Glue (+3 dB)",      0.0f,  1, 0,     -16.0f, 1.5f, 30.0f, 250.0f, 1.0f,   -16.0f, 1.5f, 30.0f, 250.0f, 1.0f,   100.0f, 80.0f, 1, 0,   3.0f, -0.3f, 0, 100.0f,
      "1.5:1 holding the mix together, a little louder. Should be hard to hear with MATCH on - just more 'finished'.", { 1, 0, 3.0f, 40.0f, 50.0f, 3.0f, 40.0f, 50.0f, 50.0f, 200.0f, 16000.0f } },
    { "Mastering", "Master - Rock & Punk (+5 dB)",      0.0f,  1, 0,     -14.0f, 3.0f, 30.0f, 120.0f, 2.0f,   -14.0f, 3.0f, 30.0f, 120.0f, 2.0f,   100.0f, 90.0f, 0, 0,   5.0f, -0.3f, 0, 100.0f,
      "Firmer and faster: guitars and drums dense and in your face. 30 ms lets the snare crack through first.", { 1, 0, 4.0f, 50.0f, 50.0f, 4.0f, 50.0f, 50.0f, 40.0f, 250.0f, 16000.0f } },
    { "Mastering", "Master - Heavy & Loud (+7 dB)",     0.0f,  1, 0,     -16.0f, 3.0f, 20.0f, 100.0f, 2.0f,   -16.0f, 3.0f, 20.0f, 100.0f, 2.0f,    85.0f, 100.0f, 0, 1,  7.0f, -0.3f, 0, 100.0f,
      "Loud metal and heavy rock. VINTAGE for weight, 85% MIX keeps some of the hits' snap. Listen to the kick.", { 1, 0, 5.0f, 55.0f, 50.0f, 5.0f, 55.0f, 50.0f, 40.0f, 250.0f, 16000.0f } },
    { "Mastering", "Master - Synthwave Smooth & Wide",  0.0f,  1, 1,     -16.0f, 2.0f, 40.0f, 300.0f, 1.0f,   -20.0f, 1.5f, 40.0f, 300.0f, 1.0f,   100.0f, 80.0f, 1, 0,   4.0f, -0.3f, 1, 300.0f,
      "Mid glued, the wide pads and synths left breathing; a slow, steady limiter release with no pumping.", { 1, 1, 3.0f, 50.0f, 50.0f, 5.0f, 60.0f, 40.0f, 60.0f, 200.0f, 16000.0f } },
    { "Mastering", "Master - Darksynth Dense",          0.0f,  1, 1,     -18.0f, 3.0f, 25.0f, 150.0f, 2.0f,   -18.0f, 2.0f, 30.0f, 200.0f, 1.0f,   100.0f, 90.0f, 1, 1,   6.0f, -0.3f, 0, 100.0f,
      "A thick, heavy middle (bass, kick, lead) with controlled sides. Big, dark and loud.", { 1, 1, 4.0f, 55.0f, 50.0f, 5.0f, 55.0f, 50.0f, 50.0f, 200.0f, 16000.0f } },
    { "Mastering", "Master - Jazz Natural",             0.0f,  1, 0,     -20.0f, 1.5f, 50.0f, 300.0f, 0.5f,   -20.0f, 1.5f, 50.0f, 300.0f, 0.5f,   100.0f, 60.0f, 1, 0,   1.5f, -0.3f, 0, 100.0f,
      "Barely there: the dynamics of real players kept, just the peaks caught. The room and the ride stay open.", { 1, 0, 2.0f, 35.0f, 70.0f, 2.0f, 35.0f, 70.0f, 60.0f, 200.0f, 16000.0f } },

    { "Mastering", "Master - Tame Harsh AI Fizz",       0.0f,  1, 0,     -16.0f, 1.5f, 30.0f, 250.0f, 1.0f,   -16.0f, 1.5f, 30.0f, 250.0f, 1.0f,   100.0f, 80.0f, 1, 0,   3.0f, -0.3f, 0, 100.0f,
      "For AI-made songs (Mureka, Suno): the resonance tamer works hard on the fizzy, metallic top end, gentle glue and +3 dB.",
      { 1, 0, 6.0f, 65.0f, 55.0f, 6.0f, 65.0f, 55.0f, 30.0f, 4000.0f, 16000.0f } },

    { "Mastering", "Master - Tighten & Brighten (M/S)",  0.0f, 1, 1,     -16.0f, 2.0f, 30.0f, 200.0f, 1.0f,   -18.0f, 1.5f, 30.0f, 250.0f, 0.5f,   100.0f, 80.0f, 1, 0,   3.0f, -0.3f, 0, 100.0f,
      "A tight, mono low end (low cut on the sides), a little mud out of the middle, air and sparkle on the sides only.",
      { 1, 0, 3.0f, 50.0f, 50.0f, 3.0f, 50.0f, 50.0f, 50.0f, 200.0f, 16000.0f },
      { true, 1, 1, { 0.5f, -1.5f, 0.0f, 0.5f, 0.5f }, { 60.0f, 300.0f, 0.0f, 3500.0f, 12000.0f }, 20.0f,
                    { 0.0f, -1.0f, 0.0f, 1.0f, 2.5f }, { 0.0f, 350.0f, 0.0f, 5000.0f, 11000.0f }, 120.0f } },
    { "Mastering", "Master - Heavy Low End",            0.0f,  1, 1,     -16.0f, 2.0f, 30.0f, 150.0f, 1.0f,   -18.0f, 1.5f, 30.0f, 200.0f, 0.5f,   100.0f, 100.0f, 1, 1,  4.0f, -0.3f, 0, 100.0f,
      "Big, heavy bass and kick weight in the middle, the lows kept mono and tight; the compressor ignores the deep end.",
      { 1, 0, 3.0f, 50.0f, 50.0f, 3.0f, 50.0f, 50.0f, 50.0f, 250.0f, 16000.0f },
      { true, 1, 1, { 2.0f, -1.0f, 0.0f, 0.0f, 0.5f }, { 70.0f, 280.0f, 0.0f, 0.0f, 12000.0f }, 20.0f,
                    { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f, 11000.0f }, 150.0f } },

    // ---- Mid / side fixes -------------------------------------------------------------
    { "Mid / Side", "M/S - Tighten the Sides",          0.0f,  1, 1,     -14.0f, 1.5f, 30.0f, 200.0f, 0.5f,   -22.0f, 3.0f, 15.0f, 150.0f, 0.0f,   100.0f, 80.0f, 1, 0,   2.0f, -0.3f, 0, 100.0f,
      "The sides held in (wide guitars, cymbals, reverb that jump out), the middle almost untouched.", { 1, 1, 2.0f, 45.0f, 50.0f, 5.0f, 60.0f, 50.0f, 50.0f, 200.0f, 16000.0f } },
    { "Mid / Side", "M/S - Punchy Middle",              0.0f,  1, 1,     -14.0f, 2.5f, 40.0f, 120.0f, 1.5f,   -18.0f, 1.5f, 30.0f, 250.0f, 0.5f,   100.0f, 90.0f, 0, 0,   3.0f, -0.3f, 0, 100.0f,
      "A slow attack on the middle so kick, snare and vocal punch through; the sides just smoothed.", { 1, 0, 3.0f, 50.0f, 50.0f, 3.0f, 50.0f, 50.0f, 50.0f, 250.0f, 16000.0f } },
    { "Mid / Side", "M/S - Steady Vocal & Bass",        0.0f,  1, 1,     -18.0f, 2.5f, 15.0f, 150.0f, 1.5f,   -20.0f, 1.2f, 30.0f, 250.0f, 0.0f,   100.0f, 40.0f, 1, 0,   3.0f, -0.3f, 0, 100.0f,
      "Evens out what's in the centre (vocal, bass, kick) and leaves the stereo picture alone.", { 1, 1, 4.0f, 55.0f, 55.0f, 3.0f, 45.0f, 50.0f, 50.0f, 300.0f, 16000.0f } },
};

//==============================================================================
inline std::vector<Preset> makeMasterPresets()
{
    std::vector<Preset> presets;

    for (auto& p : masterPresets)
    {
        presets.push_back ({ p.category, p.name, p.tip,
                             { { MasterIDs::trim, p.trimDb }, { MasterIDs::compOn, (float) p.compOn }, { MasterIDs::compMode, (float) p.mode },
                               { MasterIDs::midThreshold, p.midThreshold }, { MasterIDs::midRatio, p.midRatio }, { MasterIDs::midAttack, p.midAttack },
                               { MasterIDs::midRelease, p.midRelease }, { MasterIDs::midMakeup, p.midMakeup },
                               { MasterIDs::sideThreshold, p.sideThreshold }, { MasterIDs::sideRatio, p.sideRatio }, { MasterIDs::sideAttack, p.sideAttack },
                               { MasterIDs::sideRelease, p.sideRelease }, { MasterIDs::sideMakeup, p.sideMakeup },
                               { MasterIDs::compMix, p.mixPercent }, { MasterIDs::ignoreBelow, p.ignoreBelowHz },
                               { MasterIDs::reactsTo, (float) p.reactsTo }, { MasterIDs::character, (float) p.character },
                               { MasterIDs::limitGain, p.limitGainDb }, { MasterIDs::ceiling, p.ceilingDb },
                               { MasterIDs::releaseMode, (float) p.releaseMode }, { MasterIDs::release, p.releaseMs },
                               { MasterIDs::tamerOn, (float) p.tamer.on }, { MasterIDs::tamerMode, (float) p.tamer.midSide },
                               { MasterIDs::tMidAmount, p.tamer.midAmount }, { MasterIDs::tMidSens, p.tamer.midSens }, { MasterIDs::tMidSharp, p.tamer.midSharp },
                               { MasterIDs::tSideAmount, p.tamer.sideAmount }, { MasterIDs::tSideSens, p.tamer.sideSens }, { MasterIDs::tSideSharp, p.tamer.sideSharp },
                               { MasterIDs::tSpeed, p.tamer.speed }, { MasterIDs::tFrom, p.tamer.fromHz }, { MasterIDs::tTo, p.tamer.toHz } } });
        // (the EQ: flat unless the preset sets it - any setting a preset doesn't mention goes back to its default)
        auto& values = presets.back().values;
        if (p.eq.set)
        {
            values.push_back ({ juce::String (MasterEQIDs::on), (float) p.eq.on });
            values.push_back ({ juce::String (MasterEQIDs::mode), (float) p.eq.midSide });
            for (int part = 0; part < 2; ++part)
            {
                const float* gains = part == 0 ? p.eq.midGain : p.eq.sideGain;
                const float* freqs = part == 0 ? p.eq.midFreq : p.eq.sideFreq;
                for (int b = 0; b < 5; ++b)
                {
                    values.push_back ({ juce::String (MasterEQIDs::gain[part][b]), gains[b] });
                    values.push_back ({ juce::String (MasterEQIDs::freq[part][b]), freqs[b] > 0.0f ? freqs[b] : MasterEQIDs::bands[b].defaultHz });
                }
                values.push_back ({ juce::String (MasterEQIDs::lowCut[part]), part == 0 ? p.eq.midLowCut : p.eq.sideLowCut });
            }
        }
    }

    return presets;
}

/** The settings presets store (everything except MATCH, the LISTEN buttons and BYPASS). */
inline juce::StringArray masterPresetIDs()
{
    using namespace MasterIDs;
    juce::StringArray ids { trim, compOn, compMode, midThreshold, midRatio, midAttack, midRelease, midMakeup,
             sideThreshold, sideRatio, sideAttack, sideRelease, sideMakeup, compMix, ignoreBelow, reactsTo, character,
             limitGain, ceiling, releaseMode, release,
             tamerOn, tamerMode, tMidAmount, tMidSens, tMidSharp, tSideAmount, tSideSens, tSideSharp, tSpeed, tFrom, tTo };
    ids.add (MasterEQIDs::on);
    ids.add (MasterEQIDs::mode);
    for (int part = 0; part < 2; ++part)
    {
        for (int b = 0; b < 5; ++b) { ids.add (MasterEQIDs::freq[part][b]); ids.add (MasterEQIDs::gain[part][b]); }
        ids.add (MasterEQIDs::lowCut[part]);
    }
    return ids;
}
