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

    // Added when Bitch-Slap became Easy Verb (1.23)
    inline constexpr const char* type     = "type";      // BITCH-SLAP, ROOM, CLUB, HALL, PLATE
    inline constexpr const char* decay    = "decay";     // seconds (ROOM, CLUB, HALL, PLATE)
    inline constexpr const char* predelay = "predelay";  // ms
    inline constexpr const char* tone     = "tone";      // -100 (dark) .. +100 (bright)
}

/** TYPE: the choices (in this order - projects store the position). */
inline const juce::StringArray verbTypeNames { "Bitch-Slap", "Room", "Club", "Hall", "Plate" };
enum VerbType { bitchSlap = 0, room = 1, club = 2, hall = 3, plate = 4 };

/** DECAY, PRE-DELAY and TONE as a new project (and every Bitch-Slap preset) starts them. */
inline constexpr float defaultDecay = 1.2f, defaultPreDelay = 10.0f, defaultTone = 0.0f;

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
    { "Drums", "Snare - 80s Power",                        45.0f, 350.0f, 0, n8,
      "The classic big 80s snare: a wide burst that stops dead. Rock ballads, arena rock, synthwave." },
    { "Drums", "Snare - Rock Smack",                       38.0f, 220.0f, 0, n8,
      "A short, aggressive burst that makes the snare leap out of a heavy rock mix without washing it out." },
    { "Drums", "Snare - Punk Slap",                        32.0f, 140.0f, 0, n8,
      "Very short and tight for fast punk beats: each burst is gone before the next hit, so nothing blurs." },
    { "Drums", "Snare - Synthwave Wall",                   50.0f, 550.0f, 0, n8,
      "A long, wide burst for slower synthwave and retro pop: the snare fills the space between the beats." },
    { "Drums", "Snare - Darksynth Slam",                   58.0f, 300.0f, 0, n8,
      "Loud and nasty: the burst is louder than the snare itself. For darksynth and cyberpunk, next to distorted synths." },
    { "Drums", "Snare - Subtle Size",                      18.0f, 120.0f, 0, n8,
      "Adds size and excitement without sounding like an effect. Works on indie, funk and jazz snares too." },

    // ---- Toms and kick -----------------------------------------------------------
    { "Drums", "Toms - Big 80s Fills",               42.0f, 400.0f, 0, n8,
      "Huge tom fills. Every hit gets its own burst, so even fast fills stay clear instead of turning to mush." },
    { "Drums", "Toms - Tight Rock",                  30.0f, 250.0f, 0, n8,
      "Bigger toms that still sit tight in a busy rock or metal mix." },
    { "Drums", "Kick - Gated Thump",                 15.0f, 120.0f, 0, n8,
      "A short burst behind the kick for a bigger, rounder thump. Keep MIX low: reverb on a kick builds up fast." },

    // ---- The whole kit -----------------------------------------------------------
    { "Drums", "Drum Bus - 80s Kit",                   25.0f, 250.0f, 0, n8,
      "On the whole kit (or the drum bus): everything gets a short burst. Lower MIX if the cymbals get too washy." },
    { "Drums", "Drum Bus - Punk Room",                 20.0f, 160.0f, 0, n8,
      "A short, trashy room around the whole kit, for raw punk and garage recordings." },
    { "Drums", "Jazz Kit - Just a Touch",              10.0f,  90.0f, 0, n8,
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

    // ---- The whole mix ---------------------------------------------------------------
    // (Everything in a mix feeds the reverb at once, so it comes out far louder
    // than on one drum: these MIX settings were measured on real mixes.)
    { "Mixing & Mastering", "Mix Bus - Subtle Size",                  11.0f, 120.0f, 0, n8,
      "On the whole mix: a touch of room that makes it sound bigger and more glued, without sounding like reverb. Jazz too." },
    { "Mixing & Mastering", "Mix Bus - Rock & Punk Punch",            16.0f, 150.0f, 0, n8,
      "On the whole mix: a short, tight burst behind everything - more size and excitement, still tight. A little MIX goes a long way." },
    { "Mixing & Mastering", "Mix Bus - 80s Space",                    17.0f, 250.0f, 0, n8,
      "On the whole mix: the glossy 80s ambience, for synthwave and retro rock. It sits behind the mix - push MIX up for more." },
    { "Mixing & Mastering", "Mix Bus - Synced 1/16 (Darksynth)",      19.0f, 300.0f, 1, n16,
      "On the whole mix: every burst lasts exactly a 16th note at your song's tempo, so the space pulses in time with the beat." },

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
/*
    The natural spaces - mostly the live ones: a band playing in a real room,
    club or hall, rather than the "studio reverb" sound.

    TYPE: room, club, hall or plate. DECAY in seconds (how long the tail takes
    to die away), PRE-DELAY in ms (the gap before the reverb starts - it keeps
    the sound upfront, the space behind it), TONE -100 (dark, like a room full
    of people) .. +100 (bright).
*/
struct SpacePreset
{
    const char* category;
    const char* name;
    VerbType type;
    float mixPercent;
    float decay;        // seconds
    float preDelayMs;
    float tone;         // -100 .. +100
    const char* tip;
};

inline const SpacePreset spacePresets[] =
{
    //  category, name                                    type   mix   decay  pre   tone
    // ---- Drums ------------------------------------------------------------------
    { "Drums", "Drums - Live Room",            room, 22.0f, 0.8f,  4.0f,   0.0f,
      "The kit played in a lively room: the walls you hear in every great live drum sound. On the drum bus, or the overheads and room mics." },
    { "Drums", "Drums - Punk Rehearsal Room",  room, 26.0f, 0.5f,  0.0f,  20.0f,
      "A small, hard-walled rehearsal room: short, bright and trashy, for raw punk and garage." },
    { "Drums", "Drum Bus - Rock Club",         club, 18.0f, 1.4f,  8.0f,   0.0f,
      "The kit on a club stage: a fuller tail behind the hits, like the gig recording you always loved." },
    { "Drums", "Snare - Club Gig",             club, 25.0f, 1.3f, 10.0f,  10.0f,
      "The snare cracking round a club. The short pre-delay keeps the hit itself sharp." },
    { "Drums", "Jazz Kit - Small Club",        club, 14.0f, 1.1f, 12.0f, -30.0f,
      "Brushes and ride in a small, warm club. Dark, so the cymbals never splash." },
    { "Drums", "Drums - Arena Rock",           hall, 20.0f, 2.2f, 20.0f,   0.0f,
      "Big rock drums in a big hall: the live-album sound. Lower MIX on fast songs." },

    // ---- Vocals & instruments ----------------------------------------------------
    { "Live Spaces - Vocals & Instruments", "Vocal - Club Stage",       club, 18.0f, 1.2f, 25.0f,   0.0f,
      "A singer on a club stage. The pre-delay keeps the words upfront with the room behind them." },
    { "Live Spaces - Vocals & Instruments", "Vocal - Jazz Club",        club, 16.0f, 1.5f, 30.0f, -30.0f,
      "Warm and smoky: a jazz singer in a late-night club." },
    { "Live Spaces - Vocals & Instruments", "Vocal - Rock Hall",        hall, 15.0f, 1.9f, 35.0f,   0.0f,
      "A rock vocal filling a hall. Turn MIX down if the words start to blur." },
    { "Live Spaces - Vocals & Instruments", "Vocal - Punk Shout Room",  room, 18.0f, 0.6f,  5.0f,  15.0f,
      "A shouted vocal in a small, hard room: urgent and close, never washy." },
    { "Live Spaces - Vocals & Instruments", "Guitar - Amp in the Room", room, 20.0f, 0.7f,  3.0f,   0.0f,
      "Puts a close-miked amp back in the room it was played in - the sound of a band, not a DI." },
    { "Live Spaces - Vocals & Instruments", "Horns - Jazz Club",        club, 20.0f, 1.4f, 15.0f, -20.0f,
      "Sax, trumpet or a horn section in a club. Warm, with enough tail to bloom." },
    { "Live Spaces - Vocals & Instruments", "Piano - Concert Hall",     hall, 18.0f, 1.8f, 15.0f, -10.0f,
      "A piano on a hall's stage: rich and spacious, the notes ringing into the room." },
    { "Live Spaces - Vocals & Instruments", "Acoustic - Wooden Room",   room, 18.0f, 0.9f, 10.0f, -20.0f,
      "An acoustic guitar or a fiddle in a warm wooden room. Americana, folk, gospel." },
    { "Live Spaces - Vocals & Instruments", "Synth Pad - Big Hall",     hall, 30.0f, 3.5f, 40.0f,  20.0f,
      "A long, wide wash behind pads and strings, for synthwave and slow songs." },

    // ---- The whole mix -----------------------------------------------------------
    { "Mixing & Mastering", "Mix Bus - Live in a Room",    room, 10.0f, 0.7f,  5.0f,   0.0f,
      "On the whole mix: the band in one room together. Glues a dry, close-miked recording into a performance." },
    { "Mixing & Mastering", "Mix Bus - Live at the Club",  club, 10.0f, 1.2f, 10.0f, -10.0f,
      "On the whole mix: the gig sound. A little MIX goes a long way." },
    { "Mixing & Mastering", "Mix Bus - Live in Concert",   hall,  9.0f, 1.8f, 20.0f,   0.0f,
      "On the whole mix: the live album from a concert hall. Keep MIX low, or the low end will cloud." },

    // ---- FX send: reverb only ----------------------------------------------------
    { "FX Send (Reverb Only)", "Send - Live Room", room, 100.0f, 0.9f, 10.0f, 0.0f,
      "For an FX bus: reverb only. Send a little of every track to put the whole band in the same room." },
    { "FX Send (Reverb Only)", "Send - Club",      club, 100.0f, 1.4f, 15.0f, 0.0f,
      "For an FX bus: the club. Set how much you hear with the bus fader." },
    { "FX Send (Reverb Only)", "Send - Hall",      hall, 100.0f, 2.2f, 25.0f, 0.0f,
      "For an FX bus: the hall, for vocals, pianos, strings and pads." },

    // ---- The studio plate --------------------------------------------------------
    { "Plate", "Vocal - 60s Plate", plate, 18.0f, 1.8f, 30.0f, 20.0f,
      "The classic studio plate on a vocal: silky, bright and smooth. Soul, Motown, ballads." },
    { "Plate", "Snare - Plate",     plate, 20.0f, 1.3f,  5.0f, 30.0f,
      "A bright plate behind the snare - the 70s studio sound." },
};

//==============================================================================
inline std::vector<Preset> makeSlapPresets()
{
    std::vector<Preset> presets;

    // (The Bitch-Slap presets leave the natural spaces' settings at their defaults)
    for (auto& p : slapPresets)
        presets.push_back ({ p.category, p.name, p.tip,
                             { { SlapIDs::type, (float) bitchSlap },
                               { SlapIDs::mix,  p.mixPercent },   { SlapIDs::length, p.lengthMs },
                               { SlapIDs::sync, (float) p.sync }, { SlapIDs::note,   (float) p.note },
                               { SlapIDs::decay, defaultDecay }, { SlapIDs::predelay, defaultPreDelay }, { SlapIDs::tone, defaultTone } } });

    // (...and the spaces leave LENGTH and SYNC at theirs)
    const auto& d = slapPresets[0];
    for (auto& p : spacePresets)
        presets.push_back ({ p.category, p.name, p.tip,
                             { { SlapIDs::type, (float) p.type },
                               { SlapIDs::mix,  p.mixPercent },   { SlapIDs::length, d.lengthMs },
                               { SlapIDs::sync, (float) d.sync }, { SlapIDs::note,   (float) d.note },
                               { SlapIDs::decay, p.decay }, { SlapIDs::predelay, p.preDelayMs }, { SlapIDs::tone, p.tone } } });

    return presets;
}

/** The settings presets store (everything except BYPASS). */
inline juce::StringArray slapPresetIDs()
{
    return { SlapIDs::type, SlapIDs::mix, SlapIDs::length, SlapIDs::sync, SlapIDs::note,
             SlapIDs::decay, SlapIDs::predelay, SlapIDs::tone };
}
