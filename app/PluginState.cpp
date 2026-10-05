#include "PluginState.hpp"

#include <nlohmann/json.hpp>

namespace {
MidiEngine::MapBank mapsFor(const PluginState::Snapshot& state)
{
    if (state.profile)
        return velcal::effectiveMaps(*state.profile);
    MidiEngine::MapBank maps;
    for (auto& map : maps)
        map = velcal::VelocityMap::identity();
    return maps;
}
}

PluginState::PluginState()
{
    midi.enableHostMode();
    velcal::CalibrationProfile profile;
    profile.profileName = "DAW MIDI calibration";
    profile.generated = velcal::calibrate({});
    current.profile = std::move(profile);
}

PluginState::Snapshot PluginState::snapshot() const
{
    const std::scoped_lock lock(mutex);
    return current;
}

bool PluginState::publish(Snapshot next, std::uint64_t expectedRevision)
{
    const auto maps = mapsFor(next);
    {
        const std::scoped_lock lock(mutex);
        if (current.revision != expectedRevision)
            return false;
        next.revision = current.revision + 1;
        current = std::move(next);
        midi.setMaps(maps);
    }
    if (onChange)
        onChange();
    return true;
}

std::string PluginState::serialize() const
{
    const auto state = snapshot();
    nlohmann::json json = {{"velcalPluginState", 1}, {"dirty", state.dirty},
        {"keyGroup", state.keyGroup}, {"profilePath", state.profileFile.getFullPathName().toStdString()}};
    json["profile"] = state.profile
        ? nlohmann::json::parse(velcal::serializeProfile(*state.profile)) : nlohmann::json(nullptr);
    return json.dump();
}

bool PluginState::restore(const std::string& data)
{
    try {
        const auto json = nlohmann::json::parse(data);
        if (json.at("velcalPluginState").get<int>() != 1)
            return false;
        Snapshot next;
        if (!json.at("profile").is_null())
            next.profile = velcal::deserializeProfile(json.at("profile").dump());
        next.dirty = json.value("dirty", false);
        next.keyGroup = json.value("keyGroup", 1) == 2 ? 2 : 1;
        const auto path = juce::String(json.value("profilePath", std::string{}));
        if (juce::File::isAbsolutePath(path))
            next.profileFile = juce::File(path);
        const auto maps = mapsFor(next);
        const std::scoped_lock lock(mutex);
        next.revision = current.revision + 1;
        current = std::move(next);
        midi.setMaps(maps);
        midi.stopHostCapture();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::uint64_t PluginState::revision() const
{
    const std::scoped_lock lock(mutex);
    return current.revision;
}
