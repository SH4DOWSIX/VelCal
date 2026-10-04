#include "MidiEngine.hpp"

#include <cmath>

namespace {

constexpr std::size_t maximumMessagesPerSecond = 1'000;
constexpr std::size_t maximumQueuedMessages = 512;

} // namespace

MidiEngine::MidiEngine()
{
    auto identity = std::make_shared<MapBank>();
    for (auto& map : *identity)
        map = velcal::VelocityMap::identity();
    std::atomic_store(&maps, std::shared_ptr<const MapBank>(std::move(identity)));
    outputWorker = std::thread([this] { outputWorkerLoop(); });
}

MidiEngine::~MidiEngine()
{
    stopRouting();
    workerExit.store(true, std::memory_order_release);
    outputQueueReady.notify_one();
    if (outputWorker.joinable())
        outputWorker.join();
}

void MidiEngine::setMaps(const MapBank& newMaps)
{
    std::atomic_store(
        &maps,
        std::shared_ptr<const MapBank>(std::make_shared<MapBank>(newMaps)));
}

bool MidiEngine::startRouting(
    const juce::String& inputIdentifier,
    const juce::String& outputIdentifier,
    const bool createVirtualOutput,
    juce::String& error)
{
    stopRouting();
    routingSafetyTripped.store(false, std::memory_order_release);
    rateWindowStartedMs = juce::Time::getMillisecondCounter();
    messagesInRateWindow = 0;

    std::unique_ptr<juce::MidiOutput> openedOutput = createVirtualOutput
        ? juce::MidiOutput::createNewDevice("VelCal Output")
        : juce::MidiOutput::openDevice(outputIdentifier);
    if (openedOutput == nullptr) {
        error = createVirtualOutput
            ? "This platform MIDI backend could not create VelCal Output. On Windows, select an installed virtual MIDI cable until Windows MIDI Services is enabled."
            : "The selected MIDI output could not be opened.";
        return false;
    }

    auto openedInput = juce::MidiInput::openDevice(inputIdentifier, this);
    if (openedInput == nullptr) {
        error = "The selected MIDI input could not be opened. Close other software that may have exclusive access to it.";
        return false;
    }

    std::atomic_store(&output, std::shared_ptr<juce::MidiOutput>(std::move(openedOutput)));
    input = std::move(openedInput);
    routing.store(true, std::memory_order_release);
    input->start();
    return true;
}

bool MidiEngine::startCapture(
    const juce::String& inputIdentifier,
    juce::String& error)
{
    stopRouting();
    routingSafetyTripped.store(false, std::memory_order_release);
    {
        const std::scoped_lock lock(captureMutex);
        capturedEvents.clear();
    }

    auto openedInput = juce::MidiInput::openDevice(inputIdentifier, this);
    if (openedInput == nullptr) {
        error = "The selected MIDI input could not be opened. Close other software that may have exclusive access to it.";
        return false;
    }

    input = std::move(openedInput);
    capturing.store(true, std::memory_order_release);
    input->start();
    return true;
}

std::vector<velcal::NoteOn> MidiEngine::getCapturedEventsSnapshot()
{
    const std::scoped_lock lock(captureMutex);
    return capturedEvents;
}

std::vector<velcal::NoteOn> MidiEngine::finishCapture()
{
    capturing.store(false, std::memory_order_release);
    if (input != nullptr)
        input->stop();
    input.reset();

    const std::scoped_lock lock(captureMutex);
    auto result = std::move(capturedEvents);
    capturedEvents.clear();
    return result;
}

void MidiEngine::cancelCapture()
{
    static_cast<void>(finishCapture());
}

void MidiEngine::stopRouting()
{
    routing.store(false, std::memory_order_release);
    capturing.store(false, std::memory_order_release);
    if (input != nullptr)
        input->stop();
    input.reset();
    {
        const std::scoped_lock lock(outputQueueMutex);
        outputQueue.clear();
    }
    std::atomic_store(&output, std::shared_ptr<juce::MidiOutput>{});
}

bool MidiEngine::isRouting() const noexcept
{
    return routing.load(std::memory_order_acquire);
}

bool MidiEngine::isCapturing() const noexcept
{
    return capturing.load(std::memory_order_acquire);
}

MidiEngine::Activity MidiEngine::getActivity() const noexcept
{
    return {
        messagesReceived.load(std::memory_order_relaxed),
        messagesSent.load(std::memory_order_relaxed),
        lastNote.load(std::memory_order_relaxed),
        lastRawVelocity.load(std::memory_order_relaxed),
        lastCorrectedVelocity.load(std::memory_order_relaxed),
        routingSafetyTripped.load(std::memory_order_acquire),
        messagesDropped.load(std::memory_order_relaxed),
    };
}

void MidiEngine::tripRoutingSafety()
{
    routing.store(false, std::memory_order_release);
    routingSafetyTripped.store(true, std::memory_order_release);
    messagesDropped.fetch_add(1, std::memory_order_relaxed);
    const std::scoped_lock lock(outputQueueMutex);
    outputQueue.clear();
}

void MidiEngine::outputWorkerLoop()
{
    while (!workerExit.load(std::memory_order_acquire)) {
        juce::MidiMessage message;
        {
            std::unique_lock lock(outputQueueMutex);
            outputQueueReady.wait(lock, [this] {
                return workerExit.load(std::memory_order_acquire) || !outputQueue.empty();
            });
            if (workerExit.load(std::memory_order_acquire))
                break;
            message = std::move(outputQueue.front());
            outputQueue.pop_front();
        }

        const auto currentOutput = std::atomic_load(&output);
        if (routing.load(std::memory_order_acquire) && currentOutput != nullptr) {
            currentOutput->sendMessageNow(message);
            messagesSent.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void MidiEngine::handleIncomingMidiMessage(
    juce::MidiInput*,
    const juce::MidiMessage& message)
{
    messagesReceived.fetch_add(1, std::memory_order_relaxed);
    auto outgoing = message;

    if (routing.load(std::memory_order_acquire)) {
        const auto nowMs = static_cast<std::uint64_t>(juce::Time::getMillisecondCounter());
        if (nowMs - rateWindowStartedMs >= 1'000) {
            rateWindowStartedMs = nowMs;
            messagesInRateWindow = 0;
        }
        if (++messagesInRateWindow > maximumMessagesPerSecond) {
            tripRoutingSafety();
            return;
        }
    }

    if (message.isNoteOn()) {
        const auto note = static_cast<std::uint8_t>(message.getNoteNumber());
        const auto rawVelocity = static_cast<std::uint8_t>(message.getVelocity());
        if (capturing.load(std::memory_order_acquire)) {
            const auto timestampUs = static_cast<velcal::TimestampUs>(
                std::llround(juce::Time::getMillisecondCounterHiRes() * 1000.0));
            const std::scoped_lock lock(captureMutex);
            capturedEvents.push_back({note, rawVelocity, timestampUs});
        }
        const auto currentMaps = std::atomic_load(&maps);
        const auto correctedVelocity = currentMaps->at(note).apply(rawVelocity);
        outgoing = juce::MidiMessage::noteOn(
            message.getChannel(),
            static_cast<int>(note),
            correctedVelocity);
        outgoing.setTimeStamp(message.getTimeStamp());
        lastNote.store(note, std::memory_order_relaxed);
        lastRawVelocity.store(rawVelocity, std::memory_order_relaxed);
        lastCorrectedVelocity.store(correctedVelocity, std::memory_order_relaxed);
    }

    if (routing.load(std::memory_order_acquire)) {
        bool queueOverflow = false;
        {
            const std::scoped_lock lock(outputQueueMutex);
            queueOverflow = outputQueue.size() >= maximumQueuedMessages;
            if (!queueOverflow)
                outputQueue.push_back(std::move(outgoing));
        }
        if (queueOverflow) {
            tripRoutingSafety();
            return;
        }
        outputQueueReady.notify_one();
    }
}
