#pragma once

#include "PresetBrowser.h"

//==============================================================================
/*
    Parameter IDs: the names the plugin uses internally, and that your DAW
    stores in saved projects. Don't rename these once you've used the plugin
    in a project, or that project won't find its settings again.
*/
namespace TapeIDs
{
    inline constexpr const char* speed   = "speed";     // 15 IPS / 30 IPS / CASSETTE
    inline constexpr const char* drive   = "drive";     // the record level, dB
    inline constexpr const char* wow     = "wow";       // WOW & FLUTTER, %
    inline constexpr const char* hiss    = "hiss";      // %
    inline constexpr const char* quality = "quality";   // OVERSAMPLING: Auto / Off / 4x / 8x
}

//==============================================================================
/*
    The presets.

    DRIVE is the record level: 0 dB means a track at -18 dBFS (RMS) - a
    normal recording level - hits the tape at its normal operating level
    (0 VU on the meter). A hot mix bus is already several dB over at 0, so
    watch the VU meter rather than the number: around 0 VU is the subtle
    "it's on tape" sound; +3 to +6 thickens and rounds the peaks; +8 and
    up is pushing it, the way engineers hit tape on drums.

    The real-machine presets keep WOW & FLUTTER and HISS at 0 - on a
    good machine they're barely there. Only the cassette ones use them.

    speed: 0 = 15 IPS, 1 = 30 IPS, 2 = CASSETTE
*/
struct TapePreset
{
    const char* category;
    const char* name;
    int   speed;
    float driveDb;          // -12 .. +18
    float wowPercent;       //   0 .. 100
    float hissPercent;      //   0 .. 100
    const char* tip;
};

inline constexpr int ips15 = 0, ips30 = 1, cassette = 2;

inline const TapePreset tapePresets[] =
{
    //  category, name                                   speed     drive   wow   hiss

    { "", "Default settings",                              ips15,     0.0f,  0.0f,  0.0f,
      "A 24-track at 15 ips, lined up at the normal level: the round, deep low end of the head bump and a touch of "
      "saturation on the loudest moments. Turn DRIVE up to hit the tape harder." },

    // ---- Drums -----------------------------------------------------------------
    { "Drums", "Kick - Round Low End",                     ips15,     4.0f,  0.0f,  0.0f,
      "The head bump fattens the kick's low end, and the tape rounds the click just a little." },
    { "Drums", "Snare - Softened Crack",                   ips15,     7.0f,  0.0f,  0.0f,
      "Hits the tape hard enough that the snare's spike rounds over and the body comes up - a classic recorded-to-tape snare." },
    { "Drums", "Overheads - Smoother Cymbals",             ips15,     5.0f,  0.0f,  0.0f,
      "Loud cymbal crashes soften (the highs reach the tape's limit first); quiet detail is left alone." },
    { "Drums", "Room Mics - Squashed Into Tape",           ips15,    10.0f,  0.0f,  0.0f,
      "Room mics pushed well into the tape: thick, compressed and dense." },

    // ---- Drum Bus --------------------------------------------------------------
    { "Drum Bus", "Drum Bus - On Tape",                    ips15,     3.0f,  0.0f,  0.0f,
      "The whole kit printed to tape at a healthy level: weight in the lows, the edges slightly rounded." },
    { "Drum Bus", "Drum Bus - Hit the Tape Hard",          ips15,     9.0f,  0.0f,  0.0f,
      "The 70s rock trick: the kit recorded hot, so the tape squashes the peaks and everything sounds bigger and thicker." },

    // ---- Bass ------------------------------------------------------------------
    { "Bass", "Bass - Warm & Round",                       ips15,     4.0f,  0.0f,  0.0f,
      "The head bump lifts the deep notes and the tape evens out the plucks. DI bass sounds recorded." },
    { "Bass", "Bass - Pushed Into Tape",                   ips15,     9.0f,  0.0f,  0.0f,
      "Pushed hard: thicker, more even, a slight growl on the loudest notes." },

    // ---- Guitars ---------------------------------------------------------------
    { "Guitars", "Electric Guitar - Smoother Edges",       ips15,     5.0f,  0.0f,  0.0f,
      "Takes the fizz off the loudest parts of a distorted guitar and gives it more body." },
    { "Guitars", "Acoustic Guitar - Gentle Warmth",        ips30,     2.0f,  0.0f,  0.0f,
      "30 ips: almost perfect, so the acoustic stays clear and open, just a little warmer and rounder." },

    // ---- Vocals ----------------------------------------------------------------
    { "Vocals", "Vocal - Subtle Tape",                     ips15,     2.0f,  0.0f,  0.0f,
      "A gentle tape sound on a vocal: loud S's and peaks soften a little, nothing else changes." },
    { "Vocals", "Vocal - Warm & Thick",                    ips15,     6.0f,  0.0f,  0.0f,
      "Pushed a bit harder for a thicker, more even vocal that sits in a rock mix." },
    { "Vocals", "Lo-Fi Vocal - Cassette",                  cassette,  4.0f, 35.0f, 25.0f,
      "A vocal bounced to cassette: soft top, a little wobble and hiss. For lo-fi and demo-tape sounds." },

    // ---- Keys & Synths ---------------------------------------------------------
    { "Keys & Synths", "Synth - Analogue Warmth",          ips15,     5.0f,  0.0f,  0.0f,
      "Takes the digital edge off soft synths: deeper lows, rounded peaks." },
    { "Keys & Synths", "Keys - Worn Cassette",             cassette,  3.0f, 55.0f, 35.0f,
      "Piano or keys on a well-used cassette: the pitch drifts gently, hiss underneath. Lo-fi hip-hop and chillwave." },

    // ---- Mix Bus ---------------------------------------------------------------
    { "Mix Bus", "Mix Bus - 15 ips Glue",                  ips15,     0.0f,  0.0f,  0.0f,
      "The whole mix through a 15 ips machine at the normal level: a deeper, rounder low end and gently held-in peaks." },
    { "Mix Bus", "Mix Bus - 15 ips Pushed",                ips15,     5.0f,  0.0f,  0.0f,
      "Hit a bit harder: denser and thicker, the loudest peaks and cymbals rounded off. Rock and punk." },
    { "Mix Bus", "Mix Bus - 30 ips Clean Warmth",          ips30,     2.0f,  0.0f,  0.0f,
      "30 ips on the mix: barely there - a little extra weight around 100 Hz and slightly softer peaks." },
    { "Mix Bus", "Lo-Fi Bus - Cassette Demo",              cassette,  4.0f, 35.0f, 25.0f,
      "The whole mix bounced to a decent cassette: softer top, a little wobble, a little hiss." },
    { "Mix Bus", "Lo-Fi Bus - Worn-Out Walkman",           cassette,  8.0f, 85.0f, 55.0f,
      "A tape that's been played a hundred times: obvious wobble, hiss, squashed highs." },

    // ---- Mastering -------------------------------------------------------------
    { "Mastering", "Master - 30 ips Touch",                ips30,     0.0f,  0.0f,  0.0f,
      "Mixed down to a 30 ips half-inch: the most subtle setting there is. A little weight, nothing you'd point at." },
    { "Mastering", "Master - 30 ips Warmth",               ips30,     3.0f,  0.0f,  0.0f,
      "30 ips, a little hotter: the peaks round over slightly and the mix sounds a touch fuller." },
    { "Mastering", "Master - 15 ips Weight",               ips15,     0.0f,  0.0f,  0.0f,
      "Mixed down at 15 ips: the deeper head bump adds weight under the kick and bass." },
};

//==============================================================================
inline std::vector<Preset> makeTapePresets()
{
    std::vector<Preset> presets;

    for (auto& p : tapePresets)
        presets.push_back ({ p.category, p.name, p.tip,
                             { { TapeIDs::speed, (float) p.speed }, { TapeIDs::drive, p.driveDb },
                               { TapeIDs::wow, p.wowPercent },      { TapeIDs::hiss, p.hissPercent } } });

    return presets;
}

/** The settings presets store (everything except BYPASS, AUTO-GAIN and OVERSAMPLING). */
inline juce::StringArray tapePresetIDs()
{
    return { TapeIDs::speed, TapeIDs::drive, TapeIDs::wow, TapeIDs::hiss };
}
