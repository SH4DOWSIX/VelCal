#include "velcal/calibration.hpp"
#include "velcal/profile.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

velcal::CalibrationPress makePress(
    const std::uint64_t sequence,
    const int reference,
    const int hotNoteVelocity,
    const bool addOutlier = false)
{
    velcal::CalibrationPress press;
    press.sequence = sequence;
    press.group = velcal::KeyGroup::whiteKeys;
    press.accepted = true;
    press.referenceVelocity = reference;

    const std::vector<int> notes{60, 62, 64, 65, 67, 69, 71};
    for (const auto note : notes) {
        auto velocity = reference + static_cast<int>((sequence + note) % 3) - 1;
        if (note == 65)
            velocity = hotNoteVelocity;
        if (addOutlier && note == 69)
            velocity = 127;
        press.notes.push_back({
            static_cast<std::uint8_t>(note),
            static_cast<std::uint8_t>(velocity),
            static_cast<velcal::TimestampUs>(sequence * 100'000 + note)});
    }
    return press;
}

void pressCollectorGroupsAndRejectsBadPresses()
{
    velcal::PressCollectorConfig config;
    config.lowestNote = 60;
    config.highestNote = 71;
    config.minimumCompletionRatio = 0.75;
    velcal::PressCollector collector(velcal::KeyGroup::whiteKeys, config);

    const std::vector<int> whiteNotes{60, 62, 64, 65, 67, 69, 71};
    for (std::size_t i = 0; i < whiteNotes.size(); ++i) {
        collector.add({static_cast<std::uint8_t>(whiteNotes[i]), 64,
            static_cast<velcal::TimestampUs>(i * 1'000)});
    }
    const auto good = collector.flush();
    expect(good.has_value() && good->accepted, "complete simultaneous press is accepted");
    expect(good.has_value() && std::abs(good->referenceVelocity - 64.0) < 0.01,
        "press reference is the median velocity");

    velcal::PressCollector idleCollector(velcal::KeyGroup::whiteKeys, config);
    idleCollector.add({60, 64, 0});
    expect(!idleCollector.flushIfIdle(100'000).has_value(), "active press is not flushed early");
    expect(idleCollector.flushIfIdle(300'000).has_value(), "quiet press can be flushed without a new note");

    velcal::PressCollector badCollector(velcal::KeyGroup::whiteKeys, config);
    badCollector.add({60, 60, 0});
    badCollector.add({60, 61, 1'000});
    badCollector.add({61, 60, 2'000});
    const auto bad = badCollector.flush();
    expect(bad.has_value() && !bad->accepted, "duplicate and wrong-colour press is rejected");
    expect(bad.has_value() && bad->issues.size() >= 2, "bad press retains quality reasons");

    velcal::PressCollectorConfig slowConfig;
    slowConfig.lowestNote = 21;
    slowConfig.highestNote = 57;
    velcal::PressCollector slowCollector(velcal::KeyGroup::whiteKeys, slowConfig);
    bool splitEarly = false;
    std::size_t eventIndex = 0;
    for (int note = slowConfig.lowestNote; note <= slowConfig.highestNote; ++note) {
        if (!velcal::belongsToGroup(static_cast<std::uint8_t>(note), velcal::KeyGroup::whiteKeys))
            continue;
        splitEarly = splitEarly || slowCollector.add({
            static_cast<std::uint8_t>(note),
            64,
            static_cast<velcal::TimestampUs>(eventIndex++ * 6'000)}).has_value();
    }
    const auto slowPress = slowCollector.flush();
    expect(!splitEarly, "a slowly serialized chord is not split at 90 ms");
    expect(slowPress.has_value() && slowPress->accepted && slowPress->notes.size() == 22,
        "a 22-key chord delivered over 126 ms remains one press");

    velcal::PressCollector delayedCollector(velcal::KeyGroup::whiteKeys, slowConfig);
    delayedCollector.add({21, 20, 0});
    delayedCollector.add({23, 20, 141'000});
    expect(delayedCollector.flush().has_value(),
        "soft-key waves separated by 141 ms remain in one physical press");
}

void shortSectionRangeIsInferredFromRepeatedKeys()
{
    std::vector<velcal::NoteOn> events;
    const std::vector<int> expectedNotes{60, 62, 64, 65, 67};
    for (int press = 0; press < 5; ++press) {
        const auto start = static_cast<velcal::TimestampUs>(press * 700'000);
        for (std::size_t index = 0; index < expectedNotes.size(); ++index) {
            events.push_back({
                static_cast<std::uint8_t>(expectedNotes[index]),
                static_cast<std::uint8_t>(35 + press * 15 + index),
                start + static_cast<velcal::TimestampUs>(index * 55'000)});
        }
        if (press == 1)
            events.push_back({72, 50, start + 280'000});
    }

    const auto result = velcal::analyzeSectionCapture(
        events, velcal::KeyGroup::whiteKeys, 7);
    expect(result.rangeInferred, "short-section key range is inferred");
    expect(result.lowestNote == 60 && result.highestNote == 67,
        "an occasional stray key does not expand the inferred bar range");
    expect(result.presses.size() == 5, "staggered short-bar strikes remain five presses");
    expect(result.presses[0].accepted, "a complete inferred short-bar press is accepted");
    expect(result.presses[1].segmentId == 7 && !result.presses[1].accepted,
        "the stray key is retained and rejects only its own press");

    events.resize(expectedNotes.size() * 2);
    const auto tooShort = velcal::analyzeSectionCapture(
        events, velcal::KeyGroup::whiteKeys, 8);
    expect(!tooShort.rangeInferred, "fewer than three presses cannot establish a bar range");
}

void nonlinearHotKeyIsCorrected()
{
    std::vector<velcal::CalibrationPress> presses;
    std::uint64_t sequence = 1;
    for (const auto reference : {18, 24, 31, 38, 47, 55, 63, 72, 80, 89, 98, 108, 116}) {
        const auto hot = std::min(127, static_cast<int>(std::lround(reference * (1.04 + reference / 700.0))));
        presses.push_back(makePress(sequence++, reference, hot));
    }

    velcal::CalibrationConfig config;
    config.minimumSamplesPerKey = 6;
    config.samplesForFullStrength = 10;
    config.desiredSamplesPerRegion = 5;
    config.deadband = 1.5;
    const auto result = velcal::calibrate(presses, config);
    const auto& map = result.noteMaps[65];

    expect(map.isMonotonic(), "generated nonlinear map is monotonic");
    expect(map.apply(119) < 119, "hot key is reduced at high velocity");
    expect((119 - map.apply(119)) > (25 - map.apply(25)),
        "high-velocity correction can exceed low-velocity correction");
    expect(result.coverage.score > 0.7, "mixed-strength presses produce useful coverage");
    expect(result.noteStats[65].samplesUsedByRegion[0] == 4
            && result.noteStats[65].samplesUsedByRegion[1] == 5
            && result.noteStats[65].samplesUsedByRegion[2] == 4,
        "per-note statistics retain soft, medium, and firm sample counts");
}

void calibrationImprovesConsistencyAndResistsOutlier()
{
    std::vector<velcal::CalibrationPress> training;
    std::vector<velcal::CalibrationPress> validation;
    std::uint64_t sequence = 1;
    for (int repeat = 0; repeat < 3; ++repeat) {
        for (const auto reference : {22, 35, 50, 66, 82, 98, 114}) {
            const auto hot = std::min(127, static_cast<int>(std::lround(reference * 1.18)));
            training.push_back(makePress(sequence++, reference, hot, repeat == 1 && reference == 66));
        }
    }
    for (const auto reference : {27, 44, 61, 76, 94, 109}) {
        const auto hot = std::min(127, static_cast<int>(std::lround(reference * 1.18)));
        validation.push_back(makePress(sequence++, reference, hot));
    }

    velcal::CalibrationConfig config;
    config.samplesForFullStrength = 12;
    const auto result = velcal::calibrate(training, config);
    const auto metrics = velcal::validate(validation, result.noteMaps);

    expect(metrics.calibratedMeanAbsoluteDeviation < metrics.rawMeanAbsoluteDeviation,
        "calibration improves held-out press consistency");
    expect(metrics.improvementFraction > 0.35,
        "held-out consistency improves by a meaningful amount");
    expect(result.noteMaps[69].apply(80) >= 75,
        "one extreme observation does not create a large correction");
}

void tinyDifferencesStayInsideDeadband()
{
    std::vector<velcal::CalibrationPress> presses;
    for (std::uint64_t sequence = 1; sequence <= 20; ++sequence)
        presses.push_back(makePress(sequence, 60 + static_cast<int>(sequence % 3), 62));

    const auto result = velcal::calibrate(presses);
    expect(result.noteMaps[60].apply(64) == 64, "typical key remains unchanged");
}

void overlappingShortSectionsAreAligned()
{
    std::vector<velcal::CalibrationPress> presses;
    std::uint64_t sequence = 1;
    for (int repeat = 0; repeat < 3; ++repeat) {
        for (const auto force : {24, 38, 54, 70, 86, 102, 116}) {
            velcal::CalibrationPress left;
            left.sequence = sequence++;
            left.segmentId = 10;
            left.group = velcal::KeyGroup::whiteKeys;
            left.accepted = true;
            left.referenceVelocity = force;
            for (const auto note : {60, 62, 64, 65, 67})
                left.notes.push_back({static_cast<std::uint8_t>(note),
                    static_cast<std::uint8_t>(force), 0});
            presses.push_back(left);

            velcal::CalibrationPress right;
            right.sequence = sequence++;
            right.segmentId = 20;
            right.group = velcal::KeyGroup::whiteKeys;
            right.accepted = true;
            const auto hot = std::min(127, static_cast<int>(std::lround(force * 1.20)));
            right.referenceVelocity = hot;
            for (const auto note : {65, 67})
                right.notes.push_back({static_cast<std::uint8_t>(note),
                    static_cast<std::uint8_t>(force), 0});
            for (const auto note : {69, 71, 72})
                right.notes.push_back({static_cast<std::uint8_t>(note),
                    static_cast<std::uint8_t>(hot), 0});
            presses.push_back(right);
        }
    }

    velcal::CalibrationConfig config;
    config.samplesForFullStrength = 12;
    config.deadband = 1.0;
    const auto result = velcal::calibrate(presses, config);

    expect(result.segmentsConnected, "overlapping short sections form one calibration chain");
    expect(result.segmentAlignments.size() == 2, "both short sections are represented");
    expect(result.noteMaps[69].apply(96) < 88,
        "section-wide sensitivity is recovered through overlap keys");
    expect(result.noteMaps[65].apply(80) >= 76,
        "shared reference keys remain close to their original response");

    auto disconnectedPresses = presses;
    for (auto& press : disconnectedPresses) {
        if (press.segmentId != 20)
            continue;
        press.notes.erase(
            std::remove_if(
                press.notes.begin(), press.notes.end(),
                [](const auto& event) { return event.note == 65 || event.note == 67; }),
            press.notes.end());
    }
    const auto disconnected = velcal::calibrate(disconnectedPresses, config);
    expect(!disconnected.segmentsConnected,
        "sections without shared keys are reported as disconnected");
}

void profileRoundTripPreservesMeasurementsAndMaps()
{
    velcal::CalibrationProfile profile;
    profile.profileName = "Kawai test";
    profile.createdUtc = "2026-09-29T00:00:00Z";
    profile.inputDevice.name = "KAWAI USB MIDI";
    profile.presses.push_back(makePress(1, 64, 76));
    profile.generated = velcal::calibrate(profile.presses);
    profile.noteAdjustments[65] = -4;
    profile.globalCurve = {"Soft", -0.35, 3, 124};
    profile.globalCurve.points = {{1.0, 3.0}, {64.0, 78.0}, {127.0, 124.0}};
    profile.noteCurveOverrides[65].points = {
        {1.0, 1.0}, {64.0, 60.0}, {127.0, 127.0}};
    profile.userGlobalPresets.push_back({"My curve", 0.2, 5, 120});

    const auto path = std::filesystem::current_path() / "velcal-profile-roundtrip.json";
    velcal::saveProfile(profile, path);
    const auto loaded = velcal::loadProfile(path);
    std::filesystem::remove(path);

    expect(loaded.schemaVersion == velcal::currentProfileSchemaVersion,
        "profile schema version survives round trip");
    expect(loaded.profileName == profile.profileName, "profile name survives round trip");
    expect(loaded.inputDevice.name == profile.inputDevice.name,
        "MIDI device identity survives round trip");
    expect(loaded.presses.size() == 1 && loaded.presses[0].notes.size() == 7,
        "raw press measurements survive round trip");
    expect(loaded.generated.noteMaps[65].values == profile.generated.noteMaps[65].values,
        "generated velocity map survives round trip");
    expect(loaded.noteAdjustments[65] == -4,
        "per-key adjustments survive round trip");
    expect(loaded.globalCurve.name == "Soft" && loaded.globalCurve.curvature == -0.35,
        "global curve survives round trip");
    expect(loaded.globalCurve.points.size() == 3
            && loaded.noteCurveOverrides[65].points.size() == 3,
        "editable global and per-note curves survive round trip");
    expect(loaded.userGlobalPresets.size() == 1
            && loaded.userGlobalPresets[0].name == "My curve",
        "user global-curve presets survive round trip");
}

void globalVelocityCurvesAreMonotonic()
{
    const auto soft = velcal::makeVelocityCurve(-0.5, 1, 127);
    const auto firm = velcal::makeVelocityCurve(0.5, 1, 127);
    expect(soft.isMonotonic() && firm.isMonotonic(),
        "global velocity curves remain monotonic");
    expect(soft.apply(64) > 64, "soft preset raises mid-range velocity");
    expect(firm.apply(64) < 64, "firm preset lowers mid-range velocity");
    expect(velcal::makeVelocityCurve(0.0, 10, 110).apply(1) == 10,
        "global curve applies its minimum output");
    const std::vector<velcal::VelocityCurvePoint> points{
        {1.0, 1.0}, {32.0, 18.0}, {80.0, 100.0}, {127.0, 127.0}};
    const auto edited = velcal::makeVelocityCurve(points, true);
    expect(edited.isMonotonic(), "editable smooth curve remains monotonic");
    expect(edited.apply(32) == 18 && edited.apply(80) == 100,
        "editable curve passes through its control points");
}

} // namespace

int main()
{
    pressCollectorGroupsAndRejectsBadPresses();
    shortSectionRangeIsInferredFromRepeatedKeys();
    nonlinearHotKeyIsCorrected();
    calibrationImprovesConsistencyAndResistsOutlier();
    tinyDifferencesStayInsideDeadband();
    overlappingShortSectionsAreAligned();
    profileRoundTripPreservesMeasurementsAndMaps();
    globalVelocityCurvesAreMonotonic();

    if (failures == 0) {
        std::cout << "All VelCal core tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << failures << " test(s) failed.\n";
    return EXIT_FAILURE;
}
