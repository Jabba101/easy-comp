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

/** The MULTIBAND: per part (0 = MID or STEREO, 1 = SIDE) each band's THRESHOLD, RATIO, SPEED and GAIN; per band ON and SOLO; the three splits. */
namespace MasterMBIDs
{
    inline constexpr const char* on   = "mbon";
    inline constexpr const char* mode = "mbmode";
    inline constexpr const char* threshold[2][4] { { "mbm0t", "mbm1t", "mbm2t", "mbm3t" }, { "mbs0t", "mbs1t", "mbs2t", "mbs3t" } };
    inline constexpr const char* ratio[2][4]     { { "mbm0r", "mbm1r", "mbm2r", "mbm3r" }, { "mbs0r", "mbs1r", "mbs2r", "mbs3r" } };
    inline constexpr const char* speed[2][4]     { { "mbm0s", "mbm1s", "mbm2s", "mbm3s" }, { "mbs0s", "mbs1s", "mbs2s", "mbs3s" } };
    inline constexpr const char* gain[2][4]      { { "mbm0g", "mbm1g", "mbm2g", "mbm3g" }, { "mbs0g", "mbs1g", "mbs2g", "mbs3g" } };
    inline constexpr const char* bandOn[4]       { "mb0on", "mb1on", "mb2on", "mb3on" };
    inline constexpr const char* solo[4]         { "mb0solo", "mb1solo", "mb2solo", "mb3solo" };   // (for listening: not in presets)
    inline constexpr const char* split[3]        { "mbx0", "mbx1", "mbx2" };
    inline constexpr const char* bandNames[4]    { "LOW", "LOW MID", "HIGH MID", "HIGH" };
    struct SplitInfo { float lowHz, highHz, defaultHz; };
    inline constexpr SplitInfo splits[3] { { 40.0f, 400.0f, 120.0f }, { 300.0f, 4000.0f, 1000.0f }, { 2000.0f, 14000.0f, 6000.0f } };
}

/** PUNCH (the Easy Trans engine: the start of each hit louder, the tails longer or shorter) and WIDTH. */
namespace MasterPunchIDs
{
    inline constexpr const char* on      = "punchon";
    inline constexpr const char* mode    = "punchmode";   // STEREO or MID + SIDE
    inline constexpr const char* punch[2]   { "pmpunch", "pspunch" };     // % (-100 .. +100)
    inline constexpr const char* sustain[2] { "pmsustain", "pssustain" }; // %
    inline constexpr const char* mix     = "punchmix";    // %
}
namespace MasterWidthIDs
{
    inline constexpr const char* on       = "widthon";
    inline constexpr const char* width    = "width";      // % of the sides (100 = as it is)
    inline constexpr const char* monoBass = "monobass";   // Hz: the sides below it removed (20 = off)
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

namespace MasterModules { inline juce::StringArray ids (int module); }

/** The settings presets store (everything except MATCH, the LISTEN buttons, SOLO and BYPASS). */
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
    for (int m : { 4, 5, 6 })   // (the MULTIBAND, PUNCH and WIDTH)
        for (auto& id : MasterModules::ids (m)) ids.add (id);
    return ids;
}

//==============================================================================
/*
    Each module's own presets (its Pr button): they change only that module's
    settings - anything of the module's a preset doesn't mention goes back to
    its default, everything else in the chain stays as it is.
*/
namespace MasterModules
{
    enum { tamer = 0, eq, comp, limiter, multiband, punch, width, count };
    inline const char* names[count] { "Resonance Tamer", "EQ", "Compressor", "Limiter", "Multiband", "Punch", "Width" };

    /** The settings each module's presets (and its INIT) cover. */
    inline juce::StringArray ids (int module)
    {
        using namespace MasterIDs;
        if (module == tamer)   return { tamerOn, tamerMode, tMidAmount, tMidSens, tMidSharp, tSideAmount, tSideSens, tSideSharp, tSpeed, tFrom, tTo };
        if (module == comp)    return { compOn, compMode, midThreshold, midRatio, midAttack, midRelease, midMakeup,
                                        sideThreshold, sideRatio, sideAttack, sideRelease, sideMakeup, compMix, ignoreBelow, reactsTo, character };
        if (module == limiter) return { limitGain, ceiling, releaseMode, release };
        if (module == punch)   return { MasterPunchIDs::on, MasterPunchIDs::mode, MasterPunchIDs::punch[0], MasterPunchIDs::sustain[0],
                                        MasterPunchIDs::punch[1], MasterPunchIDs::sustain[1], MasterPunchIDs::mix };
        if (module == width)   return { MasterWidthIDs::on, MasterWidthIDs::width, MasterWidthIDs::monoBass };
        if (module == multiband)
        {
            juce::StringArray list { MasterMBIDs::on, MasterMBIDs::mode };
            for (int part = 0; part < 2; ++part)
                for (int b = 0; b < 4; ++b)
                    for (auto* id : { MasterMBIDs::threshold[part][b], MasterMBIDs::ratio[part][b], MasterMBIDs::speed[part][b], MasterMBIDs::gain[part][b] })
                        list.add (id);
            for (int b = 0; b < 4; ++b) list.add (MasterMBIDs::bandOn[b]);
            for (auto* id : MasterMBIDs::split) list.add (id);
            return list;
        }
        juce::StringArray list { MasterEQIDs::on, MasterEQIDs::mode };
        for (int part = 0; part < 2; ++part)
        {
            for (int b = 0; b < 5; ++b) { list.add (MasterEQIDs::freq[part][b]); list.add (MasterEQIDs::gain[part][b]); }
            list.add (MasterEQIDs::lowCut[part]);
        }
        return list;
    }

    inline Preset tamerPreset (const char* name, const char* tip, int midSide, float mAmount, float mSens, float mSharp,
                               float sAmount, float sSens, float sSharp, float speed, float from, float to)
    {
        using namespace MasterIDs;
        return { "", name, tip, { { tamerOn, 1.0f }, { tamerMode, (float) midSide }, { tMidAmount, mAmount }, { tMidSens, mSens }, { tMidSharp, mSharp },
                                  { tSideAmount, sAmount }, { tSideSens, sSens }, { tSideSharp, sSharp }, { tSpeed, speed }, { tFrom, from }, { tTo, to } } };
    }

    /** gains / freqs: the five bands (freq 0 = the band's own default); lowCut 20 = off. */
    inline Preset eqPreset (const char* name, const char* tip, int midSide,
                            std::array<float, 5> midGain, std::array<float, 5> midFreq, float midLowCut,
                            std::array<float, 5> sideGain = {}, std::array<float, 5> sideFreq = {}, float sideLowCut = 20.0f)
    {
        Preset p { "", name, tip, { { MasterEQIDs::on, 1.0f }, { MasterEQIDs::mode, (float) midSide } } };
        for (int part = 0; part < 2; ++part)
        {
            const auto& g = part == 0 ? midGain : sideGain;
            const auto& f = part == 0 ? midFreq : sideFreq;
            for (int b = 0; b < 5; ++b)
            {
                p.values.push_back ({ juce::String (MasterEQIDs::gain[part][b]), g[(size_t) b] });
                p.values.push_back ({ juce::String (MasterEQIDs::freq[part][b]), f[(size_t) b] > 0.0f ? f[(size_t) b] : MasterEQIDs::bands[b].defaultHz });
            }
            p.values.push_back ({ juce::String (MasterEQIDs::lowCut[part]), part == 0 ? midLowCut : sideLowCut });
        }
        return p;
    }

    inline Preset compPreset (const char* name, const char* tip, int midSide, std::array<float, 5> mid, std::array<float, 5> side,
                              float mix, float ignore, int reactsChoice, int characterChoice)
    {
        using namespace MasterIDs;
        return { "", name, tip, { { compOn, 1.0f }, { compMode, (float) midSide },
                                  { midThreshold, mid[0] }, { midRatio, mid[1] }, { midAttack, mid[2] }, { midRelease, mid[3] }, { midMakeup, mid[4] },
                                  { sideThreshold, side[0] }, { sideRatio, side[1] }, { sideAttack, side[2] }, { sideRelease, side[3] }, { sideMakeup, side[4] },
                                  { compMix, mix }, { ignoreBelow, ignore }, { reactsTo, (float) reactsChoice }, { character, (float) characterChoice } } };
    }

    inline Preset limiterPreset (const char* name, const char* tip, float gain, float ceilingDb, int releaseModeValue, float releaseMs)
    {
        using namespace MasterIDs;
        return { "", name, tip, { { limitGain, gain }, { ceiling, ceilingDb }, { releaseMode, (float) releaseModeValue }, { release, releaseMs } } };
    }

    /** A band: threshold, ratio, speed (%), gain. */
    struct MB { float t = 0.0f, r = 2.0f, s = 50.0f, g = 0.0f; };
    inline Preset mbPreset (const char* name, const char* tip, int midSide, std::array<MB, 4> mid, std::array<MB, 4> side,
                            std::array<float, 3> splits = { 120.0f, 1000.0f, 6000.0f })
    {
        Preset p { "", name, tip, { { MasterMBIDs::on, 1.0f }, { MasterMBIDs::mode, (float) midSide } } };
        for (int part = 0; part < 2; ++part)
            for (int b = 0; b < 4; ++b)
            {
                const auto& v = (part == 0 ? mid : side)[(size_t) b];
                p.values.push_back ({ juce::String (MasterMBIDs::threshold[part][b]), v.t });
                p.values.push_back ({ juce::String (MasterMBIDs::ratio[part][b]), v.r });
                p.values.push_back ({ juce::String (MasterMBIDs::speed[part][b]), v.s });
                p.values.push_back ({ juce::String (MasterMBIDs::gain[part][b]), v.g });
            }
        for (int b = 0; b < 4; ++b) p.values.push_back ({ juce::String (MasterMBIDs::bandOn[b]), 1.0f });
        for (int sp = 0; sp < 3; ++sp) p.values.push_back ({ juce::String (MasterMBIDs::split[sp]), splits[(size_t) sp] });
        return p;
    }

    inline Preset punchPreset (const char* name, const char* tip, int midSide, float mp, float ms, float sp, float ss, float mix)
    {
        using namespace MasterPunchIDs;
        return { "", name, tip, { { on, 1.0f }, { mode, (float) midSide }, { MasterPunchIDs::punch[0], mp }, { sustain[0], ms }, { MasterPunchIDs::punch[1], sp }, { sustain[1], ss },
                                  { MasterPunchIDs::mix, mix } } };
    }
    inline Preset widthPreset (const char* name, const char* tip, float w, float mono)
    {
        return { "", name, tip, { { MasterWidthIDs::on, 1.0f }, { MasterWidthIDs::width, w }, { MasterWidthIDs::monoBass, mono } } };
    }

    inline std::vector<Preset> presets (int module)
    {
        if (module == punch)
            return {
                punchPreset ("Bring Back the Snap", "The attack of kick and snare brought back after all the levelling: +20% PUNCH.", 0, 20, 0, 20, 0, 100),
                punchPreset ("Drums Forward (M/S)", "More punch in the middle (kick, snare), the sides left as they are.", 1, 30, 0, 0, 0, 100),
                punchPreset ("Punch & Tight Tails", "Sharper hits with shorter tails: a tighter, drier, more modern master.", 0, 20, -15, 20, -15, 100),
                punchPreset ("Softer Attacks", "Takes the edge off spiky hits and picks: smoother, rounder, less clicky.", 0, -20, 0, -20, 0, 100),
                punchPreset ("More Room (M/S)", "Longer tails at the sides: more room and space around the mix.", 1, 0, 0, 0, 25, 100),
                punchPreset ("Gentle Lift (50% mix)", "+40% PUNCH blended in at half: a subtle, natural lift.", 0, 40, 0, 40, 0, 50),
            };
        if (module == width)
            return {
                widthPreset ("As It Is", "100%, no mono bass: the width doesn't change.", 100, 20),
                widthPreset ("A Little Wider (115%)", "The sides up a touch: a little more open, still natural.", 115, 20),
                widthPreset ("Wider (130%)", "Clearly wider. Check MONO now and then: nothing should disappear.", 130, 20),
                widthPreset ("Wide & Tight (130% + mono bass)", "Wider on top, the bass and kick mono below 120 Hz: big and solid.", 130, 120),
                widthPreset ("Mono Bass Only (120 Hz)", "The width as it is, everything under 120 Hz in the middle: a tight, focused low end.", 100, 120),
                widthPreset ("Narrower (80%)", "Pulls an over-wide mix in a little: more solid and centred.", 80, 20),
            };
        if (module == multiband)
        {
            const MB off { 0.0f, 1.0f, 50.0f, 0.0f };
            return {
                mbPreset ("Gentle Control", "Every band held in lightly (2:1, slow): the mix evened out across the spectrum, 1-2 dB each.", 0,
                          { MB { -20, 2, 60, 0 }, MB { -22, 2, 60, 0 }, MB { -24, 2, 55, 0 }, MB { -28, 2, 50, 0 } }, {}),
                mbPreset ("Tame Boomy Lows", "Only the low band (under 150 Hz) held down when a bass note or kick booms; the rest untouched.", 0,
                          { MB { -18, 3, 45, 0 }, off, off, off }, {}, { 150.0f, 1000.0f, 6000.0f }),
                mbPreset ("Control Harsh Highs", "The top bands caught when cymbals and vocals get sharp: fast and firm, the body left alone.", 0,
                          { off, off, MB { -26, 3, 25, 0 }, MB { -30, 3, 20, 0 } }, {}, { 120.0f, 1000.0f, 5000.0f }),
                mbPreset ("Heavy Rock Density", "Firm on the low and low-mid bands: guitars and drums packed solid, the top kept open.", 0,
                          { MB { -20, 3, 40, 0.5f }, MB { -22, 3, 40, 0 }, MB { -24, 2, 45, 0 }, MB { -28, 1.5f, 50, 0 } }, {}),
                mbPreset ("Tight Mono Lows (M/S)", "The sides' low band held right down (heavy bass kept mono and tight); the middle breathes.", 1,
                          { MB { -18, 1.5f, 60, 0 }, MB { -22, 1.5f, 60, 0 }, MB { -24, 1.5f, 55, 0 }, MB { -28, 1.5f, 50, 0 } },
                          { MB { -36, 6, 30, -2 }, MB { -26, 2, 50, 0 }, MB { -26, 2, 50, 0 }, MB { -30, 1.5f, 50, 0 } }),
                mbPreset ("Smooth Wide Highs (M/S)", "Harsh, wide highs at the sides smoothed; the middle's top (vocal, snare) left bright.", 1,
                          { off, off, off, off },
                          { off, MB { -26, 2, 50, 0 }, MB { -28, 3, 30, 0 }, MB { -32, 3, 25, 0 } }),
            };
        }
        if (module == tamer)
            return {
                tamerPreset ("Gentle Polish", "Only the spots that clearly stick out, gently: a little smoother, nothing more.", 0, 3, 45, 50, 3, 45, 50, 50, 200, 16000),
                tamerPreset ("Harsh Top End", "Works on the 2-16 kHz harshness only: cymbals, guitars and vocal edge smoothed when they bite.", 0, 6, 60, 50, 6, 60, 50, 40, 2000, 16000),
                tamerPreset ("Boxy Mids", "The 300 Hz - 1.5 kHz honk and box of a room or a busy mix, broad and slow.", 0, 5, 55, 35, 5, 55, 35, 60, 300, 1500),
                tamerPreset ("Ringing Notes & Whistles", "Narrow, ringing resonances (a snare's ring, a whistling guitar), caught fast.", 0, 6, 55, 85, 6, 55, 85, 20, 200, 16000),
                tamerPreset ("AI Fizz", "The fizzy, metallic top end of AI-made songs (Mureka, Suno), from 4 kHz up.", 0, 6, 65, 55, 6, 65, 55, 30, 4000, 16000),
                tamerPreset ("Wide Harsh Synths (M/S)", "Harsh, wide synths and guitars at the sides tamed hard; the middle only touched lightly.", 1, 2, 45, 50, 6, 65, 45, 50, 1000, 16000),
            };
        if (module == eq)
            return {
                eqPreset ("Flat", "Every band at 0 dB, no low cut: the EQ does nothing.", 0, {}, {}, 20.0f),
                eqPreset ("Tighten & Brighten (M/S)", "Tight, mono lows (side low cut 120 Hz), a little mud out of the middle, air on the sides.", 1,
                          { 0.5f, -1.5f, 0.0f, 0.5f, 0.5f }, { 60.0f, 300.0f, 0.0f, 3500.0f, 12000.0f }, 20.0f,
                          { 0.0f, -1.0f, 0.0f, 1.0f, 2.5f }, { 0.0f, 350.0f, 0.0f, 5000.0f, 11000.0f }, 120.0f),
                eqPreset ("Heavy Low End (M/S)", "Weight for the kick and bass in the middle, the lows kept mono and tight.", 1,
                          { 2.0f, -1.0f, 0.0f, 0.0f, 0.5f }, { 70.0f, 280.0f, 0.0f, 0.0f, 12000.0f }, 20.0f,
                          { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f, 11000.0f }, 150.0f),
                eqPreset ("Mono Bass Only (M/S)", "Nothing but a low cut on the sides (120 Hz): the bass and kick mono and focused.", 1,
                          {}, {}, 20.0f, {}, {}, 120.0f),
                eqPreset ("Clear the Mud", "A broad, gentle dip in the low mids (250 Hz): clearer, less woolly.", 0,
                          { 0.0f, -2.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 250.0f, 0.0f, 0.0f, 0.0f }, 20.0f),
                eqPreset ("Gentle Air", "A soft lift of the very top (12 kHz shelf): more open, never harsh.", 0,
                          { 0.0f, 0.0f, 0.0f, 0.0f, 1.5f }, { 0.0f, 0.0f, 0.0f, 0.0f, 12000.0f }, 20.0f),
                eqPreset ("Warm & Smooth", "A little low shelf warmth, the upper mids eased: for bright, edgy mixes.", 0,
                          { 1.0f, 0.0f, 0.0f, -1.0f, -0.5f }, { 100.0f, 0.0f, 0.0f, 3500.0f, 10000.0f }, 20.0f),
            };
        if (module == comp)
            return {
                compPreset ("Glue 1.5:1", "Barely there: holds the mix together. Aim for 1-2 dB.", 0, { -16, 1.5f, 30, 250, 1 }, { -16, 1.5f, 30, 250, 1 }, 100, 80, 1, 0),
                compPreset ("Glue 2:1", "The usual master glue: 2:1, slow, ignoring the deep bass. Aim for 1-3 dB.", 0, { -14, 2, 30, 200, 1 }, { -14, 2, 30, 200, 1 }, 100, 80, 1, 0),
                compPreset ("Rock & Punk", "Firmer and faster: dense and in your face, the snare crack let through first.", 0, { -14, 3, 30, 120, 2 }, { -14, 3, 30, 120, 2 }, 100, 90, 0, 0),
                compPreset ("Heavy & Loud", "VINTAGE weight, 85% MIX to keep some of the hits' snap.", 0, { -16, 3, 20, 100, 2 }, { -16, 3, 20, 100, 2 }, 85, 100, 0, 1),
                compPreset ("Jazz Natural", "1.5:1, slow and gentle: the players' dynamics kept.", 0, { -20, 1.5f, 50, 300, 0.5f }, { -20, 1.5f, 50, 300, 0.5f }, 100, 60, 1, 0),
                compPreset ("Tighten the Sides (M/S)", "The sides held in, the middle almost untouched.", 1, { -14, 1.5f, 30, 200, 0.5f }, { -22, 3, 15, 150, 0 }, 100, 80, 1, 0),
                compPreset ("Punchy Middle (M/S)", "A slow attack on the middle so kick, snare and vocal punch through; the sides smoothed.", 1, { -14, 2.5f, 40, 120, 1.5f }, { -18, 1.5f, 30, 250, 0.5f }, 100, 90, 0, 0),
                compPreset ("Steady Vocal & Bass (M/S)", "The centre (vocal, bass, kick) evened out; the stereo picture left alone.", 1, { -18, 2.5f, 15, 150, 1.5f }, { -20, 1.2f, 30, 250, 0 }, 100, 40, 1, 0),
            };
        return {
            limiterPreset ("Safety Ceiling Only", "Nothing until a peak would pass -0.3 dBTP: overs protection only.", 0, -0.3f, 0, 100),
            limiterPreset ("Gentle Push (+2 dB)", "Just the tallest peaks shaved: a little louder, no audible change.", 2, -0.3f, 0, 100),
            limiterPreset ("Firm Push (+4 dB)", "Louder, still clean on most mixes. Listen to the kick and snare.", 4, -0.3f, 0, 100),
            limiterPreset ("Loud (+6 dB)", "Loud rock, punk and electronic masters. Check with MATCH.", 6, -0.3f, 0, 100),
            limiterPreset ("Dense & Steady (slow)", "A slow, steady release: smooth for synthwave, pads and ballads.", 4, -0.3f, 1, 400),
            limiterPreset ("For MP3 / Streaming (-1 dBTP)", "A -1 dBTP ceiling: room for the peaks a conversion adds.", 2, -1.0f, 0, 100),
        };
    }
}
