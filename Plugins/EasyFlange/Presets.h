#pragma once

#include "PresetBrowser.h"

//==============================================================================
/*
    Parameter IDs: the names the plugin uses internally, and that your DAW
    stores in saved projects. Don't rename these once you've used the plugin
    in a project, or that project won't find its settings again.
*/
namespace FlangeIDs
{
    inline constexpr const char* type     = "type";       // FLANGER / CHORUS / VIBRATO
    inline constexpr const char* rate     = "rate";       // Hz, when SYNC is OFF
    inline constexpr const char* sync     = "sync";       // OFF or TEMPO
    inline constexpr const char* note     = "note";       // one swing as a note, when SYNC is TEMPO
    inline constexpr const char* depth    = "depth";      // %
    inline constexpr const char* feedback = "feedback";   // % (-90 .. +90, FLANGER only)
    inline constexpr const char* width    = "width";      // %
    inline constexpr const char* mix      = "mix";        // %
}

/** One full swing as a note when SYNC is TEMPO, shortest first. (Never reorder.) */
namespace FlangeNotes
{
    struct Length { const char* name; double beats; };

    inline const Length lengths[] =
    {
        { "1/16 note", 0.25 },
        { "1/8 note",  0.5 },
        { "1/4 note",  1.0 },
        { "1/2 note",  2.0 },
        { "1 bar",     4.0 },
        { "2 bars",    8.0 },
        { "4 bars",    16.0 },
        { "8 bars",    32.0 },
    };

    inline constexpr int count = (int) (sizeof (lengths) / sizeof (lengths[0]));

    enum { n16 = 0, n8, n4, n2, bar1, bar2, bar4, bar8 };
}

//==============================================================================
/*
    The presets. TYPE 0 = FLANGER, 1 = CHORUS, 2 = VIBRATO. SYNC 1 uses the
    NOTE column (one full swing) locked to the beat; SYNC 0 uses RATE (Hz).
*/
struct FlangePreset
{
    const char* category;
    const char* name;
    int   type;
    int   sync;
    float rateHz;
    int   note;
    float depthPercent;
    float feedbackPercent;
    float widthPercent;
    float mixPercent;
    const char* tip;
};

inline const FlangePreset flangePresets[] =
{
    //  category, name                                   type sync  rate  note                depth    fb  width   mix
    { "", "Default settings",                              0, 0,  0.30f, FlangeNotes::bar2,   70.0f, 50.0f, 50.0f,  50.0f,
      "A classic flanger: a slow jet-plane sweep. Try CHORUS for a wide, shimmering double, or VIBRATO for a pitch wobble." },

    // ---- Guitars ---------------------------------------------------------------
    { "Guitars", "Guitar - Jet Flanger",                   0, 0,  0.20f, FlangeNotes::bar2,   85.0f, 70.0f, 40.0f,  50.0f,
      "The big, slow jet-plane whoosh on a distorted guitar: 70s and 80s hard rock." },
    { "Guitars", "Guitar - 80s Clean Chorus",              1, 0,  0.80f, FlangeNotes::n4,     55.0f,  0.0f, 80.0f,  50.0f,
      "Shimmering, wide clean guitar: the 80s rock ballad and new wave sound." },
    { "Guitars", "Guitar - Subtle Thickener",              1, 0,  0.40f, FlangeNotes::n4,     30.0f,  0.0f, 60.0f,  30.0f,
      "Barely-there chorus that makes one guitar sound like a double-tracked pair." },
    { "Guitars", "Guitar - Sweep in Time",                 0, 1,  0.25f, FlangeNotes::bar2,   80.0f, 60.0f, 50.0f,  50.0f,
      "A flanger sweep every two bars, locked to the song: builds and breakdowns." },
    { "Guitars", "Guitar - Metallic Flange",               0, 0,  0.50f, FlangeNotes::n4,     60.0f, 85.0f, 30.0f,  50.0f,
      "High feedback: a ringing, metallic, almost robotic flange. Industrial and cyberpunk." },

    // ---- Bass ------------------------------------------------------------------
    { "Bass", "Bass - Chorus",                             1, 0,  0.60f, FlangeNotes::n4,     45.0f,  0.0f, 50.0f,  40.0f,
      "The fretless / 80s chorus bass sound: lush on top, with the low end still solid." },
    { "Bass", "Bass - Slow Flange",                        0, 0,  0.15f, FlangeNotes::bar4,   60.0f, 40.0f, 20.0f,  40.0f,
      "A gentle, slow flange on bass: movement in post-punk and new wave lines." },

    // ---- Keys & Synths ---------------------------------------------------------
    { "Keys & Synths", "Electric Piano - Chorus",          1, 0,  0.70f, FlangeNotes::n4,     50.0f,  0.0f, 90.0f,  45.0f,
      "Wide, glassy chorus on electric piano: the classic 70s / 80s keys sound." },
    { "Keys & Synths", "Synth Pad - Wide Chorus",          1, 0,  0.30f, FlangeNotes::n4,     65.0f,  0.0f, 100.0f, 50.0f,
      "A huge, wide, slowly moving pad: synthwave and ambient." },
    { "Keys & Synths", "Organ - Leslie Warble",            2, 0,  6.00f, FlangeNotes::n8,     35.0f,  0.0f, 70.0f, 100.0f,
      "A fast, wide pitch warble - the rotating-speaker sound on organ." },
    { "Keys & Synths", "Synth - Tape Warble",              2, 0,  0.80f, FlangeNotes::n4,     25.0f,  0.0f, 20.0f, 100.0f,
      "A slow, gentle pitch drift like an old tape: lo-fi and synthwave leads." },

    // ---- Vocals ----------------------------------------------------------------
    { "Vocals", "Vocal - Wide Double",                     1, 0,  0.50f, FlangeNotes::n4,     35.0f,  0.0f, 100.0f, 35.0f,
      "Makes a vocal sound wider and doubled, without an obvious effect." },
    { "Vocals", "Vocal - Vibrato",                         2, 0,  5.50f, FlangeNotes::n8,     25.0f,  0.0f,  0.0f, 100.0f,
      "Adds a singer's vibrato to a straight note. Use it on held notes." },

    // ---- Drums -----------------------------------------------------------------
    { "Drums", "Overheads - Jet Sweep",                    0, 1,  0.25f, FlangeNotes::bar4,   75.0f, 55.0f, 50.0f,  40.0f,
      "Cymbals whooshing slowly over four bars: the classic flanged-drums trick." },
};

//==============================================================================
inline std::vector<Preset> makeFlangePresets()
{
    std::vector<Preset> presets;

    for (auto& p : flangePresets)
        presets.push_back ({ p.category, p.name, p.tip,
                             { { FlangeIDs::type, (float) p.type }, { FlangeIDs::sync, (float) p.sync }, { FlangeIDs::rate, p.rateHz },
                               { FlangeIDs::note, (float) p.note }, { FlangeIDs::depth, p.depthPercent }, { FlangeIDs::feedback, p.feedbackPercent },
                               { FlangeIDs::width, p.widthPercent }, { FlangeIDs::mix, p.mixPercent } } });

    return presets;
}

/** The settings presets store (everything except BYPASS). */
inline juce::StringArray flangePresetIDs()
{
    return { FlangeIDs::type, FlangeIDs::sync, FlangeIDs::rate, FlangeIDs::note, FlangeIDs::depth,
             FlangeIDs::feedback, FlangeIDs::width, FlangeIDs::mix };
}
