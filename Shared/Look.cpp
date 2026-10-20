#include "Look.h"

//==============================================================================
EasyLookAndFeel::EasyLookAndFeel()
{
   #if JUCE_WINDOWS
    setDefaultSansSerifTypefaceName ("Segoe UI");
   #endif

    setColour (juce::ResizableWindow::backgroundColourId,      Palette::background);

    setColour (juce::Slider::rotarySliderFillColourId,         Palette::accent);
    setColour (juce::Slider::rotarySliderOutlineColourId,      Palette::knobTrack);
    setColour (juce::Slider::thumbColourId,                    Palette::textBright);
    setColour (juce::Slider::textBoxTextColourId,              Palette::textBright);
    setColour (juce::Slider::textBoxBackgroundColourId,        juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId,           juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId,         Palette::accent.withAlpha (0.35f));

    setColour (juce::Label::textColourId,                      Palette::textBright);
    setColour (juce::Label::textWhenEditingColourId,           Palette::textBright);
    setColour (juce::Label::outlineWhenEditingColourId,        Palette::accent);
    setColour (juce::TextEditor::backgroundColourId,           Palette::knobBody);
    setColour (juce::TextEditor::textColourId,                 Palette::textBright);
    setColour (juce::TextEditor::highlightColourId,            Palette::accent.withAlpha (0.35f));
    setColour (juce::TextEditor::highlightedTextColourId,      Palette::textBright);
    setColour (juce::TextEditor::outlineColourId,              Palette::accent);
    setColour (juce::TextEditor::focusedOutlineColourId,       Palette::accent);
    setColour (juce::CaretComponent::caretColourId,            Palette::accent);

    setColour (juce::PopupMenu::backgroundColourId,            Palette::panel);
    setColour (juce::PopupMenu::textColourId,                  Palette::textBright);
    setColour (juce::PopupMenu::headerTextColourId,            Palette::textDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::accent);
    setColour (juce::PopupMenu::highlightedTextColourId,       Palette::background);
}

void EasyLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float startAngle, float endAngle,
                                        juce::Slider& slider)
{
    const auto area   = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
    const float size  = juce::jmin (area.getWidth(), area.getHeight());
    const auto centre = area.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    const float trackThickness = juce::jmax (4.0f, size * 0.07f);
    const float arcRadius      = (size - trackThickness) * 0.5f;
    const juce::PathStrokeType stroke (trackThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    // A switched-off knob (e.g. MAKE-UP while AUTO-GAIN is on) is drawn faded.
    const float alpha = slider.isEnabled() ? 1.0f : 0.3f;

    // A knob whose middle is "no change" (like a transient shaper's PUNCH)
    // fills from the top; other knobs fill from the left end.
    const bool fromCentre = slider.getProperties().getWithDefault ("fillFromCentre", false);
    const float fillStart = fromCentre ? (startAngle + endAngle) * 0.5f : startAngle;

    // The grey track that the value travels along
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId).withMultipliedAlpha (alpha));
    g.strokePath (track, stroke);

    // The coloured part showing the current value
    if (std::abs (angle - fillStart) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             juce::jmin (fillStart, angle), juce::jmax (fillStart, angle), true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId).withMultipliedAlpha (alpha));
        g.strokePath (value, stroke);
    }

    // The knob itself
    const float bodyRadius = arcRadius - trackThickness * 1.5f;
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);
    g.setColour (Palette::knobBody.withMultipliedAlpha (alpha));
    g.fillEllipse (body);
    g.setColour (Palette::panelEdge.brighter (0.25f).withMultipliedAlpha (alpha));
    g.drawEllipse (body, 1.0f);

    // The pointer line
    const float pointerWidth = juce::jmax (2.5f, size * 0.03f);
    juce::Path pointer;
    pointer.addRoundedRectangle (-pointerWidth * 0.5f, -bodyRadius * 0.82f,
                                 pointerWidth, bodyRadius * 0.5f, pointerWidth * 0.5f);
    g.setColour (slider.findColour (juce::Slider::thumbColourId).withMultipliedAlpha (alpha));
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

juce::Label* EasyLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* box = LookAndFeel_V4::createSliderTextBox (slider);
    box->setFont (juce::Font (juce::FontOptions (15.0f)));
    return box;
}

void EasyLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    // The value boxes under the knobs get a soft rounded background, so they
    // look like something you can click and type into.
    if (dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr)
    {
        const auto bounds = label.getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (Palette::background);
        g.fillRoundedRectangle (bounds, 7.0f);

        if (label.isBeingEdited())
        {
            g.setColour (Palette::accent);
            g.drawRoundedRectangle (bounds, 7.0f, 1.0f);
        }
        else
        {
            g.setColour (label.findColour (juce::Label::textColourId));
            g.setFont (getLabelFont (label));
            g.drawFittedText (label.getText(), label.getLocalBounds(), juce::Justification::centred, 1);
        }

        return;
    }

    LookAndFeel_V4::drawLabel (g, label);
}

juce::Font EasyLookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (15.0f));
}

//==============================================================================
void drawPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& number, const juce::String& caption)
{
    const auto bounds = area.toFloat();

    g.setColour (Palette::panel);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (Palette::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    auto captionArea = area.reduced (14, 0).withTrimmedTop (10).withHeight (22);
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));

    if (number.isNotEmpty())
    {
        g.setColour (Palette::accent);
        g.drawText (number, captionArea.removeFromLeft (16), juce::Justification::centredLeft, false);
        g.setColour (Palette::textDim);
        g.drawText (caption, captionArea, juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour (Palette::textDim);
        g.drawText (caption, captionArea, juce::Justification::centred, false);
    }
}
