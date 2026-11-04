#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

juce::AudioProcessorValueTreeState::ParameterLayout EasyMasterAudioProcessor::createParameterLayout()
{
    const auto& d = masterPresets[0];   // "Default settings"
    using namespace MasterIDs;

    juce::NormalisableRange<float> trimRange (-12.0f, 12.0f, 0.1f);
    juce::NormalisableRange<float> thresholdRange (-40.0f, 0.0f, 0.1f);
    juce::NormalisableRange<float> ratioRange (1.0f, 10.0f, 0.1f);       ratioRange.setSkewForCentre (2.5f);
    juce::NormalisableRange<float> attackRange (0.5f, 100.0f, 0.1f);     attackRange.setSkewForCentre (20.0f);
    juce::NormalisableRange<float> releaseRange (20.0f, 1000.0f, 1.0f);  releaseRange.setSkewForCentre (200.0f);
    juce::NormalisableRange<float> makeupRange (0.0f, 12.0f, 0.1f);
    juce::NormalisableRange<float> mixRange (0.0f, 100.0f, 1.0f);
    juce::NormalisableRange<float> ignoreRange (20.0f, 300.0f, 1.0f);    ignoreRange.setSkewForCentre (80.0f);
    juce::NormalisableRange<float> gainRange (0.0f, TruePeakLimiter::maxGainDb, 0.1f);
    juce::NormalisableRange<float> ceilingRange (TruePeakLimiter::minCeilingDb, TruePeakLimiter::maxCeilingDb, 0.1f);
    juce::NormalisableRange<float> limitReleaseRange (TruePeakLimiter::minReleaseMs, TruePeakLimiter::maxReleaseMs, 1.0f);
    limitReleaseRange.setSkewForCentre (100.0f);

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto choice = [&layout] (const char* id, const juce::String& name, juce::StringArray options, int def)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, options, def));
    };
    const auto ignoreText = [] (float hz) { return hz <= 20.5f ? juce::String ("Off") : Format::hertz (hz); };

    layout.add (makeKnob (trim, 1, "Input Trim", trimRange, d.trimDb, Format::signedDecibels));

    // EQ
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { MasterEQIDs::on, 3 }, "EQ On", true));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { MasterEQIDs::mode, 3 }, "EQ Mode", juce::StringArray { "Stereo", "Mid + Side" }, 0));
        juce::NormalisableRange<float> gainEqRange (-MasteringEQ::maxGainDb, MasteringEQ::maxGainDb, 0.1f);
        juce::NormalisableRange<float> lowCutRange (MasteringEQ::lowCutOffHz, 300.0f, 1.0f);  lowCutRange.setSkewForCentre (80.0f);
        const auto lowCutText = [] (float hz) { return hz <= MasteringEQ::lowCutOffHz + 0.5f ? juce::String ("Off") : Format::hertz (hz); };
        const char* partName[2] { "Mid", "Side" };
        for (int part = 0; part < 2; ++part)
        {
            for (int b = 0; b < 5; ++b)
            {
                const auto& info = MasterEQIDs::bands[b];
                juce::NormalisableRange<float> freqRange (info.lowHz, info.highHz, 1.0f);
                freqRange.setSkewForCentre (std::sqrt (info.lowHz * info.highHz));
                const juce::String name = juce::String ("EQ ") + partName[part] + " " + juce::String (info.name).toLowerCase();
                layout.add (makeKnob (MasterEQIDs::freq[part][b], 3, name + " Freq", freqRange, info.defaultHz, Format::hertz, Format::hertzFromText));
                layout.add (makeKnob (MasterEQIDs::gain[part][b], 3, name + " Gain", gainEqRange, 0.0f, Format::signedDecibels));
            }
            layout.add (makeKnob (MasterEQIDs::lowCut[part], 3, juce::String ("EQ ") + partName[part] + " Low Cut", lowCutRange, MasteringEQ::lowCutOffHz,
                                  lowCutText, [] (const juce::String& t) { return t.trim().equalsIgnoreCase ("off") ? MasteringEQ::lowCutOffHz : Format::hertzFromText (t); }));
        }
    }

    // MULTIBAND
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { MasterMBIDs::on, 4 }, "Multiband On", false));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { MasterMBIDs::mode, 4 }, "Multiband Mode", juce::StringArray { "Stereo", "Mid + Side" }, 0));
        juce::NormalisableRange<float> mbThreshold (-40.0f, 0.0f, 0.1f);
        juce::NormalisableRange<float> mbRatio (1.0f, 10.0f, 0.1f);  mbRatio.setSkewForCentre (2.5f);
        juce::NormalisableRange<float> mbSpeed (0.0f, 100.0f, 1.0f);
        juce::NormalisableRange<float> mbGain (-12.0f, 12.0f, 0.1f);
        const char* partName[2] { "Mid", "Side" };
        for (int part = 0; part < 2; ++part)
            for (int b = 0; b < 4; ++b)
            {
                const juce::String name = juce::String ("MB ") + partName[part] + " " + juce::String (MasterMBIDs::bandNames[b]).toLowerCase();
                layout.add (makeKnob (MasterMBIDs::threshold[part][b], 4, name + " Threshold", mbThreshold, 0.0f, Format::decibels));
                layout.add (makeKnob (MasterMBIDs::ratio[part][b],     4, name + " Ratio",     mbRatio,     2.0f, Format::ratio));
                layout.add (makeKnob (MasterMBIDs::speed[part][b],     4, name + " Speed",     mbSpeed,     50.0f, Format::percent));
                layout.add (makeKnob (MasterMBIDs::gain[part][b],      4, name + " Gain",      mbGain,      0.0f, Format::signedDecibels));
            }
        for (int b = 0; b < 4; ++b)
        {
            layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { MasterMBIDs::bandOn[b], 4 }, juce::String ("MB ") + MasterMBIDs::bandNames[b] + " On", true));
            layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { MasterMBIDs::solo[b], 4 }, juce::String ("MB ") + MasterMBIDs::bandNames[b] + " Solo", false));
        }
        for (int sp = 0; sp < 3; ++sp)
        {
            const auto& info = MasterMBIDs::splits[sp];
            juce::NormalisableRange<float> r (info.lowHz, info.highHz, 1.0f);
            r.setSkewForCentre (std::sqrt (info.lowHz * info.highHz));
            layout.add (makeKnob (MasterMBIDs::split[sp], 4, "MB Split " + juce::String (sp + 1), r, info.defaultHz, Format::hertz, Format::hertzFromText));
        }
    }

    // RESONANCE TAMER
    {
        juce::NormalisableRange<float> amountRange (0.0f, 12.0f, 0.1f);
        juce::NormalisableRange<float> percentRange (0.0f, 100.0f, 1.0f);
        juce::NormalisableRange<float> fromRange (40.0f, 12000.0f, 1.0f);  fromRange.setSkewForCentre (800.0f);
        juce::NormalisableRange<float> toRange (1000.0f, 20000.0f, 1.0f);  toRange.setSkewForCentre (6000.0f);
        const auto& t = d.tamer;
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { tamerOn, 2 }, "Tamer On", t.on != 0));
        choice (tamerMode, "Tamer Mode", { "Stereo", "Mid + Side" }, t.midSide);
        layout.add (makeKnob (tMidAmount,  2, "Tamer Mid Amount",       amountRange,  t.midAmount,  Format::decibels));
        layout.add (makeKnob (tMidSens,    2, "Tamer Mid Sensitivity",  percentRange, t.midSens,    Format::percent));
        layout.add (makeKnob (tMidSharp,   2, "Tamer Mid Sharpness",    percentRange, t.midSharp,   Format::percent));
        layout.add (makeKnob (tSideAmount, 2, "Tamer Side Amount",      amountRange,  t.sideAmount, Format::decibels));
        layout.add (makeKnob (tSideSens,   2, "Tamer Side Sensitivity", percentRange, t.sideSens,   Format::percent));
        layout.add (makeKnob (tSideSharp,  2, "Tamer Side Sharpness",   percentRange, t.sideSharp,  Format::percent));
        layout.add (makeKnob (tSpeed,      2, "Tamer Speed",            percentRange, t.speed,      Format::percent));
        layout.add (makeKnob (tFrom,       2, "Tamer From",             fromRange,    t.fromHz,     Format::hertz, Format::hertzFromText));
        layout.add (makeKnob (tTo,         2, "Tamer To",               toRange,      t.toHz,       Format::hertz, Format::hertzFromText));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { tListen, 2 }, "Tamer Listen", false));
    }

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { compOn, 1 }, "Compressor On", d.compOn != 0));
    choice (compMode, "Compressor Mode", { "Stereo", "Mid + Side" }, d.mode);
    layout.add (makeKnob (midThreshold, 1, "Mid Threshold", thresholdRange, d.midThreshold, Format::decibels));
    layout.add (makeKnob (midRatio,     1, "Mid Ratio",     ratioRange,     d.midRatio,     Format::ratio));
    layout.add (makeKnob (midAttack,    1, "Mid Attack",    attackRange,    d.midAttack,    Format::milliseconds));
    layout.add (makeKnob (midRelease,   1, "Mid Release",   releaseRange,   d.midRelease,   Format::milliseconds));
    layout.add (makeKnob (midMakeup,    1, "Mid Make-up",   makeupRange,    d.midMakeup,    Format::signedDecibels));
    layout.add (makeKnob (sideThreshold, 1, "Side Threshold", thresholdRange, d.sideThreshold, Format::decibels));
    layout.add (makeKnob (sideRatio,     1, "Side Ratio",     ratioRange,     d.sideRatio,     Format::ratio));
    layout.add (makeKnob (sideAttack,    1, "Side Attack",    attackRange,    d.sideAttack,    Format::milliseconds));
    layout.add (makeKnob (sideRelease,   1, "Side Release",   releaseRange,   d.sideRelease,   Format::milliseconds));
    layout.add (makeKnob (sideMakeup,    1, "Side Make-up",   makeupRange,    d.sideMakeup,    Format::signedDecibels));
    layout.add (makeKnob (compMix,       1, "Compressor Mix", mixRange,       d.mixPercent,    Format::percent));
    layout.add (makeKnob (ignoreBelow,   1, "Ignore Below",   ignoreRange,    d.ignoreBelowHz, ignoreText, Format::hertzFromText));
    choice (reactsTo,  "Reacts To", { "Peaks", "Average" }, d.reactsTo);
    choice (character, "Character", { "Modern", "Vintage" }, d.character);

    layout.add (makeKnob (limitGain, 1, "Limiter Gain", gainRange, d.limitGainDb, Format::signedDecibels));
    layout.add (makeKnob (ceiling,   1, "Ceiling", ceilingRange, d.ceilingDb, [] (float db) { return juce::String (db, 1) + " dBTP"; }));
    choice (releaseMode, "Limiter Release Mode", { "Auto", "Set" }, d.releaseMode);
    layout.add (makeKnob (release, 1, "Limiter Release", limitReleaseRange, d.releaseMs, Format::milliseconds));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { match, 1 }, "Match", false));
    choice (listen, "Listen", { "Stereo", "Mid", "Side", "Mono" }, MasterListen::stereo);

    addCommonParameters (layout, false);   // BYPASS
    return layout;
}

//==============================================================================
EasyMasterAudioProcessor::EasyMasterAudioProcessor()
    : EasyProcessor ("EasyMasterSettings", createParameterLayout(), makeMasterPresets(), masterPresetIDs())
{
    using namespace MasterIDs;
    trimValue = v (trim); compOnValue = v (compOn); compModeValue = v (compMode); mixValue = v (compMix);
    ignoreValue = v (ignoreBelow); reactsValue = v (reactsTo); characterValue = v (character);
    limitGainValue = v (limitGain); ceilingValue = v (ceiling); releaseModeValue = v (releaseMode); releaseValue = v (release);
    matchValue = v (match); listenValue = v (listen); bypassValue = v (CommonIDs::bypass);
    tamerValues = { v (tamerOn), v (tamerMode), v (tMidAmount), v (tMidSens), v (tMidSharp), v (tSideAmount), v (tSideSens), v (tSideSharp),
                    v (tSpeed), v (tFrom), v (tTo), v (tListen) };
    mbOnValue = v (MasterMBIDs::on); mbModeValue = v (MasterMBIDs::mode);
    for (int part = 0; part < 2; ++part)
        for (int b = 0; b < 4; ++b)
            mbBandValues[(size_t) part][(size_t) b] = { v (MasterMBIDs::threshold[part][b]), v (MasterMBIDs::ratio[part][b]), v (MasterMBIDs::speed[part][b]), v (MasterMBIDs::gain[part][b]) };
    for (int b = 0; b < 4; ++b) { mbBandOnValues[(size_t) b] = v (MasterMBIDs::bandOn[b]); mbSoloValues[(size_t) b] = v (MasterMBIDs::solo[b]); }
    for (int sp = 0; sp < 3; ++sp) mbSplitValues[(size_t) sp] = v (MasterMBIDs::split[sp]);
    eqOnValue = v (MasterEQIDs::on); eqModeValue = v (MasterEQIDs::mode);
    for (int part = 0; part < 2; ++part)
    {
        for (int b = 0; b < 5; ++b) { eqFreqValues[(size_t) part][(size_t) b] = v (MasterEQIDs::freq[part][b]); eqGainValues[(size_t) part][(size_t) b] = v (MasterEQIDs::gain[part][b]); }
        eqLowCutValues[(size_t) part] = v (MasterEQIDs::lowCut[part]);
    }
    midValues  = { v (midThreshold),  v (midRatio),  v (midAttack),  v (midRelease),  v (midMakeup) };
    sideValues = { v (sideThreshold), v (sideRatio), v (sideAttack), v (sideRelease), v (sideMakeup) };
}

bool EasyMasterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& input = layouts.getMainInputChannelSet();
    const auto& output = layouts.getMainOutputChannelSet();
    if (output != juce::AudioChannelSet::stereo() && output != juce::AudioChannelSet::mono())
        return false;
    return input == output || (input == juce::AudioChannelSet::mono() && output == juce::AudioChannelSet::stereo());
}

Compressor::Settings EasyMasterAudioProcessor::compressorSettings (bool side) const noexcept
{
    const auto& values = side ? sideValues : midValues;
    Compressor::Settings s;
    s.thresholdDb = values[0]->load();
    s.ratio       = values[1]->load();
    s.attackMs    = values[2]->load();
    s.releaseMs   = values[3]->load();
    s.makeupDb    = values[4]->load();
    s.mix         = mixValue->load() / 100.0f;
    s.lowCutHz    = ignoreValue->load();
    s.reactToAverage = reactsValue->load() > 0.5f;
    s.vintage     = characterValue->load() > 0.5f;
    s.bypassed    = compOnValue->load() < 0.5f;
    return s;
}

MultibandCompressor::Settings EasyMasterAudioProcessor::multibandSettings() const noexcept
{
    MultibandCompressor::Settings s;
    s.bypassed = mbOnValue->load() < 0.5f;
    s.midSide = juce::roundToInt (mbModeValue->load()) == MasterMode::midSide;
    for (int part = 0; part < 2; ++part)
        for (int b = 0; b < 4; ++b)
        {
            const auto& vals = mbBandValues[(size_t) part][(size_t) b];
            auto& band = (part == 0 ? s.mid : s.side)[(size_t) b];
            band = { vals[0]->load(), vals[1]->load(), vals[2]->load() / 100.0f, vals[3]->load() };
        }
    for (int b = 0; b < 4; ++b)
    {
        s.bandOn[(size_t) b] = mbBandOnValues[(size_t) b]->load() > 0.5f;
        s.solo[(size_t) b] = mbSoloValues[(size_t) b]->load() > 0.5f && ! s.bypassed;
    }
    for (int sp = 0; sp < 3; ++sp) s.splitHz[(size_t) sp] = mbSplitValues[(size_t) sp]->load();
    return s;
}

MasteringEQ::Settings EasyMasterAudioProcessor::eqSettings() const noexcept
{
    MasteringEQ::Settings s;
    s.bypassed = eqOnValue->load() < 0.5f;
    s.midSide = juce::roundToInt (eqModeValue->load()) == MasterMode::midSide;
    for (int part = 0; part < 2; ++part)
    {
        auto& p = part == 0 ? s.mid : s.side;
        for (int b = 0; b < 5; ++b)
        {
            p.freqHz[(size_t) b] = eqFreqValues[(size_t) part][(size_t) b]->load();
            p.gainDb[(size_t) b] = eqGainValues[(size_t) part][(size_t) b]->load();
        }
        p.lowCutHz = eqLowCutValues[(size_t) part]->load();
    }
    return s;
}

ResonanceTamer::Settings EasyMasterAudioProcessor::tamerSettings() const noexcept
{
    ResonanceTamer::Settings s;
    const auto& t = tamerValues;
    s.bypassed = t[0]->load() < 0.5f || bypassValue->load() > 0.5f;
    s.midSide = juce::roundToInt (t[1]->load()) == MasterMode::midSide;
    s.mid  = { t[2]->load(), t[3]->load() / 100.0f, t[4]->load() / 100.0f };
    s.side = { t[5]->load(), t[6]->load() / 100.0f, t[7]->load() / 100.0f };
    s.speed = t[8]->load() / 100.0f;
    s.fromHz = t[9]->load();
    s.toHz = juce::jmax (t[10]->load(), s.fromHz * 1.25f);
    s.listen = t[11]->load() > 0.5f && ! s.bypassed;
    return s;
}

TruePeakLimiter::Settings EasyMasterAudioProcessor::limiterSettings() const noexcept
{
    TruePeakLimiter::Settings s;
    s.gainDb      = limitGainValue->load();
    s.ceilingDb   = ceilingValue->load();
    s.autoRelease = juce::roundToInt (releaseModeValue->load()) == 0;
    s.releaseMs   = releaseValue->load();
    return s;
}

//==============================================================================
void EasyMasterAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    rate = sampleRate;
    const int block = juce::jmax (1, samplesPerBlock);
    midComp.prepare (sampleRate, compressorSettings (false));
    sideComp.prepare (sampleRate, compressorSettings (true));
    limiter.prepare (sampleRate, 2, limiterSettings());
    tamer.prepare (sampleRate, tamerSettings());
    eq.prepare (sampleRate, eqSettings());
    multiband.prepare (sampleRate, block, multibandSettings());
    loudness.prepare (sampleRate);
    originalLoudness.prepare (sampleRate);
    mid.setSize (1, block);
    sides.setSize (1, block);
    original.setSize (2, block);

    trimGain.reset (sampleRate, 0.05);
    trimGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (trimValue->load()));
    matchGain.reset (sampleRate, 0.5);
    matchGain.setCurrentAndTargetValue (1.0f);
    bypassFade.reset (sampleRate, 0.02);
    bypassFade.setCurrentAndTargetValue (bypassValue->load() > 0.5f ? 1.0f : 0.0f);
    const int listening = juce::roundToInt (listenValue->load());
    for (size_t i = 0; i < listenFades.size(); ++i)
    {
        listenFades[i].reset (sampleRate, 0.03);
        listenFades[i].setCurrentAndTargetValue ((int) i == listening ? 1.0f : 0.0f);
    }
    playingMode = juce::roundToInt (compModeValue->load());
    modeChanging = false;
    modeFade = 1.0f;

    latency = tamer.getLatencySamples() + limiter.getLatencySamples();
    setLatencySamples (latency);
    for (auto& d : dryDelay)
        d.assign ((size_t) juce::jmax (1, latency), 0.0f);
    dryPos = 0;

    sumLR = sumLL = sumRR = 0.0;
    momentLength = juce::jmax (1, juce::roundToInt (sampleRate * 0.01));
    momentFilled = 0;
    building = {};
    inputPeakHeld = 0.0f;
    inputPeakCount = 0;
}

void EasyMasterAudioProcessor::compress (juce::AudioBuffer<float>& buffer, int n) noexcept
{
    const int wanted = juce::roundToInt (compModeValue->load());
    if (wanted != playingMode && ! modeChanging)
        modeChanging = true;

    float midCut = 0.0f, sideCut = 0.0f;
    if (playingMode == MasterMode::stereo)
    {
        juce::AudioBuffer<float> both (buffer.getArrayOfWritePointers(), 2, 0, n);
        midCut = sideCut = midComp.process (both).maxReductionDb;
    }
    else
    {
        float* left = buffer.getWritePointer (0);
        float* right = buffer.getWritePointer (1);
        float* m = mid.getWritePointer (0);
        float* s = sides.getWritePointer (0);
        for (int i = 0; i < n; ++i)
        {
            m[i] = 0.5f * (left[i] + right[i]);
            s[i] = 0.5f * (left[i] - right[i]);
        }
        juce::AudioBuffer<float> midPart (mid.getArrayOfWritePointers(), 1, 0, n), sidePart (sides.getArrayOfWritePointers(), 1, 0, n);
        midCut = midComp.process (midPart).maxReductionDb;
        sideCut = sideComp.process (sidePart).maxReductionDb;
        for (int i = 0; i < n; ++i)
        {
            left[i] = m[i] + s[i];
            right[i] = m[i] - s[i];
        }
    }

    // Changing STEREO <-> MID + SIDE: a 10 ms dip and back, switching at the bottom (no click)
    if (modeChanging || modeFade < 1.0f)
    {
        const float step = 1.0f / (0.01f * (float) rate);
        for (int i = 0; i < n; ++i)
        {
            modeFade = juce::jlimit (0.0f, 1.0f, modeFade + (modeChanging ? -step : step));
            const float level = modeFade * modeFade * (3.0f - 2.0f * modeFade);
            buffer.getWritePointer (0)[i] *= level;
            buffer.getWritePointer (1)[i] *= level;
            if (modeChanging && modeFade <= 0.0f)
            {
                playingMode = wanted;
                modeChanging = false;
                midComp.prepare (rate, compressorSettings (false));   // (fresh, so the switch doesn't carry old gain over)
                sideComp.prepare (rate, compressorSettings (true));
            }
        }
    }

    building.midCutDb = juce::jmax (building.midCutDb, midCut);
    building.sideCutDb = juce::jmax (building.sideCutDb, playingMode == MasterMode::stereo ? midCut : sideCut);
    nowMidCut.store (midCut);
    nowSideCut.store (playingMode == MasterMode::stereo ? midCut : sideCut);
}

void EasyMasterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2)
    {
        // (a mono master: the tamer and the limiter's safety, no stereo parts - the same delay)
        tamer.setSettings (tamerSettings());
        tamer.process (buffer);
        eq.setSettings (eqSettings());
        eq.process (buffer);
        multiband.setSettings (multibandSettings());
        multiband.process (buffer);
        limiter.setSettings (limiterSettings());
        limiter.process (buffer, 1);
        return;
    }
    if (getTotalNumInputChannels() == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, total);

    midComp.setSettings (compressorSettings (false));
    sideComp.setSettings (compressorSettings (true));
    limiter.setSettings (limiterSettings());
    tamer.setSettings (tamerSettings());
    eq.setSettings (eqSettings());
    multiband.setSettings (multibandSettings());
    trimGain.setTargetValue (juce::Decibels::decibelsToGain (trimValue->load()));
    bypassFade.setTargetValue (bypassValue->load() > 0.5f ? 1.0f : 0.0f);
    const int listening = juce::roundToInt (listenValue->load());
    for (size_t i = 0; i < listenFades.size(); ++i)
        listenFades[i].setTargetValue ((int) i == listening ? 1.0f : 0.0f);

    // MATCH: the whole chain as loud as the original (short-term loudness, followed over half a second)
    {
        const float before = originalLoudness.getShortTermLufs(), after = loudness.getShortTermLufs();
        const bool heard = before > -70.0f && after > -70.0f;
        const float db = (matchValue->load() > 0.5f && heard) ? juce::jlimit (-24.0f, 24.0f, before - after) : (matchValue->load() > 0.5f ? matchDb.load() : 0.0f);
        matchDb.store (db);
        matchGain.setTargetValue (juce::Decibels::decibelsToGain (db));
    }

    const float limitGain = juce::Decibels::decibelsToGain (limitGainValue->load());

    for (int start = 0; start < total;)
    {
        const int n = std::min ({ total - start, momentLength - momentFilled, mid.getNumSamples() });
        juce::AudioBuffer<float> part (buffer.getArrayOfWritePointers(), 2, start, n);

        // The original (for BYPASS and MATCH), and the input warning
        for (int c = 0; c < 2; ++c)
        {
            original.copyFrom (c, 0, part, c, 0, n);
            inputPeakHeld = juce::jmax (inputPeakHeld, part.getMagnitude (c, 0, n));
        }
        {
            juce::AudioBuffer<float> originalPart (original.getArrayOfWritePointers(), 2, 0, n);
            originalLoudness.process (originalPart);
        }
        inputPeakCount += n;
        if (inputPeakCount >= (int) (rate * 0.2))
        {
            inputPeak.store (juce::Decibels::gainToDecibels (inputPeakHeld, -100.0f));
            inputPeakHeld = 0.0f;
            inputPeakCount = 0;
        }

        // INPUT
        for (int i = 0; i < n; ++i)
        {
            const float g = trimGain.getNextValue();
            part.getWritePointer (0)[i] *= g;
            part.getWritePointer (1)[i] *= g;
        }

        // RESONANCE TAMER (always running, so the delay never changes; switched off it passes the sound straight through)
        tamer.process (part);
        nowTamerCut[0].store (tamerValues[0]->load() > 0.5f ? tamer.getMostCutDb (0) : 0.0f);
        nowTamerCut[1].store (tamerValues[0]->load() > 0.5f ? tamer.getMostCutDb (1) : 0.0f);

        // EQ
        eq.process (part);

        // MULTIBAND
        multiband.process (part);
        for (int b = 0; b < 4; ++b)
            for (int pt = 0; pt < 2; ++pt)
            {
                storeIfBigger (mbCut[(size_t) b][(size_t) pt], mbOnValue->load() > 0.5f ? multiband.getReductionDb (b, pt) : 0.0f);
                storeIfBigger (mbPeak[(size_t) b][(size_t) pt], multiband.getBandPeak (b, pt));
            }

        // COMPRESSOR
        compress (part, n);

        // LIMITER
        building.inDb = juce::jmax (building.inDb, juce::Decibels::gainToDecibels (juce::jmax (part.getMagnitude (0, 0, n), part.getMagnitude (1, 0, n)) * limitGain, -100.0f));
        limiter.process (part, 2);
        building.outDb = juce::jmax (building.outDb, limiter.getOutputPeakDb());
        building.limitCutDb = juce::jmax (building.limitCutDb, limiter.getReductionDb());
        nowLimitCut.store (limiter.getReductionDb());
        highestPeak.store (juce::jmax (highestPeak.load(), limiter.getOutputPeakDb()));

        // The meters read the master here
        loudness.process (part);
        {
            const float* l = part.getReadPointer (0);
            const float* r = part.getReadPointer (1);
            const double fall = std::exp (-(double) n / (0.3 * rate));
            double lr = 0.0, ll = 0.0, rr = 0.0;
            for (int i = 0; i < n; ++i) { lr += l[i] * r[i]; ll += l[i] * l[i]; rr += r[i] * r[i]; }
            sumLR = sumLR * fall + lr; sumLL = sumLL * fall + ll; sumRR = sumRR * fall + rr;
            correlation.store (sumLL > 1.0e-9 && sumRR > 1.0e-9 ? (float) (sumLR / std::sqrt (sumLL * sumRR)) : 1.0f);
        }

        // MATCH, LISTEN, BYPASS
        float* left = part.getWritePointer (0);
        float* right = part.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float g = matchGain.getNextValue();
            float l = left[i] * g, r = right[i] * g;
            const float m = 0.5f * (l + r), s = 0.5f * (l - r);
            const float fs = listenFades[0].getNextValue(), fm = listenFades[1].getNextValue(),
                        fsi = listenFades[2].getNextValue(), fmo = listenFades[3].getNextValue();
            // (MID and MONO: the middle in both speakers; SIDE: the difference, in both speakers)
            const float heardL = l * fs + m * (fm + fmo) + s * fsi;
            const float heardR = r * fs + m * (fm + fmo) + s * fsi;

            const float dryL = dryDelay[0][(size_t) dryPos], dryR = dryDelay[1][(size_t) dryPos];
            dryDelay[0][(size_t) dryPos] = original.getSample (0, i);
            dryDelay[1][(size_t) dryPos] = original.getSample (1, i);
            dryPos = (dryPos + 1) % (int) dryDelay[0].size();

            const float b = bypassFade.getNextValue();
            left[i]  = b >= 1.0f ? dryL : heardL * (1.0f - b) + dryL * b;
            right[i] = b >= 1.0f ? dryR : heardR * (1.0f - b) + dryR * b;
        }

        momentFilled += n;
        if (momentFilled >= momentLength)
        {
            if (momentFifo.getFreeSpace() > 0)
                moments[(size_t) momentFifo.write (1).startIndex1] = building;
            building = {};
            momentFilled = 0;
        }
        start += n;
    }
}

int EasyMasterAudioProcessor::takeMoments (Moment* destination, int maxMoments) noexcept
{
    const auto read = momentFifo.read (juce::jmin (maxMoments, momentFifo.getNumReady()));
    int written = 0;
    for (int i = 0; i < read.blockSize1; ++i)  destination[written++] = moments[(size_t) (read.startIndex1 + i)];
    for (int i = 0; i < read.blockSize2; ++i)  destination[written++] = moments[(size_t) (read.startIndex2 + i)];
    return written;
}

EasyMasterAudioProcessor::Readings EasyMasterAudioProcessor::getReadings() const noexcept
{
    Readings r;
    r.momentary = loudness.getMomentaryLufs();
    r.shortTerm = loudness.getShortTermLufs();
    r.integrated = loudness.getIntegratedLufs();
    r.highestPeak = highestPeak.load();
    r.correlation = correlation.load();
    r.midCut = nowMidCut.load();
    r.sideCut = nowSideCut.load();
    r.limitCut = nowLimitCut.load();
    r.matchDb = matchDb.load();
    r.tamerMidCut = nowTamerCut[0].load();
    r.tamerSideCut = nowTamerCut[1].load();
    return r;
}

EasyMasterAudioProcessor::MultibandPicture EasyMasterAudioProcessor::takeMultibandPicture() noexcept
{
    MultibandPicture p;
    for (int b = 0; b < 4; ++b)
        for (int pt = 0; pt < 2; ++pt)
        {
            p.cutDb[(size_t) b][(size_t) pt] = mbCut[(size_t) b][(size_t) pt].exchange (0.0f);
            p.peakDb[(size_t) b][(size_t) pt] = juce::Decibels::gainToDecibels (mbPeak[(size_t) b][(size_t) pt].exchange (0.0f), -100.0f);
        }
    return p;
}

EasyMasterAudioProcessor::TamerPicture EasyMasterAudioProcessor::getTamerPicture() const noexcept
{
    TamerPicture p;
    for (int part = 0; part < 2; ++part)
        for (int b = 0; b < ResonanceTamer::numBands; ++b)
        {
            p.levelDb[(size_t) part][(size_t) b] = tamer.getBandLevelDb (part, b);
            p.cutDb[(size_t) part][(size_t) b] = tamer.getBandCutDb (part, b);
        }
    for (int b = 0; b < ResonanceTamer::numBands; ++b)
        p.centreHz[(size_t) b] = tamer.getBandCentreHz (b);
    return p;
}

//==============================================================================
juce::AudioProcessorEditor* EasyMasterAudioProcessor::createEditor()
{
    return new EasyMasterAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EasyMasterAudioProcessor();
}
