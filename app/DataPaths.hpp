#pragma once

#include <JuceHeader.h>

inline juce::File velcalProfileDirectory()
{
    const auto overridePath = juce::SystemStats::getEnvironmentVariable("VELCAL_DATA_DIR", {});
    if (juce::File::isAbsolutePath(overridePath))
        return juce::File(overridePath).getChildFile("profiles");
#if VELCAL_INSTALLED
   #if JUCE_LINUX
    const auto xdg = juce::SystemStats::getEnvironmentVariable("XDG_DATA_HOME", {});
    const auto root = juce::File::isAbsolutePath(xdg) ? juce::File(xdg)
        : juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".local/share");
   #elif JUCE_MAC
    const auto root = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Library/Application Support");
   #else
    const auto root = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
   #endif
    return root.getChildFile("VelCal/profiles");
#else
    return juce::File(VELCAL_DEFAULT_PROFILE_DIR);
#endif
}
