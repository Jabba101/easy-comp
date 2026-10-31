#include "HoloScreen.h"

namespace Holo
{
    namespace
    {
        const juce::Colour gridLine   { 0xff14405a };
        const juce::Colour gridFaint  { 0xff10324a };
        const juce::Colour gridLabel  { 0xff5fb8d8 };
        const juce::Colour traceGrey  { 0xff5d7385 };

        constexpr float eqRangeDb = 15.0f;   // the EQ page shows +-15 dB
    }

    Screen::Screen (EasyStripAudioProcessor& p) : strip (p)
    {
        fft = std::make_unique<juce::dsp::FFT> (fftOrder);
        fftData.assign ((size_t) fftSize * 2, 0.0f);
        window.resize ((size_t) fftSize);
        for (int i = 0; i < fftSize; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1));
        spectrumDb.assign ((size_t) fftSize / 2, -120.0f);

        analyserButton.fontSize = 13.0f;
        analyserLink = std::make_unique<ButtonLink> (*strip.getEQ().parameters.getParameter (EQIDs::analyser), analyserButton);
        addChildComponent (analyserButton);
        setPage (2);
    }

    Screen::~Screen() = default;

    void Screen::setPage (int module)
    {
        page = juce::jlimit (0, StripModules::numModules - 1, module);
        analyserButton.setVisible (page == 2);
        dragging = hovering = -1;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }

    void Screen::resized()
    {
        const auto plot = eqPlot();
        analyserButton.setBounds ((int) plot.getX() + 6, (int) plot.getY() + 4, 84, 22);
    }

    void Screen::addMoment (int module, float a, float b, float c)
    {
        traces[(size_t) module].add (a, b, c);
    }

    void Screen::addCompFrame (float levelDb, float reductionDb)
    {
        compFrames[(size_t) compNext] = { levelDb, reductionDb };
        compNext = (compNext + 1) % (int) compFrames.size();
        if (levelDb > -150.0f)
            compLevelDb = levelDb;
    }

    void Screen::refresh()
    {
        if (page == 2 && strip.getEQ().parameters.getRawParameterValue (EQIDs::analyser)->load() > 0.5f)
            readAnalyser();

        // The VU needle: the reading already swings like one; this smooths the steps between frames
        tapeVu += (juce::jlimit (-22.0f, 4.0f, strip.getTape().getRecordVu()) - tapeVu) * 0.6f;
        repaint();
    }

    juce::Rectangle<float> Screen::plotArea() const
    {
        auto r = getLocalBounds().toFloat().reduced (6.0f);
        return r.withTrimmedLeft (30.0f).withTrimmedRight (8.0f).withTrimmedTop (18.0f).withTrimmedBottom (22.0f);
    }

    juce::Rectangle<float> Screen::eqPlot() const   { return plotArea(); }

    //==========================================================================
    void Screen::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat();
        const auto shape = chamfered (r.reduced (0.5f), 10.0f);

        g.setColour (Colours::cyan.withAlpha (0.10f));
        g.strokePath (shape, juce::PathStrokeType (6.0f));
        g.setColour (Colours::screen);
        g.fillPath (shape);
        g.setGradientFill (juce::ColourGradient (Colours::cyan.withAlpha (0.07f), r.getCentreX(), r.getCentreY(),
                                                 juce::Colours::transparentBlack, r.getX(), r.getY(), true));
        g.fillPath (shape);
        g.setColour (Colours::cyan);
        g.strokePath (shape, juce::PathStrokeType (1.2f));

        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (shape);

            switch (page)
            {
                case 0: paintGate (g);  break;
                case 1: paintAmp (g);   break;
                case 2: paintEQ (g);    break;
                case 3: paintDS (g);    break;
                case 4: paintComp (g);  break;
                case 5: paintTrans (g); break;
                case 6: paintSlap (g);  break;
                case 8: paintTape (g);  break;
                default: paintClip (g); break;
            }
        }
    }

    void Screen::drawFrame (juce::Graphics& g, const juce::String& caption)
    {
        const auto plot = plotArea();
        g.setFont (text (14.0f, 1));
        g.setColour (gridLabel);
        g.drawText (caption, juce::Rectangle<float> (plot.getX(), plot.getY() - 17.0f, plot.getWidth(), 16.0f), juce::Justification::centredRight, false);
    }

    void Screen::drawLevelGrid (juce::Graphics& g, juce::Rectangle<float> plot, float lowDb, float highDb, std::initializer_list<float> marks)
    {
        g.setFont (text (12.0f, 0));
        for (auto db : marks)
        {
            const float y = plot.getBottom() - plot.getHeight() * (db - lowDb) / (highDb - lowDb);
            g.setColour (std::abs (db) < 0.01f ? juce::Colour (0xff2a6a8a) : gridFaint);
            g.fillRect (plot.getX(), y, plot.getWidth(), 1.0f);
            g.setColour (gridLabel);
            g.drawText ((db > 0.0f ? "+" : "") + juce::String (juce::roundToInt (db)),
                        juce::Rectangle<float> (plot.getX() - 34.0f, y - 7.0f, 29.0f, 14.0f), juce::Justification::centredRight, false);
        }

        // seconds along the bottom
        g.setColour (gridLabel.withAlpha (0.7f));
        g.drawText ("NOW", juce::Rectangle<float> (plot.getRight() - 40.0f, plot.getBottom() + 4.0f, 40.0f, 14.0f), juce::Justification::centredRight, false);
        g.drawText ("2 S AGO", juce::Rectangle<float> (plot.getX(), plot.getBottom() + 4.0f, 80.0f, 14.0f), juce::Justification::centredLeft, false);
    }

    juce::Path Screen::tracePath (const Trace& t, int field, juce::Rectangle<float> plot, float lowDb, float highDb, bool closed) const
    {
        juce::Path p;
        const auto yFor = [&] (float db) { return plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (db - lowDb) / (highDb - lowDb)); };

        for (int i = 0; i < historyLength; ++i)
        {
            const float x = plot.getX() + plot.getWidth() * (float) i / (float) (historyLength - 1);
            const float y = yFor (t.at (i)[(size_t) field]);
            if (i == 0) p.startNewSubPath (x, closed ? plot.getBottom() : y);
            if (i > 0 || closed) p.lineTo (x, y);
        }

        if (closed)
        {
            p.lineTo (plot.getRight(), plot.getBottom());
            p.closeSubPath();
        }
        return p;
    }

    //==========================================================================
    // EQ
    //==========================================================================
    float Screen::xForHz (float hz) const
    {
        const auto plot = eqPlot();
        return plot.getX() + std::log10 (juce::jmax (20.0f, hz) / 20.0f) / 3.0f * plot.getWidth();
    }

    float Screen::hzForX (float x) const
    {
        const auto plot = eqPlot();
        return 20.0f * std::pow (10.0f, 3.0f * (x - plot.getX()) / plot.getWidth());
    }

    float Screen::yForDb (float db) const
    {
        const auto plot = eqPlot();
        return plot.getCentreY() - db / eqRangeDb * plot.getHeight() * 0.5f;
    }

    float Screen::dbForY (float y) const
    {
        const auto plot = eqPlot();
        return (plot.getCentreY() - y) / (plot.getHeight() * 0.5f) * eqRangeDb;
    }

    juce::Point<float> Screen::eqDotPosition (int band) const
    {
        auto& eq = strip.getEQ();
        const auto s = eq.getCurrentSettings();
        const auto& b = s.bands[(size_t) band];
        const bool mid = b.target != Equalizer::Target::side;
        const float atDot = (float) juce::Decibels::gainToDecibels (   // (responseAt gives a gain, not dB)
            Equalizer::responseAt (s, b.freqHz, eq.getCurrentSampleRate() > 0.0 ? eq.getCurrentSampleRate() : 48000.0, mid), -60.0);
        return { xForHz (b.freqHz), yForDb (juce::jlimit (-eqRangeDb, eqRangeDb, atDot)) };
    }

    int Screen::dotAt (juce::Point<float> p) const
    {
        for (int b = Equalizer::numBands; --b >= 0;)
            if (eqDotPosition (b).getDistanceFrom (p) < 11.0f)
                return b;
        return -1;
    }

    void Screen::readAnalyser()
    {
        strip.getEQ().copyRecentOutput (fftData.data(), fftSize);
        for (int i = 0; i < fftSize; ++i)
            fftData[(size_t) i] *= window[(size_t) i];
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        fft->performFrequencyOnlyForwardTransform (fftData.data(), true);

        const float scale = 4.0f / (float) fftSize;
        for (int i = 0; i < fftSize / 2; ++i)
        {
            const float db = juce::Decibels::gainToDecibels (fftData[(size_t) i] * scale, -120.0f);
            auto& shown = spectrumDb[(size_t) i];
            shown = db > shown ? db : shown + (db - shown) * 0.25f;
        }
    }

    void Screen::paintEQ (juce::Graphics& g)
    {
        auto& eq = strip.getEQ();
        const auto plot = eqPlot();
        const auto s = eq.getCurrentSettings();
        const double rate = eq.getCurrentSampleRate() > 0.0 ? eq.getCurrentSampleRate() : 48000.0;

        // Grid
        for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            g.setColour (gridLine);
            g.fillRect (xForHz (hz), plot.getY(), 1.0f, plot.getHeight());
        }
        for (float db : { 12.0f, 6.0f, -6.0f, -12.0f })
        {
            g.setColour (gridFaint);
            g.fillRect (plot.getX(), yForDb (db), plot.getWidth(), 1.0f);
        }
        {
            const float dashes[] = { 4.0f, 5.0f };
            g.setColour (juce::Colour (0xff2a6a8a));
            g.drawDashedLine (juce::Line<float> (plot.getX(), yForDb (0.0f), plot.getRight(), yForDb (0.0f)), dashes, 2, 1.0f);
        }

        g.setFont (text (12.5f, 0));
        g.setColour (gridLabel);
        for (auto [label, hz] : { std::pair<const char*, float> { "50", 50.0f }, { "100", 100.0f }, { "200", 200.0f }, { "500", 500.0f },
                                  { "1K", 1000.0f }, { "2K", 2000.0f }, { "5K", 5000.0f }, { "10K", 10000.0f } })
            g.drawText (label, juce::Rectangle<float> (xForHz (hz) + 3.0f, plot.getBottom() + 3.0f, 40.0f, 15.0f), juce::Justification::centredLeft, false);
        for (float db : { 12.0f, 0.0f, -12.0f })
            g.drawText ((db > 0.0f ? "+" : "") + juce::String (juce::roundToInt (db)),
                        juce::Rectangle<float> (plot.getX() - 32.0f, yForDb (db) - 7.0f, 27.0f, 14.0f), juce::Justification::centredRight, false);

        // The analyser (faint, behind)
        if (eq.parameters.getRawParameterValue (EQIDs::analyser)->load() > 0.5f)
        {
            juce::Path spectrum;
            spectrum.startNewSubPath (plot.getX(), plot.getBottom());
            for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
            {
                const float hz = hzForX (x);
                const float bin = hz / (float) rate * (float) fftSize;
                const int i = juce::jlimit (1, fftSize / 2 - 2, (int) bin);
                const float frac = juce::jlimit (0.0f, 1.0f, bin - (float) i);
                const float db = spectrumDb[(size_t) i] + (spectrumDb[(size_t) i + 1] - spectrumDb[(size_t) i]) * frac
                                 + 4.5f * std::log2 (juce::jmax (20.0f, hz) / 1000.0f);   // (tilted, as music falls off)
                const float y = plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 84.0f) / 84.0f);
                spectrum.lineTo (x, y);
            }
            spectrum.lineTo (plot.getRight(), plot.getBottom());
            spectrum.closeSubPath();
            g.setColour (juce::Colour (0xff2c5c78).withAlpha (0.55f));
            g.fillPath (spectrum);
        }

        // The curves
        const auto curve = [&] (bool mid)
        {
            juce::Path p;
            bool first = true;
            for (float x = plot.getX(); x <= plot.getRight() + 0.5f; x += 2.0f)
            {
                const float db = (float) juce::Decibels::gainToDecibels (Equalizer::responseAt (s, hzForX (x), rate, mid), -60.0);
                const float y = yForDb (juce::jlimit (-eqRangeDb - 2.0f, eqRangeDb + 2.0f, db));
                if (first) p.startNewSubPath (x, y); else p.lineTo (x, y);
                first = false;
            }
            return p;
        };

        bool split = s.monoBassHz > Equalizer::monoBassOffHz + 0.5f;
        for (auto& b : s.bands)
            split = split || (b.target != Equalizer::Target::stereo && std::abs (b.gainDb) > 0.01f);

        const auto fillUnder = [&] (const juce::Path& p, juce::Colour c)
        {
            juce::Path area (p);
            area.lineTo (plot.getRight(), yForDb (0.0f));
            area.lineTo (plot.getX(), yForDb (0.0f));
            area.closeSubPath();
            g.setGradientFill (juce::ColourGradient (c.withAlpha (0.30f), 0.0f, plot.getY(), c.withAlpha (0.0f), 0.0f, plot.getBottom(), false));
            g.fillPath (area);
        };

        const auto midCurve = curve (true);
        if (split)
        {
            const auto sideCurve = curve (false);
            fillUnder (sideCurve, Colours::side);
            fillUnder (midCurve, Colours::mid);
            strokeGlow (g, sideCurve, Colours::side, 2.4f);
            strokeGlow (g, midCurve, Colours::mid, 2.4f);

            g.setFont (text (14.5f, 1));
            g.setColour (Colours::mid);
            g.drawText ("MID", juce::Rectangle<float> (plot.getRight() - 84.0f, plot.getY() + 2.0f, 36.0f, 16.0f), juce::Justification::centredLeft, false);
            g.setColour (Colours::side);
            g.drawText ("SIDE", juce::Rectangle<float> (plot.getRight() - 46.0f, plot.getY() + 2.0f, 40.0f, 16.0f), juce::Justification::centredLeft, false);
        }
        else
        {
            fillUnder (midCurve, Colours::mid);
            strokeGlow (g, midCurve, Colours::mid, 2.4f);
        }

        // The dots (numbered, so you know which band is which)
        for (int b = 0; b < Equalizer::numBands; ++b)
        {
            const auto pos = eqDotPosition (b);
            const auto colour = s.bands[(size_t) b].target == Equalizer::Target::side ? Colours::side : Colours::mid;
            const float radius = (b == hovering || b == dragging) ? 8.0f : 6.5f;
            g.setColour (colour.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (radius * 2.0f + 6.0f, radius * 2.0f + 6.0f).withCentre (pos));
            g.setColour (colour);
            g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (pos));
            g.setColour (juce::Colour (0xff04101e));
            g.setFont (text (11.5f, 2));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (pos), juce::Justification::centred, false);
        }
    }

    void Screen::mouseMove (const juce::MouseEvent& e)
    {
        if (page != 2)
            return;
        const int over = dotAt (e.position);
        if (over != hovering)
        {
            hovering = over;
            setMouseCursor (over >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void Screen::mouseDown (const juce::MouseEvent& e)
    {
        if (page != 2)
            return;
        dragging = dotAt (e.position);
        if (dragging < 0)
            return;

        auto& params = strip.getEQ().parameters;
        dragFreq = params.getParameter (EQIDs::freq[dragging]);
        dragGain = params.getParameter (EQIDs::gain[dragging]);
        dragFreq->beginChangeGesture();
        dragGain->beginChangeGesture();
    }

    void Screen::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragging < 0)
            return;

        const float hz = hzForX (e.position.x);
        const float db = juce::jlimit (-Equalizer::maxGainDb, Equalizer::maxGainDb, dbForY (e.position.y));
        dragFreq->setValueNotifyingHost (dragFreq->convertTo0to1 (hz));
        dragGain->setValueNotifyingHost (dragGain->convertTo0to1 (db));

        if (auto* host = findParentComponentOfClass<Host>())
        {
            host->settingMoved (*dragGain);
            host->showValue ("EQ BAND " + juce::String (dragging + 1), dragFreq->getCurrentValueAsText() + ",  " + dragGain->getCurrentValueAsText());
        }
        repaint();
    }

    void Screen::mouseUp (const juce::MouseEvent&)
    {
        if (dragging >= 0)
        {
            dragFreq->endChangeGesture();
            dragGain->endChangeGesture();
            if (auto* host = findParentComponentOfClass<Host>())
                host->valueShown();
        }
        dragging = -1;
    }

    void Screen::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (page != 2)
            return;
        const int b = dotAt (e.position);
        if (b < 0)
            return;
        auto* gain = strip.getEQ().parameters.getParameter (EQIDs::gain[b]);
        gain->beginChangeGesture();
        gain->setValueNotifyingHost (gain->convertTo0to1 (0.0f));   // (back to 0 dB, as in Easy EQ)
        gain->endChangeGesture();
        if (auto* host = findParentComponentOfClass<Host>())
            host->settingMoved (*gain);
    }

    //==========================================================================
    // The other pages
    //==========================================================================
    void Screen::paintGate (juce::Graphics& g)
    {
        const auto plot = plotArea();
        drawFrame (g, "GATE:  HEARS (GREY)   COMES OUT (GREEN)   THRESHOLD (LINE)");
        drawLevelGrid (g, plot, -80.0f, 0.0f, { 0.0f, -20.0f, -40.0f, -60.0f, -80.0f });

        const auto& t = traces[0];
        // open moments as a faint band along the top
        for (int i = 0; i < historyLength; ++i)
            if (t.at (i)[2] > 0.5f)
            {
                g.setColour (Colours::gate.withAlpha (0.10f));
                g.fillRect (plot.getX() + plot.getWidth() * (float) i / (float) historyLength, plot.getY(), plot.getWidth() / (float) historyLength + 0.5f, plot.getHeight());
            }

        g.setColour (traceGrey.withAlpha (0.55f));
        g.fillPath (tracePath (t, 0, plot, -80.0f, 0.0f, true));
        strokeGlow (g, tracePath (t, 1, plot, -80.0f, 0.0f, false), Colours::gate, 1.6f, 0.5f);

        const float threshold = strip.getGate().getCurrentSettings().thresholdDb;
        const float y = plot.getBottom() - plot.getHeight() * (threshold + 80.0f) / 80.0f;
        const float dashes[] = { 5.0f, 4.0f };
        g.setColour (Colours::ink);
        g.drawDashedLine (juce::Line<float> (plot.getX(), y, plot.getRight(), y), dashes, 2, 1.3f);
    }

    void Screen::paintAmp (juce::Graphics& g)
    {
        auto& amp = strip.getAmp();
        const auto settings = amp.getCurrentSettings();
        const auto plot = plotArea();
        drawFrame (g, "AMP:  THE CURVE AT THIS DRIVE (IN ACROSS, OUT UP)");

        const auto square = plot.withSizeKeepingCentre (plot.getHeight() * 1.6f, plot.getHeight());
        g.setColour (gridFaint);
        for (int i = 0; i <= 4; ++i)
        {
            g.fillRect (square.getX() + square.getWidth() * (float) i / 4.0f, square.getY(), 1.0f, square.getHeight());
            g.fillRect (square.getX(), square.getY() + square.getHeight() * (float) i / 4.0f, square.getWidth(), 1.0f);
        }

        const float dashes[] = { 4.0f, 5.0f };
        g.setColour (juce::Colour (0xff2a6a8a));
        g.drawDashedLine (juce::Line<float> (square.getBottomLeft(), square.getTopRight()), dashes, 2, 1.0f);

        juce::Path p;
        for (int i = 0; i <= 200; ++i)
        {
            const float x = -1.0f + 2.0f * (float) i / 200.0f;
            const float y = ValveAmp::transfer (x, settings.drive);
            const float y2 = settings.mix * y + (1.0f - settings.mix) * x;
            const juce::Point<float> pt (square.getX() + square.getWidth() * (x + 1.0f) * 0.5f,
                                         square.getBottom() - square.getHeight() * (juce::jlimit (-1.2f, 1.2f, y2) + 1.0f) * 0.5f);
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        strokeGlow (g, p, Colours::amp, 2.4f);

        g.setFont (text (15.0f, 1));
        g.setColour (Colours::amp);
        g.drawText ("DRIVE " + juce::String (juce::roundToInt (settings.drive * 100.0f)) + "%",
                    juce::Rectangle<float> (plot.getX() + 4.0f, plot.getY() + 4.0f, 140.0f, 18.0f), juce::Justification::centredLeft, false);
        if (settings.autoGain)
        {
            g.setColour (Colours::dim);
            g.drawText ("LEVEL MATCH " + formatValue (Format::signedDb, amp.getMatchGainDb()) + " dB",
                        juce::Rectangle<float> (plot.getX() + 4.0f, plot.getY() + 24.0f, 180.0f, 18.0f), juce::Justification::centredLeft, false);
        }
    }

    void Screen::paintDS (juce::Graphics& g)
    {
        const auto plot = plotArea();
        drawFrame (g, "DS:  WHOLE SOUND (GREY)   HARSH BAND (VIOLET)   LINE   TURNED DOWN (RED)");
        drawLevelGrid (g, plot, -60.0f, 0.0f, { 0.0f, -20.0f, -40.0f, -60.0f });

        const auto& t = traces[3];
        g.setColour (traceGrey.withAlpha (0.5f));
        g.fillPath (tracePath (t, 0, plot, -60.0f, 0.0f, true));
        g.setColour (Colours::ds.withAlpha (0.45f));
        g.fillPath (tracePath (t, 1, plot, -60.0f, 0.0f, true));

        // turned down: red, hanging from the top (0..24 dB)
        juce::Path down;
        down.startNewSubPath (plot.getX(), plot.getY());
        for (int i = 0; i < historyLength; ++i)
            down.lineTo (plot.getX() + plot.getWidth() * (float) i / (float) (historyLength - 1),
                         plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, t.at (i)[2] / 24.0f));
        down.lineTo (plot.getRight(), plot.getY());
        down.closeSubPath();
        g.setColour (Colours::warn.withAlpha (0.55f));
        g.fillPath (down);
    }

    void Screen::paintComp (juce::Graphics& g)
    {
        auto& comp = strip.getComp();
        const auto settings = comp.getCurrentSettings();
        const auto plot = plotArea();
        drawFrame (g, "GAIN REDUCTION (LAST 4 S)");
        g.setFont (text (14.0f, 1));
        g.drawText ("COMP:  THE CURVE", juce::Rectangle<float> (plot.getX(), plot.getY() - 17.0f, 200.0f, 16.0f), juce::Justification::centredLeft, false);

        // The curve, in a square on the left
        const auto square = plot.withWidth (plot.getHeight());
        const auto toScreen = [&] (float inDb, float outDb)
        {
            return juce::Point<float> (square.getX() + square.getWidth() * (inDb + 60.0f) / 60.0f,
                                       square.getBottom() - square.getHeight() * juce::jlimit (0.0f, 1.0f, (outDb + 60.0f) / 60.0f));
        };

        g.setColour (gridFaint);
        for (int i = 0; i <= 4; ++i)
        {
            g.fillRect (square.getX() + square.getWidth() * (float) i / 4.0f, square.getY(), 1.0f, square.getHeight());
            g.fillRect (square.getX(), square.getY() + square.getHeight() * (float) i / 4.0f, square.getWidth(), 1.0f);
        }
        const float dashes[] = { 4.0f, 5.0f };
        g.setColour (juce::Colour (0xff2a6a8a));
        g.drawDashedLine (juce::Line<float> (square.getBottomLeft(), square.getTopRight()), dashes, 2, 1.0f);

        juce::Path curve;
        for (int i = 0; i <= 120; ++i)
        {
            const float inDb = -60.0f + 60.0f * (float) i / 120.0f;
            const auto pt = toScreen (inDb, Compressor::staticOutputDb (inDb, settings, comp.getAutoGainDb()));
            if (i == 0) curve.startNewSubPath (pt); else curve.lineTo (pt);
        }
        strokeGlow (g, curve, Colours::comp, 2.4f);

        if (compLevelDb > -60.0f)
        {
            const auto dot = toScreen (compLevelDb, Compressor::staticOutputDb (compLevelDb, settings, comp.getAutoGainDb()));
            g.setColour (juce::Colours::white);
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (dot));
        }

        g.setFont (text (12.0f, 0));
        g.setColour (gridLabel);
        g.drawText ("-60", juce::Rectangle<float> (square.getX() - 34.0f, square.getBottom() - 7.0f, 29.0f, 14.0f), juce::Justification::centredRight, false);
        g.drawText ("0", juce::Rectangle<float> (square.getX() - 34.0f, square.getY() - 7.0f, 29.0f, 14.0f), juce::Justification::centredRight, false);
        g.drawText ("IN", juce::Rectangle<float> (square.getCentreX() - 20.0f, square.getBottom() + 3.0f, 40.0f, 14.0f), juce::Justification::centred, false);

        // Gain reduction over time, on the right
        const auto history = plot.withTrimmedLeft (square.getWidth() + 40.0f);
        g.setColour (gridFaint);
        for (float db : { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f })
        {
            const float y = history.getY() + history.getHeight() * db / 24.0f;
            g.fillRect (history.getX(), y, history.getWidth(), 1.0f);
            g.setColour (gridLabel);
            g.drawText (juce::String (juce::roundToInt (db)), juce::Rectangle<float> (history.getX() - 30.0f, y - 7.0f, 25.0f, 14.0f), juce::Justification::centredRight, false);
            g.setColour (gridFaint);
        }

        juce::Path gr;
        gr.startNewSubPath (history.getX(), history.getY());
        const int n = (int) compFrames.size();
        for (int i = 0; i < n; ++i)
        {
            const auto& f = compFrames[(size_t) ((compNext + i) % n)];
            gr.lineTo (history.getX() + history.getWidth() * (float) i / (float) (n - 1),
                       history.getY() + history.getHeight() * juce::jlimit (0.0f, 1.0f, f[1] / 24.0f));
        }
        gr.lineTo (history.getRight(), history.getY());
        gr.closeSubPath();
        g.setGradientFill (juce::ColourGradient (Colours::reduction, 0.0f, history.getY(), Colours::reduction.withAlpha (0.25f), 0.0f, history.getBottom(), false));
        g.fillPath (gr);
    }

    void Screen::paintTrans (juce::Graphics& g)
    {
        const auto plot = plotArea();
        drawFrame (g, "TRANS:  BEFORE (GREY)   AFTER (MAGENTA)");
        drawLevelGrid (g, plot, -60.0f, 0.0f, { 0.0f, -20.0f, -40.0f, -60.0f });
        g.setColour (traceGrey.withAlpha (0.55f));
        g.fillPath (tracePath (traces[5], 0, plot, -60.0f, 0.0f, true));
        strokeGlow (g, tracePath (traces[5], 1, plot, -60.0f, 0.0f, false), Colours::trans, 1.6f, 0.5f);
    }

    void Screen::paintSlap (juce::Graphics& g)
    {
        const auto plot = plotArea();
        drawFrame (g, "BITCH-SLAP:  YOUR SOUND (GREY)   THE BURST (RED)");
        drawLevelGrid (g, plot, -60.0f, 0.0f, { 0.0f, -20.0f, -40.0f, -60.0f });
        g.setColour (traceGrey.withAlpha (0.55f));
        g.fillPath (tracePath (traces[6], 0, plot, -60.0f, 0.0f, true));
        g.setColour (Colours::slap.withAlpha (0.5f));
        g.fillPath (tracePath (traces[6], 1, plot, -60.0f, 0.0f, true));
        strokeGlow (g, tracePath (traces[6], 1, plot, -60.0f, 0.0f, false), Colours::slap, 1.4f, 0.5f);
    }

    void Screen::paintClip (juce::Graphics& g)
    {
        const auto plot = plotArea();
        drawLevelGrid (g, plot, -12.0f, 12.0f, { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f });

        const auto& t = traces[7];
        g.setColour (traceGrey.withAlpha (0.55f));
        g.fillPath (tracePath (t, 0, plot, -12.0f, 12.0f, true));
        g.setColour (Colours::clip.withAlpha (0.75f));
        g.fillPath (tracePath (t, 1, plot, -12.0f, 12.0f, true));

        // What was shaved off, in red
        float most = 0.0f;
        for (int i = 0; i < historyLength; ++i)
        {
            const auto& m = t.at (i);
            if (m[0] > m[1] + 0.05f && m[1] > -100.0f)
            {
                most = juce::jmax (most, m[0] - m[1]);
                const auto yFor = [&] (float db) { return plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 12.0f) / 24.0f); };
                g.setColour (Colours::warn.withAlpha (0.8f));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (plot.getX() + plot.getWidth() * (float) i / (float) historyLength, yFor (m[0]),
                                                                        plot.getX() + plot.getWidth() * (float) (i + 1) / (float) historyLength + 0.5f, yFor (m[1])));
            }
        }

        const float ceiling = strip.getClip().getCurrentSettings().ceilingDb;
        const float y = plot.getBottom() - plot.getHeight() * (ceiling + 12.0f) / 24.0f;
        const float dashes[] = { 5.0f, 4.0f };
        g.setColour (Colours::ink);
        g.drawDashedLine (juce::Line<float> (plot.getX(), y, plot.getRight(), y), dashes, 2, 1.3f);

        drawFrame (g, "CLIP:  PUSHED (GREY)   COMES OUT (ORANGE)   SHAVED (RED)   CEILING (LINE)     PEAKS CUT BY UP TO "
                          + juce::String (most, 1) + " dB");
    }

    //==========================================================================
    // TAPE
    //==========================================================================
    void Screen::paintTape (juce::Graphics& g)
    {
        auto& tape = strip.getTape();
        const auto settings = tape.getCurrentSettings();
        const auto plot = plotArea();
        static const char* speedNames[] { "15 IPS", "30 IPS", "CASSETTE" };
        drawFrame (g, "TAPE:  HOW HARD IT HITS THE TAPE (VU)   THE MACHINE AT " + juce::String (speedNames[(int) settings.speed]));

        // Left: the VU meter
        auto meter = plot.withWidth (plot.getWidth() * 0.4f).reduced (6.0f, 2.0f);
        {
            constexpr float lowVu = -20.0f, highVu = 3.0f, sweep = 0.8f;
            const float radius = juce::jmin (meter.getWidth() * 0.6f, meter.getHeight() * 1.05f);
            const juce::Point<float> pivot (meter.getCentreX(), meter.getY() + radius + 4.0f);
            const auto angleOf = [&] (float vu)
            {
                const float a = juce::Decibels::decibelsToGain (juce::jlimit (lowVu, highVu, vu));
                const float lo = juce::Decibels::decibelsToGain (lowVu), hi = juce::Decibels::decibelsToGain (highVu);
                return -sweep + 2.0f * sweep * (a - lo) / (hi - lo);
            };
            const auto at = [&] (float angle, float r)   { return pivot + juce::Point<float> (std::sin (angle) * r, -std::cos (angle) * r); };

            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (meter.toNearestInt());

            for (int part = 0; part < 2; ++part)
            {
                juce::Path arc;
                arc.addCentredArc (pivot.x, pivot.y, radius * 0.82f, radius * 0.82f, 0.0f,
                                   angleOf (part == 0 ? lowVu : 0.0f), angleOf (part == 0 ? 0.0f : highVu), true);
                g.setColour (part == 0 ? gridLabel : Colours::warn);
                g.strokePath (arc, juce::PathStrokeType (part == 0 ? 1.4f : 4.0f));
            }

            g.setFont (text (12.0f, 1));
            for (float mark : { -20.0f, -10.0f, -5.0f, -3.0f, -1.0f, 0.0f, 1.0f, 3.0f })
            {
                const float angle = angleOf (mark);
                g.setColour (mark > 0.0f ? Colours::warn : gridLabel);
                g.drawLine ({ at (angle, radius * 0.82f), at (angle, radius * 0.9f) }, 1.4f);
                g.drawText ((mark > 0.0f ? "+" : "") + juce::String ((int) mark),
                            juce::Rectangle<float> (28.0f, 13.0f).withCentre (at (angle, radius * 0.99f)), juce::Justification::centred, false);
            }

            const float angle = angleOf (tapeVu);
            juce::Path needle;
            needle.startNewSubPath (at (angle, radius * 0.2f));
            needle.lineTo (at (angle, radius * 0.92f));
            strokeGlow (g, needle, tapeVu > 0.0f ? Colours::warn : Colours::tape, 2.0f);

            g.setColour (Colours::tape);
            g.setFont (text (15.0f, 2));
            const juce::String state = tapeVu < -12.0f ? "LOW LEVEL" : (tapeVu < -3.0f ? "SUBTLE" : (tapeVu < 0.5f ? "ON TAPE"
                                     : (tapeVu < 2.5f ? "PUSHED" : "HITTING IT HARD")));
            g.drawText (strip.isModuleOn (StripModules::tapeIndex) ? state : juce::String ("OFF"),
                        meter.removeFromBottom (18.0f), juce::Justification::centred, false);
        }

        // Right: the response at this speed (a quiet signal; the loud highs soften on top of it)
        auto curve = plot.withTrimmedLeft (plot.getWidth() * 0.4f + 34.0f);
        {
            constexpr float range = 4.0f;
            const auto xOf = [&] (double hz)   { return curve.getX() + curve.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0)); };
            const auto yOf = [&] (float db)    { return curve.getCentreY() - db / range * curve.getHeight() * 0.5f; };

            g.setFont (text (12.0f, 0));
            for (double hz : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
            {
                g.setColour (gridFaint);
                g.fillRect (xOf (hz), curve.getY(), 1.0f, curve.getHeight());
                g.setColour (gridLabel.withAlpha (0.8f));
                g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                            juce::Rectangle<float> (30.0f, 14.0f).withCentre ({ xOf (hz), curve.getBottom() + 10.0f }), juce::Justification::centred, false);
            }
            for (float db : { -3.0f, 0.0f, 3.0f })
            {
                g.setColour (std::abs (db) < 0.01f ? juce::Colour (0xff2a6a8a) : gridFaint);
                g.fillRect (curve.getX(), yOf (db), curve.getWidth(), 1.0f);
                g.setColour (gridLabel);
                g.drawText ((db > 0.0f ? "+" : "") + juce::String ((int) db),
                            juce::Rectangle<float> (curve.getX() - 34.0f, yOf (db) - 7.0f, 29.0f, 14.0f), juce::Justification::centredRight, false);
            }

            juce::Path p;
            for (int i = 0; i <= 200; ++i)
            {
                const double hz = 20.0 * std::pow (1000.0, (double) i / 200.0);
                const juce::Point<float> pt (xOf (hz), yOf (juce::jlimit (-range, range, TapeMachine::responseDb (settings.speed, hz))));
                if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            }
            strokeGlow (g, p, Colours::tape, 2.2f);
        }
    }
}
