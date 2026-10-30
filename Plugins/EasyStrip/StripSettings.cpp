#include "StripSettings.h"

namespace
{
    juce::File& testFile()
    {
        static juce::File file;
        return file;
    }

    std::unique_ptr<juce::PropertiesFile> openFile()
    {
        if (testFile() != juce::File())
            return std::make_unique<juce::PropertiesFile> (testFile(), juce::PropertiesFile::Options());

        juce::PropertiesFile::Options options;
        options.applicationName     = "Easy Strip";
        options.folderName          = "Aardvark Easy Suite";
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        return std::make_unique<juce::PropertiesFile> (options);
    }
}

void StripSettings::useFileForTesting (const juce::File& file)
{
    testFile() = file;
}

StripSettings StripSettings::load()
{
    StripSettings s;
    auto file = openFile();
    s.scale     = juce::jlimit (0.5f, 2.0f, (float) file->getDoubleValue ("scale", s.scale));
    s.skin      = juce::jlimit (0, 1, file->getIntValue ("skin", s.skin));
    s.ticker    = file->getBoolValue ("ticker", s.ticker);
    s.meterZero = juce::jlimit (0, 1, file->getIntValue ("meterZero", s.meterZero));
    s.dim       = juce::jlimit (0, 2, file->getIntValue ("dim", s.dim));
    s.lufsMeter = file->getBoolValue ("lufsMeter", s.lufsMeter);
    return s;
}

void StripSettings::save() const
{
    auto file = openFile();
    file->setValue ("scale", (double) scale);
    file->setValue ("skin", skin);
    file->setValue ("ticker", ticker);
    file->setValue ("meterZero", meterZero);
    file->setValue ("dim", dim);
    file->setValue ("lufsMeter", lufsMeter);
    file->saveIfNeeded();
}
