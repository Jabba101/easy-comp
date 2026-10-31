#pragma once

#include "PresetBrowser.h"

//==============================================================================
/*
    Parameter IDs: the names the plugin uses internally, and that your DAW
    stores in saved projects. Don't rename these once you've used the plugin
    in a project, or that project won't find its settings again.
*/
namespace DelayIDs
{
    inline constexpr const char* time      = "time";        // ms, when SYNC is OFF
    inline constexpr const char* sync      = "sync";        // OFF or TEMPO
    inline constexpr const char* note      = "note";        // TIME as a note, when SYNC is TEMPO
    inline constexpr const char* feedback  = "feedback";    // %
    inline constexpr const char* mode      = "mode";        // STEREO / PING-PONG / WIDE
    inline constexpr const char* lowCut    = "lowcut";      // Hz (20 = off)
    inline constexpr const char* highCut   = "highcut";     // Hz (20000 = off)
    inline constexpr const char* character = "character";   // CLEAN / TAPE
    inline constexpr const char* duck      = "duck";        // %
    inline constexpr const char* mix       = "mix";         // %
}

/** TIME as a note when SYNC is TEMPO, shortest first. (Never reorder these:
    projects store the position in this list.) */
namespace DelayNotes
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
        { "dotted 1/2",   3.0 },
        { "whole note",   4.0 },
    };

    inline constexpr int count = (int) (sizeof (lengths) / sizeof (lengths[0]));

    enum { n32 = 0, n16T, n16, n8T, n16D, n8, n4T, n8D, n4, n2T, n4D, n2, n2D, n1 };
}

//==============================================================================
/*
    The presets.

    SYNC 1 (TEMPO) uses the NOTE column and follows your song's tempo; SYNC 0
    uses TIME in ms. MODE 0 = STEREO, 1 = PING-PONG, 2 = WIDE. TAPE 1 = a tape
    echo (warmer repeats, a little wobble).

    MIX: these are for putting Easy Delay straight on the track (an insert).
    On an FX send, use the "Send -" presets (MIX 100%: echoes only).
*/
struct DelayPreset
{
    const char* category;
    const char* name;
    int   sync;          // 0 = OFF (TIME in ms), 1 = TEMPO (NOTE)
    float timeMs;
    int   note;
    float feedbackPercent;
    int   mode;          // 0 STEREO, 1 PING-PONG, 2 WIDE
    float lowCutHz;      // 20 = off
    float highCutHz;     // 20000 = off
    int   tape;          // 0 CLEAN, 1 TAPE
    float duckPercent;
    float mixPercent;
    const char* tip;
};

inline const DelayPreset delayPresets[] =
{
    //  category, name                                  sync  time   note             fb  mode   lowcut  highcut tape duck  mix
    { "", "Default settings",                              1, 375.0f, DelayNotes::n8,  25.0f, 0, 150.0f,  8000.0f, 0,  0.0f, 20.0f,
      "Eighth-note echoes, following your song's tempo, fading away gently. Turn MIX up for more, FEEDBACK for more repeats." },

    // ---- Vocals ----------------------------------------------------------------
    { "Vocals", "Vocal - Slapback",                        0, 110.0f, DelayNotes::n16, 0.0f,  0, 250.0f,  6000.0f, 1,  0.0f, 22.0f,
      "One quick tape echo right behind the voice: 50s rock 'n' roll, rockabilly, and a classic way to thicken a rock vocal." },
    { "Vocals", "Vocal - Quarter-Note Throw",              1, 500.0f, DelayNotes::n4,  30.0f, 1, 300.0f,  5000.0f, 0, 70.0f, 30.0f,
      "Echoes that bounce left and right in the gaps between your lines - DUCK keeps them out of the way while you sing." },
    { "Vocals", "Vocal - Eighth-Note Space",               1, 250.0f, DelayNotes::n8,  20.0f, 0, 250.0f,  6000.0f, 0, 50.0f, 15.0f,
      "A short, tucked-in echo that gives a dry vocal space without sounding like an effect." },
    { "Vocals", "Vocal - Dub Echo",                        1, 750.0f, DelayNotes::n8D, 60.0f, 1, 400.0f,  3000.0f, 1, 40.0f, 30.0f,
      "Dark tape echoes that keep repeating and bend if you move TIME: dub, reggae and spacey breakdowns." },

    // ---- Guitars ---------------------------------------------------------------
    { "Guitars", "Guitar - Dotted-Eighth Lead",            1, 375.0f, DelayNotes::n8D, 35.0f, 0, 200.0f,  7000.0f, 0, 30.0f, 30.0f,
      "The dotted-eighth echo that turns simple picking into a rolling, rhythmic pattern. Big in U2-style and 80s rock." },
    { "Guitars", "Guitar - Wide Doubler",                  0,  18.0f, DelayNotes::n32,  0.0f, 2, 150.0f, 12000.0f, 0,  0.0f, 45.0f,
      "Very short echoes, different on each side: one guitar sounds like two, spread left and right." },
    { "Guitars", "Guitar - Spaghetti Western",             1, 500.0f, DelayNotes::n4,  45.0f, 0, 200.0f,  4500.0f, 1,  0.0f, 30.0f,
      "Twangy guitar into a tape echo: dark, wobbly repeats. Surf, western and Americana." },
    { "Guitars", "Guitar - Solo Ping-Pong",                1, 250.0f, DelayNotes::n8,  35.0f, 1, 250.0f,  6000.0f, 0, 60.0f, 25.0f,
      "A guitar solo with echoes bouncing side to side, ducked so the fast notes stay clear." },

    // ---- Drums -----------------------------------------------------------------
    { "Drums", "Snare - Dub Splash",                       1, 750.0f, DelayNotes::n8D, 55.0f, 1, 400.0f,  3500.0f, 1,  0.0f, 25.0f,
      "Dub-style tape echoes on the snare (automate MIX to throw them in on single hits)." },
    { "Drums", "Drums - Sixteenth Shimmer",                1, 125.0f, DelayNotes::n16, 20.0f, 1, 600.0f, 10000.0f, 0, 40.0f, 15.0f,
      "Tight sixteenth-note echoes on hats or percussion: movement and width without blurring the groove." },

    // ---- Bass ------------------------------------------------------------------
    { "Bass", "Bass - Dub Echo",                           1, 500.0f, DelayNotes::n4,  35.0f, 0, 120.0f,  2500.0f, 1, 60.0f, 20.0f,
      "Dark, ducked quarter-note echoes on a bass: dub and trip-hop. The low end of the echoes is cut so the bass stays solid." },

    // ---- Keys & Synths ---------------------------------------------------------
    { "Keys & Synths", "Synth - Synthwave Ping-Pong",      1, 250.0f, DelayNotes::n8,  40.0f, 1, 250.0f,  6000.0f, 0, 30.0f, 25.0f,
      "The 80s synth lead: eighth-note echoes bouncing left and right, a little dark so they sit behind the lead." },
    { "Keys & Synths", "Synth - Darksynth Tape",            1, 375.0f, DelayNotes::n8D, 50.0f, 2, 300.0f,  4000.0f, 1, 30.0f, 25.0f,
      "Dotted-eighth tape echoes, wide and dark, wobbling slightly: a menacing synth line." },
    { "Keys & Synths", "Keys - Jazz Room Echo",            0,  90.0f, DelayNotes::n32, 10.0f, 2, 200.0f,  7000.0f, 1,  0.0f, 15.0f,
      "A short, warm tape echo on piano or organ - more like a lively room than an effect." },
    { "Keys & Synths", "Synth - Arp Triplets",             1, 333.0f, DelayNotes::n8T, 35.0f, 1, 300.0f,  8000.0f, 0, 30.0f, 25.0f,
      "Triplet echoes over a straight arpeggio: the pattern starts to roll and tumble." },

    // ---- FX Send ---------------------------------------------------------------
    { "FX Send", "Send - Slapback",                        0, 110.0f, DelayNotes::n16, 0.0f,  0, 250.0f,  6000.0f, 1,  0.0f, 100.0f,
      "For an FX send: one quick tape echo, echoes only. Send any track to it for a touch of 50s space." },
    { "FX Send", "Send - Quarter-Note Echo",               1, 500.0f, DelayNotes::n4,  35.0f, 1, 300.0f,  5000.0f, 0,  0.0f, 100.0f,
      "For an FX send: quarter-note echoes bouncing left and right, echoes only." },
    { "FX Send", "Send - Tape Echo",                       1, 375.0f, DelayNotes::n8D, 45.0f, 0, 300.0f,  4000.0f, 1,  0.0f, 100.0f,
      "For an FX send: a dark, warm tape echo, echoes only. Synthwave, dub, psychedelic rock." },
};

//==============================================================================
inline std::vector<Preset> makeDelayPresets()
{
    std::vector<Preset> presets;

    for (auto& p : delayPresets)
        presets.push_back ({ p.category, p.name, p.tip,
                             { { DelayIDs::sync, (float) p.sync }, { DelayIDs::time, p.timeMs }, { DelayIDs::note, (float) p.note },
                               { DelayIDs::feedback, p.feedbackPercent }, { DelayIDs::mode, (float) p.mode },
                               { DelayIDs::lowCut, p.lowCutHz }, { DelayIDs::highCut, p.highCutHz },
                               { DelayIDs::character, (float) p.tape }, { DelayIDs::duck, p.duckPercent },
                               { DelayIDs::mix, p.mixPercent } } });

    return presets;
}

/** The settings presets store (everything except BYPASS). */
inline juce::StringArray delayPresetIDs()
{
    return { DelayIDs::sync, DelayIDs::time, DelayIDs::note, DelayIDs::feedback, DelayIDs::mode,
             DelayIDs::lowCut, DelayIDs::highCut, DelayIDs::character, DelayIDs::duck, DelayIDs::mix };
}
