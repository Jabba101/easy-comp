#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

//==============================================================================
/** How values read, both in the plugins and in your DAW's automation lanes. */
namespace Format
{
    inline juce::String decibels (float value)         { return juce::String (value, 1) + " dB"; }
    inline juce::String signedDecibels (float value)   { return (value >= 0.05f ? "+" : "") + juce::String (value, 1) + " dB"; }
    inline juce::String ratio (float value)            { return juce::String (value, 1) + " : 1"; }
    inline juce::String percent (float value)          { return juce::String (juce::roundToInt (value)) + "%"; }
    inline juce::String signedPercent (float value)    { return (value >= 0.5f ? "+" : "") + juce::String (juce::roundToInt (value)) + "%"; }

    inline juce::String milliseconds (float ms)
    {
        if (ms < 1.0f)   return juce::String (ms, 2) + " ms";
        if (ms < 10.0f)  return juce::String (ms, 1) + " ms";
        return juce::String (juce::roundToInt (ms)) + " ms";
    }

    /** "150 Hz", "2.5 kHz" */
    inline juce::String hertz (float hz)
    {
        if (hz < 999.5f)  return juce::String (juce::roundToInt (hz)) + " Hz";
        return juce::String (hz / 1000.0f, 1) + " kHz";
    }

    /** Understands "150", "150 Hz", "2.5k" and "2.5 kHz". */
    inline float hertzFromText (const juce::String& text)
    {
        const auto cleaned = text.trim().toLowerCase();
        const float value = cleaned.getFloatValue();
        return cleaned.containsChar ('k') ? value * 1000.0f : value;
    }
}

//==============================================================================
/** Makes a knob setting. versionHint: which release added it (so DAWs can
    tell old and new settings apart). valueToText: how its value reads.
    textToValue: how typed text is understood (by default, "-15" and
    "-15 dB" both mean -15). */
inline std::unique_ptr<juce::AudioParameterFloat> makeKnob (const char* id, int versionHint, const juce::String& name,
                                                            juce::NormalisableRange<float> range, float defaultValue,
                                                            std::function<juce::String (float)> valueToText,
                                                            std::function<float (const juce::String&)> textToValue = {})
{
    if (textToValue == nullptr)
        textToValue = [] (const juce::String& text) { return text.getFloatValue(); };

    // Frequency knobs: a number typed without "k" or "Hz" that's too small
    // for this knob, but fits it as kHz, is taken as kHz - so typing 10 into
    // a knob that runs from 2.5 kHz to 20 kHz gives 10 kHz, not its lowest.
    // ("10 Hz" typed in full still means 10 Hz.)
    if (valueToText (range.convertFrom0to1 (0.5f)).endsWith ("Hz"))
    {
        textToValue = [textToValue, range] (const juce::String& text)
        {
            const float value = textToValue (text);
            const auto cleaned = text.trim().toLowerCase();
            const bool saidUnits = cleaned.containsChar ('k') || cleaned.contains ("hz") || cleaned == "off";

            if (! saidUnits && value < range.start && value * 1000.0f >= range.start * 0.999f && value * 1000.0f <= range.end * 1.001f)
                return value * 1000.0f;

            return value;
        };
    }

    auto attributes = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([valueToText] (float value, int maximumLength)
                                      {
                                          const auto text = valueToText (value);
                                          return maximumLength > 0 ? text.substring (0, maximumLength) : text;
                                      })
        .withValueFromStringFunction ([textToValue] (const juce::String& text) { return textToValue (text); });

    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, versionHint }, name, range, defaultValue, attributes);
}

//==============================================================================
/** Keeps the bigger of the stored value and a new one, without locking (so
    the audio never has to wait for the plugin window). */
inline void storeIfBigger (std::atomic<float>& store, float newValue) noexcept
{
    float previous = store.load();

    while (newValue > previous && ! store.compare_exchange_weak (previous, newValue))
    {
    }
}

/** The same, keeping the smaller value. */
inline void storeIfSmaller (std::atomic<float>& store, float newValue) noexcept
{
    float previous = store.load();

    while (newValue < previous && ! store.compare_exchange_weak (previous, newValue))
    {
    }
}
