#pragma once

/** The words the help ticker shows for each of Easy Master's controls. */
namespace MasterHints
{
    inline constexpr const char* tamerAmountHint =
        "The most it will turn any spot down. 2-4 dB is usually plenty on a master; 6 dB and more for harsh or boxy mixes "
        "(and AI-made songs). It only acts where something sticks out - more AMOUNT doesn't mean more of the time.";
    inline constexpr const char* tamerSensHint =
        "How little a spot has to stick out above the sound around it before it's turned down. Higher catches more, "
        "smaller bumps; too high starts to dull the mix. Check with LISTEN.";
    inline constexpr const char* tamerSharpHint =
        "SHARP (high): only narrow, ringing spots - a whistling resonance, a ringing snare, a honk. SOFT (low): broader "
        "areas too - boxiness, harshness spread over a range.";

    inline constexpr const char* thresholdHint =
        "Where the compressor starts to work. For glue on a master, set it so the meter shows 1-3 dB of turning down on the "
        "loud parts - more than about 4 dB and you'll hear it squeeze.";
    inline constexpr const char* ratioHint =
        "How firmly it turns down above the THRESHOLD. 1.5:1 - 2:1 is glue; 3:1 and more is obvious compression. On a "
        "master, gentle almost always sounds better.";
    inline constexpr const char* attackHint =
        "How quickly it reacts. 20-50 ms lets the punch of the kick and snare through before it acts; fast (under 10 ms) "
        "flattens the hits.";
    inline constexpr const char* releaseHint =
        "How quickly it lets go. Set it to move with the song: 100-200 ms for busy rock, 300 ms+ for slow, smooth music. "
        "Too fast pumps, too slow sits on everything.";
    inline constexpr const char* makeupHint =
        "Turns the compressed sound back up. On a master you mostly leave this low and let the LIMITER's GAIN set the "
        "loudness.";

    inline constexpr const char* trimHint =
        "Turns the mix up or down before anything else, so the compressor sees a sensible level. If the warning shows "
          "the mix clipping on the way in, turn the mix down in your DAW instead (the clip is already there).";
    inline constexpr const char* compMixHint =
        "How much of the compressed sound you hear, blended with the uncompressed. 70-85% keeps the hits' snap with most "
          "of the glue (parallel compression).";
    inline constexpr const char* ignoreBelowHint =
        "The compressor doesn't react to the deep bass under this frequency (it still compresses it). Stops a heavy kick "
          "and bass making the whole mix pump. 60-100 Hz for heavy low end; Off for none.";
    inline constexpr const char* limitGainHint =
        "Turns the master up into the ceiling: each dB more is a dB louder, with the peaks held at the ceiling. Use MATCH "
          "to hear whether it's still clean - when the kick and snare soften, back off.";
    inline constexpr const char* ceilingHint =
        "The most the output may ever reach, as true peak (the peaks between the samples too). -0.3 dBTP by default; "
          "-1 dBTP for files that will be converted to MP3 / AAC.";
    inline constexpr const char* limitReleaseTimeHint =
        "With RELEASE on SET: how quickly the limiter lets go. AUTO is almost always best (quick after a lone peak, "
          "slower through a sustained passage - no pumping, no distortion).";
    inline constexpr const char* tamerSpeedHint =
        "How quickly it reacts to a spot that rings out, and lets go after. Fast (low) catches a single note that rings; "
          "slow (high) is smoother and acts more like a gentle, moving EQ.";
    inline constexpr const char* tamerFromHint =
        "The lowest frequency it works on (soft edge). 200 Hz by default: low notes are music, not resonances. Lower it "
          "for a boomy room or a bass note that booms.";
    inline constexpr const char* tamerToHint =
        "The highest frequency it works on (soft edge). Set FROM 4 kHz and TO 16 kHz to work only on harshness and fizz.";

    inline constexpr const char* gettingStartedHint =
        "Pick a preset for the song, set TRIM so the compressor shows 1-3 dB, push the limiter's GAIN to taste, then check "
        "with MATCH and BYPASS. Hover over anything for help.";
    inline constexpr const char* compOnHint = "Switches the compressor on or off (the rest of the chain keeps working).";
    inline constexpr const char* modeHint =
        "STEREO: one compressor for the whole mix. MID + SIDE: the middle (kick, bass, snare, vocal) and the sides (wide "
        "guitars, synths, reverb) each get their own compressor: the green MID row and the blue SIDE row.";
    inline constexpr const char* reactsHint =
        "PEAKS: reacts to every hit - tighter, more controlling. AVERAGE: reacts to the overall loudness - smoother, the "
        "usual choice for glue on a master.";
    inline constexpr const char* characterHint =
        "MODERN: clean and precise. VINTAGE: a softer, rounder, slightly thicker compression, like an old hardware unit.";
    inline constexpr const char* limitReleaseHint =
        "AUTO: lets go quickly after a lone peak and more slowly while holding back a sustained passage - no pumping, no "
        "distortion. SET: the RELEASE time you choose.";
    inline constexpr const char* matchHint =
        "Plays the whole chain exactly as loud as the original (it follows the loudness over a few seconds). Click BYPASS "
        "on and off with MATCH on: you're judging the sound, not the volume. Switch it off when you're done.";
    inline constexpr const char* listenHint =
        "For checking, not for exporting: STEREO as normal; MID = only the middle; SIDE = only the difference between "
        "left and right (the width - reverb, wide guitars); MONO = how it sounds on a phone or club speaker.";
    inline constexpr const char* abHint =
        "Two sets of settings to compare: A and B. COPY copies the one you're on to the other. The one you're not on is "
        "kept with your project.";
    inline constexpr const char* metersHint =
        "The master as it leaves the limiter. LUFS: loudness - MOMENTARY (now), SHORT-TERM (3 s), INTEGRATED (the song "
        "so far). Readings, never targets. Click to reset the integrated reading and the highest peak.";
    inline constexpr const char* displayHint =
        "The last six seconds: grey = into the limiter, amber = out, the dashed line = the CEILING; from the top, red = "
        "the limiter turning down, green = the compressor (MID), blue = the compressor (SIDE).";
    inline constexpr const char* initAllHint =
        "Puts every setting back to its default (the 'Default settings' preset). BYPASS, MATCH and LISTEN stay as they are.";
    inline constexpr const char* chainHint =
        "The order the sound goes through. Click a stage to show its controls and its own display below; the lamp shows "
        "whether it's on. INPUT, LIMITER and the meters are always on screen.";
    inline constexpr const char* mbOnHint = "Switches the multiband on or off (the rest of the chain keeps working). Off by default.";
    inline constexpr const char* mbModeHint =
        "STEREO: one set of settings for the whole mix (the top row). MID + SIDE: the middle (green) and the sides (blue) "
        "each split into four bands with their own settings - e.g. the sides' low band held right down: tight, mono bass.";
    inline constexpr const char* mbThresholdHint =
        "Where this band starts to be turned down. Set it so the band's red bar moves 1-3 dB on the loud moments - a bass "
        "note that booms, a cymbal that bites. At 0 dB nothing happens.";
    inline constexpr const char* mbRatioHint = "How firmly this band is held once it passes its THRESHOLD. 1.5-3:1 for mastering.";
    inline constexpr const char* mbSpeedHint =
        "How quickly this band reacts and lets go (attack 3-60 ms with release 40-600 ms). Fast for the highs (cymbals), "
        "slower for the lows (bass notes): too fast on the lows can distort.";
    inline constexpr const char* mbGainHint = "Turns this band up or down afterwards (-12 to +12 dB) - like a broad EQ on the band.";
    inline constexpr const char* mbBandOnHint = "Switches this band's compression and GAIN off: it passes untouched.";
    inline constexpr const char* mbSoloHint = "Hear only this band (or the bands that are soloed) - for setting it up. Switch it off afterwards.";
    inline constexpr const char* mbSplitHint =
        "Where one band ends and the next begins. Drag the lines on the display, or fine-tune here. 120 Hz / 1 kHz / 6 kHz "
        "to start: the lows, the body, the presence, the air.";
    inline constexpr const char* eqOnHint = "Switches the EQ on or off (the rest of the chain keeps working).";
    inline constexpr const char* eqModeHint =
        "STEREO: one EQ for the whole mix (the top row). MID + SIDE: a complete EQ for the middle (green: kick, bass, snare, "
        "vocal) and another for the sides (blue: the width) - e.g. a low cut on the sides only for a tight, mono low end.";
    inline constexpr const char* eqFreqHint =
        "Where this band works. LOW SHELF lifts or lowers everything below it, HIGH SHELF everything above; the three "
        "bells work around their frequency.";
    inline constexpr const char* eqGainHint =
        "How much this band lifts (right) or lowers (left). On a master, small moves: 0.5-2 dB is a lot. The curves are "
        "broad and gentle, like a passive EQ: they keep the same width however far you go.";
    inline constexpr const char* eqLowCutHint =
        "Removes the rumble below this frequency (12 dB per octave). On the SIDES (MID + SIDE), 100-150 Hz makes the "
        "low end mono and tight - the mastering trick for heavy bass. Off on the middle unless there's real rumble.";
    inline constexpr const char* eqDisplayHint =
        "Drag a numbered dot to move its band: across for the frequency, up or down for the gain (then fine-tune with the "
        "knobs). Double-click a dot: back to 0 dB. C = the LOW CUT (drag it across). Green = middle, blue = sides.";
    inline constexpr const char* tamerOnHint = "Switches the resonance tamer on or off (the rest of the chain keeps working).";
    inline constexpr const char* tamerModeHint =
        "STEREO: one set of cuts for the whole mix, the same on left and right - the stereo picture can't move. MID + SIDE: "
        "the middle and the sides each have their own settings (green and blue rows) and their own cuts.";
    inline constexpr const char* tamerListenHint =
        "Plays only what the tamer is taking out. It should sound thin, ringing and harsh - the bits you wanted gone - "
        "never like whole instruments. Switch it off when you're done.";
    inline constexpr const char* tamerDisplayHint =
        "The mix right now, a sixth of an octave at a time (amber = middle or both, blue = sides in MID + SIDE), and from "
        "the top, how much each spot is being turned down. The shaded ends are outside FROM / TO.";

    inline constexpr const char* punchOnHint = "Switches PUNCH on or off (the rest of the chain keeps working). Off by default.";
    inline constexpr const char* punchModeHint =
        "STEREO: one setting for the whole mix (the top row). MID + SIDE: the middle (green: kick, snare, vocal) and the "
        "sides (blue: the room, wide guitars, cymbals) each get their own PUNCH and SUSTAIN - e.g. more snap in the middle only.";
    inline constexpr const char* punchHint =
        "The start of each hit: right = louder (more snap and crack - the drums come forward), left = softer (rounder, "
        "less clicky). After compression and before the limiter, +10 to +30% brings back the punch the levelling took away.";
    inline constexpr const char* sustainHint =
        "The tail of each hit: right = longer (more ring, more room), left = shorter (tighter, drier). Small amounts on a "
        "master: -20 to +20%.";
    inline constexpr const char* punchMixHint = "How much of the shaped sound you hear, blended with the sound before PUNCH. 50% is a gentle, natural lift.";
    inline constexpr const char* widthOnHint = "Switches WIDTH on or off (the rest of the chain keeps working). Off by default.";
    inline constexpr const char* widthHint =
        "How loud the sides are: 100% = as it is, more = wider, less = narrower, 0% = mono. The middle (kick, bass, snare, "
        "vocal) is never touched. 110-130% is plenty; check LISTEN: MONO now and then - nothing should disappear.";
    inline constexpr const char* monoBassHint =
        "Everything below this frequency goes to the middle (the sides' lows taken out, 24 dB per octave): the bass and kick "
        "dead centre and tight on any system. 80-150 Hz for heavy low end; Off for none.";

}
