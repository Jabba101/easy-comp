#pragma once

#include "EasyProcessor.h"
#include "LoudnessMeter.h"
#include "GainStager.h"
#include "MixNetwork.h"
#include "ConsoleColour.h"
#include "../EasyGate/PluginProcessor.h"
#include "../EasyAmp/PluginProcessor.h"
#include "../EasyEQ/PluginProcessor.h"
#include "../EasyDS/PluginProcessor.h"
#include "../EasyComp/PluginProcessor.h"
#include "../EasyTransient/PluginProcessor.h"
#include "../BitchSlap/PluginProcessor.h"
#include "../EasyClip/PluginProcessor.h"
#include "../EasyTape/PluginProcessor.h"
#include "../EasyDelay/PluginProcessor.h"
#include "../EasyTremolo/PluginProcessor.h"
#include "../EasyWah/PluginProcessor.h"
#include "../EasyFlange/PluginProcessor.h"
#include "../EasyFilter/PluginProcessor.h"
#include "../EasyLimiter/PluginProcessor.h"

//==============================================================================
/**
    The Aardvark Easy Strip: every Easy plugin in one channel strip.

    Each module IS its standalone plugin - the very same code, sound,
    presets and window - running inside the strip, in this order:

        INPUT > TAPE > GATE > WAH > AMP > EQ > DS > COMP > TRANS > FLANGE > FILTER > TREMOLO > DELAY > VERB > CLIP > LIMITER > OUTPUT

    (TAPE first: as if the track was recorded to tape and you're mixing
    from the machine. ORDER's TAPE AT THE END moves it to just before CLIP.
    WAH, FLANGE, FILTER, TREMOLO, DELAY, VERB and LIMITER's panels are in the WEAPON
    SELECT rack: WAH straight after GATE (before AMP, like a pedal into an amp,
    and before anything evens out how hard you play), FLANGE, FILTER and TREMOLO
    after COMP (or COMP would even out the pulsing), DELAY before
    VERB, so the reverb catches the echoes.)

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
    Module<EasyDelayAudioProcessor>     delay { "Easy Delay" };
    Module<EasyTremoloAudioProcessor>   trem  { "Easy Tremolo" };
    Module<EasyWahAudioProcessor>       wah   { "Easy Wah" };
    Module<EasyFlangeAudioProcessor>    flange { "Easy Flange" };
    Module<EasyFilterAudioProcessor>    filter { "Easy Filter" };
    Module<EasyLimiterAudioProcessor>   limiter { "Easy Limiter" };

    // (The numbers are what the strip's saved settings use: new modules go on the end.)
    enum Index { gateIndex, ampIndex, eqIndex, dsIndex, compIndex, transIndex, slapIndex, clipIndex, tapeIndex, delayIndex, tremIndex, wahIndex, flangeIndex, filterIndex, limiterIndex };
    static constexpr int numModules = 15;
    std::array<EasyProcessor*, numModules> all { &gate, &amp, &eq, &ds, &comp, &trans, &slap, &clip, &tape, &delay, &trem, &wah, &flange, &filter, &limiter };

    static constexpr const char* keys[numModules]  { "gate", "amp", "eq", "ds", "comp", "trans", "slap", "clip", "tape", "delay", "tremolo", "wah", "flange", "filter", "limiter" };
    static constexpr const char* names[numModules] { "Gate", "Amp", "EQ", "DS", "Comp", "Trans", "Verb", "Clip", "Tape", "Delay", "Tremolo", "Wah", "Flange", "Filter", "Limiter" };
    static constexpr const char* tabs[numModules]  { "GATE", "AMP", "EQ", "DS", "COMP", "TRANS", "VERB", "CLIP", "TAPE", "DELAY", "TREMOLO", "WAH", "FLANGE", "FILTER", "LIMITER" };

    /** The effects that live in the WEAPON SELECT rack (not on the main page). */
    static bool isRackEffect (int m) noexcept       { return m == slapIndex || m == delayIndex || m == tremIndex || m == wahIndex || m == flangeIndex || m == filterIndex || m == limiterIndex; }
    static bool hasSidechain (int m) noexcept       { return m == 0 || m == 4; }   // GATE and COMP
};

namespace StripIDs
{
    inline constexpr const char* trim         = "trim";
    inline constexpr const char* polarity     = "polarity";
    inline constexpr const char* level        = "level";
    inline constexpr const char* oversampling = "oversampling";
    inline constexpr const char* desk         = "desk";          // DESK: the console colour (0 = none)
    inline constexpr const char* deskDrive    = "deskdrive";     // ... and how hard it's driven

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
                                public Mix::Member,
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
    double getTailLengthSeconds() const override;   // (VERB's and DELAY's tails)

    /** Your DAW tells the plugin the track's name (and again whenever it's renamed). */
    void updateTrackProperties (const TrackProperties&) override;
    /** The track's name, as your DAW last said (empty if it hasn't). Message thread only. */
    juce::String getTrackName() const   { return trackName; }

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
    EasyDelayAudioProcessor&     getDelay() noexcept    { return delay; }
    EasyTremoloAudioProcessor&   getTremolo() noexcept  { return trem; }
    EasyWahAudioProcessor&       getWah() noexcept      { return wah; }
    EasyFlangeAudioProcessor&    getFlange() noexcept   { return flange; }
    EasyFilterAudioProcessor&    getFilter() noexcept   { return filter; }
    EasyLimiterAudioProcessor&   getLimiter() noexcept  { return limiter; }
    bool isModuleOn (int m) const noexcept;

    /** The loudest the sound was going in / coming out since last asked (gain, not dB). */
    float takeInputPeak() noexcept                     { return inputPeak.exchange (0.0f); }

    /** Resting: nothing coming in and nothing ringing out, so the modules are skipped (see SLEEP below). */
    bool isAsleep() const noexcept                     { return asleepNow.load(); }
    /** (Tests) switch the resting off, to compare the sound with and without it. */
    static void setSleepAllowedForTesting (bool allowed) noexcept   { sleepAllowed().store (allowed); }
    float takeOutputPeak() noexcept                    { return outputPeak.exchange (0.0f); }
    float getShortTermLufs() const noexcept            { return loudness.getShortTermLufs(); }
    float getIntegratedLufs() const noexcept           { return loudness.getIntegratedLufs(); }
    void resetIntegratedLufs() noexcept                { loudness.resetIntegrated(); }

    //==== ORDER ===============================================================
    using Order = std::array<int, StripModules::numModules>;

    /** The order the modules run in, for a set of the five switches (see StripIDs). TAPE
        first (or, TAPE AT THE END, just before CLIP), then GATE; VERB and CLIP last. */
    static Order orderFor (bool eqAfterComp, bool deEssBeforeDrive, bool driveAfterComp, bool shapeBeforeComp,
                           bool tapeAtEnd = false) noexcept;

    /** The order the switches ask for right now (the sound follows within ~20 ms). */
    Order getWantedOrder() const noexcept;

    /** e.g. "TAPE > GATE > DS > AMP > EQ > COMP > TRANS > VERB > CLIP" */
    static juce::String describe (const Order&);
    bool isStandardOrder() const noexcept   { return getWantedOrder() == orderFor (false, false, false, false); }

    //==== LEARN: gain staging (INPUT) =========================================
    /** Starts listening to the track coming in (while the DAW plays); it sets TRIM by itself after 30 s of sound. */
    void startLearning();
    /** Pressed again: finishes early and sets TRIM (from 3 s of sound on; otherwise just stops). */
    void finishLearning();
    bool isLearning() const noexcept                    { return stager.isListening(); }
    double getLearnSeconds() const noexcept             { return stager.heardSeconds(); }
    /** The last time LEARN set TRIM (message thread). learnCount goes up each time. */
    GainStager::Result getLastLearn() const noexcept    { return lastLearn; }
    int getLearnCount() const noexcept                  { return learnCount; }
    void applyPendingUpdates()                          { handleUpdateNowIfNeeded(); }   // (tests: what the message thread would do next)

    //==== THE MIX (every Easy Strip in the project) ==========================
    juce::String getMixName() const override             { return trackName.isNotEmpty() ? trackName : juce::String ("Easy Strip"); }
    juce::String getMixPreset() const override;
    int getChosenRole() const override                   { return (int) parameters.state.getProperty ("mixRole", (int) Mix::automatic); }
    void setChosenRole (int role) override               { parameters.state.setProperty ("mixRole", role, nullptr); }
    float getMixLevelDb() const override                 { return levelValue->load(); }
    void setMixLevelDb (float db) override;
    float getMixTrimDb() const override                  { return trimValue->load(); }
    void startMixLearn() override                        { startLearning(); }
    int getMixDesk() const override                      { return juce::roundToInt (deskValue->load()); }
    int getMixDeskDrive() const override                 { return juce::roundToInt (deskDriveValue->load()); }
    void setMixDesk (int desk, int drive) override;
    /** A strip preset, and - if it names one - its DESK (other presets leave the DESK as it is). */
    void loadPreset (const Preset&) override;
    bool isMixLearning() const override                  { return isLearning(); }
    Mix::Listener& getMixListener() override             { return mixListener; }
    float getLevelWhileListening() const override        { return levelWhileListening.load(); }

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
    /** What each module is told about the song's position: the DAW's, moved on to the chunk it's working on and back
        by the delay of the modules before it - so TREMOLO, WAH, FLANGE and FILTER lock to the beat the listener hears,
        whatever comes before them (the EQ's MASTER mode delays the sound by up to 43 ms). */
    struct ModuleHead : public juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        {
            if (! valid) return {};
            auto p = info;
            if (shift != 0)
            {
                if (auto t = p.getTimeInSamples()) p.setTimeInSamples (*t + shift);
                if (auto t = p.getTimeInSeconds()) p.setTimeInSeconds (*t + (double) shift / rate);
                if (auto q = p.getPpqPosition())
                    if (auto bpm = p.getBpm())
                        p.setPpqPosition (*q + (double) shift / rate * *bpm / 60.0);
            }
            return p;
        }
        PositionInfo info;
        bool valid = false;
        int64_t shift = 0;
        double rate = 48000.0;
    };
    std::array<ModuleHead, StripModules::numModules> moduleHeads;

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
    juce::String trackName;

    std::atomic<float>* trimValue = nullptr;
    std::atomic<float>* polarityValue = nullptr;
    std::atomic<float>* levelValue = nullptr;
    std::atomic<float>* bypassValue = nullptr;
    std::atomic<float>* autoGainValue = nullptr;
    std::atomic<float>* deskValue = nullptr;
    std::atomic<float>* deskDriveValue = nullptr;
    ConsoleColour desk;

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

    // SLEEP: with nothing coming in (track and sidechain digitally silent for 2 s) and nothing still
    // ringing out (under -120 dBFS for 1 s), the modules are left exactly as they are and skipped,
    // until sound arrives again. A silent track then costs next to nothing.
    int inputQuietFor = 0, outputQuietFor = 0;
    bool asleep = false;
    std::atomic<bool> asleepNow { false }, settingTouched { false };   // (a setting changed: stay awake to hear what it does)
    static std::atomic<bool>& sleepAllowed() noexcept   { static std::atomic<bool> allowed { true }; return allowed; }

    // ORDER: changing it dips the sound (~30 ms), swaps, and comes back (no clicks)
    std::array<std::atomic<float>*, StripIDs::numOrderSwitches> orderValues {};
    Order order {};
    enum class OrderFade { steady, out, hold, in } orderFade = OrderFade::steady;
    float orderGain = 1.0f;
    int orderHold = 0;   // (after swapping, stay silent while the new order's filters settle)
    LoudnessMeter loudness;
    GainStager stager;
    Mix::Listener mixListener;
    int mixRound = -1;
    double mixPlayed = 0.0;
    std::atomic<float> levelWhileListening { 0.0f };
    GainStager::Result lastLearn;
    int learnCount = 0;
    void applyLearned();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyStripAudioProcessor)
};
