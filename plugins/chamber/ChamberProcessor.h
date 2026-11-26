#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

#include "ScopeDisplay.h"
#include "cscore/chamber_engine.h"

// CS Chamber: monophonic MIDI instrument, stereo XY out (left = X, right = Y).
// Each note births a cloud-chamber track, drawn once per period at the
// note's pitch.
class ChamberProcessor final : public juce::AudioProcessor, private juce::Thread {
public:
  ChamberProcessor();
  ~ChamberProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
  using juce::AudioProcessor::processBlock;

  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override { return true; }

  const juce::String getName() const override { return JucePlugin_Name; }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override { return 2.0; } // the longest Release

  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String&) override {}

  void getStateInformation(juce::MemoryBlock&) override;
  void setStateInformation(const void*, int) override;

  juce::AudioProcessorValueTreeState& state() noexcept { return state_; }
  cs::ScopeFeed& scopeFeed() noexcept { return scopeFeed_; }

private:
  static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
  cs::ChamberParams readParams() const noexcept;
  void handleMidi(const juce::MidiMessage&) noexcept;
  void run() override; // the builder thread: makes picture tables off the audio thread

  juce::AudioProcessorValueTreeState state_;
  cs::ChamberEngine engine_;
  cs::ScopeFeed scopeFeed_;

  std::atomic<float>*type_, *seedMode_, *diffusion_, *drift_, *condense_, *wallMode_, *wallRoot_, *wallShare_, *size_,
      *attack_, *release_, *glide_, *bendRange_, *gain_, *scopeDecay_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChamberProcessor)
};
