#include "MainComponent.hpp"
#include "DataPaths.hpp"

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
    static void accent(MainComponent& component, std::size_t index) { component.chooseAccent(index); }
    static juce::uint32 accent(const MainComponent& component) { return component.green; }
    static void refreshAppearance(MainComponent& component) { component.loadAppearance(); }
    static bool accentControlsMatch(const MainComponent& component)
    {
        return component.keyAdjustmentSlider.findColour(juce::Slider::trackColourId)
                == juce::Colour(component.green)
            && component.saveProfileButton.findColour(juce::TextButton::buttonColourId)
                == juce::Colour(component.green).darker(0.55f);
    }
    static bool themeButtonFits(MainComponent& component)
    {
        component.setSize(860, 820);
        const auto bounds = component.themeButton.getBounds();
        return component.getLocalBounds().contains(bounds)
            && bounds.getY() > component.saveProfileButton.getBottom()
            && !bounds.intersects(component.globalTabButton.getBounds())
            && !bounds.intersects(component.statusLabel.getBounds())
            && !bounds.intersects(component.curveBounds.toNearestInt());
    }
    static void openPalette(MainComponent& component) { component.showThemePalette(); }
    static bool paletteFits(const MainComponent& component)
    { return component.themePopup && component.getLocalBounds().contains(component.themePopup->getBounds()); }
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
    static bool discardPromptOpen(const MainComponent& component) { return component.discardPromptOpen; }
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
    static juce::String displayedProfile(const MainComponent& component) { return component.profileBox.getText(); }
    static void selectCurveTab(MainComponent& component, bool global)
    { component.setActiveTab(global); }
    static void clickSmooth(MainComponent& component)
    {
        component.smoothCurveToggle.setToggleState(
            !component.smoothCurveToggle.getToggleState(), juce::sendNotification);
    }
    static bool smoothDisplayed(const MainComponent& component)
    { return component.smoothCurveToggle.getToggleState(); }
    static bool smoothStored(MainComponent& component) { return component.editableCurveSmooth(); }
    static bool hasEditablePoints(MainComponent& component)
    { return !component.editableCurvePoints().empty(); }
    static void detailedCalibration(MainComponent& component)
    {
        component.profile->generated.noteMaps[component.selectedNote].values[40] = 41;
        component.profile->noteAdjustments[component.selectedNote] = 4;
        component.updateEditingControls();
        component.updateEffectiveMaps();
    }
    static velcal::CalibrationProfile profile(const MainComponent& component) { return *component.profile; }
    static std::uint8_t selectedNote(const MainComponent& component) { return component.selectedNote; }
    static void selectNote(MainComponent& component, std::uint8_t note)
    {
        component.selectedNote = note;
        component.updateEditingControls();
    }
    static void addManualCurve(MainComponent& component)
    { component.profile->noteCurveOverrides[72].points = {{1, 1}, {64, 80}, {127, 127}}; }
    static void resetKey(MainComponent& component) { component.buttonClicked(&component.resetKeyButton); }
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

template <typename Predicate>
bool waitForGui(Predicate predicate, const std::chrono::milliseconds timeout = 2s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
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

void accentPreferencePersistsWithoutChangingProfiles()
{
    const auto file = velcalProfileDirectory().getChildFile(".velcal-appearance.json");
    struct RestorePreference {
        juce::File file;
        bool existed;
        juce::MemoryBlock bytes;
        bool enabled{true};
        ~RestorePreference()
        {
            if (!enabled) return;
            if (existed) file.replaceWithData(bytes.getData(), bytes.getSize());
            else file.deleteFile();
        }
    } restore{file, file.existsAsFile(), {}};
    if (restore.existed && !file.loadFileAsData(restore.bytes)) {
        expect(false, "test can preserve existing appearance preferences");
        restore.enabled = false;
        return;
    }
    file.deleteFile();
    {
        MainComponent component;
        expect(MainComponentTestAccess::accent(component) == velcal_ui::accent,
            "missing appearance preferences default to teal");
        MainComponentTestAccess::newProfile(component);
        const auto before = velcal::serializeProfile(MainComponentTestAccess::profile(component));
        const auto dirty = MainComponentTestAccess::dirty(component);
        for (std::size_t i = 0; i < velcal_ui::accents.size(); ++i) {
            MainComponentTestAccess::accent(component, i);
            expect(MainComponentTestAccess::accent(component) == velcal_ui::accents[i].colour
                    && MainComponentTestAccess::accentControlsMatch(component),
                "each accent updates shared controls");
        }
        expect(velcal::serializeProfile(MainComponentTestAccess::profile(component)) == before
                && MainComponentTestAccess::dirty(component) == dirty,
            "appearance changes do not modify calibration or dirty state");
        expect(MainComponentTestAccess::themeButtonFits(component), "theme button fits the minimum editor size");
        MainComponentTestAccess::openPalette(component);
        expect(MainComponentTestAccess::paletteFits(component), "palette stays inside the editor");
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    {
        MainComponent reopened;
        PluginState plugin;
        MainComponent editor(&plugin);
        expect(MainComponentTestAccess::accent(reopened) == velcal_ui::accents.back().colour
                && MainComponentTestAccess::accent(editor) == velcal_ui::accents.back().colour,
            "standalone and plugin editors recall the saved accent");
        const auto before = plugin.serialize();
        MainComponentTestAccess::accent(editor, 12);
        MainComponentTestAccess::refreshAppearance(reopened);
        expect(MainComponentTestAccess::accent(reopened) == velcal_ui::accents[12].colour
                && plugin.serialize() == before,
            "open editors share appearance without changing DAW state");
        MainComponentTestAccess::accent(editor, velcal_ui::accents.size());
        expect(MainComponentTestAccess::accent(editor) == velcal_ui::accents[12].colour,
            "invalid palette indices are ignored");
        file.replaceWithText("{broken");
        MainComponentTestAccess::refreshAppearance(reopened);
        expect(MainComponentTestAccess::accent(reopened) == velcal_ui::accent,
            "damaged preferences safely fall back to teal");
        file.replaceWithText("{\"accent\":\"Unknown\"}");
        MainComponentTestAccess::refreshAppearance(editor);
        expect(MainComponentTestAccess::accent(editor) == velcal_ui::accent,
            "unknown saved accents safely fall back to teal");
    }
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

void firstSmoothClickAppliesToNewCurve()
{
    MainComponent component;
    for (const bool global : {false, true}) {
        MainComponentTestAccess::newProfile(component);
        MainComponentTestAccess::selectCurveTab(component, global);
        expect(!MainComponentTestAccess::hasEditablePoints(component),
            "smooth regression starts with an automatic curve");
        expect(MainComponentTestAccess::smoothDisplayed(component)
                && MainComponentTestAccess::smoothStored(component),
            "automatic curve starts with smoothing enabled");
        MainComponentTestAccess::clickSmooth(component);
        expect(!MainComponentTestAccess::smoothDisplayed(component)
                && !MainComponentTestAccess::smoothStored(component),
            "first click disables smoothing in the UI and stored curve");
        expect(MainComponentTestAccess::hasEditablePoints(component) == global
                && MainComponentTestAccess::dirty(component),
            "smooth only seeds a manual curve for the global tab and marks the setting unsaved");
        MainComponentTestAccess::clickSmooth(component);
        expect(MainComponentTestAccess::smoothDisplayed(component)
                && MainComponentTestAccess::smoothStored(component),
            "second click enables smoothing in the UI and stored curve");
    }
}

void smoothPreservesAutomaticCalibration()
{
    MainComponent component;
    MainComponentTestAccess::newProfile(component);
    MainComponentTestAccess::selectCurveTab(component, false);
    MainComponentTestAccess::detailedCalibration(component);
    const auto before = MainComponentTestAccess::profile(component);
    const auto selectedNote = MainComponentTestAccess::selectedNote(component);
    const auto expectedMaps = velcal::effectiveMaps(before);
    for (int click = 0; click < 2; ++click) {
        MainComponentTestAccess::clickSmooth(component);
        const auto after = MainComponentTestAccess::profile(component);
        expect(!MainComponentTestAccess::hasEditablePoints(component),
            "smooth toggle keeps per-key calibration automatic");
        expect(after.noteAdjustments == before.noteAdjustments,
            "smooth toggle preserves per-key trim");
        const auto maps = velcal::effectiveMaps(after);
        for (std::size_t note = 0; note < maps.size(); ++note) {
            if (click == 1)
                expect(maps[note].values == expectedMaps[note].values,
                    "off/on restores the same smoothed MIDI outputs");
            expect(after.generated.noteMaps[note].values == before.generated.noteMaps[note].values,
                "smooth toggle leaves generated calibration unchanged");
        }
        if (click == 0)
            expect(maps[selectedNote].apply(40) == 45,
                "Smooth off restores the original calibrated output plus trim");
        else
            expect(maps[selectedNote].apply(40) == 44,
                "Smooth on removes the quantization kink from MIDI correction");
        const auto loaded = velcal::deserializeProfile(velcal::serializeProfile(after));
        expect(loaded.noteCurveOverrides[selectedNote].points.empty()
                && loaded.noteCurveOverrides[selectedNote].smooth == after.noteCurveOverrides[selectedNote].smooth,
            "automatic smoothing preference survives profile serialization without a manual curve");
    }
}

void smoothSwitchAppliesToWholeKeyboard()
{
    MainComponent component;
    MainComponentTestAccess::newProfile(component);
    MainComponentTestAccess::addManualCurve(component);
    for (int click = 0; click < 2; ++click) {
        MainComponentTestAccess::clickSmooth(component);
        const bool expected = click == 1;
        const auto profile = MainComponentTestAccess::profile(component);
        for (const auto& curve : profile.noteCurveOverrides)
            expect(curve.smooth == expected, "one Smooth click updates all automatic and manual key curves");
        expect(profile.noteCurveOverrides[72].points.size() == 3,
            "whole-keyboard Smooth preserves manual control points");
        for (const auto note : {21, 60, 72, 108}) {
            MainComponentTestAccess::selectNote(component, static_cast<std::uint8_t>(note));
            expect(MainComponentTestAccess::smoothDisplayed(component) == expected,
                "Smooth switch does not change when selecting another key");
        }
        const auto recalled = velcal::deserializeProfile(velcal::serializeProfile(profile));
        for (const auto& curve : recalled.noteCurveOverrides)
            expect(curve.smooth == expected, "whole-keyboard smoothing survives profile recall");
    }
    MainComponentTestAccess::clickSmooth(component);
    MainComponentTestAccess::resetKey(component);
    expect(!MainComponentTestAccess::smoothDisplayed(component)
            && !MainComponentTestAccess::smoothStored(component),
        "Reset key preserves whole-keyboard Smooth off");
    MainComponentTestAccess::newProfile(component);
    expect(MainComponentTestAccess::smoothDisplayed(component), "new profiles default Smooth to on");
    MainComponentTestAccess::selectCurveTab(component, true);
    MainComponentTestAccess::clickSmooth(component);
    const auto profile = MainComponentTestAccess::profile(component);
    for (const auto& curve : profile.noteCurveOverrides)
        expect(curve.smooth, "global curve smoothing remains independent of key-curve smoothing");
}

void unsavedChangesRequireConfirmation()
{
    MainComponent component;
    MainComponentTestAccess::newProfile(component);
    bool closed = false;
    expect(MainComponentTestAccess::name(component) == "New calibration",
        "new standalone profiles have a neutral name rather than a MIDI port name");
    component.requestClose([&closed] { closed = true; });
    expect(closed, "an unchanged profile closes without a prompt");
    MainComponentTestAccess::editTrim(component);
    expect(MainComponentTestAccess::dirty(component) && MainComponentTestAccess::hasDirtyMarker(component),
        "editing a curve or trim visibly marks the profile unsaved");
    const auto answerPrompt = [&component](const int answer) {
        juce::AlertWindow* alert = nullptr;
        const auto shown = waitForGui([&alert] {
            alert = dynamic_cast<juce::AlertWindow*>(
                juce::ModalComponentManager::getInstance()->getModalComponent(0));
            return alert != nullptr && alert->getName() == "Discard unsaved changes?";
        });
        expect(shown, "discard confirmation is shown before the timeout");
        if (!shown)
            return;
        alert->exitModalState(answer);
        expect(waitForGui([&component] {
            return !MainComponentTestAccess::discardPromptOpen(component);
        }), "discard confirmation callback completes before the timeout");
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
    expect(MainComponentTestAccess::displayedProfile(component) == "velcal-app-load-test",
        "standalone displays the loaded filename rather than its internal profile name");
    MainComponentTestAccess::editTrim(component);
    MainComponentTestAccess::requestNewProfile(component);
    answerPrompt(0);
    expect(MainComponentTestAccess::name(component) == "Replacement"
            && MainComponentTestAccess::dirty(component),
        "creating a new profile cannot silently discard curve-only edits");
    MainComponentTestAccess::save(component, file);
    expect(!MainComponentTestAccess::dirty(component) && !MainComponentTestAccess::hasDirtyMarker(component),
        "a successful save clears the unsaved marker");
    expect(MainComponentTestAccess::displayedProfile(component) == "velcal-app-load-test"
            && MainComponentTestAccess::name(component) == "Replacement",
        "saving keeps filename display without rewriting the internal profile metadata");
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
    accentPreferencePersistsWithoutChangingProfiles();
    firstSmoothClickAppliesToNewCurve();
    smoothPreservesAutomaticCalibration();
    smoothSwitchAppliesToWholeKeyboard();
    unsavedChangesRequireConfirmation();
    if (failures == 0)
        std::cout << "All VelCal app tests passed.\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
