#include "ChamberProcessor.h"

#include "ChamberEditor.h"

ChamberProcessor::ChamberProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

void ChamberProcessor::prepareToPlay(double, int) {}

void ChamberProcessor::releaseResources() {}

bool ChamberProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
  // XY needs exactly two channels.
  return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ChamberProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
  juce::ScopedNoDenormals noDenormals;
  buffer.clear();
  midi.clear();
}

juce::AudioProcessorEditor* ChamberProcessor::createEditor() { return new ChamberEditor(*this); }

void ChamberProcessor::getStateInformation(juce::MemoryBlock&) {}

void ChamberProcessor::setStateInformation(const void*, int) {}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChamberProcessor(); }
