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
    inline constexpr const char* character = "character"; // added in version 1.6: STYLE
    inline constexpr const char* lowEnd    = "thrust";   // added in version 1.6: LOW END
    inline constexpr const char* listensTo = "listensto"; // added in version 1.6
}

/** REACTS TO: the two choices (in this order). */
enum ReactsTo { peaks = 0, average = 1 };

/** CHARACTER - STYLE: the two choices (in this order). */
enum Style { modern = 0, vintage = 1 };

/** CHARACTER - LOW END: the two choices (in this order). */
enum LowEnd { normal = 0, thrust = 1 };

/** LISTENS TO: the two choices (in this order). */
enum ListensTo { listensToTrack = 0, listensToSidechain = 1 };

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
    20 means off. "reacts to" is peaks or average. "style" is modern or
    vintage, and "low end" is normal or thrust (the CHARACTER panel).
    Presets don't touch the BYPASS and AUTO-GAIN buttons, or LISTENS TO
    (which depends on how your tracks are routed): a preset that needs the
    sidechain says so in its tip.
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
    Style style;        //  modern or vintage
    LowEnd lowEnd;      //  normal or thrust
    const char* tip;
};

inline const CompPreset compressorPresets[] =
{
    //  category, name                              thresh  ratio  attack  release  make-up     mix  ignore  reacts   style    low end

    { "", "Default settings",                       -20.0f,  4.0f, 10.0f,  100.0f,   0.0f, 100.0f,  20.0f, peaks,   modern,  normal,
      "An all-round starting point. Turn THRESHOLD down until the meter moves about 3-6 dB on the loud parts." },

    // ---- Drums -----------------------------------------------------------------
    { "Drums", "Kick - Punch",                      -26.0f,  4.0f, 25.0f,   80.0f,   2.5f, 100.0f,  20.0f, peaks,   modern,  normal,
      "The slow attack lets the beater's click through. Aim for 3-6 dB on each kick." },
    { "Drums", "Kick - Rock & Metal",               -22.0f,  6.0f, 10.0f,   50.0f,   3.5f, 100.0f,  20.0f, peaks,   modern,  normal,
      "Makes every kick the same size, even in fast double-kick parts - the quick RELEASE recovers between hits. Aim for "
      "4-8 dB." },
    { "Drums", "Kick - Hip-Hop & R&B",              -21.0f,  4.0f,  5.0f,  150.0f,   3.0f, 100.0f,  20.0f, peaks,   modern,  normal,
      "Rounds off the click and holds up the body, for a fat, full kick. Aim for 3-6 dB on each kick." },
    { "Drums", "Snare - Crack",                     -24.5f,  4.0f, 15.0f,  100.0f,   3.5f, 100.0f, 100.0f, peaks,   modern,  normal,
      "Keeps the stick's crack while evening out the hits. Aim for 3-6 dB on each hit." },
    { "Drums", "Snare - Fat",                       -23.0f,  5.0f,  3.0f,  120.0f,   5.5f, 100.0f, 100.0f, peaks,   modern,  normal,
      "Clamps the first crack and brings up the body and ring, for a big rock snare. Aim for 6-10 dB on each hit." },
    { "Drums", "Snare - Jazz & Funk",               -24.5f,  2.5f, 15.0f,   80.0f,   2.5f, 100.0f, 100.0f, peaks,   modern,  normal,
      "A light touch that keeps ghost notes and accents feeling natural. Aim for 2-4 dB on the loud hits." },
    { "Drums", "Snare - Vintage Thick",             -23.5f,  4.0f, 10.0f,  120.0f,   4.0f, 100.0f, 100.0f, peaks,   vintage, normal,
      "VINTAGE follows each hit down closely, bringing up the body and ring for a thick, full snare. Aim for 4-6 dB on "
      "each hit." },
    { "Drums", "Toms - Fuller",                     -22.0f,  3.0f,  5.0f,  200.0f,   4.0f, 100.0f,  20.0f, peaks,   modern,  normal,
      "Brings up the ring of the toms. Aim for 4-6 dB on each hit." },
    { "Drums", "Hi-Hat - Even",                     -29.5f,  3.0f,  2.0f,   60.0f,   2.5f, 100.0f, 300.0f, peaks,   modern,  normal,
      "Evens out a hi-hat played unevenly. IGNORE BELOW stops kick spill setting it off. Aim for 2-4 dB." },
    { "Drums", "Overheads - Smooth",                -24.5f,  2.0f, 20.0f,  250.0f,   2.0f, 100.0f, 120.0f, peaks,   modern,  normal,
      "Gentle control of the whole kit and cymbals. Aim for just 1-3 dB." },
    { "Drums", "Overheads - Rock",                  -28.5f,  4.0f, 10.0f,  120.0f,   4.5f, 100.0f, 150.0f, peaks,   modern,  normal,
      "Brings the cymbals and the whole kit forward, for a bigger, more exciting sound. Aim for 3-6 dB." },
    { "Drums", "Room Mics - Crushed",               -31.5f, 12.0f,  1.0f,  300.0f,  12.0f, 100.0f,  20.0f, peaks,   modern,  normal,
      "Squashes the room sound for size and excitement. 10 dB or more is fine here - blend it in with the fader." },
    { "Drums", "Drum Bus - Glue",                   -22.5f,  2.0f, 30.0f,  150.0f,   1.5f, 100.0f, 100.0f, peaks,   modern,  normal,
      "Makes the kit sound like one instrument. Aim for 1-3 dB: if you can clearly hear it working, back off." },
    { "Drums", "Drum Bus - Rock & Pop",             -24.0f,  4.0f, 30.0f,  100.0f,   2.5f, 100.0f,  80.0f, peaks,   modern,  normal,
      "Punchy, upfront drums: the slow ATTACK keeps every hit sharp. Aim for 3-5 dB on the loud hits." },
    { "Drums", "Drum Bus - Hip-Hop & EDM",          -22.0f,  6.0f, 10.0f,   60.0f,   3.5f, 100.0f,  20.0f, peaks,   modern,  normal,
      "Hard and fast, for in-your-face drums. The kick makes the rest pump a little - turn IGNORE BELOW up (80-150 Hz) if "
      "you don't want that." },
    { "Drums", "Drum Bus - Jazz & Acoustic",        -21.5f,  1.5f, 30.0f,  250.0f,   1.0f, 100.0f, 100.0f, average, modern,  normal,
      "Barely there: keeps all the natural dynamics of the playing. Aim for 1-2 dB." },
    { "Drums", "Drum Bus - Parallel Smash",         -31.5f, 10.0f,  1.0f,  120.0f,  14.0f,  35.0f,  80.0f, peaks,   modern,  normal,
      "Heavy squash blended under the original (MIX 35%). Turn MIX up for more thickness, down for more natural." },
    { "Drums", "Drum Bus - Vintage Glue",           -27.0f,  4.0f, 30.0f,  150.0f,   3.0f, 100.0f,  20.0f, peaks,   vintage, thrust,
      "Our take on the classic console drum bus: VINTAGE glues the kit together and THRUST keeps the kick hitting hard. "
      "Aim for 3-5 dB on the loud hits." },
    { "Drums", "Drum Bus - Punch (THRUST)",         -29.0f,  4.0f, 20.0f,   80.0f,   3.0f, 100.0f,  20.0f, peaks,   modern,  thrust,
      "THRUST makes the compressor hear less of the kick, so the low end punches through while the snare and cymbals are "
      "held in check. Aim for 3-6 dB." },
    { "Drums", "Percussion - Even",                 -21.5f,  3.0f,  5.0f,  100.0f,   2.5f, 100.0f,  20.0f, peaks,   modern,  normal,
      "Evens out shakers, tambourine, congas and hand drums so they sit steadily in the groove. Aim for 2-4 dB." },

    // ---- Bass ------------------------------------------------------------------
    { "Bass", "Bass - Steady",                      -26.0f,  4.0f, 10.0f,  120.0f,   4.0f, 100.0f,  20.0f, average, modern,  normal,
      "Keeps every note the same size so the bass sits solidly under the mix - works in most styles. Aim for 4-6 dB." },
    { "Bass", "Bass - Rock & Punk",                 -23.5f,  4.0f,  5.0f,   80.0f,   4.0f, 100.0f,  20.0f, peaks,   modern,  normal,
      "For a picked bass: catches the pick's spikes and keeps the growl steady. Aim for 4-6 dB." },
    { "Bass", "Bass - Slap & Funk",                 -23.0f,  6.0f,  1.0f,   80.0f,   5.0f, 100.0f,  20.0f, peaks,   modern,  normal,
      "Catches the big slaps and pops so they don't jump out. Aim for 6-10 dB on the pops." },
    { "Bass", "Upright Bass - Jazz & Folk",         -25.5f,  2.5f, 15.0f,  200.0f,   3.5f, 100.0f,  20.0f, average, modern,  normal,
      "Light control that keeps the feel of the playing. Aim for 2-4 dB." },
    { "Bass", "Bass - Metal",                       -19.5f,  8.0f,  3.0f,   60.0f,   5.5f, 100.0f,  20.0f, peaks,   modern,  normal,
      "Holds a heavy bass completely steady so the guitars can sit on top. Aim for 6-10 dB." },
    { "Bass", "Synth Bass & 808",                   -16.5f,  4.0f, 20.0f,  150.0f,   3.5f, 100.0f,  20.0f, average, modern,  normal,
      "Keeps a synth bass or 808 at a steady level. On long 808s, a slower RELEASE (300 ms or more) stops the tail "
      "swelling up. Aim for 3-6 dB." },
    { "Bass", "Bass - Vintage Thick",               -24.0f,  4.0f, 10.0f,  150.0f,   4.5f, 100.0f,  20.0f, peaks,   vintage, normal,
      "Old-school feedback smoothing: holds every note up and keeps the growl thick and steady. Aim for 4-6 dB." },

    // ---- Guitars ---------------------------------------------------------------
    { "Guitars", "Electric Guitar - Clean",         -28.5f,  3.0f, 10.0f,  150.0f,   3.5f, 100.0f,  20.0f, average, modern,  normal,
      "Evens out a clean part so the quiet notes don't get lost. Aim for 3-5 dB." },
    { "Guitars", "Electric Guitar - Funk",          -24.0f,  4.0f,  2.0f,   60.0f,   4.5f, 100.0f,  20.0f, peaks,   modern,  normal,
      "Snappy control for choppy funk and R&B rhythm parts. Aim for 4-6 dB." },
    { "Guitars", "Electric Guitar - Distorted",     -19.5f,  2.0f, 20.0f,  150.0f,   1.5f, 100.0f,  20.0f, peaks,   modern,  normal,
      "Distortion already squashes the sound, so only a little is needed - just to steady it. Aim for 1-3 dB." },
    { "Guitars", "Electric Guitar - Lead",          -22.0f,  5.0f, 10.0f,  300.0f,   5.0f, 100.0f,  20.0f, average, modern,  normal,
      "Holds long notes up so solos sing and sustain. Aim for 5-8 dB." },
    { "Guitars", "Acoustic Guitar - Strummed",      -27.5f,  4.0f, 15.0f,  120.0f,   4.0f, 100.0f,  80.0f, peaks,   modern,  normal,
      "Evens out a strummed part so it sits steadily behind the vocal. Aim for 3-6 dB." },
    { "Guitars", "Acoustic Guitar - Fingerpicked",  -24.5f,  2.5f, 15.0f,  200.0f,   3.0f, 100.0f,  80.0f, average, modern,  normal,
      "Gentle and natural - keeps the dynamics of the playing. Aim for 2-4 dB." },

    // ---- Keys ------------------------------------------------------------------
    { "Keys", "Piano - Pop & Rock",                 -24.0f,  4.0f, 10.0f,  120.0f,   4.0f, 100.0f,  20.0f, peaks,   modern,  normal,
      "Holds a piano steady so it sits forward in a busy mix. Aim for 3-6 dB." },
    { "Keys", "Piano - Ballad & Jazz",              -22.5f,  2.0f, 30.0f,  300.0f,   2.0f, 100.0f,  20.0f, average, modern,  normal,
      "Just takes the edge off the loudest chords, keeping the feel of the playing. Aim for 1-3 dB." },
    { "Keys", "Electric Piano - Smooth",            -22.5f,  3.0f, 15.0f,  200.0f,   3.5f, 100.0f,  20.0f, average, modern,  normal,
      "Smooths an electric piano for soul, R&B and pop. Aim for 3-5 dB." },
    { "Keys", "Organ - Steady",                     -22.5f,  2.0f, 30.0f,  300.0f,   2.0f, 100.0f,  20.0f, average, modern,  normal,
      "An organ has little natural dynamics, so this just steadies the level. Aim for 1-3 dB." },
    { "Keys", "Synth Pad - Smooth",                 -20.5f,  2.0f, 50.0f,  400.0f,   2.0f, 100.0f,  20.0f, average, modern,  normal,
      "Gently steadies a pad so it stays in the background. Aim for 1-3 dB." },
    { "Keys", "Synth Lead - Upfront",               -18.5f,  4.0f,  5.0f,  100.0f,   3.0f, 100.0f,  20.0f, peaks,   modern,  normal,
      "Keeps a lead line upfront and even. Aim for 3-6 dB." },
    { "Keys", "Synth Pluck & Arp",                  -19.0f,  3.0f,  2.0f,   80.0f,   2.0f, 100.0f,  20.0f, peaks,   modern,  normal,
      "Tightens plucks and arpeggios so every note is the same size. Aim for 2-4 dB." },

    // ---- Vocals ----------------------------------------------------------------
    { "Vocals", "Vocal - Pop & R&B",                -28.5f,  4.0f,  5.0f,   80.0f,   6.0f, 100.0f, 100.0f, average, modern,  normal,
      "Keeps every word clear and upfront. Aim for 5-8 dB on the loud words." },
    { "Vocals", "Vocal - Rock & Metal",             -31.0f,  6.0f,  3.0f,   60.0f,   7.0f, 100.0f, 100.0f, peaks,   modern,  normal,
      "Holds a belted or screamed vocal on top of loud guitars. Aim for 6-10 dB." },
    { "Vocals", "Vocal - Ballad & Acoustic",        -22.0f,  2.5f, 15.0f,  200.0f,   3.0f, 100.0f, 100.0f, average, modern,  normal,
      "Smooths the loud notes but keeps the emotion of the quiet ones. Aim for 2-4 dB on the loud notes." },
    { "Vocals", "Vocal - Rap & Hip-Hop",            -26.0f,  5.0f,  3.0f,   60.0f,   5.5f, 100.0f, 100.0f, peaks,   modern,  normal,
      "Keeps every word at the same level so the lyrics come through the beat. Aim for 5-8 dB." },
    { "Vocals", "Vocal - Jazz & Soul",              -23.5f,  2.0f, 20.0f,  250.0f,   3.5f, 100.0f, 100.0f, average, modern,  normal,
      "Lets the singer's dynamics breathe - it just catches the biggest notes. Aim for 2-3 dB." },
    { "Vocals", "Vocal - Country & Folk",           -27.5f,  3.0f, 10.0f,  150.0f,   6.0f, 100.0f, 100.0f, average, modern,  normal,
      "A warm, steady vocal that still sounds natural. Aim for 3-5 dB." },
    { "Vocals", "Vocal - Vintage Smooth",           -29.0f,  3.0f, 10.0f,  150.0f,   5.0f, 100.0f, 100.0f, average, vintage, normal,
      "A smooth, forgiving squeeze that keeps every word forward without sounding squashed. Aim for 4-6 dB on the loud "
      "words." },
    { "Vocals", "Vocal - Second Compressor",        -24.5f,  2.0f, 30.0f,  200.0f,   2.0f, 100.0f, 100.0f, average, modern,  normal,
      "For a second Easy Comp after the first: a little more smoothing (1-3 dB) sounds more natural than one compressor "
      "doing it all." },
    { "Vocals", "Vocal - Parallel Thickener",       -33.5f, 10.0f,  1.0f,  150.0f,  12.5f,  30.0f, 100.0f, peaks,   modern,  normal,
      "Blends a heavily squashed copy under the original (MIX 30%), for a thicker vocal that keeps its dynamics." },
    { "Vocals", "Backing Vocals - Tucked In",       -25.5f,  5.0f,  5.0f,  100.0f,   6.5f, 100.0f, 100.0f, average, modern,  normal,
      "Holds harmonies and doubles steady so they sit behind the lead. Aim for 5-8 dB." },
    { "Vocals", "Spoken Word & Podcast",            -27.0f,  3.0f, 10.0f,  150.0f,   5.0f, 100.0f,  80.0f, average, modern,  normal,
      "Keeps a speaking voice at an easy, even level. Aim for 4-6 dB." },

    // ---- Strings, Brass & Wind -------------------------------------------------
    { "Strings, Brass & Wind", "Strings - Section", -23.0f,  2.0f, 30.0f,  300.0f,   2.0f, 100.0f,  20.0f, average, modern,  normal,
      "Gentle control for a string section that keeps the swells. Aim for 1-3 dB." },
    { "Strings, Brass & Wind", "Strings - Solo Violin & Cello", -23.0f,  2.5f, 20.0f,  200.0f,   3.5f, 100.0f,  20.0f, average, modern,  normal,
      "Smooths a solo string part so the quiet notes aren't lost. Aim for 2-4 dB." },
    { "Strings, Brass & Wind", "Brass Section - Punchy", -24.0f,  4.0f, 10.0f,  100.0f,   4.0f, 100.0f,  20.0f, peaks,   modern,  normal,
      "Tight, punchy horn stabs for funk, soul and pop. Aim for 3-6 dB." },
    { "Strings, Brass & Wind", "Horns - Solo Trumpet & Sax", -24.0f,  3.0f, 10.0f,  150.0f,   3.5f, 100.0f,  20.0f, average, modern,  normal,
      "Evens out a solo horn without losing its expression. Aim for 3-5 dB." },
    { "Strings, Brass & Wind", "Flute & Woodwind - Gentle", -18.5f,  2.0f, 15.0f,  200.0f,   2.0f, 100.0f,  20.0f, average, modern,  normal,
      "Light control for flute, clarinet and other woodwind. Aim for 1-3 dB." },

    // ---- Mix Bus ---- Mix Bus & Master --------- Mastering --------------------------------------------------
    { "Mix Bus", "Mix Bus - Glue (Pop & Rock)", -20.5f,  2.0f, 30.0f,  200.0f,   1.5f, 100.0f, 100.0f, average, modern,  normal,
      "Ties the whole mix together. Aim for 1-3 dB - if you can clearly hear it working, back off." },
    { "Mix Bus", "Mix Bus - Hip-Hop & EDM", -28.0f,  4.0f, 30.0f,  100.0f,   2.5f, 100.0f, 120.0f, peaks,   modern,  normal,
      "Firmer glue with punch. IGNORE BELOW stops the kick and bass making the whole mix pump. Aim for 2-4 dB." },
    { "Mix Bus", "Mix Bus - Jazz, Folk & Classical", -21.0f,  1.5f, 30.0f,  400.0f,   1.0f, 100.0f, 100.0f, average, modern,  normal,
      "Barely-there control that keeps all the natural dynamics. Aim for about 1 dB - you should hardly hear it." },
    { "Mix Bus", "Mix Bus - Vintage Glue", -24.5f,  2.0f, 30.0f,  200.0f,   1.5f, 100.0f,  20.0f, peaks,   vintage, thrust,
      "Classic bus glue: VINTAGE ties the mix together and THRUST stops the kick and bass making it pump. Aim for 1-3 dB." },
    { "Mix Bus", "Group Bus - Glue",       -21.5f,  2.0f, 20.0f,  200.0f,   2.0f, 100.0f,  80.0f, average, modern,  normal,
      "Ties a group of tracks together - guitars, keys or backing vocals sent to one bus. Aim for 1-3 dB." },
    { "Mastering", "Master - Light Touch",   -16.5f,  1.5f, 30.0f,  300.0f,   1.0f, 100.0f, 100.0f, average, modern,  normal,
      "A gentle 1-2 dB of control for a finished mix, at the very end of the chain. (For real loudness, a limiter comes "
      "after it.)" },

    // ---- Sidechain Tricks ------------------------------------------------------
    { "Sidechain Tricks", "Bass - Ducked by the Kick", -19.5f,  4.0f,  1.0f,  100.0f,   0.0f, 100.0f,  20.0f, peaks,   modern,  normal,
      "Send the kick to the sidechain and set LISTENS TO: SIDECHAIN. The bass dips on every kick so the two don't fight. "
      "Aim for 3-6 dB on each kick." },
    { "Sidechain Tricks", "Music - Ducked Under a Voice", -33.0f,  4.0f, 20.0f,  500.0f,   0.0f, 100.0f, 100.0f, average, modern,  normal,
      "For podcasts and voice-overs: send the voice to the sidechain, LISTENS TO: SIDECHAIN. The music dips while someone "
      "talks and comes back in the pauses. Aim for 6-10 dB." },
    { "Sidechain Tricks", "Synth Pad - Pumping to the Kick", -23.5f, 10.0f,  0.5f,  180.0f,   0.0f, 100.0f,  20.0f, peaks,   modern,  normal,
      "The dance-music pump: send the kick to the sidechain, LISTENS TO: SIDECHAIN. The pad ducks hard on every kick and "
      "swells back in between. Aim for 8-12 dB." },
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
                               { CompIDs::lowCut,    p.lowCutHz },    { CompIDs::reactsTo, (float) p.reactsTo },
                               { CompIDs::character, (float) p.style }, { CompIDs::lowEnd,  (float) p.lowEnd } } });

    return presets;
}

/** The settings presets store (everything except BYPASS, AUTO-GAIN and LISTENS TO). */
inline juce::StringArray compressorPresetIDs()
{
    return { CompIDs::threshold, CompIDs::ratio, CompIDs::attack, CompIDs::release,
             CompIDs::makeup, CompIDs::mix, CompIDs::lowCut, CompIDs::reactsTo,
             CompIDs::character, CompIDs::lowEnd };
}
