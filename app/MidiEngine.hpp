#pragma once

#include "velcal/calibration.hpp"

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class MidiEngine final : private juce::MidiInputCallback {
public:
    using MapBank = std::array<velcal::VelocityMap, 128>;

    class OutputDevice {
    public:
        virtual ~OutputDevice() = default;
        virtual void send(const juce::MidiMessage& message) = 0;
    };
    using OutputFactory = std::function<std::unique_ptr<OutputDevice>(const juce::String&, bool)>;

    struct Activity {
        std::uint64_t messagesReceived{};
        std::uint64_t messagesSent{};
        std::uint8_t lastNote{};
        std::uint8_t lastRawVelocity{};
        std::uint8_t lastCorrectedVelocity{};
        bool safetyTripped{};
        std::uint64_t messagesDropped{};
        bool outputOpening{};
        juce::String outputError;
    };

    explicit MidiEngine(OutputFactory factory = {});
    ~MidiEngine() override;

    void setMaps(const MapBank& maps);
    bool startRouting(
        const juce::String& inputIdentifier,
        const juce::String& outputIdentifier,
        bool createVirtualOutput,
        juce::String& error);
    bool startCapture(const juce::String& inputIdentifier, juce::String& error);
    std::vector<velcal::NoteOn> getCapturedEventsSnapshot();
    std::vector<velcal::NoteOn> finishCapture();
    void cancelCapture();
    void stopRouting();
    bool isRouting() const noexcept;
    bool isCapturing() const noexcept;
    Activity getActivity() const;

private:
    void handleIncomingMidiMessage(
        juce::MidiInput* source,
        const juce::MidiMessage& message) override;
    struct OutputSession;
    bool startOutput(const juce::String& identifier, bool createVirtual, juce::String& error);
    static void outputWorkerLoop(
        const std::shared_ptr<OutputSession>& session, const OutputFactory& factory,
        const juce::String& identifier, bool createVirtual);
    void tripRoutingSafety();
    friend struct MidiEngineTestAccess;

    std::unique_ptr<juce::MidiInput> input;
    OutputFactory outputFactory;
    std::shared_ptr<OutputSession> outputSession;
    std::shared_ptr<OutputSession> stalledOutput;
    std::shared_ptr<const MapBank> maps;
    std::atomic<bool> capturing{false};
    std::mutex captureMutex;
    std::vector<velcal::NoteOn> capturedEvents;
    std::thread outputWorker;
    std::uint64_t rateWindowStartedMs{};
    std::size_t messagesInRateWindow{};
    std::atomic<std::uint64_t> messagesReceived{0};
    std::atomic<std::uint64_t> messagesSent{0};
    std::atomic<std::uint64_t> messagesDropped{0};
    std::atomic<std::uint8_t> lastNote{0};
    std::atomic<std::uint8_t> lastRawVelocity{0};
    std::atomic<std::uint8_t> lastCorrectedVelocity{0};
};
