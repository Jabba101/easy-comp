#pragma once

#include "PresetBrowser.h"

//==============================================================================
/*
    Parameter IDs: the names the plugin uses internally, and that your DAW
    stores in saved projects. Don't rename these once you've used the plugin
    in a project, or that project won't find its settings again.
*/
namespace SlapIDs
{
    inline constexpr const char* mix    = "mix";
    inline constexpr const char* length = "length";
    inline constexpr const char* sync   = "sync";    // OFF or TEMPO
    inline constexpr const char* note   = "note";    // LENGTH as a note, when SYNC is TEMPO
}

/** SYNC: the two choices (in this order). */
enum SyncMode { syncOff = 0, syncTempo = 1 };

//==============================================================================
/** The note lengths LENGTH can be set to when SYNC is TEMPO, shortest first.
    (Never reorder these: projects store the position in this list.) */
struct NoteLength
{
    const char* name;    // as the knob shows it
    double beats;        // in quarter notes
};

inline const NoteLength noteLengths[] =
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
};

inline constexpr int numNoteLengths = (int) (sizeof (noteLengths) / sizeof (noteLengths[0]));

/** Positions in that list, for the presets below. */
enum Note { n32 = 0, n16T, n16, n8T, n16D, n8, n4T, n8D, n4, n2T, n4D, n2 };

//==============================================================================
/*
    The presets.

    They're starting points: every recording and every song is different.
    Most of the sound is in two settings: MIX (how loud the burst is - at 50%
    it's about as loud as the snare) and LENGTH (how long before it slams
    shut). Rule of thumb: the burst should end before the next snare hit, or
    the next tom in a fill.

    To add your own: copy a line, then change the category (which sub-menu
    it's in), the name, the numbers and the tip. (You can also save presets
    from the plugin's menu without touching any code.)

    SYNC 0 = OFF (LENGTH in ms), 1 = TEMPO (LENGTH as a note, from the NOTE
    column). Presets don't touch BYPASS.
*/
struct SlapPreset
{
    const char* category;
    const char* name;
    float mixPercent;   //  0 .. 100 (50 = dry and reverb both full, 100 = reverb only)
    float lengthMs;     // 50 .. 1000
    int sync;           // 0 = OFF, 1 = TEMPO
    int note;           // used when SYNC is TEMPO
    const char* tip;
};

inline const SlapPreset slapPresets[] =
{
    //  category, name                                     mix  length sync note
    { "", "Default settings",                              35.0f, 300.0f, 0, n8,
      "Play your drums and turn up MIX until the burst sits where you like it. LENGTH sets how long each burst lasts before it slams shut." },

    // ---- Snare -------------------------------------------------------------------
    { "Snare", "Snare - 80s Power",                        45.0f, 350.0f, 0, n8,
      "The classic big 80s snare: a wide burst that stops dead. Rock ballads, arena rock, synthwave." },
    { "Snare", "Snare - Rock Smack",                       38.0f, 220.0f, 0, n8,
      "A short, aggressive burst that makes the snare leap out of a heavy rock mix without washing it out." },
    { "Snare", "Snare - Punk Slap",                        32.0f, 140.0f, 0, n8,
      "Very short and tight for fast punk beats: each burst is gone before the next hit, so nothing blurs." },
    { "Snare", "Snare - Synthwave Wall",                   50.0f, 550.0f, 0, n8,
      "A long, wide burst for slower synthwave and retro pop: the snare fills the space between the beats." },
    { "Snare", "Snare - Darksynth Slam",                   58.0f, 300.0f, 0, n8,
      "Loud and nasty: the burst is louder than the snare itself. For darksynth and cyberpunk, next to distorted synths." },
    { "Snare", "Snare - Subtle Size",                      18.0f, 120.0f, 0, n8,
      "Adds size and excitement without sounding like an effect. Works on indie, funk and jazz snares too." },

    // ---- Toms and kick -----------------------------------------------------------
    { "Toms & Kick", "Toms - Big 80s Fills",               42.0f, 400.0f, 0, n8,
      "Huge tom fills. Every hit gets its own burst, so even fast fills stay clear instead of turning to mush." },
    { "Toms & Kick", "Toms - Tight Rock",                  30.0f, 250.0f, 0, n8,
      "Bigger toms that still sit tight in a busy rock or metal mix." },
    { "Toms & Kick", "Kick - Gated Thump",                 15.0f, 120.0f, 0, n8,
      "A short burst behind the kick for a bigger, rounder thump. Keep MIX low: reverb on a kick builds up fast." },

    // ---- The whole kit -----------------------------------------------------------
    { "Whole Kit", "Drum Bus - 80s Kit",                   25.0f, 250.0f, 0, n8,
      "On the whole kit (or the drum bus): everything gets a short burst. Lower MIX if the cymbals get too washy." },
    { "Whole Kit", "Drum Bus - Punk Room",                 20.0f, 160.0f, 0, n8,
      "A short, trashy room around the whole kit, for raw punk and garage recordings." },
    { "Whole Kit", "Jazz Kit - Just a Touch",              10.0f,  90.0f, 0, n8,
      "A whisper of space around a dry jazz kit. You should miss it when you switch BYPASS on, not hear it as reverb." },

    // ---- Electronic --------------------------------------------------------------
    { "Electronic", "Clap - Synthwave",                    45.0f, 450.0f, 0, n8,
      "A big, glossy burst on claps and drum-machine snares, for synthwave and outrun." },
    { "Electronic", "Drum Machine - Cyberpunk Hit",        52.0f, 280.0f, 0, n8,
      "Hard, metallic and wide, for drum machines in cyberpunk and darksynth. Push MIX past 50% for an even harder hit." },
    { "Electronic", "Synth Stab - Gated Burst",            30.0f, 200.0f, 0, n8,
      "Short stabs and chord hits get a burst that stops dead, so they sound huge without smearing the rhythm." },

    // ---- Vocals ------------------------------------------------------------------
    { "Vocals", "Vocal - 80s Shout",                       20.0f, 220.0f, 0, n8,
      "On a shouted vocal or a gang chant: big and wide, but it stops before it blurs the words." },

    // ---- FX send: reverb only ----------------------------------------------------
    { "FX Send (Reverb Only)", "Send - 80s Snare & Toms", 100.0f, 350.0f, 0, n8,
      "For an FX bus: reverb only. Send the snare and toms to the bus, and set how much you hear with the bus fader." },
    { "FX Send (Reverb Only)", "Send - Tight Room",       100.0f, 180.0f, 0, n8,
      "For an FX bus: a short, tight burst. Send a little of every drum to it for a kit that sounds bigger but stays dry." },

    // ---- Locked to the song's tempo ----------------------------------------------
    { "Tempo Sync", "Synced - 1/8 Note",                   40.0f, 300.0f, 1, n8,
      "Each burst lasts exactly an 8th note at your song's tempo, so it stops just as the next 8th note starts." },
    { "Tempo Sync", "Synced - 1/16 Note (Fast Songs)",     35.0f, 300.0f, 1, n16,
      "A 16th note long: for fast punk, metal and drum & bass, where an 8th note would blur." },
    { "Tempo Sync", "Synced - 1/4 Note (Slow Songs)",      42.0f, 300.0f, 1, n4,
      "A whole beat long: for slow ballads and synthwave, where the burst fills the gap until the next beat." },
};

//==============================================================================
inline std::vector<Preset> makeSlapPresets()
{
    std::vector<Preset> presets;

    for (auto& p : slapPresets)
        presets.push_back ({ p.category, p.name, p.tip,
                             { { SlapIDs::mix,  p.mixPercent },   { SlapIDs::length, p.lengthMs },
                               { SlapIDs::sync, (float) p.sync }, { SlapIDs::note,   (float) p.note } } });

    return presets;
}

/** The settings presets store (everything except BYPASS). */
inline juce::StringArray slapPresetIDs()
{
    return { SlapIDs::mix, SlapIDs::length, SlapIDs::sync, SlapIDs::note };
}
