#pragma once

#include "HoloControls.h"
#include "PluginProcessor.h"   // (the strip's: StripModules, and the module processors' types - headers only)

//==============================================================================
// HOLO_ONLY_MODULE: a standalone plugin builds the hologram parts for its own
// module only (so it doesn't need the other fourteen processors). -1 = all.
#ifndef HOLO_ONLY_MODULE
 #define HOLO_ONLY_MODULE -1
#endif
#define HOLO_WANTS(m) (HOLO_ONLY_MODULE < 0 || HOLO_ONLY_MODULE == (m))

namespace Holo
{
    class Screen;

    //==========================================================================
    /**
        What a module's panel ("slab") and the big screen need from the window
        around them. The Easy Strip's View is one; each standalone plugin's
        SoloView is the other (with just its own module).
    */
    class SlabHost
    {
    public:
        virtual ~SlabHost() = default;

        virtual EasyProcessor& getModule (int module) = 0;
        virtual bool isModuleOn (int module) const = 0;
        virtual int getMainBusNumInputChannels() const = 0;

        virtual void addHint (juce::Component&, const juce::String& title, const juce::String& text) = 0;
        virtual juce::String moduleHint (int module, const juce::String& title) const = 0;
        virtual void registerControl (int module, const juce::String& parameterID, ParamControl&) = 0;
        virtual void announce (const juce::String& title, const juce::String& text, int seconds) = 0;
        virtual void showPresetMenu (int module, juce::Component& under) = 0;
        virtual void setClipAdvancedShown (bool) {}
        virtual Screen& getScreen() = 0;

        // The module processors, by type
        EasyGateAudioProcessor&      getGate()      { return static_cast<EasyGateAudioProcessor&>      (getModule (StripModules::gateIndex)); }
        EasyAmpAudioProcessor&       getAmp()       { return static_cast<EasyAmpAudioProcessor&>       (getModule (StripModules::ampIndex)); }
        EasyEQAudioProcessor&        getEQ()        { return static_cast<EasyEQAudioProcessor&>        (getModule (StripModules::eqIndex)); }
        EasyDSAudioProcessor&        getDS()        { return static_cast<EasyDSAudioProcessor&>        (getModule (StripModules::dsIndex)); }
        EasyCompAudioProcessor&      getComp()      { return static_cast<EasyCompAudioProcessor&>      (getModule (StripModules::compIndex)); }
        EasyTransientAudioProcessor& getTrans()     { return static_cast<EasyTransientAudioProcessor&> (getModule (StripModules::transIndex)); }
        BitchSlapAudioProcessor&     getSlap()      { return static_cast<BitchSlapAudioProcessor&>     (getModule (StripModules::slapIndex)); }
        EasyClipAudioProcessor&      getClip()      { return static_cast<EasyClipAudioProcessor&>      (getModule (StripModules::clipIndex)); }
        EasyTapeAudioProcessor&      getTape()      { return static_cast<EasyTapeAudioProcessor&>      (getModule (StripModules::tapeIndex)); }
        EasyLimiterAudioProcessor&   getLimiter()   { return static_cast<EasyLimiterAudioProcessor&>   (getModule (StripModules::limiterIndex)); }
    };
}
