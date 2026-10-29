#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

juce::AudioProcessorValueTreeState::ParameterLayout EasyDSAudioProcessor::createParameterLayout()
{
    const auto& defaults = dsPresets[0];   // "Default settings"

    // Knob ranges. "Skew" makes a knob spend more of its turn on the useful
    // values: e.g. FROM's halfway point is 4.5 kHz, not 8.5 kHz.
    juce::NormalisableRange<float> fromRange (1000.0f, 16000.0f, 1.0f);
    fromRange.setSkewForCentre (4500.0f);

    juce::NormalisableRange<float> toRange (2000.0f, DeEsser::toOffHz, 1.0f);
    toRange.setSkewForCentre (8000.0f);

    juce::NormalisableRange<float> thresholdRange (-30.0f, 20.0f, 0.1f);
    juce::NormalisableRange<float> rangeRange (0.0f, 24.0f, 0.1f);

    // TO reads "Off" at the top: then the harsh band is everything above FROM.
    const auto toText = [] (float hz)  { return hz >= DeEsser::toOffHz - 0.5f ? juce::String ("Off") : Format::hertz (hz); };
    const auto toFromText = [] (const juce::String& text)
    {
        return text.trim().equalsIgnoreCase ("off") ? DeEsser::toOffHz : Format::hertzFromText (text);
    };

    const auto rangeFromText = [] (const juce::String& text) { return std::abs (text.getFloatValue()); };   // ("-6" means 6 too)

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // (The numbers after the IDs say which release added each setting.)
    layout.add (makeKnob (DSIDs::from,      1, "From",      fromRange,      defaults.fromHz,      Format::hertz, Format::hertzFromText));
    layout.add (makeKnob (DSIDs::to,        1, "To",        toRange,        defaults.toHz,        toText, toFromText));
    layout.add (makeKnob (DSIDs::threshold, 1, "Threshold", thresholdRange, defaults.thresholdDb, Format::signedDecibels));
    layout.add (makeKnob (DSIDs::range,     1, "Range",     rangeRange,     defaults.rangeDb,     Format::decibels, rangeFromText));

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { DSIDs::turnsDown, 1 }, "Turns Down",
                                                              juce::StringArray { "Harsh Band", "Whole Sound" }, (int) defaults.turnsDown));

    addCommonParameters (layout, false);   // BYPASS (a de-esser doesn't need AUTO-GAIN)
    return layout;
}

//==============================================================================
EasyDSAudioProcessor::EasyDSAudioProcessor()
    : EasyProcessor ("EasyDSSettings", createParameterLayout(), makeDSPresets(), dsPresetIDs())
{
    fromValue      = parameters.getRawParameterValue (DSIDs::from);
    toValue        = parameters.getRawParameterValue (DSIDs::to);
    thresholdValue = parameters.getRawParameterValue (DSIDs::threshold);
    rangeValue     = parameters.getRawParameterValue (DSIDs::range);
    turnsDownValue = parameters.getRawParameterValue (DSIDs::turnsDown);
    bypassValue    = parameters.getRawParameterValue (CommonIDs::bypass);

    // The look-ahead, until the DAW says which sample rate it's using
    setLatencySamples ((int) std::lround (DeEsser::lookAheadSeconds * 44100.0));
    finderListener.prepare (44100.0);
}

//==============================================================================
void EasyDSAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    deEsser.prepare (sampleRate, juce::jmax (1, juce::roundToInt (sampleRate * 0.005)), getCurrentSettings());   // 5 ms moments

    // The sound comes out 2 ms late (the look-ahead): telling the DAW lets it
    // play this track 2 ms early to make up for it, so everything stays in time.
    setLatencySamples (deEsser.getLatencySamples());

    finderListener.prepare (sampleRate);
    meterReductionDb.store (0.0f);
}

void EasyDSAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;   // avoids CPU spikes from ultra-tiny numbers

    auto track = getBusBuffer (buffer, false, 0);

    // FIND listens to your track as it comes in (before it's de-essed).
    if (finding.load())
    {
        if (finderNeedsReset.exchange (false))
            finderListener.reset();

        finderListener.process (track, [this] (const HarshnessFinder::Frame& frame)
        {
            // (If the window isn't collecting them, the frame is simply dropped.)
            if (frameFifo.getFreeSpace() > 0)
            {
                const auto write = frameFifo.write (1);
                frames[(size_t) write.startIndex1] = frame;
            }
        });
    }

    deEsser.setSettings (getCurrentSettings());
    const float mostDb = deEsser.process (track, [this] (const DeEsser::Moment& moment) { pushMoment (moment); });

    storeIfBigger (meterReductionDb, mostDb);
}

void EasyDSAudioProcessor::pushMoment (const DeEsser::Moment& m) noexcept
{
    // (The levels are powers: half of what gainToDecibels gives is their level in dB.)
    const auto powerToDb = [] (float power) { return 0.5f * FastMath::gainToDecibels (power, 2.0f * DeEsser::silenceDb); };

    const Moment moment { powerToDb (m.soundLevel), powerToDb (m.bandLevel), powerToDb (m.lineLevel),
                          FastMath::gainToDecibels (m.gain, DeEsser::silenceDb) };

    // (So even a quick S between two looks shows on the meter.)
    storeIfBigger (meterReductionDb, -moment.gainDb);

    // (If the window isn't reading them - e.g. it's closed - the moment is simply dropped.)
    if (momentFifo.getFreeSpace() > 0)
    {
        const auto write = momentFifo.write (1);
        moments[(size_t) write.startIndex1] = moment;
    }
}

int EasyDSAudioProcessor::takeMoments (Moment* destination, int maxMoments) noexcept
{
    const auto read = momentFifo.read (juce::jmin (maxMoments, momentFifo.getNumReady()));
    int written = 0;

    for (int i = 0; i < read.blockSize1; ++i)  destination[written++] = moments[(size_t) (read.startIndex1 + i)];
    for (int i = 0; i < read.blockSize2; ++i)  destination[written++] = moments[(size_t) (read.startIndex2 + i)];

    return written;
}

//==============================================================================
void EasyDSAudioProcessor::startFinding() noexcept
{
    // (Anything left over from an earlier FIND is thrown away first.)
    HarshnessFinder::Frame leftOver[64];
    while (takeFrames (leftOver, 64) > 0) {}

    finderNeedsReset.store (true);
    finding.store (true);
}

int EasyDSAudioProcessor::takeFrames (HarshnessFinder::Frame* destination, int maxFrames) noexcept
{
    const auto read = frameFifo.read (juce::jmin (maxFrames, frameFifo.getNumReady()));
    int written = 0;

    for (int i = 0; i < read.blockSize1; ++i)  destination[written++] = frames[(size_t) (read.startIndex1 + i)];
    for (int i = 0; i < read.blockSize2; ++i)  destination[written++] = frames[(size_t) (read.startIndex2 + i)];

    return written;
}

DeEsser::Settings EasyDSAudioProcessor::getCurrentSettings() const noexcept
{
    DeEsser::Settings settings;
    settings.fromHz      = fromValue->load();
    settings.toHz        = toValue->load();
    settings.thresholdDb = thresholdValue->load();
    settings.rangeDb     = rangeValue->load();
    settings.wholeSound  = turnsDownValue->load() > 0.5f;
    settings.listen      = listening.load();
    settings.bypassed    = bypassValue->load() > 0.5f;
    return settings;
}

//==============================================================================
juce::AudioProcessorEditor* EasyDSAudioProcessor::createEditor()
{
    return new EasyDSAudioProcessorEditor (*this);
}

#if ! EASY_STRIP_MODULE   // (inside the Easy Strip, this plugin is one of its modules, not a plugin of its own)
// JUCE calls this to create the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EasyDSAudioProcessor();
}
#endif
