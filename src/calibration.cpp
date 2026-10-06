#include "velcal/calibration.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <tuple>
#include <utility>

namespace velcal {
namespace {

struct Observation {
    double raw{};
    double target{};
    std::size_t region{};
};

using RegionRatios = std::array<std::vector<double>, 4>;
using NoteRatios = std::array<RegionRatios, 128>;

struct SegmentKey {
    KeyGroup group{KeyGroup::whiteKeys};
    SegmentId id{};

    bool operator<(const SegmentKey& other) const noexcept
    {
        return std::tie(group, id) < std::tie(other.group, other.id);
    }
};

std::size_t velocityRegion(const double reference)
{
    if (reference <= 42.0)
        return 0;
    if (reference <= 84.0)
        return 1;
    return 2;
}

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

double clampVelocity(const double value)
{
    return std::max(1.0, std::min(127.0, value));
}

std::vector<Observation> rejectOutliers(
    const std::vector<Observation>& observations,
    const CalibrationConfig& config)
{
    if (observations.size() < 5)
        return observations;

    std::vector<double> corrections;
    corrections.reserve(observations.size());
    for (const auto& observation : observations)
        corrections.push_back(observation.target - observation.raw);

    const auto centre = median(corrections);
    std::vector<double> deviations;
    deviations.reserve(corrections.size());
    for (const auto correction : corrections)
        deviations.push_back(std::abs(correction - centre));

    const auto mad = median(std::move(deviations));
    const auto threshold = std::max(
        config.minimumOutlierThreshold,
        config.outlierSigma * 1.4826 * mad);

    std::vector<Observation> retained;
    retained.reserve(observations.size());
    for (std::size_t i = 0; i < observations.size(); ++i) {
        if (std::abs(corrections[i] - centre) <= threshold)
            retained.push_back(observations[i]);
    }
    return retained;
}

std::vector<std::pair<double, double>> makeKnots(
    const std::vector<Observation>& observations,
    const CalibrationConfig& config)
{
    const auto binWidth = std::max<std::size_t>(1, config.velocityBinWidth);
    const auto binCount = (127 + binWidth - 1) / binWidth;
    std::vector<std::pair<double, double>> knots;

    for (std::size_t bin = 0; bin < binCount; ++bin) {
        const auto first = 1 + static_cast<int>(bin * binWidth);
        const auto last = std::min(127, first + static_cast<int>(binWidth) - 1);
        std::vector<double> raw;
        std::vector<double> target;
        for (const auto& observation : observations) {
            if (observation.raw >= first && observation.raw <= last) {
                raw.push_back(observation.raw);
                target.push_back(observation.target);
            }
        }
        if (!raw.empty())
            knots.emplace_back(median(std::move(raw)), median(std::move(target)));
    }

    std::sort(knots.begin(), knots.end());
    return knots;
}

double interpolate(const std::vector<std::pair<double, double>>& knots, const double x)
{
    if (x <= knots.front().first) {
        const auto correction = knots.front().second - knots.front().first;
        return clampVelocity(x + correction);
    }
    if (x >= knots.back().first) {
        const auto correction = knots.back().second - knots.back().first;
        return clampVelocity(x + correction);
    }

    const auto upper = std::upper_bound(
        knots.begin(), knots.end(), x,
        [](const double value, const auto& knot) { return value < knot.first; });
    const auto lower = upper - 1;
    const auto span = upper->first - lower->first;
    if (span <= std::numeric_limits<double>::epsilon())
        return lower->second;
    const auto amount = (x - lower->first) / span;
    return lower->second + amount * (upper->second - lower->second);
}

void enforceMonotonic(std::array<double, 127>& values)
{
    struct Block {
        std::size_t begin{};
        std::size_t end{};
        double total{};
        double weight{};
    };

    std::vector<Block> blocks;
    blocks.reserve(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        blocks.push_back({i, i, values[i], 1.0});
        while (blocks.size() >= 2) {
            auto& right = blocks.back();
            auto& left = blocks[blocks.size() - 2];
            if ((left.total / left.weight) <= (right.total / right.weight))
                break;
            left.end = right.end;
            left.total += right.total;
            left.weight += right.weight;
            blocks.pop_back();
        }
    }

    for (const auto& block : blocks) {
        const auto value = block.total / block.weight;
        for (auto i = block.begin; i <= block.end; ++i)
            values[i] = value;
    }
}

VelocityMap fitMap(
    const std::vector<Observation>& allObservations,
    const CalibrationConfig& config,
    NoteCalibrationStats& stats)
{
    stats.samplesSeen = allObservations.size();
    if (allObservations.empty())
        return VelocityMap::identity();

    std::vector<double> signedDifferences;
    signedDifferences.reserve(allObservations.size());
    for (const auto& observation : allObservations)
        signedDifferences.push_back(observation.raw - observation.target);
    stats.medianRawMinusReference = median(std::move(signedDifferences));

    const auto observations = rejectOutliers(allObservations, config);
    stats.samplesUsed = observations.size();
    for (const auto& observation : observations) {
        if (observation.region < stats.samplesUsedByRegion.size())
            ++stats.samplesUsedByRegion[observation.region];
    }
    stats.confidence = std::min(
        1.0,
        static_cast<double>(observations.size())
            / static_cast<double>(std::max<std::size_t>(1, config.samplesForFullStrength)));

    if (observations.size() < config.minimumSamplesPerKey)
        return VelocityMap::identity();

    const auto knots = makeKnots(observations, config);
    if (knots.empty())
        return VelocityMap::identity();

    const auto appliedStrength = std::max(0.0, std::min(1.0, config.strength)) * stats.confidence;
    std::array<double, 127> fitted{};
    for (int velocity = 1; velocity <= 127; ++velocity) {
        const auto predicted = interpolate(knots, velocity);
        auto correction = predicted - velocity;
        if (std::abs(correction) <= config.deadband)
            correction = 0.0;
        fitted[static_cast<std::size_t>(velocity - 1)] =
            clampVelocity(velocity + correction * appliedStrength);
    }

    enforceMonotonic(fitted);
    VelocityMap result = VelocityMap::identity();
    for (int velocity = 1; velocity <= 127; ++velocity) {
        result.values[static_cast<std::size_t>(velocity)] = static_cast<std::uint8_t>(
            std::lround(clampVelocity(fitted[static_cast<std::size_t>(velocity - 1)])));
    }
    return result;
}

std::vector<SegmentAlignment> alignSegments(
    const std::vector<CalibrationPress>& presses,
    const CalibrationConfig& config,
    bool& allConnected)
{
    std::map<SegmentKey, NoteRatios> ratios;
    for (const auto& press : presses) {
        if (!press.accepted || press.referenceVelocity <= 0.0)
            continue;
        auto& segment = ratios[{press.group, press.segmentId}];
        const auto region = velocityRegion(press.referenceVelocity);
        for (const auto& event : press.notes) {
            if (event.velocity == 0 || !belongsToGroup(event.note, press.group))
                continue;
            const auto ratio = static_cast<double>(event.velocity) / press.referenceVelocity;
            segment[event.note][region].push_back(ratio);
            segment[event.note][3].push_back(ratio);
        }
    }

    std::vector<SegmentAlignment> alignments;
    alignments.reserve(ratios.size());
    for (const auto& entry : ratios) {
        alignments.push_back({
            entry.first.id,
            entry.first.group,
            {1.0, 1.0, 1.0},
            false});
    }
    if (alignments.empty()) {
        allConnected = true;
        return alignments;
    }

    struct Edge {
        std::size_t neighbour{};
        std::array<double, 3> neighbourToCurrent{1.0, 1.0, 1.0};
    };
    std::vector<std::vector<Edge>> graph(alignments.size());

    for (std::size_t a = 0; a < alignments.size(); ++a) {
        for (std::size_t b = a + 1; b < alignments.size(); ++b) {
            if (alignments[a].group != alignments[b].group)
                continue;
            const auto& ratiosA = ratios.at({alignments[a].group, alignments[a].segmentId});
            const auto& ratiosB = ratios.at({alignments[b].group, alignments[b].segmentId});
            std::array<std::vector<double>, 4> factors;
            std::size_t sharedNotes = 0;

            for (std::size_t note = 0; note < 128; ++note) {
                if (ratiosA[note][3].empty() || ratiosB[note][3].empty())
                    continue;
                ++sharedNotes;
                for (std::size_t region = 0; region < 4; ++region) {
                    if (ratiosA[note][region].empty() || ratiosB[note][region].empty())
                        continue;
                    const auto responseA = median(ratiosA[note][region]);
                    const auto responseB = median(ratiosB[note][region]);
                    if (responseA > 0.0)
                        factors[region].push_back(responseB / responseA);
                }
            }

            if (sharedNotes < config.minimumSharedNotesPerSegment)
                continue;

            const auto overallFactor = factors[3].empty() ? 1.0 : median(factors[3]);
            std::array<double, 3> bToA{};
            std::array<double, 3> aToB{};
            for (std::size_t region = 0; region < 3; ++region) {
                bToA[region] = factors[region].size() >= config.minimumSharedNotesPerSegment
                    ? median(factors[region])
                    : overallFactor;
                bToA[region] = std::max(0.5, std::min(2.0, bToA[region]));
                aToB[region] = 1.0 / bToA[region];
            }
            graph[a].push_back({b, bToA});
            graph[b].push_back({a, aToB});
        }
    }

    for (const auto group : {KeyGroup::whiteKeys, KeyGroup::blackKeys}) {
        const auto root = std::find_if(
            alignments.begin(), alignments.end(),
            [group](const auto& alignment) { return alignment.group == group; });
        if (root == alignments.end())
            continue;

        std::queue<std::size_t> pending;
        const auto rootIndex = static_cast<std::size_t>(std::distance(alignments.begin(), root));
        alignments[rootIndex].connectedToRoot = true;
        pending.push(rootIndex);
        while (!pending.empty()) {
            const auto current = pending.front();
            pending.pop();
            for (const auto& edge : graph[current]) {
                if (alignments[edge.neighbour].connectedToRoot)
                    continue;
                for (std::size_t region = 0; region < 3; ++region) {
                    alignments[edge.neighbour].velocityRegionScales[region] =
                        alignments[current].velocityRegionScales[region]
                        * edge.neighbourToCurrent[region];
                }
                alignments[edge.neighbour].connectedToRoot = true;
                pending.push(edge.neighbour);
            }
        }
    }

    allConnected = std::all_of(
        alignments.begin(), alignments.end(),
        [](const auto& alignment) { return alignment.connectedToRoot; });
    return alignments;
}

} // namespace

VelocityMap VelocityMap::identity() noexcept
{
    VelocityMap map;
    for (std::size_t velocity = 0; velocity < map.values.size(); ++velocity)
        map.values[velocity] = static_cast<std::uint8_t>(velocity);
    return map;
}

std::optional<std::size_t> insertVelocityCurvePoint(
    std::vector<VelocityCurvePoint>& points, double input, double output)
{
    if (points.size() < 2)
        return std::nullopt;
    input = std::round(std::clamp(input, 1.0, 127.0));
    const auto position = std::lower_bound(
        points.begin(), points.end(), input,
        [](const auto& point, const double value) { return point.input < value; });
    const auto index = static_cast<std::size_t>(std::distance(points.begin(), position));
    if (position != points.end() && position->input == input)
        return index;
    if (index == 0 || index == points.size() || points.size() >= 128)
        return std::nullopt;
    if (input < points[index - 1].input + 1.0 || input > points[index].input - 1.0)
        return std::nullopt;
    output = std::clamp(output, points[index - 1].output, points[index].output);
    points.insert(position, {input, output});
    return index;
}

void moveVelocityCurvePoint(
    std::vector<VelocityCurvePoint>& points, const std::size_t index,
    double input, double output)
{
    if (index >= points.size())
        return;
    input = std::round(std::clamp(input, 1.0, 127.0));
    output = std::round(std::clamp(output, 1.0, 127.0));
    if (index == 0)
        input = 1.0;
    else if (index + 1 == points.size())
        input = 127.0;
    else {
        const auto minimum = points[index - 1].input + 1.0;
        const auto maximum = points[index + 1].input - 1.0;
        input = minimum <= maximum ? std::clamp(input, minimum, maximum) : points[index].input;
    }
    if (index > 0)
        output = std::max(output, points[index - 1].output);
    if (index + 1 < points.size())
        output = std::min(output, points[index + 1].output);
    points[index] = {input, output};
}

double regionalCoverageScore(
    const std::array<NoteCalibrationStats, 128>& stats, std::size_t target) noexcept
{
    target = std::max<std::size_t>(1, target);
    std::array<std::size_t, 3> weakest{target, target, target};
    bool measured = false;
    for (const auto& note : stats) {
        if (note.samplesSeen == 0)
            continue;
        measured = true;
        for (std::size_t region = 0; region < weakest.size(); ++region)
            weakest[region] = std::min(weakest[region], note.samplesUsedByRegion[region]);
    }
    if (!measured)
        return 0.0;
    double score = 0.0;
    for (const auto count : weakest)
        score += static_cast<double>(count) / static_cast<double>(target);
    return score / 3.0;
}

std::uint8_t VelocityMap::apply(const std::uint8_t velocity) const noexcept
{
    return values[velocity];
}

bool VelocityMap::isMonotonic() const noexcept
{
    return std::is_sorted(values.begin(), values.end());
}

VelocityMap makeVelocityCurve(
    const double curvature,
    std::uint8_t minimumOutput,
    std::uint8_t maximumOutput) noexcept
{
    if (minimumOutput > maximumOutput)
        std::swap(minimumOutput, maximumOutput);

    VelocityMap map;
    map.values[0] = 0;
    const auto shapedCurvature = std::clamp(curvature, -1.0, 1.0);
    const auto exponent = shapedCurvature >= 0.0
        ? 1.0 + shapedCurvature * 3.0
        : 1.0 / (1.0 - shapedCurvature * 3.0);
    for (std::size_t velocity = 1; velocity < map.values.size(); ++velocity) {
        const auto normalized = static_cast<double>(velocity - 1) / 126.0;
        const auto shaped = std::pow(normalized, exponent);
        const auto output = static_cast<int>(std::lround(
            minimumOutput + shaped * (maximumOutput - minimumOutput)));
        map.values[velocity] = static_cast<std::uint8_t>(std::clamp(output, 1, 127));
    }
    return map;
}

double evaluateVelocityCurve(
    const std::vector<VelocityCurvePoint>& points,
    const bool smooth,
    const double input) noexcept
{
    const auto count = std::min<std::size_t>(points.size(), 128);
    if (count < 2)
        return std::clamp(input, 1.0, 127.0);
    if (input <= points.front().input)
        return std::clamp(points.front().output, 1.0, 127.0);
    if (input >= points[count - 1].input)
        return std::clamp(points[count - 1].output, 1.0, 127.0);

    std::size_t segment = 0;
    while (segment + 1 < count && input > points[segment + 1].input)
        ++segment;
    const auto x0 = points[segment].input;
    const auto x1 = points[segment + 1].input;
    const auto span = x1 - x0;
    if (span <= 0.0)
        return std::clamp(points[segment].output, 1.0, 127.0);
    const auto t = std::clamp((input - x0) / span, 0.0, 1.0);
    if (!smooth)
        return std::clamp(
            points[segment].output
                + t * (points[segment + 1].output - points[segment].output),
            1.0, 127.0);

    std::array<double, 128> deltas{};
    std::array<double, 128> tangents{};
    for (std::size_t index = 0; index + 1 < count; ++index) {
        const auto width = points[index + 1].input - points[index].input;
        deltas[index] = width > 0.0
            ? (points[index + 1].output - points[index].output) / width
            : 0.0;
    }
    tangents[0] = deltas[0];
    tangents[count - 1] = deltas[count - 2];
    for (std::size_t index = 1; index + 1 < count; ++index) {
        if (deltas[index - 1] <= 0.0 || deltas[index] <= 0.0) {
            tangents[index] = 0.0;
            continue;
        }
        const auto previousWidth = points[index].input - points[index - 1].input;
        const auto nextWidth = points[index + 1].input - points[index].input;
        const auto firstWeight = 2.0 * nextWidth + previousWidth;
        const auto secondWeight = nextWidth + 2.0 * previousWidth;
        tangents[index] = (firstWeight + secondWeight)
            / (firstWeight / deltas[index - 1] + secondWeight / deltas[index]);
    }

    const auto t2 = t * t;
    const auto t3 = t2 * t;
    const auto result = (2.0 * t3 - 3.0 * t2 + 1.0) * points[segment].output
        + (t3 - 2.0 * t2 + t) * span * tangents[segment]
        + (-2.0 * t3 + 3.0 * t2) * points[segment + 1].output
        + (t3 - t2) * span * tangents[segment + 1];
    return std::clamp(result, 1.0, 127.0);
}

VelocityMap makeVelocityCurve(
    const std::vector<VelocityCurvePoint>& points,
    const bool smooth) noexcept
{
    auto map = VelocityMap::identity();
    if (points.size() < 2)
        return map;
    for (std::size_t velocity = 1; velocity < map.values.size(); ++velocity) {
        const auto output = static_cast<int>(std::lround(evaluateVelocityCurve(
            points, smooth, static_cast<double>(velocity))));
        map.values[velocity] = static_cast<std::uint8_t>(std::clamp(output, 1, 127));
        if (velocity > 1)
            map.values[velocity] = std::max(map.values[velocity], map.values[velocity - 1]);
    }
    return map;
}

std::vector<VelocityCurvePoint> smoothCalibrationPoints(const VelocityMap& map)
{
    constexpr std::array<int, 9> anchors{1, 16, 32, 48, 64, 80, 96, 112, 127};
    std::vector<VelocityCurvePoint> points;
    for (const auto input : anchors)
        points.push_back({static_cast<double>(input),
            static_cast<double>(map.apply(static_cast<std::uint8_t>(input)))});

    while (points.size() < 127) {
        double worstError = 1.0 + 1.0e-9;
        int worstInput = 0;
        for (int input = 1; input <= 127; ++input) {
            const auto error = std::abs(evaluateVelocityCurve(points, true, input)
                - map.apply(static_cast<std::uint8_t>(input)));
            if (error > worstError) {
                worstError = error;
                worstInput = input;
            }
        }
        if (worstInput == 0)
            break;
        const auto position = std::lower_bound(points.begin(), points.end(), worstInput,
            [](const VelocityCurvePoint& point, int input) { return point.input < input; });
        points.insert(position, {static_cast<double>(worstInput),
            static_cast<double>(map.apply(static_cast<std::uint8_t>(worstInput)))});
    }
    return points;
}

CalibrationResult calibrate(
    const std::vector<CalibrationPress>& presses,
    const CalibrationConfig& config)
{
    CalibrationResult result;
    for (auto& map : result.noteMaps)
        map = VelocityMap::identity();

    result.segmentAlignments = alignSegments(presses, config, result.segmentsConnected);
    std::map<SegmentKey, std::array<double, 3>> segmentScales;
    for (const auto& alignment : result.segmentAlignments)
        segmentScales[{alignment.group, alignment.segmentId}] = alignment.velocityRegionScales;

    std::array<std::vector<Observation>, 128> observations;
    for (const auto& press : presses) {
        if (!press.accepted || press.notes.empty())
            continue;

        if (press.referenceVelocity <= 42.0)
            ++result.coverage.lowPresses;
        else if (press.referenceVelocity <= 84.0)
            ++result.coverage.mediumPresses;
        else
            ++result.coverage.highPresses;

        const auto region = velocityRegion(press.referenceVelocity);
        const auto scaleEntry = segmentScales.find({press.group, press.segmentId});
        const auto referenceScale = scaleEntry == segmentScales.end()
            ? 1.0
            : scaleEntry->second[region];
        const auto alignedReference = clampVelocity(press.referenceVelocity * referenceScale);

        for (const auto& event : press.notes) {
            if (event.velocity > 0 && belongsToGroup(event.note, press.group))
                observations[event.note].push_back({
                    static_cast<double>(event.velocity),
                    alignedReference,
                    region});
        }
    }

    for (std::size_t note = 0; note < observations.size(); ++note)
        result.noteMaps[note] = fitMap(observations[note], config, result.noteStats[note]);
    result.coverage.score = regionalCoverageScore(result.noteStats, config.desiredSamplesPerRegion);

    return result;
}

ValidationMetrics validate(
    const std::vector<CalibrationPress>& presses,
    const std::array<VelocityMap, 128>& maps)
{
    ValidationMetrics metrics;
    double rawError = 0.0;
    double calibratedError = 0.0;

    for (const auto& press : presses) {
        if (!press.accepted || press.notes.empty())
            continue;

        std::vector<double> corrected;
        corrected.reserve(press.notes.size());
        for (const auto& event : press.notes) {
            if (event.velocity > 0)
                corrected.push_back(maps[event.note].apply(event.velocity));
        }
        if (corrected.empty())
            continue;

        const auto correctedReference = median(corrected);
        ++metrics.presses;
        for (const auto& event : press.notes) {
            if (event.velocity == 0)
                continue;
            rawError += std::abs(static_cast<double>(event.velocity) - press.referenceVelocity);
            calibratedError += std::abs(
                static_cast<double>(maps[event.note].apply(event.velocity)) - correctedReference);
            ++metrics.observations;
        }
    }

    if (metrics.observations != 0) {
        metrics.rawMeanAbsoluteDeviation = rawError / static_cast<double>(metrics.observations);
        metrics.calibratedMeanAbsoluteDeviation = calibratedError / static_cast<double>(metrics.observations);
    }
    if (metrics.rawMeanAbsoluteDeviation > 0.0) {
        metrics.improvementFraction = 1.0
            - metrics.calibratedMeanAbsoluteDeviation / metrics.rawMeanAbsoluteDeviation;
    }
    return metrics;
}

} // namespace velcal
