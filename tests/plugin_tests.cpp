#include "PluginProcessor.hpp"
#include "MainComponent.hpp"
#include "DataPaths.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>

struct MainComponentTestAccess {
    static void trim(MainComponent& component, int value)
    {
        component.keyAdjustmentSlider.setValue(value, juce::dontSendNotification);
        component.sliderValueChanged(&component.keyAdjustmentSlider);
    }
    static bool deviceControlsHidden(const MainComponent& component)
    {
        return !component.midiInputBox.isVisible() && !component.midiOutputBox.isVisible()
            && !component.routingToggle.isVisible();
    }
    static void sync(MainComponent& component) { component.syncPluginState(); }
    static void clickCurveTab(MainComponent& component, bool global)
    { component.buttonClicked(global ? &component.globalTabButton : &component.perKeyTabButton); }
    static bool globalTab(const MainComponent& component) { return component.showingGlobalCurve; }
    static int adjustment(const MainComponent& component) { return component.profile->noteAdjustments[60]; }
    static void begin(MainComponent& component) { component.beginSectionCapture(); }
    static void finish(MainComponent& component) { component.finishSectionCapture(); }
    static std::size_t presses(const MainComponent& component) { return component.profile->presses.size(); }
    static void load(MainComponent& component, const juce::File& file) { component.loadProfile(file); }
    static juce::String displayedName(const MainComponent& component) { return component.profileBox.getText(); }
    static int selectedProfile(const MainComponent& component) { return component.profileBox.getSelectedId(); }
    static juce::String selectedProfileText(const MainComponent& component)
    { return component.profileBox.getItemText(component.profileBox.indexOfItemId(component.profileBox.getSelectedId())); }
    static bool noSavedProfiles(const MainComponent& component)
    {
        for (int index = 0; index < component.profileBox.getNumItems(); ++index)
            if (component.profileBox.getItemText(index) == "No saved profiles"
                && !component.profileBox.isItemEnabled(component.profileBox.getItemId(index)))
                return true;
        return false;
    }
    static void selectCurrentProfile(MainComponent& component) { component.comboBoxChanged(&component.profileBox); }
    static void newProfileWithHiddenDevice(MainComponent& component)
    {
        component.midiInputs.add({"LM - Keysight Input", "test-virtual-input"});
        component.midiInputBox.addItem("LM - Keysight Input", 1);
        component.midiInputBox.setSelectedId(1, juce::dontSendNotification);
        component.replaceWithNewProfile();
    }
};

namespace {
int failures{};
void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void processingAndRecall()
{
    VelCalPluginProcessor processor;
    processor.prepareToPlay(48000.0, 480);
    {
        MainComponent editor(&processor.state);
        expect(MainComponentTestAccess::deviceControlsHidden(editor), "DAW owns device selection");
        MainComponentTestAccess::trim(editor, 7);
    }
    juce::MemoryBlock saved;
    processor.getStateInformation(saved);
    VelCalPluginProcessor restored;
    restored.prepareToPlay(48000.0, 480);
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    expect(restored.state.snapshot().profile->noteAdjustments[60] == 7, "host state embeds unsaved edits");
    juce::AudioBuffer<float> audio(2, 480);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(3, 60, static_cast<juce::uint8>(80)), 17);
    midi.addEvent(juce::MidiMessage::noteOff(3, 60, static_cast<juce::uint8>(32)), 23);
    midi.addEvent(juce::MidiMessage::controllerEvent(3, 64, 127), 24);
    midi.addEvent(juce::MidiMessage::pitchWheel(3, 1234), 25);
    midi.addEvent(juce::MidiMessage::noteOn(3, 60, static_cast<juce::uint8>(0)), 26);
    const juce::uint8 sysex[]{1, 2, 3, 4};
    midi.addEvent(juce::MidiMessage::createSysExMessage(sysex, 4), 27);
    auto expected = midi;
    restored.processBlock(audio, midi);
    int index{};
    auto original = expected.begin();
    for (const auto event : midi) {
        const auto before = *original++;
        expect(event.samplePosition == before.samplePosition, "sample offsets are preserved");
        if (index++ == 0) {
            expect(event.getMessage().getVelocity() == 87 && event.getMessage().getChannel() == 3,
                "correction works without an editor and preserves channel");
        } else {
            expect(event.numBytes == before.numBytes
                && std::memcmp(event.data, before.data, static_cast<std::size_t>(event.numBytes)) == 0,
                "other MIDI messages pass through unchanged");
        }
    }
    expect(index == 6, "no MIDI events lost");
    VelCalPluginProcessor independent;
    expect(independent.state.snapshot().profile->noteAdjustments[60] == 0, "instances are independent");
    restored.setStateInformation("invalid", 7);
    expect(restored.state.snapshot().profile->noteAdjustments[60] == 7, "invalid state preserves current profile");
    {
        MainComponent editor(&restored.state);
        expect(MainComponentTestAccess::adjustment(editor) == 7, "editor reopening restores edits");
        auto changed = processor.state.snapshot();
        changed.profile->noteAdjustments[60] = 12;
        expect(processor.state.publish(changed, changed.revision), "updated profile publishes");
        processor.getStateInformation(saved);
        restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        MainComponentTestAccess::sync(editor);
        expect(MainComponentTestAccess::adjustment(editor) == 12, "open editor follows host recall");
    }
}

void tabsAndSettingsFollowHostState()
{
    VelCalPluginProcessor processor;
    auto settings = processor.state.snapshot();
    settings.profile->noteAdjustments[60] = 7;
    settings.profile->noteCurveOverrides[60].points = {{1, 1}, {64, 80}, {127, 127}};
    for (auto& curve : settings.profile->noteCurveOverrides)
        curve.smooth = false;
    settings.profile->globalCurve.curvature = 0.4;
    settings.profile->globalCurve.minimumOutput = 9;
    settings.profile->globalCurve.maximumOutput = 115;
    settings.profile->globalCurve.points = {{1, 9}, {64, 72}, {127, 115}};
    settings.profile->globalCurve.smooth = false;
    settings.profile->userGlobalPresets.push_back(settings.profile->globalCurve);
    settings.keyGroup = 2;
    settings.dirty = true;
    expect(processor.state.publish(settings, settings.revision), "host-recall settings publish");
    const auto profile = velcal::serializeProfile(*processor.state.snapshot().profile);
    MainComponent editor(&processor.state);
    for (const bool global : {true, false}) {
        int notifications{};
        processor.state.onChange = [&] { ++notifications; };
        MainComponentTestAccess::clickCurveTab(editor, global);
        processor.state.onChange = nullptr;
        expect(notifications == 1, "tab click notifies the host of changed plugin state");
        expect(velcal::serializeProfile(*processor.state.snapshot().profile) == profile
                && processor.state.snapshot().dirty,
            "tab clicks preserve profile settings and dirty state");
        juce::MemoryBlock saved;
        processor.getStateInformation(saved);
        MainComponentTestAccess::clickCurveTab(editor, !global);
        processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        MainComponentTestAccess::sync(editor);
        expect(MainComponentTestAccess::globalTab(editor) == global,
            "saved host preset overrides a later tab change in an open editor");
        VelCalPluginProcessor restored;
        restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        expect(restored.state.snapshot().showingGlobalCurve == global
                && restored.state.snapshot().keyGroup == 2
                && restored.state.snapshot().dirty
                && velcal::serializeProfile(*restored.state.snapshot().profile) == profile,
            "host recall restores tab, key group, trims, curves, Smooth and user presets without an editor");
        {
            MainComponent reopened(&restored.state);
            expect(MainComponentTestAccess::globalTab(reopened) == global,
                "new editor displays the tab restored before it opened");
        }
        {
            MainComponent reopened(&restored.state);
            expect(MainComponentTestAccess::globalTab(reopened) == global,
                "closing and reopening an editor preserves the instance tab");
        }
        VelCalPluginProcessor independent;
        expect(!independent.state.snapshot().showingGlobalCurve,
            "fresh plugin instances start on per-key independently");
    }
    auto legacy = nlohmann::json::parse(processor.state.serialize());
    legacy.erase("curveTab");
    expect(processor.state.restore(legacy.dump()), "older host state without a tab remains supported");
    MainComponentTestAccess::sync(editor);
    expect(!MainComponentTestAccess::globalTab(editor), "older host state defaults to per-key");
    for (const auto& invalid : {nlohmann::json(42), nlohmann::json("unknown"), nlohmann::json(nullptr)}) {
        MainComponentTestAccess::clickCurveTab(editor, true);
        legacy["curveTab"] = invalid;
        expect(processor.state.restore(legacy.dump()), "invalid tab does not prevent restoring profile state");
        MainComponentTestAccess::sync(editor);
        expect(!MainComponentTestAccess::globalTab(editor), "invalid host tab defaults to per-key");
    }
}

void firstRunAndExternalProfiles()
{
    const auto previousRoot = juce::SystemStats::getEnvironmentVariable("VELCAL_DATA_DIR", {});
    if (!juce::File::isAbsolutePath(previousRoot)) {
        expect(false, "profile tests require an isolated absolute data root");
        return;
    }
    const auto root = juce::File(previousRoot).getChildFile("profile-test-" + juce::Uuid().toString());
    const auto setDataRoot = [](const juce::String& value) {
       #if JUCE_WINDOWS
        return _putenv_s("VELCAL_DATA_DIR", value.toRawUTF8());
       #else
        return setenv("VELCAL_DATA_DIR", value.toRawUTF8(), 1);
       #endif
    };
    expect(setDataRoot(root.getFullPathName()) == 0, "test data root can be selected");
    const auto profiles = root.getChildFile("profiles");
    expect(!profiles.exists(), "first-run test starts without a profile folder");
    {
        MainComponent standalone;
        expect(profiles.isDirectory(), "standalone creates its profile folder before saving");
        expect(MainComponentTestAccess::displayedName(standalone) == "No saved profiles"
                && MainComponentTestAccess::noSavedProfiles(standalone),
            "standalone with no library or active profile shows No saved profiles");
    }
    expect(profiles.deleteFile(), "empty standalone profile folder can be removed");
    PluginState state;
    {
        MainComponent editor(&state);
        expect(profiles.isDirectory(), "plugin creates its profile folder without standalone or saving");
        expect(MainComponentTestAccess::displayedName(editor) == "New calibration (unsaved)"
                && MainComponentTestAccess::selectedProfileText(editor) == "New calibration (unsaved)"
                && MainComponentTestAccess::noSavedProfiles(editor),
            "empty plugin library has a selectable unsaved profile and a clear no-saved-profiles entry");
        MainComponentTestAccess::newProfileWithHiddenDevice(editor);
        const auto fresh = state.snapshot();
        expect(fresh.profile->profileName == "New calibration"
                && fresh.profile->inputDevice.name == "DAW MIDI"
                && fresh.profile->inputDevice.endpointId.empty(),
            "new plugin profile never inherits a hidden physical MIDI device");

        velcal::CalibrationProfile imported;
        imported.profileName = "Imported calibration";
        imported.noteAdjustments[60] = 9;
        imported.generated = velcal::calibrate({});
        const auto external = root.getChildFile("old-file-name.velcal.json");
        velcal::saveProfile(imported, std::filesystem::u8path(external.getFullPathName().toStdString()));
        bool pathConsistent = true;
        state.onChange = [&] {
            const auto snapshot = state.snapshot();
            if (snapshot.profile->profileName == imported.profileName)
                pathConsistent = pathConsistent && snapshot.profileFile == external;
        };
        MainComponentTestAccess::load(editor, external);
        expect(MainComponentTestAccess::displayedName(editor) == "old-file-name"
                && MainComponentTestAccess::selectedProfile(editor) != 0,
            "external profile displays its filename rather than internal profile name");
        expect(MainComponentTestAccess::adjustment(editor) == 9 && pathConsistent,
            "external profile settings and path publish together");
        expect(!profiles.getChildFile(external.getFileName()).exists(),
            "opening an external profile does not require moving or copying it");
        state.onChange = nullptr;
    }
    const auto savedState = state.serialize();
    const auto external = root.getChildFile("old-file-name.velcal.json");
    expect(external.deleteFile(), "external profile can be removed before embedded DAW-state recall");
    PluginState recalled;
    expect(recalled.restore(savedState), "external profile survives DAW project recall without its file");
    {
        MainComponent editor(&recalled);
        expect(MainComponentTestAccess::displayedName(editor) == "old-file-name"
                && MainComponentTestAccess::selectedProfileText(editor) == "old-file-name"
                && MainComponentTestAccess::noSavedProfiles(editor)
                && MainComponentTestAccess::adjustment(editor) == 9,
            "reopened plugin keeps the missing file's name, selected entry and embedded settings");
        MainComponentTestAccess::selectCurrentProfile(editor);
        expect(MainComponentTestAccess::adjustment(editor) == 9,
            "selecting the active missing-file profile does not reload or discard embedded settings");
        velcal::CalibrationProfile library;
        library.profileName = "Library calibration";
        library.generated = velcal::calibrate({});
        const auto file = profiles.getChildFile("different-file-name.velcal.json");
        velcal::saveProfile(library, std::filesystem::u8path(file.getFullPathName().toStdString()));
        MainComponentTestAccess::load(editor, file);
        expect(MainComponentTestAccess::displayedName(editor) == "different-file-name",
            "library and external profiles both display filenames");
    }
    expect(setDataRoot(previousRoot) == 0, "original test data root is restored");
    expect(root.deleteRecursively(), "isolated profile test files are removed");
}

void calibrationUsesRawNotesAndSampleClock()
{
    PluginState state;
    juce::String error;
    expect(state.midi.startCapture({}, error), "host capture requires no device");
    MidiEngine::MapBank maps;
    for (auto& map : maps)
        map = velcal::VelocityMap::identity();
    for (std::size_t velocity = 1; velocity < 128; ++velocity)
        maps[60].values[velocity] = 100;
    state.midi.setMaps(maps);
    {
        MainComponent editor(&state);
        expect(state.midi.isCapturing(), "opening an editor preserves active capture");
    }
    for (int press = 0; press < 3; ++press) {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(40)), 48);
        midi.addEvent(juce::MidiMessage::noteOn(1, 62, static_cast<juce::uint8>(45)), 96);
        midi.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(50)), 144);
        state.midi.processHostMidi(midi, 48000.0, 24000);
    }
    const auto notes = state.midi.finishCapture();
    expect(notes.size() == 9 && notes.front().velocity == 40, "capture records raw input before correction");
    expect(notes.front().timestampUs == 1000 && notes[3].timestampUs == 501000,
        "capture uses sample timing across blocks");
    expect(velcal::analyzeSectionCapture(notes, velcal::KeyGroup::whiteKeys, 1).rangeInferred,
        "host notes feed the existing section calibration algorithm");
    expect(state.midi.startCapture({}, error), "another capture starts");
    expect(state.midi.finishCapture().empty(), "new capture cannot include old section notes");
}

void calibrationWorkflowAndOverflow()
{
    PluginState state;
    MainComponent editor(&state);
    MainComponentTestAccess::begin(editor);
    expect(state.midi.isCapturing(), "shared UI starts capture using DAW input");
    for (int press = 0; press < 3; ++press) {
        juce::MidiBuffer midi;
        for (const auto note : {60, 62, 64})
            midi.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(50)), 0);
        state.midi.processHostMidi(midi, 48000.0, 24000);
    }
    MainComponentTestAccess::finish(editor);
    expect(MainComponentTestAccess::presses(editor) == 3, "shared UI completes a host calibration section");
    PluginState recalled;
    expect(recalled.restore(state.serialize()) && recalled.snapshot().profile->presses.size() == 3,
        "completed raw measurements survive project recall");
    MainComponentTestAccess::begin(editor);
    juce::MidiBuffer flood;
    for (int index = 0; index < 32768; ++index)
        flood.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(50)), index);
    state.midi.processHostMidi(flood, 48000.0, 32768);
    expect(!state.midi.isCapturing() && state.midi.getActivity().outputError.isNotEmpty(),
        "capture overflow stops recording and reports an error");
    expect(flood.getNumEvents() == 32768, "capture overflow does not interrupt host MIDI output");
    MainComponentTestAccess::finish(editor);
    expect(MainComponentTestAccess::presses(editor) == 3, "overflow never adds a truncated section");
    MainComponentTestAccess::begin(editor);
    expect(state.midi.isCapturing() && state.midi.getActivity().outputError.isEmpty(),
        "capture can restart after overflow");
    state.midi.cancelCapture();
}

void instrumentPresentationAndSilentOutput()
{
    VelCalPluginProcessor processor;
   #if VELCAL_AU_MIDI_EFFECT
    expect(JucePlugin_IsSynth == 0 && JucePlugin_IsMidiEffect == 1 && processor.isMidiEffect(),
        "Logic target declares an AU MIDI effect");
    expect(processor.acceptsMidi() && processor.producesMidi(), "AU exposes MIDI input and output");
    expect(processor.getBusCount(true) == 0 && processor.getBusCount(false) == 0,
        "AU MIDI effect has no audio buses");
    expect(processor.isBusesLayoutSupported(processor.getBusesLayout()), "AU MIDI-only layout is supported");
   #else
    expect(JucePlugin_IsSynth == 1 && JucePlugin_IsMidiEffect == 0 && !processor.isMidiEffect(),
        "plugin declares an instrument rather than a MIDI-only effect");
    expect(processor.acceptsMidi() && processor.producesMidi(), "instrument exposes MIDI input and output");
    expect(processor.getBusCount(true) == 0 && processor.getBusCount(false) == 1
            && processor.getTotalNumOutputChannels() == 2,
        "default instrument layout has a stereo output and no audio inputs");
    auto layout = processor.getBusesLayout();
    expect(processor.isBusesLayoutSupported(layout), "default stereo layout is supported");
    layout.outputBuses.set(0, juce::AudioChannelSet::mono());
    expect(processor.isBusesLayoutSupported(layout), "mono output is supported");
    layout.outputBuses.set(0, juce::AudioChannelSet::create5point1());
    expect(!processor.isBusesLayoutSupported(layout), "unsupported surround layout is rejected");
    layout.outputBuses.set(0, juce::AudioChannelSet::stereo());
    layout.inputBuses.add(juce::AudioChannelSet::stereo());
    expect(!processor.isBusesLayoutSupported(layout), "audio input layout is rejected");
   #endif

    processor.prepareToPlay(48000.0, 480);
    auto state = processor.state.snapshot();
    state.profile->noteAdjustments[60] = 7;
    expect(processor.state.publish(state, state.revision), "test velocity map publishes");
    juce::AudioBuffer<float> audio(2, 480);
    const auto fillAudio = [&] {
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            for (int sample = 0; sample < audio.getNumSamples(); ++sample)
                audio.setSample(channel, sample, 0.75f);
    };
    fillAudio();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(80)), 17);
    processor.processBlock(audio, midi);
    expect(audio.getMagnitude(0, audio.getNumSamples()) == 0.0f, "all output channels are silent");
    expect((*midi.begin()).getMessage().getVelocity() == 87, "silent instrument outputs corrected MIDI");
    fillAudio();
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(80)), 17);
    processor.processBlockBypassed(audio, midi);
    expect(audio.getMagnitude(0, audio.getNumSamples()) == 0.0f, "bypassed instrument output stays silent");
    expect((*midi.begin()).getMessage().getVelocity() == 80 && (*midi.begin()).samplePosition == 17,
        "bypass passes original MIDI through with its timing unchanged");
   #if VELCAL_AU_MIDI_EFFECT
    juce::AudioBuffer<float> noAudio(0, 480);
    processor.processBlock(noAudio, midi);
    expect((*midi.begin()).getMessage().getVelocity() == 87,
        "AU corrects MIDI with a zero-channel audio buffer");
   #endif
}
}

int main()
{
    const juce::ScopedJuceInitialiser_GUI juce;
    const auto dataRoot = juce::SystemStats::getEnvironmentVariable("VELCAL_DATA_DIR", {});
    if (juce::File::isAbsolutePath(dataRoot))
        expect(velcalProfileDirectory() == juce::File(dataRoot).getChildFile("profiles"),
            "installed data override uses the isolated profile directory");
    firstRunAndExternalProfiles();
    processingAndRecall();
    tabsAndSettingsFollowHostState();
    calibrationUsesRawNotesAndSampleClock();
    calibrationWorkflowAndOverflow();
    instrumentPresentationAndSilentOutput();
    if (failures == 0)
        std::cout << "All VelCal plugin tests passed.\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
