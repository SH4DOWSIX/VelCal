#pragma once

#include "velcal/calibration.hpp"

#include <filesystem>
#include <string>

namespace velcal {

inline constexpr std::uint32_t currentProfileSchemaVersion = 4;
inline constexpr const char* currentAlgorithmVersion = "0.4.0";

struct MidiDeviceIdentity {
    std::string name;
    std::string endpointId;
    std::string manufacturer;
};

struct VelocityCurveSettings {
    std::string name{"Linear"};
    double curvature{};
    std::uint8_t minimumOutput{1};
    std::uint8_t maximumOutput{127};
    std::vector<VelocityCurvePoint> points;
    bool smooth{true};
};

struct NoteCurveOverride {
    std::vector<VelocityCurvePoint> points;
    bool smooth{true};
};

struct CalibrationProfile {
    std::uint32_t schemaVersion{currentProfileSchemaVersion};
    std::string profileName;
    std::string createdUtc;
    std::string algorithmVersion{currentAlgorithmVersion};
    MidiDeviceIdentity inputDevice;
    CalibrationConfig settings;
    std::vector<CalibrationPress> presses;
    CalibrationResult generated;
    std::array<int, 128> noteAdjustments{};
    std::array<NoteCurveOverride, 128> noteCurveOverrides{};
    VelocityCurveSettings globalCurve;
    std::vector<VelocityCurveSettings> userGlobalPresets;
};

void saveProfile(const CalibrationProfile& profile, const std::filesystem::path& path);
CalibrationProfile loadProfile(const std::filesystem::path& path);

} // namespace velcal
