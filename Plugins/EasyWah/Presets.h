#pragma once

#include "PresetBrowser.h"

//==============================================================================
/*
    Parameter IDs: the names the plugin uses internally, and that your DAW
    stores in saved projects. Don't rename these once you've used the plugin
    in a project, or that project won't find its settings again.
*/
namespace WahIDs
{
    inline constexpr const char* mode        = "mode";          // AUTO / TEMPO
    inline constexpr const char* sensitivity = "sensitivity";   // %
    inline constexpr const char* response    = "response";      // SNAPPY / SMOOTH / LAZY
    inline constexpr const char* direction   = "direction";     // UP / DOWN
    inline constexpr const char* note        = "note";          // TEMPO: one sweep
    inline constexpr const char* pedal       = "pedal";         // rest position, %
    inline constexpr const char* from        = "from";          // heel, Hz
    inline constexpr const char* to          = "to";            // toe, Hz
    inline constexpr const char* resonance   = "resonance";     // %
    inline constexpr const char* mix         = "mix";           // %
}

/** TEMPO: one sweep (back and forth) as a note, shortest first. (Never reorder.) */
namespace WahNotes
{
    struct Length { const char* name; double beats; };

    inline const Length lengths[] =
    {
        { "1/16 note",   0.25 },
        { "1/8 triplet", 1.0 / 3.0 },
        { "1/8 note",    0.5 },
        { "1/4 triplet", 2.0 / 3.0 },
        { "dotted 1/8",  0.75 },
        { "1/4 note",    1.0 },
        { "dotted 1/4",  1.5 },
        { "1/2 note",    2.0 },
        { "1 bar",       4.0 },
        { "2 bars",      8.0 },
    };

    inline constexpr int count = (int) (sizeof (lengths) / sizeof (lengths[0]));

    enum { n16 = 0, n8T, n8, n4T, n8D, n4, n4D, n2, bar1, bar2 };
}

//==============================================================================
/*
    The presets.

    MODE 0 = AUTO (follows your playing), 1 = TEMPO (rocks in time).
    RESPONSE 0 SNAPPY, 1 SMOOTH, 2 LAZY. DIRECTION 0 UP, 1 DOWN.
    SENSITIVITY depends on how loud your track is: these assume a track
    peaking around -12 to -6 dBFS. If the wah barely moves, turn it up.
*/
struct WahPreset
{
    const char* category;
    const char* name;
    int   mode;
    float sensitivityPercent;
    int   response;
    int   direction;
    int   note;
    float pedalPercent;
    float fromHz, toHz;
    float resonancePercent;
    float mixPercent;
    const char* tip;
};

inline const WahPreset wahPresets[] =
{
    //  category, name                                   mode  sens resp dir  note          pedal  from    to    reso   mix
    { "", "Default settings",                              0,  60.0f, 1, 0, WahNotes::n4,   0.0f, 400.0f, 2200.0f, 60.0f, 100.0f,
      "An automatic wah that follows your playing: play harder and it opens up. Set SENSITIVITY so the loudest notes open it fully." },

    // ---- Guitars ---------------------------------------------------------------
    { "Guitars", "Guitar - Funk Envelope",                 0,  70.0f, 0, 0, WahNotes::n4,   0.0f, 350.0f, 2500.0f, 70.0f, 100.0f,
      "Every strum goes 'wow': quick and quacky. 70s funk rhythm guitar - play it muted and choppy." },
    { "Guitars", "Guitar - Smooth Lead Wah",               0,  55.0f, 1, 0, WahNotes::n4,  10.0f, 450.0f, 2000.0f, 55.0f, 100.0f,
      "A vocal, singing wah that follows each note of a solo." },
    { "Guitars", "Guitar - Cocked Wah",                    0,   0.0f, 1, 0, WahNotes::n4,  45.0f, 400.0f, 2200.0f, 70.0f, 100.0f,
      "The pedal parked halfway and left there: a nasal, honky lead tone with no movement at all." },
    { "Guitars", "Guitar - Wah in Time",                   1,  85.0f, 1, 0, WahNotes::n8,   0.0f, 400.0f, 2200.0f, 65.0f, 100.0f,
      "The pedal rocking in eighth notes, locked to the song: 'wakka-wakka' rhythm guitar." },
    { "Guitars", "Guitar - Slow Psychedelic Sweep",        1,  90.0f, 1, 0, WahNotes::bar1, 0.0f, 350.0f, 2800.0f, 55.0f, 100.0f,
      "One slow sweep a bar, heel to toe and back: swirling 60s psychedelia." },

    // ---- Bass ------------------------------------------------------------------
    { "Bass", "Bass - Funk Envelope",                      0,  65.0f, 0, 0, WahNotes::n4,   0.0f, 180.0f, 1200.0f, 60.0f, 100.0f,
      "Slap or finger funk bass with an envelope filter: every note goes 'bwow'. Bootsy territory." },
    { "Bass", "Bass - Envelope Under Clean Lows",          0,  65.0f, 0, 0, WahNotes::n4,   0.0f, 200.0f, 1400.0f, 60.0f,  60.0f,
      "The envelope filter blended with your clean bass, so the low end stays solid underneath." },
    { "Bass", "Bass - Synth Filter Down",                  0,  60.0f, 1, 1, WahNotes::n4, 100.0f, 200.0f, 2000.0f, 65.0f, 100.0f,
      "Works backwards: each note starts dark and opens as it fades - a synth-bass sweep." },

    // ---- Keys & Synths ---------------------------------------------------------
    { "Keys & Synths", "Clavinet - Funky Wah",             0,  70.0f, 0, 0, WahNotes::n4,   0.0f, 400.0f, 3000.0f, 70.0f, 100.0f,
      "The 70s clavinet through an envelope filter: bright, quacking, rhythmic." },
    { "Keys & Synths", "Electric Piano - Auto Wah",        0,  55.0f, 1, 0, WahNotes::n4,   5.0f, 350.0f, 2000.0f, 50.0f, 100.0f,
      "A softer, rounder auto-wah on electric piano: jazz-funk and fusion." },
    { "Keys & Synths", "Synth - Rhythmic Sweep",           1,  80.0f, 1, 0, WahNotes::n8D,  0.0f, 300.0f, 3500.0f, 70.0f, 100.0f,
      "Dotted-eighth sweeps locked to the tempo on a held synth: movement without touching the filter." },

    // ---- Drums -----------------------------------------------------------------
    { "Drums", "Drum Loop - Filter Wobble",                1,  70.0f, 1, 0, WahNotes::n4,  10.0f, 300.0f, 3000.0f, 55.0f,  70.0f,
      "A breakbeat or loop sweeping in time - a dance and big-beat trick." },
};

//==============================================================================
inline std::vector<Preset> makeWahPresets()
{
    std::vector<Preset> presets;

    for (auto& p : wahPresets)
        presets.push_back ({ p.category, p.name, p.tip,
                             { { WahIDs::mode, (float) p.mode }, { WahIDs::sensitivity, p.sensitivityPercent },
                               { WahIDs::response, (float) p.response }, { WahIDs::direction, (float) p.direction },
                               { WahIDs::note, (float) p.note }, { WahIDs::pedal, p.pedalPercent },
                               { WahIDs::from, p.fromHz }, { WahIDs::to, p.toHz },
                               { WahIDs::resonance, p.resonancePercent }, { WahIDs::mix, p.mixPercent } } });

    return presets;
}

/** The settings presets store (everything except BYPASS and AUTO-GAIN). */
inline juce::StringArray wahPresetIDs()
{
    return { WahIDs::mode, WahIDs::sensitivity, WahIDs::response, WahIDs::direction, WahIDs::note,
             WahIDs::pedal, WahIDs::from, WahIDs::to, WahIDs::resonance, WahIDs::mix };
}
