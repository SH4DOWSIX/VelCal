#include "PluginProcessor.hpp"
#include "MainComponent.hpp"
#include "DataPaths.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>

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
    static int adjustment(const MainComponent& component) { return component.profile->noteAdjustments[60]; }
    static void begin(MainComponent& component) { component.beginSectionCapture(); }
    static void finish(MainComponent& component) { component.finishSectionCapture(); }
    static std::size_t presses(const MainComponent& component) { return component.profile->presses.size(); }
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
    processingAndRecall();
    calibrationUsesRawNotesAndSampleClock();
    calibrationWorkflowAndOverflow();
    instrumentPresentationAndSilentOutput();
    if (failures == 0)
        std::cout << "All VelCal plugin tests passed.\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
