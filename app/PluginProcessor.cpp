#include "PluginProcessor.hpp"
#include "MainComponent.hpp"

#include <cmath>

namespace {
class VelCalPluginEditor final : public juce::AudioProcessorEditor {
public:
    explicit VelCalPluginEditor(VelCalPluginProcessor& owner)
        : AudioProcessorEditor(owner), component(&owner.state)
    {
        addAndMakeVisible(component);
        setResizable(true, true);
        setResizeLimits(860, 820, 1800, 1200);
        setSize(1180, 820);
    }
    void resized() override { component.setBounds(getLocalBounds()); }
private:
    MainComponent component;
};
}

VelCalPluginProcessor::VelCalPluginProcessor()
   #if VELCAL_AU_MIDI_EFFECT
    : AudioProcessor(BusesProperties{})
   #else
    : AudioProcessor(BusesProperties().withOutput("Silent output", juce::AudioChannelSet::stereo(), true))
   #endif
{
    state.onChange = [this] {
        updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
    };
}

void VelCalPluginProcessor::prepareToPlay(double sampleRate, int)
{
    hostSampleRate = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 44100.0;
}

bool VelCalPluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
   #if VELCAL_AU_MIDI_EFFECT
    return layouts.inputBuses.isEmpty() && layouts.outputBuses.isEmpty();
   #else
    return layouts.inputBuses.isEmpty() && layouts.outputBuses.size() == 1
        && (layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
            || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono());
   #endif
}

void VelCalPluginProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    audio.clear();
    state.midi.processHostMidi(midi, hostSampleRate, audio.getNumSamples());
}

void VelCalPluginProcessor::processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer&)
{
    audio.clear();
}

juce::AudioProcessorEditor* VelCalPluginProcessor::createEditor()
{
    return new VelCalPluginEditor(*this);
}

void VelCalPluginProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    const auto json = state.serialize();
    destination.replaceAll(json.data(), json.size());
}

void VelCalPluginProcessor::setStateInformation(const void* data, int size)
{
    if (data != nullptr && size > 0)
        state.restore(std::string(static_cast<const char*>(data), static_cast<std::size_t>(size)));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VelCalPluginProcessor();
}
