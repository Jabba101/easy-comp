#pragma once

#include "PresetBrowser.h"

//==============================================================================
/*
    Parameter IDs: the names the plugin uses internally, and that your DAW
    stores in saved projects. Don't rename these once you've used the plugin
    in a project, or that project won't find its settings again.

    The bands, in order: LOW, LOW MID, HIGH MID, HIGH.
*/
namespace EQIDs
{
    inline constexpr const char* freq[]   { "lowfreq",   "lowmidfreq",   "highmidfreq",   "highfreq" };
    inline constexpr const char* gain[]   { "lowgain",   "lowmidgain",   "highmidgain",   "highgain" };
    inline constexpr const char* target[] { "lowtarget", "lowmidtarget", "highmidtarget", "hightarget" };   // ST / MID / SIDE
    inline constexpr const char* lowShelf  = "lowshelf";    // LOW: BELL / SHELF
    inline constexpr const char* highShelf = "highshelf";   // HIGH: BELL / SHELF

    inline constexpr const char* hpf       = "hpf";
    inline constexpr const char* hpfSlope  = "hpfslope";    // 12 / 24 dB per octave
    inline constexpr const char* lpf       = "lpf";
    inline constexpr const char* lpfSlope  = "lpfslope";
    inline constexpr const char* monoBass  = "monobass";

    inline constexpr const char* master    = "master";      // linear phase
    inline constexpr const char* precision = "precision";   // MASTER's PRECISION: LOW / MEDIUM / HIGH
    inline constexpr const char* analyser  = "analyser";    // the spectrum behind the curve
}

//==============================================================================
/*
    The presets.

    Each band: frequency (Hz), gain (dB, -12..+12), and who it works on:
    ST = the whole stereo sound, MID = only the middle, SIDE = only the
    sides. LOW and HIGH also say whether they're a SHELF (true) or a BELL.
    The filters: HPF / LPF in Hz (20 / 20000 = OFF), 24 = 24 dB per octave
    instead of 12. MONO BASS in Hz (20 = OFF).

    They're starting points: every recording is different. Pick one, then
    move the bands and listen - LISTEN on a band lets you hear exactly what
    it covers.
*/
struct EQBandPreset
{
    float freqHz, gainDb;
    int target;   // 0 = ST, 1 = MID, 2 = SIDE
};

struct EQPreset
{
    const char* category;
    const char* name;
    EQBandPreset low;      bool lowShelf;
    EQBandPreset lowMid;
    EQBandPreset highMid;
    EQBandPreset high;     bool highShelf;
    float hpfHz;  bool hpf24;
    float lpfHz;  bool lpf24;
    float monoBassHz;
    const char* tip;
};

inline const EQPreset eqPresets[] =
{
    //  category, name                          LOW                    shelf  LOW MID            HIGH MID            HIGH                   shelf  HPF       LPF          MONO
    { "", "Default settings",                  { 100, 0, 0 },         true,  { 250, 0, 0 },     { 3000, 0, 0 },     { 10000, 0, 0 },       true,  20, false, 20000, false, 20,
      "Changes nothing yet. Turn a band's GAIN up or down, or drag its dot on the screen. Hold LISTEN to hear what a band covers." },

    // ---- Drums -----------------------------------------------------------------
    { "Drums", "Kick - Punch & Click",          { 60, 3, 0 },          false, { 350, -4, 0 },    { 3500, 3, 0 },     { 10000, 0, 0 },       true,  30, false, 20000, false, 20,
      "More thump at 60 Hz, less cardboard at 350 Hz, and the beater's click at 3.5 kHz." },
    { "Drums", "Kick - Tight & Modern",         { 50, 2, 0 },          false, { 300, -5, 0 },    { 4500, 4, 0 },     { 10000, 0, 0 },       true,  35, true,  20000, false, 20,
      "Deep but tight low end, the boxiness scooped out, and a bright click for fast rock, metal and synthwave." },
    { "Drums", "Snare - Fat & Cracking",        { 100, 0, 0 },         true,  { 200, 3, 0 },     { 5000, 3, 0 },     { 10000, 0, 0 },       true,  80, false, 20000, false, 20,
      "Body at 200 Hz and stick crack at 5 kHz; the low rumble below 80 Hz goes." },
    { "Drums", "Snare - Less Ring",             { 100, 0, 0 },         true,  { 450, -3, 0 },    { 1000, -2, 0 },    { 10000, 0, 0 },       true,  90, false, 20000, false, 20,
      "Takes some of the ring and honk out of a snare. Move LOW MID until the ring you don't like gets quieter." },
    { "Drums", "Toms - Big & Clear",            { 90, 3, 0 },          false, { 400, -4, 0 },    { 4000, 2, 0 },     { 10000, 0, 0 },       true,  50, false, 20000, false, 20,
      "Boom at 90 Hz, mud out at 400 Hz, attack at 4 kHz. Move LOW to each tom's own note." },
    { "Drums", "Overheads - Cymbals Not Kit",   { 100, 0, 0 },         true,  { 400, -2, 0 },    { 3000, 0, 0 },     { 10000, 2, 0 },       true,  150, true, 20000, false, 20,
      "Overheads as cymbal mics: below 150 Hz goes (the close mics do that job), a little air on top." },
    { "Drums", "Room Mics - Big & Dark",        { 120, 2, 0 },         true,  { 500, -3, 0 },    { 3000, -2, 0 },    { 10000, 0, 0 },       true,  40, false, 9000, false, 20,
      "A bigger, darker room: more low weight, less harsh top, so the room sits behind the kit." },
    { "Drums", "Drum Bus - Polish",             { 60, 1.5f, 0 },       true,  { 350, -1.5f, 0 }, { 3000, 0, 0 },     { 12000, 2, 0 },       true,  25, false, 20000, false, 20,
      "On the whole kit: a little more weight and air, a little less mud. Subtle on purpose." },

    // ---- Bass ------------------------------------------------------------------
    { "Bass", "Bass - Clear & Solid",           { 80, 2, 0 },          false, { 250, -3, 0 },    { 1500, 3, 0 },     { 10000, 0, 0 },       true,  35, false, 20000, false, 20,
      "Solid low end, less mud, and the growl at 1.5 kHz that lets the bass be heard on small speakers." },
    { "Bass", "Bass - Under Distorted Guitars", { 100, 1, 0 },         true,  { 400, -2, 0 },    { 900, 4, 0 },      { 10000, 0, 0 },       true,  40, false, 7000, false, 20,
      "Pushes the bass's growl around 900 Hz so it cuts through heavy guitars; the top above 7 kHz goes." },

    // ---- Guitars ---------------------------------------------------------------
    { "Guitars", "Distorted Guitar - Tighten",  { 100, 0, 0 },         true,  { 300, -3, 0 },    { 3500, 1.5f, 0 },  { 10000, 0, 0 },       true,  90, false, 9000, false, 20,
      "Takes the flub and mud out of heavy guitars and rolls off the fizz, so they sit tight around the bass and drums." },
    { "Guitars", "Clean Guitar - Sparkle",      { 100, 0, 0 },         true,  { 400, -2, 0 },    { 3000, 1, 0 },     { 10000, 3, 0 },       true,  80, false, 20000, false, 20,
      "Shimmer and sparkle for clean and chorused guitars." },
    { "Guitars", "Acoustic Guitar - Less Boom", { 100, 0, 0 },         true,  { 200, -4, 0 },    { 5000, 2, 0 },     { 12000, 2, 0 },       true,  80, false, 20000, false, 20,
      "Tames the boom of an acoustic close to the mic, and brings out the strings." },

    // ---- Vocals ----------------------------------------------------------------
    { "Vocals", "Vocal - Clear & Present",      { 100, 0, 0 },         true,  { 300, -2, 0 },    { 3500, 2, 0 },     { 12000, 2, 0 },       true,  90, false, 20000, false, 20,
      "The everyday vocal EQ: rumble out, a little less mud, more presence and air." },
    { "Vocals", "Vocal - Less Boxy",            { 100, 0, 0 },         true,  { 450, -4, 0 },    { 3000, 0, 0 },     { 10000, 0, 0 },       true,  90, false, 20000, false, 20,
      "For a vocal that sounds like it was recorded in a cupboard: cuts the boxiness around 450 Hz. Move LOW MID to the worst spot." },
    { "Vocals", "Vocal - Less Nasal",           { 100, 0, 0 },         true,  { 250, 0, 0 },     { 1100, -4, 0 },    { 10000, 0, 0 },       true,  90, false, 20000, false, 20,
      "Cuts the honky, nasal sound around 1 kHz. Move HIGH MID until the nasal tone drops away." },
    { "Vocals", "Vocal - Radio Telephone",      { 100, 0, 0 },         true,  { 250, 0, 0 },     { 1800, 6, 0 },     { 10000, 0, 0 },       true,  500, true, 3500, true,  20,
      "The thin, old-radio effect for intros and breakdowns: everything below 500 Hz and above 3.5 kHz goes." },

    // ---- Synths & keys ---------------------------------------------------------
    { "Synths & Keys", "Synth Bass - Deep & Tight", { 50, 2, 0 },      false, { 300, -3, 0 },    { 2000, 2, 0 },     { 10000, 0, 0 },       true,  30, true,  20000, false, 20,
      "Deep sub, less mud, and a little bite so the notes are heard on small speakers." },
    { "Synths & Keys", "Synth Pad - Out of the Way", { 100, 0, 0 },    true,  { 400, -3, 0 },    { 3000, -2, 0 },    { 12000, 1, 0 },       true,  200, false, 20000, false, 20,
      "Makes room for the vocal and drums: the pad keeps its shimmer but gives up the lows and the middle." },
    { "Synths & Keys", "Piano - Clear",         { 100, 0, 0 },         true,  { 250, -2, 0 },    { 3000, 1.5f, 0 },  { 12000, 1.5f, 0 },    true,  60, false, 20000, false, 20,
      "A little less mud and a little more clarity and air for piano and keys." },

    // ---- The whole mix ---------------------------------------------------------
    { "Mix Bus", "Mix Bus - Gentle Polish", { 50, 1, 0 },     true,  { 300, -1, 0 },    { 3000, 0, 0 },     { 12000, 1.5f, 0 },    true,  20, false, 20000, false, 20,
      "On the whole mix: a touch more weight and air, a touch less mud." },
    { "Mastering", "Master - Mono Bass & Air", { 100, 0, 0 },   true,  { 300, 0, 0 },     { 3000, 0, 0 },     { 12000, 1.5f, 0 },    true,  25, false, 20000, false, 120,
      "Keeps everything below 120 Hz in the middle (tighter bass, safer for vinyl and clubs) and adds a little air." },
    { "Mix Bus", "Mix Bus - Wider (Side Air)", { 100, 0, 0 }, true,  { 300, 0, 0 },     { 3000, 0, 0 },     { 8000, 3, 2 },        true,  20, false, 20000, false, 100,
      "Brightens only the sides: the mix sounds wider while the vocal, kick and bass in the middle stay as they are." },
    { "Mix Bus", "Mix Bus - Clear the Middle", { 100, 0, 0 }, true,  { 300, -2, 1 },    { 3000, 0, 0 },     { 10000, 0, 0 },       true,  20, false, 20000, false, 20,
      "Takes a little mud out of the middle only (vocal, bass, kick, snare), leaving the wide parts untouched." },
};

//==============================================================================
inline std::vector<Preset> makeEQPresets()
{
    std::vector<Preset> presets;

    for (auto& p : eqPresets)
    {
        const EQBandPreset* bands[] { &p.low, &p.lowMid, &p.highMid, &p.high };
        Preset preset { p.category, p.name, p.tip, {} };

        for (int b = 0; b < 4; ++b)
        {
            preset.values.push_back ({ EQIDs::freq[b],   bands[b]->freqHz });
            preset.values.push_back ({ EQIDs::gain[b],   bands[b]->gainDb });
            preset.values.push_back ({ EQIDs::target[b], (float) bands[b]->target });
        }

        preset.values.push_back ({ EQIDs::lowShelf,  p.lowShelf ? 1.0f : 0.0f });
        preset.values.push_back ({ EQIDs::highShelf, p.highShelf ? 1.0f : 0.0f });
        preset.values.push_back ({ EQIDs::hpf,       p.hpfHz });
        preset.values.push_back ({ EQIDs::hpfSlope,  p.hpf24 ? 1.0f : 0.0f });
        preset.values.push_back ({ EQIDs::lpf,       p.lpfHz });
        preset.values.push_back ({ EQIDs::lpfSlope,  p.lpf24 ? 1.0f : 0.0f });
        preset.values.push_back ({ EQIDs::monoBass,  p.monoBassHz });
        presets.push_back (preset);
    }

    return presets;
}

/** The settings presets store (everything except BYPASS, AUTO-GAIN, MASTER and the analyser). */
inline juce::StringArray eqPresetIDs()
{
    juce::StringArray ids;
    for (int b = 0; b < 4; ++b)
        ids.addArray ({ EQIDs::freq[b], EQIDs::gain[b], EQIDs::target[b] });
    ids.addArray ({ EQIDs::lowShelf, EQIDs::highShelf, EQIDs::hpf, EQIDs::hpfSlope, EQIDs::lpf, EQIDs::lpfSlope, EQIDs::monoBass });
    return ids;
}
