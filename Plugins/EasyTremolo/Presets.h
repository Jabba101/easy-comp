#pragma once

#include "PresetBrowser.h"

//==============================================================================
/*
    Parameter IDs: the names the plugin uses internally, and that your DAW
    stores in saved projects. Don't rename these once you've used the plugin
    in a project, or that project won't find its settings again.
*/
namespace TremoloIDs
{
    inline constexpr const char* rate  = "rate";     // Hz, when SYNC is OFF
    inline constexpr const char* sync  = "sync";     // OFF or TEMPO
    inline constexpr const char* note  = "note";     // one cycle as a note, when SYNC is TEMPO
    inline constexpr const char* depth = "depth";    // %
    inline constexpr const char* shape = "shape";    // SINE / TRIANGLE / SQUARE / PUMP
    inline constexpr const char* mode  = "mode";     // TREMOLO / AUTO-PAN
}

/** One cycle as a note when SYNC is TEMPO, shortest first. (Never reorder
    these: projects store the position in this list.) */
namespace TremoloNotes
{
    struct Length { const char* name; double beats; };   // beats = quarter notes

    inline const Length lengths[] =
    {
        { "1/32 note",    0.125 },
        { "1/16 triplet", 1.0 / 6.0 },
        { "1/16 note",    0.25 },
        { "1/8 triplet",  1.0 / 3.0 },
        { "dotted 1/16",  0.375 },
        { "1/8 note",     0.5 },
        { "1/4 triplet",  2.0 / 3.0 },
        { "dotted 1/8",   0.75 },
        { "1/4 note",     1.0 },
        { "1/2 triplet",  4.0 / 3.0 },
        { "dotted 1/4",   1.5 },
        { "1/2 note",     2.0 },
        { "1 bar",        4.0 },
        { "2 bars",       8.0 },
    };

    inline constexpr int count = (int) (sizeof (lengths) / sizeof (lengths[0]));

    enum { n32 = 0, n16T, n16, n8T, n16D, n8, n4T, n8D, n4, n2T, n4D, n2, bar1, bar2 };
}

//==============================================================================
/*
    The presets.

    SYNC 1 (TEMPO) uses the NOTE column (one full cycle: dip and back) and
    stays locked to the beat; SYNC 0 uses RATE in Hz. SHAPE 0 SINE,
    1 TRIANGLE, 2 SQUARE, 3 PUMP. MODE 0 TREMOLO, 1 AUTO-PAN.
*/
struct TremoloPreset
{
    const char* category;
    const char* name;
    int   sync;
    float rateHz;
    int   note;
    float depthPercent;
    int   shape;
    int   mode;
    const char* tip;
};

inline const TremoloPreset tremoloPresets[] =
{
    //  category, name                                     sync  rate  note                 depth shape mode
    { "", "Default settings",                                1,  4.0f, TremoloNotes::n8,    50.0f, 0, 0,
      "A smooth eighth-note pulse, locked to your song. Turn DEPTH up for a deeper throb, or try AUTO-PAN." },

    // ---- Guitars ---------------------------------------------------------------
    { "Guitars", "Guitar - Vintage Amp Tremolo",             0,  5.5f, TremoloNotes::n8,    55.0f, 0, 0,
      "The tremolo built into old valve amps: a smooth, throbbing pulse. Surf, blues, Americana, spaghetti western." },
    { "Guitars", "Guitar - Surf Shimmer",                    0,  7.5f, TremoloNotes::n16,   60.0f, 1, 0,
      "Fast and choppy-smooth: reverb-soaked surf guitar and twangy 60s lines." },
    { "Guitars", "Guitar - Hard Chop",                       1,  8.0f, TremoloNotes::n16,   95.0f, 2, 0,
      "The guitar chopped on and off in sixteenths, locked to the beat: a stuttering, machine-gun rhythm." },
    { "Guitars", "Guitar - Slow Swirl",                      1,  1.0f, TremoloNotes::n2,    60.0f, 0, 1,
      "The guitar drifting slowly from side to side across the speakers. Psychedelic and dreamy." },

    // ---- Keys & Synths ---------------------------------------------------------
    { "Keys & Synths", "Electric Piano - Classic Auto-Pan",  0,  4.0f, TremoloNotes::n8,    70.0f, 0, 1,
      "The suitcase electric piano bouncing left and right: soul, jazz-funk, smooth 70s." },
    { "Keys & Synths", "Organ - Gentle Pulse",               0,  3.0f, TremoloNotes::n8,    30.0f, 0, 0,
      "A gentle, slow pulse on organ or pads - movement without an obvious effect." },
    { "Keys & Synths", "Synth Pad - Trance Gate",            1,  8.0f, TremoloNotes::n16,  100.0f, 2, 0,
      "A held pad chopped into sixteenths in time with the song: trance, EDM, synthwave builds." },
    { "Keys & Synths", "Synth Pad - Pumping",                1,  2.0f, TremoloNotes::n4,    60.0f, 3, 0,
      "Dips on every beat and swells back: the pumping sidechain sound, without needing a kick sent to it." },
    { "Keys & Synths", "Synth Pad - Slow Wide Pan",          1,  0.5f, TremoloNotes::bar1,  60.0f, 0, 1,
      "A pad sweeping slowly across the speakers once a bar: wide and moving behind everything." },
    { "Keys & Synths", "Synth - Darksynth Stutter",          1, 16.0f, TremoloNotes::n32,   80.0f, 2, 0,
      "A fast, aggressive thirty-second-note stutter: menacing darksynth and cyberpunk lines." },

    // ---- Vocals ----------------------------------------------------------------
    { "Vocals", "Vocal - Wobble",                            0,  6.0f, TremoloNotes::n8,    30.0f, 0, 0,
      "A subtle, fast wobble on the voice: dreamy, vintage, a little uneasy." },
    { "Vocals", "Vocal - Side to Side",                      1,  2.0f, TremoloNotes::n4,    40.0f, 0, 1,
      "A backing vocal or ad-lib moving from side to side in time with the song." },

    // ---- Drums -----------------------------------------------------------------
    { "Drums", "Hi-Hats - Wide Pan",                         1,  1.0f, TremoloNotes::n2,    50.0f, 0, 1,
      "Hats or shakers drifting gently left and right: movement and width without touching the groove." },
    { "Drums", "Drum Loop - Pumping",                        1,  2.0f, TremoloNotes::n4,    35.0f, 3, 0,
      "A loop or percussion pumping on the beat, the way a compressor pumps from the kick." },

    // ---- Mix Bus ---------------------------------------------------------------
    { "Mixing & Mastering", "Mix Bus - Gentle Pump",                    1,  2.0f, TremoloNotes::n4,    20.0f, 3, 0,
      "A gentle pump on the whole mix in time with the beat: synthwave, EDM and French house." },
};

//==============================================================================
inline std::vector<Preset> makeTremoloPresets()
{
    std::vector<Preset> presets;

    for (auto& p : tremoloPresets)
        presets.push_back ({ p.category, p.name, p.tip,
                             { { TremoloIDs::sync, (float) p.sync }, { TremoloIDs::rate, p.rateHz }, { TremoloIDs::note, (float) p.note },
                               { TremoloIDs::depth, p.depthPercent }, { TremoloIDs::shape, (float) p.shape }, { TremoloIDs::mode, (float) p.mode } } });

    return presets;
}

/** The settings presets store (everything except BYPASS). */
inline juce::StringArray tremoloPresetIDs()
{
    return { TremoloIDs::sync, TremoloIDs::rate, TremoloIDs::note, TremoloIDs::depth, TremoloIDs::shape, TremoloIDs::mode };
}
