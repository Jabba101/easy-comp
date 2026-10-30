#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

namespace
{
    struct BandRange { float low, high, centre; const char* name; };
    constexpr BandRange bandRanges[] { { 30.0f, 400.0f, 110.0f, "Low" }, { 75.0f, 1200.0f, 300.0f, "Low Mid" },
                                       { 800.0f, 8000.0f, 2500.0f, "High Mid" }, { 2500.0f, 20000.0f, 7000.0f, "High" } };
}

juce::AudioProcessorValueTreeState::ParameterLayout EasyEQAudioProcessor::createParameterLayout()
{
    const auto defaults = makeEQPresets().front();   // "Default settings"
    const auto defaultOf = [&] (const char* id)
    {
        for (auto& [key, value] : defaults.values)
            if (key == id) return value;
        return 0.0f;
    };

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int b = 0; b < 4; ++b)
    {
        const auto& r = bandRanges[b];
        juce::NormalisableRange<float> freqRange (r.low, r.high, 1.0f);
        freqRange.setSkewForCentre (r.centre);
        juce::NormalisableRange<float> gainRange (-Equalizer::maxGainDb, Equalizer::maxGainDb, 0.1f);
        const juce::String name (r.name);

        // (The numbers after the IDs say which release added each setting.)
        layout.add (makeKnob (EQIDs::freq[b], 1, name + " Freq", freqRange, defaultOf (EQIDs::freq[b]), Format::hertz, Format::hertzFromText));
        layout.add (makeKnob (EQIDs::gain[b], 1, name + " Gain", gainRange, defaultOf (EQIDs::gain[b]), Format::signedDecibels));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { EQIDs::target[b], 1 }, name + " Works On",
                                                                  juce::StringArray { "Stereo", "Mid", "Side" }, 0));
    }

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { EQIDs::lowShelf, 1 }, "Low Shape",
                                                              juce::StringArray { "Bell", "Shelf" }, defaultOf (EQIDs::lowShelf) > 0.5f ? 1 : 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { EQIDs::highShelf, 1 }, "High Shape",
                                                              juce::StringArray { "Bell", "Shelf" }, defaultOf (EQIDs::highShelf) > 0.5f ? 1 : 0));

    juce::NormalisableRange<float> hpfRange (Equalizer::hpfOffHz, 500.0f, 1.0f);
    hpfRange.setSkewForCentre (100.0f);
    juce::NormalisableRange<float> lpfRange (2000.0f, Equalizer::lpfOffHz, 1.0f);
    lpfRange.setSkewForCentre (6300.0f);
    juce::NormalisableRange<float> monoRange (Equalizer::monoBassOffHz, 300.0f, 1.0f);
    monoRange.setSkewForCentre (90.0f);

    const auto offBelow = [] (float off)
    {
        return std::make_pair (std::function<juce::String (float)> ([off] (float hz) { return hz <= off + 0.5f ? juce::String ("Off") : Format::hertz (hz); }),
                               std::function<float (const juce::String&)> ([off] (const juce::String& t) { return t.trim().equalsIgnoreCase ("off") ? off : Format::hertzFromText (t); }));
    };
    const auto offAbove = [] (float off)
    {
        return std::make_pair (std::function<juce::String (float)> ([off] (float hz) { return hz >= off - 0.5f ? juce::String ("Off") : Format::hertz (hz); }),
                               std::function<float (const juce::String&)> ([off] (const juce::String& t) { return t.trim().equalsIgnoreCase ("off") ? off : Format::hertzFromText (t); }));
    };

    const auto hpfText = offBelow (Equalizer::hpfOffHz), lpfText = offAbove (Equalizer::lpfOffHz), monoText = offBelow (Equalizer::monoBassOffHz);
    const juce::StringArray slopes { "12 dB/oct", "24 dB/oct" };

    layout.add (makeKnob (EQIDs::hpf, 1, "HPF", hpfRange, defaultOf (EQIDs::hpf), hpfText.first, hpfText.second));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { EQIDs::hpfSlope, 1 }, "HPF Slope", slopes, 0));
    layout.add (makeKnob (EQIDs::lpf, 1, "LPF", lpfRange, defaultOf (EQIDs::lpf), lpfText.first, lpfText.second));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { EQIDs::lpfSlope, 1 }, "LPF Slope", slopes, 0));
    layout.add (makeKnob (EQIDs::monoBass, 1, "Mono Bass", monoRange, defaultOf (EQIDs::monoBass), monoText.first, monoText.second));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { EQIDs::master, 1 }, "Master (Linear Phase)", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { EQIDs::precision, 2 }, "Master Precision",
                                                              juce::StringArray { "Low", "Medium", "High" }, 2));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { EQIDs::analyser, 1 }, "Analyser", true));

    addCommonParameters (layout);   // BYPASS and AUTO-GAIN (off to start with)
    return layout;
}

//==============================================================================
EasyEQAudioProcessor::EasyEQAudioProcessor()
    : EasyProcessor ("EasyEQSettings", createParameterLayout(), makeEQPresets(), eqPresetIDs())
{
    for (int b = 0; b < 4; ++b)
    {
        freqValue[(size_t) b]   = parameters.getRawParameterValue (EQIDs::freq[b]);
        gainValue[(size_t) b]   = parameters.getRawParameterValue (EQIDs::gain[b]);
        targetValue[(size_t) b] = parameters.getRawParameterValue (EQIDs::target[b]);
    }

    lowShelfValue  = parameters.getRawParameterValue (EQIDs::lowShelf);
    highShelfValue = parameters.getRawParameterValue (EQIDs::highShelf);
    hpfValue       = parameters.getRawParameterValue (EQIDs::hpf);
    hpfSlopeValue  = parameters.getRawParameterValue (EQIDs::hpfSlope);
    lpfValue       = parameters.getRawParameterValue (EQIDs::lpf);
    lpfSlopeValue  = parameters.getRawParameterValue (EQIDs::lpfSlope);
    monoBassValue  = parameters.getRawParameterValue (EQIDs::monoBass);
    masterValue    = parameters.getRawParameterValue (EQIDs::master);
    precisionValue = parameters.getRawParameterValue (EQIDs::precision);
    analyserValue  = parameters.getRawParameterValue (EQIDs::analyser);
    autoGainValue  = parameters.getRawParameterValue (CommonIDs::autoGain);
    bypassValue    = parameters.getRawParameterValue (CommonIDs::bypass);
}

EasyEQAudioProcessor::~EasyEQAudioProcessor()
{
    cancelPendingUpdate();
}

//==============================================================================
void EasyEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const auto settings = getCurrentSettings();
    const int block = juce::jmax (samplesPerBlock, 512);

    eq.prepare (sampleRate, settings);
    linear.prepare (sampleRate, block, settings, juce::roundToInt (precisionValue->load()));

    original.setSize (2, block, false, true, true);
    linearCopy.setSize (2, block, false, true, true);

    // The original, delayed by MASTER's delay (for BYPASS and LISTEN while it's on;
    // room for the longest, HIGH)
    delayLength = linear.getLatencySamples();
    int ringSize = 1;
    while (ringSize < LinearPhaseEQ::maximumLength (sampleRate) / 2 + block + 1)
        ringSize <<= 1;
    delayRing.assign ((size_t) (2 * ringSize), 0.0f);
    delayMask = ringSize - 1;
    delayPos = 0;

    masterOn = masterValue->load() > 0.5f;
    masterFade.reset (sampleRate, 0.03);
    masterFade.setCurrentAndTargetValue (masterOn ? 1.0f : 0.0f);
    setLatencySamples (masterOn ? delayLength : 0);
}

void EasyEQAudioProcessor::reset()
{
    eq.reset();
    linear.reset();
    std::fill (delayRing.begin(), delayRing.end(), 0.0f);
}

void EasyEQAudioProcessor::handleAsyncUpdate()
{
    setLatencySamples (masterOn ? delayLength : 0);   // (told to the DAW from the message thread)
}

void EasyEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    const int channels = juce::jmin (2, buffer.getNumChannels());
    const int total = buffer.getNumSamples();

    // MASTER switched: tell the DAW about the new delay, and crossfade
    const bool wantMaster = masterValue->load() > 0.5f;
    if (wantMaster != masterOn)
    {
        masterOn = wantMaster;
        masterFade.setTargetValue (masterOn ? 1.0f : 0.0f);
        triggerAsyncUpdate();
    }

    const auto settings = getCurrentSettings();
    eq.setSettings (settings);
    linear.setSettings (settings);
    linear.setPrecision (juce::roundToInt (precisionValue->load()));

    // (In pieces no longer than the buffers prepared for them)
    for (int start = 0; start < total;)
    {
        const int n = juce::jmin (original.getNumSamples(), total - start);
        juce::AudioBuffer<float> piece (buffer.getArrayOfWritePointers(), channels, start, n);
        juce::AudioBuffer<float> dry (original.getArrayOfWritePointers(), channels, 0, n);

        for (int ch = 0; ch < channels; ++ch)
            dry.copyFrom (ch, 0, piece, ch, 0, n);

        const bool linearNeeded = masterOn || masterFade.isSmoothing();
        juce::AudioBuffer<float> lin (linearCopy.getArrayOfWritePointers(), channels, 0, n);

        if (linearNeeded)
        {
            for (int ch = 0; ch < channels; ++ch)
                lin.copyFrom (ch, 0, piece, ch, 0, n);
            linear.process (lin);
        }
        else
        {
            linear.takeNewFilter();   // (so the delay follows PRECISION even while MASTER is off)
        }

        // PRECISION changed and the new filter's in use: a new delay
        if (linear.getLatencySamples() != delayLength)
        {
            delayLength = linear.getLatencySamples();
            triggerAsyncUpdate();
        }

        eq.process (piece);

        // Crossfade the minimum-phase and linear-phase versions (and the
        // original, undelayed and delayed, to go with them)
        for (int i = 0; i < n; ++i)
        {
            const float fade = masterFade.getNextValue();

            for (int ch = 0; ch < channels; ++ch)
            {
                const float in = dry.getSample (ch, i);
                float& slot = delayRing[(size_t) (ch * (delayMask + 1) + ((delayPos + i) & delayMask))];
                const float delayed = delayRing[(size_t) (ch * (delayMask + 1) + ((delayPos + i - delayLength) & delayMask))];
                slot = in;

                if (fade > 0.0f)
                {
                    piece.setSample (ch, i, piece.getSample (ch, i) * (1.0f - fade) + lin.getSample (ch, i) * fade);
                    dry.setSample (ch, i, in * (1.0f - fade) + delayed * fade);
                }
            }
        }

        delayPos = (delayPos + n) & delayMask;

        eq.finish (piece, dry);

        // The analyser's feed
        if (analyserValue->load() > 0.5f)
        {
            int pos = recentPos.load();
            for (int i = 0; i < n; ++i)
            {
                recent[(size_t) pos] = channels == 2 ? 0.5f * (piece.getSample (0, i) + piece.getSample (1, i)) : piece.getSample (0, i);
                pos = (pos + 1) % analyserLength;
            }
            recentPos.store (pos);
        }

        start += n;
    }

    autoGainDb.store (eq.getAutoGainDb());
}

void EasyEQAudioProcessor::copyRecentOutput (float* destination, int count) const noexcept
{
    const int end = recentPos.load();
    count = juce::jmin (count, analyserLength);

    for (int i = 0; i < count; ++i)
        destination[i] = recent[(size_t) ((end - count + i + analyserLength) % analyserLength)];
}

Equalizer::Settings EasyEQAudioProcessor::getCurrentSettings() const noexcept
{
    Equalizer::Settings s;

    for (int b = 0; b < 4; ++b)
    {
        auto& band = s.bands[(size_t) b];
        band.freqHz = freqValue[(size_t) b]->load();
        band.gainDb = gainValue[(size_t) b]->load();
        band.target = (Equalizer::Target) juce::jlimit (0, 2, (int) targetValue[(size_t) b]->load());
        band.shelf  = (b == 0 && lowShelfValue->load() > 0.5f) || (b == 3 && highShelfValue->load() > 0.5f);
    }

    s.hpfHz      = hpfValue->load();
    s.hpfSteep   = hpfSlopeValue->load() > 0.5f;
    s.lpfHz      = lpfValue->load();
    s.lpfSteep   = lpfSlopeValue->load() > 0.5f;
    s.monoBassHz = monoBassValue->load();
    s.autoGain   = autoGainValue->load() > 0.5f;
    s.bypassed   = bypassValue->load() > 0.5f;
    s.listenBand = listenBand.load();
    return s;
}

//==============================================================================
juce::AudioProcessorEditor* EasyEQAudioProcessor::createEditor()
{
    return new EasyEQAudioProcessorEditor (*this);
}

#if ! EASY_STRIP_MODULE   // (inside the Easy Strip, this plugin is one of its modules, not a plugin of its own)
// JUCE calls this to create the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EasyEQAudioProcessor();
}
#endif
