#pragma once

#include "EasyProcessor.h"
#include "LoudnessMeter.h"
#include "../EasyGate/PluginProcessor.h"
#include "../EasyAmp/PluginProcessor.h"
#include "../EasyEQ/PluginProcessor.h"
#include "../EasyDS/PluginProcessor.h"
#include "../EasyComp/PluginProcessor.h"
#include "../EasyTransient/PluginProcessor.h"
#include "../BitchSlap/PluginProcessor.h"
#include "../EasyClip/PluginProcessor.h"
#include "../EasyTape/PluginProcessor.h"

//==============================================================================
/**
    The Aardvark Easy Strip: every Easy plugin in one channel strip.

    Each module IS its standalone plugin - the very same code, sound,
    presets and window - running inside the strip, in this order:

        INPUT > TAPE > GATE > AMP > EQ > DS > COMP > TRANS > BITCH-SLAP > CLIP > OUTPUT

    (TAPE first: as if the track was recorded to tape and you're mixing
    from the machine. ORDER's TAPE AT THE END moves it to just before CLIP.
    BITCH-SLAP's panel is in the WEAPON SELECT rack; TAPE has its old place.)

    How it fits together:
      - Settings. The strip shows your DAW one list of settings: every
        module's, with the module's name in front ("Gate Threshold"). Each
        block of audio, any that changed are handed to the module. Changes
        made in a module's own window (knobs, its presets, its INIT, A / B)
        come back the other way, so automation and saving always see them.
      - Power. A module's BYPASS is its power switch. Once it's been off
        for 40 ms it isn't run at all - the strip just delays the sound by
        the module's own delay - so a switched-off module costs nothing,
        and switching never shifts the timing. Switching back on, the
        module warms up silently, then crossfades in.
      - Sidechain: one input, shared by GATE and COMP.
      - OVERSAMPLING: one setting for AMP, TAPE and CLIP.
      - The strip's delay is all the modules' delays added up (switched
        off or not), so it only changes when the EQ's MASTER is switched.
      - INPUT: TRIM and a polarity flip. OUTPUT: LEVEL, with peak and LUFS
        meters.
      - Saving: the strip's settings, plus each module's own extras (its
        preset name, the EQ's other A / B set).
*/
class EasyStripAudioProcessor;

/** The modules, made before the strip's settings (which are built from theirs). */
struct StripModules
{
    template <typename Base>
    struct Module : public Base
    {
        explicit Module (juce::String folder) : presetFolder (std::move (folder)) {}
        juce::String getPresetFolderName() const override   { return presetFolder; }   // (your own presets: shared with the standalone)
        juce::String presetFolder;
    };

    Module<EasyGateAudioProcessor>      gate  { "Easy Gate" };
    Module<EasyAmpAudioProcessor>       amp   { "Easy Amp" };
    Module<EasyEQAudioProcessor>        eq    { "Easy EQ" };
    Module<EasyDSAudioProcessor>        ds    { "Easy DS" };
    Module<EasyCompAudioProcessor>      comp  { "Easy Comp" };
    Module<EasyTransientAudioProcessor> trans { "Easy Transient" };
    Module<BitchSlapAudioProcessor>     slap  { "Bitch-Slap" };
    Module<EasyClipAudioProcessor>      clip  { "Easy Clip" };
    Module<EasyTapeAudioProcessor>      tape  { "Easy Tape" };

    // (The numbers are what the strip's saved settings use: new modules go on the end.)
    enum Index { gateIndex, ampIndex, eqIndex, dsIndex, compIndex, transIndex, slapIndex, clipIndex, tapeIndex };
    static constexpr int numModules = 9;
    std::array<EasyProcessor*, numModules> all { &gate, &amp, &eq, &ds, &comp, &trans, &slap, &clip, &tape };

    static constexpr const char* keys[numModules]  { "gate", "amp", "eq", "ds", "comp", "trans", "slap", "clip", "tape" };
    static constexpr const char* names[numModules] { "Gate", "Amp", "EQ", "DS", "Comp", "Trans", "Bitch-Slap", "Clip", "Tape" };
    static constexpr const char* tabs[numModules]  { "GATE", "AMP", "EQ", "DS", "COMP", "TRANS", "BITCH-SLAP", "CLIP", "TAPE" };
    static bool hasSidechain (int m) noexcept       { return m == 0 || m == 4; }   // GATE and COMP
};

namespace StripIDs
{
    inline constexpr const char* trim         = "trim";
    inline constexpr const char* polarity     = "polarity";
    inline constexpr const char* level        = "level";
    inline constexpr const char* oversampling = "oversampling";

    // ORDER: five switches (the panels never move; only the order the sound goes through)
    inline constexpr const char* eqAfterComp      = "eqaftercomp";       // COMP, then EQ
    inline constexpr const char* deEssBeforeDrive = "deessbeforedrive";  // DS before AMP
    inline constexpr const char* driveAfterComp   = "driveaftercomp";    // COMP, then AMP
    inline constexpr const char* shapeBeforeComp  = "shapebeforecomp";   // TRANS before COMP
    inline constexpr const char* tapeAtEnd        = "tapeatend";         // TAPE just before CLIP, not first
    inline constexpr const char* orderSwitches[] { eqAfterComp, deEssBeforeDrive, driveAfterComp, shapeBeforeComp, tapeAtEnd };
    inline constexpr const char* orderNames[]    { "EQ AFTER COMP", "DE-ESS BEFORE DRIVE", "DRIVE AFTER COMP", "SHAPE BEFORE COMP", "TAPE AT THE END" };
    inline constexpr int numOrderSwitches = 5;
    inline juce::String moduleID (int module, const juce::String& id)   { return juce::String (StripModules::keys[module]) + "_" + id; }
}

//==============================================================================
class EasyStripAudioProcessor : private StripModules,
                                public EasyProcessor,
                                private juce::AsyncUpdater
{
public:
    EasyStripAudioProcessor();
    ~EasyStripAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using EasyProcessor::processBlock;
    void reset() override;
    juce::AudioProcessorEditor* createEditor() override;
    double getTailLengthSeconds() const override   { return 2.0; }   // (Bitch-Slap's tail)

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==== For the window ======================================================
    static constexpr int numModules = StripModules::numModules;
    EasyProcessor& getModule (int m) noexcept           { return *all[(size_t) m]; }
    EasyGateAudioProcessor&      getGate() noexcept     { return gate; }
    EasyAmpAudioProcessor&       getAmp() noexcept      { return amp; }
    EasyEQAudioProcessor&        getEQ() noexcept       { return eq; }
    EasyDSAudioProcessor&        getDS() noexcept       { return ds; }
    EasyCompAudioProcessor&      getComp() noexcept     { return comp; }
    EasyTransientAudioProcessor& getTrans() noexcept    { return trans; }
    BitchSlapAudioProcessor&     getSlap() noexcept     { return slap; }
    EasyClipAudioProcessor&      getClip() noexcept     { return clip; }
    EasyTapeAudioProcessor&      getTape() noexcept     { return tape; }
    bool isModuleOn (int m) const noexcept;

    /** The loudest the sound was going in / coming out since last asked (gain, not dB). */
    float takeInputPeak() noexcept                     { return inputPeak.exchange (0.0f); }
    float takeOutputPeak() noexcept                    { return outputPeak.exchange (0.0f); }
    float getShortTermLufs() const noexcept            { return loudness.getShortTermLufs(); }
    float getIntegratedLufs() const noexcept           { return loudness.getIntegratedLufs(); }
    void resetIntegratedLufs() noexcept                { loudness.resetIntegrated(); }

    //==== ORDER ===============================================================
    using Order = std::array<int, StripModules::numModules>;

    /** The order the modules run in, for a set of the five switches (see StripIDs). TAPE
        first (or, TAPE AT THE END, just before CLIP), then GATE; BITCH-SLAP and CLIP last. */
    static Order orderFor (bool eqAfterComp, bool deEssBeforeDrive, bool driveAfterComp, bool shapeBeforeComp,
                           bool tapeAtEnd = false) noexcept;

    /** The order the switches ask for right now (the sound follows within ~20 ms). */
    Order getWantedOrder() const noexcept;

    /** e.g. "TAPE > GATE > DS > AMP > EQ > COMP > TRANS > BITCH-SLAP > CLIP" */
    static juce::String describe (const Order&);
    bool isStandardOrder() const noexcept   { return getWantedOrder() == orderFor (false, false, false, false); }

    /** How many dB the strip's AUTO-GAIN would add (or take away) right now. */
    float getAutoGainDb() const noexcept               { return autoGainDb.load(); }

    /** Hands changed settings to the modules from the window, for when the
        DAW isn't running audio (normally the audio does it, each block). */
    void pushSettingsIfIdle (int& lastSeenCount, int& quietFrames) noexcept
    {
        const int now = blocksProcessed.load();
        quietFrames = now == lastSeenCount ? quietFrames + 1 : 0;
        lastSeenCount = now;
        if (quietFrames > 3)
            pushSettings();
    }

    /** (Tests) whether a switched-off module is really being skipped. */
    bool isModuleSkipped (int m) const noexcept        { return links[(size_t) m].skipped; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (StripModules&);
    static std::vector<Preset> makeStripPresets (StripModules&);
    static juce::StringArray stripPresetIDs (StripModules&);
    void handleAsyncUpdate() override;
    void connectModules();
    void pushSettings() noexcept;
    int totalLatency() const noexcept;
    void updateAutoGain (double heardIn, double heardOut, int numSamples) noexcept;

    // One module setting and its twin in the strip's list
    struct Link : public juce::AudioProcessorParameter::Listener
    {
        EasyStripAudioProcessor* owner = nullptr;
        juce::RangedAudioParameter* strip = nullptr;
        juce::RangedAudioParameter* module = nullptr;
        std::atomic<float> last { -1.0f };
        std::atomic<bool> pushing { false };   // (set while the strip hands a value over, so it isn't sent back)

        void parameterValueChanged (int, float newValue) override;
        void parameterGestureChanged (int, bool starting) override;
    };

    struct ModuleLink
    {
        std::vector<std::unique_ptr<Link>> settings;
        juce::RangedAudioParameter* stripBypass = nullptr;
        std::vector<float> ring;            // the sound going in, for while it's skipped (and warming up)
        bool skipped = false;
        int offFor = 0, warmUp = 0, fadeIn = 0;
    };

    std::array<ModuleLink, numModules> links;
    juce::RangedAudioParameter* oversamplingParameter = nullptr;
    std::array<juce::RangedAudioParameter*, 3> qualitySettings {};   // AMP's, CLIP's and TAPE's own OVERSAMPLING
    float lastOversampling = -1.0f;

    std::atomic<float>* trimValue = nullptr;
    std::atomic<float>* polarityValue = nullptr;
    std::atomic<float>* levelValue = nullptr;
    std::atomic<float>* bypassValue = nullptr;
    std::atomic<float>* autoGainValue = nullptr;

    // AUTO-GAIN for the whole strip
    LoudnessMatcher autoIn, autoOut;   // (only their ear filters are used)
    double autoListenIn = 0.0, autoListenOut = 0.0;
    float autoTrim = 1.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> autoGainSmoothed;
    std::atomic<float> autoGainDb { 0.0f };

    juce::SmoothedValue<float> inputGain, outputGain, bypassFade;
    juce::AudioBuffer<float> sidechain, work4, dryRing;
    int ringSize = 1, ringMask = 0, writePos = 0, channelsInUse = 2;
    int reportedLatency = 0;

    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };
    std::atomic<int> blocksProcessed { 0 };

    // ORDER: changing it dips the sound (~30 ms), swaps, and comes back (no clicks)
    std::array<std::atomic<float>*, StripIDs::numOrderSwitches> orderValues {};
    Order order {};
    enum class OrderFade { steady, out, hold, in } orderFade = OrderFade::steady;
    float orderGain = 1.0f;
    int orderHold = 0;   // (after swapping, stay silent while the new order's filters settle)
    LoudnessMeter loudness;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyStripAudioProcessor)
};
