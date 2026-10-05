#include "MidiEngine.hpp"

#include <chrono>
#include <cmath>

namespace {

constexpr std::size_t maximumMessagesPerSecond = 1'000;
constexpr std::size_t maximumQueuedMessages = 512;
constexpr std::uint32_t outputTimeoutMs = 2'000;
constexpr auto shutdownWait = std::chrono::milliseconds(250);

class JuceOutput final : public MidiEngine::OutputDevice {
public:
    explicit JuceOutput(std::unique_ptr<juce::MidiOutput> device) : output(std::move(device)) {}
    void send(const juce::MidiMessage& message) override { output->sendMessageNow(message); }
private:
    std::unique_ptr<juce::MidiOutput> output;
};

} // namespace

struct MidiEngine::OutputSession {
    std::mutex mutex;
    std::condition_variable ready;
    std::condition_variable completed;
    std::deque<juce::MidiMessage> queue;
    std::atomic<bool> stopped{false};
    std::atomic<bool> opening{true};
    std::atomic<bool> safetyTripped{false};
    std::atomic<bool> operationInProgress{false};
    std::atomic<std::uint32_t> operationStartedMs{0};
    std::atomic<std::uint64_t> sent{0};
    bool finished{};
    juce::String error;
};

MidiEngine::MidiEngine(OutputFactory factory) : outputFactory(std::move(factory))
{
    if (!outputFactory) {
        outputFactory = [](const juce::String& identifier, const bool createVirtual)
            -> std::unique_ptr<OutputDevice> {
            auto device = createVirtual ? juce::MidiOutput::createNewDevice("VelCal Output")
                                        : juce::MidiOutput::openDevice(identifier);
            if (!device)
                return {};
            return std::make_unique<JuceOutput>(std::move(device));
        };
    }
    auto identity = std::make_shared<MapBank>();
    for (auto& map : *identity)
        map = velcal::VelocityMap::identity();
    std::atomic_store(&maps, std::shared_ptr<const MapBank>(std::move(identity)));
}

MidiEngine::~MidiEngine() { stopRouting(); }

void MidiEngine::setMaps(const MapBank& newMaps)
{
    std::atomic_store(&maps,
        std::shared_ptr<const MapBank>(std::make_shared<MapBank>(newMaps)));
}

bool MidiEngine::startOutput(
    const juce::String& identifier, const bool createVirtual, juce::String& error)
{
    if (stalledOutput) {
        const std::scoped_lock lock(stalledOutput->mutex);
        if (!stalledOutput->finished) {
            error = "The previous MIDI output is still busy. Wait for it to recover before routing again.";
            return false;
        }
    }
    stalledOutput.reset();
    rateWindowStartedMs = juce::Time::getMillisecondCounter();
    messagesInRateWindow = 0;
    auto session = std::make_shared<OutputSession>();
    std::atomic_store(&outputSession, session);
    outputWorker = std::thread([session, factory = outputFactory, identifier, createVirtual] {
        outputWorkerLoop(session, factory, identifier, createVirtual);
    });
    return true;
}

bool MidiEngine::startRouting(
    const juce::String& inputIdentifier, const juce::String& outputIdentifier,
    const bool createVirtualOutput, juce::String& error)
{
    stopRouting();
    auto openedInput = juce::MidiInput::openDevice(inputIdentifier, this);
    if (!openedInput) {
        error = "The selected MIDI input could not be opened. Close other software that may have exclusive access to it.";
        return false;
    }
    if (!startOutput(outputIdentifier, createVirtualOutput, error))
        return false;
    input = std::move(openedInput);
    input->start();
    return true;
}

bool MidiEngine::startCapture(const juce::String& inputIdentifier, juce::String& error)
{
    stopRouting();
    {
        const std::scoped_lock lock(captureMutex);
        capturedEvents.clear();
    }
    auto openedInput = juce::MidiInput::openDevice(inputIdentifier, this);
    if (!openedInput) {
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
    if (input)
        input->stop();
    input.reset();
    const std::scoped_lock lock(captureMutex);
    auto result = std::move(capturedEvents);
    capturedEvents.clear();
    return result;
}

void MidiEngine::cancelCapture() { static_cast<void>(finishCapture()); }

void MidiEngine::stopRouting()
{
    const auto session = std::atomic_load(&outputSession);
    if (session) {
        const std::scoped_lock lock(session->mutex);
        session->stopped.store(true, std::memory_order_release);
        session->queue.clear();
        session->ready.notify_one();
    }
    capturing.store(false, std::memory_order_release);
    if (input)
        input->stop();
    input.reset();
    if (outputWorker.joinable()) {
        std::unique_lock lock(session->mutex);
        const auto finished = session->completed.wait_for(lock, shutdownWait,
            [&session] { return session->finished; });
        lock.unlock();
        if (finished)
            outputWorker.join();
        else {
            // A blocked driver owns only this session, never MidiEngine or its UI.
            stalledOutput = session;
            outputWorker.detach();
        }
        messagesSent.fetch_add(session->sent.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    std::atomic_store(&outputSession, std::shared_ptr<OutputSession>{});
}

bool MidiEngine::isRouting() const noexcept
{
    const auto session = std::atomic_load(&outputSession);
    return session && !session->stopped.load(std::memory_order_acquire);
}

bool MidiEngine::isCapturing() const noexcept { return capturing.load(std::memory_order_acquire); }

MidiEngine::Activity MidiEngine::getActivity() const
{
    Activity activity{
        messagesReceived.load(std::memory_order_relaxed),
        messagesSent.load(std::memory_order_relaxed),
        lastNote.load(std::memory_order_relaxed),
        lastRawVelocity.load(std::memory_order_relaxed),
        lastCorrectedVelocity.load(std::memory_order_relaxed),
        false, messagesDropped.load(std::memory_order_relaxed), false, {}};
    const auto session = std::atomic_load(&outputSession);
    if (!session)
        return activity;
    if (session->operationInProgress.load(std::memory_order_acquire)
        && static_cast<std::uint32_t>(juce::Time::getMillisecondCounter()
            - session->operationStartedMs.load(std::memory_order_relaxed)) > outputTimeoutMs) {
        const std::scoped_lock lock(session->mutex);
        session->stopped.store(true, std::memory_order_release);
        session->error = "Routing stopped: the MIDI output is not responding";
        session->queue.clear();
        session->ready.notify_one();
    }
    activity.messagesSent += session->sent.load(std::memory_order_relaxed);
    activity.safetyTripped = session->safetyTripped.load(std::memory_order_acquire);
    activity.outputOpening = session->opening.load(std::memory_order_acquire);
    const std::scoped_lock lock(session->mutex);
    activity.outputError = session->error;
    return activity;
}

void MidiEngine::tripRoutingSafety()
{
    const auto session = std::atomic_load(&outputSession);
    if (!session)
        return;
    const std::scoped_lock lock(session->mutex);
    session->stopped.store(true, std::memory_order_release);
    session->safetyTripped.store(true, std::memory_order_release);
    messagesDropped.fetch_add(session->queue.size() + 1, std::memory_order_relaxed);
    session->queue.clear();
    session->ready.notify_one();
}

void MidiEngine::outputWorkerLoop(
    const std::shared_ptr<OutputSession>& session, const OutputFactory& factory,
    const juce::String& identifier, const bool createVirtual)
{
    const auto beginOperation = [&session] {
        session->operationStartedMs.store(juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
        session->operationInProgress.store(true, std::memory_order_release);
    };
    std::unique_ptr<OutputDevice> output;
    try {
        beginOperation();
        output = factory(identifier, createVirtual);
        session->operationInProgress.store(false, std::memory_order_release);
        if (!output) {
            const std::scoped_lock lock(session->mutex);
            if (session->error.isEmpty())
                session->error = "The selected MIDI output could not be opened.";
            session->stopped.store(true, std::memory_order_release);
        }
        session->opening.store(false, std::memory_order_release);
        std::array<bool, 16> touchedChannels{};
        const auto send = [&](const juce::MidiMessage& message) {
            beginOperation();
            output->send(message);
            session->operationInProgress.store(false, std::memory_order_release);
            session->sent.fetch_add(1, std::memory_order_relaxed);
        };
        while (output && !session->stopped.load(std::memory_order_acquire)) {
            juce::MidiMessage message;
            {
                std::unique_lock lock(session->mutex);
                session->ready.wait(lock, [&session] {
                    return session->stopped.load(std::memory_order_acquire) || !session->queue.empty();
                });
                if (session->stopped.load(std::memory_order_acquire))
                    break;
                message = std::move(session->queue.front());
                session->queue.pop_front();
            }
            if (session->stopped.load(std::memory_order_acquire))
                break;
            send(message);
            const auto channel = message.getChannel();
            if (channel > 0 && (message.isNoteOnOrOff()
                || (message.isController() && (message.getControllerNumber() == 64
                    || message.getControllerNumber() == 66 || message.getControllerNumber() == 69))))
                touchedChannels[static_cast<std::size_t>(channel - 1)] = true;
        }
        if (output) {
            for (int channel = 1; channel <= 16; ++channel) {
                if (!touchedChannels[static_cast<std::size_t>(channel - 1)])
                    continue;
                send(juce::MidiMessage::controllerEvent(channel, 64, 0));
                send(juce::MidiMessage::controllerEvent(channel, 66, 0));
                send(juce::MidiMessage::controllerEvent(channel, 69, 0));
                send(juce::MidiMessage::allNotesOff(channel));
                send(juce::MidiMessage::allSoundOff(channel));
            }
        }
    } catch (const std::exception& error) {
        const std::scoped_lock lock(session->mutex);
        session->error = juce::String("MIDI output failed: ") + error.what();
        session->stopped.store(true, std::memory_order_release);
    }
    beginOperation();
    output.reset();
    session->operationInProgress.store(false, std::memory_order_release);
    session->opening.store(false, std::memory_order_release);
    {
        const std::scoped_lock lock(session->mutex);
        session->finished = true;
    }
    session->completed.notify_all();
}

void MidiEngine::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
{
    messagesReceived.fetch_add(1, std::memory_order_relaxed);
    auto outgoing = message;
    const auto session = std::atomic_load(&outputSession);
    const auto routing = session && !session->stopped.load(std::memory_order_acquire)
        && !session->opening.load(std::memory_order_acquire);
    if (routing) {
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
        outgoing = juce::MidiMessage::noteOn(message.getChannel(), static_cast<int>(note), correctedVelocity);
        outgoing.setTimeStamp(message.getTimeStamp());
        lastNote.store(note, std::memory_order_relaxed);
        lastRawVelocity.store(rawVelocity, std::memory_order_relaxed);
        lastCorrectedVelocity.store(correctedVelocity, std::memory_order_relaxed);
    }
    if (routing) {
        bool overflow = false;
        {
            const std::scoped_lock lock(session->mutex);
            if (session->stopped.load(std::memory_order_acquire))
                return;
            overflow = session->queue.size() >= maximumQueuedMessages;
            if (!overflow)
                session->queue.push_back(std::move(outgoing));
        }
        if (overflow)
            tripRoutingSafety();
        else
            session->ready.notify_one();
    }
}
