#pragma once

#include "PresetBrowser.h"

//==============================================================================
/*
    Parameter IDs: the names the plugin uses internally, and that your DAW
    stores in saved projects. Don't rename these once you've used the plugin
    in a project, or that project won't find its settings again.
*/
namespace DSIDs
{
    inline constexpr const char* from      = "from";
    inline constexpr const char* to        = "to";
    inline constexpr const char* threshold = "threshold";
    inline constexpr const char* range     = "range";
    inline constexpr const char* turnsDown = "turnsdown";   // HARSH BAND or WHOLE SOUND
}

/** TURNS DOWN: the two choices (in this order). */
enum TurnsDown { harshBand = 0, wholeSound = 1 };

//==============================================================================
/*
    The presets.

    They're starting points: every voice and every recording is different.
    After choosing one, play your track and watch the display: set THRESHOLD
    so the amber line (the harsh band) goes over the dashed line only on the
    S sounds (or the harsh cymbal hits), not on ordinary words.

    To add your own: copy a line, then change the category (which sub-menu
    it's in), the name, the numbers and the tip. (You can also save presets
    from the plugin's menu without touching any code.)

    TO 20000 means off: everything above FROM. Presets don't touch BYPASS.
*/
struct DSPreset
{
    const char* category;
    const char* name;
    float fromHz;        //  1000 .. 16000
    float toHz;          //  2000 .. 20000 (20000 = off)
    float thresholdDb;   //   -30 .. +20
    float rangeDb;       //     0 .. 24
    TurnsDown turnsDown;
    const char* tip;
};

inline const DSPreset dsPresets[] =
{
    //  category, name                                     from        to  thresh  range  turns down

    { "", "Default settings",                           5000.0f, 20000.0f,  -8.0f,  6.0f, harshBand,
      "Play your track and watch the display. Set THRESHOLD so the amber line goes over the dashed line on the S sounds "
      "only." },

    // ---- Vocals ----------------------------------------------------------------
    { "Vocals", "Male Vocal",                           4000.0f, 20000.0f,  -8.0f,  6.0f, harshBand,
      "Softens the S and SH sounds of a lower voice. If S sounds start to lisp, lower RANGE; if they still hiss, lower "
      "THRESHOLD." },
    { "Vocals", "Female Vocal",                         5500.0f, 20000.0f,  -8.0f,  6.0f, harshBand,
      "Softens the S sounds of a higher voice, and leaves the air and sparkle above them alone." },
    { "Vocals", "Rap & Fast Delivery",                  4500.0f, 20000.0f, -10.0f,  8.0f, harshBand,
      "Firm and quick, for fast lines full of S and T sounds: keeps the words crisp without the hiss." },
    { "Vocals", "Soft & Breathy Vocal",                 5000.0f, 20000.0f,  -8.0f,  4.0f, harshBand,
      "A light touch for close, breathy singing: tames the S sounds and leaves the breath alone." },
    { "Vocals", "Backing Vocals",                       4500.0f, 20000.0f, -10.0f, 10.0f, harshBand,
      "Firmer: stacked voices pile their S sounds up, so these come down more and sit behind the lead." },
    { "Vocals", "Very Sibilant Vocal",                  5000.0f, 20000.0f, -10.0f,  9.0f, harshBand,
      "For a singer or a bright mic with really sharp S sounds. Still lispy? Lower RANGE a little." },
    { "Vocals", "Classic (Whole Sound)",                5000.0f, 20000.0f,  -8.0f,  4.0f, wholeSound,
      "The classic de-esser: the whole voice dips for a moment on each S. Firmer, with an old-school sound." },
    { "Vocals", "Speech & Podcasts",                    4500.0f, 20000.0f,  -8.0f,  6.0f, harshBand,
      "For voice-overs and podcasts: evens out sharp S sounds, which get tiring on headphones." },

    // ---- Drums -----------------------------------------------------------------
    { "Drums", "Overheads - Harsh Cymbals",             2500.0f,  8000.0f,   0.0f,  6.0f, harshBand,
      "Tames the loud crash hits on the overheads and leaves the rest of the kit alone." },
    { "Drums", "Hi-Hat - Too Bright",                   6000.0f, 20000.0f,   6.0f,  6.0f, harshBand,
      "On a hi-hat mic: turns down just the sharpest, brightest hits, so the hat sits back in the mix." },
    { "Drums", "Room Mics - Splashy",                   2000.0f,  8000.0f,   0.0f,  6.0f, harshBand,
      "Tames the cymbal splash in room mics, so you can turn the room up for size without it getting harsh." },

    // ---- Guitars ---------------------------------------------------------------
    { "Guitars", "Acoustic Guitar - String Squeaks",    2000.0f,  5000.0f,  -6.0f,  6.0f, harshBand,
      "Turns down the squeak of fingers sliding along the strings between chords." },

    // ---- Mix -------------------------------------------------------------------
    { "Mix Bus", "Mix Bus - Gentle",                        5000.0f, 20000.0f,  -6.0f,  3.0f, harshBand,
      "A gentle touch on a whole mix: takes the edge off the harshest S sounds and cymbals only." },
};

//==============================================================================
inline std::vector<Preset> makeDSPresets()
{
    std::vector<Preset> presets;

    for (auto& p : dsPresets)
        presets.push_back ({ p.category, p.name, p.tip,
                             { { DSIDs::from,  p.fromHz },  { DSIDs::to,        p.toHz },
                               { DSIDs::threshold, p.thresholdDb }, { DSIDs::range, p.rangeDb },
                               { DSIDs::turnsDown, (float) p.turnsDown } } });

    return presets;
}

/** The settings presets store (everything except BYPASS). */
inline juce::StringArray dsPresetIDs()
{
    return { DSIDs::from, DSIDs::to, DSIDs::threshold, DSIDs::range, DSIDs::turnsDown };
}
