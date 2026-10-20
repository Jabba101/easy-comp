#pragma once

#include "PresetBrowser.h"

//==============================================================================
/*
    Parameter IDs: the names the plugin uses internally, and that your DAW
    stores in saved projects. Don't rename these once you've used the plugin
    in a project, or that project won't find its settings again.
*/
namespace CompIDs
{
    inline constexpr const char* threshold = "threshold";
    inline constexpr const char* ratio     = "ratio";
    inline constexpr const char* attack    = "attack";
    inline constexpr const char* release   = "release";
    inline constexpr const char* makeup    = "makeup";
    inline constexpr const char* mix       = "mix";
    inline constexpr const char* lowCut    = "lowcut";   // added in version 1.1
    inline constexpr const char* reactsTo  = "reactsto"; // added in version 1.3
}

/** REACTS TO: the two choices (in this order). */
enum ReactsTo { peaks = 0, average = 1 };

//==============================================================================
/*
    The presets.

    They're starting points, not magic numbers: every recording hits the
    plugin at a different level. After choosing one, adjust THRESHOLD until
    the meter shows the amount the tip suggests.

    To add your own: copy a line, then change the category (which sub-menu
    it's in), the name, the numbers and the tip. (You can also save presets
    from the plugin's menu without touching any code.)

    "ignore below" is the low-cut for what the compressor listens to:
    20 means off. "reacts to" is peaks or average. Presets don't touch the
    BYPASS and AUTO-GAIN buttons.
*/
struct CompPreset
{
    const char* category;
    const char* name;
    float thresholdDb;  // -60 .. 0
    float ratio;        //   1 .. 20
    float attackMs;     // 0.1 .. 100
    float releaseMs;    //  10 .. 1000
    float makeupDb;     //   0 .. 24
    float mixPercent;   //   0 .. 100
    float lowCutHz;     //  20 (off) .. 500
    ReactsTo reactsTo;  //  peaks or average
    const char* tip;
};

inline const CompPreset compressorPresets[] =
{
    //  category, name                              thresh  ratio  attack  release  make-up     mix  ignore  reacts to

    { "", "Default settings",                       -20.0f,  4.0f, 10.0f,  100.0f,   0.0f, 100.0f,  20.0f, peaks,
      "An all-round starting point. Turn THRESHOLD down until the meter moves about 3-6 dB on the loud parts." },

    // ---- Drums -----------------------------------------------------------------
    { "Drums", "Kick - Punch",                      -26.0f,  4.0f, 25.0f,   80.0f,   2.5f, 100.0f,  20.0f, peaks,
      "The slow attack lets the beater's click through. Aim for 3-6 dB on each kick." },
    { "Drums", "Kick - Rock & Metal",               -22.0f,  6.0f, 10.0f,   50.0f,   3.5f, 100.0f,  20.0f, peaks,
      "Makes every kick the same size, even in fast double-kick parts - the quick RELEASE recovers between hits. Aim for "
      "4-8 dB." },
    { "Drums", "Kick - Hip-Hop & R&B",              -21.0f,  4.0f,  5.0f,  150.0f,   3.0f, 100.0f,  20.0f, peaks,
      "Rounds off the click and holds up the body, for a fat, full kick. Aim for 3-6 dB on each kick." },
    { "Drums", "Snare - Crack",                     -24.5f,  4.0f, 15.0f,  100.0f,   3.5f, 100.0f, 100.0f, peaks,
      "Keeps the stick's crack while evening out the hits. Aim for 3-6 dB on each hit." },
    { "Drums", "Snare - Fat",                       -23.0f,  5.0f,  3.0f,  120.0f,   5.5f, 100.0f, 100.0f, peaks,
      "Clamps the first crack and brings up the body and ring, for a big rock snare. Aim for 6-10 dB on each hit." },
    { "Drums", "Snare - Jazz & Funk",               -24.5f,  2.5f, 15.0f,   80.0f,   2.5f, 100.0f, 100.0f, peaks,
      "A light touch that keeps ghost notes and accents feeling natural. Aim for 2-4 dB on the loud hits." },
    { "Drums", "Toms - Fuller",                     -22.0f,  3.0f,  5.0f,  200.0f,   4.0f, 100.0f,  20.0f, peaks,
      "Brings up the ring of the toms. Aim for 4-6 dB on each hit." },
    { "Drums", "Hi-Hat - Even",                     -29.5f,  3.0f,  2.0f,   60.0f,   2.5f, 100.0f, 300.0f, peaks,
      "Evens out a hi-hat played unevenly. IGNORE BELOW stops kick spill setting it off. Aim for 2-4 dB." },
    { "Drums", "Overheads - Smooth",                -24.5f,  2.0f, 20.0f,  250.0f,   2.0f, 100.0f, 120.0f, peaks,
      "Gentle control of the whole kit and cymbals. Aim for just 1-3 dB." },
    { "Drums", "Overheads - Rock",                  -28.5f,  4.0f, 10.0f,  120.0f,   4.5f, 100.0f, 150.0f, peaks,
      "Brings the cymbals and the whole kit forward, for a bigger, more exciting sound. Aim for 3-6 dB." },
    { "Drums", "Room Mics - Crushed",               -31.5f, 12.0f,  1.0f,  300.0f,  12.0f, 100.0f,  20.0f, peaks,
      "Squashes the room sound for size and excitement. 10 dB or more is fine here - blend it in with the fader." },
    { "Drums", "Drum Bus - Glue",                   -22.5f,  2.0f, 30.0f,  150.0f,   1.5f, 100.0f, 100.0f, peaks,
      "Makes the kit sound like one instrument. Aim for 1-3 dB: if you can clearly hear it working, back off." },
    { "Drums", "Drum Bus - Rock & Pop",             -24.0f,  4.0f, 30.0f,  100.0f,   2.5f, 100.0f,  80.0f, peaks,
      "Punchy, upfront drums: the slow ATTACK keeps every hit sharp. Aim for 3-5 dB on the loud hits." },
    { "Drums", "Drum Bus - Hip-Hop & EDM",          -22.0f,  6.0f, 10.0f,   60.0f,   3.5f, 100.0f,  20.0f, peaks,
      "Hard and fast, for in-your-face drums. The kick makes the rest pump a little - turn IGNORE BELOW up (80-150 Hz) if "
      "you don't want that." },
    { "Drums", "Drum Bus - Jazz & Acoustic",        -21.5f,  1.5f, 30.0f,  250.0f,   1.0f, 100.0f, 100.0f, average,
      "Barely there: keeps all the natural dynamics of the playing. Aim for 1-2 dB." },
    { "Drums", "Drum Bus - Parallel Smash",         -31.5f, 10.0f,  1.0f,  120.0f,  14.0f,  35.0f,  80.0f, peaks,
      "Heavy squash blended under the original (MIX 35%). Turn MIX up for more thickness, down for more natural." },
    { "Drums", "Percussion - Even",                 -21.5f,  3.0f,  5.0f,  100.0f,   2.5f, 100.0f,  20.0f, peaks,
      "Evens out shakers, tambourine, congas and hand drums so they sit steadily in the groove. Aim for 2-4 dB." },

    // ---- Bass ------------------------------------------------------------------
    { "Bass", "Bass - Steady",                      -26.0f,  4.0f, 10.0f,  120.0f,   4.0f, 100.0f,  20.0f, average,
      "Keeps every note the same size so the bass sits solidly under the mix - works in most styles. Aim for 4-6 dB." },
    { "Bass", "Bass - Rock & Punk",                 -23.5f,  4.0f,  5.0f,   80.0f,   4.0f, 100.0f,  20.0f, peaks,
      "For a picked bass: catches the pick's spikes and keeps the growl steady. Aim for 4-6 dB." },
    { "Bass", "Bass - Slap & Funk",                 -23.0f,  6.0f,  1.0f,   80.0f,   5.0f, 100.0f,  20.0f, peaks,
      "Catches the big slaps and pops so they don't jump out. Aim for 6-10 dB on the pops." },
    { "Bass", "Upright Bass - Jazz & Folk",         -25.5f,  2.5f, 15.0f,  200.0f,   3.5f, 100.0f,  20.0f, average,
      "Light control that keeps the feel of the playing. Aim for 2-4 dB." },
    { "Bass", "Bass - Metal",                       -19.5f,  8.0f,  3.0f,   60.0f,   5.5f, 100.0f,  20.0f, peaks,
      "Holds a heavy bass completely steady so the guitars can sit on top. Aim for 6-10 dB." },
    { "Bass", "Synth Bass & 808",                   -16.5f,  4.0f, 20.0f,  150.0f,   3.5f, 100.0f,  20.0f, average,
      "Keeps a synth bass or 808 at a steady level. On long 808s, a slower RELEASE (300 ms or more) stops the tail "
      "swelling up. Aim for 3-6 dB." },

    // ---- Guitars ---------------------------------------------------------------
    { "Guitars", "Electric Guitar - Clean",         -28.5f,  3.0f, 10.0f,  150.0f,   3.5f, 100.0f,  20.0f, average,
      "Evens out a clean part so the quiet notes don't get lost. Aim for 3-5 dB." },
    { "Guitars", "Electric Guitar - Funk",          -24.0f,  4.0f,  2.0f,   60.0f,   4.5f, 100.0f,  20.0f, peaks,
      "Snappy control for choppy funk and R&B rhythm parts. Aim for 4-6 dB." },
    { "Guitars", "Electric Guitar - Distorted",     -19.5f,  2.0f, 20.0f,  150.0f,   1.5f, 100.0f,  20.0f, peaks,
      "Distortion already squashes the sound, so only a little is needed - just to steady it. Aim for 1-3 dB." },
    { "Guitars", "Electric Guitar - Lead",          -22.0f,  5.0f, 10.0f,  300.0f,   5.0f, 100.0f,  20.0f, average,
      "Holds long notes up so solos sing and sustain. Aim for 5-8 dB." },
    { "Guitars", "Acoustic Guitar - Strummed",      -27.5f,  4.0f, 15.0f,  120.0f,   4.0f, 100.0f,  80.0f, peaks,
      "Evens out a strummed part so it sits steadily behind the vocal. Aim for 3-6 dB." },
    { "Guitars", "Acoustic Guitar - Fingerpicked",  -24.5f,  2.5f, 15.0f,  200.0f,   3.0f, 100.0f,  80.0f, average,
      "Gentle and natural - keeps the dynamics of the playing. Aim for 2-4 dB." },

    // ---- Keys ------------------------------------------------------------------
    { "Keys", "Piano - Pop & Rock",                 -24.0f,  4.0f, 10.0f,  120.0f,   4.0f, 100.0f,  20.0f, peaks,
      "Holds a piano steady so it sits forward in a busy mix. Aim for 3-6 dB." },
    { "Keys", "Piano - Ballad & Jazz",              -22.5f,  2.0f, 30.0f,  300.0f,   2.0f, 100.0f,  20.0f, average,
      "Just takes the edge off the loudest chords, keeping the feel of the playing. Aim for 1-3 dB." },
    { "Keys", "Electric Piano - Smooth",            -22.5f,  3.0f, 15.0f,  200.0f,   3.5f, 100.0f,  20.0f, average,
      "Smooths an electric piano for soul, R&B and pop. Aim for 3-5 dB." },
    { "Keys", "Organ - Steady",                     -22.5f,  2.0f, 30.0f,  300.0f,   2.0f, 100.0f,  20.0f, average,
      "An organ has little natural dynamics, so this just steadies the level. Aim for 1-3 dB." },
    { "Keys", "Synth Pad - Smooth",                 -20.5f,  2.0f, 50.0f,  400.0f,   2.0f, 100.0f,  20.0f, average,
      "Gently steadies a pad so it stays in the background. Aim for 1-3 dB." },
    { "Keys", "Synth Lead - Upfront",               -18.5f,  4.0f,  5.0f,  100.0f,   3.0f, 100.0f,  20.0f, peaks,
      "Keeps a lead line upfront and even. Aim for 3-6 dB." },
    { "Keys", "Synth Pluck & Arp",                  -19.0f,  3.0f,  2.0f,   80.0f,   2.0f, 100.0f,  20.0f, peaks,
      "Tightens plucks and arpeggios so every note is the same size. Aim for 2-4 dB." },

    // ---- Vocals ----------------------------------------------------------------
    { "Vocals", "Vocal - Pop & R&B",                -28.5f,  4.0f,  5.0f,   80.0f,   6.0f, 100.0f, 100.0f, average,
      "Keeps every word clear and upfront. Aim for 5-8 dB on the loud words." },
    { "Vocals", "Vocal - Rock & Metal",             -31.0f,  6.0f,  3.0f,   60.0f,   7.0f, 100.0f, 100.0f, peaks,
      "Holds a belted or screamed vocal on top of loud guitars. Aim for 6-10 dB." },
    { "Vocals", "Vocal - Ballad & Acoustic",        -22.0f,  2.5f, 15.0f,  200.0f,   3.0f, 100.0f, 100.0f, average,
      "Smooths the loud notes but keeps the emotion of the quiet ones. Aim for 2-4 dB on the loud notes." },
    { "Vocals", "Vocal - Rap & Hip-Hop",            -26.0f,  5.0f,  3.0f,   60.0f,   5.5f, 100.0f, 100.0f, peaks,
      "Keeps every word at the same level so the lyrics come through the beat. Aim for 5-8 dB." },
    { "Vocals", "Vocal - Jazz & Soul",              -23.5f,  2.0f, 20.0f,  250.0f,   3.5f, 100.0f, 100.0f, average,
      "Lets the singer's dynamics breathe - it just catches the biggest notes. Aim for 2-3 dB." },
    { "Vocals", "Vocal - Country & Folk",           -27.5f,  3.0f, 10.0f,  150.0f,   6.0f, 100.0f, 100.0f, average,
      "A warm, steady vocal that still sounds natural. Aim for 3-5 dB." },
    { "Vocals", "Vocal - Second Compressor",        -24.5f,  2.0f, 30.0f,  200.0f,   2.0f, 100.0f, 100.0f, average,
      "For a second Easy Comp after the first: a little more smoothing (1-3 dB) sounds more natural than one compressor "
      "doing it all." },
    { "Vocals", "Vocal - Parallel Thickener",       -33.5f, 10.0f,  1.0f,  150.0f,  12.5f,  30.0f, 100.0f, peaks,
      "Blends a heavily squashed copy under the original (MIX 30%), for a thicker vocal that keeps its dynamics." },
    { "Vocals", "Backing Vocals - Tucked In",       -25.5f,  5.0f,  5.0f,  100.0f,   6.5f, 100.0f, 100.0f, average,
      "Holds harmonies and doubles steady so they sit behind the lead. Aim for 5-8 dB." },
    { "Vocals", "Spoken Word & Podcast",            -27.0f,  3.0f, 10.0f,  150.0f,   5.0f, 100.0f,  80.0f, average,
      "Keeps a speaking voice at an easy, even level. Aim for 4-6 dB." },

    // ---- Strings, Brass & Wind -------------------------------------------------
    { "Strings, Brass & Wind", "Strings - Section", -23.0f,  2.0f, 30.0f,  300.0f,   2.0f, 100.0f,  20.0f, average,
      "Gentle control for a string section that keeps the swells. Aim for 1-3 dB." },
    { "Strings, Brass & Wind", "Strings - Solo Violin & Cello",-23.0f,  2.5f, 20.0f,  200.0f,   3.5f, 100.0f,  20.0f, average,
      "Smooths a solo string part so the quiet notes aren't lost. Aim for 2-4 dB." },
    { "Strings, Brass & Wind", "Brass Section - Punchy",-24.0f,  4.0f, 10.0f,  100.0f,   4.0f, 100.0f,  20.0f, peaks,
      "Tight, punchy horn stabs for funk, soul and pop. Aim for 3-6 dB." },
    { "Strings, Brass & Wind", "Horns - Solo Trumpet & Sax",-24.0f,  3.0f, 10.0f,  150.0f,   3.5f, 100.0f,  20.0f, average,
      "Evens out a solo horn without losing its expression. Aim for 3-5 dB." },
    { "Strings, Brass & Wind", "Flute & Woodwind - Gentle",-18.5f,  2.0f, 15.0f,  200.0f,   2.0f, 100.0f,  20.0f, average,
      "Light control for flute, clarinet and other woodwind. Aim for 1-3 dB." },

    // ---- Mix Bus & Master ------------------------------------------------------
    { "Mix Bus & Master", "Mix Bus - Glue (Pop & Rock)",-20.5f,  2.0f, 30.0f,  200.0f,   1.5f, 100.0f, 100.0f, average,
      "Ties the whole mix together. Aim for 1-3 dB - if you can clearly hear it working, back off." },
    { "Mix Bus & Master", "Mix Bus - Hip-Hop & EDM",-28.0f,  4.0f, 30.0f,  100.0f,   2.5f, 100.0f, 120.0f, peaks,
      "Firmer glue with punch. IGNORE BELOW stops the kick and bass making the whole mix pump. Aim for 2-4 dB." },
    { "Mix Bus & Master", "Mix Bus - Jazz, Folk & Classical",-21.0f,  1.5f, 30.0f,  400.0f,   1.0f, 100.0f, 100.0f, average,
      "Barely-there control that keeps all the natural dynamics. Aim for about 1 dB - you should hardly hear it." },
    { "Mix Bus & Master", "Group Bus - Glue",       -21.5f,  2.0f, 20.0f,  200.0f,   2.0f, 100.0f,  80.0f, average,
      "Ties a group of tracks together - guitars, keys or backing vocals sent to one bus. Aim for 1-3 dB." },
    { "Mix Bus & Master", "Master - Light Touch",   -16.5f,  1.5f, 30.0f,  300.0f,   1.0f, 100.0f, 100.0f, average,
      "A gentle 1-2 dB of control for a finished mix, at the very end of the chain. (For real loudness, a limiter comes "
      "after it.)" },
};

//==============================================================================
inline std::vector<Preset> makeCompressorPresets()
{
    std::vector<Preset> presets;

    for (auto& p : compressorPresets)
        presets.push_back ({ p.category, p.name, p.tip,
                             { { CompIDs::threshold, p.thresholdDb }, { CompIDs::ratio,    p.ratio },
                               { CompIDs::attack,    p.attackMs },    { CompIDs::release,  p.releaseMs },
                               { CompIDs::makeup,    p.makeupDb },    { CompIDs::mix,      p.mixPercent },
                               { CompIDs::lowCut,    p.lowCutHz },    { CompIDs::reactsTo, (float) p.reactsTo } } });

    return presets;
}

/** The settings presets store (everything except BYPASS and AUTO-GAIN). */
inline juce::StringArray compressorPresetIDs()
{
    return { CompIDs::threshold, CompIDs::ratio, CompIDs::attack, CompIDs::release,
             CompIDs::makeup, CompIDs::mix, CompIDs::lowCut, CompIDs::reactsTo };
}
