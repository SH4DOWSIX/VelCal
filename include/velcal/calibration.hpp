#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace velcal {

using TimestampUs = std::int64_t;
using SegmentId = std::uint32_t;

enum class KeyGroup {
    whiteKeys,
    blackKeys,
};

bool isBlackKey(std::uint8_t note) noexcept;
bool belongsToGroup(std::uint8_t note, KeyGroup group) noexcept;

struct NoteOn {
    std::uint8_t note{};
    std::uint8_t velocity{};
    TimestampUs timestampUs{};
};

enum class PressIssue {
    incomplete,
    duplicateNote,
    unexpectedNote,
    invalidVelocity,
};

struct CalibrationPress {
    std::uint64_t sequence{};
    SegmentId segmentId{};
    KeyGroup group{KeyGroup::whiteKeys};
    TimestampUs startedAtUs{};
    TimestampUs endedAtUs{};
    std::vector<NoteOn> notes;
    std::vector<PressIssue> issues;
    bool accepted{};
    double referenceVelocity{};
};

struct PressCollectorConfig {
    std::uint8_t lowestNote{21};
    std::uint8_t highestNote{108};
    double minimumCompletionRatio{0.90};
    TimestampUs quietWindowUs{250'000};
    TimestampUs maximumPressSpanUs{400'000};
};

class PressCollector {
public:
    PressCollector(KeyGroup group, PressCollectorConfig config = {}, SegmentId segmentId = 0);

    std::optional<CalibrationPress> add(NoteOn event);
    std::optional<CalibrationPress> flush();
    std::optional<CalibrationPress> flushIfIdle(TimestampUs nowUs);
    std::size_t expectedNoteCount() const noexcept;

private:
    CalibrationPress finishCurrent();
    void begin(NoteOn event);

    KeyGroup group_;
    PressCollectorConfig config_;
    SegmentId segmentId_{};
    std::vector<NoteOn> current_;
    std::uint64_t nextSequence_{1};
};

struct SectionCaptureConfig {
    PressCollectorConfig press;
    std::size_t minimumPressesForInference{3};
    double minimumNoteOccurrenceRatio{0.60};
};

struct SectionCaptureAnalysis {
    bool rangeInferred{};
    std::uint8_t lowestNote{};
    std::uint8_t highestNote{};
    std::size_t preliminaryPresses{};
    std::vector<CalibrationPress> presses;
};

SectionCaptureAnalysis analyzeSectionCapture(
    const std::vector<NoteOn>& events,
    KeyGroup group,
    SegmentId segmentId,
    const SectionCaptureConfig& config = {});

struct CalibrationConfig {
    std::size_t velocityBinWidth{16};
    std::size_t minimumSamplesPerKey{6};
    std::size_t samplesForFullStrength{24};
    std::size_t desiredSamplesPerRegion{8};
    double deadband{2.0};
    double strength{1.0};
    double outlierSigma{3.5};
    double minimumOutlierThreshold{6.0};
    std::size_t minimumSharedNotesPerSegment{2};
};

struct VelocityMap {
    std::array<std::uint8_t, 128> values{};

    static VelocityMap identity() noexcept;
    std::uint8_t apply(std::uint8_t velocity) const noexcept;
    bool isMonotonic() const noexcept;
};

struct VelocityCurvePoint {
    double input{1.0};
    double output{1.0};
};

std::optional<std::size_t> insertVelocityCurvePoint(
    std::vector<VelocityCurvePoint>& points, double input, double output);
void moveVelocityCurvePoint(
    std::vector<VelocityCurvePoint>& points, std::size_t index, double input, double output);

VelocityMap makeVelocityCurve(
    double curvature,
    std::uint8_t minimumOutput = 1,
    std::uint8_t maximumOutput = 127) noexcept;
double evaluateVelocityCurve(
    const std::vector<VelocityCurvePoint>& points,
    bool smooth,
    double input) noexcept;
VelocityMap makeVelocityCurve(
    const std::vector<VelocityCurvePoint>& points,
    bool smooth) noexcept;

struct NoteCalibrationStats {
    std::size_t samplesSeen{};
    std::size_t samplesUsed{};
    std::array<std::size_t, 3> samplesUsedByRegion{};
    double medianRawMinusReference{};
    double confidence{};
};

struct Coverage {
    std::size_t lowPresses{};
    std::size_t mediumPresses{};
    std::size_t highPresses{};
    double score{};
};

double regionalCoverageScore(
    const std::array<NoteCalibrationStats, 128>& stats, std::size_t target) noexcept;

struct SegmentAlignment {
    SegmentId segmentId{};
    KeyGroup group{KeyGroup::whiteKeys};
    std::array<double, 3> velocityRegionScales{1.0, 1.0, 1.0};
    bool connectedToRoot{};
};

struct CalibrationResult {
    std::array<VelocityMap, 128> noteMaps{};
    std::array<NoteCalibrationStats, 128> noteStats{};
    Coverage coverage;
    std::vector<SegmentAlignment> segmentAlignments;
    bool segmentsConnected{true};
};

struct ValidationMetrics {
    std::size_t presses{};
    std::size_t observations{};
    double rawMeanAbsoluteDeviation{};
    double calibratedMeanAbsoluteDeviation{};
    double improvementFraction{};
};

CalibrationResult calibrate(
    const std::vector<CalibrationPress>& presses,
    const CalibrationConfig& config = {});

ValidationMetrics validate(
    const std::vector<CalibrationPress>& presses,
    const std::array<VelocityMap, 128>& maps);

} // namespace velcal
