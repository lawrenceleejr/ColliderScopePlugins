#include "MuxProcessor.h"

#include "MuxEditor.h"

MuxProcessor::MuxProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("A", juce::AudioChannelSet::stereo(), true)
                         .withInput("B (sidechain)", juce::AudioChannelSet::stereo(), false)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

void MuxProcessor::prepareToPlay(double, int) {}

void MuxProcessor::releaseResources() {}

bool MuxProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
  // XY needs exactly two channels on A and on the output; B is stereo or off.
  const auto stereo = juce::AudioChannelSet::stereo();
  if (layouts.getMainInputChannelSet() != stereo || layouts.getMainOutputChannelSet() != stereo)
    return false;
  const auto sidechain = layouts.getChannelSet(true, 1);
  return sidechain.isDisabled() || sidechain == stereo;
}

void MuxProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
  juce::ScopedNoDenormals noDenormals;
  // A occupies channels 0-1 in and out: passing it through means leaving it alone.
  // Silence anything beyond the main bus that the host handed us as output.
  for (int ch = getMainBusNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
    buffer.clear(ch, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* MuxProcessor::createEditor() { return new MuxEditor(*this); }

void MuxProcessor::getStateInformation(juce::MemoryBlock&) {}

void MuxProcessor::setStateInformation(const void*, int) {}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MuxProcessor(); }
