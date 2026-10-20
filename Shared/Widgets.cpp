#include "Widgets.h"

//==============================================================================
// Hint bar
//==============================================================================
void HintBar::setHint (const juce::String& newTitle, const juce::String& newText)
{
    if (newTitle == title && newText == text)
        return;

    title = newTitle;
    text  = newText;
    repaint();
}

void HintBar::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    g.setColour (Palette::panel);
    g.fillRoundedRectangle (area, 10.0f);

    // The "?" badge
    const auto badge = area.removeFromLeft (area.getHeight()).withSizeKeepingCentre (26.0f, 26.0f);
    g.setColour (Palette::accent);
    g.fillEllipse (badge);
    g.setColour (Palette::background);
    g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
    g.drawText ("?", badge, juce::Justification::centred, false);

    // Title in amber, then the explanation
    juce::AttributedString message;
    message.append (title + "    ", juce::Font (juce::FontOptions (14.5f, juce::Font::bold)), Palette::accent);
    message.append (text, juce::Font (juce::FontOptions (14.5f)), Palette::textBright);
    message.setJustification (juce::Justification::centredLeft);
    message.setWordWrap (juce::AttributedString::byWord);
    message.setLineSpacing (2.0f);
    message.draw (g, area.withTrimmedRight (18.0f).reduced (0.0f, 6.0f));
}

//==============================================================================
// Light-up buttons
//==============================================================================
LightButton::LightButton (const juce::String& buttonText, juce::Colour colourWhenOn)
    : juce::Button (buttonText), lightColour (colourWhenOn)
{
    setClickingTogglesState (true);   // each click flips it on/off
}

void LightButton::paintButton (juce::Graphics& g, bool isMouseOver, bool isMouseDown)
{
    const bool on = getToggleState();
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (on ? lightColour.withAlpha (0.14f) : Palette::panel);
    g.fillRoundedRectangle (bounds, 7.0f);

    g.setColour (on ? lightColour : (isMouseOver ? Palette::textDim : Palette::panelEdge));
    g.drawRoundedRectangle (bounds, 7.0f, 1.0f);

    // The LED, with a soft glow when lit (a little tighter on narrow buttons)
    const bool narrow = getWidth() < 110;
    auto content = bounds.reduced (narrow ? 9.0f : 12.0f, 0.0f);
    const auto led = content.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 10.0f);

    if (on)
    {
        g.setColour (lightColour.withAlpha (0.3f));
        g.fillEllipse (led.expanded (4.0f));
    }

    g.setColour (on ? lightColour : Palette::knobTrack);
    g.fillEllipse (led);

    content.removeFromLeft (narrow ? 7.0f : 9.0f);
    g.setColour (on || isMouseOver ? Palette::textBright : Palette::textDim);
    g.setFont (juce::Font (juce::FontOptions (13.5f, juce::Font::bold)));
    g.drawFittedText (getButtonText(), content.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);

    if (isMouseDown)
    {
        g.setColour (juce::Colours::black.withAlpha (0.15f));
        g.fillRoundedRectangle (bounds, 7.0f);
    }
}

//==============================================================================
// Choice switch
//==============================================================================
ChoiceSwitch::ChoiceSwitch (juce::RangedAudioParameter& parameter, const juce::StringArray& optionNames, juce::Colour colourWhenOn)
    : attachment (parameter, [this] (float value)
                  {
                      // The parameter changed (a click, a preset, the DAW): light the matching button.
                      const juce::ScopedValueSetter<bool> quiet (ignoreClicks, true);
                      const int chosen = juce::roundToInt (value);

                      for (int i = 0; i < options.size(); ++i)
                          options[i]->setToggleState (i == chosen, juce::dontSendNotification);
                  })
{
    for (int i = 0; i < optionNames.size(); ++i)
    {
        auto* option = options.add (new LightButton (optionNames[i], colourWhenOn));
        option->setRadioGroupId (1);   // only one can be on

        option->onClick = [this, i]
        {
            if (! options[i]->getToggleState() || ignoreClicks)
                return;

            attachment.setValueAsCompleteGesture ((float) i);

            if (onUserChange != nullptr)
                onUserChange();
        };

        addAndMakeVisible (option);
    }

    attachment.sendInitialUpdate();
}

void ChoiceSwitch::resized()
{
    // The options stacked one above the other, with a small gap
    constexpr int gap = 8;
    auto area = getLocalBounds();
    const int optionHeight = (area.getHeight() - gap * (options.size() - 1)) / juce::jmax (1, options.size());

    for (auto* option : options)
    {
        option->setBounds (area.removeFromTop (optionHeight));
        area.removeFromTop (gap);
    }
}
