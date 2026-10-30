#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "StripPresets.h"

namespace
{
    // Settings the strip shares out instead of copying per module
    bool isSharedSetting (int module, const juce::String& id)
    {
        return id == "quality" && (module == 1 || module == 7);   // AMP's and CLIP's OVERSAMPLING
    }

    constexpr int fadeLength = 480;          // a module switched back on crossfades in over ~10 ms
    constexpr int maxRingLatency = 16384;    // the longest module delay the strip can line up (EQ MASTER at 192 kHz)
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout EasyStripAudioProcessor::createParameterLayout (StripModules& modules)
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (makeKnob (StripIDs::trim, 1, "Input Trim", { -24.0f, 24.0f, 0.1f }, 0.0f, Format::signedDecibels));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { StripIDs::polarity, 1 }, "Input Polarity Flip", false));
    layout.add (makeKnob (StripIDs::level, 1, "Output Level", { -24.0f, 24.0f, 0.1f }, 0.0f, Format::signedDecibels));
    for (int i = 0; i < 4; ++i)
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { StripIDs::orderSwitches[i], 2 },
                                                                "Order: " + juce::String (StripIDs::orderNames[i]).toLowerCase(), false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { StripIDs::oversampling, 1 }, "Oversampling (Amp, Clip)",
                                                              juce::StringArray { "4x Mixing / 8x Export", "Off", "4x", "8x" }, 0));

    // Every module's settings, with the module's name in front
    for (int m = 0; m < StripModules::numModules; ++m)
    {
        for (auto* p : modules.all[(size_t) m]->getParameters())
        {
            auto* setting = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (setting == nullptr || isSharedSetting (m, setting->paramID))
                continue;

            const juce::ParameterID id { StripIDs::moduleID (m, setting->paramID), 1 };
            const juce::String name = juce::String (StripModules::names[m]) + " " + setting->getName (64);

            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (setting))
            {
                layout.add (std::make_unique<juce::AudioParameterChoice> (id, name, choice->choices, choice->getIndex()));
            }
            else if (auto* toggle = dynamic_cast<juce::AudioParameterBool*> (setting))
            {
                // A module's BYPASS is its power switch: every module starts switched off
                const bool off = setting->paramID == CommonIDs::bypass;
                layout.add (std::make_unique<juce::AudioParameterBool> (id, off ? juce::String (StripModules::names[m]) + " Off" : name, off ? true : toggle->get()));
            }
            else
            {
                auto attributes = juce::AudioParameterFloatAttributes()
                    .withStringFromValueFunction ([setting] (float value, int length) { return setting->getText (setting->convertTo0to1 (value), length); })
                    .withValueFromStringFunction ([setting] (const juce::String& text) { return setting->convertFrom0to1 (setting->getValueForText (text)); });

                layout.add (std::make_unique<juce::AudioParameterFloat> (id, name, setting->getNormalisableRange(),
                                                                         setting->convertFrom0to1 (setting->getDefaultValue()), attributes));
            }
        }
    }

    addCommonParameters (layout, true);    // the whole strip's BYPASS and AUTO-GAIN
    return layout;
}

std::vector<Preset> EasyStripAudioProcessor::makeStripPresets (StripModules& modules)
{
    std::vector<Preset> presets;

    Preset init { "", "Default settings",
                  "Every module switched off and back to its defaults. Switch on the ones you need, or pick a preset.", {} };
    for (int m = 0; m < StripModules::numModules; ++m)
        init.values.push_back ({ StripIDs::moduleID (m, CommonIDs::bypass), 1.0f });
    presets.push_back (init);

    // Each recipe: the modules it switches on, each with one of its own presets
    for (auto& recipe : StripPresetRecipes::all())
    {
        Preset preset { recipe.category, recipe.name, recipe.tip, {} };
        std::array<bool, StripModules::numModules> on {};

        for (auto& use : recipe.uses)
        {
            const int m = (int) use.module;
            const Preset* found = nullptr;
            for (auto& p : modules.all[(size_t) m]->getBuiltInPresets())
                if (p.name == use.preset)
                    found = &p;

            jassert (found != nullptr);   // (a module preset was renamed: update StripPresets.h)
            if (found == nullptr)
                continue;

            on[(size_t) m] = true;
            for (auto& [id, value] : found->values)
                preset.values.push_back ({ StripIDs::moduleID (m, id), value });
        }

        for (int m = 0; m < StripModules::numModules; ++m)
            preset.values.push_back ({ StripIDs::moduleID (m, CommonIDs::bypass), on[(size_t) m] ? 0.0f : 1.0f });

        for (auto* swap : recipe.order)
            preset.values.push_back ({ swap, 1.0f });

        // Knobs set on top of the module presets
        for (auto& set : recipe.sets)
        {
            const auto id = StripIDs::moduleID ((int) set.module, set.id);
            jassert (modules.all[(size_t) set.module]->parameters.getParameter (set.id) != nullptr);   // (a typo in StripPresets.h)
            bool replaced = false;
            for (auto& [existing, value] : preset.values)
                if (existing == id) { value = set.value; replaced = true; }
            if (! replaced)
                preset.values.push_back ({ id, set.value });
        }

        presets.push_back (std::move (preset));
    }

    return presets;
}

juce::StringArray EasyStripAudioProcessor::stripPresetIDs (StripModules& modules)
{
    // What strip presets (and A / B) cover: INPUT and OUTPUT, which modules are on,
    // and each module's own preset settings. (Not each module's AUTO-GAIN,
    // the EQ's MASTER and PRECISION, or OVERSAMPLING - as in the standalones.)
    juce::StringArray ids { StripIDs::trim, StripIDs::polarity, StripIDs::level };
    for (auto* id : StripIDs::orderSwitches)
        ids.add (id);   // (the ORDER is part of presets and A / B too)

    for (int m = 0; m < StripModules::numModules; ++m)
    {
        ids.add (StripIDs::moduleID (m, CommonIDs::bypass));
        for (auto& id : modules.all[(size_t) m]->getPresetParameterIDs())
            if (! isSharedSetting (m, id))
                ids.add (StripIDs::moduleID (m, id));
    }

    return ids;
}

//==============================================================================
EasyStripAudioProcessor::EasyStripAudioProcessor()
    : EasyProcessor ("EasyStripSettings", createParameterLayout (*this), makeStripPresets (*this), stripPresetIDs (*this), true)
{
    trimValue     = parameters.getRawParameterValue (StripIDs::trim);
    polarityValue = parameters.getRawParameterValue (StripIDs::polarity);
    levelValue    = parameters.getRawParameterValue (StripIDs::level);
    bypassValue   = parameters.getRawParameterValue (CommonIDs::bypass);
    autoGainValue = parameters.getRawParameterValue (CommonIDs::autoGain);
    for (int i = 0; i < 4; ++i)
        orderValues[(size_t) i] = parameters.getRawParameterValue (StripIDs::orderSwitches[i]);
    order = getWantedOrder();
    oversamplingParameter = parameters.getParameter (StripIDs::oversampling);

    connectModules();
    pushSettings();
    setLatencySamples (totalLatency());
}

EasyStripAudioProcessor::~EasyStripAudioProcessor()
{
    cancelPendingUpdate();

    for (auto& module : links)
        for (auto& link : module.settings)
            link->module->removeListener (link.get());
}

void EasyStripAudioProcessor::connectModules()
{
    for (int m = 0; m < numModules; ++m)
    {
        for (auto* p : all[(size_t) m]->getParameters())
        {
            auto* setting = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (setting == nullptr)
                continue;

            if (isSharedSetting (m, setting->paramID))
            {
                qualitySettings[m == 1 ? 0 : 1] = setting;
                continue;
            }

            auto link = std::make_unique<Link>();
            link->owner = this;
            link->module = setting;
            link->strip = parameters.getParameter (StripIDs::moduleID (m, setting->paramID));
            jassert (link->strip != nullptr);
            setting->addListener (link.get());

            if (setting->paramID == CommonIDs::bypass)
                links[(size_t) m].stripBypass = link->strip;

            links[(size_t) m].settings.push_back (std::move (link));
        }
    }
}

//==== Settings: strip -> module (audio thread), module -> strip (its window) ====
void EasyStripAudioProcessor::Link::parameterValueChanged (int, float newValue)
{
    if (pushing.load())
        return;

    // Changed in the module's own window (a knob, a preset, INIT, A / B): tell the strip, and so the DAW
    last.store (newValue);
    strip->setValueNotifyingHost (newValue);
    owner->markPresetEdited();   // (the strip's preset name then shows it's been changed)
}

void EasyStripAudioProcessor::Link::parameterGestureChanged (int, bool starting)
{
    if (starting) strip->beginChangeGesture();
    else          strip->endChangeGesture();
}

void EasyStripAudioProcessor::pushSettings() noexcept
{
    for (auto& module : links)
    {
        for (auto& link : module.settings)
        {
            const float value = link->strip->getValue();

            if (std::abs (value - link->last.load()) > 1.0e-7f)
            {
                link->last.store (value);
                link->pushing.store (true);
                link->module->setValueNotifyingHost (value);
                link->pushing.store (false);
            }
        }
    }

    const float oversampling = oversamplingParameter->getValue();
    if (std::abs (oversampling - lastOversampling) > 1.0e-7f)
    {
        lastOversampling = oversampling;
        for (auto* q : qualitySettings)
            if (q != nullptr)
                q->setValueNotifyingHost (oversampling);
    }
}

bool EasyStripAudioProcessor::isModuleOn (int m) const noexcept
{
    return links[(size_t) m].stripBypass != nullptr && links[(size_t) m].stripBypass->getValue() < 0.5f;
}

int EasyStripAudioProcessor::totalLatency() const noexcept
{
    int total = 0;
    for (auto* module : all)
        total += module->getLatencySamples();
    return total;
}

void EasyStripAudioProcessor::handleAsyncUpdate()
{
    setLatencySamples (reportedLatency);
}

//==============================================================================
void EasyStripAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int block = juce::jmax (64, samplesPerBlock);
    channelsInUse = juce::jlimit (1, 2, getMainBusNumInputChannels());
    const auto mainSet = channelsInUse == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();

    // Every setting handed over afresh first (so each module starts as it's set: on or off)
    for (auto& module : links)
        for (auto& link : module.settings)
            link->last.store (-1.0f);
    lastOversampling = -1.0f;
    pushSettings();

    for (auto* module : all)
    {
        auto layout = module->getBusesLayout();
        layout.inputBuses.getReference (0) = mainSet;
        layout.outputBuses.getReference (0) = mainSet;
        if (layout.inputBuses.size() > 1)
            layout.inputBuses.getReference (1) = mainSet;

        module->setBusesLayout (layout);
        module->setNonRealtime (isNonRealtime());
        module->setRateAndBufferSizeDetails (sampleRate, block);
        module->prepareToPlay (sampleRate, block);
    }

    ringSize = 1;
    while (ringSize < maxRingLatency + block + 1)
        ringSize <<= 1;
    ringMask = ringSize - 1;
    writePos = 0;

    for (auto& module : links)
    {
        module.ring.assign ((size_t) (2 * ringSize), 0.0f);
        module.skipped = false;
        module.offFor = module.warmUp = 0;
        module.fadeIn = fadeLength;
    }

    dryRing.setSize (2, ringSize);
    dryRing.clear();
    sidechain.setSize (2, block);
    sidechain.clear();

    for (auto* s : { &inputGain, &outputGain })
        s->reset (sampleRate, 0.03);
    bypassFade.reset (sampleRate, 0.02);
    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (trimValue->load()) * (polarityValue->load() > 0.5f ? -1.0f : 1.0f));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (levelValue->load()));
    bypassFade.setCurrentAndTargetValue (bypassValue->load() > 0.5f ? 1.0f : 0.0f);

    order = getWantedOrder();
    orderFade = OrderFade::steady;
    orderGain = 1.0f;

    autoIn.prepare (sampleRate, 100.0f);
    autoOut.prepare (sampleRate, 100.0f);
    autoListenIn = autoListenOut = 0.0;
    autoTrim = 1.0f;
    autoGainSmoothed.reset (sampleRate, 0.05);
    autoGainSmoothed.setCurrentAndTargetValue (1.0f);

    loudness.prepare (sampleRate);
    inputPeak.store (0.0f);
    outputPeak.store (0.0f);

    reportedLatency = totalLatency();
    setLatencySamples (reportedLatency);
}

void EasyStripAudioProcessor::reset()
{
    for (auto* module : all)
        module->reset();
    for (auto& module : links)
        std::fill (module.ring.begin(), module.ring.end(), 0.0f);
    dryRing.clear();
}

void EasyStripAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int channels = juce::jmin (channelsInUse, getMainBusNumInputChannels(), buffer.getNumChannels());
    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    if (channels <= 0)
        return;

    pushSettings();

    for (auto* module : all)
    {
        module->setPlayHead (getPlayHead());     // (Bitch-Slap's SYNC follows the song's tempo)
        module->setNonRealtime (isNonRealtime());
    }

    inputGain.setTargetValue (juce::Decibels::decibelsToGain (trimValue->load()) * (polarityValue->load() > 0.5f ? -1.0f : 1.0f));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (levelValue->load()));
    bypassFade.setTargetValue (bypassValue->load() > 0.5f ? 1.0f : 0.0f);

    auto sidechainIn = getBusCount (true) > 1 && getBus (true, 1)->isEnabled() ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
    juce::MidiBuffer noMidi;
    const int maxChunk = sidechain.getNumSamples();
    const int total = buffer.getNumSamples();

    for (int start = 0; start < total; start += maxChunk)
    {
        const int n = juce::jmin (maxChunk, total - start);
        float* main[2] { buffer.getWritePointer (0, start), channels > 1 ? buffer.getWritePointer (1, start) : nullptr };

        // The original (for the strip's own BYPASS), then INPUT: TRIM and polarity
        float loudestIn = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float g = inputGain.getNextValue();
            for (int ch = 0; ch < channels; ++ch)
            {
                dryRing.setSample (ch, (writePos + i) & ringMask, main[ch][i]);
                main[ch][i] *= g;
                loudestIn = juce::jmax (loudestIn, std::abs (main[ch][i]));
            }
        }
        storeIfBigger (inputPeak, loudestIn);

        // The sidechain for GATE and COMP (silence if nothing's sent to it)
        for (int ch = 0; ch < channels; ++ch)
        {
            if (sidechainIn.getNumChannels() > 0)
                sidechain.copyFrom (ch, 0, sidechainIn, juce::jmin (ch, sidechainIn.getNumChannels() - 1), start, n);
            else
                sidechain.clear (ch, 0, n);
        }

        // ORDER changed: dip out, swap (once silent), come back in
        if (orderFade == OrderFade::steady && getWantedOrder() != order)
            orderFade = OrderFade::out;
        else if (orderFade == OrderFade::out && orderGain <= 0.0f)
        {
            order = getWantedOrder();
            orderFade = OrderFade::hold;
            orderHold = totalLatency() + 256;
        }

        // The modules, in order
        for (int step = 0; step < numModules; ++step)
        {
            const int m = order[(size_t) step];
            auto& link = links[(size_t) m];
            auto& module = *all[(size_t) m];
            const int latency = juce::jmin (module.getLatencySamples(), maxRingLatency);
            const bool off = link.stripBypass->getValue() > 0.5f;

            // Keep what goes in, for while it's skipped (or warming up)
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < n; ++i)
                    link.ring[(size_t) (ch * ringSize + ((writePos + i) & ringMask))] = main[ch][i];

            link.offFor = off ? link.offFor + n : 0;

            if (! link.skipped && off && link.offFor > (int) (getSampleRate() * 0.04))
                link.skipped = true;   // (faded out by its own BYPASS by now: from here on it costs nothing)

            if (link.skipped && ! off)
            {
                link.skipped = false;
                module.reset();
                link.warmUp = latency + 64;
                link.fadeIn = 0;
            }

            const auto delayed = [&] (int ch, int i) { return link.ring[(size_t) (ch * ringSize + ((writePos + i - latency) & ringMask))]; };

            if (link.skipped)
            {
                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < n; ++i)
                        main[ch][i] = delayed (ch, i);
                continue;
            }

            // Run the module (with the sidechain behind its main channels, for GATE and COMP)
            float* pointers[4] { main[0], main[1], sidechain.getWritePointer (0), channels > 1 ? sidechain.getWritePointer (1) : nullptr };
            if (channels == 1)
                pointers[1] = sidechain.getWritePointer (0);

            const int moduleChannels = StripModules::hasSidechain (m) ? channels * 2 : channels;
            juce::AudioBuffer<float> moduleBuffer (pointers, moduleChannels, n);
            module.processBlock (moduleBuffer, noMidi);

            // Just switched back on: the delayed original while it warms up, then a crossfade
            if (link.warmUp > 0 || link.fadeIn < fadeLength)
            {
                for (int i = 0; i < n; ++i)
                {
                    float t = 1.0f;
                    if (link.warmUp > 0)               { --link.warmUp; t = 0.0f; }
                    else if (link.fadeIn < fadeLength) { t = (float) link.fadeIn++ / (float) fadeLength; }

                    for (int ch = 0; ch < channels; ++ch)
                        main[ch][i] = delayed (ch, i) * (1.0f - t) + main[ch][i] * t;
                }
            }
        }

        // AUTO-GAIN (the whole strip), then OUTPUT: LEVEL, then the strip's BYPASS
        // (the original, delayed by the whole strip)
        const int latencyNow = juce::jmin (totalLatency(), ringSize - maxChunk - 1);
        autoGainSmoothed.setTargetValue (autoGainValue->load() > 0.5f ? autoTrim : 1.0f);
        double heardIn = 0.0, heardOut = 0.0;
        float loudestOut = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            if (orderFade == OrderFade::out)       orderGain = juce::jmax (0.0f, orderGain - 1.0f / 480.0f);
            else if (orderFade == OrderFade::hold) { if (--orderHold <= 0) orderFade = OrderFade::in; }
            else if (orderFade == OrderFade::in)   { orderGain = juce::jmin (1.0f, orderGain + 1.0f / 480.0f); if (orderGain >= 1.0f) orderFade = OrderFade::steady; }

            const float g = outputGain.getNextValue() * autoGainSmoothed.getNextValue() * orderGain;
            const float bypass = bypassFade.getNextValue();

            for (int ch = 0; ch < channels; ++ch)
            {
                const float original = dryRing.getSample (ch, (writePos + i - latencyNow) & ringMask);
                heardIn  += autoIn.measure (ch, original);      // (what came in, lined up with what comes out)
                heardOut += autoOut.measure (ch, main[ch][i]);  // (what the modules made of it)
                main[ch][i] = main[ch][i] * g * (1.0f - bypass) + original * bypass;
                loudestOut = juce::jmax (loudestOut, std::abs (main[ch][i]));
            }
        }
        storeIfBigger (outputPeak, loudestOut);
        updateAutoGain (heardIn, heardOut, n);

        juce::AudioBuffer<float> out (main, channels, n);
        loudness.process (out);

        writePos = (writePos + n) & ringMask;
    }

    blocksProcessed.fetch_add (1);

    const int latency = totalLatency();
    if (latency != reportedLatency)
    {
        reportedLatency = latency;
        triggerAsyncUpdate();
    }
}

//==============================================================================
EasyStripAudioProcessor::Order EasyStripAudioProcessor::orderFor (bool eqAfterComp, bool deEssBeforeDrive,
                                                                bool driveAfterComp, bool shapeBeforeComp) noexcept
{
    enum { gate, amp, eq, ds, comp, trans, slap, clip };
    Order o {};
    int n = 0;
    o[(size_t) n++] = gate;

    // Before COMP
    if (deEssBeforeDrive)  o[(size_t) n++] = ds;
    if (! driveAfterComp)  o[(size_t) n++] = amp;
    if (! eqAfterComp)     o[(size_t) n++] = eq;
    if (! deEssBeforeDrive) o[(size_t) n++] = ds;
    if (shapeBeforeComp)   o[(size_t) n++] = trans;

    o[(size_t) n++] = comp;

    // After COMP (drive first, then the EQ shapes it)
    if (driveAfterComp)    o[(size_t) n++] = amp;
    if (eqAfterComp)       o[(size_t) n++] = eq;
    if (! shapeBeforeComp) o[(size_t) n++] = trans;

    o[(size_t) n++] = slap;
    o[(size_t) n++] = clip;
    return o;
}

EasyStripAudioProcessor::Order EasyStripAudioProcessor::getWantedOrder() const noexcept
{
    const auto on = [this] (int i) { return orderValues[(size_t) i] != nullptr && orderValues[(size_t) i]->load() > 0.5f; };
    return orderFor (on (0), on (1), on (2), on (3));
}

juce::String EasyStripAudioProcessor::describe (const Order& o)
{
    juce::StringArray names;
    for (auto m : o)
        names.add (m == 6 ? "BITCH-SLAP" : StripModules::tabs[m]);
    return names.joinIntoString (" > ");
}

//==============================================================================
void EasyStripAudioProcessor::updateAutoGain (double heardIn, double heardOut, int n) noexcept
{
    // Compares how loud it sounds going in (after TRIM) and coming out of the
    // modules - the last couple of seconds, as ears weigh loudness - and trims
    // to match: quick to turn down, gentle to turn up, at most 12 dB either way.
    // It keeps listening while it's off, so it's ready the moment it's on.
    const float trim = juce::Decibels::decibelsToGain (trimValue->load());
    heardIn *= (double) (trim * trim);
    if (heardIn / n < 1.0e-9)
        return;   // (silence: hold)

    const double rate = 1.0 - std::exp (-(double) n / (getSampleRate() * 1.5));
    autoListenIn  += rate * (heardIn / n - autoListenIn);
    autoListenOut += rate * (heardOut / n - autoListenOut);

    double wanted = juce::jlimit (0.25, 4.0, std::sqrt (autoListenIn / juce::jmax (autoListenOut, 1.0e-20)));
    if (isModuleOn (7))
        wanted = juce::jmin (wanted, 1.0);   // (with CLIP on it only turns down, so CLIP's CEILING still holds)

    const double speed = wanted < autoTrim ? rate * 3.0 : rate;
    autoTrim = (float) (autoTrim + speed * (wanted - autoTrim));
    autoGainDb.store (juce::Decibels::gainToDecibels (autoTrim));
}

//==============================================================================
void EasyStripAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryBlock own;
    EasyProcessor::getStateInformation (own);

    juce::XmlElement root ("EasyStrip");
    if (auto settings = getXmlFromBinary (own.getData(), (int) own.getSize()))
        root.addChildElement (settings.release());

    // Each module's own extras (its preset name, the EQ's other A / B set...)
    for (int m = 0; m < numModules; ++m)
    {
        juce::MemoryBlock moduleState;
        all[(size_t) m]->getStateInformation (moduleState);
        auto* element = root.createNewChildElement ("Module");
        element->setAttribute ("key", StripModules::keys[m]);
        element->setAttribute ("state", moduleState.toBase64Encoding());
    }

    copyXmlToBinary (root, destData);
}

void EasyStripAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto root = getXmlFromBinary (data, sizeInBytes);
    if (root == nullptr || ! root->hasTagName ("EasyStrip"))
        return;

    // The modules first, then the strip's settings (which have the last word)
    for (auto* element : root->getChildWithTagNameIterator ("Module"))
    {
        for (int m = 0; m < numModules; ++m)
        {
            if (element->getStringAttribute ("key") == StripModules::keys[m])
            {
                juce::MemoryBlock moduleState;
                if (moduleState.fromBase64Encoding (element->getStringAttribute ("state")))
                    all[(size_t) m]->setStateInformation (moduleState.getData(), (int) moduleState.getSize());
            }
        }
    }

    if (auto* settings = root->getChildByName (parameters.state.getType()))
    {
        juce::MemoryBlock own;
        copyXmlToBinary (*settings, own);
        EasyProcessor::setStateInformation (own.getData(), (int) own.getSize());
    }

    for (auto& module : links)
        for (auto& link : module.settings)
            link->last.store (-1.0f);   // (hand everything over again at the next block)
    lastOversampling = -1.0f;
}

//==============================================================================
juce::AudioProcessorEditor* EasyStripAudioProcessor::createEditor()
{
    return new EasyStripAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EasyStripAudioProcessor();
}
