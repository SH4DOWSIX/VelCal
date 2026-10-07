#include "velcal/calibration.hpp"
#include "velcal/profile.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

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

void automaticSmoothingRemovesKinksWithoutLosingCalibration()
{
    auto kink = velcal::VelocityMap::identity();
    kink.values[40] = 41;
    const auto points = velcal::smoothCalibrationPoints(kink);
    expect(points.size() < 127, "automatic smoothing does not anchor every quantized MIDI step");
    expect(std::abs(velcal::evaluateVelocityCurve(points, true, 39.5) - 39.5) < 1.0e-8,
        "automatic smoothing removes a small kink between the broad calibration anchors");
    expect(velcal::makeVelocityCurve(points, true).apply(40) == 40 && kink.apply(40) == 41,
        "smoothing affects MIDI correction without modifying the original calibration");

    auto abrupt = velcal::VelocityMap::identity();
    for (int input = 1; input <= 127; ++input)
        abrupt.values[static_cast<std::size_t>(input)] = static_cast<std::uint8_t>(
            input < 64 ? std::max(1, input / 2) : std::min(127, input + 20));
    for (const auto& map : {velcal::VelocityMap::identity(), kink,
             velcal::makeVelocityCurve(-0.45), abrupt}) {
        const auto fitted = velcal::smoothCalibrationPoints(map);
        const auto output = velcal::makeVelocityCurve(fitted, true);
        expect(output.isMonotonic(), "automatic smoothing remains monotonic");
        expect(output.apply(1) == map.apply(1) && output.apply(127) == map.apply(127),
            "automatic smoothing preserves calibrated endpoints");
        for (int input = 1; input <= 127; ++input)
            expect(std::abs(velcal::evaluateVelocityCurve(fitted, true, input)
                    - map.apply(static_cast<std::uint8_t>(input))) <= 1.0 + 1.0e-8,
                "adaptive smoothing preserves significant calibration detail within one velocity step");
    }

    velcal::CalibrationProfile profile;
    profile.generated = velcal::calibrate({});
    profile.generated.noteMaps[60] = kink;
    expect(velcal::effectiveMaps(profile)[60].apply(40) == 40,
        "automatic Smooth on uses the same fit as the graph");
    profile.noteCurveOverrides[60].smooth = false;
    expect(velcal::effectiveMaps(profile)[60].values == kink.values,
        "automatic Smooth off restores the complete original MIDI map");
    profile.noteCurveOverrides[60].smooth = true;
    expect(velcal::effectiveMaps(profile)[60].apply(40) == 40,
        "automatic Smooth off/on restores the same smoothed map");
    profile.noteCurveOverrides[60].points = {{1, 1}, {40, 60}, {127, 127}};
    expect(velcal::effectiveMaps(profile)[60].apply(40) == 60,
        "manual curve control points are not replaced by automatic smoothing");
    profile.noteCurveOverrides[60].smooth = false;
    const auto recalled = velcal::deserializeProfile(velcal::serializeProfile(profile));
    for (const auto& curve : recalled.noteCurveOverrides)
        expect(!curve.smooth, "legacy mixed per-key smoothing loads as whole-keyboard Smooth off");
    expect(recalled.noteCurveOverrides[60].points.size() == 3,
        "legacy smoothing normalization preserves manual curve points");
}

void crowdedCurvePointsRemainEditable()
{
    std::vector<velcal::VelocityCurvePoint> points{
        {1, 1}, {63, 40}, {64, 90}, {127, 127}};
    const auto index = velcal::insertVelocityCurvePoint(points, 64, 10);
    expect(index && *index == 2 && points.size() == 4,
        "clicking an existing input selects it without inserting a duplicate");
    if (index)
        velcal::moveVelocityCurvePoint(points, *index, 60, 10);
    expect(points[2].input == 64 && points[2].output >= points[1].output,
        "moving an adjacent point preserves ordering");
    std::vector<velcal::VelocityCurvePoint> fractional{
        {1, 1}, {63.5, 40}, {64, 60}, {64.5, 90}, {127, 127}};
    velcal::moveVelocityCurvePoint(fractional, 2, 80, 75);
    expect(fractional[2].input == 64 && fractional[2].output == 75,
        "crowded fractional profile points can move vertically without reversed clamp bounds");
    std::vector<velcal::VelocityCurvePoint> dense;
    for (int input = 1; input <= 127; ++input)
        dense.push_back({static_cast<double>(input), static_cast<double>(input)});
    for (int input = 1; input <= 127; ++input) {
        const auto selected = velcal::insertVelocityCurvePoint(dense, input, 127 - input);
        expect(selected && dense.size() == 127, "a fully populated curve never grows duplicate points");
    }
    expect(velcal::makeVelocityCurve(points, true).isMonotonic(),
        "edited crowded curves still generate monotonic maps");
}

void coverageRequiresEachSampledKeyAndRejectsOutliers()
{
    std::vector<velcal::CalibrationPress> presses;
    for (int section = 0; section < 2; ++section) {
        for (int repeat = 0; repeat < 4; ++repeat) {
            for (const auto reference : {24, 64, 104}) {
                auto press = makePress(presses.size() + 1, reference, reference);
                press.segmentId = static_cast<velcal::SegmentId>(section + 1);
                if (section == 1)
                    for (auto& note : press.notes)
                        note.note = static_cast<std::uint8_t>(note.note + 12);
                presses.push_back(std::move(press));
            }
        }
    }
    auto result = velcal::calibrate(presses);
    expect(result.coverage.lowPresses == 8 && result.coverage.mediumPresses == 8
            && result.coverage.highPresses == 8,
        "two partial sections still retain their total press counts");
    expect(std::abs(result.coverage.score - 0.5) < 0.001,
        "two half-sampled sections report 50 percent, not 100 percent");
    auto complete = presses;
    complete.insert(complete.end(), presses.begin(), presses.end());
    result = velcal::calibrate(complete);
    expect(result.coverage.score == 1.0, "all sampled keys reaching 8/8/8 reports full coverage");
    complete[2].notes[4].velocity = 1;
    result = velcal::calibrate(complete);
    expect(result.noteStats[67].samplesUsedByRegion[2] == 7 && result.coverage.score < 1.0,
        "a rejected firm outlier prevents full coverage even with enough total presses");
}

void curvePresetLibraryRoundTripsAndValidates()
{
    velcal::VelocityCurveSettings curve;
    curve.name = "Gentle";
    curve.curvature = 0.5;
    curve.minimumOutput = 9;
    curve.maximumOutput = 115;
    curve.smooth = false;
    curve.points = {{1, 9}, {64, 72}, {127, 115}};
    const auto bytes = velcal::serializeCurvePresets({curve});
    const auto restored = velcal::deserializeCurvePresets(bytes);
    expect(restored.size() == 1 && velcal::serializeCurvePresets(restored) == bytes,
        "preset library preserves curve name, parameters, points and Smooth");
    expect(velcal::deserializeCurvePresets(velcal::serializeCurvePresets({})).empty(),
        "empty preset libraries round trip");
    for (const auto& invalid : {std::string("{broken"),
             std::string("{\"velcalCurvePresets\":2,\"presets\":[]}"),
             std::string("{\"velcalCurvePresets\":1,\"presets\":{}}"),
             std::string("{\"velcalCurvePresets\":1,\"presets\":[{\"minimumOutput\":0}]}"),
             std::string("{\"velcalCurvePresets\":1,\"presets\":[{\"points\":[{\"input\":2,\"output\":3}]}]}")}) {
        bool rejected = false;
        try { static_cast<void>(velcal::deserializeCurvePresets(invalid)); }
        catch (const std::exception&) { rejected = true; }
        expect(rejected, "malformed, future-version and invalid curve libraries are rejected");
    }
}

void failedProfileSavesPreservePreviousFile()
{
    const auto directory = std::filesystem::current_path() / "velcal-atomic-save-test";
    std::filesystem::create_directory(directory);
    const auto path = directory / "profile.velcal.json";
    velcal::CalibrationProfile profile;
    profile.profileName = "Original";
    profile.generated = velcal::calibrate({});
    velcal::saveProfile(profile, path);
    auto invalid = profile;
    invalid.profileName = std::string(1, static_cast<char>(0xff));
    bool failed = false;
    try { velcal::saveProfile(invalid, path); }
    catch (const std::exception&) { failed = true; }
    expect(failed && velcal::loadProfile(path).profileName == "Original",
        "serialization failure leaves the previously saved profile intact");
#if defined(_WIN32)
    const auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    expect(handle != INVALID_HANDLE_VALUE, "test can lock the destination against replacement");
    if (handle != INVALID_HANDLE_VALUE) {
        profile.profileName = "Replacement";
        failed = false;
        try { velcal::saveProfile(profile, path); }
        catch (const std::exception&) { failed = true; }
        CloseHandle(handle);
        expect(failed && velcal::loadProfile(path).profileName == "Original",
            "replacement failure leaves the old file readable and unchanged");
    }
#endif
    profile.profileName = "Replacement";
    profile.generated.noteStats[60].samplesSeen = 12;
    profile.generated.noteStats[60].samplesUsed = 12;
    profile.generated.noteStats[60].samplesUsedByRegion = {4, 4, 4};
    profile.generated.coverage.score = 1.0;
    velcal::saveProfile(profile, path);
    expect(velcal::loadProfile(path).profileName == "Replacement",
        "a successful save replaces an existing file");
    expect(velcal::loadProfile(path).generated.coverage.score == 0.5,
        "loading schema-4 profiles refreshes stale total-only coverage without changing maps");
    expect(std::distance(std::filesystem::directory_iterator(directory),
               std::filesystem::directory_iterator{}) == 1,
        "failed and successful saves leave no temporary profile files");
    std::filesystem::remove_all(directory);
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
    automaticSmoothingRemovesKinksWithoutLosingCalibration();
    crowdedCurvePointsRemainEditable();
    coverageRequiresEachSampledKeyAndRejectsOutliers();
    failedProfileSavesPreservePreviousFile();
    curvePresetLibraryRoundTripsAndValidates();

    if (failures == 0) {
        std::cout << "All VelCal core tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << failures << " test(s) failed.\n";
    return EXIT_FAILURE;
}
