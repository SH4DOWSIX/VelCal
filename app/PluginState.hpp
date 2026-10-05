#pragma once

#include "MidiEngine.hpp"
#include "velcal/profile.hpp"

#include <optional>

class PluginState final {
public:
    struct Snapshot {
        std::optional<velcal::CalibrationProfile> profile;
        juce::File profileFile;
        bool dirty{};
        int keyGroup{1};
        std::uint64_t revision{};
    };

    PluginState();
    Snapshot snapshot() const;
    bool publish(Snapshot next, std::uint64_t expectedRevision);
    std::string serialize() const;
    bool restore(const std::string& data);
    std::uint64_t revision() const;
    std::function<void()> onChange;
    MidiEngine midi;

private:
    mutable std::mutex mutex;
    Snapshot current;
};
