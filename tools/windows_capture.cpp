#include "velcal/calibration.hpp"
#include "velcal/profile.hpp"

#include <Windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

class MidiEventBuffer {
public:
    void push(const velcal::NoteOn event)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(event);
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.clear();
    }

    std::vector<velcal::NoteOn> snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<velcal::NoteOn> events_;
};

void CALLBACK midiInputCallback(
    HMIDIIN,
    const UINT message,
    const DWORD_PTR instance,
    const DWORD_PTR packedMessage,
    const DWORD_PTR timestampMs)
{
    if (message != MIM_DATA || instance == 0)
        return;

    const auto status = static_cast<std::uint8_t>(packedMessage & 0xff);
    const auto note = static_cast<std::uint8_t>((packedMessage >> 8) & 0xff);
    const auto velocity = static_cast<std::uint8_t>((packedMessage >> 16) & 0xff);
    if ((status & 0xf0) != 0x90 || velocity == 0)
        return;

    auto* buffer = reinterpret_cast<MidiEventBuffer*>(instance);
    buffer->push({note, velocity, static_cast<velcal::TimestampUs>(timestampMs) * 1'000});
}

std::string noteName(const std::uint8_t note)
{
    static constexpr const char* names[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return std::string(names[note % 12]) + std::to_string(static_cast<int>(note) / 12 - 1);
}

std::string groupName(const velcal::KeyGroup group)
{
    return group == velcal::KeyGroup::whiteKeys ? "white" : "black";
}

std::string issueName(const velcal::PressIssue issue)
{
    switch (issue) {
    case velcal::PressIssue::incomplete: return "incomplete";
    case velcal::PressIssue::duplicateNote: return "duplicate-note";
    case velcal::PressIssue::unexpectedNote: return "unexpected-note";
    case velcal::PressIssue::invalidVelocity: return "invalid-velocity";
    }
    return "unknown";
}

std::string joinedIssues(const std::vector<velcal::PressIssue>& issues)
{
    std::ostringstream output;
    for (std::size_t i = 0; i < issues.size(); ++i) {
        if (i != 0)
            output << '|';
        output << issueName(issues[i]);
    }
    return output.str();
}

std::string readLine(const std::string& prompt)
{
    std::cout << prompt << std::flush;
    std::string input;
    if (!std::getline(std::cin, input)) {
        std::cout << "\nCapture cancelled.\n";
        std::exit(0);
    }
    return input;
}

int readNumber(const std::string& prompt, const int minimum, const int maximum)
{
    while (true) {
        const auto input = readLine(prompt);
        try {
            std::size_t used = 0;
            const auto value = std::stoi(input, &used);
            if (used == input.size() && value >= minimum && value <= maximum)
                return value;
        } catch (...) {
        }
        std::cout << "Enter a number from " << minimum << " to " << maximum << ".\n";
    }
}

std::vector<std::wstring> listMidiInputs()
{
    std::vector<std::wstring> names;
    const auto count = midiInGetNumDevs();
    for (UINT index = 0; index < count; ++index) {
        MIDIINCAPSW capabilities{};
        if (midiInGetDevCapsW(index, &capabilities, sizeof(capabilities)) == MMSYSERR_NOERROR)
            names.emplace_back(capabilities.szPname);
        else
            names.emplace_back(L"Unknown MIDI input");
    }
    return names;
}

std::string utf8FromWide(const std::wstring& value)
{
    if (value.empty())
        return {};
    const auto size = WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::string utcTimestamp()
{
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
    gmtime_s(&utc, &now);
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

std::vector<std::uint8_t> expectedNotes(
    const std::uint8_t lowest,
    const std::uint8_t highest,
    const velcal::KeyGroup group)
{
    std::vector<std::uint8_t> notes;
    for (int note = lowest; note <= highest; ++note) {
        if (velcal::belongsToGroup(static_cast<std::uint8_t>(note), group))
            notes.push_back(static_cast<std::uint8_t>(note));
    }
    return notes;
}

bool learnSection(
    MidiEventBuffer& buffer,
    const velcal::KeyGroup group,
    std::uint8_t& lowest,
    std::uint8_t& highest)
{
    while (true) {
        readLine("Press Enter when ready to teach the section...");
        buffer.clear();
        std::cout << "Press every " << groupName(group)
                  << " key covered by the bar once, then press Enter.\n";
        readLine("");

        const auto events = buffer.snapshot();
        std::set<std::uint8_t> detected;
        for (const auto& event : events) {
            if (velcal::belongsToGroup(event.note, group))
                detected.insert(event.note);
        }
        if (detected.size() < 2) {
            std::cout << "Fewer than two matching keys were detected. Try again.\n";
            continue;
        }

        lowest = *detected.begin();
        highest = *detected.rbegin();
        const auto expected = expectedNotes(lowest, highest, group);
        std::cout << "Detected section: ";
        for (const auto note : expected)
            std::cout << noteName(note) << '(' << static_cast<int>(note) << ") ";
        std::cout << "\n";

        if (detected.size() != expected.size()) {
            std::cout << "Some keys inside that span were not detected. Please teach it again.\n";
            continue;
        }
        const auto confirmation = readLine("Use this section? [Y/n] ");
        if (confirmation.empty() || confirmation == "y" || confirmation == "Y")
            return true;
    }
}

std::vector<velcal::CalibrationPress> captureSection(
    MidiEventBuffer& buffer,
    const velcal::KeyGroup group,
    const velcal::SegmentId segmentId,
    const std::uint8_t lowest,
    const std::uint8_t highest)
{
    readLine("Press Enter when ready to begin recording...");
    buffer.clear();
    std::cout << "Recording. Press the bar repeatedly from very soft to very hard.\n"
              << "Press Enter when finished.\n";
    readLine("");
    auto events = buffer.snapshot();
    std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
        return a.timestampUs < b.timestampUs;
    });

    velcal::PressCollectorConfig config;
    config.lowestNote = lowest;
    config.highestNote = highest;
    velcal::PressCollector collector(group, config, segmentId);
    std::vector<velcal::CalibrationPress> presses;
    for (const auto& event : events) {
        if (auto completed = collector.add(event))
            presses.push_back(std::move(*completed));
    }
    if (auto completed = collector.flush())
        presses.push_back(std::move(*completed));

    const auto valid = std::count_if(presses.begin(), presses.end(), [](const auto& press) {
        return press.accepted;
    });
    std::cout << "Section " << segmentId << ": " << valid << " valid, "
              << presses.size() - static_cast<std::size_t>(valid) << " rejected presses.\n";
    for (const auto& press : presses) {
        std::cout << "  Press " << press.sequence << ": "
                  << (press.accepted ? "accepted" : "rejected")
                  << ", reference " << std::fixed << std::setprecision(1)
                  << press.referenceVelocity;
        if (!press.issues.empty())
            std::cout << ", " << joinedIssues(press.issues);
        std::cout << '\n';
    }
    return presses;
}

bool writeCsv(
    const std::filesystem::path& path,
    const std::vector<velcal::CalibrationPress>& presses)
{
    std::ofstream output(path);
    if (!output)
        return false;
    output << "segment,group,press,accepted,issues,reference,note,note_name,velocity,timestamp_us\n";
    for (const auto& press : presses) {
        for (const auto& event : press.notes) {
            output << press.segmentId << ',' << groupName(press.group) << ','
                   << press.sequence << ',' << (press.accepted ? 1 : 0) << ','
                   << joinedIssues(press.issues) << ',' << press.referenceVelocity << ','
                   << static_cast<int>(event.note) << ',' << noteName(event.note) << ','
                   << static_cast<int>(event.velocity) << ',' << event.timestampUs << '\n';
        }
    }
    return output.good();
}

} // namespace

int main()
{
    std::cout << "VelCal physical capture prototype\n\n";
    const auto devices = listMidiInputs();
    if (devices.empty()) {
        std::cout << "No MIDI input devices were found.\n";
        return 1;
    }
    for (std::size_t index = 0; index < devices.size(); ++index)
        std::wcout << L"  " << index << L": " << devices[index] << L'\n';
    const auto deviceIndex = readNumber(
        "Select MIDI input: ", 0, static_cast<int>(devices.size() - 1));
    const auto groupInput = readLine("Calibrate white or black keys? [w/b] ");
    const auto group = (!groupInput.empty() && (groupInput[0] == 'b' || groupInput[0] == 'B'))
        ? velcal::KeyGroup::blackKeys
        : velcal::KeyGroup::whiteKeys;

    MidiEventBuffer buffer;
    HMIDIIN inputHandle{};
    const auto openResult = midiInOpen(
        &inputHandle,
        static_cast<UINT>(deviceIndex),
        reinterpret_cast<DWORD_PTR>(&midiInputCallback),
        reinterpret_cast<DWORD_PTR>(&buffer),
        CALLBACK_FUNCTION);
    if (openResult != MMSYSERR_NOERROR) {
        std::cout << "Could not open that MIDI input (error " << openResult << ").\n";
        return 1;
    }
    if (midiInStart(inputHandle) != MMSYSERR_NOERROR) {
        std::cout << "Could not start the MIDI input.\n";
        midiInClose(inputHandle);
        return 1;
    }

    std::vector<velcal::CalibrationPress> allPresses;
    velcal::SegmentId segmentId = 1;
    bool captureAnother = true;
    while (captureAnother) {
        std::uint8_t lowest = 0;
        std::uint8_t highest = 0;
        learnSection(buffer, group, lowest, highest);
        auto presses = captureSection(buffer, group, segmentId, lowest, highest);
        allPresses.insert(
            allPresses.end(),
            std::make_move_iterator(presses.begin()),
            std::make_move_iterator(presses.end()));
        ++segmentId;
        const auto answer = readLine("Capture another overlapping section? [y/N] ");
        captureAnother = answer == "y" || answer == "Y";
    }

    midiInStop(inputHandle);
    midiInReset(inputHandle);
    midiInClose(inputHandle);

    const auto result = velcal::calibrate(allPresses);
    std::cout << "\nCoverage: low " << result.coverage.lowPresses
              << ", medium " << result.coverage.mediumPresses
              << ", high " << result.coverage.highPresses << "\n";
    std::cout << "Section chain: "
              << (result.segmentsConnected ? "connected" : "DISCONNECTED - add overlap") << "\n";

    const std::filesystem::path captureDirectory(VELCAL_CAPTURE_DIR);
    std::filesystem::create_directories(captureDirectory);
    const auto stamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const auto path = captureDirectory
        / ("velcal-" + groupName(group) + '-' + std::to_string(stamp) + ".csv");
    if (!writeCsv(path, allPresses)) {
        std::cout << "Could not write capture file.\n";
        return 1;
    }
    std::cout << "Raw capture saved to " << path.string() << "\n";

    const std::filesystem::path profileDirectory(VELCAL_PROFILE_DIR);
    const auto profilePath = profileDirectory
        / ("velcal-" + groupName(group) + '-' + std::to_string(stamp) + ".velcal.json");
    velcal::CalibrationProfile profile;
    profile.profileName = utf8FromWide(devices[static_cast<std::size_t>(deviceIndex)])
        + " " + groupName(group) + " calibration";
    profile.createdUtc = utcTimestamp();
    profile.inputDevice.name = utf8FromWide(devices[static_cast<std::size_t>(deviceIndex)]);
    profile.presses = allPresses;
    profile.generated = result;
    try {
        velcal::saveProfile(profile, profilePath);
        std::cout << "Calibration profile saved to " << profilePath.string() << "\n";
    } catch (const std::exception& error) {
        std::cout << "Could not write profile: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
