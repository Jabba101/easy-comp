#pragma once

#include "PresetBrowser.h"

//==============================================================================
/*
    Parameter IDs: the names the plugin uses internally, and that your DAW
    stores in saved projects. Don't rename these once you've used the plugin
    in a project, or that project won't find its settings again.
*/
namespace FilterIDs
{
    inline constexpr const char* type      = "type";        // LOW-PASS / HIGH-PASS / BAND-PASS
    inline constexpr const char* cutoff    = "cutoff";      // Hz
    inline constexpr const char* resonance = "resonance";   // %
    inline constexpr const char* depth     = "depth";       // %
    inline constexpr const char* shape     = "shape";       // SINE ... RANDOM
    inline constexpr const char* rate      = "rate";        // Hz, when SYNC is OFF
    inline constexpr const char* sync      = "sync";        // OFF or TEMPO
    inline constexpr const char* note      = "note";        // one cycle as a note, when SYNC is TEMPO
    inline constexpr const char* width     = "width";       // %
    inline constexpr const char* mix       = "mix";         // %
}

/** One cycle as a note when SYNC is TEMPO, shortest first. (Never reorder.) */
namespace FilterNotes
{
    struct Length { const char* name; double beats; };

    inline const Length lengths[] =
    {
        { "1/32 note",    0.125 },
        { "1/16 triplet", 1.0 / 6.0 },
        { "1/16 note",    0.25 },
        { "1/8 triplet",  1.0 / 3.0 },
        { "1/8 note",     0.5 },
        { "dotted 1/8",   0.75 },
        { "1/4 triplet",  2.0 / 3.0 },
        { "1/4 note",     1.0 },
        { "dotted 1/4",   1.5 },
        { "1/2 note",     2.0 },
        { "1 bar",        4.0 },
        { "2 bars",       8.0 },
        { "4 bars",       16.0 },
        { "8 bars",       32.0 },
    };

    inline constexpr int count = (int) (sizeof (lengths) / sizeof (lengths[0]));

    enum { n32 = 0, n16T, n16, n8T, n8, n8D, n4T, n4, n4D, n2, bar1, bar2, bar4, bar8 };
}

/** TYPE and SHAPE positions (never reorder). */
namespace FilterType  { enum { lowPass = 0, highPass, bandPass }; }
namespace FilterShape { enum { sine = 0, triangle, rampUp, rampDown, square, random }; }

//==============================================================================
/*
    The presets. CUTOFF is the middle of the movement; DEPTH 100% moves it
    three octaves up and down. SYNC 1 uses the NOTE column (one cycle)
    locked to the beat; SYNC 0 uses RATE (Hz). MIX 100% = filtered only.
*/
struct FilterPreset
{
    const char* category;
    const char* name;
    int   type;
    float cutoffHz;
    float resonancePercent;
    float depthPercent;
    int   shape;
    int   sync;
    float rateHz;
    int   note;
    float widthPercent;
    float mixPercent;
    const char* tip;
};

namespace FP
{
    using namespace FilterType;
    using namespace FilterShape;
    using namespace FilterNotes;
}

inline const FilterPreset filterPresets[] =
{
    //  category, name                                    type          cutoff  reso  depth  shape          sync rate   note        width  mix
    { "", "Default settings",                             FP::lowPass,  1000.0f, 35.0f, 60.0f, FP::sine,      1, 0.5f, FP::bar1,    0.0f, 100.0f,
      "A low-pass filter sweeping up and down once a bar, in time with the song. Try SHAPE: SQUARE or RANDOM for tempo pulses, and WIDTH for stereo." },

    // ---- Synths ------------------------------------------------------------------
    { "Synths", "Synth Pad - Slow Wide Sweep",            FP::lowPass,   700.0f, 45.0f, 70.0f, FP::sine,      1, 0.5f, FP::bar4,   40.0f, 100.0f,
      "A pad opening and closing over four bars, the two sides drifting apart. Synthwave and ambient." },
    { "Synths", "Synth - Tempo Pulse 1/8",                FP::lowPass,   900.0f, 50.0f, 75.0f, FP::square,    1, 0.5f, FP::n8,      0.0f, 100.0f,
      "The filter jumping open and shut every 8th note: turns a held chord into a rhythm." },
    { "Synths", "Synth - Sample & Hold 1/16",             FP::lowPass,  1200.0f, 60.0f, 80.0f, FP::random,    1, 0.5f, FP::n16,    60.0f, 100.0f,
      "A new random filter setting every 16th note, different on each side: the classic bubbling S&H sound." },
    { "Synths", "Synthwave Arp - Filter Sweep",           FP::lowPass,  1500.0f, 40.0f, 60.0f, FP::triangle,  1, 0.5f, FP::bar2,   40.0f, 100.0f,
      "An arpeggio brightening and darkening over two bars - the outrun sound." },
    { "Synths", "Darksynth - Ramp Pump 1/4",              FP::lowPass,   700.0f, 55.0f, 80.0f, FP::rampUp,    1, 0.5f, FP::n4,      0.0f, 100.0f,
      "The filter shuts on every beat and opens through it: an aggressive pumping drive for darksynth and cyberpunk." },
    { "Synths", "Cyberpunk - Stereo Pulse 1/16",          FP::bandPass, 1800.0f, 55.0f, 70.0f, FP::square,    1, 0.5f, FP::n16,   100.0f, 100.0f,
      "Left and right pulsing in opposite 16ths: a twitching, wide, mechanical texture." },

    // ---- Bass ---------------------------------------------------------------------
    { "Bass", "Bass - Wobble 1/8",                        FP::lowPass,   300.0f, 65.0f, 70.0f, FP::sine,      1, 0.5f, FP::n8,      0.0f, 100.0f,
      "A synth bass wobbling every 8th note. The low end stays: only the growl on top moves." },
    { "Bass", "Bass - Wobble Triplets",                   FP::lowPass,   300.0f, 65.0f, 70.0f, FP::sine,      1, 0.5f, FP::n8T,     0.0f, 100.0f,
      "The same wobble in 8th-note triplets: a rolling, swung groove." },
    { "Bass", "Bass - Slow Growl",                        FP::lowPass,   450.0f, 50.0f, 45.0f, FP::sine,      1, 0.5f, FP::bar1,    0.0f, 100.0f,
      "A gentle, bar-long movement on a sustained bass: keeps it alive without wobbling." },

    // ---- Drums --------------------------------------------------------------------
    { "Drums", "Drum Loop - Wide Stereo Sweep",           FP::lowPass,  2000.0f, 25.0f, 70.0f, FP::sine,      1, 0.5f, FP::bar2,  100.0f, 100.0f,
      "A drum loop opening on one side as it closes on the other, over two bars: very wide, always moving." },
    { "Drums", "Drum Bus - High-Pass Build (4 bars)",     FP::highPass,  200.0f, 30.0f, 70.0f, FP::rampUp,    1, 0.5f, FP::bar4,    0.0f, 100.0f,
      "The low end thinning out over four bars, then crashing back in: the build-up before a drop." },
    { "Drums", "Hats - Tempo Chop 1/16",                  FP::bandPass, 6000.0f, 40.0f, 50.0f, FP::square,    1, 0.5f, FP::n16,    50.0f, 100.0f,
      "Hi-hats and shakers chopped into a 16th-note stutter." },

    // ---- Guitars ------------------------------------------------------------------
    { "Guitars", "Guitar - Auto Filter Funk 1/8",         FP::bandPass, 1000.0f, 55.0f, 70.0f, FP::triangle,  1, 0.5f, FP::n8,      0.0f, 100.0f,
      "A quacking, rhythmic filter on clean funk guitar, in 8th notes." },
    { "Guitars", "Guitar - Wide Swirl",                   FP::lowPass,  2500.0f, 30.0f, 50.0f, FP::sine,      0, 0.3f, FP::bar1,  100.0f, 100.0f,
      "A slow, swirling movement, the sides moving apart: psychedelic and shoegaze." },

    // ---- Vocals -------------------------------------------------------------------
    { "Vocals", "Vocal - Telephone",                      FP::bandPass, 1500.0f, 30.0f,  0.0f, FP::sine,      0, 0.5f, FP::bar1,    0.0f, 100.0f,
      "No movement: a thin, boxy telephone voice for intros and breakdowns." },
    { "Vocals", "Vocal - Old Radio Drift",                FP::bandPass, 1800.0f, 35.0f, 25.0f, FP::sine,      0, 0.2f, FP::bar1,    0.0f, 100.0f,
      "A narrow, slowly drifting radio sound." },

    // ---- The whole mix ------------------------------------------------------------
    { "Mixing & Mastering", "Mix Bus - Through the Wall",            FP::lowPass,   350.0f, 10.0f,  0.0f, FP::sine,      0, 0.5f, FP::bar1,    0.0f, 100.0f,
      "No movement: the band playing in the next room. Automate MIX or CUTOFF to bring it back." },
    { "Mixing & Mastering", "Mix Bus - Breakdown Sweep (8 bars)",    FP::lowPass,  1200.0f, 30.0f, 80.0f, FP::rampUp,    1, 0.5f, FP::bar8,    0.0f, 100.0f,
      "The whole mix opening up from muffled to full over eight bars." },
};

//==============================================================================
inline std::vector<Preset> makeFilterPresets()
{
    std::vector<Preset> presets;

    for (auto& p : filterPresets)
        presets.push_back ({ p.category, p.name, p.tip,
                             { { FilterIDs::type, (float) p.type }, { FilterIDs::cutoff, p.cutoffHz }, { FilterIDs::resonance, p.resonancePercent },
                               { FilterIDs::depth, p.depthPercent }, { FilterIDs::shape, (float) p.shape }, { FilterIDs::sync, (float) p.sync },
                               { FilterIDs::rate, p.rateHz }, { FilterIDs::note, (float) p.note }, { FilterIDs::width, p.widthPercent },
                               { FilterIDs::mix, p.mixPercent } } });

    return presets;
}

/** The settings presets store (everything except BYPASS). */
inline juce::StringArray filterPresetIDs()
{
    return { FilterIDs::type, FilterIDs::cutoff, FilterIDs::resonance, FilterIDs::depth, FilterIDs::shape,
             FilterIDs::sync, FilterIDs::rate, FilterIDs::note, FilterIDs::width, FilterIDs::mix };
}
