#pragma once

#include "PresetBrowser.h"

//==============================================================================
/*
    Parameter IDs: the names the plugin uses internally, and that your DAW
    stores in saved projects. Don't rename these once you've used the plugin
    in a project, or that project won't find its settings again.
*/
namespace GateIDs
{
    inline constexpr const char* threshold = "threshold";
    inline constexpr const char* range     = "range";
    inline constexpr const char* attack    = "attack";
    inline constexpr const char* hold      = "hold";
    inline constexpr const char* release   = "release";
    inline constexpr const char* lowCut    = "lowcut";     // IGNORE BELOW
    inline constexpr const char* highCut   = "highcut";    // IGNORE ABOVE
    inline constexpr const char* openedBy  = "openedby";   // THIS TRACK or SIDECHAIN
}

/** OPENED BY: the two choices (in this order). */
enum OpenedBy { thisTrack = 0, sidechain = 1 };

//==============================================================================
/*
    The presets.

    They're starting points: every recording is different. After choosing
    one, play your track and watch the display - move THRESHOLD so the line
    sits above the bleed or noise between the hits, and below the hits.

    To add your own: copy a line, then change the category (which sub-menu
    it's in), the name, the numbers and the tip. (You can also save presets
    from the plugin's menu without touching any code.)

    RANGE 80 means FULL (silence while closed). IGNORE BELOW 20 and IGNORE
    ABOVE 20000 mean off. Presets don't touch BYPASS or OPENED BY (which
    depends on how your tracks are routed): a preset that needs a sidechain
    says so in its tip.
*/
struct GatePreset
{
    const char* category;
    const char* name;
    float thresholdDb;   // -80 .. 0
    float rangeDb;       //   0 .. 80 (80 = FULL)
    float attackMs;      // 0.1 .. 50
    float holdMs;        //   0 .. 2000
    float releaseMs;     //   5 .. 2000
    float lowCutHz;      //  20 (off) .. 5000      IGNORE BELOW
    float highCutHz;     //  50 .. 20000 (off)     IGNORE ABOVE
    const char* tip;
};

inline const GatePreset gatePresets[] =
{
    //  category, name                                   thresh  range  attack   hold  release  ignore below  ignore above

    { "", "Default settings",                            -40.0f, 40.0f,  0.5f,  50.0f, 150.0f,     20.0f, 20000.0f,
      "Play your track and watch the display. Set THRESHOLD between the sound you want and the noise or bleed you don't." },

    // ---- Drums -----------------------------------------------------------------
    { "Drums", "Kick - Natural",                         -24.0f, 20.0f,  0.3f,  50.0f, 150.0f,     20.0f,  5000.0f,
      "Tidies up the kick mic between hits but keeps its natural ring. Set THRESHOLD so only the kick goes over the line." },
    { "Drums", "Kick - Tight",                           -22.0f, 35.0f,  0.3f,  30.0f,  90.0f,     20.0f,  5000.0f,
      "A short, punchy kick with the bleed between hits mostly gone, for fast rock and metal. Shorter RELEASE = tighter still." },
    { "Drums", "Snare - Natural",                        -20.0f, 18.0f,  0.3f,  80.0f, 200.0f,    200.0f, 20000.0f,
      "Turns down kick and hi-hat bleed between snare hits but keeps the ring. IGNORE BELOW stops the kick opening it." },
    { "Drums", "Snare - Tight & Dry",                    -18.0f, 30.0f,  0.3f,  30.0f,  90.0f,    200.0f, 20000.0f,
      "A short, dry snare with the bleed between hits mostly gone. If ghost notes go missing, lower THRESHOLD." },
    { "Drums", "Toms - Natural",                         -20.0f, 20.0f,  0.5f, 150.0f, 400.0f,     20.0f,  3000.0f,
      "Quietens the tom mics between fills but lets each hit ring out. IGNORE ABOVE stops cymbals and hi-hat opening them." },
    { "Drums", "Toms - Tight & Clean",                   -20.0f, 40.0f,  0.5f,  60.0f, 200.0f,     20.0f,  3000.0f,
      "Clean, controlled toms: the rest of the kit is gone between fills, and each hit is a little shorter." },
    { "Drums", "Room Mics - 80s Gated Snare",            -20.0f, 80.0f,  0.5f, 300.0f,  30.0f,    200.0f, 20000.0f,
      "The huge 80s snare: the room booms, then stops dead. Send the snare's close mic to the sidechain, OPENED BY: SIDECHAIN." },

    // ---- Guitars ---------------------------------------------------------------
    { "Guitars", "Electric Guitar - Amp Noise",          -40.0f, 25.0f,  0.5f,  50.0f, 120.0f,    150.0f, 20000.0f,
      "Removes hiss and hum when you're not playing. Set THRESHOLD just above the noise in the gaps (watch the display)." },
    { "Guitars", "Distorted Guitar - Tight Stops",       -38.0f, 80.0f,  0.3f,  20.0f,  60.0f,    150.0f, 20000.0f,
      "Total silence in the stops, for tight high-gain rhythm parts in rock and metal. Set THRESHOLD just above the amp noise." },
    { "Guitars", "Clean Guitar - Hum Between Parts",     -46.0f, 18.0f,  1.0f, 150.0f, 300.0f,    150.0f, 20000.0f,
      "Gently lowers amp hum and buzz between parts, and closes slowly so the last notes ring out." },

    // ---- Bass ------------------------------------------------------------------
    { "Bass", "Bass - Amp Hum",                          -47.0f, 20.0f,  1.0f,  80.0f, 150.0f,     20.0f, 20000.0f,
      "Lowers amp hum and noise between notes and phrases. Set THRESHOLD just above the hum in the gaps." },

    // ---- Vocals ----------------------------------------------------------------
    { "Vocals", "Vocal - Gentle Clean-Up",               -46.0f, 12.0f,  1.0f, 200.0f, 300.0f,     20.0f, 20000.0f,
      "Lowers room noise between phrases without making the gaps sound dead. If the start of a line gets clipped, lower THRESHOLD." },
    { "Vocals", "Vocal - Remove Headphone Bleed",        -45.0f, 30.0f,  1.0f, 250.0f, 300.0f,     20.0f, 20000.0f,
      "For a click or backing track leaking from the headphones between phrases. Set THRESHOLD just above the leak." },
    { "Vocals", "Speech - Room Noise",                   -47.0f, 12.0f,  2.0f, 250.0f, 300.0f,     20.0f, 20000.0f,
      "For podcasts and voice-overs: lowers room noise between sentences and keeps the voice natural." },

    // ---- Sidechain tricks (another track opens the gate) ----------------------
    { "Sidechain Tricks", "Bass - Locked to the Kick",   -24.0f, 15.0f,  0.5f,  40.0f, 100.0f,     20.0f,  5000.0f,
      "Send the kick to the sidechain, OPENED BY: SIDECHAIN. The bass dips between kick hits, so the two lock together." },
    { "Sidechain Tricks", "Synth Pad - Chopped by a Rhythm", -30.0f, 80.0f, 0.5f,  20.0f,  50.0f,     20.0f, 20000.0f,
      "The trance-gate effect: send a hi-hat or rhythm part to the sidechain, OPENED BY: SIDECHAIN. The pad plays only on its hits." },
};

//==============================================================================
inline std::vector<Preset> makeGatePresets()
{
    std::vector<Preset> presets;

    for (auto& p : gatePresets)
        presets.push_back ({ p.category, p.name, p.tip,
                             { { GateIDs::threshold, p.thresholdDb }, { GateIDs::range,   p.rangeDb },
                               { GateIDs::attack,    p.attackMs },    { GateIDs::hold,    p.holdMs },
                               { GateIDs::release,   p.releaseMs },   { GateIDs::lowCut,  p.lowCutHz },
                               { GateIDs::highCut,   p.highCutHz } } });

    return presets;
}

/** The settings presets store (everything except BYPASS and OPENED BY). */
inline juce::StringArray gatePresetIDs()
{
    return { GateIDs::threshold, GateIDs::range, GateIDs::attack, GateIDs::hold,
             GateIDs::release, GateIDs::lowCut, GateIDs::highCut };
}
