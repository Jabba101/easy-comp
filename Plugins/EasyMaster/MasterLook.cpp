#include "MasterLook.h"
#include "Widgets.h"

namespace MasterSkin
{
    void drawModulePanel (juce::Graphics& g, juce::Rectangle<int> area, juce::Colour tint, const juce::String& number, const juce::String& caption)
    {
        Holo::drawModule (g, area.toFloat(), tint);
        Holo::drawHeading (g, { (float) area.getX() + 12.0f, (float) area.getY() + 9.0f }, tint, number.getIntValue(), caption);
    }

    void drawCluster (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour tint, int tone)
    {
        // (light glass, dark, blue-tinted: like the AMEK's grey, light and blue panels - neighbours always differ)
        const juce::Colour fills[3] { juce::Colour (0xff18304a).interpolatedWith (tint, 0.10f), juce::Colour (0xff040a12),
                                      juce::Colour (0xff0b2d4a).interpolatedWith (tint, 0.05f) };
        const float edges[3] { 0.42f, 0.22f, 0.5f };
        const auto shape = Holo::chamfered (r, 7.0f);
        const auto fill = fills[tone % 3];
        g.setGradientFill (juce::ColourGradient (fill.brighter (0.08f), r.getX(), r.getY(), fill.darker (0.15f), r.getX(), r.getBottom(), false));
        g.fillPath (shape);
        g.setColour (tint.withAlpha (edges[tone % 3]));
        g.strokePath (shape, juce::PathStrokeType (1.0f));
    }

    void drawBackdrop (juce::Graphics& g, juce::Rectangle<int> window, juce::Rectangle<int> header)
    {
        const auto all = window.toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0d2338), all.getCentreX(), all.getHeight() * 0.08f,
                                                 juce::Colour (0xff02050a), all.getCentreX(), all.getHeight() * 1.1f, true));
        g.fillAll();

        const auto glass = all.reduced (5.0f);
        const auto shape = Holo::chamfered (glass, 16.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xeb0a1a2a), 0.0f, glass.getY(), juce::Colour (0xf5050d17), 0.0f, glass.getBottom(), false));
        g.fillPath (shape);
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (shape);
            g.setColour (cyan.withAlpha (0.025f));
            for (float x = glass.getX(); x < glass.getRight(); x += 24.0f) g.fillRect (x, glass.getY(), 1.0f, glass.getHeight());
            for (float y = glass.getY(); y < glass.getBottom(); y += 24.0f) g.fillRect (glass.getX(), y, glass.getWidth(), 1.0f);
        }
        g.setColour (cyan.withAlpha (0.10f));
        g.strokePath (shape, juce::PathStrokeType (8.0f));
        g.setColour (cyan.withAlpha (0.6f));
        g.strokePath (shape, juce::PathStrokeType (1.2f));

        const auto h = header.toFloat();
        g.setColour (juce::Colour (0xb3061220));
        g.fillPath (Holo::chamfered (h, 8.0f));
        g.setColour (cyan.withAlpha (0.4f));
        g.strokePath (Holo::chamfered (h.reduced (0.5f), 8.0f), juce::PathStrokeType (1.0f));
    }
}

//==============================================================================
MasterLook::MasterLook()
{
    using namespace MasterSkin;
    setColour (juce::Slider::textBoxTextColourId,          juce::Colours::white);
    setColour (juce::Slider::textBoxHighlightColourId,     cyan.withAlpha (0.35f));
    setColour (juce::Label::textColourId,                  textBright);
    setColour (juce::Label::textWhenEditingColourId,       juce::Colours::white);
    setColour (juce::Label::outlineWhenEditingColourId,    cyan);
    setColour (juce::TextEditor::backgroundColourId,       juce::Colour (0xff04101c));
    setColour (juce::TextEditor::textColourId,             juce::Colours::white);
    setColour (juce::TextEditor::highlightColourId,        cyan.withAlpha (0.35f));
    setColour (juce::TextEditor::outlineColourId,          cyan);
    setColour (juce::TextEditor::focusedOutlineColourId,   cyan);
    setColour (juce::CaretComponent::caretColourId,        cyan);
    setColour (juce::PopupMenu::backgroundColourId,        juce::Colour (0xf2061423));
    setColour (juce::PopupMenu::textColourId,              textBright);
    setColour (juce::PopupMenu::headerTextColourId,        cyan);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, cyan.withAlpha (0.22f));
    setColour (juce::PopupMenu::highlightedTextColourId,   juce::Colours::white);
    setColour (juce::AlertWindow::backgroundColourId,      juce::Colour (0xff061423));
    setColour (juce::AlertWindow::textColourId,            textBright);
    setColour (juce::AlertWindow::outlineColourId,         cyan);
    setColour (juce::TextButton::buttonColourId,           juce::Colour (0x80040c16));
    setColour (juce::TextButton::textColourOffId,          cyan);
}

//==============================================================================
void MasterLook::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                   float startAngle, float endAngle, juce::Slider& slider)
{
    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float size = juce::jmin (area.getWidth(), area.getHeight()) - 4.0f;
    const auto centre = area.getCentre();
    const float thickness = size >= 90.0f ? 6.0f : (size >= 60.0f ? 4.5f : 3.6f);
    const float radius = (size - thickness) * 0.5f - 2.0f;
    const auto tint = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    const auto arc = [&] (float from, float to)
    {
        juce::Path p;
        p.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, from, to, true);
        return p;
    };

    // (a faint disc of glass behind the number)
    g.setColour (juce::Colour (0x66020a14));
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f - thickness, radius * 2.0f - thickness).withCentre (centre));

    g.setColour (Holo::Colours::track);
    g.strokePath (arc (startAngle, endAngle), juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool fromCentre = slider.getProperties().getWithDefault ("fillFromCentre", false);
    const bool fromEnd = slider.getProperties().getWithDefault ("fillFromEnd", false);
    const float from = fromCentre ? (startAngle + endAngle) * 0.5f : (fromEnd ? endAngle : startAngle);
    if (std::abs (angle - from) > 0.01f)
        Holo::strokeGlow (g, arc (juce::jmin (from, angle), juce::jmax (from, angle)), tint, thickness, 0.8f);

    const juce::Point<float> dot (centre.x + radius * std::sin (angle), centre.y - radius * std::cos (angle));
    const float dotRadius = size >= 90.0f ? 5.0f : 3.4f;
    g.setColour (juce::Colour (0xffeaf4ff));
    g.fillEllipse (juce::Rectangle<float> (dotRadius * 2.0f, dotRadius * 2.0f).withCentre (dot));
}

juce::Slider::SliderLayout MasterLook::getSliderLayout (juce::Slider& slider)
{
    auto layout = EasyLookAndFeel::getSliderLayout (slider);
    if (slider.isRotary())
    {
        // The number sits inside the ring (double-click it to type a value)
        const auto b = slider.getLocalBounds();
        const int size = juce::jmin (b.getWidth(), b.getHeight());
        layout.sliderBounds = b;
        layout.textBoxBounds = juce::Rectangle<int> (juce::roundToInt ((float) size * 0.76f), juce::jlimit (24, 44, juce::roundToInt ((float) size * 0.56f)))
                                   .withCentre (b.getCentre());
    }
    return layout;
}

juce::Label* MasterLook::createSliderTextBox (juce::Slider& slider)
{
    auto* box = EasyLookAndFeel::createSliderTextBox (slider);
    box->setFont (Holo::text (17.0f, 1));
    box->setJustificationType (juce::Justification::centred);
    return box;
}

void MasterLook::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float, float,
                                   juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearBar)
    {
        EasyLookAndFeel::drawLinearSlider (g, x, y, width, height, sliderPos, 0.0f, 0.0f, style, slider);
        return;
    }
    // (the multiband's splits: a lit fill and the value on it)
    const auto r = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (0.5f);
    const auto tint = MasterSkin::multiband;
    g.setColour (juce::Colour (0x80040c16));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (tint.withAlpha (0.28f));
    g.fillRoundedRectangle (r.withRight (juce::jlimit (r.getX(), r.getRight(), sliderPos)), 4.0f);
    g.setColour (tint.withAlpha (slider.isMouseOverOrDragging() ? 1.0f : 0.6f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.setColour (juce::Colours::white);
    g.setFont (Holo::text (15.0f, 1));
    g.drawText (slider.getTextFromValue (slider.getValue()), r, juce::Justification::centred, false);
}

void MasterLook::drawLabel (juce::Graphics& g, juce::Label& label)
{
    if (auto* slider = dynamic_cast<juce::Slider*> (label.getParentComponent()))
    {
        // A knob's number: the value big, its unit small underneath (like Easy Strip)
        if (label.isBeingEdited())
        {
            g.setColour (MasterSkin::cyan);
            g.drawRoundedRectangle (label.getLocalBounds().toFloat().reduced (0.5f), 3.0f, 1.0f);
            return;
        }
        if (! slider->isRotary()) return;
        const auto text = label.getText().trim();
        auto number = text, unit = juce::String();
        if (text.containsChar (' '))
        {
            number = text.upToFirstOccurrenceOf (" ", false, false);
            unit = text.fromFirstOccurrenceOf (" ", false, false);
        }
        const auto b = label.getLocalBounds().toFloat();
        const bool twoLines = unit.isNotEmpty() && b.getHeight() >= 24.0f;
        float size = juce::jmin (twoLines ? b.getHeight() * 0.56f : b.getHeight() * 0.72f, 26.0f);
        while (size > 9.0f && Holo::widthOf (Holo::text (size, 1), twoLines ? number : text) > b.getWidth())
            size -= 1.0f;
        g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (slider->isEnabled() ? 1.0f : 0.5f));
        g.setFont (Holo::text (size, 1));
        if (! twoLines)
        {
            g.drawText (text, b, juce::Justification::centred, false);
            return;
        }
        const float unitSize = juce::jlimit (9.0f, 12.5f, b.getHeight() * 0.34f);
        const float total = size + unitSize;
        const float top = b.getCentreY() - total * 0.5f;
        g.drawText (number, juce::Rectangle<float> (b.getX(), top, b.getWidth(), size), juce::Justification::centred, false);
        g.setColour (Holo::Colours::dim);
        g.setFont (Holo::text (unitSize, 0));
        g.drawText (unit, juce::Rectangle<float> (b.getX(), top + size - 1.0f, b.getWidth(), unitSize + 1.0f), juce::Justification::centred, false);
        return;
    }

    // Every other label: Barlow Condensed (labels made in the old fonts a touch bigger - Barlow is narrower)
    if (label.isBeingEdited()) return;
    const auto f = label.getFont();
    const bool alreadyBarlow = f.getTypefaceName().containsIgnoreCase ("Barlow");
    const auto font = alreadyBarlow ? f : Holo::text (f.getHeight() * 1.18f, f.isBold() ? 2 : 0);
    g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (label.isEnabled() ? 1.0f : 0.5f));
    g.setFont (font);
    const auto area = label.getBorderSize().subtractedFrom (label.getLocalBounds());
    g.drawFittedText (label.getText(), area, label.getJustificationType(), juce::jmax (1, (int) ((float) area.getHeight() / font.getHeight())), 0.9f);
}

//==============================================================================
void MasterLook::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool over, bool down)
{
    const auto r = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool isInit = button.getButtonText() == "INIT";
    const auto edge = isInit ? Holo::Colours::warn : button.findColour (juce::TextButton::textColourOffId);
    if (isInit && down)
    {
        g.setColour (Holo::Colours::warn);
        g.fillRoundedRectangle (r, 4.0f);
    }
    else
    {
        g.setColour (isInit ? juce::Colour (0x5928040a) : juce::Colour (0x80040c16));
        g.fillRoundedRectangle (r, 4.0f);
        if (down)
        {
            g.setColour (edge.withAlpha (0.25f));
            g.fillRoundedRectangle (r, 4.0f);
        }
    }
    g.setColour (edge.withAlpha (over ? 1.0f : 0.7f));
    g.drawRoundedRectangle (r, 4.0f, over ? 1.6f : 1.0f);
}

void MasterLook::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool down)
{
    const bool isInit = button.getButtonText() == "INIT";
    auto colour = isInit ? (down ? juce::Colours::white : juce::Colour (0xffff7783)) : button.findColour (juce::TextButton::textColourOffId);
    if (colour == MasterSkin::textBright) colour = MasterSkin::cyan.interpolatedWith (juce::Colours::white, 0.3f);
    g.setColour (colour.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.4f));
    g.setFont (Holo::text (juce::jmin (16.0f, (float) button.getHeight() * 0.68f), isInit ? 2 : 1));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (3, 0), juce::Justification::centred, 1, 0.8f);
}

juce::Font MasterLook::getPopupMenuFont()   { return Holo::text (17.0f, 1); }

void MasterLook::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    g.setColour (MasterSkin::cyan.withAlpha (0.55f));
    g.drawRect (0, 0, width, height, 1);
}

//==============================================================================
void MasterLook::paintLightButton (juce::Graphics& g, juce::Button& button, juce::Colour tint, bool over, bool down)
{
    const auto r = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();
    const bool power = button.getButtonText() == "ON";
    juce::Colour fill, edge, ink;
    if (power)
    {
        fill = on ? tint : juce::Colours::transparentBlack;
        edge = on ? tint : Holo::Colours::offEdge;
        ink  = on ? juce::Colour (0xff04101e) : Holo::Colours::offText;
    }
    else
    {
        fill = on ? tint : juce::Colour (0x80040c16);
        edge = on ? tint : tint.withAlpha (0.55f);
        ink  = on ? juce::Colour (0xff04101e) : tint.interpolatedWith (juce::Colours::white, 0.15f);
    }
    g.setColour (fill);
    g.fillRoundedRectangle (r, 4.0f);
    if (on)
    {
        Holo::paintLampFace (g, r, fill, 4.0f, Holo::lampSeed (button.getButtonText(), button.getBoundsInParent()));
        g.setColour (fill.darker (0.35f).withAlpha (0.8f));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    }
    g.setColour (edge.withMultipliedAlpha (over && ! on ? 1.0f : 0.92f));
    g.drawRoundedRectangle (r, 4.0f, over && ! on ? 1.6f : 1.0f);
    if (down)
    {
        g.setColour (juce::Colours::black.withAlpha (0.15f));
        g.fillRoundedRectangle (r, 4.0f);
    }
    g.setColour (ink.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.4f));
    g.setFont (Holo::text (juce::jmin (power ? 16.0f : 15.0f, r.getHeight() * 0.62f), power ? 2 : 1));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1, 0.8f);
}

void MasterLook::paintHintBar (juce::Graphics& g, juce::Component& bar, const juce::String& title, const juce::String& text)
{
    // The ticker along the bottom (like Easy Strip's)
    auto area = bar.getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xc0061220));
    g.fillPath (Holo::chamfered (area, 8.0f));
    g.setColour (MasterSkin::cyan.withAlpha (0.45f));
    g.strokePath (Holo::chamfered (area.reduced (0.5f), 8.0f), juce::PathStrokeType (1.0f));

    const auto badge = area.removeFromLeft (area.getHeight()).withSizeKeepingCentre (26.0f, 26.0f);
    g.setColour (MasterSkin::cyan);
    g.fillEllipse (badge);
    g.setColour (juce::Colour (0xff04101e));
    g.setFont (Holo::text (19.0f, 2));
    g.drawText ("?", badge, juce::Justification::centred, false);

    juce::AttributedString message;
    message.append (title + "    ", Holo::text (18.0f, 2), Holo::Colours::selected);
    message.append (text, Holo::text (17.5f, 0), MasterSkin::textBright);
    message.setJustification (juce::Justification::centredLeft);
    message.setWordWrap (juce::AttributedString::byWord);
    message.setLineSpacing (1.0f);
    message.draw (g, area.withTrimmedRight (18.0f).reduced (0.0f, 4.0f));
}

void MasterLook::paintPresetBox (juce::Graphics& g, juce::Component& box, const juce::String& shown, bool isEmpty, bool isNaming, bool over)
{
    const auto r = box.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0x99040c16));
    g.fillPath (Holo::chamfered (r, 6.0f));
    g.setColour (MasterSkin::cyan.withAlpha (over ? 0.95f : 0.5f));
    g.strokePath (Holo::chamfered (r, 6.0f), juce::PathStrokeType (1.0f));
    if (isNaming) return;

    auto area = box.getLocalBounds().reduced (12, 0);
    const auto arrowArea = area.removeFromRight (14).toFloat().withSizeKeepingCentre (12.0f, 7.0f);
    juce::Path arrow;
    arrow.startNewSubPath (arrowArea.getTopLeft());
    arrow.lineTo (arrowArea.getCentreX(), arrowArea.getBottom());
    arrow.lineTo (arrowArea.getTopRight());
    Holo::strokeGlow (g, arrow, MasterSkin::cyan, 2.0f, 0.5f);
    area.removeFromRight (8);
    if (isEmpty)
    {
        g.setColour (MasterSkin::textDim);
        g.setFont (Holo::text (18.0f, 1));
        g.drawText (shown, area, juce::Justification::centredLeft, true);
    }
    else
        Holo::drawGlowText (g, shown, area.toFloat(), juce::Justification::centredLeft, Holo::text (18.0f, 1), Holo::Colours::selected, 0.4f);
}

void MasterLook::paintButtonGlows (juce::Graphics& g, juce::Component& root)
{
    std::function<void (juce::Component&)> visit = [&] (juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (! child->isVisible()) continue;
            if (auto* b = dynamic_cast<LightButton*> (child))
            {
                if (b->getToggleState())
                    Holo::drawGlow (g, root.getLocalArea (b, b->getLocalBounds()).toFloat(), b->getLightColour(), 4.0f, 13.0f, 0.75f);
            }
            else if (dynamic_cast<ChoiceSwitch*> (child) != nullptr)
                visit (*child);
        }
    };
    visit (root);
}
