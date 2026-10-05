#include "MainComponent.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>

struct MidiEngineTestAccess {
    static bool start(MidiEngine& engine, juce::String& error)
    {
        engine.stopRouting();
        return engine.startOutput("test", false, error);
    }
    static void receive(MidiEngine& engine, const juce::MidiMessage& message)
    { engine.handleIncomingMidiMessage(nullptr, message); }
    static void capture(MidiEngine& engine) { engine.capturing.store(true); }
};

struct MainComponentTestAccess {
    static void startCapture(MainComponent& component)
    {
        MidiEngineTestAccess::capture(component.midiEngine);
        component.updateCaptureControls();
    }
    static bool controlsEnabled(const MainComponent& component)
    {
        return component.keyGroupBox.isEnabled() && component.midiInputBox.isEnabled()
            && component.routingToggle.isEnabled()
            && component.captureButton.getButtonText() == "Start section";
    }
    static void changeOutput(MainComponent& component)
    { component.comboBoxChanged(&component.midiOutputBox); }
    static void newProfile(MainComponent& component) { component.replaceWithNewProfile(); }
    static void clear(MainComponent& component) { component.clearMeasurementsConfirmed(); }
    static bool dirty(const MainComponent& component) { return component.profileDirty; }
    static void editTrim(MainComponent& component)
    {
        component.keyAdjustmentSlider.setValue(4, juce::dontSendNotification);
        component.sliderValueChanged(&component.keyAdjustmentSlider);
    }
    static bool hasDirtyMarker(const MainComponent& component)
    { return component.profileBox.getText().endsWith(" *"); }
    static void load(MainComponent& component, const juce::File& file) { component.loadProfile(file); }
    static void save(MainComponent& component, const juce::File& file) { component.writeProfile(file); }
    static void requestNewProfile(MainComponent& component) { component.createNewProfile(); }
    static std::string name(const MainComponent& component) { return component.profile->profileName; }
};

namespace {

using namespace std::chrono_literals;
int failures = 0;

void expect(const bool condition, const char* description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

template <typename Predicate>
bool waitFor(Predicate predicate, const std::chrono::milliseconds timeout = 1s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(2ms);
    }
    return true;
}

struct FakeState {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<juce::MidiMessage> messages;
    bool blockOpen{};
    bool blockSend{};
    bool blockClose{};
    bool openEntered{};
    bool sendEntered{};
    bool closeEntered{};
    bool released{};
    bool destroyed{};

    void release()
    {
        const std::scoped_lock lock(mutex);
        released = true;
        changed.notify_all();
    }
    bool has(bool FakeState::*flag)
    { const std::scoped_lock lock(mutex); return this->*flag; }
    std::vector<juce::MidiMessage> snapshot()
    { const std::scoped_lock lock(mutex); return messages; }
};

class FakeOutput final : public MidiEngine::OutputDevice {
public:
    explicit FakeOutput(std::shared_ptr<FakeState> data) : state(std::move(data)) {}
    ~FakeOutput() override
    {
        std::unique_lock lock(state->mutex);
        state->closeEntered = true;
        state->changed.wait(lock, [this] { return !state->blockClose || state->released; });
        state->destroyed = true;
    }
    void send(const juce::MidiMessage& message) override
    {
        std::unique_lock lock(state->mutex);
        state->sendEntered = true;
        state->changed.wait(lock, [this] { return !state->blockSend || state->released; });
        state->messages.push_back(message);
    }
private:
    std::shared_ptr<FakeState> state;
};

MidiEngine::OutputFactory fakeFactory(const std::shared_ptr<FakeState>& state)
{
    return [state](const juce::String&, bool) -> std::unique_ptr<MidiEngine::OutputDevice> {
        std::unique_lock lock(state->mutex);
        state->openEntered = true;
        state->changed.wait(lock, [&state] { return !state->blockOpen || state->released; });
        return std::make_unique<FakeOutput>(state);
    };
}

bool startReady(MidiEngine& engine)
{
    juce::String error;
    const auto started = MidiEngineTestAccess::start(engine, error);
    return started && waitFor([&engine] { return !engine.getActivity().outputOpening; });
}

void routingPreservesMessagesAndCleansUp()
{
    const auto state = std::make_shared<FakeState>();
    MidiEngine engine(fakeFactory(state));
    expect(startReady(engine), "fake output opens");
    MidiEngine::MapBank maps;
    for (auto& map : maps)
        map = velcal::VelocityMap::identity();
    maps[60].values[64] = 90;
    engine.setMaps(maps);
    const juce::uint8 sysex[]{1, 2, 3};
    const std::vector<juce::MidiMessage> incoming{
        juce::MidiMessage::noteOn(2, 60, static_cast<juce::uint8>(64)),
        juce::MidiMessage::controllerEvent(2, 64, 127),
        juce::MidiMessage::pitchWheel(3, 1234),
        juce::MidiMessage::createSysExMessage(sysex, 3),
        juce::MidiMessage::noteOn(2, 60, static_cast<juce::uint8>(0))};
    for (const auto& message : incoming)
        MidiEngineTestAccess::receive(engine, message);
    expect(waitFor([&state] { return state->snapshot().size() == 5; }),
        "all queued messages are delivered in order");
    engine.stopRouting();
    const auto sent = state->snapshot();
    expect(sent.size() == 10, "stop sends five cleanup messages for the used note channel only");
    if (sent.size() == 10) {
        expect(sent[0].getVelocity() == 90 && sent[0].getChannel() == 2,
            "note-on velocity is mapped without changing its channel");
        for (std::size_t index = 1; index < incoming.size(); ++index)
            expect(sent[index].getRawDataSize() == incoming[index].getRawDataSize()
                && std::equal(sent[index].getRawData(), sent[index].getRawData() + sent[index].getRawDataSize(),
                    incoming[index].getRawData()), "non-note-on messages pass through byte-for-byte");
        expect(sent[5].isController() && sent[5].getControllerNumber() == 64
                && sent[5].getControllerValue() == 0,
            "stop releases sustain after previously sent notes");
        expect(sent[8].isAllNotesOff() && sent[9].isAllSoundOff(),
            "stop silences held and sustained notes");
    }
    expect(state->has(&FakeState::destroyed), "normal stop closes the output before returning");
}

void floodCutoffAlsoCleansUp()
{
    const auto state = std::make_shared<FakeState>();
    MidiEngine engine(fakeFactory(state));
    expect(startReady(engine), "flood-test output opens");
    MidiEngineTestAccess::receive(engine, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(80)));
    expect(waitFor([&state] { return !state->snapshot().empty(); }), "note reaches output before cutoff");
    for (int index = 0; index < 1100; ++index)
        MidiEngineTestAccess::receive(engine, juce::MidiMessage::controllerEvent(1, 1, index % 128));
    expect(engine.getActivity().safetyTripped && !engine.isRouting(), "excess traffic stops routing");
    engine.stopRouting();
    const auto sent = state->snapshot();
    expect(!sent.empty() && sent.back().isAllSoundOff(), "safety cutoff also sends note cleanup");
}

void blockedDriversDoNotOwnTheEngine()
{
    for (const auto stage : {0, 1, 2}) {
        const auto state = std::make_shared<FakeState>();
        state->blockOpen = stage == 0;
        state->blockSend = stage == 1;
        state->blockClose = stage == 2;
        const auto started = std::chrono::steady_clock::now();
        {
            MidiEngine engine(fakeFactory(state));
            juce::String error;
            expect(MidiEngineTestAccess::start(engine, error), "blocked output starts asynchronously");
            expect(std::chrono::steady_clock::now() - started < 500ms,
                "output opening does not block its caller");
            if (stage == 0)
                expect(waitFor([&state] { return state->has(&FakeState::openEntered); }), "open call entered");
            else {
                expect(waitFor([&engine] { return !engine.getActivity().outputOpening; }), "output is ready");
                if (stage == 1) {
                    MidiEngineTestAccess::receive(engine,
                        juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(80)));
                    expect(waitFor([&state] { return state->has(&FakeState::sendEntered); }), "send call entered");
                    for (int index = 0; index < 600; ++index)
                        MidiEngineTestAccess::receive(engine, juce::MidiMessage::controllerEvent(1, 1, 0));
                    expect(engine.getActivity().safetyTripped, "a blocked send triggers bounded queue protection");
                }
            }
            const auto stopping = std::chrono::steady_clock::now();
            engine.stopRouting();
            expect(std::chrono::steady_clock::now() - stopping < 1s,
                "stop returns despite a blocked open, send, or close");
            expect(!MidiEngineTestAccess::start(engine, error) && error.isNotEmpty(),
                "restart cannot accumulate workers while an old driver call is blocked");
        }
        state->release();
        expect(waitFor([&state] { return state->has(&FakeState::destroyed); }),
            "a recovering worker cleans up safely after the engine is destroyed");
        if (stage == 1) {
            const auto messages = state->snapshot();
            expect(!messages.empty() && messages.back().isAllSoundOff(),
                "a delayed send is followed by cleanup when the driver recovers");
        }
    }
}

void outputFailuresAreReported()
{
    MidiEngine engine([](const juce::String&, bool) -> std::unique_ptr<MidiEngine::OutputDevice> { return {}; });
    juce::String error;
    expect(MidiEngineTestAccess::start(engine, error), "output failure is handled asynchronously");
    expect(waitFor([&engine] { return engine.getActivity().outputError.isNotEmpty(); }),
        "failed opening reports an error");
    expect(!engine.isRouting(), "failed output never claims routing is active");
    const auto state = std::make_shared<FakeState>();
    state->blockOpen = true;
    MidiEngine blocked(fakeFactory(state));
    expect(MidiEngineTestAccess::start(blocked, error), "watchdog-test output starts");
    expect(waitFor([&blocked] { return blocked.getActivity().outputError.isNotEmpty(); }, 3s),
        "watchdog reports an unresponsive output and stops routing");
    expect(!blocked.isRouting(), "watchdog disables routing");
    state->release();
    blocked.stopRouting();
}

void recoveredOutputsCanRestartWithoutStaleMessages()
{
    const auto oldState = std::make_shared<FakeState>();
    const auto newState = std::make_shared<FakeState>();
    oldState->blockSend = true;
    const auto opened = std::make_shared<std::atomic<int>>(0);
    MidiEngine engine([oldState, newState, opened](const juce::String& id, bool isVirtual) {
        return fakeFactory(opened->fetch_add(1) == 0 ? oldState : newState)(id, isVirtual);
    });
    expect(startReady(engine), "initial recovery-test output opens");
    MidiEngineTestAccess::receive(engine, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(80)));
    expect(waitFor([&oldState] { return oldState->has(&FakeState::sendEntered); }), "old send is blocked");
    engine.stopRouting();
    oldState->release();
    juce::String error;
    expect(waitFor([&engine, &error] { return MidiEngineTestAccess::start(engine, error); }),
        "routing can restart once the detached session has cleaned up");
    expect(waitFor([&engine] { return !engine.getActivity().outputOpening; }), "replacement output becomes ready");
    MidiEngineTestAccess::receive(engine, juce::MidiMessage::noteOn(1, 65, static_cast<juce::uint8>(70)));
    expect(waitFor([&newState] { return !newState->snapshot().empty(); }), "replacement receives new messages");
    engine.stopRouting();
    const auto oldMessages = oldState->snapshot();
    const auto newMessages = newState->snapshot();
    expect(!oldMessages.empty() && oldMessages.back().isAllSoundOff(), "old session finishes its own cleanup");
    expect(!newMessages.empty() && newMessages.front().getNoteNumber() == 65,
        "a stale note from the old session never reaches the replacement output");
}

void captureExitRestoresControls()
{
    MainComponent component;
    MainComponentTestAccess::startCapture(component);
    expect(!MainComponentTestAccess::controlsEnabled(component), "capture disables editing of input and key group");
    MainComponentTestAccess::changeOutput(component);
    expect(MainComponentTestAccess::controlsEnabled(component), "changing output cancels capture and restores controls");
    MainComponentTestAccess::startCapture(component);
    MainComponentTestAccess::newProfile(component);
    expect(MainComponentTestAccess::controlsEnabled(component), "new profile restores capture controls");
    MainComponentTestAccess::startCapture(component);
    MainComponentTestAccess::clear(component);
    expect(MainComponentTestAccess::controlsEnabled(component), "clearing measurements restores capture controls");
}

void unsavedChangesRequireConfirmation()
{
    MainComponent component;
    MainComponentTestAccess::newProfile(component);
    bool closed = false;
    component.requestClose([&closed] { closed = true; });
    expect(closed, "an unchanged profile closes without a prompt");
    MainComponentTestAccess::editTrim(component);
    expect(MainComponentTestAccess::dirty(component) && MainComponentTestAccess::hasDirtyMarker(component),
        "editing a curve or trim visibly marks the profile unsaved");
    const auto answerPrompt = [](const int answer) {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
        auto* modal = juce::ModalComponentManager::getInstance()->getModalComponent(0);
        expect(modal != nullptr, "discard confirmation is shown");
        if (modal)
            modal->exitModalState(answer);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    };
    closed = false;
    component.requestClose([&closed] { closed = true; });
    expect(!closed, "unsaved changes prevent immediate close");
    answerPrompt(0);
    expect(!closed && MainComponentTestAccess::dirty(component), "Cancel retains unsaved edits");
    component.requestClose([&closed] { closed = true; });
    answerPrompt(1);
    expect(closed, "Discard allows closing");

    const auto path = std::filesystem::current_path() / "velcal-app-load-test.velcal.json";
    velcal::CalibrationProfile replacement;
    replacement.profileName = "Replacement";
    replacement.generated = velcal::calibrate({});
    velcal::saveProfile(replacement, path);
    const auto file = juce::File(juce::String(path.u8string()));
    const auto previousName = MainComponentTestAccess::name(component);
    MainComponentTestAccess::load(component, file);
    answerPrompt(0);
    expect(MainComponentTestAccess::name(component) == previousName
            && MainComponentTestAccess::dirty(component),
        "Cancel preserves the previous profile when switching");
    MainComponentTestAccess::load(component, file);
    answerPrompt(1);
    expect(MainComponentTestAccess::name(component) == "Replacement"
            && !MainComponentTestAccess::dirty(component),
        "confirming a switch loads the requested profile and clears the dirty flag");
    MainComponentTestAccess::editTrim(component);
    MainComponentTestAccess::requestNewProfile(component);
    answerPrompt(0);
    expect(MainComponentTestAccess::name(component) == "Replacement"
            && MainComponentTestAccess::dirty(component),
        "creating a new profile cannot silently discard curve-only edits");
    MainComponentTestAccess::save(component, file);
    expect(!MainComponentTestAccess::dirty(component) && !MainComponentTestAccess::hasDirtyMarker(component),
        "a successful save clears the unsaved marker");
    std::filesystem::remove(path);
}

} // namespace

int main()
{
    const juce::ScopedJuceInitialiser_GUI initialiseJuce;
    juce::LookAndFeel::getDefaultLookAndFeel().setUsingNativeAlertWindows(false);
    routingPreservesMessagesAndCleansUp();
    floodCutoffAlsoCleansUp();
    blockedDriversDoNotOwnTheEngine();
    outputFailuresAreReported();
    recoveredOutputsCanRestartWithoutStaleMessages();
    captureExitRestoresControls();
    unsavedChangesRequireConfirmation();
    if (failures == 0)
        std::cout << "All VelCal app tests passed.\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
