#include "velcal/calibration.hpp"
#include "velcal/profile.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace {

struct SectionKey {
    velcal::KeyGroup group{velcal::KeyGroup::whiteKeys};
    velcal::SegmentId segment{};

    bool operator<(const SectionKey& other) const noexcept
    {
        return std::tie(group, segment) < std::tie(other.group, other.segment);
    }
};

std::vector<std::string> splitCsvRow(const std::string& row)
{
    std::vector<std::string> fields;
    std::istringstream input(row);
    std::string field;
    while (std::getline(input, field, ','))
        fields.push_back(field);
    return fields;
}

const char* groupName(const velcal::KeyGroup group)
{
    return group == velcal::KeyGroup::whiteKeys ? "white" : "black";
}

std::vector<velcal::CalibrationPress> loadAndRegroup(const std::string& path)
{
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("could not open capture file");

    std::string row;
    std::getline(input, row);
    std::map<SectionKey, std::vector<velcal::NoteOn>> sections;
    while (std::getline(input, row)) {
        const auto fields = splitCsvRow(row);
        if (fields.size() < 10)
            continue;
        const SectionKey key{
            fields[1] == "black" ? velcal::KeyGroup::blackKeys : velcal::KeyGroup::whiteKeys,
            static_cast<velcal::SegmentId>(std::stoul(fields[0]))};
        sections[key].push_back({
            static_cast<std::uint8_t>(std::stoul(fields[6])),
            static_cast<std::uint8_t>(std::stoul(fields[8])),
            static_cast<velcal::TimestampUs>(std::stoll(fields[9]))});
    }

    std::vector<velcal::CalibrationPress> presses;
    for (auto& entry : sections) {
        auto& events = entry.second;
        if (events.empty())
            continue;
        std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
            return a.timestampUs < b.timestampUs;
        });
        const auto noteBounds = std::minmax_element(
            events.begin(), events.end(),
            [](const auto& a, const auto& b) { return a.note < b.note; });
        velcal::PressCollectorConfig config;
        config.lowestNote = noteBounds.first->note;
        config.highestNote = noteBounds.second->note;
        velcal::PressCollector collector(entry.first.group, config, entry.first.segment);
        for (const auto& event : events) {
            if (auto completed = collector.add(event))
                presses.push_back(std::move(*completed));
        }
        if (auto completed = collector.flush())
            presses.push_back(std::move(*completed));
    }
    return presses;
}

} // namespace

int main(const int argc, char** argv)
{
    if (argc < 2 || argc > 3) {
        std::cout << "Usage: velcal_analyze <capture.csv> [output.velcal.json]\n";
        return 1;
    }

    try {
        const auto presses = loadAndRegroup(argv[1]);
        const auto result = velcal::calibrate(presses);

        std::vector<velcal::CalibrationPress> trainingPresses;
        std::vector<velcal::CalibrationPress> heldOutPresses;
        std::map<SectionKey, std::size_t> acceptedIndex;
        for (const auto& press : presses) {
            if (!press.accepted) {
                trainingPresses.push_back(press);
                continue;
            }
            auto& index = acceptedIndex[{press.group, press.segmentId}];
            if ((index++ % 5) == 4)
                heldOutPresses.push_back(press);
            else
                trainingPresses.push_back(press);
        }
        const auto heldOutCalibration = velcal::calibrate(trainingPresses);
        const auto validation = velcal::validate(heldOutPresses, heldOutCalibration.noteMaps);

        std::cout << "Reprocessed " << presses.size() << " physical presses\n";
        std::map<SectionKey, std::array<std::size_t, 2>> counts;
        for (const auto& press : presses)
            ++counts[{press.group, press.segmentId}][press.accepted ? 0 : 1];
        for (const auto& entry : counts) {
            std::cout << "  " << groupName(entry.first.group) << " segment "
                      << entry.first.segment << ": " << entry.second[0]
                      << " accepted, " << entry.second[1] << " rejected\n";
        }

        std::cout << "Coverage: low=" << result.coverage.lowPresses
                  << ", medium=" << result.coverage.mediumPresses
                  << ", high=" << result.coverage.highPresses
                  << ", score=" << std::fixed << std::setprecision(2)
                  << result.coverage.score << '\n';
        std::cout << "Segment chains: "
                  << (result.segmentsConnected ? "connected" : "DISCONNECTED") << '\n';
        for (const auto& alignment : result.segmentAlignments) {
            std::cout << "  " << groupName(alignment.group) << " segment "
                      << alignment.segmentId << " scales: "
                      << alignment.velocityRegionScales[0] << ", "
                      << alignment.velocityRegionScales[1] << ", "
                      << alignment.velocityRegionScales[2] << '\n';
        }

        std::cout << "Held-out mean deviation (" << validation.presses << " presses): raw="
                  << validation.rawMeanAbsoluteDeviation << ", calibrated="
                  << validation.calibratedMeanAbsoluteDeviation << ", improvement="
                  << validation.improvementFraction * 100.0 << "%\n\n";
        std::cout << "Meaningful generated corrections:\n"
                  << "note  samples  bias    map(32,64,96)\n";
        for (std::size_t note = 0; note < 128; ++note) {
            const auto& stats = result.noteStats[note];
            const auto& map = result.noteMaps[note];
            if (stats.samplesUsed == 0)
                continue;
            const auto meaningful = std::abs(stats.medianRawMinusReference) >= 2.0
                || map.apply(32) != 32 || map.apply(64) != 64 || map.apply(96) != 96;
            if (!meaningful)
                continue;
            std::cout << std::setw(4) << note << std::setw(9) << stats.samplesUsed
                      << std::setw(7) << std::setprecision(1) << stats.medianRawMinusReference
                      << "    " << static_cast<int>(map.apply(32)) << ','
                      << static_cast<int>(map.apply(64)) << ','
                      << static_cast<int>(map.apply(96)) << '\n';
        }

        auto outputPath = argc == 3
            ? std::filesystem::path(argv[2])
            : std::filesystem::path(VELCAL_PROFILE_DIR)
                / (std::filesystem::path(argv[1]).stem().string() + ".velcal.json");
        velcal::CalibrationProfile profile;
        profile.profileName = std::filesystem::path(argv[1]).stem().string();
        profile.createdUtc = "imported-from-csv";
        profile.inputDevice.name = "Unknown MIDI input (CSV import)";
        profile.presses = presses;
        profile.generated = result;
        velcal::saveProfile(profile, outputPath);
        std::cout << "\nProfile saved to " << outputPath.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Analysis failed: " << error.what() << '\n';
        return 1;
    }
}
