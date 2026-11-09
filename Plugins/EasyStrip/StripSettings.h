#pragma once

#include <juce_data_structures/juce_data_structures.h>

//==============================================================================
/**
    The Easy Strip's SETUP choices. They belong to you, not to a project:
    one set for every Easy Strip, kept in a small settings file (on Windows,
    in AppData > Roaming > Aardvark Easy Suite).
*/
struct StripSettings
{
    enum Skin { hologram = 0, classic = 1 };

    float scale      = 0.8f;    // window size: 1 = 100% (the HOLOGRAM panel is 1800 wide at 100%)
    int   skin       = hologram;
    bool  ticker     = true;    // the hint strip along the bottom
    int   meterZero  = 0;       // what 0 on the INPUT / OUTPUT meters means: 0 = 0 dBFS, 1 = -18 dBFS
    int   dim        = 2;       // DISPLAY BRIGHTNESS: 0 = LOW, 1 = MEDIUM, 2 = HIGH (full)
    bool  lufsMeter  = false;   // the OUTPUT meter shows LUFS instead of PEAK

    float getMeterZeroDb() const noexcept   { return meterZero == 1 ? -18.0f : 0.0f; }

    static StripSettings load();
    void save() const;

    /** (Tests) keep the settings somewhere else. */
    static void useFileForTesting (const juce::File&);

    static constexpr float sizes[] { 0.6f, 0.7f, 0.8f, 0.9f, 1.0f, 1.25f, 1.5f };
};

//==============================================================================
/**
    The single plugins' window choices (Easy Comp, Easy Gate...): one set for
    all of them, kept beside the Easy Strip's.
*/
struct SoloSettings
{
    float scale  = 1.0f;                     // 1 = 100%
    int   skin   = StripSettings::hologram;  // HOLOGRAM or CLASSIC
    bool  ticker = true;                     // the hint strip along the bottom
    int   dim    = 2;                        // DISPLAY BRIGHTNESS: 0 LOW, 1 MEDIUM, 2 HIGH

    static SoloSettings load();
    void save() const;

    /** (Tests) keep the settings somewhere else. */
    static void useFileForTesting (const juce::File&);

    static constexpr float sizes[] { 0.75f, 0.9f, 1.0f, 1.15f, 1.3f, 1.5f, 1.75f };
};
