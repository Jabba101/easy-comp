#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

juce::AudioProcessorValueTreeState::ParameterLayout EasyClipAudioProcessor::createParameterLayout()
{
    const auto& defaults = clipPresets[0];   // "Default settings"

    // Knob ranges. "Skew" makes a knob spend more of its turn on the useful
    // values: e.g. PROTECT LOWS' halfway point is 100 Hz, not 260 Hz.
    juce::NormalisableRange<float> pushRange (0.0f, 24.0f, 0.1f);
    pushRange.setSkewForCentre (8.0f);
    juce::NormalisableRange<float> ceilingRange (-24.0f, 0.0f, 0.1f);
    juce::NormalisableRange<float> percentRange (0.0f, 100.0f, 1.0f);
    juce::NormalisableRange<float> protectRange (Clipper::protectOffHz, 500.0f, 1.0f);
    protectRange.setSkewForCentre (100.0f);
    juce::NormalisableRange<float> sideRange (-12.0f, 0.0f, 0.1f);
    juce::NormalisableRange<float> biasRange (-100.0f, 100.0f, 1.0f);

    const auto protectText = [] (float hz)  { return hz <= Clipper::protectOffHz + 0.5f ? juce::String ("Off") : Format::hertz (hz); };
    const auto protectFromText = [] (const juce::String& text)
    {
        return text.trim().equalsIgnoreCase ("off") ? Clipper::protectOffHz : Format::hertzFromText (text);
    };

    // TOP and BOTTOM are below the ceiling: "-3" and "3" both mean 3 dB below.
    const auto sideFromText = [] (const juce::String& text) { return -std::abs (text.getFloatValue()); };

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // (The numbers after the IDs say which release added each setting.)
    layout.add (makeKnob (ClipIDs::push,        1, "Push",         pushRange,    defaults.pushDb,          Format::signedDecibels));
    layout.add (makeKnob (ClipIDs::ceiling,     1, "Ceiling",      ceilingRange, defaults.ceilingDb,       Format::decibels));
    layout.add (makeKnob (ClipIDs::softness,    1, "Softness",     percentRange, defaults.softnessPercent, Format::percent));
    layout.add (makeKnob (ClipIDs::protectLows, 1, "Protect Lows", protectRange, defaults.protectLowsHz,   protectText, protectFromText));
    layout.add (makeKnob (ClipIDs::top,         1, "Top",          sideRange,    defaults.topDb,           Format::decibels, sideFromText));
    layout.add (makeKnob (ClipIDs::bottom,      1, "Bottom",       sideRange,    defaults.bottomDb,        Format::decibels, sideFromText));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ClipIDs::link, 1 }, "Link", true));
    layout.add (makeKnob (ClipIDs::bias,        1, "Bias",         biasRange,    defaults.biasPercent,     Format::signedPercent));
    layout.add (makeKnob (ClipIDs::blend,       1, "Blend",        percentRange, defaults.blendPercent,    Format::percent));

    // AUTO: 4x while you mix, 8x when your DAW exports (renders) the song.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ClipIDs::quality, 1 }, "Oversampling",
                                                              juce::StringArray { "Auto", "Off", "4x", "8x" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ClipIDs::view, 1 }, "View",
                                                              juce::StringArray { "Simple", "Advanced" }, 0));

    addCommonParameters (layout, false);   // BYPASS (no AUTO-GAIN)
    return layout;
}

//==============================================================================
EasyClipAudioProcessor::EasyClipAudioProcessor()
    : EasyProcessor ("EasyClipSettings", createParameterLayout(), makeClipPresets(), clipPresetIDs())
{
    pushValue        = parameters.getRawParameterValue (ClipIDs::push);
    ceilingValue     = parameters.getRawParameterValue (ClipIDs::ceiling);
    softnessValue    = parameters.getRawParameterValue (ClipIDs::softness);
    protectLowsValue = parameters.getRawParameterValue (ClipIDs::protectLows);
    topValue         = parameters.getRawParameterValue (ClipIDs::top);
    bottomValue      = parameters.getRawParameterValue (ClipIDs::bottom);
    biasValue        = parameters.getRawParameterValue (ClipIDs::bias);
    blendValue       = parameters.getRawParameterValue (ClipIDs::blend);
    qualityValue     = parameters.getRawParameterValue (ClipIDs::quality);
    bypassValue      = parameters.getRawParameterValue (CommonIDs::bypass);

    // Tell the DAW the delay straight away (it's the same at any sample rate).
    clipper.prepare (44100.0, 2, getCurrentSettings());
    setLatencySamples (clipper.getLatencySamples());
}

//==============================================================================
void EasyClipAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    clipper.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()), getCurrentSettings());
    setLatencySamples (clipper.getLatencySamples());

    pushedLevels.assign ((size_t) juce::jmax (samplesPerBlock, 4096), 0.0f);
    meterShavedDb.store (0.0f);

    momentLength = juce::jmax (1, juce::roundToInt (sampleRate * 0.005));   // 5 ms
    momentFilled = 0;
    momentPushed = momentOut = 0.0f;
}

void EasyClipAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    const auto settings = getCurrentSettings();
    clipper.setSettings (settings);

    // Below the knee (where the clip starts to bend) nothing is shaved, so
    // small differences there - from oversampling's filters - don't count.
    const float rounding = juce::jmax (settings.softness, std::abs (settings.bias));
    const float kneeDb = settings.ceilingDb + juce::jmin (settings.topDb, settings.bottomDb)
                       + juce::Decibels::gainToDecibels (1.0f - rounding, -60.0f);

    const int numSamples = buffer.getNumSamples();

    // (A DAW can send a longer block than it promised: grow the level store,
    // rarely and only then.)
    if ((int) pushedLevels.size() < numSamples)
        pushedLevels.resize ((size_t) numSamples);

    clipper.process (buffer, pushedLevels.data());

    // The display: how loud each 5 ms moment would have been, and how loud it came out
    for (int i = 0; i < numSamples; ++i)
    {
        float out = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            out = juce::jmax (out, std::abs (buffer.getSample (ch, i)));

        momentPushed = juce::jmax (momentPushed, pushedLevels[(size_t) i]);
        momentOut = juce::jmax (momentOut, out);

        if (++momentFilled >= momentLength)
        {
            const Moment moment { juce::Decibels::gainToDecibels (momentPushed, Clipper::silenceDb),
                                  juce::Decibels::gainToDecibels (momentOut, Clipper::silenceDb) };

            if (moment.pushedDb > kneeDb && moment.pushedDb > Clipper::silenceDb + 1.0f)
                storeIfBigger (meterShavedDb, juce::jmax (0.0f, moment.pushedDb - moment.outDb));

            // (If the window isn't reading them - e.g. it's closed - the moment is simply dropped.)
            if (momentFifo.getFreeSpace() > 0)
            {
                const auto write = momentFifo.write (1);
                moments[(size_t) write.startIndex1] = moment;
            }

            momentFilled = 0;
            momentPushed = momentOut = 0.0f;
        }
    }
}

int EasyClipAudioProcessor::takeMoments (Moment* destination, int maxMoments) noexcept
{
    const auto read = momentFifo.read (juce::jmin (maxMoments, momentFifo.getNumReady()));
    int written = 0;

    for (int i = 0; i < read.blockSize1; ++i)  destination[written++] = moments[(size_t) (read.startIndex1 + i)];
    for (int i = 0; i < read.blockSize2; ++i)  destination[written++] = moments[(size_t) (read.startIndex2 + i)];

    return written;
}

Clipper::Settings EasyClipAudioProcessor::getCurrentSettings() const noexcept
{
    Clipper::Settings settings;
    settings.pushDb        = pushValue->load();
    settings.ceilingDb     = ceilingValue->load();
    settings.softness      = softnessValue->load() / 100.0f;   // the knobs show %, the maths wants 0..1
    settings.protectLowsHz = protectLowsValue->load();
    settings.topDb         = topValue->load();
    settings.bottomDb      = bottomValue->load();
    settings.bias          = biasValue->load() / 100.0f;
    settings.blend         = blendValue->load() / 100.0f;
    const int chosen = juce::jlimit (0, 3, (int) qualityValue->load());
    settings.quality       = chosen == 0 ? (isNonRealtime() ? Clipper::Quality::times8 : Clipper::Quality::times4)
                                         : (Clipper::Quality) (chosen - 1);
    settings.bypassed      = bypassValue->load() > 0.5f;
    return settings;
}

bool EasyClipAudioProcessor::isAdvancedInUse() const noexcept
{
    return std::abs (topValue->load()) > 0.05f || std::abs (bottomValue->load()) > 0.05f
        || std::abs (biasValue->load()) > 0.5f || blendValue->load() < 99.5f;
}

void EasyClipAudioProcessor::resetAdvanced()
{
    for (auto& id : clipAdvancedIDs())
    {
        if (auto* parameter = parameters.getParameter (id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->getDefaultValue());
            parameter->endChangeGesture();
        }
    }

    markPresetEdited();
}

//==============================================================================
juce::AudioProcessorEditor* EasyClipAudioProcessor::createEditor()
{
    return new EasyClipAudioProcessorEditor (*this);
}

// JUCE calls this to create the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EasyClipAudioProcessor();
}
