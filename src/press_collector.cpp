#include "velcal/calibration.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>

namespace velcal {
namespace {

double median(std::vector<double> values)
{
    if (values.empty())
        return 0.0;

    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    if ((values.size() % 2) != 0)
        return *middle;

    const auto lower = std::max_element(values.begin(), middle);
    return (*lower + *middle) * 0.5;
}

} // namespace

bool isBlackKey(const std::uint8_t note) noexcept
{
    switch (note % 12) {
    case 1:
    case 3:
    case 6:
    case 8:
    case 10:
        return true;
    default:
        return false;
    }
}

bool belongsToGroup(const std::uint8_t note, const KeyGroup group) noexcept
{
    return isBlackKey(note) == (group == KeyGroup::blackKeys);
}

PressCollector::PressCollector(
    const KeyGroup group,
    PressCollectorConfig config,
    const SegmentId segmentId)
    : group_(group), config_(config), segmentId_(segmentId)
{
}

std::size_t PressCollector::expectedNoteCount() const noexcept
{
    std::size_t count = 0;
    for (int note = config_.lowestNote; note <= config_.highestNote; ++note) {
        if (belongsToGroup(static_cast<std::uint8_t>(note), group_))
            ++count;
    }
    return count;
}

void PressCollector::begin(const NoteOn event)
{
    current_.clear();
    current_.push_back(event);
}

std::optional<CalibrationPress> PressCollector::add(const NoteOn event)
{
    if (current_.empty()) {
        begin(event);
        return std::nullopt;
    }

    const auto sinceLast = event.timestampUs - current_.back().timestampUs;
    const auto sinceFirst = event.timestampUs - current_.front().timestampUs;
    if (sinceLast > config_.quietWindowUs || sinceFirst > config_.maximumPressSpanUs) {
        auto completed = finishCurrent();
        begin(event);
        return completed;
    }

    current_.push_back(event);
    return std::nullopt;
}

std::optional<CalibrationPress> PressCollector::flush()
{
    if (current_.empty())
        return std::nullopt;
    return finishCurrent();
}

std::optional<CalibrationPress> PressCollector::flushIfIdle(const TimestampUs nowUs)
{
    if (current_.empty() || nowUs - current_.back().timestampUs <= config_.quietWindowUs)
        return std::nullopt;
    return finishCurrent();
}

CalibrationPress PressCollector::finishCurrent()
{
    CalibrationPress press;
    press.sequence = nextSequence_++;
    press.segmentId = segmentId_;
    press.group = group_;
    press.startedAtUs = current_.front().timestampUs;
    press.endedAtUs = current_.back().timestampUs;
    press.notes = std::move(current_);
    current_.clear();

    std::set<std::uint8_t> uniqueExpectedNotes;
    std::vector<double> velocities;
    for (const auto& event : press.notes) {
        if (event.velocity == 0) {
            press.issues.push_back(PressIssue::invalidVelocity);
            continue;
        }
        if (event.note < config_.lowestNote || event.note > config_.highestNote
            || !belongsToGroup(event.note, group_)) {
            press.issues.push_back(PressIssue::unexpectedNote);
            continue;
        }
        if (!uniqueExpectedNotes.insert(event.note).second) {
            press.issues.push_back(PressIssue::duplicateNote);
            continue;
        }
        velocities.push_back(event.velocity);
    }

    const auto expected = expectedNoteCount();
    const auto completion = expected == 0
        ? 0.0
        : static_cast<double>(uniqueExpectedNotes.size()) / static_cast<double>(expected);
    if (completion < config_.minimumCompletionRatio)
        press.issues.push_back(PressIssue::incomplete);

    press.referenceVelocity = median(std::move(velocities));
    press.accepted = press.issues.empty();
    return press;
}

SectionCaptureAnalysis analyzeSectionCapture(
    const std::vector<NoteOn>& events,
    const KeyGroup group,
    const SegmentId segmentId,
    const SectionCaptureConfig& config)
{
    SectionCaptureAnalysis analysis;
    if (events.empty())
        return analysis;

    std::vector<std::vector<NoteOn>> preliminary;
    for (const auto& event : events) {
        if (preliminary.empty()
            || event.timestampUs - preliminary.back().back().timestampUs
                > config.press.quietWindowUs
            || event.timestampUs - preliminary.back().front().timestampUs
                > config.press.maximumPressSpanUs) {
            preliminary.push_back({event});
        } else {
            preliminary.back().push_back(event);
        }
    }
    analysis.preliminaryPresses = preliminary.size();
    if (preliminary.size() < config.minimumPressesForInference)
        return analysis;

    std::array<std::size_t, 128> occurrences{};
    for (const auto& press : preliminary) {
        std::array<bool, 128> seen{};
        for (const auto& event : press) {
            if (event.velocity != 0 && belongsToGroup(event.note, group))
                seen[event.note] = true;
        }
        for (std::size_t note = 0; note < seen.size(); ++note) {
            if (seen[note])
                ++occurrences[note];
        }
    }

    const auto requiredOccurrences = static_cast<std::size_t>(std::ceil(
        config.minimumNoteOccurrenceRatio * static_cast<double>(preliminary.size())));
    std::optional<std::uint8_t> lowest;
    std::optional<std::uint8_t> highest;
    for (std::size_t note = 0; note < occurrences.size(); ++note) {
        if (occurrences[note] < requiredOccurrences)
            continue;
        const auto midiNote = static_cast<std::uint8_t>(note);
        if (!lowest)
            lowest = midiNote;
        highest = midiNote;
    }
    if (!lowest || !highest)
        return analysis;

    analysis.rangeInferred = true;
    analysis.lowestNote = *lowest;
    analysis.highestNote = *highest;

    auto collectorConfig = config.press;
    collectorConfig.lowestNote = *lowest;
    collectorConfig.highestNote = *highest;
    PressCollector collector(group, collectorConfig, segmentId);
    for (const auto& event : events) {
        if (const auto completed = collector.add(event))
            analysis.presses.push_back(*completed);
    }
    if (const auto completed = collector.flush())
        analysis.presses.push_back(*completed);
    return analysis;
}

} // namespace velcal
