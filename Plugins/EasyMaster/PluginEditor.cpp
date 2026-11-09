#include "PluginEditor.h"
#include "MasterConsole.h"
#include "Parameters.h"

#include "MasterHints.h"
using namespace MasterHints;

namespace
{
    constexpr float topDb = 2.0f, bottomDb = -30.0f, cutScaleDb = 12.0f;
    const juce::Colour midColour = MasterSkin::meterLow;
    const juce::Colour sideColour = MasterSkin::meterLow.interpolatedWith (juce::Colours::deepskyblue, 0.7f);

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
    g.setColour (MasterSkin::background);
    g.fillRoundedRectangle (area, 6.0f);

    auto plot = area.reduced (14.0f, 12.0f).withTrimmedLeft (34.0f).withTrimmedBottom (40.0f);
    // (two lanes: how much is being turned down on top, the levels underneath)
    const auto cutLane = plot.removeFromTop (plot.getHeight() * 0.3f);
    plot.removeFromTop (8.0f);
    const auto yOf = [&] (float db) { return plot.getY() + plot.getHeight() * (topDb - juce::jlimit (bottomDb, topDb, db)) / (topDb - bottomDb); };
    const auto xOf = [&] (int age) { return plot.getRight() - (float) age / (float) (numMoments - 1) * plot.getWidth(); };
    const auto cutY = [&] (float db) { return cutLane.getY() + cutLane.getHeight() * juce::jlimit (0.0f, 1.0f, db / cutScaleDb); };

    g.setFont (MasterSkin::font (11.0f));
    for (float db : { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f })
    {
        g.setColour (MasterSkin::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (yOf (db)), plot.getX(), plot.getRight());
        g.setColour (MasterSkin::textDim);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 36.0f, yOf (db) - 7.0f, 32.0f, 14.0f), juce::Justification::centredRight, false);
    }
    g.setColour (MasterSkin::panel.withAlpha (0.6f));
    g.fillRect (cutLane);
    for (float db : { 0.0f, 6.0f, 12.0f })
    {
        g.setColour (MasterSkin::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (cutY (db)), cutLane.getX(), cutLane.getRight());
        g.setColour (MasterSkin::textDim);
        g.drawText (db < 0.5f ? juce::String ("0") : "-" + juce::String ((int) db), juce::Rectangle<float> (cutLane.getX() - 36.0f, cutY (db) - 7.0f, 32.0f, 14.0f),
                    juce::Justification::centredRight, false);
    }
    const auto laneName = [&g] (juce::Rectangle<float> lane, const juce::String& name)
    {
        const auto tag = juce::Rectangle<float> (lane.getX() + 4.0f, lane.getBottom() - 18.0f, (float) name.length() * 7.2f + 12.0f, 15.0f);
        g.setColour (MasterSkin::background.withAlpha (0.85f));
        g.fillRoundedRectangle (tag, 3.0f);
        g.setColour (MasterSkin::textDim);
        g.setFont (MasterSkin::font (10.5f, true));
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
        g.setColour (MasterSkin::textDim.withAlpha (0.3f));
        g.fillPath (in);
        g.setColour (MasterSkin::accent.withAlpha (0.16f));
        g.fillPath (out);
        g.setColour (MasterSkin::accent);
        g.strokePath (outLine, juce::PathStrokeType (1.4f));
    }
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (cutLane.toNearestInt());
        g.setColour (MasterSkin::meterHigh.withAlpha (0.5f));
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
        g.setColour (MasterSkin::textBright);
        g.fillPath (dashed);
        g.setFont (MasterSkin::font (12.0f, true));
        g.drawText ("CEILING " + juce::String (ceilingDb, 1) + " dBTP", juce::Rectangle<float> (plot.getRight() - 200.0f, y + 3.0f, 196.0f, 14.0f),
                    juce::Justification::centredRight, false);
    }

    laneName (cutLane, "TURNING DOWN");
    laneName (plot, "LEVEL");

    // Key
    auto key = juce::Rectangle<float> (plot.getX(), plot.getBottom() + 20.0f, plot.getWidth(), 16.0f);
    g.setFont (MasterSkin::font (12.0f));
    const auto item = [&] (juce::Colour c, const juce::String& t, float w)
    {
        g.setColour (c);
        g.fillRect (key.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, 10.0f));
        key.removeFromLeft (6.0f);
        g.setColour (MasterSkin::textDim);
        g.drawText (t, key.removeFromLeft (w), juce::Justification::centredLeft, false);
        key.removeFromLeft (12.0f);
    };
    item (MasterSkin::textDim.withAlpha (0.6f), "into the limiter", 100.0f);
    item (MasterSkin::accent, "out", 30.0f);
    item (MasterSkin::meterHigh.withAlpha (0.7f), "limiter turning down", 130.0f);
    item (midColour, midSide ? "compressor: MID" : "compressor", midSide ? 116.0f : 76.0f);
    if (midSide)
        item (sideColour, "compressor: SIDE", 116.0f);

    if (bypassed)
    {
        g.setColour (MasterSkin::background.withAlpha (0.65f));
        g.fillRect (area);
        const auto badge = juce::Rectangle<float> (230.0f, 58.0f).withCentre (area.getCentre());
        g.setColour (MasterSkin::panel);
        g.fillRoundedRectangle (badge, 8.0f);
        g.setColour (MasterSkin::meterHigh);
        g.drawRoundedRectangle (badge.reduced (0.75f), 8.0f, 1.5f);
        auto lines = badge.reduced (10.0f, 9.0f);
        g.setFont (MasterSkin::font (17.0f, true));
        g.drawText ("BYPASSED", lines.removeFromTop (lines.getHeight() * 0.55f), juce::Justification::centred, false);
        g.setColour (MasterSkin::textDim);
        g.setFont (MasterSkin::font (12.5f));
        g.drawText ("You're hearing the original mix", lines, juce::Justification::centred, false);
    }
}

//==============================================================================
namespace
{
    void paintBypassedBadge (juce::Graphics& g, juce::Rectangle<float> area)
    {
        g.setColour (MasterSkin::background.withAlpha (0.65f));
        g.fillRect (area);
        const auto badge = juce::Rectangle<float> (230.0f, 58.0f).withCentre (area.getCentre());
        g.setColour (MasterSkin::panel);
        g.fillRoundedRectangle (badge, 8.0f);
        g.setColour (MasterSkin::meterHigh);
        g.drawRoundedRectangle (badge.reduced (0.75f), 8.0f, 1.5f);
        auto lines = badge.reduced (10.0f, 9.0f);
        g.setFont (MasterSkin::font (17.0f, true));
        g.drawText ("BYPASSED", lines.removeFromTop (lines.getHeight() * 0.55f), juce::Justification::centred, false);
        g.setColour (MasterSkin::textDim);
        g.setFont (MasterSkin::font (12.5f));
        g.drawText ("You're hearing the original mix", lines, juce::Justification::centred, false);
    }

    void paintOffNote (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text)
    {
        const auto box = juce::Rectangle<float> (juce::jmin (area.getWidth() - 20.0f, 420.0f), 30.0f).withCentre (area.getCentre());
        g.setColour (MasterSkin::background.withAlpha (0.85f));
        g.fillRoundedRectangle (box, 6.0f);
        g.setColour (MasterSkin::textDim);
        g.setFont (MasterSkin::font (13.0f));
        g.drawText (text, box, juce::Justification::centred, false);
    }
}

PunchDisplay::PunchDisplay() : history ((size_t) MasterDisplay::numMoments) {}

void PunchDisplay::addMoments (const EasyMasterAudioProcessor::Moment* m, int count)
{
    if (count <= 0) return;
    for (int i = 0; i < count; ++i)
    {
        history[(size_t) next] = m[i];
        next = (next + 1) % MasterDisplay::numMoments;
    }
    if (isVisible()) repaint();
}

void PunchDisplay::setState (bool isOn, bool isBypassed)
{
    if (isOn != on || isBypassed != bypassed) { on = isOn; bypassed = isBypassed; repaint(); }
}

void PunchDisplay::paint (juce::Graphics& g)
{
    const int numMoments = MasterDisplay::numMoments;
    const auto area = getLocalBounds().toFloat();
    g.setColour (MasterSkin::background);
    g.fillRoundedRectangle (area, 6.0f);

    auto plot = area.reduced (14.0f, 12.0f).withTrimmedLeft (34.0f).withTrimmedBottom (40.0f);
    const auto levelLane = plot.removeFromTop (plot.getHeight() * 0.4f);
    plot.removeFromTop (10.0f);
    const auto& shapeLane = plot;
    constexpr float shapeScaleDb = 9.0f;
    const auto xOf = [&] (int age) { return plot.getRight() - (float) age / (float) (numMoments - 1) * plot.getWidth(); };
    const auto levelY = [&] (float db) { return levelLane.getY() + levelLane.getHeight() * (topDb - juce::jlimit (bottomDb, topDb, db)) / (topDb - bottomDb); };
    const auto shapeY = [&] (float db) { return shapeLane.getCentreY() - shapeLane.getHeight() * 0.5f * juce::jlimit (-1.0f, 1.0f, db / shapeScaleDb); };

    g.setFont (MasterSkin::font (11.0f));
    for (float db : { 0.0f, -12.0f, -24.0f })
    {
        g.setColour (MasterSkin::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (levelY (db)), levelLane.getX(), levelLane.getRight());
        g.setColour (MasterSkin::textDim);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (levelLane.getX() - 36.0f, levelY (db) - 7.0f, 32.0f, 14.0f), juce::Justification::centredRight, false);
    }
    g.setColour (MasterSkin::panel.withAlpha (0.6f));
    g.fillRect (shapeLane);
    for (float db : { -6.0f, 0.0f, 6.0f })
    {
        g.setColour (std::abs (db) < 0.5f ? MasterSkin::textDim.withAlpha (0.6f) : MasterSkin::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (shapeY (db)), shapeLane.getX(), shapeLane.getRight());
        g.setColour (MasterSkin::textDim);
        g.drawText (db > 0.0f ? "+" + juce::String ((int) db) : juce::String ((int) db), juce::Rectangle<float> (shapeLane.getX() - 36.0f, shapeY (db) - 7.0f, 32.0f, 14.0f),
                    juce::Justification::centredRight, false);
    }

    juce::Path level, levelLine, up, down;
    level.startNewSubPath (levelLane.getBottomRight());
    up.startNewSubPath (shapeLane.getRight(), shapeLane.getCentreY());
    down.startNewSubPath (shapeLane.getRight(), shapeLane.getCentreY());
    for (int age = 0; age < numMoments; ++age)
    {
        const auto& m = history[(size_t) ((next - 1 - age + numMoments * 2) % numMoments)];
        const float x = xOf (age);
        level.lineTo (x, levelY (m.outDb));
        if (age == 0) levelLine.startNewSubPath (x, levelY (m.outDb)); else levelLine.lineTo (x, levelY (m.outDb));
        up.lineTo (x, shapeY (m.punchUpDb));
        down.lineTo (x, shapeY (-m.punchDownDb));
    }
    level.lineTo (levelLane.getBottomLeft()); level.closeSubPath();
    up.lineTo (shapeLane.getX(), shapeLane.getCentreY()); up.closeSubPath();
    down.lineTo (shapeLane.getX(), shapeLane.getCentreY()); down.closeSubPath();
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (levelLane.toNearestInt());
        g.setColour (MasterSkin::accent.withAlpha (0.16f));
        g.fillPath (level);
        g.setColour (MasterSkin::accent);
        g.strokePath (levelLine, juce::PathStrokeType (1.4f));
    }
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (shapeLane.toNearestInt());
        g.setColour (midColour.withAlpha (0.75f));
        g.fillPath (up);
        g.setColour (sideColour.withAlpha (0.75f));
        g.fillPath (down);
    }

    const auto laneName = [&g] (juce::Rectangle<float> lane, const juce::String& name)
    {
        const auto tag = juce::Rectangle<float> (lane.getX() + 4.0f, lane.getY() + 3.0f, (float) name.length() * 7.2f + 12.0f, 15.0f);
        g.setColour (MasterSkin::background.withAlpha (0.85f));
        g.fillRoundedRectangle (tag, 3.0f);
        g.setColour (MasterSkin::textDim);
        g.setFont (MasterSkin::font (10.5f, true));
        g.drawText (name, tag, juce::Justification::centred, false);
    };
    laneName (levelLane, "LEVEL OUT");
    laneName (shapeLane, "PUNCH: TURNING UP / DOWN");

    g.setColour (MasterSkin::textDim);
    g.setFont (MasterSkin::font (11.0f));
    g.drawText ("6 seconds ago", juce::Rectangle<float> (plot.getX(), plot.getBottom() + 3.0f, 120.0f, 14.0f), juce::Justification::centredLeft, false);
    g.drawText ("now", juce::Rectangle<float> (plot.getRight() - 60.0f, plot.getBottom() + 3.0f, 60.0f, 14.0f), juce::Justification::centredRight, false);

    auto key = juce::Rectangle<float> (plot.getX(), plot.getBottom() + 20.0f, plot.getWidth(), 16.0f);
    g.setFont (MasterSkin::font (12.0f));
    const auto item = [&] (juce::Colour c, const juce::String& t, float w)
    {
        g.setColour (c);
        g.fillRect (key.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, 10.0f));
        key.removeFromLeft (6.0f);
        g.setColour (MasterSkin::textDim);
        g.drawText (t, key.removeFromLeft (w), juce::Justification::centredLeft, false);
        key.removeFromLeft (12.0f);
    };
    item (MasterSkin::accent, "level out", 64.0f);
    item (midColour.withAlpha (0.8f), "turned up (more snap, longer tails)", 220.0f);
    item (sideColour.withAlpha (0.8f), "turned down (softer, tighter)", 190.0f);

    if (! on && ! bypassed) paintOffNote (g, shapeLane, "PUNCH is off - switch it ON to shape the hits");
    if (bypassed) paintBypassedBadge (g, area);
}

//==============================================================================
void WidthDisplay::addPoints (const float* left, const float* right, int count)
{
    for (int i = 0; i < count; ++i)
    {
        pointL[(size_t) next] = left[i];
        pointR[(size_t) next] = right[i];
        next = (next + 1) % numPoints;
    }
    if (count > 0 && isVisible()) repaint();
}

void WidthDisplay::setState (const StereoWidth::Settings& s, float sideToMidDb, float corr, bool isOn, bool isBypassed, double sampleRate)
{
    settings = s; sideToMid = sideToMidDb; correlation = corr; on = isOn; bypassed = isBypassed; rate = sampleRate;
    if (isVisible()) repaint();
}

void WidthDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (MasterSkin::background);
    g.fillRoundedRectangle (area, 6.0f);

    auto inside = area.reduced (14.0f, 12.0f);
    // Left: the stereo picture (a square)
    const float side = juce::jmin (inside.getHeight(), inside.getWidth() * 0.45f);
    const auto scope = inside.removeFromLeft (side).withSizeKeepingCentre (side, side);
    inside.removeFromLeft (24.0f);
    {
        g.setColour (MasterSkin::panel.withAlpha (0.6f));
        g.fillEllipse (scope.reduced (2.0f));
        g.setColour (MasterSkin::panelEdge);
        g.drawEllipse (scope.reduced (2.0f), 1.0f);
        const auto c = scope.getCentre();
        const float r = scope.getWidth() * 0.5f - 4.0f;
        g.drawLine (c.x, scope.getY() + 4.0f, c.x, scope.getBottom() - 4.0f, 1.0f);
        g.drawLine (scope.getX() + 4.0f, c.y, scope.getRight() - 4.0f, c.y, 1.0f);
        // (the L and R diagonals)
        g.setColour (MasterSkin::panelEdge.withAlpha (0.6f));
        g.drawLine (c.x - r * 0.707f, c.y - r * 0.707f, c.x + r * 0.707f, c.y + r * 0.707f, 1.0f);
        g.drawLine (c.x + r * 0.707f, c.y - r * 0.707f, c.x - r * 0.707f, c.y + r * 0.707f, 1.0f);
        g.setColour (MasterSkin::textDim);
        g.setFont (MasterSkin::font (10.5f, true));
        g.drawText ("MID", juce::Rectangle<float> (c.x - 20.0f, scope.getY() + 6.0f, 40.0f, 13.0f), juce::Justification::centred, false);
        g.drawText ("SIDE", juce::Rectangle<float> (scope.getRight() - 40.0f, c.y - 15.0f, 36.0f, 13.0f), juce::Justification::centredRight, false);
        g.drawText ("L", juce::Rectangle<float> (c.x - r * 0.707f - 14.0f, c.y - r * 0.707f - 14.0f, 14.0f, 13.0f), juce::Justification::centred, false);
        g.drawText ("R", juce::Rectangle<float> (c.x + r * 0.707f, c.y - r * 0.707f - 14.0f, 14.0f, 13.0f), juce::Justification::centred, false);

        // (scaled to the loudest point, following slowly, so the shape - not the level - is what you see)
        float peak = 1.0e-4f;
        for (int i = 0; i < numPoints; ++i)
            peak = juce::jmax (peak, std::abs (pointL[(size_t) i] + pointR[(size_t) i]), std::abs (pointL[(size_t) i] - pointR[(size_t) i]));
        const float wanted = 0.70710678f / peak;
        zoom = wanted < zoom ? wanted : zoom + (wanted - zoom) * 0.08f;
        const float k = r * zoom * 0.70710678f;
        g.setColour ((on ? sideColour : MasterSkin::accent).withAlpha (0.35f));
        for (int i = 0; i < numPoints; ++i)
        {
            const float l = pointL[(size_t) i], rr = pointR[(size_t) i];
            const float x = c.x + (l - rr) * k, y = c.y - (l + rr) * k;
            g.fillRect (x - 0.75f, y - 0.75f, 1.5f, 1.5f);
        }
    }

    // Right: the readings, then what WIDTH and MONO BASS do to the sides
    auto readings = inside.removeFromTop (juce::jmin (120.0f, inside.getHeight() * 0.42f));
    const auto bar = [&g] (juce::Rectangle<float> row, const juce::String& name, const juce::String& value, float position, float centre, juce::Colour c)
    {
        g.setColour (MasterSkin::textDim);
        g.setFont (MasterSkin::font (12.0f, true));
        auto top = row.removeFromTop (16.0f);
        g.drawText (name, top, juce::Justification::centredLeft, false);
        g.setColour (MasterSkin::textBright);
        g.drawText (value, top, juce::Justification::centredRight, false);
        const auto track = row.removeFromTop (12.0f).reduced (0.0f, 1.0f);
        g.setColour (MasterSkin::panel);
        g.fillRoundedRectangle (track, 3.0f);
        const float x0 = track.getX() + track.getWidth() * centre, x1 = track.getX() + track.getWidth() * juce::jlimit (0.0f, 1.0f, position);
        g.setColour (c);
        g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (x0, x1), track.getY(), std::abs (x1 - x0) + 2.0f, track.getHeight()), 3.0f);
    };
    const float sm = juce::jlimit (-30.0f, 0.0f, sideToMid);
    bar (readings.removeFromTop (34.0f), "SIDES NEXT TO THE MIDDLE", sideToMid <= -99.0f ? juce::String ("--") : juce::String (sideToMid, 1) + " dB",
         (sm + 30.0f) / 30.0f, 0.0f, sideColour);
    readings.removeFromTop (6.0f);
    bar (readings.removeFromTop (34.0f), "CORRELATION", juce::String (correlation, 2), (correlation + 1.0f) * 0.5f, 0.5f,
         correlation < 0.0f ? MasterSkin::meterHigh : (correlation < 0.3f ? MasterSkin::meterMid : MasterSkin::meterLow));
    readings.removeFromTop (4.0f);
    g.setColour (MasterSkin::textDim);
    g.setFont (MasterSkin::font (11.0f));
    g.drawFittedText ("Usually -6 to -15 dB, correlation above 0. Below 0 = parts will vanish in mono.", readings.toNearestInt(), juce::Justification::topLeft, 2);

    inside.removeFromTop (8.0f);
    auto curve = inside.withTrimmedBottom (16.0f).withTrimmedLeft (30.0f);
    g.setColour (MasterSkin::panel.withAlpha (0.6f));
    g.fillRect (curve);
    const auto xOf = [&] (double hz) { return curve.getX() + curve.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0)); };
    const auto yOf = [&] (float pct) { return curve.getBottom() - curve.getHeight() * juce::jlimit (0.0f, 1.0f, pct / 200.0f); };
    g.setFont (MasterSkin::font (10.5f));
    for (float pct : { 0.0f, 100.0f, 200.0f })
    {
        g.setColour (std::abs (pct - 100.0f) < 0.5f ? MasterSkin::textDim.withAlpha (0.6f) : MasterSkin::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (yOf (pct)), curve.getX(), curve.getRight());
        g.setColour (MasterSkin::textDim);
        g.drawText (juce::String ((int) pct) + "%", juce::Rectangle<float> (curve.getX() - 32.0f, yOf (pct) - 7.0f, 30.0f, 14.0f), juce::Justification::centredRight, false);
    }
    for (double hz : { 100.0, 1000.0, 10000.0 })
    {
        g.setColour (MasterSkin::panelEdge);
        g.drawVerticalLine (juce::roundToInt (xOf (hz)), curve.getY(), curve.getBottom());
        g.setColour (MasterSkin::textDim);
        g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz), juce::Rectangle<float> (xOf (hz) - 20.0f, curve.getBottom() + 2.0f, 40.0f, 13.0f),
                    juce::Justification::centred, false);
    }
    juce::Path p;
    auto shown = settings;
    shown.bypassed = shown.bypassed || ! on;
    for (int i = 0; i <= 200; ++i)
    {
        const double hz = 20.0 * std::pow (1000.0, i / 200.0);
        const float y = yOf ((float) (StereoWidth::sideResponseAt (shown, hz, rate) * 100.0));
        if (i == 0) p.startNewSubPath (xOf (hz), y); else p.lineTo (xOf (hz), y);
    }
    g.setColour (on ? sideColour : MasterSkin::textDim);
    g.strokePath (p, juce::PathStrokeType (2.0f));
    g.setColour (MasterSkin::textDim);
    g.setFont (MasterSkin::font (10.5f, true));
    g.drawText ("THE SIDES' LEVEL ACROSS THE RANGE", curve.reduced (6.0f, 4.0f), juce::Justification::topLeft, false);

    if (! on && ! bypassed) paintOffNote (g, curve, "WIDTH is off - the picture shows the mix as it is");
    if (bypassed) paintBypassedBadge (g, area);
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
        g.setColour (MasterSkin::background);
        g.fillRoundedRectangle (box.reduced (3.0f), 5.0f);
        g.setColour (MasterSkin::textDim);
        g.setFont (MasterSkin::font (11.0f, true));
        g.drawText (caption, box.removeFromTop (18.0f).translated (0.0f, 3.0f), juce::Justification::centred, false);
        g.setColour (colour);
        g.setFont (MasterSkin::font (20.0f, true));
        g.drawText (value, box.translated (0.0f, -2.0f), juce::Justification::centred, false);
    };
    bigNumber (lufsRow.removeFromLeft (third), "MOMENTARY", lufsText (r.momentary), MasterSkin::textBright);
    bigNumber (lufsRow.removeFromLeft (third), "SHORT-TERM", lufsText (r.shortTerm), MasterSkin::textBright);
    bigNumber (lufsRow, "INTEGRATED", lufsText (r.integrated), MasterSkin::accent);
    area.removeFromTop (4.0f);

    // True peak and PLR
    auto peakRow = area.removeFromTop (48.0f);
    const float half = peakRow.getWidth() / 2.0f;
    const bool over = r.highestPeak > -0.05f;
    bigNumber (peakRow.removeFromLeft (half), "HIGHEST TRUE PEAK",
               r.highestPeak <= -99.0f ? juce::String ("--") : juce::String (r.highestPeak, 2) + " dB", over ? MasterSkin::meterHigh : MasterSkin::textBright);
    const float plr = r.highestPeak - r.integrated;
    bigNumber (peakRow, "PLR (PEAK TO LOUDNESS)", (r.integrated <= -69.9f || r.highestPeak <= -99.0f) ? juce::String ("--") : juce::String (plr, 1) + " dB",
               MasterSkin::textBright);
    area.removeFromTop (4.0f);

    // Correlation
    {
        auto row = area.removeFromTop (30.0f);
        g.setColour (MasterSkin::textDim);
        g.setFont (MasterSkin::font (11.0f, true));
        g.drawText ("STEREO CORRELATION", row.removeFromTop (14.0f), juce::Justification::centredLeft, false);
        auto bar = row.reduced (0.0f, 3.0f);
        g.setColour (MasterSkin::background);
        g.fillRoundedRectangle (bar, 3.0f);
        const float cx = bar.getCentreX();
        const float x = bar.getX() + bar.getWidth() * (0.5f + 0.5f * juce::jlimit (-1.0f, 1.0f, r.correlation));
        const auto colour = r.correlation < 0.0f ? MasterSkin::meterHigh : (r.correlation < 0.3f ? MasterSkin::meterMid : MasterSkin::meterLow);
        g.setColour (colour.withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (cx, x), bar.getY() + 2.0f, juce::jmax (cx, x), bar.getBottom() - 2.0f));
        g.setColour (MasterSkin::textDim);
        g.drawVerticalLine (juce::roundToInt (cx), bar.getY(), bar.getBottom());
        g.setFont (MasterSkin::font (10.5f));
        g.drawText ("-1", bar.reduced (4.0f, 0.0f), juce::Justification::centredLeft, false);
        g.drawText ("+1", bar.reduced (4.0f, 0.0f), juce::Justification::centredRight, false);
    }
    area.removeFromTop (4.0f);

    // Turning down now
    g.setColour (MasterSkin::textDim);
    g.setFont (MasterSkin::font (11.0f, true));
    g.drawText ("TURNING DOWN NOW", area.removeFromTop (14.0f), juce::Justification::centredLeft, false);
    const auto cutBar = [&] (const juce::String& name, float db, juce::Colour colour, bool active)
    {
        auto row = area.removeFromTop (18.0f).reduced (0.0f, 2.0f);
        g.setColour (active ? MasterSkin::textBright : MasterSkin::textDim.withAlpha (0.5f));
        g.setFont (MasterSkin::font (11.5f));
        g.drawText (name, row.removeFromLeft (110.0f), juce::Justification::centredLeft, false);
        auto value = row.removeFromRight (56.0f);
        g.setColour (MasterSkin::background);
        g.fillRoundedRectangle (row, 3.0f);
        if (active && db > 0.05f)
        {
            g.setColour (colour);
            g.fillRoundedRectangle (row.withWidth (row.getWidth() * juce::jlimit (0.0f, 1.0f, db / 12.0f)), 3.0f);
        }
        g.setColour (active ? MasterSkin::textBright : MasterSkin::textDim.withAlpha (0.5f));
        g.drawText (active ? juce::String (db, 1) + " dB" : juce::String ("off"), value, juce::Justification::centredRight, false);
    };
    cutBar ("TAMER (deepest)", shownTamer, MasterSkin::accent, tamerOn);
    cutBar (midSide ? "COMPRESSOR MID" : "COMPRESSOR", shownMid, midColour, compOn);
    cutBar ("COMPRESSOR SIDE", shownSide, sideColour, compOn && midSide);
    cutBar ("LIMITER", shownLimit, MasterSkin::meterHigh, true);

    g.setColour (MasterSkin::textDim);
    g.setFont (MasterSkin::font (11.0f));
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
    g.setColour (MasterSkin::background);
    g.fillRoundedRectangle (area, 6.0f);
    auto plot = area.reduced (14.0f, 12.0f).withTrimmedLeft (34.0f).withTrimmedBottom (40.0f);
    const auto cutLane = plot.removeFromTop (plot.getHeight() * 0.32f);
    plot.removeFromTop (8.0f);
    const int bands = ResonanceTamer::numBands;
    const float slot = plot.getWidth() / (float) bands;
    constexpr float levelTop = 0.0f, levelRange = 72.0f, cutRange = 12.0f;
    const auto levelY = [&] (float db) { return plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (db - (levelTop - levelRange)) / levelRange); };
    const auto cutH = [&] (float db) { return cutLane.getHeight() * juce::jlimit (0.0f, 1.0f, db / cutRange); };

    g.setFont (MasterSkin::font (11.0f));
    for (float db : { -24.0f, -48.0f })
    {
        g.setColour (MasterSkin::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (levelY (db)), plot.getX(), plot.getRight());
        g.setColour (MasterSkin::textDim);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 36.0f, levelY (db) - 7.0f, 32.0f, 14.0f), juce::Justification::centredRight, false);
    }
    g.setColour (MasterSkin::panel.withAlpha (0.6f));
    g.fillRect (cutLane);
    for (float db : { 0.0f, 6.0f, 12.0f })
    {
        const float y = cutLane.getY() + cutH (db);
        g.setColour (MasterSkin::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (y), cutLane.getX(), cutLane.getRight());
        g.setColour (MasterSkin::textDim);
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
            g.setColour (MasterSkin::accent.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (x + 1.0f, ym, w * 0.5f, plot.getBottom() - ym));
            g.setColour (sideColour.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (x + 1.0f + w * 0.5f, ys, w * 0.5f, plot.getBottom() - ys));
        }
        else
        {
            g.setColour (MasterSkin::accent.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (x + 1.0f, ym, w, plot.getBottom() - ym));
        }
        // Cuts, hanging from the top of their lane
        if (on && ! bypassed)
        {
            const float cm = cutH (picture.cutDb[0][(size_t) b]);
            if (midSide)
            {
                const float cs = cutH (picture.cutDb[1][(size_t) b]);
                g.setColour (MasterSkin::meterHigh.withAlpha (0.8f));
                g.fillRect (juce::Rectangle<float> (x + 1.0f, cutLane.getY(), w * 0.5f, cm));
                g.setColour (sideColour.interpolatedWith (MasterSkin::meterHigh, 0.35f));
                g.fillRect (juce::Rectangle<float> (x + 1.0f + w * 0.5f, cutLane.getY(), w * 0.5f, cs));
            }
            else
            {
                g.setColour (MasterSkin::meterHigh.withAlpha (0.8f));
                g.fillRect (juce::Rectangle<float> (x + 1.0f, cutLane.getY(), w, cm));
            }
        }
        if (! inRange)   // (outside FROM / TO: shaded over)
        {
            g.setColour (MasterSkin::background.withAlpha (0.7f));
            g.fillRect (juce::Rectangle<float> (x, cutLane.getY(), slot, plot.getBottom() - cutLane.getY()));
        }
    }

    // Frequencies along the bottom
    g.setColour (MasterSkin::textDim);
    g.setFont (MasterSkin::font (11.0f));
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
        g.setColour (MasterSkin::background.withAlpha (0.85f));
        g.fillRoundedRectangle (tag, 3.0f);
        g.setColour (MasterSkin::textDim);
        g.setFont (MasterSkin::font (10.5f, true));
        g.drawText (name, tag, juce::Justification::centred, false);
    };
    laneName (cutLane, "TURNING DOWN");
    laneName (plot, "THE MIX NOW");

    auto key = juce::Rectangle<float> (plot.getX(), plot.getBottom() + 20.0f, plot.getWidth(), 16.0f);
    g.setFont (MasterSkin::font (12.0f));
    const auto item = [&] (juce::Colour c, const juce::String& t, float w)
    {
        g.setColour (c);
        g.fillRect (key.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, 10.0f));
        key.removeFromLeft (6.0f);
        g.setColour (MasterSkin::textDim);
        g.drawText (t, key.removeFromLeft (w), juce::Justification::centredLeft, false);
        key.removeFromLeft (12.0f);
    };
    item (MasterSkin::accent, midSide ? "middle" : "the mix", midSide ? 50.0f : 56.0f);
    if (midSide) item (sideColour, "sides", 40.0f);
    item (MasterSkin::meterHigh.withAlpha (0.8f), midSide ? "middle turned down" : "turned down", midSide ? 120.0f : 80.0f);
    if (midSide) item (sideColour.interpolatedWith (MasterSkin::meterHigh, 0.35f), "sides turned down", 120.0f);

    if (bypassed || ! on)
    {
        g.setColour (MasterSkin::background.withAlpha (0.6f));
        g.fillRect (cutLane);
        g.setColour (MasterSkin::textDim);
        g.setFont (MasterSkin::font (14.0f, true));
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
    g.setColour (MasterSkin::background);
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
        g.setColour (MasterSkin::textDim.withAlpha (0.16f));
        g.fillPath (mix);
    }

    g.setFont (MasterSkin::font (11.0f));
    for (float step : { 1.0f, 0.5f, 0.0f, -0.5f, -1.0f })
    {
        const float db = step * range;
        g.setColour (std::abs (db) < 0.01f ? MasterSkin::textDim.withAlpha (0.5f) : MasterSkin::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (yOf (db)), plot.getX(), plot.getRight());
        g.setColour (MasterSkin::textDim);
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 36.0f, yOf (db) - 7.0f, 32.0f, 14.0f), juce::Justification::centredRight, false);
    }
    for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        g.setColour (MasterSkin::panelEdge);
        g.drawVerticalLine (juce::roundToInt (xOf (hz)), plot.getY(), plot.getBottom());
        g.setColour (MasterSkin::textDim);
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
            g.setColour (lit ? colour.withAlpha (0.35f) : MasterSkin::background);
            g.fillEllipse (dot);
            g.setColour (colour);
            g.drawEllipse (dot, lit ? 2.0f : 1.5f);
            g.setFont (MasterSkin::font (10.0f, true));
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
            curve (0, MasterSkin::accent, 2.2f);
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
        g.setColour (MasterSkin::panel.withAlpha (0.95f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (MasterSkin::textBright);
        g.setFont (MasterSkin::font (12.0f, true));
        g.drawText (text, box, juce::Justification::centred, false);
    }

    auto key = juce::Rectangle<float> (plot.getX(), plot.getBottom() + 20.0f, plot.getWidth(), 16.0f);
    g.setFont (MasterSkin::font (12.0f));
    const auto item = [&] (juce::Colour c, const juce::String& t, float w)
    {
        g.setColour (c);
        g.fillRect (key.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, 10.0f));
        key.removeFromLeft (6.0f);
        g.setColour (MasterSkin::textDim);
        g.drawText (t, key.removeFromLeft (w), juce::Justification::centredLeft, false);
        key.removeFromLeft (12.0f);
    };
    if (settings.midSide) { item (midColour, "middle", 50.0f); item (sideColour, "sides", 40.0f); }
    else                    item (MasterSkin::accent, "the whole mix", 90.0f);
    item (MasterSkin::textDim.withAlpha (0.4f), "the mix coming in", 120.0f);
    g.setColour (MasterSkin::textDim);
    g.drawText ("drag a dot to move it - double-click: back to 0 dB - C = LOW CUT", key, juce::Justification::centredRight, false);
    if (! on || bypassed)
    {
        g.setColour (MasterSkin::textDim);
        g.setFont (MasterSkin::font (14.0f, true));
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
    g.setColour (MasterSkin::background);
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
        g.setColour (MasterSkin::textDim.withAlpha (0.14f));
        g.fillPath (mix);
    }
    g.setFont (MasterSkin::font (11.0f));
    for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        g.setColour (MasterSkin::panelEdge.withAlpha (0.6f));
        g.drawVerticalLine (juce::roundToInt (xOf (hz)), plot.getY(), plot.getBottom());
        g.setColour (MasterSkin::textDim);
        g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (xOf (hz) - 20.0f, plot.getBottom() + 3.0f, 40.0f, 14.0f), juce::Justification::centred, false);
    }
    for (float db : { 0.0f, 6.0f, 12.0f })
    {
        const float y = plot.getY() + plot.getHeight() * 0.6f * db / cutRange;
        g.setColour (MasterSkin::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (MasterSkin::textDim);
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
            g.setColour (MasterSkin::background.withAlpha (0.7f));
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
                g.setColour (MasterSkin::meterHigh.withAlpha (0.85f));
                g.fillRect (left.withHeight (cutH (shownCut[(size_t) b][0])));
                g.setColour (sideColour.interpolatedWith (MasterSkin::meterHigh, 0.35f));
                g.fillRect (right.withHeight (cutH (shownCut[(size_t) b][1])));
            }
            else
            {
                g.setColour (MasterSkin::meterHigh.withAlpha (0.85f));
                g.fillRect (bar.withHeight (cutH (shownCut[(size_t) b][0])));
            }
        }
        // The band's name and how much (bottom)
        g.setColour (bandOn ? (heard ? MasterSkin::textBright : MasterSkin::textDim) : MasterSkin::textDim.withAlpha (0.5f));
        g.setFont (MasterSkin::font (13.0f, true));
        auto label = region.withTrimmedTop (region.getHeight() - 46.0f);
        g.drawText (juce::String (b + 1) + "  " + MasterMBIDs::bandNames[b] + (bandOn ? "" : "  (off)"), label.removeFromTop (20.0f), juce::Justification::centred, false);
        g.setFont (MasterSkin::font (11.5f));
        const auto cutText = [&] (float db) { return db < 0.05f ? juce::String ("0.0") : "-" + juce::String (db, 1); };
        g.setColour (MasterSkin::textDim);
        g.drawText (settings.midSide ? "mid " + cutText (shownCut[(size_t) b][0]) + " / side " + cutText (shownCut[(size_t) b][1]) + " dB"
                                     : cutText (shownCut[(size_t) b][0]) + " dB", label, juce::Justification::centred, false);
    }
    // The splits: drag them
    for (int sp = 0; sp < MultibandCompressor::numSplits; ++sp)
    {
        const float x = splitX (sp);
        const bool lit = sp == hovered || sp == dragging;
        g.setColour (lit ? MasterSkin::accent : MasterSkin::accent.withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> (x - (lit ? 1.5f : 1.0f), plot.getY(), lit ? 3.0f : 2.0f, plot.getHeight()));
        const auto tag = juce::Rectangle<float> (64.0f, 18.0f).withCentre ({ x, plot.getY() + plot.getHeight() * 0.72f });
        g.setColour (MasterSkin::panel);
        g.fillRoundedRectangle (tag, 4.0f);
        g.setColour (MasterSkin::accent);
        g.drawRoundedRectangle (tag, 4.0f, 1.0f);
        g.setFont (MasterSkin::font (11.0f, true));
        g.drawText (Format::hertz (settings.splitHz[(size_t) sp]), tag, juce::Justification::centred, false);
    }

    auto key = juce::Rectangle<float> (plot.getX(), plot.getBottom() + 20.0f, plot.getWidth(), 16.0f);
    g.setFont (MasterSkin::font (12.0f));
    const auto item = [&] (juce::Colour c, const juce::String& t, float w)
    {
        g.setColour (c);
        g.fillRect (key.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, 10.0f));
        key.removeFromLeft (6.0f);
        g.setColour (MasterSkin::textDim);
        g.drawText (t, key.removeFromLeft (w), juce::Justification::centredLeft, false);
        key.removeFromLeft (12.0f);
    };
    item (MasterSkin::meterHigh.withAlpha (0.85f), settings.midSide ? "middle turned down" : "turned down", settings.midSide ? 120.0f : 80.0f);
    if (settings.midSide) item (sideColour.interpolatedWith (MasterSkin::meterHigh, 0.35f), "sides turned down", 120.0f);
    g.setColour (MasterSkin::textDim);
    g.drawText ("drag the lines between the bands to move the splits", key, juce::Justification::centredRight, false);
    if (! on || bypassed)
    {
        g.setColour (MasterSkin::textDim);
        g.setFont (MasterSkin::font (14.0f, true));
        g.drawText (bypassed ? "BYPASSED" : "THE MULTIBAND IS OFF", plot.withHeight (24.0f).translated (0.0f, 6.0f), juce::Justification::centred, false);
    }
}


//==============================================================================
//==============================================================================
// REFERENCE
//==============================================================================
namespace
{
    const juce::Colour refColour { 0xffffd257 };   // the reference: gold
    const juce::Colour ourColour = MasterSkin::cyan;  // the master: cyan

    juce::String timeText (double seconds)
    {
        const int s = juce::jmax (0, (int) std::floor (seconds + 0.05));
        return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }
}

ReferenceDisplay::ReferenceDisplay (EasyMasterAudioProcessor& p) : processor (p)
{
    incoming.resize (16384);
    frame.resize ((size_t) BandSpectrum::fftSize);
    for (auto* b : { &load, &remove, &match })
    {
        b->fontSize = 13.5f;
        addAndMakeVisible (*b);
    }
    load.onClick = [this] { chooseFile(); };
    remove.onClick = [this] { processor.clearReference(); repaint(); };
    match.setClickingTogglesState (false);
    match.onClick = [this] { processor.setReferenceMatching (! processor.getReference().isMatching()); };
}

void ReferenceDisplay::resized()
{
    auto top = getLocalBounds().reduced (12, 8).removeFromTop (28);
    load.setBounds (top.removeFromLeft (104));
    top.removeFromLeft (6);
    remove.setBounds (top.removeFromLeft (72));
    match.setBounds (top.removeFromRight (132));
}

juce::Rectangle<float> ReferenceDisplay::plotArea() const
{
    return getLocalBounds().toFloat().reduced (14.0f, 8.0f).withTrimmedTop (60.0f).withTrimmedLeft (30.0f).withTrimmedBottom (112.0f);
}

juce::Rectangle<float> ReferenceDisplay::overviewArea() const
{
    const auto all = getLocalBounds().toFloat().reduced (14.0f, 8.0f);
    return { all.getX(), all.getBottom() - 62.0f, all.getWidth(), 44.0f };
}

float ReferenceDisplay::xOf (double hz) const
{
    const auto p = plotArea();
    return p.getX() + p.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0));
}

double ReferenceDisplay::secondsAt (float x) const
{
    const auto o = overviewArea();
    return juce::jlimit (0.0, processor.getReference().getLengthSeconds(), (double) ((x - o.getX()) / o.getWidth()) * processor.getReference().getLengthSeconds());
}

float ReferenceDisplay::xOfSeconds (double seconds) const
{
    const auto o = overviewArea();
    const double length = juce::jmax (0.001, processor.getReference().getLengthSeconds());
    return o.getX() + o.getWidth() * (float) juce::jlimit (0.0, 1.0, seconds / length);
}

std::vector<float> ReferenceDisplay::smoothed (const std::vector<float>& bands)
{
    // (each band with its neighbours: half an octave - the shape, not every wiggle)
    std::vector<float> out (bands.size());
    for (size_t b = 0; b < bands.size(); ++b)
    {
        double e = 0.0; int n = 0;
        for (size_t k = b > 1 ? b - 1 : 0; k <= juce::jmin (bands.size() - 1, b + 1); ++k) { e += std::pow (10.0, bands[k] / 10.0); ++n; }
        out[b] = (float) (10.0 * std::log10 (e / n));
    }
    return out;
}

void ReferenceDisplay::update()
{
    auto& ref = processor.getReference();

    // The master's picture: every half window of what you hear, averaged over about six seconds
    const double rate = processor.getSampleRate() > 0 ? processor.getSampleRate() : 48000.0;
    if (std::abs (rate - analyserRate) > 0.5) { analyser.prepare (rate); analyser.setRate (rate); analyserRate = rate; frameFilled = 0; haveOurs = false; }
    const int count = ref.takeOurs (incoming.data(), (int) incoming.size());
    bool changed = false;
    for (int i = 0; i < count; ++i)
    {
        frame[(size_t) frameFilled++] = incoming[(size_t) i];
        if (frameFilled == BandSpectrum::fftSize)
        {
            std::array<float, BandSpectrum::numBands> bands {};
            analyser.analyse (frame.data(), bands.data());
            const double hop = BandSpectrum::fftSize / 2 / rate, keep = std::exp (-hop / 6.0);
            for (int b = 0; b < BandSpectrum::numBands; ++b)
                ourAverage[(size_t) b] = haveOurs ? ourAverage[(size_t) b] * keep + bands[(size_t) b] * (1.0 - keep) : bands[(size_t) b];
            haveOurs = true;
            changed = true;
            // (slide the window on by half)
            std::copy (frame.begin() + BandSpectrum::fftSize / 2, frame.end(), frame.begin());
            frameFilled = BandSpectrum::fftSize / 2;
        }
    }
    if (changed && ref.getOurLufs() > -70.0f)
    {
        ourBands.resize ((size_t) BandSpectrum::numBands);
        for (int b = 0; b < BandSpectrum::numBands; ++b)
            ourBands[(size_t) b] = (float) (10.0 * std::log10 (ourAverage[(size_t) b] + 1.0e-20)) - ref.getOurLufs();
    }

    // The reference's picture and overview: when the song or the part changes
    if (ref.getStatus() != shownStatus || ref.getName() != shownName || std::abs (ref.getStart() - shownStart) > 1.0e-6 || std::abs (ref.getEnd() - shownEnd) > 1.0e-6)
    {
        shownStatus = ref.getStatus(); shownName = ref.getName(); shownStart = ref.getStart(); shownEnd = ref.getEnd();
        refBands = ref.getSectionSpectrum();
        overview = ref.getOverview();
    }
    match.setToggleState (ref.isMatching(), juce::dontSendNotification);
    remove.setEnabled (ref.getStatus() != ReferenceTrack::empty);
    repaint();
}

void ReferenceDisplay::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a reference song", processor.getReference().getFile(), "*.wav;*.flac;*.mp3;*.aif;*.aiff;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& c)
    {
        if (c.getResult().existsAsFile()) processor.loadReference (c.getResult());
    });
}

bool ReferenceDisplay::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;flac;mp3;aif;aiff;ogg")) return true;
    return false;
}

void ReferenceDisplay::filesDropped (const juce::StringArray& files, int, int)
{
    dropping = false;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;flac;mp3;aif;aiff;ogg")) { processor.loadReference (juce::File (f)); break; }
    repaint();
}

void ReferenceDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragFrom = -1.0; dragged = false; downX = e.position.x;
    if (processor.getReference().getStatus() == ReferenceTrack::ready && overviewArea().expanded (0.0f, 6.0f).contains (e.position))
        dragFrom = secondsAt (e.position.x);
}

void ReferenceDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom < 0.0 || std::abs (e.position.x - downX) < 4.0f) return;
    dragged = true;
    const double t = secondsAt (e.position.x);
    processor.setReferenceSection (juce::jmin (dragFrom, t), juce::jmax (dragFrom, t));
}

void ReferenceDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragFrom >= 0.0 && ! dragged)
    {
        // A click: START here (the loop's end stays if it's still after it)
        const auto& ref = processor.getReference();
        processor.setReferenceSection (dragFrom, ref.getEnd() > dragFrom + 1.0 ? ref.getEnd() : 0.0);
    }
    dragFrom = -1.0;
}

void ReferenceDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (overviewArea().expanded (0.0f, 6.0f).contains (e.position))
        processor.setReferenceSection (0.0, 0.0);   // the whole song
}

juce::String ReferenceDisplay::describeDifference() const
{
    if (refBands.size() != (size_t) BandSpectrum::numBands || ourBands.size() != (size_t) BandSpectrum::numBands) return {};
    // In plain regions (a third of an octave smoothed); the biggest one each way
    const auto r = smoothed (refBands), o = smoothed (ourBands);
    int more = -1, less = -1; float mostMore = 1.5f, mostLess = -1.5f;   // (under 1.5 dB: not worth mentioning)
    for (int b = 6; b < BandSpectrum::numBands - 2; ++b)   // (40 Hz - 16 kHz: below that, too little to judge by)
    {
        const float d = r[(size_t) b] - o[(size_t) b];
        if (d > mostMore) { mostMore = d; more = b; }
        if (d < mostLess) { mostLess = d; less = b; }
    }
    const auto hz = [] (int b) { const double f = BandSpectrum::centreHz (b); return f >= 1000.0 ? juce::String (f / 1000.0, 1) + " kHz" : juce::String (juce::roundToInt (f / (f > 200.0 ? 10.0 : 5.0)) * (f > 200.0 ? 10 : 5)) + " Hz"; };
    juce::StringArray parts;
    if (more >= 0) parts.add ("the reference has more around " + hz (more) + " (+" + juce::String (mostMore, 1) + " dB)");
    if (less >= 0) parts.add ("yours has more around " + hz (less) + " (+" + juce::String (-mostLess, 1) + " dB)");
    if (parts.isEmpty()) return "Very close: no difference bigger than 1.5 dB.";
    auto text = parts.joinIntoString ("; ") + ".";
    return text.substring (0, 1).toUpperCase() + text.substring (1);
}

void ReferenceDisplay::paint (juce::Graphics& g)
{
    const auto& ref = processor.getReference();
    const auto area = getLocalBounds().toFloat();
    g.setColour (MasterSkin::background);
    g.fillRoundedRectangle (area, 6.0f);
    const auto plot = plotArea();
    const int status = ref.getStatus();

    // The song's name (beside the buttons)
    {
        auto line = getLocalBounds().reduced (12, 8).removeFromTop (28).toFloat().withTrimmedLeft (190.0f).withTrimmedRight (140.0f);
        g.setFont (MasterSkin::font (13.5f, true));
        juce::String text; juce::Colour colour = MasterSkin::textBright;
        if (status == ReferenceTrack::ready)   text = ref.getName();
        else if (status == ReferenceTrack::loading) { text = "Reading the song..."; colour = MasterSkin::accent; }
        else if (status == ReferenceTrack::failed)  { text = ref.getError(); colour = MasterSkin::meterHigh; }
        else { text = "No reference loaded"; colour = MasterSkin::textDim; }
        g.setColour (colour);
        g.drawFittedText (text, line.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }
    // ... and under them, the loudness numbers
    {
        auto line = juce::Rectangle<float> (area.getX() + 14.0f, area.getY() + 40.0f, area.getWidth() - 28.0f, 18.0f);
        g.setFont (MasterSkin::font (12.0f));
        g.setColour (MasterSkin::textDim);
        juce::String text;
        if (status == ReferenceTrack::ready)
        {
            const float refLufs = ref.getSectionLufs(), ourLufs = ref.getOurLufs();
            text = "Reference " + (refLufs > -70.0f ? juce::String (refLufs, 1) + " LUFS" : juce::String ("--"))
                 + (ref.getEnd() > 0.0 ? " (" + timeText (ref.getStart()) + " to " + timeText (ref.getEnd()) + ")" : (ref.getStart() > 0.0 ? " (from " + timeText (ref.getStart()) + ")" : juce::String()))
                 + "   -   yours " + (ourLufs > -70.0f ? juce::String (ourLufs, 1) + " LUFS (last 20 s)" : juce::String ("-- (press play)"));
            if (refLufs > -70.0f && ourLufs > -70.0f)
            {
                const float d = refLufs - ourLufs;
                text << "   -   " << (std::abs (d) < 0.25f ? juce::String ("the same loudness")
                                       : "the reference is " + juce::String (std::abs (d), 1) + " dB " + (d > 0 ? "louder" : "quieter"))
                     << (ref.isMatching() ? ", matched" : "");
            }
        }
        g.drawText (text, line, juce::Justification::centredLeft, true);
    }

    // The frequency pictures (the same loudness: only the balance differs)
    const float topDbShown = [&]
    {
        float m = -100.0f;
        for (auto* v : { &refBands, &ourBands }) for (float x : *v) m = juce::jmax (m, x);
        return m > -99.0f ? std::ceil ((m + 3.0f) / 6.0f) * 6.0f : 0.0f;
    }();
    const float spanDb = 36.0f;
    const auto yOf = [&] (float db) { return plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, (topDbShown - db) / spanDb); };
    g.setFont (MasterSkin::font (11.0f));
    for (float db = topDbShown; db >= topDbShown - spanDb - 0.01f; db -= 6.0f)
    {
        g.setColour (MasterSkin::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (yOf (db)), plot.getX(), plot.getRight());
    }
    g.setColour (MasterSkin::textDim);
    g.drawText ("6 dB", juce::Rectangle<float> (plot.getX() - 34.0f, yOf (topDbShown - 6.0f) - 7.0f, 30.0f, 14.0f), juce::Justification::centredRight, false);
    for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        g.setColour (MasterSkin::panelEdge);
        g.drawVerticalLine (juce::roundToInt (xOf (hz)), plot.getY(), plot.getBottom());
        g.setColour (MasterSkin::textDim);
        g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (xOf (hz) - 20.0f, plot.getBottom() + 3.0f, 40.0f, 14.0f), juce::Justification::centred, false);
    }
    const auto curvePath = [&] (const std::vector<float>& bands)
    {
        juce::Path path;
        const auto s = smoothed (bands);
        for (int b = 0; b < BandSpectrum::numBands; ++b)
        {
            const float x = xOf (BandSpectrum::centreHz (b)), y = yOf (s[(size_t) b]);
            if (b == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        return path;
    };
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (plot.toNearestInt());
        const bool both = refBands.size() == (size_t) BandSpectrum::numBands && ourBands.size() == (size_t) BandSpectrum::numBands;
        if (both)
        {
            // Between the two: gold where the reference has more, cyan where yours has
            const auto r = smoothed (refBands), o = smoothed (ourBands);
            for (int b = 0; b + 1 < BandSpectrum::numBands; ++b)
            {
                const float x0 = xOf (BandSpectrum::centreHz (b)), x1 = xOf (BandSpectrum::centreHz (b + 1));
                juce::Path strip;
                strip.startNewSubPath (x0, yOf (r[(size_t) b])); strip.lineTo (x1, yOf (r[(size_t) b + 1]));
                strip.lineTo (x1, yOf (o[(size_t) b + 1])); strip.lineTo (x0, yOf (o[(size_t) b])); strip.closeSubPath();
                g.setColour ((r[(size_t) b] + r[(size_t) b + 1] > o[(size_t) b] + o[(size_t) b + 1] ? refColour : ourColour).withAlpha (0.16f));
                g.fillPath (strip);
            }
        }
        if (ourBands.size() == (size_t) BandSpectrum::numBands)
        {
            g.setColour (ourColour);
            g.strokePath (curvePath (ourBands), juce::PathStrokeType (2.0f));
        }
        if (refBands.size() == (size_t) BandSpectrum::numBands)
        {
            g.setColour (refColour);
            g.strokePath (curvePath (refBands), juce::PathStrokeType (2.0f));
        }
    }
    // Words over the picture: what's missing, or the biggest differences
    {
        juce::String text; juce::Colour colour = MasterSkin::textDim;
        if (status == ReferenceTrack::empty || status == ReferenceTrack::failed)
            text = "Click LOAD SONG, or drop a song here (WAV, FLAC, MP3, AIFF, OGG): a finished track to compare yours with.";
        else if (status == ReferenceTrack::loading)
            text = "Reading and measuring the song...";
        else if (ourBands.empty())
            text = "Press play in your DAW: the reference plays along, and your master's picture appears here.";
        else { text = describeDifference(); colour = MasterSkin::textBright; }
        g.setColour (colour);
        g.setFont (MasterSkin::font (13.0f, true));
        g.drawFittedText (text, plot.reduced (8.0f, 4.0f).removeFromTop (34.0f).toNearestInt(), juce::Justification::centredTop, 2);
    }
    if (ref.isListening())
    {
        auto badge = juce::Rectangle<float> (230.0f, 24.0f).withCentre ({ plot.getCentreX(), plot.getBottom() - 20.0f });
        g.setColour (refColour.withAlpha (0.2f));
        g.fillRoundedRectangle (badge, 4.0f);
        g.setColour (refColour);
        g.drawRoundedRectangle (badge, 4.0f, 1.2f);
        g.setFont (MasterSkin::font (13.0f, true));
        g.drawText ("YOU'RE HEARING THE REFERENCE", badge, juce::Justification::centred, false);
    }

    // The key
    {
        auto key = juce::Rectangle<float> (plot.getX(), plot.getBottom() + 20.0f, plot.getWidth(), 16.0f);
        g.setFont (MasterSkin::font (12.0f));
        const auto item = [&] (juce::Colour c, const juce::String& t, float w)
        {
            g.setColour (c);
            g.fillRect (key.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, 3.0f));
            key.removeFromLeft (6.0f);
            g.setColour (MasterSkin::textDim);
            g.drawText (t, key.removeFromLeft (w), juce::Justification::centredLeft, false);
            key.removeFromLeft (12.0f);
        };
        item (refColour, "the reference", 80.0f);
        item (ourColour, "your master", 70.0f);
        g.setColour (MasterSkin::textDim);
        g.drawText ("both at the same loudness: only the balance differs", key, juce::Justification::centredRight, false);
    }

    // The overview: the whole song's loudness, the part being compared, where it's playing
    const auto o = overviewArea();
    g.setColour (MasterSkin::panel);
    g.fillRoundedRectangle (o, 3.0f);
    if (status == ReferenceTrack::ready && ! overview.empty())
    {
        const double length = juce::jmax (0.001, ref.getLengthSeconds());
        const float sx = xOfSeconds (ref.getStart()), ex = ref.getEnd() > 0.0 ? xOfSeconds (ref.getEnd()) : o.getRight();
        g.setColour (refColour.withAlpha (0.13f));
        g.fillRect (juce::Rectangle<float> (sx, o.getY(), ex - sx, o.getHeight()));
        const int columns = juce::jmax (1, (int) o.getWidth());
        const float loudestOfAll = *std::max_element (overview.begin(), overview.end());   // (the top of the strip: the song's loudest moment; 30 dB below it, nothing)
        for (int c = 0; c < columns; ++c)
        {
            const size_t a = (size_t) ((double) c / columns * (double) overview.size()), b = juce::jmax (a + 1, (size_t) ((double) (c + 1) / columns * (double) overview.size()));
            float loudest = -200.0f;
            for (size_t k = a; k < juce::jmin (b, overview.size()); ++k) loudest = juce::jmax (loudest, overview[k]);
            const float h = o.getHeight() * juce::jlimit (0.0f, 1.0f, (loudest - loudestOfAll + 30.0f) / 30.0f);
            const float x = o.getX() + (float) c;
            g.setColour ((x >= sx && x <= ex ? refColour : MasterSkin::textDim).withAlpha (0.75f));
            g.fillRect (x, o.getBottom() - h, 1.0f, h);
        }
        g.setColour (refColour);
        g.fillRect (sx - 1.0f, o.getY(), 2.0f, o.getHeight());
        if (ref.getEnd() > 0.0) g.fillRect (ex - 1.0f, o.getY(), 2.0f, o.getHeight());
        if (ref.isPlaying())
        {
            g.setColour (juce::Colours::white);
            g.fillRect (xOfSeconds (ref.getPositionSeconds()) - 0.5f, o.getY() - 2.0f, 1.5f, o.getHeight() + 4.0f);
        }
        g.setFont (MasterSkin::font (11.5f));
        g.setColour (MasterSkin::textDim);
        g.drawText ("0:00", juce::Rectangle<float> (o.getX(), o.getBottom() + 2.0f, 40.0f, 14.0f), juce::Justification::centredLeft, false);
        g.drawText (timeText (length), juce::Rectangle<float> (o.getRight() - 40.0f, o.getBottom() + 2.0f, 40.0f, 14.0f), juce::Justification::centredRight, false);
        g.drawText ("click: start there   -   drag: choose a part to loop   -   double-click: the whole song",
                    juce::Rectangle<float> (o.getX() + 44.0f, o.getBottom() + 2.0f, o.getWidth() - 88.0f, 14.0f), juce::Justification::centred, false);
    }
    else
    {
        g.setColour (MasterSkin::textDim.withAlpha (0.6f));
        g.setFont (MasterSkin::font (12.0f));
        g.drawText ("the song's loudness from start to finish will show here", o, juce::Justification::centred, false);
    }

    if (dropping)
    {
        g.setColour (ourColour.withAlpha (0.12f));
        g.fillRoundedRectangle (area, 6.0f);
        g.setColour (ourColour);
        g.drawRoundedRectangle (area.reduced (2.0f), 6.0f, 2.0f);
    }
}

EasyMasterAudioProcessorEditor::EasyMasterAudioProcessorEditor (EasyMasterAudioProcessor& p)
    : AudioProcessorEditor (&p), masterProcessor (p)
{
    setLookAndFeel (&look);
    console = std::make_unique<MasterHolo::Console> (p);
    addAndMakeVisible (*console);

    const auto w = MasterHolo::Console::width, h = MasterHolo::Console::height;
    setResizable (true, true);
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio ((double) w / (double) h);
        c->setSizeLimits (w / 2, h / 2, w * 2, h * 2);
    }
    const float scale = juce::jlimit (0.5f, 2.0f, p.windowScale);
    setSize (juce::roundToInt ((float) w * scale), juce::roundToInt ((float) h * scale));
}

EasyMasterAudioProcessorEditor::~EasyMasterAudioProcessorEditor()
{
    console = nullptr;
    setLookAndFeel (nullptr);
}

void EasyMasterAudioProcessorEditor::resized()
{
    if (console == nullptr) return;
    const float scale = (float) getWidth() / (float) MasterHolo::Console::width;
    console->setTransform (juce::AffineTransform::scale (scale));
    masterProcessor.windowScale = scale;
}
