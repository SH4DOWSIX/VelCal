#pragma once

#include "PluginState.hpp"

class VelCalPluginProcessor final : public juce::AudioProcessor {
public:
    VelCalPluginProcessor();
    const juce::String getName() const override { return "VelCal"; }
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override;
    void processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return VELCAL_AU_MIDI_EFFECT != 0; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int size) override;
    PluginState state;

private:
    double hostSampleRate{44100.0};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VelCalPluginProcessor)
};
