#include "velcal/profile.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace velcal {
namespace {

using Json = nlohmann::json;

const char* keyGroupName(const KeyGroup group)
{
    return group == KeyGroup::whiteKeys ? "white" : "black";
}

KeyGroup parseKeyGroup(const std::string& value)
{
    if (value == "white")
        return KeyGroup::whiteKeys;
    if (value == "black")
        return KeyGroup::blackKeys;
    throw std::runtime_error("profile contains an unknown key group");
}

const char* issueName(const PressIssue issue)
{
    switch (issue) {
    case PressIssue::incomplete: return "incomplete";
    case PressIssue::duplicateNote: return "duplicateNote";
    case PressIssue::unexpectedNote: return "unexpectedNote";
    case PressIssue::invalidVelocity: return "invalidVelocity";
    }
    throw std::runtime_error("profile contains an unknown press issue");
}

PressIssue parseIssue(const std::string& value)
{
    if (value == "incomplete") return PressIssue::incomplete;
    if (value == "duplicateNote") return PressIssue::duplicateNote;
    if (value == "unexpectedNote") return PressIssue::unexpectedNote;
    if (value == "invalidVelocity") return PressIssue::invalidVelocity;
    throw std::runtime_error("profile contains an unknown press issue");
}

Json writeSettings(const CalibrationConfig& settings)
{
    return {
        {"velocityBinWidth", settings.velocityBinWidth},
        {"minimumSamplesPerKey", settings.minimumSamplesPerKey},
        {"samplesForFullStrength", settings.samplesForFullStrength},
        {"desiredSamplesPerRegion", settings.desiredSamplesPerRegion},
        {"deadband", settings.deadband},
        {"strength", settings.strength},
        {"outlierSigma", settings.outlierSigma},
        {"minimumOutlierThreshold", settings.minimumOutlierThreshold},
        {"minimumSharedNotesPerSegment", settings.minimumSharedNotesPerSegment},
    };
}

CalibrationConfig readSettings(const Json& json)
{
    CalibrationConfig settings;
    settings.velocityBinWidth = json.value("velocityBinWidth", settings.velocityBinWidth);
    settings.minimumSamplesPerKey = json.value("minimumSamplesPerKey", settings.minimumSamplesPerKey);
    settings.samplesForFullStrength = json.value("samplesForFullStrength", settings.samplesForFullStrength);
    settings.desiredSamplesPerRegion = json.value("desiredSamplesPerRegion", settings.desiredSamplesPerRegion);
    settings.deadband = json.value("deadband", settings.deadband);
    settings.strength = json.value("strength", settings.strength);
    settings.outlierSigma = json.value("outlierSigma", settings.outlierSigma);
    settings.minimumOutlierThreshold = json.value(
        "minimumOutlierThreshold", settings.minimumOutlierThreshold);
    settings.minimumSharedNotesPerSegment = json.value(
        "minimumSharedNotesPerSegment", settings.minimumSharedNotesPerSegment);
    return settings;
}

Json writeCurve(const VelocityCurveSettings& curve)
{
    Json points = Json::array();
    for (const auto& point : curve.points)
        points.push_back({{"input", point.input}, {"output", point.output}});
    return {
        {"name", curve.name},
        {"curvature", curve.curvature},
        {"minimumOutput", curve.minimumOutput},
        {"maximumOutput", curve.maximumOutput},
        {"points", std::move(points)},
        {"smooth", curve.smooth},
    };
}

std::vector<VelocityCurvePoint> readCurvePoints(const Json& json)
{
    std::vector<VelocityCurvePoint> points;
    if (!json.is_array())
        throw std::runtime_error("profile curve points must be an array");
    if (json.size() > 128)
        throw std::runtime_error("profile contains too many curve points");
    for (const auto& source : json) {
        VelocityCurvePoint point{
            source.at("input").get<double>(),
            source.at("output").get<double>()};
        if (point.input < 1.0 || point.input > 127.0
            || point.output < 1.0 || point.output > 127.0)
            throw std::runtime_error("profile contains a curve point outside the MIDI range");
        if (!points.empty()
            && (point.input <= points.back().input || point.output < points.back().output))
            throw std::runtime_error("profile contains a non-monotonic velocity curve");
        points.push_back(point);
    }
    if (!points.empty()
        && (points.size() < 2 || points.front().input != 1.0 || points.back().input != 127.0))
        throw std::runtime_error("profile custom curves must span input velocities 1 to 127");
    return points;
}

VelocityCurveSettings readCurve(const Json& json)
{
    VelocityCurveSettings curve;
    curve.name = json.value("name", curve.name);
    curve.curvature = std::clamp(json.value("curvature", curve.curvature), -1.0, 1.0);
    const auto minimum = json.value("minimumOutput", static_cast<unsigned int>(curve.minimumOutput));
    const auto maximum = json.value("maximumOutput", static_cast<unsigned int>(curve.maximumOutput));
    if (minimum > 127 || maximum > 127 || minimum == 0 || maximum == 0)
        throw std::runtime_error("profile contains an invalid global velocity range");
    curve.minimumOutput = static_cast<std::uint8_t>(minimum);
    curve.maximumOutput = static_cast<std::uint8_t>(maximum);
    if (curve.minimumOutput > curve.maximumOutput)
        throw std::runtime_error("profile global velocity range is reversed");
    if (json.contains("points"))
        curve.points = readCurvePoints(json.at("points"));
    curve.smooth = json.value("smooth", true);
    return curve;
}

Json writePress(const CalibrationPress& press)
{
    Json issues = Json::array();
    for (const auto issue : press.issues)
        issues.push_back(issueName(issue));

    Json notes = Json::array();
    for (const auto& note : press.notes) {
        notes.push_back({
            {"note", note.note},
            {"velocity", note.velocity},
            {"timestampUs", note.timestampUs},
        });
    }
    return {
        {"sequence", press.sequence},
        {"segmentId", press.segmentId},
        {"group", keyGroupName(press.group)},
        {"startedAtUs", press.startedAtUs},
        {"endedAtUs", press.endedAtUs},
        {"accepted", press.accepted},
        {"referenceVelocity", press.referenceVelocity},
        {"issues", std::move(issues)},
        {"notes", std::move(notes)},
    };
}

CalibrationPress readPress(const Json& json)
{
    CalibrationPress press;
    press.sequence = json.at("sequence").get<std::uint64_t>();
    press.segmentId = json.value("segmentId", SegmentId{});
    press.group = parseKeyGroup(json.at("group").get<std::string>());
    press.startedAtUs = json.at("startedAtUs").get<TimestampUs>();
    press.endedAtUs = json.at("endedAtUs").get<TimestampUs>();
    press.accepted = json.at("accepted").get<bool>();
    press.referenceVelocity = json.at("referenceVelocity").get<double>();
    for (const auto& issue : json.at("issues"))
        press.issues.push_back(parseIssue(issue.get<std::string>()));
    for (const auto& noteJson : json.at("notes")) {
        const auto note = noteJson.at("note").get<unsigned int>();
        const auto velocity = noteJson.at("velocity").get<unsigned int>();
        if (note > 127 || velocity > 127)
            throw std::runtime_error("profile contains an invalid MIDI value");
        press.notes.push_back({
            static_cast<std::uint8_t>(note),
            static_cast<std::uint8_t>(velocity),
            noteJson.at("timestampUs").get<TimestampUs>(),
        });
    }
    return press;
}

Json writeResult(const CalibrationResult& result)
{
    Json maps = Json::array();
    Json stats = Json::array();
    for (std::size_t note = 0; note < 128; ++note) {
        maps.push_back(result.noteMaps[note].values);
        const auto& noteStats = result.noteStats[note];
        stats.push_back({
            {"samplesSeen", noteStats.samplesSeen},
            {"samplesUsed", noteStats.samplesUsed},
            {"samplesUsedByRegion", noteStats.samplesUsedByRegion},
            {"medianRawMinusReference", noteStats.medianRawMinusReference},
            {"confidence", noteStats.confidence},
        });
    }

    Json alignments = Json::array();
    for (const auto& alignment : result.segmentAlignments) {
        alignments.push_back({
            {"segmentId", alignment.segmentId},
            {"group", keyGroupName(alignment.group)},
            {"velocityRegionScales", alignment.velocityRegionScales},
            {"connectedToRoot", alignment.connectedToRoot},
        });
    }
    return {
        {"noteMaps", std::move(maps)},
        {"noteStats", std::move(stats)},
        {"coverage", {
            {"lowPresses", result.coverage.lowPresses},
            {"mediumPresses", result.coverage.mediumPresses},
            {"highPresses", result.coverage.highPresses},
            {"score", result.coverage.score},
        }},
        {"segmentAlignments", std::move(alignments)},
        {"segmentsConnected", result.segmentsConnected},
    };
}

CalibrationResult readResult(const Json& json)
{
    CalibrationResult result;
    const auto& maps = json.at("noteMaps");
    if (maps.size() != 128)
        throw std::runtime_error("profile must contain 128 note maps");
    for (std::size_t note = 0; note < 128; ++note) {
        const auto values = maps.at(note).get<std::array<unsigned int, 128>>();
        for (std::size_t velocity = 0; velocity < 128; ++velocity) {
            if (values[velocity] > 127)
                throw std::runtime_error("profile contains an invalid velocity map");
            result.noteMaps[note].values[velocity] = static_cast<std::uint8_t>(values[velocity]);
        }
        if (!result.noteMaps[note].isMonotonic())
            throw std::runtime_error("profile contains a non-monotonic velocity map");
    }

    const auto& stats = json.at("noteStats");
    if (stats.size() != 128)
        throw std::runtime_error("profile must contain 128 note statistics entries");
    for (std::size_t note = 0; note < 128; ++note) {
        auto& target = result.noteStats[note];
        const auto& source = stats.at(note);
        target.samplesSeen = source.at("samplesSeen").get<std::size_t>();
        target.samplesUsed = source.at("samplesUsed").get<std::size_t>();
        if (source.contains("samplesUsedByRegion")) {
            target.samplesUsedByRegion = source.at("samplesUsedByRegion")
                .get<std::array<std::size_t, 3>>();
        }
        target.medianRawMinusReference = source.at("medianRawMinusReference").get<double>();
        target.confidence = source.at("confidence").get<double>();
    }

    const auto& coverage = json.at("coverage");
    result.coverage.lowPresses = coverage.at("lowPresses").get<std::size_t>();
    result.coverage.mediumPresses = coverage.at("mediumPresses").get<std::size_t>();
    result.coverage.highPresses = coverage.at("highPresses").get<std::size_t>();
    result.coverage.score = coverage.at("score").get<double>();
    result.segmentsConnected = json.at("segmentsConnected").get<bool>();
    for (const auto& source : json.at("segmentAlignments")) {
        SegmentAlignment alignment;
        alignment.segmentId = source.at("segmentId").get<SegmentId>();
        alignment.group = parseKeyGroup(source.at("group").get<std::string>());
        alignment.velocityRegionScales = source.at("velocityRegionScales")
            .get<std::array<double, 3>>();
        alignment.connectedToRoot = source.at("connectedToRoot").get<bool>();
        result.segmentAlignments.push_back(alignment);
    }
    return result;
}

} // namespace

void saveProfile(const CalibrationProfile& profile, const std::filesystem::path& path)
{
    Json presses = Json::array();
    for (const auto& press : profile.presses)
        presses.push_back(writePress(press));

    Json json = {
        {"schemaVersion", profile.schemaVersion},
        {"profileName", profile.profileName},
        {"createdUtc", profile.createdUtc},
        {"algorithmVersion", profile.algorithmVersion},
        {"inputDevice", {
            {"name", profile.inputDevice.name},
            {"endpointId", profile.inputDevice.endpointId},
            {"manufacturer", profile.inputDevice.manufacturer},
        }},
        {"settings", writeSettings(profile.settings)},
        {"presses", std::move(presses)},
        {"generated", writeResult(profile.generated)},
        {"noteAdjustments", profile.noteAdjustments},
        {"globalCurve", writeCurve(profile.globalCurve)},
        {"userGlobalPresets", Json::array()},
    };

    for (const auto& preset : profile.userGlobalPresets)
        json["userGlobalPresets"].push_back(writeCurve(preset));
    json["noteCurveOverrides"] = Json::array();
    for (std::size_t note = 0; note < profile.noteCurveOverrides.size(); ++note) {
        const auto& curve = profile.noteCurveOverrides[note];
        if (curve.points.empty())
            continue;
        Json points = Json::array();
        for (const auto& point : curve.points)
            points.push_back({{"input", point.input}, {"output", point.output}});
        json["noteCurveOverrides"].push_back({
            {"note", note},
            {"smooth", curve.smooth},
            {"points", std::move(points)},
        });
    }

    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output)
        throw std::runtime_error("could not create profile file");
    output << std::setw(2) << json << '\n';
    if (!output.good())
        throw std::runtime_error("could not finish writing profile file");
}

CalibrationProfile loadProfile(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("could not open profile file");
    Json json;
    input >> json;

    CalibrationProfile profile;
    profile.schemaVersion = json.at("schemaVersion").get<std::uint32_t>();
    const auto sourceSchemaVersion = profile.schemaVersion;
    if (sourceSchemaVersion > currentProfileSchemaVersion)
        throw std::runtime_error("profile was created by a newer VelCal version");
    profile.profileName = json.at("profileName").get<std::string>();
    profile.createdUtc = json.at("createdUtc").get<std::string>();
    profile.algorithmVersion = json.at("algorithmVersion").get<std::string>();
    const auto& device = json.at("inputDevice");
    profile.inputDevice.name = device.value("name", std::string{});
    profile.inputDevice.endpointId = device.value("endpointId", std::string{});
    profile.inputDevice.manufacturer = device.value("manufacturer", std::string{});
    profile.settings = readSettings(json.at("settings"));
    if (sourceSchemaVersion < 4) {
        if (profile.settings.desiredSamplesPerRegion == 5)
            profile.settings.desiredSamplesPerRegion = 8;
        if (profile.settings.samplesForFullStrength == 18)
            profile.settings.samplesForFullStrength = 24;
    }
    for (const auto& press : json.at("presses"))
        profile.presses.push_back(readPress(press));
    profile.generated = readResult(json.at("generated"));
    if (sourceSchemaVersion < 4)
        profile.generated = calibrate(profile.presses, profile.settings);
    if (json.contains("noteAdjustments")) {
        const auto adjustments = json.at("noteAdjustments").get<std::array<int, 128>>();
        for (const auto adjustment : adjustments) {
            if (adjustment < -24 || adjustment > 24)
                throw std::runtime_error("profile contains an invalid per-key adjustment");
        }
        profile.noteAdjustments = adjustments;
    }
    if (json.contains("globalCurve"))
        profile.globalCurve = readCurve(json.at("globalCurve"));
    if (json.contains("userGlobalPresets")) {
        for (const auto& preset : json.at("userGlobalPresets"))
            profile.userGlobalPresets.push_back(readCurve(preset));
    }
    if (json.contains("noteCurveOverrides")) {
        for (const auto& source : json.at("noteCurveOverrides")) {
            const auto note = source.at("note").get<std::size_t>();
            if (note >= profile.noteCurveOverrides.size())
                throw std::runtime_error("profile contains an invalid note curve number");
            profile.noteCurveOverrides[note].points = readCurvePoints(source.at("points"));
            profile.noteCurveOverrides[note].smooth = source.value("smooth", true);
        }
    }
    profile.schemaVersion = currentProfileSchemaVersion;
    profile.algorithmVersion = currentAlgorithmVersion;
    return profile;
}

} // namespace velcal
