#include "HoloControls.h"

namespace Holo
{
    namespace
    {
        bool sayOff (const juce::RangedAudioParameter& p)
        {
            return p.getCurrentValueAsText().trim().equalsIgnoreCase ("off");
        }

        juce::Path arc (juce::Point<float> centre, float radius, float fromDegrees, float toDegrees)
        {
            juce::Path p;
            p.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                             juce::degreesToRadians (fromDegrees), juce::degreesToRadians (toDegrees), true);
            return p;
        }

        juce::Point<float> onCircle (juce::Point<float> centre, float radius, float degrees)
        {
            const float a = juce::degreesToRadians (degrees);
            return { centre.x + radius * std::sin (a), centre.y - radius * std::cos (a) };
        }
    }

    juce::String formatValue (Format format, float v)
    {
        switch (format)
        {
            case Format::signedDb:      return (v >= -0.05f ? "+" : "") + juce::String (std::abs (v) < 0.05f ? 0.0f : v, 1);
            case Format::db:            return juce::String (v, 1);
            case Format::ratio:         return juce::String (v, 1);
            case Format::msPrecise:     return v < 9.995f ? juce::String (v, 2) : v < 999.95f ? juce::String (v, 1) : juce::String (juce::roundToInt (v));
            case Format::ms:            return v < 0.995f ? juce::String (v, 2) : v < 9.95f ? juce::String (v, 1) : juce::String (juce::roundToInt (v));
            case Format::percent:       return juce::String (juce::roundToInt (v));
            case Format::signedPercent: { const int r = juce::roundToInt (v); return (r > 0 ? "+" : "") + juce::String (r); }
            case Format::hertz:         return v >= 999.5f ? juce::String (v / 1000.0f, v >= 9950.0f ? 0 : 1) + "k" : juce::String (juce::roundToInt (v));
            case Format::whole:         return juce::String (juce::roundToInt (v));
            case Format::asText:        break;
        }
        return juce::String (v, 1);
    }

    //==========================================================================
    ParamControl::ParamControl (juce::RangedAudioParameter& p, const juce::String& t, Format f, const juce::String& u)
        : parameter (p), title (t), unit (u), format (f),
          attachment (p, [this] (float v) { current = v; repaint(); }, nullptr)
    {
        attachment.sendInitialUpdate();
        setRepaintsOnMouseActivity (false);
    }

    juce::String ParamControl::getValueText() const
    {
        const auto words = parameter.getCurrentValueAsText().trim();
        if (sayOff (parameter) || ! words.containsAnyOf ("0123456789"))
            return words.toUpperCase();   // OFF, FULL...
        if (format == Format::asText)
            return parameter.getCurrentValueAsText().toUpperCase();
        return formatValue (format, current);
    }

    juce::String ParamControl::getUnit() const
    {
        return getValueText().containsAnyOf ("0123456789") ? unit : juce::String();
    }

    Host* ParamControl::host()   { return findParentComponentOfClass<Host>(); }

    void ParamControl::startMove()
    {
        if (moving)
            return;
        moving = true;
        attachment.beginGesture();
        if (partner != nullptr && linked && linked())
            partner->attachment.beginGesture();
    }

    void ParamControl::endMove()
    {
        if (! moving)
            return;
        moving = false;
        attachment.endGesture();
        if (partner != nullptr)
            partner->attachment.endGesture();
        if (auto* h = host())
            h->valueShown();
    }

    void ParamControl::moveBy (float newNormalised)
    {
        newNormalised = juce::jlimit (0.0f, 1.0f, newNormalised);

        if (partner != nullptr && linked && linked())
        {
            // Both move by the same amount, stopping together at either end
            const float otherNow = partner->getNormalised();
            float delta = newNormalised - getNormalised();
            delta = juce::jlimit (-otherNow, 1.0f - otherNow, delta);
            attachment.setValueAsPartOfGesture (parameter.convertFrom0to1 (getNormalised() + delta));
            partner->attachment.setValueAsPartOfGesture (partner->parameter.convertFrom0to1 (otherNow + delta));
        }
        else
        {
            attachment.setValueAsPartOfGesture (parameter.convertFrom0to1 (newNormalised));
        }

        if (auto* h = host())
        {
            h->settingMoved (parameter);
            h->showValue (title.replace ("|", " ").replace ("\n", " "), parameter.getCurrentValueAsText());
        }
    }

    void ParamControl::setFromUser (float normalised)
    {
        startMove();
        moveBy (normalised);
        endMove();
    }

    void ParamControl::resetToDefault()
    {
        startMove();
        attachment.setValueAsPartOfGesture (parameter.convertFrom0to1 (parameter.getDefaultValue()));
        if (auto* h = host())
            h->settingMoved (parameter);
        endMove();
    }

    void ParamControl::linkWith (ParamControl& other, std::function<bool()> isLinked)
    {
        partner = &other;
        linked = isLinked;
        other.partner = this;
        other.linked = isLinked;
    }

    void ParamControl::mouseDown (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
            return;
        dragStartY = e.position.y;
        dragStartValue = getNormalised();
        startMove();
        if (auto* h = host())
            h->showValue (title.replace ("|", " ").replace ("\n", " "), parameter.getCurrentValueAsText());
    }

    void ParamControl::mouseDrag (const juce::MouseEvent& e)
    {
        if (! moving)
            return;
        const float pixelsForFullRange = e.mods.isShiftDown() ? 800.0f : 180.0f;
        moveBy (dragStartValue + (dragStartY - e.position.y) / pixelsForFullRange);
    }

    void ParamControl::mouseUp (const juce::MouseEvent&)
    {
        endMove();
    }

    void ParamControl::mouseDoubleClick (const juce::MouseEvent& e)
    {
        endMove();

        if (! getNumberArea().expanded (4.0f, 6.0f).contains (e.position))
        {
            resetToDefault();   // the control itself: back to its default
            return;
        }

        // The number: type a value
        if (auto* h = host())
        {
            juce::Component::SafePointer<ParamControl> self (this);
            h->typeValue (*this, getNumberArea().toNearestInt(), getValueText(), [self] (const juce::String& typed)
            {
                if (self == nullptr)
                    return;

                auto& p = self->parameter;
                const auto text = typed.trim();
                float normalised;

                if (text.equalsIgnoreCase ("off"))
                {
                    // OFF is at whichever end the setting calls OFF
                    if (p.getText (0.0f, 32).trim().equalsIgnoreCase ("off"))       normalised = 0.0f;
                    else if (p.getText (1.0f, 32).trim().equalsIgnoreCase ("off"))  normalised = 1.0f;
                    else return;
                }
                else
                {
                    if (! text.containsAnyOf ("0123456789"))
                        return;
                    normalised = juce::jlimit (0.0f, 1.0f, p.getValueForText (text));
                }

                self->setFromUser (normalised);
            });
        }
    }

    void ParamControl::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        const float direction = (std::abs (wheel.deltaY) > 1.0e-6f ? wheel.deltaY : -wheel.deltaX) * (wheel.isReversed ? -1.0f : 1.0f);
        if (std::abs (direction) < 1.0e-6f)
            return;
        setFromUser (getNormalised() + (direction > 0.0f ? 1.0f : -1.0f) * (e.mods.isShiftDown() ? 0.005f : 0.02f));
    }

    //==========================================================================
    Ring::Ring (juce::RangedAudioParameter& p, const juce::String& t, Format f, const juce::String& u,
                Size s, juce::Colour c, bool show)
        : ParamControl (p, t, f, u), size (s), showTitle (show)
    {
        tint = c;
        setSize (getIdealWidth(), getIdealHeight());
    }

    int Ring::getIdealWidth() const
    {
        float w = (float) size;
        if (showTitle)
            for (auto& line : juce::StringArray::fromTokens (title, "|", {}))
                w = juce::jmax (w, widthOf (text (14.0f), line) + 4.0f);
        return (int) std::ceil (w);
    }

    int Ring::getIdealHeight() const
    {
        const int lines = showTitle ? juce::StringArray::fromTokens (title, "|", {}).size() : 0;
        return lines * 15 + (lines > 0 ? 3 : 0) + (int) std::ceil ((float) size * 0.92f);
    }

    juce::Rectangle<float> Ring::ringArea() const
    {
        const float sz = (float) size;
        return juce::Rectangle<float> (sz, sz * 0.92f).withCentre ({ getWidth() * 0.5f, (float) getHeight() - sz * 0.46f });
    }

    juce::Rectangle<float> Ring::getNumberArea() const
    {
        const auto area = ringArea();
        const float r = (float) size * 0.4f * 0.72f;
        return juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre ({ area.getCentreX(), area.getY() + (float) size * 0.52f });
    }

    void Ring::paint (juce::Graphics& g)
    {
        const float sz = (float) size;
        const auto area = ringArea();
        const juce::Point<float> centre (area.getCentreX(), area.getY() + sz * 0.52f);
        const float radius = sz * 0.4f;
        const float thickness = size >= big ? 6.0f : 4.0f;
        const float n = getNormalised();
        const float angle = -135.0f + 270.0f * n;

        if (showTitle)
        {
            g.setColour (Colours::ink);
            g.setFont (text (14.0f, 1));
            auto lines = juce::StringArray::fromTokens (title, "|", {});
            for (int i = 0; i < lines.size(); ++i)
                g.drawText (lines[i], juce::Rectangle<float> (0.0f, (float) i * 15.0f, (float) getWidth(), 15.0f), juce::Justification::centred, false);
        }

        g.setColour (Colours::track);
        g.strokePath (arc (centre, radius, -135.0f, 135.0f), juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float from = centred ? 0.0f : -135.0f;
        if (std::abs (angle - from) > 0.6f)
            strokeGlow (g, arc (centre, radius, juce::jmin (from, angle), juce::jmax (from, angle)), tint, thickness, 0.8f);

        const auto dot = onCircle (centre, radius, angle);
        const float dotRadius = size >= big ? 5.0f : 3.2f;
        g.setColour (juce::Colour (0xffeaf4ff));
        g.fillEllipse (juce::Rectangle<float> (dotRadius * 2.0f, dotRadius * 2.0f).withCentre (dot));

        float fontSize = size == large ? 28.0f : size == big ? 25.0f : size == small ? 16.0f : size == tiny ? 15.0f : 18.0f;
        auto valueText = getValueText();
        if (format == Format::asText)
        {
            // Note lengths, compact: "1/8 NOTE" -> "1/8", "DOTTED 1/8" -> "1/8 D", "1/16 TRIPLET" -> "1/16 T"
            valueText = valueText.replace (" NOTE", "").replace (" TRIPLET", " T").replace ("WHOLE", "1/1");
            if (valueText.startsWith ("DOTTED "))
                valueText = valueText.fromFirstOccurrenceOf ("DOTTED ", false, false) + " D";
        }
        while (fontSize > 9.0f && widthOf (text (fontSize, 1), valueText) > sz * 0.82f)
            fontSize -= 1.0f;   // (shrinks to fit inside the ring)
        g.setColour (Colours::value);
        g.setFont (text (fontSize, 1));
        g.drawText (valueText, juce::Rectangle<float> (sz, fontSize).withCentre (centre), juce::Justification::centred, false);

        const auto u = getUnit();
        if (u.isNotEmpty())
        {
            g.setColour (Colours::dim);
            g.setFont (text (11.5f, 0));
            g.drawText (u, juce::Rectangle<float> (sz, 12.0f).withCentre (centre.translated (0.0f, fontSize * 0.32f + 8.0f)), juce::Justification::centred, false);
        }
    }

    //==========================================================================
    Bar::Bar (juce::RangedAudioParameter& p, const juce::String& t, Format f, const juce::String& u, juce::Colour c, bool centre)
        : ParamControl (p, t, f, u), centreZero (centre)
    {
        tint = c;
    }

    juce::Rectangle<float> Bar::getBarArea() const
    {
        auto r = getLocalBounds().toFloat();
        if (showLabels)
        {
            r.removeFromTop (19.0f);
            r.removeFromBottom (16.0f);
        }
        return r;
    }

    juce::Rectangle<float> Bar::getNumberArea() const
    {
        const auto bar = getBarArea();
        const float n = getNormalised();
        float y;
        if (centreZero)
            y = n >= 0.5f ? bar.getBottom() - bar.getHeight() * juce::jmin (n, 0.93f) + 2.0f
                          : bar.getBottom() - bar.getHeight() * juce::jmax (n, 0.07f) - 2.0f - 20.0f;
        else
            y = bar.getBottom() - bar.getHeight() * juce::jlimit (0.12f, 0.95f, n) - (n < 0.12f ? -10.0f : 2.0f) ;

        if (! centreZero)
            y += 2.0f;
        return { bar.getX(), juce::jlimit (bar.getY(), bar.getBottom() - 20.0f, y), bar.getWidth(), 20.0f };
    }

    void Bar::paint (juce::Graphics& g)
    {
        const auto bar = getBarArea();
        const float n = getNormalised();

        if (showLabels)
        {
            g.setColour (Colours::ink);
            g.setFont (text (14.0f, 1));
            g.drawText (title, getLocalBounds().toFloat().removeFromTop (17.0f), juce::Justification::centred, false);
            g.setColour (Colours::dim);
            g.setFont (text (12.0f, 0));
            g.drawText (getUnit(), getLocalBounds().toFloat().removeFromBottom (14.0f), juce::Justification::centred, false);
        }

        g.setColour (juce::Colour (0xb3040e1a));
        g.fillRoundedRectangle (bar, 3.0f);

        const float yN = bar.getBottom() - bar.getHeight() * n;
        const float yMid = bar.getCentreY();
        juce::Rectangle<float> fill;

        if (centreZero)
            fill = juce::Rectangle<float>::leftTopRightBottom (bar.getX(), juce::jmin (yN, yMid), bar.getRight(), juce::jmax (yN, yMid));
        else
            fill = bar.withTop (yN);

        if (! fill.isEmpty())
        {
            g.setGradientFill (juce::ColourGradient (tint.withAlpha (0.70f), 0.0f, bar.getBottom(), tint.withAlpha (0.32f), 0.0f, bar.getY(), false));
            g.fillRect (fill.reduced (1.0f, 0.0f));
        }

        // The fill line
        if (! centreZero || std::abs (n - 0.5f) > 0.002f)
        {
            g.setColour (tint.withAlpha (0.45f));
            g.fillRect (bar.getX() + 1.0f, yN - 3.0f, bar.getWidth() - 2.0f, 6.0f);
            g.setColour (juce::Colour (0xffe2efff));
            g.fillRect (bar.getX() + 1.0f, yN - 1.0f, bar.getWidth() - 2.0f, 2.0f);
        }

        if (centreZero)
        {
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            const float dashes[] = { 3.0f, 3.0f };
            g.drawDashedLine (juce::Line<float> (bar.getX(), yMid, bar.getRight(), yMid), dashes, 2, 1.0f);
        }

        g.setColour (tint.withAlpha (0.55f));
        g.drawRoundedRectangle (bar, 3.0f, 1.0f);

        // The value, at the line
        const auto numberArea = getNumberArea();
        g.setFont (text (17.0f, 1));
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawText (getValueText(), numberArea.translated (0.0f, 1.0f), juce::Justification::centred, false);
        g.setColour (Colours::value);
        g.drawText (getValueText(), numberArea, juce::Justification::centred, false);
    }

    //==========================================================================
    Button::Button (const juce::String& t, juce::Colour c, Style s)
        : juce::Button (t), tint (c), style (s)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void Button::paintButton (juce::Graphics& g, bool over, bool down)
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool on = getToggleState() || flash || (style == Style::init && down);
        const auto lines = juce::StringArray::fromTokens (getButtonText(), "|", {});
        const float corner = style == Style::square ? 4.0f : 4.0f;

        juce::Colour fill, edge, ink;

        switch (style)
        {
            case Style::init:
                fill = on ? Colours::warn : juce::Colour (0x5928040a);
                edge = Colours::warn;
                ink  = on ? juce::Colours::white : juce::Colour (0xffff7783);
                break;

            case Style::power:
                fill = on ? tint : juce::Colours::transparentBlack;
                edge = on ? tint : Colours::offEdge;
                ink  = on ? juce::Colour (0xff04101e) : Colours::offText;
                break;

            case Style::view:
                fill = on ? Colours::selected.withAlpha (0.22f) : juce::Colour (0x80040c16);
                edge = on ? Colours::selected : Colours::cyan.withAlpha (0.45f);
                ink  = on ? juce::Colour (0xffffe0b8) : juce::Colour (0xffcfeaf5);
                break;

            case Style::plain:
            case Style::square:
                fill = on ? tint : juce::Colour (0x80040c16);
                edge = on ? tint : tint.withAlpha (0.55f);
                ink  = on ? juce::Colour (0xff04101e) : tint.interpolatedWith (juce::Colours::white, 0.15f);
                break;
        }

        if (on && style != Style::view)
        {
            g.setColour (fill.withAlpha (0.18f));
            g.fillRoundedRectangle (r.expanded (3.0f), corner + 3.0f);
        }
        else if (on)
        {
            g.setColour (Colours::selected.withAlpha (0.16f));
            g.fillRoundedRectangle (r.expanded (3.0f), corner + 3.0f);
        }

        g.setColour (fill);
        g.fillRoundedRectangle (r, corner);
        g.setColour (edge.withMultipliedAlpha (over && ! on ? 1.0f : 0.92f));
        g.drawRoundedRectangle (r, corner, over && ! on ? 1.6f : 1.0f);

        const int weight = style == Style::power || style == Style::init || style == Style::square ? 2 : 1;
        const float size = lines.size() > 1 ? juce::jmin (fontSize, r.getHeight() / (float) lines.size() - 2.0f) : fontSize;
        g.setFont (displayFont ? Holo::display (size) : Holo::text (size, weight));
        if (displayFont)
        {
            fill = juce::Colour (0xff0a0c10);
            g.setColour (fill);
            g.fillRoundedRectangle (r, corner);
            if (on)   // (lit: the text glows in the tint on the dark face, e.g. WEAPON SELECT while the rack is open)
            {
                g.setColour (tint.withAlpha (0.22f));
                g.fillRoundedRectangle (r, corner);
                ink = tint.interpolatedWith (juce::Colours::white, 0.35f);
            }
            g.setColour (edge);
            g.drawRoundedRectangle (r, corner, 1.0f);
        }
        g.setColour (ink.withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f));

        const float lineHeight = size * 1.02f;
        const float top = r.getCentreY() - lineHeight * (float) lines.size() * 0.5f;
        for (int i = 0; i < lines.size(); ++i)
            g.drawText (lines[i], juce::Rectangle<float> (r.getX(), top + lineHeight * (float) i, r.getWidth(), lineHeight),
                        juce::Justification::centred, false);
    }

    //==========================================================================
    ButtonLink::ButtonLink (juce::RangedAudioParameter& p, juce::Button& b, bool invert)
        : parameter (p), button (b), inverted (invert),
          attachment (p, [this] (float v) { button.setToggleState ((v >= 0.5f) != inverted, juce::dontSendNotification); }, nullptr)
    {
        button.setClickingTogglesState (false);
        button.addListener (this);
        attachment.sendInitialUpdate();
    }

    ButtonLink::~ButtonLink()
    {
        button.removeListener (this);
    }

    void ButtonLink::buttonClicked (juce::Button*)
    {
        const bool nowOn = ! button.getToggleState();
        attachment.setValueAsCompleteGesture ((nowOn != inverted) ? 1.0f : 0.0f);
        if (auto* host = button.findParentComponentOfClass<Host>())
            host->settingMoved (parameter);
    }

    //==========================================================================
    Choice::Choice (juce::RangedAudioParameter& p, const juce::StringArray& n, juce::Colour c, bool side)
        : tint (c), parameter (&p), names (n), sideBySide (side)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (p, [this] (float v)
        {
            chosen = juce::jlimit (0, names.size() - 1, juce::roundToInt (v));
            repaint();
        }, nullptr);
        attachment->sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    Choice::Choice (const juce::StringArray& n, juce::Colour c, bool side)
        : tint (c), names (n), sideBySide (side)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    juce::Rectangle<float> Choice::segment (int i) const
    {
        const float gap = 3.0f;
        const int count = names.size();
        if (sideBySide)
        {
            const float w = ((float) getWidth() - gap * (float) (count - 1)) / (float) count;
            return { (w + gap) * (float) i, 0.0f, w, (float) getHeight() };
        }
        const float h = ((float) getHeight() - gap * (float) (count - 1)) / (float) count;
        return { 0.0f, (h + gap) * (float) i, (float) getWidth(), h };
    }

    void Choice::paint (juce::Graphics& g)
    {
        for (int i = 0; i < names.size(); ++i)
        {
            const auto r = segment (i).reduced (0.5f);
            const bool on = i == chosen;

            if (on)
            {
                g.setColour (tint.withAlpha (0.18f));
                g.fillRoundedRectangle (r.expanded (2.5f), 5.0f);
                g.setColour (tint);
                g.fillRoundedRectangle (r, 3.0f);
            }

            g.setColour (on ? tint : tint.withAlpha (0.5f));
            g.drawRoundedRectangle (r, 3.0f, 1.0f);
            g.setColour (on ? juce::Colour (0xff04101e) : tint.interpolatedWith (juce::Colours::white, 0.15f));
            g.setFont (text (fontSize, 1));
            g.drawText (names[i], r, juce::Justification::centred, false);
        }
    }

    void Choice::choose (int index)
    {
        index = juce::jlimit (0, names.size() - 1, index);
        if (attachment != nullptr)
        {
            attachment->setValueAsCompleteGesture ((float) index);
            if (auto* host = findParentComponentOfClass<Host>())
                host->settingMoved (*parameter);
        }
        else
        {
            setChosen (index);
        }
        if (onUserChoice)
            onUserChoice (index);
    }

    void Choice::mouseDown (const juce::MouseEvent& e)
    {
        for (int i = 0; i < names.size(); ++i)
            if (segment (i).contains (e.position))
                choose (i);
    }

    //==========================================================================
    void Lamp::setLit (float amount)
    {
        amount = juce::jlimit (0.0f, 1.0f, amount);
        if (std::abs (amount - lit) > 0.01f)
        {
            lit = amount;
            repaint();
        }
    }

    void Lamp::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat().withSizeKeepingCentre (12.0f, 12.0f);
        if (lit > 0.0f)
        {
            g.setColour (colour.withAlpha (0.25f * lit));
            g.fillEllipse (r.expanded (4.0f));
            g.setColour (colour.withAlpha (lit));
            g.fillEllipse (r);
        }
        g.setColour (colour);
        g.drawEllipse (r, 1.0f);
    }

    //==========================================================================
    Meter::Meter (float lo, float hi, std::vector<float> m)
        : lowest (lo), highest (hi), marks (std::move (m))
    {
        setInterceptsMouseClicks (false, false);
    }

    void Meter::setLevels (float l, float r)
    {
        const float fall = 1.2f;
        const float before[2] { shown[0], shown[1] };
        shown[0] = juce::jmax (juce::jmax (l, lowest - 1.0f), shown[0] - fall);
        shown[1] = juce::jmax (juce::jmax (r, lowest - 1.0f), shown[1] - fall);
        if (std::abs (before[0] - shown[0]) > 0.05f || std::abs (before[1] - shown[1]) > 0.05f)
            repaint();
    }

    void Meter::paint (juce::Graphics& g)
    {
        const auto area = getLocalBounds().toFloat();
        const auto yFor = [&] (float db) { return area.getY() + area.getHeight() * (1.0f - juce::jlimit (0.0f, 1.0f, (db - lowest) / (highest - lowest))); };

        g.setFont (text (11.5f, 0));
        g.setColour (Colours::dim);
        for (auto m : marks)
            g.drawText ((m > 0.0f ? "+" : "") + juce::String (juce::roundToInt (m)),
                        juce::Rectangle<float> (area.getX(), juce::jlimit (area.getY(), area.getBottom() - 12.0f, yFor (m) - 6.0f), 22.0f, 12.0f),
                        juce::Justification::centredRight, false);

        for (int ch = 0; ch < 2; ++ch)
        {
            const juce::Rectangle<float> bar (area.getRight() - 8.0f - (1 - (float) ch) * 11.0f, area.getY(), 8.0f, area.getHeight());
            g.setColour (juce::Colour (0xb3040e1a));
            g.fillRoundedRectangle (bar, 2.0f);
            const float top = yFor (shown[ch]);
            if (top < bar.getBottom())
            {
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2fe0ff), 0.0f, bar.getBottom(), juce::Colour (0xffff4a5a), 0.0f, bar.getY(), false));
                g.fillRect (bar.withTop (top).reduced (1.0f, 0.0f));
            }
            g.setColour (juce::Colour (0xff1c3448));
            g.drawRoundedRectangle (bar, 2.0f, 1.0f);
        }

        if (showLine)
        {
            const float y = yFor (lineDb);
            const float dashes[] = { 4.0f, 3.0f };
            g.setColour (lineColour);
            g.drawDashedLine (juce::Line<float> (area.getX() + 24.0f, y, area.getRight(), y), dashes, 2, 1.2f);
        }
    }

    //==========================================================================
    GainBar::GainBar (float r, bool c, juce::Colour col) : range (r), centreZero (c), colour (col)
    {
        setInterceptsMouseClicks (false, false);
    }

    void GainBar::setGainDb (float db)
    {
        const float before = shownDb;
        if (centreZero)
            shownDb = std::abs (db) >= std::abs (shownDb) ? db : shownDb * 0.82f;
        else
            shownDb = db >= shownDb ? db : juce::jmax (db, shownDb - 0.8f);
        if (std::abs (shownDb) < 0.01f) shownDb = 0.0f;
        if (std::abs (before - shownDb) > 0.02f)
            repaint();
    }

    void GainBar::paint (juce::Graphics& g)
    {
        const auto bar = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (juce::Colour (0xb3040e1a));
        g.fillRoundedRectangle (bar, 2.0f);

        if (centreZero)
        {
            const float half = bar.getHeight() * 0.5f;
            const float h = half * juce::jlimit (0.0f, 1.0f, std::abs (shownDb) / range);
            const auto fill = shownDb > 0.0f ? juce::Rectangle<float> (bar.getX(), bar.getCentreY() - h, bar.getWidth(), h)
                                             : juce::Rectangle<float> (bar.getX(), bar.getCentreY(), bar.getWidth(), h);
            g.setColour (shownDb > 0.0f ? colour : Colours::warn);
            g.fillRect (fill.reduced (1.0f, 0.0f));
            g.setColour (juce::Colours::white.withAlpha (0.4f));
            g.fillRect (bar.getX(), bar.getCentreY() - 0.5f, bar.getWidth(), 1.0f);
        }
        else
        {
            const float h = bar.getHeight() * juce::jlimit (0.0f, 1.0f, shownDb / range);
            g.setGradientFill (juce::ColourGradient (colour, 0.0f, bar.getY(), colour.withAlpha (0.55f), 0.0f, bar.getY() + juce::jmax (1.0f, h), false));
            g.fillRect (bar.withHeight (h).reduced (1.0f, 0.0f));
        }

        g.setColour (colour.withAlpha (0.55f));
        g.drawRoundedRectangle (bar, 2.0f, 1.0f);
    }

    //==========================================================================
    float BigMeter::positionOf (float db)
    {
        db = juce::jlimit (-24.0f, 12.0f, db);
        return db >= 0.0f ? (1.0f / 3.0f) * (1.0f - db / 12.0f) : 1.0f / 3.0f + (2.0f / 3.0f) * (-db / 24.0f);
    }

    void BigMeter::setLevels (float l, float r)
    {
        const float levels[2] { juce::jmax (l, -30.0f), juce::jmax (r, -30.0f) };
        bool changed = false;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float was = shown[ch], heldWas = held[ch];
            shown[ch] = juce::jmax (levels[ch], shown[ch] - 1.0f);
            if (levels[ch] >= held[ch])   { held[ch] = levels[ch]; holdFor[ch] = 45; }
            else if (--holdFor[ch] < 0)   held[ch] = juce::jmax (levels[ch], held[ch] - 0.4f);
            changed = changed || std::abs (was - shown[ch]) > 0.05f || std::abs (heldWas - held[ch]) > 0.05f;
        }
        if (changed)
            repaint();
    }

    void BigMeter::paint (juce::Graphics& g)
    {
        const auto area = getLocalBounds().toFloat();
        const auto yFor = [&] (float db) { return area.getY() + area.getHeight() * positionOf (db); };

        g.setFont (text (11.5f, 0));
        for (float db : { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f })
        {
            const bool zero = std::abs (db) < 0.01f;
            g.setColour (zero ? Colours::value : Colours::dim);
            g.setFont (text (zero ? 12.5f : 11.5f, zero ? 2 : 0));
            g.drawText ((db > 0.0f ? "+" : "") + juce::String (juce::roundToInt (db)),
                        juce::Rectangle<float> (area.getX(), juce::jlimit (area.getY(), area.getBottom() - 12.0f, yFor (db) - 6.0f), 22.0f, 12.0f),
                        juce::Justification::centredRight, false);
        }

        const float left = area.getX() + 27.0f;
        const float width = (area.getRight() - 3.0f - left - 3.0f) * 0.5f;
        const float zeroY = yFor (0.0f);

        for (int ch = 0; ch < 2; ++ch)
        {
            const juce::Rectangle<float> bar (left + (width + 3.0f) * (float) ch, area.getY(), width, area.getHeight());
            g.setColour (juce::Colour (0xb3040e1a));
            g.fillRoundedRectangle (bar, 2.0f);

            const float top = yFor (shown[ch]);
            if (shown[ch] > -24.0f)
            {
                juce::ColourGradient grad (juce::Colour (0xff2fe0ff), 0.0f, bar.getBottom(), juce::Colour (0xffff4a5a), 0.0f, bar.getY(), false);
                const float zeroAt = 1.0f - positionOf (0.0f);
                grad.addColour (zeroAt * 0.94, juce::Colour (0xff2fe0ff));
                grad.addColour (zeroAt + 0.03, juce::Colour (0xffffcf4a));
                g.setGradientFill (grad);
                g.fillRect (bar.withTop (top).reduced (1.0f, 0.0f));
            }

            if (held[ch] > -24.0f)
            {
                g.setColour (held[ch] > 0.0f ? Colours::warn : Colours::value.withAlpha (0.85f));
                g.fillRect (bar.getX() + 1.0f, yFor (held[ch]) - 1.0f, bar.getWidth() - 2.0f, 2.0f);
            }

            g.setColour (juce::Colour (0xff1c3448));
            g.drawRoundedRectangle (bar, 2.0f, 1.0f);
        }

        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.fillRect (left - 2.0f, zeroY - 0.5f, area.getRight() - left - 1.0f, 1.0f);
    }
}
