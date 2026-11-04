#include "PluginEditor.h"
#include "Parameters.h"

namespace
{
    struct KnobInfo { const char* parameterID; const char* name; const char* hint; };

    constexpr const char* tamerAmountHint =
        "The most it will turn any spot down. 2-4 dB is usually plenty on a master; 6 dB and more for harsh or boxy mixes "
        "(and AI-made songs). It only acts where something sticks out - more AMOUNT doesn't mean more of the time.";
    constexpr const char* tamerSensHint =
        "How little a spot has to stick out above the sound around it before it's turned down. Higher catches more, "
        "smaller bumps; too high starts to dull the mix. Check with LISTEN.";
    constexpr const char* tamerSharpHint =
        "SHARP (high): only narrow, ringing spots - a whistling resonance, a ringing snare, a honk. SOFT (low): broader "
        "areas too - boxiness, harshness spread over a range.";

    constexpr const char* thresholdHint =
        "Where the compressor starts to work. For glue on a master, set it so the meter shows 1-3 dB of turning down on the "
        "loud parts - more than about 4 dB and you'll hear it squeeze.";
    constexpr const char* ratioHint =
        "How firmly it turns down above the THRESHOLD. 1.5:1 - 2:1 is glue; 3:1 and more is obvious compression. On a "
        "master, gentle almost always sounds better.";
    constexpr const char* attackHint =
        "How quickly it reacts. 20-50 ms lets the punch of the kick and snare through before it acts; fast (under 10 ms) "
        "flattens the hits.";
    constexpr const char* releaseHint =
        "How quickly it lets go. Set it to move with the song: 100-200 ms for busy rock, 300 ms+ for slow, smooth music. "
        "Too fast pumps, too slow sits on everything.";
    constexpr const char* makeupHint =
        "Turns the compressed sound back up. On a master you mostly leave this low and let the LIMITER's GAIN set the "
        "loudness.";

    const std::array<KnobInfo, EasyMasterAudioProcessorEditor::numKnobs> knobInfo
    {{
        { MasterIDs::trim, "TRIM",
          "Turns the mix up or down before anything else, so the compressor sees a sensible level. If the warning shows "
          "the mix clipping on the way in, turn the mix down in your DAW instead (the clip is already there)." },
        { MasterIDs::midThreshold, "THRESHOLD", thresholdHint },
        { MasterIDs::midRatio,     "RATIO",     ratioHint },
        { MasterIDs::midAttack,    "ATTACK",    attackHint },
        { MasterIDs::midRelease,   "RELEASE",   releaseHint },
        { MasterIDs::midMakeup,    "MAKE-UP",   makeupHint },
        { MasterIDs::sideThreshold, "THRESHOLD", thresholdHint },
        { MasterIDs::sideRatio,     "RATIO",     ratioHint },
        { MasterIDs::sideAttack,    "ATTACK",    attackHint },
        { MasterIDs::sideRelease,   "RELEASE",   releaseHint },
        { MasterIDs::sideMakeup,    "MAKE-UP",   makeupHint },
        { MasterIDs::compMix, "MIX",
          "How much of the compressed sound you hear, blended with the uncompressed. 70-85% keeps the hits' snap with most "
          "of the glue (parallel compression)." },
        { MasterIDs::ignoreBelow, "IGNORE BELOW",
          "The compressor doesn't react to the deep bass under this frequency (it still compresses it). Stops a heavy kick "
          "and bass making the whole mix pump. 60-100 Hz for heavy low end; Off for none." },
        { MasterIDs::limitGain, "GAIN",
          "Turns the master up into the ceiling: each dB more is a dB louder, with the peaks held at the ceiling. Use MATCH "
          "to hear whether it's still clean - when the kick and snare soften, back off." },
        { MasterIDs::ceiling, "CEILING",
          "The most the output may ever reach, as true peak (the peaks between the samples too). -0.3 dBTP by default; "
          "-1 dBTP for files that will be converted to MP3 / AAC." },
        { MasterIDs::release, "RELEASE",
          "With RELEASE on SET: how quickly the limiter lets go. AUTO is almost always best (quick after a lone peak, "
          "slower through a sustained passage - no pumping, no distortion)." },
        { MasterIDs::tMidAmount,  "AMOUNT",      tamerAmountHint },
        { MasterIDs::tMidSens,    "SENSITIVITY", tamerSensHint },
        { MasterIDs::tMidSharp,   "SHARPNESS",   tamerSharpHint },
        { MasterIDs::tSideAmount, "AMOUNT",      tamerAmountHint },
        { MasterIDs::tSideSens,   "SENSITIVITY", tamerSensHint },
        { MasterIDs::tSideSharp,  "SHARPNESS",   tamerSharpHint },
        { MasterIDs::tSpeed, "SPEED",
          "How quickly it reacts to a spot that rings out, and lets go after. Fast (low) catches a single note that rings; "
          "slow (high) is smoother and acts more like a gentle, moving EQ." },
        { MasterIDs::tFrom, "FROM",
          "The lowest frequency it works on (soft edge). 200 Hz by default: low notes are music, not resonances. Lower it "
          "for a boomy room or a bass note that booms." },
        { MasterIDs::tTo, "TO",
          "The highest frequency it works on (soft edge). Set FROM 4 kHz and TO 16 kHz to work only on harshness and fizz." },
    }};

    constexpr const char* gettingStartedHint =
        "Pick a preset for the song, set TRIM so the compressor shows 1-3 dB, push the limiter's GAIN to taste, then check "
        "with MATCH and BYPASS. Hover over anything for help.";
    constexpr const char* compOnHint = "Switches the compressor on or off (the rest of the chain keeps working).";
    constexpr const char* modeHint =
        "STEREO: one compressor for the whole mix. MID + SIDE: the middle (kick, bass, snare, vocal) and the sides (wide "
        "guitars, synths, reverb) each get their own compressor: the green MID row and the blue SIDE row.";
    constexpr const char* reactsHint =
        "PEAKS: reacts to every hit - tighter, more controlling. AVERAGE: reacts to the overall loudness - smoother, the "
        "usual choice for glue on a master.";
    constexpr const char* characterHint =
        "MODERN: clean and precise. VINTAGE: a softer, rounder, slightly thicker compression, like an old hardware unit.";
    constexpr const char* limitReleaseHint =
        "AUTO: lets go quickly after a lone peak and more slowly while holding back a sustained passage - no pumping, no "
        "distortion. SET: the RELEASE time you choose.";
    constexpr const char* matchHint =
        "Plays the whole chain exactly as loud as the original (it follows the loudness over a few seconds). Click BYPASS "
        "on and off with MATCH on: you're judging the sound, not the volume. Switch it off when you're done.";
    constexpr const char* listenHint =
        "For checking, not for exporting: STEREO as normal; MID = only the middle; SIDE = only the difference between "
        "left and right (the width - reverb, wide guitars); MONO = how it sounds on a phone or club speaker.";
    constexpr const char* abHint =
        "Two sets of settings to compare: A and B. COPY copies the one you're on to the other. The one you're not on is "
        "kept with your project.";
    constexpr const char* metersHint =
        "The master as it leaves the limiter. LUFS: loudness - MOMENTARY (now), SHORT-TERM (3 s), INTEGRATED (the song "
        "so far). Readings, never targets. Click to reset the integrated reading and the highest peak.";
    constexpr const char* displayHint =
        "The last six seconds: grey = into the limiter, amber = out, the dashed line = the CEILING; from the top, red = "
        "the limiter turning down, green = the compressor (MID), blue = the compressor (SIDE).";
    constexpr const char* initAllHint =
        "Puts every setting back to its default (the 'Default settings' preset). BYPASS, MATCH and LISTEN stay as they are.";
    constexpr const char* chainHint =
        "The order the sound goes through. Click a stage to show its controls and its own display below. Dim = coming in "
        "later stages, each in its place. INPUT, LIMITER and the meters are always on screen.";
    constexpr const char* mbOnHint = "Switches the multiband on or off (the rest of the chain keeps working). Off by default.";
    constexpr const char* mbModeHint =
        "STEREO: one set of settings for the whole mix (the top row). MID + SIDE: the middle (green) and the sides (blue) "
        "each split into four bands with their own settings - e.g. the sides' low band held right down: tight, mono bass.";
    constexpr const char* mbThresholdHint =
        "Where this band starts to be turned down. Set it so the band's red bar moves 1-3 dB on the loud moments - a bass "
        "note that booms, a cymbal that bites. At 0 dB nothing happens.";
    constexpr const char* mbRatioHint = "How firmly this band is held once it passes its THRESHOLD. 1.5-3:1 for mastering.";
    constexpr const char* mbSpeedHint =
        "How quickly this band reacts and lets go (attack 3-60 ms with release 40-600 ms). Fast for the highs (cymbals), "
        "slower for the lows (bass notes): too fast on the lows can distort.";
    constexpr const char* mbGainHint = "Turns this band up or down afterwards (-12 to +12 dB) - like a broad EQ on the band.";
    constexpr const char* mbBandOnHint = "Switches this band's compression and GAIN off: it passes untouched.";
    constexpr const char* mbSoloHint = "Hear only this band (or the bands that are soloed) - for setting it up. Switch it off afterwards.";
    constexpr const char* mbSplitHint =
        "Where one band ends and the next begins. Drag the lines on the display, or fine-tune here. 120 Hz / 1 kHz / 6 kHz "
        "to start: the lows, the body, the presence, the air.";
    constexpr const char* eqOnHint = "Switches the EQ on or off (the rest of the chain keeps working).";
    constexpr const char* eqModeHint =
        "STEREO: one EQ for the whole mix (the top row). MID + SIDE: a complete EQ for the middle (green: kick, bass, snare, "
        "vocal) and another for the sides (blue: the width) - e.g. a low cut on the sides only for a tight, mono low end.";
    constexpr const char* eqFreqHint =
        "Where this band works. LOW SHELF lifts or lowers everything below it, HIGH SHELF everything above; the three "
        "bells work around their frequency.";
    constexpr const char* eqGainHint =
        "How much this band lifts (right) or lowers (left). On a master, small moves: 0.5-2 dB is a lot. The curves are "
        "broad and gentle, like a passive EQ: they keep the same width however far you go.";
    constexpr const char* eqLowCutHint =
        "Removes the rumble below this frequency (12 dB per octave). On the SIDES (MID + SIDE), 100-150 Hz makes the "
        "low end mono and tight - the mastering trick for heavy bass. Off on the middle unless there's real rumble.";
    constexpr const char* eqDisplayHint =
        "Drag a numbered dot to move its band: across for the frequency, up or down for the gain (then fine-tune with the "
        "knobs). Double-click a dot: back to 0 dB. C = the LOW CUT (drag it across). Green = middle, blue = sides.";
    constexpr const char* tamerOnHint = "Switches the resonance tamer on or off (the rest of the chain keeps working).";
    constexpr const char* tamerModeHint =
        "STEREO: one set of cuts for the whole mix, the same on left and right - the stereo picture can't move. MID + SIDE: "
        "the middle and the sides each have their own settings (green and blue rows) and their own cuts.";
    constexpr const char* tamerListenHint =
        "Plays only what the tamer is taking out. It should sound thin, ringing and harsh - the bits you wanted gone - "
        "never like whole instruments. Switch it off when you're done.";
    constexpr const char* tamerDisplayHint =
        "The mix right now, a sixth of an octave at a time (amber = middle or both, blue = sides in MID + SIDE), and from "
        "the top, how much each spot is being turned down. The shaded ends are outside FROM / TO.";

    constexpr float topDb = 2.0f, bottomDb = -30.0f, cutScaleDb = 12.0f;
    const juce::Colour midColour = Palette::meterLow;
    const juce::Colour sideColour = Palette::meterLow.interpolatedWith (juce::Colours::deepskyblue, 0.7f);

    juce::String lufsText (float lufs) { return lufs <= -69.9f ? juce::String ("--") : juce::String (lufs, 1); }
}

//==============================================================================
MasterDisplay::MasterDisplay() : history ((size_t) numMoments) {}

void MasterDisplay::addMoments (const EasyMasterAudioProcessor::Moment* m, int count)
{
    if (count <= 0) return;
    for (int i = 0; i < count; ++i)
    {
        history[(size_t) next] = m[i];
        next = (next + 1) % numMoments;
    }
    repaint();
}

void MasterDisplay::setState (float c, bool ms, bool on, bool b)
{
    if (std::abs (c - ceilingDb) > 0.01f || ms != midSide || on != compOn || b != bypassed)
    {
        ceilingDb = c; midSide = ms; compOn = on; bypassed = b;
        repaint();
    }
}

void MasterDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (Palette::background);
    g.fillRoundedRectangle (area, 6.0f);

    auto plot = area.reduced (14.0f, 12.0f).withTrimmedLeft (34.0f).withTrimmedBottom (40.0f);
    // (two lanes: how much is being turned down on top, the levels underneath)
    const auto cutLane = plot.removeFromTop (plot.getHeight() * 0.3f);
    plot.removeFromTop (8.0f);
    const auto yOf = [&] (float db) { return plot.getY() + plot.getHeight() * (topDb - juce::jlimit (bottomDb, topDb, db)) / (topDb - bottomDb); };
    const auto xOf = [&] (int age) { return plot.getRight() - (float) age / (float) (numMoments - 1) * plot.getWidth(); };
    const auto cutY = [&] (float db) { return cutLane.getY() + cutLane.getHeight() * juce::jlimit (0.0f, 1.0f, db / cutScaleDb); };

    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    for (float db : { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f })
    {
        g.setColour (Palette::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (yOf (db)), plot.getX(), plot.getRight());
        g.setColour (Palette::textDim);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 36.0f, yOf (db) - 7.0f, 32.0f, 14.0f), juce::Justification::centredRight, false);
    }
    g.setColour (Palette::panel.withAlpha (0.6f));
    g.fillRect (cutLane);
    for (float db : { 0.0f, 6.0f, 12.0f })
    {
        g.setColour (Palette::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (cutY (db)), cutLane.getX(), cutLane.getRight());
        g.setColour (Palette::textDim);
        g.drawText (db < 0.5f ? juce::String ("0") : "-" + juce::String ((int) db), juce::Rectangle<float> (cutLane.getX() - 36.0f, cutY (db) - 7.0f, 32.0f, 14.0f),
                    juce::Justification::centredRight, false);
    }
    const auto laneName = [&g] (juce::Rectangle<float> lane, const juce::String& name)
    {
        const auto tag = juce::Rectangle<float> (lane.getX() + 4.0f, lane.getBottom() - 18.0f, (float) name.length() * 7.2f + 12.0f, 15.0f);
        g.setColour (Palette::background.withAlpha (0.85f));
        g.fillRoundedRectangle (tag, 3.0f);
        g.setColour (Palette::textDim);
        g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
        g.drawText (name, tag, juce::Justification::centred, false);
    };

    g.drawText ("6 seconds ago", juce::Rectangle<float> (plot.getX(), plot.getBottom() + 3.0f, 120.0f, 14.0f), juce::Justification::centredLeft, false);
    g.drawText ("now", juce::Rectangle<float> (plot.getRight() - 60.0f, plot.getBottom() + 3.0f, 60.0f, 14.0f), juce::Justification::centredRight, false);

    juce::Path in, out, outLine, cut, midLine, sideLine;
    in.startNewSubPath (plot.getBottomRight());
    out.startNewSubPath (plot.getBottomRight());
    cut.startNewSubPath (cutLane.getTopRight());
    for (int age = 0; age < numMoments; ++age)
    {
        const auto& m = history[(size_t) ((next - 1 - age + numMoments * 2) % numMoments)];
        const float x = xOf (age);
        in.lineTo (x, yOf (m.inDb));
        out.lineTo (x, yOf (m.outDb));
        cut.lineTo (x, cutY (m.limitCutDb));
        if (age == 0)
        {
            outLine.startNewSubPath (x, yOf (m.outDb));
            midLine.startNewSubPath (x, cutY (m.midCutDb));
            sideLine.startNewSubPath (x, cutY (m.sideCutDb));
        }
        else
        {
            outLine.lineTo (x, yOf (m.outDb));
            midLine.lineTo (x, cutY (m.midCutDb));
            sideLine.lineTo (x, cutY (m.sideCutDb));
        }
    }
    in.lineTo (plot.getBottomLeft()); in.closeSubPath();
    out.lineTo (plot.getBottomLeft()); out.closeSubPath();
    cut.lineTo (cutLane.getTopLeft()); cut.closeSubPath();

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (plot.toNearestInt());
        g.setColour (Palette::textDim.withAlpha (0.3f));
        g.fillPath (in);
        g.setColour (Palette::accent.withAlpha (0.16f));
        g.fillPath (out);
        g.setColour (Palette::accent);
        g.strokePath (outLine, juce::PathStrokeType (1.4f));
    }
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (cutLane.toNearestInt());
        g.setColour (Palette::meterHigh.withAlpha (0.5f));
        g.fillPath (cut);
        if (compOn)
        {
            if (midSide)
            {
                g.setColour (sideColour);
                g.strokePath (sideLine, juce::PathStrokeType (1.8f));
            }
            g.setColour (midColour);
            g.strokePath (midLine, juce::PathStrokeType (1.8f));
        }
    }

    // The ceiling
    {
        const float y = yOf (ceilingDb);
        juce::Path line, dashed;
        line.startNewSubPath (plot.getX(), y);
        line.lineTo (plot.getRight(), y);
        const float dashes[] { 6.0f, 4.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, line, dashes, 2);
        g.setColour (Palette::textBright);
        g.fillPath (dashed);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText ("CEILING " + juce::String (ceilingDb, 1) + " dBTP", juce::Rectangle<float> (plot.getRight() - 200.0f, y + 3.0f, 196.0f, 14.0f),
                    juce::Justification::centredRight, false);
    }

    laneName (cutLane, "TURNING DOWN");
    laneName (plot, "LEVEL");

    // Key
    auto key = juce::Rectangle<float> (plot.getX(), plot.getBottom() + 20.0f, plot.getWidth(), 16.0f);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    const auto item = [&] (juce::Colour c, const juce::String& t, float w)
    {
        g.setColour (c);
        g.fillRect (key.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, 10.0f));
        key.removeFromLeft (6.0f);
        g.setColour (Palette::textDim);
        g.drawText (t, key.removeFromLeft (w), juce::Justification::centredLeft, false);
        key.removeFromLeft (12.0f);
    };
    item (Palette::textDim.withAlpha (0.6f), "into the limiter", 100.0f);
    item (Palette::accent, "out", 30.0f);
    item (Palette::meterHigh.withAlpha (0.7f), "limiter turning down", 130.0f);
    item (midColour, midSide ? "compressor: MID" : "compressor", midSide ? 116.0f : 76.0f);
    if (midSide)
        item (sideColour, "compressor: SIDE", 116.0f);

    if (bypassed)
    {
        g.setColour (Palette::background.withAlpha (0.65f));
        g.fillRect (area);
        const auto badge = juce::Rectangle<float> (230.0f, 58.0f).withCentre (area.getCentre());
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (badge, 8.0f);
        g.setColour (Palette::meterHigh);
        g.drawRoundedRectangle (badge.reduced (0.75f), 8.0f, 1.5f);
        auto lines = badge.reduced (10.0f, 9.0f);
        g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
        g.drawText ("BYPASSED", lines.removeFromTop (lines.getHeight() * 0.55f), juce::Justification::centred, false);
        g.setColour (Palette::textDim);
        g.setFont (juce::Font (juce::FontOptions (12.5f)));
        g.drawText ("You're hearing the original mix", lines, juce::Justification::centred, false);
    }
}

//==============================================================================
void MasterMeters::setReadings (const EasyMasterAudioProcessor::Readings& readings, bool ms, bool on, bool tamerIsOn)
{
    r = readings;
    midSide = ms; compOn = on; tamerOn = tamerIsOn;
    const auto follow = [] (float& shown, float now) { shown = now > shown ? now : shown + 0.25f * (now - shown); };
    follow (shownMid, on ? r.midCut : 0.0f);
    follow (shownSide, on ? r.sideCut : 0.0f);
    follow (shownLimit, r.limitCut);
    follow (shownTamer, tamerIsOn ? juce::jmax (r.tamerMidCut, r.tamerSideCut) : 0.0f);
    repaint();
}

void MasterMeters::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    // LUFS: three big numbers
    auto lufsRow = area.removeFromTop (52.0f);
    const float third = lufsRow.getWidth() / 3.0f;
    const auto bigNumber = [&] (juce::Rectangle<float> box, const juce::String& caption, const juce::String& value, juce::Colour colour)
    {
        g.setColour (Palette::background);
        g.fillRoundedRectangle (box.reduced (3.0f), 5.0f);
        g.setColour (Palette::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (caption, box.removeFromTop (18.0f).translated (0.0f, 3.0f), juce::Justification::centred, false);
        g.setColour (colour);
        g.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
        g.drawText (value, box.translated (0.0f, -2.0f), juce::Justification::centred, false);
    };
    bigNumber (lufsRow.removeFromLeft (third), "MOMENTARY", lufsText (r.momentary), Palette::textBright);
    bigNumber (lufsRow.removeFromLeft (third), "SHORT-TERM", lufsText (r.shortTerm), Palette::textBright);
    bigNumber (lufsRow, "INTEGRATED", lufsText (r.integrated), Palette::accent);
    area.removeFromTop (4.0f);

    // True peak and PLR
    auto peakRow = area.removeFromTop (48.0f);
    const float half = peakRow.getWidth() / 2.0f;
    const bool over = r.highestPeak > -0.05f;
    bigNumber (peakRow.removeFromLeft (half), "HIGHEST TRUE PEAK",
               r.highestPeak <= -99.0f ? juce::String ("--") : juce::String (r.highestPeak, 2) + " dB", over ? Palette::meterHigh : Palette::textBright);
    const float plr = r.highestPeak - r.integrated;
    bigNumber (peakRow, "PLR (PEAK TO LOUDNESS)", (r.integrated <= -69.9f || r.highestPeak <= -99.0f) ? juce::String ("--") : juce::String (plr, 1) + " dB",
               Palette::textBright);
    area.removeFromTop (4.0f);

    // Correlation
    {
        auto row = area.removeFromTop (30.0f);
        g.setColour (Palette::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText ("STEREO CORRELATION", row.removeFromTop (14.0f), juce::Justification::centredLeft, false);
        auto bar = row.reduced (0.0f, 3.0f);
        g.setColour (Palette::background);
        g.fillRoundedRectangle (bar, 3.0f);
        const float cx = bar.getCentreX();
        const float x = bar.getX() + bar.getWidth() * (0.5f + 0.5f * juce::jlimit (-1.0f, 1.0f, r.correlation));
        const auto colour = r.correlation < 0.0f ? Palette::meterHigh : (r.correlation < 0.3f ? Palette::meterMid : Palette::meterLow);
        g.setColour (colour.withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (cx, x), bar.getY() + 2.0f, juce::jmax (cx, x), bar.getBottom() - 2.0f));
        g.setColour (Palette::textDim);
        g.drawVerticalLine (juce::roundToInt (cx), bar.getY(), bar.getBottom());
        g.setFont (juce::Font (juce::FontOptions (10.5f)));
        g.drawText ("-1", bar.reduced (4.0f, 0.0f), juce::Justification::centredLeft, false);
        g.drawText ("+1", bar.reduced (4.0f, 0.0f), juce::Justification::centredRight, false);
    }
    area.removeFromTop (4.0f);

    // Turning down now
    g.setColour (Palette::textDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawText ("TURNING DOWN NOW", area.removeFromTop (14.0f), juce::Justification::centredLeft, false);
    const auto cutBar = [&] (const juce::String& name, float db, juce::Colour colour, bool active)
    {
        auto row = area.removeFromTop (18.0f).reduced (0.0f, 2.0f);
        g.setColour (active ? Palette::textBright : Palette::textDim.withAlpha (0.5f));
        g.setFont (juce::Font (juce::FontOptions (11.5f)));
        g.drawText (name, row.removeFromLeft (110.0f), juce::Justification::centredLeft, false);
        auto value = row.removeFromRight (56.0f);
        g.setColour (Palette::background);
        g.fillRoundedRectangle (row, 3.0f);
        if (active && db > 0.05f)
        {
            g.setColour (colour);
            g.fillRoundedRectangle (row.withWidth (row.getWidth() * juce::jlimit (0.0f, 1.0f, db / 12.0f)), 3.0f);
        }
        g.setColour (active ? Palette::textBright : Palette::textDim.withAlpha (0.5f));
        g.drawText (active ? juce::String (db, 1) + " dB" : juce::String ("off"), value, juce::Justification::centredRight, false);
    };
    cutBar ("TAMER (deepest)", shownTamer, Palette::accent, tamerOn);
    cutBar (midSide ? "COMPRESSOR MID" : "COMPRESSOR", shownMid, midColour, compOn);
    cutBar ("COMPRESSOR SIDE", shownSide, sideColour, compOn && midSide);
    cutBar ("LIMITER", shownLimit, Palette::meterHigh, true);

    g.setColour (Palette::textDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("click to reset INTEGRATED and the peak", area.removeFromBottom (14.0f), juce::Justification::centredRight, false);
}

//==============================================================================
void TamerDisplay::setPicture (const EasyMasterAudioProcessor::TamerPicture& p, bool ms, bool isOn, float from, float to, bool isBypassed)
{
    picture = p;
    midSide = ms; on = isOn; fromHz = from; toHz = to; bypassed = isBypassed;
    for (int part = 0; part < 2; ++part)
        for (int b = 0; b < ResonanceTamer::numBands; ++b)
        {
            auto& shown = shownLevel[(size_t) part][(size_t) b];
            const float now = p.levelDb[(size_t) part][(size_t) b];
            shown = now > shown ? now : shown + 0.25f * (now - shown);
        }
    repaint();
}

void TamerDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (Palette::background);
    g.fillRoundedRectangle (area, 6.0f);
    auto plot = area.reduced (14.0f, 12.0f).withTrimmedLeft (34.0f).withTrimmedBottom (40.0f);
    const auto cutLane = plot.removeFromTop (plot.getHeight() * 0.32f);
    plot.removeFromTop (8.0f);
    const int bands = ResonanceTamer::numBands;
    const float slot = plot.getWidth() / (float) bands;
    constexpr float levelTop = 0.0f, levelRange = 72.0f, cutRange = 12.0f;
    const auto levelY = [&] (float db) { return plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (db - (levelTop - levelRange)) / levelRange); };
    const auto cutH = [&] (float db) { return cutLane.getHeight() * juce::jlimit (0.0f, 1.0f, db / cutRange); };

    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    for (float db : { -24.0f, -48.0f })
    {
        g.setColour (Palette::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (levelY (db)), plot.getX(), plot.getRight());
        g.setColour (Palette::textDim);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 36.0f, levelY (db) - 7.0f, 32.0f, 14.0f), juce::Justification::centredRight, false);
    }
    g.setColour (Palette::panel.withAlpha (0.6f));
    g.fillRect (cutLane);
    for (float db : { 0.0f, 6.0f, 12.0f })
    {
        const float y = cutLane.getY() + cutH (db);
        g.setColour (Palette::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (y), cutLane.getX(), cutLane.getRight());
        g.setColour (Palette::textDim);
        g.drawText (db < 0.5f ? juce::String ("0") : "-" + juce::String ((int) db), juce::Rectangle<float> (cutLane.getX() - 36.0f, y - 7.0f, 32.0f, 14.0f),
                    juce::Justification::centredRight, false);
    }

    for (int b = 0; b < bands; ++b)
    {
        const float x = plot.getX() + slot * (float) b;
        const float hz = picture.centreHz[(size_t) b];
        const bool inRange = hz >= fromHz * 0.89f && hz <= toHz * 1.12f;
        const float w = juce::jmax (2.0f, slot - 2.0f);
        // Levels: the middle (or both) in amber, the sides in blue beside it (MID + SIDE)
        const float ym = levelY (shownLevel[0][(size_t) b]);
        if (midSide)
        {
            const float ys = levelY (shownLevel[1][(size_t) b]);
            g.setColour (Palette::accent.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (x + 1.0f, ym, w * 0.5f, plot.getBottom() - ym));
            g.setColour (sideColour.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (x + 1.0f + w * 0.5f, ys, w * 0.5f, plot.getBottom() - ys));
        }
        else
        {
            g.setColour (Palette::accent.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (x + 1.0f, ym, w, plot.getBottom() - ym));
        }
        // Cuts, hanging from the top of their lane
        if (on && ! bypassed)
        {
            const float cm = cutH (picture.cutDb[0][(size_t) b]);
            if (midSide)
            {
                const float cs = cutH (picture.cutDb[1][(size_t) b]);
                g.setColour (Palette::meterHigh.withAlpha (0.8f));
                g.fillRect (juce::Rectangle<float> (x + 1.0f, cutLane.getY(), w * 0.5f, cm));
                g.setColour (sideColour.interpolatedWith (Palette::meterHigh, 0.35f));
                g.fillRect (juce::Rectangle<float> (x + 1.0f + w * 0.5f, cutLane.getY(), w * 0.5f, cs));
            }
            else
            {
                g.setColour (Palette::meterHigh.withAlpha (0.8f));
                g.fillRect (juce::Rectangle<float> (x + 1.0f, cutLane.getY(), w, cm));
            }
        }
        if (! inRange)   // (outside FROM / TO: shaded over)
        {
            g.setColour (Palette::background.withAlpha (0.7f));
            g.fillRect (juce::Rectangle<float> (x, cutLane.getY(), slot, plot.getBottom() - cutLane.getY()));
        }
    }

    // Frequencies along the bottom
    g.setColour (Palette::textDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    for (float hz : { 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float b = 6.0f * std::log2 (hz / 50.0f);
        const float x = plot.getX() + slot * (b + 0.5f);
        g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 3.0f, 40.0f, 14.0f), juce::Justification::centred, false);
    }
    const auto laneName = [&g] (juce::Rectangle<float> lane, const juce::String& name)
    {
        const auto tag = juce::Rectangle<float> (lane.getX() + 4.0f, lane.getBottom() - 18.0f, (float) name.length() * 7.2f + 12.0f, 15.0f);
        g.setColour (Palette::background.withAlpha (0.85f));
        g.fillRoundedRectangle (tag, 3.0f);
        g.setColour (Palette::textDim);
        g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
        g.drawText (name, tag, juce::Justification::centred, false);
    };
    laneName (cutLane, "TURNING DOWN");
    laneName (plot, "THE MIX NOW");

    auto key = juce::Rectangle<float> (plot.getX(), plot.getBottom() + 20.0f, plot.getWidth(), 16.0f);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    const auto item = [&] (juce::Colour c, const juce::String& t, float w)
    {
        g.setColour (c);
        g.fillRect (key.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, 10.0f));
        key.removeFromLeft (6.0f);
        g.setColour (Palette::textDim);
        g.drawText (t, key.removeFromLeft (w), juce::Justification::centredLeft, false);
        key.removeFromLeft (12.0f);
    };
    item (Palette::accent, midSide ? "middle" : "the mix", midSide ? 50.0f : 56.0f);
    if (midSide) item (sideColour, "sides", 40.0f);
    item (Palette::meterHigh.withAlpha (0.8f), midSide ? "middle turned down" : "turned down", midSide ? 120.0f : 80.0f);
    if (midSide) item (sideColour.interpolatedWith (Palette::meterHigh, 0.35f), "sides turned down", 120.0f);

    if (bypassed || ! on)
    {
        g.setColour (Palette::background.withAlpha (0.6f));
        g.fillRect (cutLane);
        g.setColour (Palette::textDim);
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.drawText (bypassed ? "BYPASSED" : "THE TAMER IS OFF", cutLane, juce::Justification::centred, false);
    }
}

//==============================================================================
void EQDisplay::setState (const MasteringEQ::Settings& s, const EasyMasterAudioProcessor::TamerPicture& p, bool isOn, bool isBypassed, double sampleRate)
{
    settings = s; on = isOn; bypassed = isBypassed; rate = sampleRate;
    centreHz = p.centreHz;
    for (int b = 0; b < ResonanceTamer::numBands; ++b)
    {
        const float now = juce::Decibels::gainToDecibels (juce::Decibels::decibelsToGain (p.levelDb[0][(size_t) b])
                                                          + juce::Decibels::decibelsToGain (p.levelDb[1][(size_t) b]), -120.0f);
        shownLevel[(size_t) b] = now > shownLevel[(size_t) b] ? now : shownLevel[(size_t) b] + 0.2f * (now - shownLevel[(size_t) b]);
    }
    repaint();
}

juce::Rectangle<float> EQDisplay::plotArea() const
{
    return getLocalBounds().toFloat().reduced (14.0f, 12.0f).withTrimmedLeft (34.0f).withTrimmedBottom (40.0f);
}

float EQDisplay::scale() const
{
    // (a ±6 dB scale - mastering moves are small - widening to ±12 when a band goes past ±5.5 dB; held still while dragging)
    if (dragging.valid()) return dragRange;
    float biggest = 0.0f;
    for (auto* part : { &settings.mid, &settings.side })
        for (float gdb : part->gainDb) biggest = juce::jmax (biggest, std::abs (gdb));
    return biggest > 5.5f ? 12.0f : 6.0f;
}

float EQDisplay::xOf (double hz) const   { const auto p = plotArea(); return p.getX() + p.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0)); }
double EQDisplay::hzAt (float x) const   { const auto p = plotArea(); return 20.0 * std::pow (1000.0, (double) ((x - p.getX()) / p.getWidth())); }
float EQDisplay::yOf (float db) const    { const auto p = plotArea(); return p.getCentreY() - p.getHeight() * 0.5f * juce::jlimit (-1.0f, 1.0f, db / scale()); }
float EQDisplay::dbAt (float y) const    { const auto p = plotArea(); return (p.getCentreY() - y) / (p.getHeight() * 0.5f) * scale(); }

juce::Point<float> EQDisplay::nodePosition (Node n) const
{
    const auto& part = n.part == 1 ? settings.side : settings.mid;
    if (n.band == MasteringEQ::numBands)   // (the LOW CUT: on the line, where it's 3 dB down)
        return { xOf (part.lowCutHz), yOf ((float) juce::Decibels::gainToDecibels (MasteringEQ::responseAt (part, part.lowCutHz, rate), -60.0)) };
    return { xOf (part.freqHz[(size_t) n.band]), yOf (part.gainDb[(size_t) n.band]) };
}

EQDisplay::Node EQDisplay::nodeAt (juce::Point<float> at) const
{
    Node best;
    float bestDistance = 14.0f;
    for (int part = settings.midSide ? 1 : 0; part >= 0; --part)   // (the middle's dots are on top: they win a tie)
        for (int b = 0; b <= MasteringEQ::numBands; ++b)
        {
            const float d = at.getDistanceFrom (nodePosition ({ part, b }));
            if (d <= bestDistance) { bestDistance = d; best = { part, b }; }
        }
    return best;
}

void EQDisplay::setParameter (const char* id, float value)
{
    if (auto* p = parameters.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void EQDisplay::mouseMove (const juce::MouseEvent& e)
{
    const auto n = nodeAt (e.position);
    if (! (n == hovered)) { hovered = n; repaint(); }
    setMouseCursor (n.valid() ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void EQDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragging = nodeAt (e.position);
    if (! dragging.valid()) return;
    dragRange = scale();
    const bool lowCut = dragging.band == MasteringEQ::numBands;
    for (const char* id : { lowCut ? MasterEQIDs::lowCut[dragging.part] : MasterEQIDs::freq[dragging.part][dragging.band],
                            lowCut ? nullptr : MasterEQIDs::gain[dragging.part][dragging.band] })
        if (id != nullptr)
            if (auto* p = parameters.getParameter (id)) p->beginChangeGesture();
}

void EQDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging.valid()) return;
    const auto at = e.position;
    const double hz = hzAt (at.x);
    if (dragging.band == MasteringEQ::numBands)
        setParameter (MasterEQIDs::lowCut[dragging.part], (float) juce::jlimit ((double) MasteringEQ::lowCutOffHz, 300.0, hz));   // (across only)
    else
    {
        const auto& info = MasterEQIDs::bands[dragging.band];
        setParameter (MasterEQIDs::freq[dragging.part][dragging.band], (float) juce::jlimit ((double) info.lowHz, (double) info.highHz, hz));
        setParameter (MasterEQIDs::gain[dragging.part][dragging.band], juce::jlimit (-MasteringEQ::maxGainDb, MasteringEQ::maxGainDb, std::round (dbAt (at.y) * 10.0f) / 10.0f));
    }
    if (onEdit) onEdit();
}

void EQDisplay::mouseUp (const juce::MouseEvent&)
{
    if (! dragging.valid()) return;
    const bool lowCut = dragging.band == MasteringEQ::numBands;
    for (const char* id : { lowCut ? MasterEQIDs::lowCut[dragging.part] : MasterEQIDs::freq[dragging.part][dragging.band],
                            lowCut ? nullptr : MasterEQIDs::gain[dragging.part][dragging.band] })
        if (id != nullptr)
            if (auto* p = parameters.getParameter (id)) p->endChangeGesture();
    dragging = {};
    repaint();
}

void EQDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto n = nodeAt (e.position);
    if (! n.valid()) return;
    if (n.band == MasteringEQ::numBands) setParameter (MasterEQIDs::lowCut[n.part], MasteringEQ::lowCutOffHz);   // (LOW CUT: off)
    else                                 setParameter (MasterEQIDs::gain[n.part][n.band], 0.0f);                  // (a band: back to 0 dB)
    if (onEdit) onEdit();
}

void EQDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (Palette::background);
    g.fillRoundedRectangle (area, 6.0f);
    const auto plot = plotArea();
    const float range = scale();

    // The mix coming in (faint)
    if (centreHz[0] > 0.0f)
    {
        juce::Path mix;
        mix.startNewSubPath (xOf (centreHz[0]), plot.getBottom());
        for (int b = 0; b < ResonanceTamer::numBands; ++b)
            mix.lineTo (xOf (centreHz[(size_t) b]), plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (shownLevel[(size_t) b] + 72.0f) / 72.0f));
        mix.lineTo (xOf (centreHz[(size_t) ResonanceTamer::numBands - 1]), plot.getBottom());
        mix.closeSubPath();
        g.setColour (Palette::textDim.withAlpha (0.16f));
        g.fillPath (mix);
    }

    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    for (float step : { 1.0f, 0.5f, 0.0f, -0.5f, -1.0f })
    {
        const float db = step * range;
        g.setColour (std::abs (db) < 0.01f ? Palette::textDim.withAlpha (0.5f) : Palette::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (yOf (db)), plot.getX(), plot.getRight());
        g.setColour (Palette::textDim);
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 36.0f, yOf (db) - 7.0f, 32.0f, 14.0f), juce::Justification::centredRight, false);
    }
    for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        g.setColour (Palette::panelEdge);
        g.drawVerticalLine (juce::roundToInt (xOf (hz)), plot.getY(), plot.getBottom());
        g.setColour (Palette::textDim);
        g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (xOf (hz) - 20.0f, plot.getBottom() + 3.0f, 40.0f, 14.0f), juce::Justification::centred, false);
    }

    const auto curve = [&] (int partIndex, juce::Colour colour, float thickness)
    {
        const auto& part = partIndex == 1 ? settings.side : settings.mid;
        juce::Path path;
        for (int i = 0; i <= 240; ++i)
        {
            const double hz = 20.0 * std::pow (1000.0, i / 240.0);
            const float db = (float) juce::Decibels::gainToDecibels (MasteringEQ::responseAt (part, hz, rate), -60.0);
            if (i == 0) path.startNewSubPath (xOf (hz), yOf (db)); else path.lineTo (xOf (hz), yOf (db));
        }
        g.setColour (colour.withAlpha (on && ! bypassed ? 1.0f : 0.35f));
        g.strokePath (path, juce::PathStrokeType (thickness));
        for (int b = 0; b <= MasteringEQ::numBands; ++b)
        {
            const Node n { partIndex, b };
            const bool lit = n == hovered || n == dragging;
            const auto dot = juce::Rectangle<float> (lit ? 19.0f : 15.0f, lit ? 19.0f : 15.0f).withCentre (nodePosition (n));
            g.setColour (lit ? colour.withAlpha (0.35f) : Palette::background);
            g.fillEllipse (dot);
            g.setColour (colour);
            g.drawEllipse (dot, lit ? 2.0f : 1.5f);
            g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
            g.drawText (b == MasteringEQ::numBands ? juce::String ("C") : juce::String (b + 1), dot, juce::Justification::centred, false);
        }
    };
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (plot.expanded (10.0f).toNearestInt());
        if (settings.midSide)
        {
            curve (1, sideColour, 2.0f);
            curve (0, midColour, 2.0f);
        }
        else
            curve (0, Palette::accent, 2.2f);
    }

    // While dragging (or hovering): what that dot is set to
    const auto shown = dragging.valid() ? dragging : hovered;
    if (shown.valid())
    {
        const auto& part = shown.part == 1 ? settings.side : settings.mid;
        const bool lowCut = shown.band == MasteringEQ::numBands;
        const juce::String text = juce::String (shown.part == 1 ? "SIDE " : (settings.midSide ? "MID " : "")) + (lowCut ? juce::String ("LOW CUT ") : juce::String (MasterEQIDs::bands[shown.band].name) + "  ")
                                  + (lowCut ? (part.lowCutHz <= MasteringEQ::lowCutOffHz + 0.5f ? juce::String ("Off") : Format::hertz (part.lowCutHz))
                                            : Format::hertz (part.freqHz[(size_t) shown.band]) + "  " + Format::signedDecibels (part.gainDb[(size_t) shown.band]));
        const auto p = nodePosition (shown);
        auto box = juce::Rectangle<float> ((float) text.length() * 7.0f + 16.0f, 20.0f).withCentre ({ p.x, p.y - 22.0f });
        box = box.constrainedWithin (plot);
        g.setColour (Palette::panel.withAlpha (0.95f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (Palette::textBright);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText (text, box, juce::Justification::centred, false);
    }

    auto key = juce::Rectangle<float> (plot.getX(), plot.getBottom() + 20.0f, plot.getWidth(), 16.0f);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    const auto item = [&] (juce::Colour c, const juce::String& t, float w)
    {
        g.setColour (c);
        g.fillRect (key.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, 10.0f));
        key.removeFromLeft (6.0f);
        g.setColour (Palette::textDim);
        g.drawText (t, key.removeFromLeft (w), juce::Justification::centredLeft, false);
        key.removeFromLeft (12.0f);
    };
    if (settings.midSide) { item (midColour, "middle", 50.0f); item (sideColour, "sides", 40.0f); }
    else                    item (Palette::accent, "the whole mix", 90.0f);
    item (Palette::textDim.withAlpha (0.4f), "the mix coming in", 120.0f);
    g.setColour (Palette::textDim);
    g.drawText ("drag a dot to move it - double-click: back to 0 dB - C = LOW CUT", key, juce::Justification::centredRight, false);
    if (! on || bypassed)
    {
        g.setColour (Palette::textDim);
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.drawText (bypassed ? "BYPASSED" : "THE EQ IS OFF", plot.withHeight (24.0f).translated (0.0f, 6.0f), juce::Justification::centred, false);
    }
}

//==============================================================================
juce::Rectangle<float> MultibandDisplay::plotArea() const
{
    return getLocalBounds().toFloat().reduced (14.0f, 12.0f).withTrimmedLeft (34.0f).withTrimmedBottom (40.0f);
}
float MultibandDisplay::xOf (double hz) const  { const auto p = plotArea(); return p.getX() + p.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0)); }
double MultibandDisplay::hzAt (float x) const  { const auto p = plotArea(); return 20.0 * std::pow (1000.0, (double) ((x - p.getX()) / p.getWidth())); }
float MultibandDisplay::splitX (int sp) const  { return xOf (settings.splitHz[(size_t) sp]); }

int MultibandDisplay::splitAt (float x) const
{
    int best = -1; float bestDistance = 8.0f;
    for (int sp = 0; sp < MultibandCompressor::numSplits; ++sp)
        if (std::abs (x - splitX (sp)) <= bestDistance) { bestDistance = std::abs (x - splitX (sp)); best = sp; }
    return best;
}

void MultibandDisplay::setState (const MultibandCompressor::Settings& s, const EasyMasterAudioProcessor::MultibandPicture& mb,
                                 const EasyMasterAudioProcessor::TamerPicture& t, bool isOn, bool isBypassed)
{
    settings = s; on = isOn; bypassed = isBypassed;
    for (int b = 0; b < 4; ++b)
        for (int pt = 0; pt < 2; ++pt)
        {
            auto& c = shownCut[(size_t) b][(size_t) pt];
            const float now = mb.cutDb[(size_t) b][(size_t) pt];
            c = now > c ? now : c + 0.25f * (now - c);
            auto& pk = shownPeak[(size_t) b][(size_t) pt];
            const float p = mb.peakDb[(size_t) b][(size_t) pt];
            pk = p > pk ? p : pk + 0.2f * (p - pk);
        }
    centreHz = t.centreHz;
    for (int b = 0; b < ResonanceTamer::numBands; ++b)
    {
        const float now = juce::Decibels::gainToDecibels (juce::Decibels::decibelsToGain (t.levelDb[0][(size_t) b]) + juce::Decibels::decibelsToGain (t.levelDb[1][(size_t) b]), -120.0f);
        shownLevel[(size_t) b] = now > shownLevel[(size_t) b] ? now : shownLevel[(size_t) b] + 0.2f * (now - shownLevel[(size_t) b]);
    }
    repaint();
}

void MultibandDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int sp = splitAt (e.position.x);
    if (sp != hovered) { hovered = sp; repaint(); }
    setMouseCursor (sp >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void MultibandDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragging = splitAt (e.position.x);
    if (dragging >= 0)
        if (auto* p = parameters.getParameter (MasterMBIDs::split[dragging])) p->beginChangeGesture();
}

void MultibandDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    const auto& info = MasterMBIDs::splits[dragging];
    if (auto* p = parameters.getParameter (MasterMBIDs::split[dragging]))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit ((double) info.lowHz, (double) info.highHz, hzAt (e.position.x))));
    if (onEdit) onEdit();
}

void MultibandDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
        if (auto* p = parameters.getParameter (MasterMBIDs::split[dragging])) p->endChangeGesture();
    dragging = -1;
    repaint();
}

void MultibandDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (Palette::background);
    g.fillRoundedRectangle (area, 6.0f);
    const auto plot = plotArea();
    constexpr float cutRange = 12.0f;

    // The mix (faint)
    if (centreHz[0] > 0.0f)
    {
        juce::Path mix;
        mix.startNewSubPath (xOf (centreHz[0]), plot.getBottom());
        for (int b = 0; b < ResonanceTamer::numBands; ++b)
            mix.lineTo (xOf (centreHz[(size_t) b]), plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (shownLevel[(size_t) b] + 72.0f) / 72.0f));
        mix.lineTo (xOf (centreHz[(size_t) ResonanceTamer::numBands - 1]), plot.getBottom());
        mix.closeSubPath();
        g.setColour (Palette::textDim.withAlpha (0.14f));
        g.fillPath (mix);
    }
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        g.setColour (Palette::panelEdge.withAlpha (0.6f));
        g.drawVerticalLine (juce::roundToInt (xOf (hz)), plot.getY(), plot.getBottom());
        g.setColour (Palette::textDim);
        g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (xOf (hz) - 20.0f, plot.getBottom() + 3.0f, 40.0f, 14.0f), juce::Justification::centred, false);
    }
    for (float db : { 0.0f, 6.0f, 12.0f })
    {
        const float y = plot.getY() + plot.getHeight() * 0.6f * db / cutRange;
        g.setColour (Palette::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (Palette::textDim);
        g.drawText (db < 0.5f ? juce::String ("0") : "-" + juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 36.0f, y - 7.0f, 32.0f, 14.0f), juce::Justification::centredRight, false);
    }

    bool anySolo = false;
    for (bool so : settings.solo) anySolo = anySolo || so;
    for (int b = 0; b < 4; ++b)
    {
        const float x0 = b == 0 ? plot.getX() : splitX (b - 1), x1 = b == 3 ? plot.getRight() : splitX (b);
        const auto region = juce::Rectangle<float>::leftTopRightBottom (x0, plot.getY(), x1, plot.getBottom());
        const bool heard = ! anySolo || settings.solo[(size_t) b];
        const bool bandOn = settings.bandOn[(size_t) b];
        if (! heard)
        {
            g.setColour (Palette::background.withAlpha (0.7f));
            g.fillRect (region);
        }
        // How much it's turning down: hanging from the top, the middle (or both) and, in MID + SIDE, the sides beside it
        const auto bar = region.reduced (region.getWidth() * 0.18f, 0.0f);
        const auto cutH = [&] (float db) { return plot.getHeight() * 0.6f * juce::jlimit (0.0f, 1.0f, db / cutRange); };
        if (on && ! bypassed && bandOn)
        {
            if (settings.midSide)
            {
                auto left = bar.withWidth (bar.getWidth() * 0.5f - 2.0f), right = left.withX (bar.getCentreX() + 2.0f);
                g.setColour (Palette::meterHigh.withAlpha (0.85f));
                g.fillRect (left.withHeight (cutH (shownCut[(size_t) b][0])));
                g.setColour (sideColour.interpolatedWith (Palette::meterHigh, 0.35f));
                g.fillRect (right.withHeight (cutH (shownCut[(size_t) b][1])));
            }
            else
            {
                g.setColour (Palette::meterHigh.withAlpha (0.85f));
                g.fillRect (bar.withHeight (cutH (shownCut[(size_t) b][0])));
            }
        }
        // The band's name and how much (bottom)
        g.setColour (bandOn ? (heard ? Palette::textBright : Palette::textDim) : Palette::textDim.withAlpha (0.5f));
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        auto label = region.withTrimmedTop (region.getHeight() - 46.0f);
        g.drawText (juce::String (b + 1) + "  " + MasterMBIDs::bandNames[b] + (bandOn ? "" : "  (off)"), label.removeFromTop (20.0f), juce::Justification::centred, false);
        g.setFont (juce::Font (juce::FontOptions (11.5f)));
        const auto cutText = [&] (float db) { return db < 0.05f ? juce::String ("0.0") : "-" + juce::String (db, 1); };
        g.setColour (Palette::textDim);
        g.drawText (settings.midSide ? "mid " + cutText (shownCut[(size_t) b][0]) + " / side " + cutText (shownCut[(size_t) b][1]) + " dB"
                                     : cutText (shownCut[(size_t) b][0]) + " dB", label, juce::Justification::centred, false);
    }
    // The splits: drag them
    for (int sp = 0; sp < MultibandCompressor::numSplits; ++sp)
    {
        const float x = splitX (sp);
        const bool lit = sp == hovered || sp == dragging;
        g.setColour (lit ? Palette::accent : Palette::accent.withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> (x - (lit ? 1.5f : 1.0f), plot.getY(), lit ? 3.0f : 2.0f, plot.getHeight()));
        const auto tag = juce::Rectangle<float> (64.0f, 18.0f).withCentre ({ x, plot.getY() + plot.getHeight() * 0.72f });
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (tag, 4.0f);
        g.setColour (Palette::accent);
        g.drawRoundedRectangle (tag, 4.0f, 1.0f);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (Format::hertz (settings.splitHz[(size_t) sp]), tag, juce::Justification::centred, false);
    }

    auto key = juce::Rectangle<float> (plot.getX(), plot.getBottom() + 20.0f, plot.getWidth(), 16.0f);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    const auto item = [&] (juce::Colour c, const juce::String& t, float w)
    {
        g.setColour (c);
        g.fillRect (key.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, 10.0f));
        key.removeFromLeft (6.0f);
        g.setColour (Palette::textDim);
        g.drawText (t, key.removeFromLeft (w), juce::Justification::centredLeft, false);
        key.removeFromLeft (12.0f);
    };
    item (Palette::meterHigh.withAlpha (0.85f), settings.midSide ? "middle turned down" : "turned down", settings.midSide ? 120.0f : 80.0f);
    if (settings.midSide) item (sideColour.interpolatedWith (Palette::meterHigh, 0.35f), "sides turned down", 120.0f);
    g.setColour (Palette::textDim);
    g.drawText ("drag the lines between the bands to move the splits", key, juce::Justification::centredRight, false);
    if (! on || bypassed)
    {
        g.setColour (Palette::textDim);
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.drawText (bypassed ? "BYPASSED" : "THE MULTIBAND IS OFF", plot.withHeight (24.0f).translated (0.0f, 6.0f), juce::Justification::centred, false);
    }
}

//==============================================================================
EasyMasterAudioProcessorEditor::EasyMasterAudioProcessorEditor (EasyMasterAudioProcessor& p)
    : EasyWindow (p, "EASY MASTER", "The mastering chain for your master bus - hover over anything for help", gettingStartedHint),
      masterProcessor (p),
      modeSwitch (*p.parameters.getParameter (MasterIDs::compMode), { "STEREO", "MID + SIDE" }, Palette::accent),
      reactsSwitch (*p.parameters.getParameter (MasterIDs::reactsTo), { "PEAKS", "AVERAGE" }, Palette::accent),
      characterSwitch (*p.parameters.getParameter (MasterIDs::character), { "MODERN", "VINTAGE" }, Palette::accent),
      releaseSwitch (*p.parameters.getParameter (MasterIDs::releaseMode), { "AUTO", "SET" }, Palette::accent),
      listenSwitch (*p.parameters.getParameter (MasterIDs::listen), { "STEREO", "MID", "SIDE", "MONO" }, Palette::meterMid),
      mbModeSwitch (*p.parameters.getParameter (MasterMBIDs::mode), { "STEREO", "MID + SIDE" }, Palette::accent),
      eqModeSwitch (*p.parameters.getParameter (MasterEQIDs::mode), { "STEREO", "MID + SIDE" }, Palette::accent),
      tamerModeSwitch (*p.parameters.getParameter (MasterIDs::tamerMode), { "STEREO", "MID + SIDE" }, Palette::accent),
      incoming ((size_t) MasterDisplay::numMoments)
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];
        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 96, 24);
        addAndMakeVisible (knob.slider);
        knob.label.setText (knobInfo[i].name, juce::dontSendNotification);
        const bool big = i == gainKnob || i == midThresholdKnob || i == sideThresholdKnob || i == tMidAmountKnob || i == tSideAmountKnob;
        knob.label.setFont (juce::Font (juce::FontOptions (big ? 15.0f : 13.0f, juce::Font::bold)));
        knob.label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (knob.label);
        knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (easyProcessor.parameters, knobInfo[i].parameterID, knob.slider);
        knob.slider.onValueChange = [this] { knobMoved(); };
        const bool side = (i >= sideThresholdKnob && i <= sideMakeupKnob) || (i >= tSideAmountKnob && i <= tSideSharpKnob);
        const bool mid = (i >= midThresholdKnob && i <= midMakeupKnob) || (i >= tMidAmountKnob && i <= tMidSharpKnob);
        const bool tamerKnob = i >= tMidAmountKnob;
        const juce::String knobTitle = juce::String (tamerKnob ? "TAMER " : "") + (side ? "SIDE " : (mid ? "MID " : "")) + knobInfo[i].name;
        addHint (knob.slider, knobTitle, knobInfo[i].hint);
        addHint (knob.label, knobTitle, knobInfo[i].hint);
        if (side) knob.slider.setColour (juce::Slider::rotarySliderFillColourId, sideColour);
        if (mid)  knob.slider.setColour (juce::Slider::rotarySliderFillColourId, midColour);
    }

    // The EQ's knobs: per part, FREQ and GAIN for each band, then LOW CUT
    for (int part = 0; part < 2; ++part)
        for (int c = 0; c < 11; ++c)
        {
            auto& knob = eqKnobs[(size_t) part][(size_t) c];
            const bool isLowCut = c == 10, isGain = ! isLowCut && c % 2 == 1;
            const int band = c / 2;
            const char* id = isLowCut ? MasterEQIDs::lowCut[part] : (isGain ? MasterEQIDs::gain[part][band] : MasterEQIDs::freq[part][band]);
            knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 22);
            knob.slider.setColour (juce::Slider::rotarySliderFillColourId, part == 0 ? midColour : sideColour);
            addAndMakeVisible (knob.slider);
            knob.label.setText (isLowCut ? "LOW CUT" : (isGain ? "GAIN" : "FREQ"), juce::dontSendNotification);
            knob.label.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
            knob.label.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (knob.label);
            knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (easyProcessor.parameters, id, knob.slider);
            knob.slider.onValueChange = [this] { knobMoved(); };
            const juce::String eqTitle = juce::String ("EQ ") + (part == 0 ? "MID " : "SIDE ")
                                       + (isLowCut ? juce::String ("LOW CUT") : juce::String (MasterEQIDs::bands[band].name) + (isGain ? " GAIN" : " FREQ"));
            const char* hint = isLowCut ? eqLowCutHint : (isGain ? eqGainHint : eqFreqHint);
            addHint (knob.slider, eqTitle, hint);
            addHint (knob.label, eqTitle, hint);
        }
    // The MULTIBAND's controls: per part, each band's THRESHOLD, RATIO, SPEED and GAIN
    for (int part = 0; part < 2; ++part)
        for (int c = 0; c < 16; ++c)
        {
            auto& knob = mbKnobs[(size_t) part][(size_t) c];
            const int band = c / 4, which = c % 4;
            const char* id = which == 0 ? MasterMBIDs::threshold[part][band] : which == 1 ? MasterMBIDs::ratio[part][band]
                           : which == 2 ? MasterMBIDs::speed[part][band] : MasterMBIDs::gain[part][band];
            knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 22);
            knob.slider.setColour (juce::Slider::rotarySliderFillColourId, part == 0 ? midColour : sideColour);
            addAndMakeVisible (knob.slider);
            const char* names[4] { "THRESH", "RATIO", "SPEED", "GAIN" };
            knob.label.setText (names[which], juce::dontSendNotification);
            knob.label.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
            knob.label.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (knob.label);
            knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (easyProcessor.parameters, id, knob.slider);
            knob.slider.onValueChange = [this] { knobMoved(); };
            const char* full[4] { "THRESHOLD", "RATIO", "SPEED", "GAIN" };
            const char* knobHints[4] { mbThresholdHint, mbRatioHint, mbSpeedHint, mbGainHint };
            const juce::String mbTitle = juce::String ("MULTIBAND ") + (part == 0 ? "MID " : "SIDE ") + MasterMBIDs::bandNames[band] + " " + full[which];
            addHint (knob.slider, mbTitle, knobHints[which]);
            addHint (knob.label, mbTitle, knobHints[which]);
        }
    for (int b = 0; b < 4; ++b)
    {
        for (auto [button, attachment, id, hint, suffix] : { std::tuple<LightButton*, std::unique_ptr<juce::ButtonParameterAttachment>*, const char*, const char*, const char*>
                                                            { &mbBandOn[(size_t) b], &mbBandOnAttachments[(size_t) b], MasterMBIDs::bandOn[b], mbBandOnHint, " ON" },
                                                            { &mbSolo[(size_t) b], &mbSoloAttachments[(size_t) b], MasterMBIDs::solo[b], mbSoloHint, " SOLO" } })
        {
            button->setClickingTogglesState (true);
            *attachment = std::make_unique<juce::ButtonParameterAttachment> (*p.parameters.getParameter (id), *button);
            addAndMakeVisible (*button);
            addHint (*button, juce::String ("MULTIBAND ") + MasterMBIDs::bandNames[b] + suffix, hint);
        }
        mbBandOn[(size_t) b].onClick = [this] { knobMoved(); showParts(); };
    }
    for (int sp = 0; sp < 3; ++sp)
    {
        auto& sl = mbSplits[(size_t) sp];
        sl.setSliderStyle (juce::Slider::LinearBar);
        sl.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 0, 0);
        sl.setColour (juce::Slider::trackColourId, Palette::accent.withAlpha (0.35f));
        addAndMakeVisible (sl);
        mbSplitAttachments[(size_t) sp] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (easyProcessor.parameters, MasterMBIDs::split[sp], sl);
        sl.onValueChange = [this] { knobMoved(); };
        addHint (sl, "MULTIBAND SPLIT " + juce::String (sp + 1), mbSplitHint);
    }
    mbSplitLabel.setText ("SPLITS", juce::dontSendNotification);
    mbSplitLabel.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    mbSplitLabel.setColour (juce::Label::textColourId, Palette::textDim);
    addAndMakeVisible (mbSplitLabel);
    addHint (mbSplitLabel, "MULTIBAND SPLITS", mbSplitHint);
    mbOnButton.setClickingTogglesState (true);
    mbOnAttachment = std::make_unique<juce::ButtonParameterAttachment> (*p.parameters.getParameter (MasterMBIDs::on), mbOnButton);
    addAndMakeVisible (mbOnButton);
    addHint (mbOnButton, "MULTIBAND ON", mbOnHint);
    mbModeSwitch.setHorizontal (true);
    addAndMakeVisible (mbModeSwitch);
    mbModeSwitch.onUserChange = [this] { knobMoved(); };
    addHint (mbModeSwitch, "MULTIBAND - STEREO / MID + SIDE", mbModeHint);
    addAndMakeVisible (mbDisplay);
    addHint (mbDisplay, "THE MULTIBAND", "The four bands across the spectrum: drag the lines between them to move the splits. From the top: how much "
             "each band is being turned down (red = the middle or both, blue = the sides). Dimmed = not heard (another band is soloed).");
    mbDisplay.onEdit = [this] { knobMoved(); };

    eqOnButton.setClickingTogglesState (true);
    eqOnAttachment = std::make_unique<juce::ButtonParameterAttachment> (*p.parameters.getParameter (MasterEQIDs::on), eqOnButton);
    addAndMakeVisible (eqOnButton);
    addHint (eqOnButton, "EQ ON", eqOnHint);
    eqModeSwitch.setHorizontal (true);
    addAndMakeVisible (eqModeSwitch);
    eqModeSwitch.onUserChange = [this] { knobMoved(); };
    addHint (eqModeSwitch, "EQ - STEREO / MID + SIDE", eqModeHint);
    addAndMakeVisible (eqDisplay);
    addHint (eqDisplay, "THE EQ'S CURVE", eqDisplayHint);
    eqDisplay.onEdit = [this] { knobMoved(); };

    initAllButton.setColour (juce::TextButton::buttonColourId, Palette::panel);
    initAllButton.setColour (juce::TextButton::textColourOffId, Palette::meterHigh);
    initAllButton.setColour (juce::ComboBox::outlineColourId, Palette::meterHigh);
    initAllButton.onClick = [this] { presetBrowser.onPresetChosen (easyProcessor.getBuiltInPresets().front()); };
    addAndMakeVisible (initAllButton);
    addHint (initAllButton, "INIT", initAllHint);

    compOnButton.setClickingTogglesState (true);
    compOnAttachment = std::make_unique<juce::ButtonParameterAttachment> (*p.parameters.getParameter (MasterIDs::compOn), compOnButton);
    addAndMakeVisible (compOnButton);
    addHint (compOnButton, "COMPRESSOR ON", compOnHint);

    tamerOnButton.setClickingTogglesState (true);
    tamerOnAttachment = std::make_unique<juce::ButtonParameterAttachment> (*p.parameters.getParameter (MasterIDs::tamerOn), tamerOnButton);
    addAndMakeVisible (tamerOnButton);
    addHint (tamerOnButton, "TAMER ON", tamerOnHint);
    tamerListenButton.setClickingTogglesState (true);
    tamerListenAttachment = std::make_unique<juce::ButtonParameterAttachment> (*p.parameters.getParameter (MasterIDs::tListen), tamerListenButton);
    addAndMakeVisible (tamerListenButton);
    addHint (tamerListenButton, "TAMER - LISTEN", tamerListenHint);

    for (auto* s : { &modeSwitch, &reactsSwitch, &characterSwitch, &listenSwitch, &tamerModeSwitch, &releaseSwitch })
        s->setHorizontal (true);
    for (auto [s, switchTitle, hint] : { std::tuple<ChoiceSwitch*, const char*, const char*> { &modeSwitch, "STEREO / MID + SIDE", modeHint },
                                   { &reactsSwitch, "REACTS TO", reactsHint }, { &characterSwitch, "CHARACTER", characterHint },
                                   { &releaseSwitch, "LIMITER RELEASE", limitReleaseHint }, { &listenSwitch, "LISTEN", listenHint },
                                   { &tamerModeSwitch, "TAMER - STEREO / MID + SIDE", tamerModeHint } })
    {
        addAndMakeVisible (*s);
        s->onUserChange = [this] { knobMoved(); };
        addHint (*s, switchTitle, hint);
    }

    matchButton.setClickingTogglesState (true);
    matchAttachment = std::make_unique<juce::ButtonParameterAttachment> (*p.parameters.getParameter (MasterIDs::match), matchButton);
    addAndMakeVisible (matchButton);
    addHint (matchButton, "MATCH", matchHint);

    for (auto* l : { &matchInfo, &inputWarning, &listenLabel, &abLabel })
    {
        l->setFont (juce::Font (juce::FontOptions (12.0f)));
        l->setColour (juce::Label::textColourId, Palette::textDim);
        l->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (*l);
    }
    listenLabel.setText ("LISTEN (for checking)", juce::dontSendNotification);
    listenLabel.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    abLabel.setText ("A / B", juce::dontSendNotification);
    abLabel.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    addHint (matchInfo, "MATCH", matchHint);
    addHint (listenLabel, "LISTEN", listenHint);
    addHint (inputWarning, "TRIM", knobInfo[trimKnob].hint);

    for (auto* b : { &slotA, &slotB })
    {
        b->setRadioGroupId (102);
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
        addHint (*b, "A / B", abHint);
    }
    slotA.onClick = [this] { easyProcessor.switchToSlot (0); };
    slotB.onClick = [this] { easyProcessor.switchToSlot (1); };
    copyButton.setColour (juce::TextButton::buttonColourId, Palette::panel);
    copyButton.setColour (juce::TextButton::textColourOffId, Palette::textBright);
    copyButton.onClick = [this] { easyProcessor.copyToOtherSlot(); };
    addAndMakeVisible (copyButton);
    addHint (copyButton, "COPY", abHint);
    addHint (abLabel, "A / B", abHint);

    for (int m = 0; m < MasterModules::count; ++m)
    {
        auto& mp = modulePresets[(size_t) m];
        mp.ids = MasterModules::ids (m);
        mp.browser.setPresets (juce::String ("Easy Master - ") + MasterModules::names[m], MasterModules::presets (m));
        mp.browser.getCurrentValues = [this, m]
        {
            std::vector<std::pair<juce::String, float>> values;
            for (auto& id : modulePresets[(size_t) m].ids)
                values.push_back ({ id, easyProcessor.parameters.getRawParameterValue (id)->load() });
            return values;
        };
        mp.browser.onPresetChosen = [this, m] (const Preset& preset) { applyModulePreset (m, &preset); };
        for (auto* b : { &mp.pr, &mp.init })
        {
            b->setColour (juce::TextButton::buttonColourId, Palette::panel);
            b->setColour (juce::TextButton::textColourOffId, b == &mp.init ? Palette::meterHigh : Palette::textBright);
            b->setColour (juce::ComboBox::outlineColourId, b == &mp.init ? Palette::meterHigh : Palette::panelEdge);
            addAndMakeVisible (*b);
        }
        mp.pr.onClick = [this, m] { showModuleMenu (m); };
        mp.init.onClick = [this, m] { applyModulePreset (m, nullptr); };
        const juce::String moduleName = juce::String (MasterModules::names[m]).toUpperCase();
        addHint (mp.pr, moduleName + " - Pr", "This module's own presets: they change only the " + moduleName
                 + " and leave the rest of the chain as it is. Save your own from the same menu.");
        addHint (mp.init, moduleName + " - INIT", "Puts only the " + moduleName + " back to its default settings; the rest of the chain stays as it is.");
        mp.name.setFont (juce::Font (juce::FontOptions (11.5f)));
        mp.name.setColour (juce::Label::textColourId, Palette::accent);
        mp.name.setJustificationType (juce::Justification::centredRight);
        mp.name.setText (masterProcessor.modulePresetNames[(size_t) m], juce::dontSendNotification);
        addAndMakeVisible (mp.name);
    }

    addAndMakeVisible (display);
    addHint (display, "WHAT IT'S DOING", displayHint);
    addAndMakeVisible (tamerDisplay);
    addHint (tamerDisplay, "WHAT THE TAMER IS DOING", tamerDisplayHint);
    addAndMakeVisible (meters);
    addHint (meters, "METERS", metersHint);
    meters.onReset = [this] { masterProcessor.resetMeters(); };

    shownPage = juce::jlimit (0, numPages - 1, masterProcessor.shownPage);
    finishSetup (1680, 900);

    for (auto& knob : knobs)
        for (auto* child : knob.slider.getChildren())
            if (auto* box = dynamic_cast<juce::Label*> (child))
                box->setEditable (false, true, false);
    showPage (shownPage);
    refreshLiveDisplays();
}

void EasyMasterAudioProcessorEditor::applyModulePreset (int module, const Preset* preset)
{
    // Only this module's settings: the ones the preset gives, the rest of the module back to its defaults
    auto& mp = modulePresets[(size_t) module];
    for (auto& id : mp.ids)
        if (auto* param = easyProcessor.parameters.getParameter (id))
        {
            float normalised = param->getDefaultValue();
            if (preset != nullptr)
                for (auto& [pid, value] : preset->values)
                    if (pid == id) normalised = param->convertTo0to1 (value);
            param->beginChangeGesture();
            param->setValueNotifyingHost (normalised);
            param->endChangeGesture();
        }
    const auto shownName = preset != nullptr ? preset->name : juce::String();
    masterProcessor.modulePresetNames[(size_t) module] = shownName;
    mp.name.setText (shownName, juce::dontSendNotification);
    knobMoved();
    showParts();
}

void EasyMasterAudioProcessorEditor::showModuleMenu (int module)
{
    auto& mp = modulePresets[(size_t) module];
    auto menu = mp.browser.buildMenu();
    juce::Component::SafePointer<EasyMasterAudioProcessorEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&mp.pr).withMinimumWidth (200), [safe, module] (int result)
    {
        if (safe == nullptr || result == 0) return;
        auto& b = safe->modulePresets[(size_t) module].browser;
        if (result != PresetBrowser::saveItem) { b.handleMenuResult (result); return; }

        // Saving your own: name it
        auto* box = new juce::AlertWindow ("Save " + juce::String (MasterModules::names[module]) + " preset", "Name your preset:", juce::MessageBoxIconType::NoIcon);
        box->addTextEditor ("name", {});
        box->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
        box->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        box->enterModalState (true, juce::ModalCallbackFunction::create ([safe, module, box] (int button)
        {
            const auto name = box->getTextEditorContents ("name").trim();
            if (safe == nullptr || button != 1 || name.isEmpty()) return;
            auto& br = safe->modulePresets[(size_t) module].browser;
            const auto savedAs = br.saveUserPreset (name);
            safe->masterProcessor.modulePresetNames[(size_t) module] = savedAs;
            safe->modulePresets[(size_t) module].name.setText (savedAs, juce::dontSendNotification);
        }), true);
    });
}

void EasyMasterAudioProcessorEditor::placeModuleButtons (int module, juce::Rectangle<int> panel)
{
    // Top right of the module's panel, on its caption line: the module preset's name, INIT, Pr
    auto& mp = modulePresets[(size_t) module];
    auto row = juce::Rectangle<int> (panel.getX(), panel.getY() + 6, panel.getWidth() - 10, 24);
    mp.pr.setBounds (row.removeFromRight (34));
    row.removeFromRight (6);
    mp.init.setBounds (row.removeFromRight (48));
    row.removeFromRight (8);
    mp.name.setBounds (row.removeFromRight (juce::jmin (260, row.getWidth() / 2)));
}

std::vector<juce::Component*> EasyMasterAudioProcessorEditor::pageComponents (int page)
{
    std::vector<juce::Component*> list;
    const int module = page == tamerPage ? MasterModules::tamer : page == eqPage ? MasterModules::eq : page == mbPage ? MasterModules::multiband : MasterModules::comp;
    for (juce::Component* c : { (juce::Component*) &modulePresets[(size_t) module].pr, (juce::Component*) &modulePresets[(size_t) module].init,
                                (juce::Component*) &modulePresets[(size_t) module].name })
        list.push_back (c);
    const auto addKnobs = [&] (int from, int to) { for (int k = from; k <= to; ++k) { list.push_back (&knobs[(size_t) k].slider); list.push_back (&knobs[(size_t) k].label); } };
    if (page == mbPage)
    {
        for (auto& row : mbKnobs) for (auto& k : row) { list.push_back (&k.slider); list.push_back (&k.label); }
        for (auto& b : mbBandOn) list.push_back (&b);
        for (auto& b : mbSolo) list.push_back (&b);
        for (auto& sl : mbSplits) list.push_back (&sl);
        for (juce::Component* c : { (juce::Component*) &mbDisplay, (juce::Component*) &mbOnButton, (juce::Component*) &mbModeSwitch, (juce::Component*) &mbSplitLabel })
            list.push_back (c);
    }
    else if (page == eqPage)
    {
        for (auto& row : eqKnobs) for (auto& k : row) { list.push_back (&k.slider); list.push_back (&k.label); }
        for (juce::Component* c : { (juce::Component*) &eqDisplay, (juce::Component*) &eqOnButton, (juce::Component*) &eqModeSwitch })
            list.push_back (c);
    }
    else if (page == tamerPage)
    {
        addKnobs (tMidAmountKnob, tToKnob);
        for (juce::Component* c : { (juce::Component*) &tamerDisplay, (juce::Component*) &tamerOnButton, (juce::Component*) &tamerListenButton, (juce::Component*) &tamerModeSwitch })
            list.push_back (c);
    }
    else
    {
        addKnobs (midThresholdKnob, ignoreKnob);
        for (juce::Component* c : { (juce::Component*) &display, (juce::Component*) &compOnButton, (juce::Component*) &modeSwitch,
                                    (juce::Component*) &reactsSwitch, (juce::Component*) &characterSwitch })
            list.push_back (c);
    }
    return list;
}

void EasyMasterAudioProcessorEditor::showPage (int page)
{
    shownPage = juce::jlimit (0, numPages - 1, page);
    masterProcessor.shownPage = shownPage;
    for (int other = 0; other < numPages; ++other)
        for (auto* c : pageComponents (other))
            c->setVisible (other == shownPage);
    showParts();
    repaint();
}

void EasyMasterAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    // The chain along the top: RESONANCE TAMER, EQ and COMPRESSOR are pages
    const auto at = e.getEventRelativeTo (this).getPosition().toFloat();
    if (chainBoxes[1].contains (at))      showPage (tamerPage);
    else if (chainBoxes[2].contains (at)) showPage (eqPage);
    else if (chainBoxes[3].contains (at)) showPage (mbPage);
    else if (chainBoxes[4].contains (at)) showPage (compPage);
}

void EasyMasterAudioProcessorEditor::showParts()
{
    // Both rows always showing; the SIDE row only works in MID + SIDE (in STEREO the top row does both)
    const auto& params = easyProcessor.parameters;
    const bool ms = masterProcessor.isMidSide(), on = params.getRawParameterValue (MasterIDs::compOn)->load() > 0.5f;
    const bool tms = masterProcessor.isTamerMidSide(), ton = params.getRawParameterValue (MasterIDs::tamerOn)->load() > 0.5f;
    const auto dim = [&] (int from, int to, int sideFrom, int sideTo, bool isMs, bool isOn)
    {
        for (int k = from; k <= to; ++k)
        {
            const bool sideKnob = k >= sideFrom && k <= sideTo;
            const bool usable = isOn && (! sideKnob || isMs);
            knobs[(size_t) k].slider.setEnabled (! sideKnob || isMs);
            knobs[(size_t) k].slider.setAlpha (usable ? 1.0f : 0.3f);
            knobs[(size_t) k].label.setAlpha (usable ? 1.0f : 0.3f);
        }
    };
    dim (midThresholdKnob, ignoreKnob, sideThresholdKnob, sideMakeupKnob, ms, on);
    dim (tMidAmountKnob, tToKnob, tSideAmountKnob, tSideSharpKnob, tms, ton);
    const bool mms = masterProcessor.isMultibandMidSide(), mon = params.getRawParameterValue (MasterMBIDs::on)->load() > 0.5f;
    for (int part = 0; part < 2; ++part)
        for (int c = 0; c < 16; ++c)
        {
            auto& k = mbKnobs[(size_t) part][(size_t) c];
            const bool bandOn = params.getRawParameterValue (MasterMBIDs::bandOn[c / 4])->load() > 0.5f;
            const bool usable = mon && bandOn && (part == 0 || mms);
            k.slider.setEnabled (part == 0 || mms);
            k.slider.setAlpha (usable ? 1.0f : 0.3f);
            k.label.setAlpha (usable ? 1.0f : 0.3f);
            if (part == 0) k.slider.setColour (juce::Slider::rotarySliderFillColourId, mms ? midColour : Palette::accent);
        }
    const bool ems = masterProcessor.isEQMidSide(), eon = params.getRawParameterValue (MasterEQIDs::on)->load() > 0.5f;
    for (int part = 0; part < 2; ++part)
        for (auto& k : eqKnobs[(size_t) part])
        {
            const bool usable = eon && (part == 0 || ems);
            k.slider.setEnabled (part == 0 || ems);
            k.slider.setAlpha (usable ? 1.0f : 0.3f);
            k.label.setAlpha (usable ? 1.0f : 0.3f);
            if (part == 0) k.slider.setColour (juce::Slider::rotarySliderFillColourId, ems ? midColour : Palette::accent);
        }
    repaint();
}

//==============================================================================
void EasyMasterAudioProcessorEditor::paintBody (juce::Graphics& g)
{
    // The chain: the stages in order; the two pages can be clicked, the one showing is lit
    {
        struct Stage { const char* name; bool built; int page; };
        const Stage stages[] { { "INPUT", true, -1 }, { "RESONANCE TAMER", true, tamerPage }, { "EQ", true, eqPage }, { "MULTIBAND", true, mbPage },
                               { "COMPRESSOR", true, compPage }, { "PUNCH", false, -1 }, { "WIDTH", false, -1 }, { "LIMITER", true, -1 }, { "OUTPUT", true, -1 } };
        const auto& params = easyProcessor.parameters;
        const bool tamerOn = params.getRawParameterValue (MasterIDs::tamerOn)->load() > 0.5f;
        const bool compOn = params.getRawParameterValue (MasterIDs::compOn)->load() > 0.5f;
        const bool eqOn = params.getRawParameterValue (MasterEQIDs::on)->load() > 0.5f;
        const bool mbOn = params.getRawParameterValue (MasterMBIDs::on)->load() > 0.5f;
        const int count = (int) std::size (stages);
        const float arrow = 18.0f;
        const float w = ((float) chainArea.getWidth() - arrow * (float) (count - 1)) / (float) count;
        for (int i = 0; i < count; ++i)
        {
            const auto& st = stages[i];
            const auto box = juce::Rectangle<float> ((float) chainArea.getX() + (float) i * (w + arrow), (float) chainArea.getY(), w, (float) chainArea.getHeight());
            chainBoxes[(size_t) i] = box;
            const bool showing = st.page >= 0 && st.page == shownPage;
            g.setColour (showing ? Palette::accent.withAlpha (0.25f) : (st.built ? Palette::panel : Palette::background));
            g.fillRoundedRectangle (box, 6.0f);
            g.setColour (showing ? Palette::accent : (st.built ? Palette::accent.withAlpha (0.55f) : Palette::panelEdge));
            g.drawRoundedRectangle (box.reduced (0.75f), 6.0f, showing ? 2.0f : 1.2f);
            g.setColour (st.built ? Palette::textBright : Palette::textDim.withAlpha (0.55f));
            g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
            auto b = box.reduced (4.0f, 4.0f);
            if (st.page >= 0)
            {
                // (a page: its name, a lamp for on / off, and "click to show" until it's showing)
                const bool isOn = st.page == tamerPage ? tamerOn : st.page == eqPage ? eqOn : st.page == mbPage ? mbOn : compOn;
                g.setColour (isOn ? Palette::meterLow : Palette::meterOff);
                g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ b.getX() + 8.0f, b.getCentreY() }));
                g.setColour (Palette::textBright);
                g.drawText (st.name, b.withTrimmedLeft (14.0f).removeFromTop (b.getHeight() * 0.6f), juce::Justification::centredBottom, false);
                g.setColour (showing ? Palette::accent : Palette::textDim);
                g.setFont (juce::Font (juce::FontOptions (10.0f)));
                g.drawText (showing ? "showing below" : "click to show", b.withTrimmedLeft (14.0f).withTrimmedTop (b.getHeight() * 0.6f), juce::Justification::centredTop, false);
            }
            else if (st.built)
                g.drawText (st.name, box, juce::Justification::centred, false);
            else
            {
                g.drawText (st.name, b.removeFromTop (b.getHeight() * 0.58f), juce::Justification::centredBottom, false);
                g.setFont (juce::Font (juce::FontOptions (10.0f)));
                g.drawText ("coming", b, juce::Justification::centredTop, false);
            }
            if (i < count - 1)
            {
                const float ax = box.getRight() + 4.0f, ay = box.getCentreY();
                juce::Path a;
                a.addTriangle (ax, ay - 5.0f, ax, ay + 5.0f, ax + arrow - 8.0f, ay);
                g.setColour (Palette::textDim.withAlpha (0.6f));
                g.fillPath (a);
            }
        }
    }

    drawPanel (g, inputPanel, "1", "INPUT");
    drawPanel (g, metersPanel, {}, "METERS");
    drawPanel (g, limiterPanel, "6", "LIMITER");
    drawPanel (g, comparePanel, "", "COMPARE");

    const auto rowName = [&g] (juce::Rectangle<int> r, const juce::String& name, const juce::String& under, juce::Colour c, bool lit)
    {
        const auto box = r.toFloat().reduced (2.0f, 6.0f);
        g.setColour (lit ? c.withAlpha (0.16f) : Palette::background);
        g.fillRoundedRectangle (box, 6.0f);
        g.setColour (lit ? c : Palette::panelEdge);
        g.drawRoundedRectangle (box.reduced (0.75f), 6.0f, 1.3f);
        g.setColour (lit ? c : Palette::textDim.withAlpha (0.5f));
        g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        auto text = box.reduced (2.0f, 0.0f);
        g.drawText (name, text.removeFromTop (text.getHeight() * 0.55f), juce::Justification::centredBottom, false);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawFittedText (under, text.toNearestInt(), juce::Justification::centredTop, 2);
    };

    if (shownPage == compPage)
    {
        drawPanel (g, pageDisplayPanel, {}, "COMPRESSOR & LIMITER - WHAT THEY'RE DOING");
        drawPanel (g, pagePanel, "5", "COMPRESSOR");
        const bool ms = masterProcessor.isMidSide();
        rowName (midRowName, ms ? "MID" : "STEREO", ms ? "kick, bass,\nsnare, vocal" : "the whole mix", midColour, true);
        rowName (sideRowName, "SIDE", ms ? "the width" : "only in\nMID + SIDE", sideColour, ms);
    }
    else if (shownPage == mbPage)
    {
        drawPanel (g, pageDisplayPanel, {}, "MULTIBAND - THE FOUR BANDS");
        drawPanel (g, pagePanel, "4", "MULTIBAND");
        const bool ms = masterProcessor.isMultibandMidSide();
        rowName (mbMidName, ms ? "MID" : "STEREO", ms ? "kick, bass,\nsnare, vocal" : "the whole mix", ms ? midColour : Palette::accent, true);
        rowName (mbSideName, "SIDE", ms ? "the width" : "only in\nMID + SIDE", sideColour, ms);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        for (int b = 0; b < 4; ++b)
        {
            g.setColour (Palette::textDim);
            g.drawText (juce::String (b + 1) + "  " + MasterMBIDs::bandNames[b], mbBandNames[(size_t) b].withTrimmedRight (144), juce::Justification::centredLeft, false);
            g.setColour (Palette::panelEdge);
            g.drawHorizontalLine (mbBandNames[(size_t) b].getBottom(), (float) mbBandNames[(size_t) b].getX(), (float) mbBandNames[(size_t) b].getRight() - 8.0f);
        }
    }
    else if (shownPage == eqPage)
    {
        drawPanel (g, pageDisplayPanel, {}, "EQ - THE CURVE");
        drawPanel (g, pagePanel, "3", "EQ");
        const bool ms = masterProcessor.isEQMidSide();
        rowName (eqMidName, ms ? "MID" : "STEREO", ms ? "kick, bass,\nsnare, vocal" : "the whole mix", ms ? midColour : Palette::accent, true);
        rowName (eqSideName, "SIDE", ms ? "the width" : "only in\nMID + SIDE", sideColour, ms);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        for (int b = 0; b < 6; ++b)
        {
            g.setColour (Palette::textDim);
            g.drawText (b < 5 ? juce::String (b + 1) + "  " + MasterEQIDs::bands[b].name : juce::String ("CLEAN UP"), eqBandNames[(size_t) b], juce::Justification::centred, false);
            g.setColour (Palette::panelEdge);
            g.drawHorizontalLine (eqBandNames[(size_t) b].getBottom(), (float) eqBandNames[(size_t) b].getX() + 8.0f, (float) eqBandNames[(size_t) b].getRight() - 8.0f);
        }
    }
    else
    {
        drawPanel (g, pageDisplayPanel, {}, "RESONANCE TAMER - WHAT IT'S DOING");
        drawPanel (g, pagePanel, "2", "RESONANCE TAMER");
        const bool ms = masterProcessor.isTamerMidSide();
        rowName (tamerMidName, ms ? "MID" : "STEREO", ms ? "the middle" : "the whole mix", midColour, true);
        rowName (tamerSideName, "SIDE", ms ? "the width" : "only in\nMID + SIDE", sideColour, ms);
    }
}

void EasyMasterAudioProcessorEditor::layoutBody (juce::Rectangle<int> area)
{
    constexpr int gap = 14;
    const auto preset = presetBrowser.getBounds();
    initAllButton.setBounds (preset.getX() - 10 - 52, preset.getCentreY() - 13, 52, 26);
    bypassButton.setBounds (bypassButton.getBounds().withX (initAllButton.getX() - 34 - bypassButton.getWidth()));

    constexpr int nameHeight = 20, spacing = 4, valueBoxHeight = 24;
    const auto placeKnob = [&] (Knob& knob, juce::Rectangle<int> column, int knobSize)
    {
        auto c = column.withSizeKeepingCentre (column.getWidth(), nameHeight + spacing + knobSize + valueBoxHeight);
        knob.label.setBounds (c.removeFromTop (nameHeight));
        c.removeFromTop (spacing);
        knob.slider.setBounds (c.withSizeKeepingCentre (juce::jmin (column.getWidth(), knobSize + 20), c.getHeight()));
    };
    const auto knobSizeFor = [&] (juce::Rectangle<int> column, int most)
    {
        return juce::jmin (most, column.getWidth() - 24, column.getHeight() - nameHeight - spacing - valueBoxHeight);
    };

    chainArea = area.removeFromTop (40);
    area.removeFromTop (gap);

    // Left: INPUT. Right: METERS, LIMITER, COMPARE (always). Middle: the page.
    inputPanel = area.removeFromLeft (150);
    area.removeFromLeft (gap);
    auto right = area.removeFromRight (400);
    area.removeFromRight (gap);
    metersPanel = right.removeFromTop (290);
    right.removeFromTop (gap);
    limiterPanel = right.removeFromTop (184);
    right.removeFromTop (gap);
    comparePanel = right;

    pageDisplayPanel = area.removeFromTop ((area.getHeight() - gap) * 46 / 100);
    area.removeFromTop (gap);
    pagePanel = area;

    meters.setBounds (metersPanel.reduced (14, 8).withTrimmedTop (22));
    display.setBounds (pageDisplayPanel.reduced (12, 12).withTrimmedTop (22));
    tamerDisplay.setBounds (display.getBounds());

    {
        auto inside = inputPanel.reduced (10, 12).withTrimmedTop (26);
        inputWarning.setBounds (inside.removeFromBottom (48));
        placeKnob (knobs[trimKnob], inside, knobSizeFor (inside, 90));
    }
    {
        // LIMITER: GAIN, CEILING, RELEASE (AUTO / SET and its time)
        auto inside = limiterPanel.reduced (12, 10).withTrimmedTop (24);
        const int w = inside.getWidth() / 3;
        auto gainColumn = inside.removeFromLeft (w), ceilingColumn = inside.removeFromLeft (w), releaseColumn = inside;
        placeKnob (knobs[gainKnob], gainColumn, knobSizeFor (gainColumn, 84));
        placeKnob (knobs[ceilingKnob], ceilingColumn, knobSizeFor (ceilingColumn, 64));
        releaseSwitch.setBounds (releaseColumn.removeFromTop (28).withSizeKeepingCentre (juce::jmin (releaseColumn.getWidth(), 120), 26));
        placeKnob (knobs[releaseKnob], releaseColumn, knobSizeFor (releaseColumn, 52));
    }
    {
        auto inside = comparePanel.reduced (14, 10).withTrimmedTop (26);
        auto top = inside.removeFromTop (32);
        matchButton.setBounds (top.removeFromLeft (110).withSizeKeepingCentre (110, 30));
        top.removeFromLeft (8);
        matchInfo.setBounds (top);
        inside.removeFromTop (8);
        listenLabel.setBounds (inside.removeFromTop (16));
        listenSwitch.setBounds (inside.removeFromTop (28));
        inside.removeFromTop (8);
        auto ab = inside.removeFromTop (28);
        abLabel.setBounds (ab.removeFromLeft (40));
        slotA.setBounds (ab.removeFromLeft (56));
        ab.removeFromLeft (8);
        slotB.setBounds (ab.removeFromLeft (56));
        ab.removeFromLeft (8);
        copyButton.setBounds (ab);
    }

    // The COMPRESSOR page
    {
        auto inside = pagePanel.reduced (14, 12).withTrimmedTop (28);
        auto row = inside.removeFromTop (32);
        compOnButton.setBounds (row.removeFromLeft (64).withSizeKeepingCentre (64, 30));
        row.removeFromLeft (12);
        modeSwitch.setBounds (row.removeFromLeft (220).withSizeKeepingCentre (220, 30));
        row.removeFromLeft (16);
        characterSwitch.setBounds (row.removeFromRight (196).withSizeKeepingCentre (196, 30));
        row.removeFromRight (12);
        reactsSwitch.setBounds (row.removeFromRight (196).withSizeKeepingCentre (196, 30));
        inside.removeFromTop (6);

        auto shared = inside.removeFromRight (inside.getWidth() / 7);
        const int rowHeight = inside.getHeight() / 2;
        auto midRow = inside.removeFromTop (rowHeight), sideRow = inside;
        midRowName = midRow.removeFromLeft (84);
        sideRowName = sideRow.removeFromLeft (84);
        const int w = midRow.getWidth() / 5;
        for (int c = 0; c < 5; ++c)
        {
            placeKnob (knobs[(size_t) (midThresholdKnob + c)], juce::Rectangle<int> (midRow.getX() + c * w, midRow.getY(), w, midRow.getHeight()),
                       knobSizeFor (juce::Rectangle<int> (0, 0, w, midRow.getHeight()), 64));
            placeKnob (knobs[(size_t) (sideThresholdKnob + c)], juce::Rectangle<int> (sideRow.getX() + c * w, sideRow.getY(), w, sideRow.getHeight()),
                       knobSizeFor (juce::Rectangle<int> (0, 0, w, sideRow.getHeight()), 64));
        }
        placeKnob (knobs[mixKnob], shared.removeFromTop (rowHeight), knobSizeFor (juce::Rectangle<int> (0, 0, shared.getWidth(), rowHeight), 60));
        placeKnob (knobs[ignoreKnob], shared, knobSizeFor (shared, 60));
    }

    // The MULTIBAND page: per row, the name, then four bands (THRESHOLD, RATIO, SPEED, GAIN); each band's ON and SOLO over it
    mbDisplay.setBounds (display.getBounds());
    {
        auto inside = pagePanel.reduced (14, 12).withTrimmedTop (28);
        auto row = inside.removeFromTop (32);
        mbOnButton.setBounds (row.removeFromLeft (64).withSizeKeepingCentre (64, 30));
        row.removeFromLeft (12);
        mbModeSwitch.setBounds (row.removeFromLeft (220).withSizeKeepingCentre (220, 30));
        for (int sp = 2; sp >= 0; --sp)
        {
            mbSplits[(size_t) sp].setBounds (row.removeFromRight (96).withSizeKeepingCentre (96, 26));
            row.removeFromRight (6);
        }
        mbSplitLabel.setBounds (row.removeFromRight (50));
        inside.removeFromTop (6);
        auto names = inside.removeFromTop (26);
        names.removeFromLeft (84);
        const int bandWidth = names.getWidth() / 4;
        for (int b = 0; b < 4; ++b)
        {
            auto n = b < 3 ? names.removeFromLeft (bandWidth) : names;
            mbBandNames[(size_t) b] = n;
            auto buttons = n.withTrimmedRight (8).removeFromRight (136);
            mbSolo[(size_t) b].setBounds (buttons.removeFromRight (76).withSizeKeepingCentre (76, 22));
            buttons.removeFromRight (4);
            mbBandOn[(size_t) b].setBounds (buttons.removeFromRight (56).withSizeKeepingCentre (56, 22));
        }
        const int rowHeight = inside.getHeight() / 2;
        auto rows = std::array<juce::Rectangle<int>, 2> { inside.removeFromTop (rowHeight), inside };
        mbMidName = rows[0].removeFromLeft (84);
        mbSideName = rows[1].removeFromLeft (84);
        for (int part = 0; part < 2; ++part)
        {
            auto r = rows[(size_t) part];
            const int w = r.getWidth() / 16;
            const int size = juce::jmin (42, w - 18, r.getHeight() - nameHeight - spacing - valueBoxHeight);   // (the same size for all of them)
            for (int c = 0; c < 16; ++c)
            {
                auto column = c == 15 ? r : r.removeFromLeft (w);
                if (c % 4 == 3) column = column.withTrimmedRight (6);   // (a little space between bands)
                placeKnob (mbKnobs[(size_t) part][(size_t) c], column, size);
            }
        }
    }

    // The EQ page: per row, the name, then five bands (FREQ + GAIN) and LOW CUT
    eqDisplay.setBounds (display.getBounds());
    {
        auto inside = pagePanel.reduced (14, 12).withTrimmedTop (28);
        auto row = inside.removeFromTop (32);
        eqOnButton.setBounds (row.removeFromLeft (64).withSizeKeepingCentre (64, 30));
        row.removeFromLeft (12);
        eqModeSwitch.setBounds (row.removeFromLeft (220).withSizeKeepingCentre (220, 30));
        inside.removeFromTop (4);
        auto names = inside.removeFromTop (18);
        names.removeFromLeft (84);
        const int pairWidth = (names.getWidth() * 2) / 11;
        for (int b = 0; b < 6; ++b)
            eqBandNames[(size_t) b] = b < 5 ? names.removeFromLeft (pairWidth) : names;
        const int rowHeight = inside.getHeight() / 2;
        auto rows = std::array<juce::Rectangle<int>, 2> { inside.removeFromTop (rowHeight), inside };
        eqMidName = rows[0].removeFromLeft (84);
        eqSideName = rows[1].removeFromLeft (84);
        for (int part = 0; part < 2; ++part)
        {
            auto r = rows[(size_t) part];
            const int w = r.getWidth() / 11;
            for (int c = 0; c < 11; ++c)
            {
                auto column = c == 10 ? r : r.removeFromLeft (w);
                placeKnob (eqKnobs[(size_t) part][(size_t) c], column, knobSizeFor (column, 50));
            }
        }
    }

    // The RESONANCE TAMER page
    {
        auto inside = pagePanel.reduced (14, 12).withTrimmedTop (28);
        auto row = inside.removeFromTop (32);
        tamerOnButton.setBounds (row.removeFromLeft (64).withSizeKeepingCentre (64, 30));
        row.removeFromLeft (12);
        tamerModeSwitch.setBounds (row.removeFromLeft (220).withSizeKeepingCentre (220, 30));
        tamerListenButton.setBounds (row.removeFromRight (210).withSizeKeepingCentre (210, 30));
        inside.removeFromTop (6);

        auto shared = inside.removeFromRight (inside.getWidth() * 3 / 10);
        const int rowHeight = inside.getHeight() / 2;
        auto midRow = inside.removeFromTop (rowHeight), sideRow = inside;
        tamerMidName = midRow.removeFromLeft (84);
        tamerSideName = sideRow.removeFromLeft (84);
        const int w = midRow.getWidth() / 3;
        for (int c = 0; c < 3; ++c)
        {
            placeKnob (knobs[(size_t) (tMidAmountKnob + c)], juce::Rectangle<int> (midRow.getX() + c * w, midRow.getY(), w, midRow.getHeight()),
                       knobSizeFor (juce::Rectangle<int> (0, 0, w, midRow.getHeight()), 66));
            placeKnob (knobs[(size_t) (tSideAmountKnob + c)], juce::Rectangle<int> (sideRow.getX() + c * w, sideRow.getY(), w, sideRow.getHeight()),
                       knobSizeFor (juce::Rectangle<int> (0, 0, w, sideRow.getHeight()), 66));
        }
        // SPEED (for both) on top; FROM and TO under it
        auto speedArea = shared.removeFromTop (rowHeight);
        placeKnob (knobs[tSpeedKnob], speedArea, knobSizeFor (speedArea, 60));
        auto fromArea = shared.removeFromLeft (shared.getWidth() / 2);
        placeKnob (knobs[tFromKnob], fromArea, knobSizeFor (fromArea, 56));
        placeKnob (knobs[tToKnob], shared, knobSizeFor (shared, 56));
    }
    for (int m : { (int) MasterModules::tamer, (int) MasterModules::eq, (int) MasterModules::comp, (int) MasterModules::multiband })
        placeModuleButtons (m, pagePanel);
    placeModuleButtons (MasterModules::limiter, limiterPanel);
    showParts();
}

void EasyMasterAudioProcessorEditor::refreshLiveDisplays()
{
    const int count = masterProcessor.takeMoments (incoming.data(), (int) incoming.size());
    display.addMoments (incoming.data(), count);

    const auto& params = easyProcessor.parameters;
    const bool ms = masterProcessor.isMidSide();
    const bool compOn = params.getRawParameterValue (MasterIDs::compOn)->load() > 0.5f;
    const bool tms = masterProcessor.isTamerMidSide();
    const bool tamerOn = params.getRawParameterValue (MasterIDs::tamerOn)->load() > 0.5f;
    const bool bypassed = params.getRawParameterValue (CommonIDs::bypass)->load() > 0.5f;
    display.setState (params.getRawParameterValue (MasterIDs::ceiling)->load(), ms, compOn, bypassed);
    const auto readings = masterProcessor.getReadings();
    meters.setReadings (readings, ms, compOn, tamerOn);
    const bool ems = masterProcessor.isEQMidSide(), eon = params.getRawParameterValue (MasterEQIDs::on)->load() > 0.5f;
    const bool mms = masterProcessor.isMultibandMidSide(), mon = params.getRawParameterValue (MasterMBIDs::on)->load() > 0.5f;
    if (shownPage == mbPage)
        mbDisplay.setState (masterProcessor.multibandSettings(), masterProcessor.takeMultibandPicture(), masterProcessor.getTamerPicture(), mon, bypassed);
    if (shownPage == eqPage)
        eqDisplay.setState (masterProcessor.eqSettings(), masterProcessor.getTamerPicture(), eon, bypassed, masterProcessor.getSampleRate() > 0 ? masterProcessor.getSampleRate() : 48000.0);
    if (shownPage == tamerPage)
        tamerDisplay.setPicture (masterProcessor.getTamerPicture(), tms, tamerOn, params.getRawParameterValue (MasterIDs::tFrom)->load(),
                                 params.getRawParameterValue (MasterIDs::tTo)->load(), bypassed);

    if (ms != shownMidSide || compOn != shownCompOn || tms != shownTamerMidSide || tamerOn != shownTamerOn || ems != shownEQMidSide || eon != shownEQOn || mms != shownMBMidSide || mon != shownMBOn)
    {
        shownMBMidSide = mms; shownMBOn = mon;
        shownMidSide = ms; shownCompOn = compOn; shownTamerMidSide = tms; shownTamerOn = tamerOn; shownEQMidSide = ems; shownEQOn = eon;
        showParts();
    }

    const bool set = juce::roundToInt (params.getRawParameterValue (MasterIDs::releaseMode)->load()) == 1;
    knobs[releaseKnob].slider.setEnabled (set);
    knobs[releaseKnob].slider.setAlpha (set ? 1.0f : 0.4f);
    knobs[releaseKnob].label.setAlpha (set ? 1.0f : 0.4f);

    const bool matching = params.getRawParameterValue (MasterIDs::match)->load() > 0.5f;
    matchInfo.setText (matching ? "Matched: " + Format::signedDecibels (readings.matchDb) + " - now BYPASS on and off"
                                : "Hear it at the original's loudness, then BYPASS on and off", juce::dontSendNotification);

    const bool clipping = masterProcessor.getInputPeakDb() > -0.01f;
    inputWarning.setText (clipping ? "The mix is CLIPPING on the way in" : "Input OK", juce::dontSendNotification);
    inputWarning.setColour (juce::Label::textColourId, clipping ? Palette::meterHigh : Palette::textDim);

    const int slot = easyProcessor.getActiveSlot();
    slotA.setToggleState (slot == 0, juce::dontSendNotification);
    slotB.setToggleState (slot == 1, juce::dontSendNotification);
    copyButton.setButtonText (slot == 0 ? "COPY A > B" : "COPY B > A");
}
