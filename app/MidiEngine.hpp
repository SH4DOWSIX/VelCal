#pragma once

#include "velcal/calibration.hpp"

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class MidiEngine final : private juce::MidiInputCallback {
public:
    using MapBank = std::array<velcal::VelocityMap, 128>;

    struct Activity {
        std::uint64_t messagesReceived{};
        std::uint64_t messagesSent{};
        std::uint8_t lastNote{};
        std::uint8_t lastRawVelocity{};
        std::uint8_t lastCorrectedVelocity{};
        bool safetyTripped{};
        std::uint64_t messagesDropped{};
    };

    MidiEngine();
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
    Activity getActivity() const noexcept;

private:
    void handleIncomingMidiMessage(
        juce::MidiInput* source,
        const juce::MidiMessage& message) override;
    void outputWorkerLoop();
    void tripRoutingSafety();

    std::unique_ptr<juce::MidiInput> input;
    std::shared_ptr<juce::MidiOutput> output;
    std::shared_ptr<const MapBank> maps;
    std::atomic<bool> routing{false};
    std::atomic<bool> capturing{false};
    std::atomic<bool> workerExit{false};
    std::atomic<bool> routingSafetyTripped{false};
    std::mutex captureMutex;
    std::vector<velcal::NoteOn> capturedEvents;
    std::mutex outputQueueMutex;
    std::condition_variable outputQueueReady;
    std::deque<juce::MidiMessage> outputQueue;
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
